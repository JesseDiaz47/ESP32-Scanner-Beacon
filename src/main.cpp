// esp32-scanner-beacon
// One board, four passive roles:
//   1. WiFi AP-mode scanner — phone connects to "jesse-scanner", sees live SSIDs
//   2. Channel analyzer — visualizes 2.4 GHz AP density from those passive scans
//   3. BLE scanner + iBeacon — scans on demand, advertises between scans
//   4. RSSI heatmap — tag a spot, capture the strongest APs, walk around, export
//
// LEDs:
//   GPIO 2  = onboard blue LED — flashes ~120ms each time a WiFi scan completes
//   GPIO 4  = any free GPIO — toggles while the BLE beacon is alive, solid during scan
//
// Power: NULLLAB 1200mAh LiPo module -> VIN. ~2-3 hours with both radios active.
// Board: ESP32-WROOM-32 DevKit V1, 30-pin.
//
// Reflashing: USB is only needed once. After that, `pio run -e ota -t upload`
// pushes firmware over WiFi (see platformio.ini). OTA requires two app
// partitions, which is why this uses min_spiffs.csv and not huge_app.csv.
//
// Radio reality: this ESP32 has one shared 2.4 GHz radio. WiFi channel sweeps,
// BLE discovery, and BLE advertising are coordinated rather than run blindly
// over one another. BLE discovery is passive and only starts on user request.

#include <ArduinoOTA.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUUID.h>
#include <BLEUtils.h>
#include <WebServer.h>
#include <WiFi.h>

#include "heatmap_csv.h"
#include "radio_coordinator.h"
#include "ui_page.h"

// secrets.h is gitignored and holds OTA_PASSWORD (+ optional STA credentials).
// Build still succeeds without it, but refuses to expose an unauthenticated
// OTA endpoint on an open access point.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

// Compile-time guards for OTA_PASSWORD, used in setup_ota(). C++11 constexpr
// permits a single return statement, hence the recursion.
constexpr size_t ota_password_length(const char* s) {
  return *s ? 1 + ota_password_length(s + 1) : 0;
}
constexpr bool ota_password_equals(const char* a, const char* b) {
  return *a == *b && (*a == '\0' || ota_password_equals(a + 1, b + 1));
}

// ─── Pin map ────────────────────────────────────────────────────────────────
constexpr uint8_t PIN_WIFI_LED = 2;   // onboard blue LED — scan heartbeat
constexpr uint8_t PIN_BLE_LED  = 4;   // any free GPIO — BLE status

// ─── WiFi AP ────────────────────────────────────────────────────────────────
// IPAddress is not a literal type on this core, so these are file-scope const.
constexpr char AP_SSID[] = "jesse-scanner";
constexpr char AP_PASS[] = "";              // open network for field use
const IPAddress AP_IP  (192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);

WebServer server(80);

// ─── WiFi scan state ────────────────────────────────────────────────────────
// bssid is copied here while the scan result is still alive. WiFi.BSSIDstr(i)
// is only valid until WiFi.scanDelete(), and the heatmap snapshot runs well
// after that — reading it there returned nothing.
struct ScanEntry {
  String  ssid;
  String  bssid;
  int32_t rssi;
  uint8_t channel;
  bool    encrypted;
};

// A prior hardware run saw 31 networks in one sweep; 64 leaves cheap headroom.
constexpr size_t MAX_ENTRIES = 64;
ScanEntry g_entries[MAX_ENTRIES];
size_t        g_entryCount      = 0;
bool          g_scanInProgress  = false;
unsigned long g_lastScanStart   = 0;
unsigned long g_wifiLedOnAt     = 0;
volatile bool g_otaActive       = false;
constexpr unsigned long SCAN_INTERVAL_MS = 10000;
constexpr unsigned long LED_FLASH_MS     = 120;

// ─── Coordinated BLE state ──────────────────────────────────────────────────
RadioCoordinator g_radio;

struct BleEntry {
  String  name;
  String  address;
  int32_t rssi;
  int32_t company;  // Bluetooth SIG company identifier; -1 when absent
};

// This classic ESP32 has no PSRAM. Keep scan results deliberately bounded.
constexpr size_t MAX_BLE_ENTRIES = 32;
constexpr uint32_t BLE_SCAN_SECONDS = 5;
BleEntry g_bleEntries[MAX_BLE_ENTRIES];
size_t g_bleEntryCount = 0;
BLEAdvertising* g_bleAdvertising = nullptr;
BLEScan* g_bleScan = nullptr;
volatile bool g_bleScanCallbackPending = false;

// ─── RSSI heatmap state ─────────────────────────────────────────────────────
// A snapshot is a tagged bundle of up to 6 APs (SSID, BSSID, channel, RSSI)
// sampled at one spot. The storage types and the CSV encoder live in
// heatmap_csv.h so the host tests exercise the shipped encoder, not a copy.
using HeatmapRow    = heatmap::Row<String>;
using HeatmapSample = heatmap::Sample<String>;
constexpr size_t MAX_HEATMAP_SAMPLES         = heatmap::MAX_SAMPLES;
constexpr size_t MAX_HEATMAP_ROWS_PER_SAMPLE = heatmap::MAX_ROWS_PER_SAMPLE;

HeatmapSample g_heatmapSamples[MAX_HEATMAP_SAMPLES];
size_t g_heatmapCount = 0;
volatile bool g_heatmapSnapshotPending = false;
String g_heatmapPendingTag;
unsigned long g_heatmapPendingAt = 0;
unsigned long g_heatmapSnapshotAt = 0;
constexpr unsigned long HEATMAP_SNAPSHOT_COOLDOWN_MS = 2000;

// A tag press must be answered by a sweep that finished after the press. This
// counter is the evidence: schedule_heatmap_snapshot() records the generation
// it needs and the capture waits for it. Without it, a press logged whichever
// sweep already sat in g_entries — up to SCAN_INTERVAL_MS stale — while the
// API reported a fresh capture.
unsigned long g_scanCompletedCount = 0;
unsigned long g_heatmapPendingAfterScan = 0;
// A sweep takes roughly 2-4 s. Give a pending press generous slack before
// dropping it, so a BLE scan holding the radio does not strand it forever.
constexpr unsigned long HEATMAP_PENDING_TIMEOUT_MS = 15000;

// Why a press was refused, so the HTTP layer can stop reporting success
// unconditionally.
enum class SnapshotRequest {
  Scheduled,
  RejectedOta,
  RejectedBleBusy,
  RejectedCooldown,
};


// Estimote's public development UUID — commonly recognized by BLE scanners.
constexpr char     IBEACON_UUID[]   = "B9407F30-F5F8-466E-AFF9-25565B57FE6D";
constexpr uint16_t IBEACON_MAJOR    = 1;
constexpr uint16_t IBEACON_MINOR    = 1;
constexpr int8_t   IBEACON_TX_POWER = -59;

// ─── Forward declarations ───────────────────────────────────────────────────
void setup_wifi_ap();
void setup_ble();
void setup_ota();
void start_wifi_scan();
void begin_ble_scan();
void complete_ble_scan();
void on_ble_scan_complete(BLEScanResults results);
void handle_root();
void handle_scan_json();
void handle_ble_scan();
void handle_ble_json();
void handle_heatmap_scan();
void handle_heatmap_json();
void handle_heatmap_csv();
void handle_heatmap_clear();
SnapshotRequest schedule_heatmap_snapshot(const String& tag);
void capture_heatmap_snapshot(const String& tag);

// ─── Arduino entry points ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] esp32-scanner-beacon");

  pinMode(PIN_WIFI_LED, OUTPUT);
  pinMode(PIN_BLE_LED, OUTPUT);
  digitalWrite(PIN_WIFI_LED, LOW);
  digitalWrite(PIN_BLE_LED, LOW);

  setup_wifi_ap();
  setup_ble();
  setup_ota();

  server.on("/", HTTP_GET, handle_root);
  server.on("/scan.json", HTTP_GET, handle_scan_json);
  server.on("/ble/scan", HTTP_POST, handle_ble_scan);
  server.on("/ble.json", HTTP_GET, handle_ble_json);
  server.on("/heatmap/scan", HTTP_POST, handle_heatmap_scan);
  server.on("/heatmap.json", HTTP_GET, handle_heatmap_json);
  server.on("/heatmap.csv", HTTP_GET, handle_heatmap_csv);
  server.on("/heatmap/clear", HTTP_POST, handle_heatmap_clear);
  server.begin();
  Serial.println("[http] listening on http://192.168.4.1/");

  start_wifi_scan();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  // A BLE request waits for any WiFi sweep already in flight, then owns the
  // radio for five seconds. No new WiFi sweep can begin while it waits.
  if (g_radio.state() == RadioState::BleRequested && !g_scanInProgress) {
    begin_ble_scan();
  }
  if (g_bleScanCallbackPending) {
    complete_ble_scan();
  }

  if (!g_otaActive && g_radio.wifiScanAllowed() && !g_scanInProgress &&
      (millis() - g_lastScanStart > SCAN_INTERVAL_MS)) {
    start_wifi_scan();
  }

  if (g_scanInProgress) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      g_entryCount = (size_t)min(n, (int)MAX_ENTRIES);
      for (size_t i = 0; i < g_entryCount; i++) {
        g_entries[i].ssid      = WiFi.SSID(i);
        g_entries[i].bssid     = WiFi.BSSIDstr(i);
        g_entries[i].rssi      = WiFi.RSSI(i);
        g_entries[i].channel   = WiFi.channel(i);
        g_entries[i].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      }
      WiFi.scanDelete();  // every field above must already be copied out
      g_scanInProgress = false;
      g_scanCompletedCount++;
      Serial.printf("[scan] %u networks\n", (unsigned)g_entryCount);
      digitalWrite(PIN_WIFI_LED, HIGH);
      g_wifiLedOnAt = millis();
    } else if (n == WIFI_SCAN_FAILED) {
      Serial.println("[scan] failed");
      g_scanInProgress = false;
    }
  }

  if (g_wifiLedOnAt && (millis() - g_wifiLedOnAt > LED_FLASH_MS)) {
    digitalWrite(PIN_WIFI_LED, LOW);
    g_wifiLedOnAt = 0;
  }

  // A heatmap snapshot wants a sweep that completed after the press — never
  // whatever was already sitting in g_entries.
  if (g_heatmapSnapshotPending && !g_scanInProgress) {
    if (g_scanCompletedCount >= g_heatmapPendingAfterScan) {
      capture_heatmap_snapshot(g_heatmapPendingTag);
    } else if (millis() - g_heatmapPendingAt > HEATMAP_PENDING_TIMEOUT_MS) {
      Serial.println("[heatmap] snapshot dropped — no sweep completed in time");
      g_heatmapSnapshotPending = false;
    } else {
      start_wifi_scan();  // no-op while BLE or OTA holds the radio
    }
  }

  // Solid means discovery is running. A heartbeat means the iBeacon is live.
  static unsigned long lastBeat = 0;
  static bool beatState = false;
  if (g_radio.state() == RadioState::BleScanning) {
    digitalWrite(PIN_BLE_LED, HIGH);
  } else if (g_radio.advertisingShouldRun() && millis() - lastBeat > 100) {
    lastBeat = millis();
    beatState = !beatState;
    digitalWrite(PIN_BLE_LED, beatState);
  } else if (!g_radio.advertisingShouldRun()) {
    digitalWrite(PIN_BLE_LED, LOW);
  }

  yield();
}

// ─── WiFi AP setup ──────────────────────────────────────────────────────────
void setup_wifi_ap() {
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK)) {
    Serial.println("[ap] softAPConfig FAILED");
  }
  if (!WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 4)) {
    Serial.println("[ap] softAP FAILED");
    return;
  }
  Serial.printf("[ap] SSID=%s  IP=%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
  Serial.printf("[sta] joining %s (non-blocking)\n", WIFI_STA_SSID);
#endif
}

// ─── BLE beacon + passive scanner setup ─────────────────────────────────────
// Parse a canonical UUID into bytes in written order. BLEUUID::getNative()
// stores uuid128 reversed, which is wrong for an iBeacon payload memcpy.
static bool parse_uuid128(const char* text, uint8_t out[16]) {
  int nibble = 0;
  for (const char* p = text; *p; ++p) {
    if (*p == '-') continue;
    if (nibble >= 32) return false;
    uint8_t value;
    if      (*p >= '0' && *p <= '9') value = *p - '0';
    else if (*p >= 'a' && *p <= 'f') value = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F') value = *p - 'A' + 10;
    else return false;
    if (nibble % 2 == 0) out[nibble / 2]  = value << 4;
    else                 out[nibble / 2] |= value;
    nibble++;
  }
  return nibble == 32;
}

void setup_ble() {
  uint8_t uuid[16];
  if (!parse_uuid128(IBEACON_UUID, uuid)) {
    Serial.println("[ble] bad IBEACON_UUID — BLE disabled");
    return;
  }

  BLEDevice::init("");
  g_bleAdvertising = BLEDevice::getAdvertising();
  g_bleScan = BLEDevice::getScan();

  uint8_t manufacturer[25];
  size_t k = 0;
  manufacturer[k++] = 0x4C; manufacturer[k++] = 0x00;
  manufacturer[k++] = 0x02; manufacturer[k++] = 0x15;
  memcpy(&manufacturer[k], uuid, 16); k += 16;
  manufacturer[k++] = (IBEACON_MAJOR >> 8) & 0xFF;
  manufacturer[k++] =  IBEACON_MAJOR       & 0xFF;
  manufacturer[k++] = (IBEACON_MINOR >> 8) & 0xFF;
  manufacturer[k++] =  IBEACON_MINOR       & 0xFF;
  manufacturer[k++] = (uint8_t)IBEACON_TX_POWER;
  static_assert(sizeof(manufacturer) == 25, "iBeacon manufacturer payload must be 25 bytes");

  BLEAdvertisementData advertisementData;
  advertisementData.setFlags(0x06);
  advertisementData.setManufacturerData(
      std::string((const char*)manufacturer, sizeof(manufacturer)));

  std::string payload = advertisementData.getPayload();
  Serial.printf("[ble] adv payload %u bytes (cap 31): ", (unsigned)payload.size());
  for (size_t i = 0; i < payload.size(); i++) {
    Serial.printf("%02X", (uint8_t)payload[i]);
  }
  Serial.println();
  if (payload.size() > 31) {
    Serial.println("[ble] payload OVER 31 bytes — BLE disabled");
    return;
  }

  g_bleAdvertising->setAdvertisementData(advertisementData);
  g_bleAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  g_bleAdvertising->setMinInterval(160);
  g_bleAdvertising->setMaxInterval(320);
  g_bleAdvertising->start();

  // Passive discovery listens only. Short window leaves room for the WiFi AP.
  g_bleScan->setActiveScan(false);
  g_bleScan->setInterval(120);
  g_bleScan->setWindow(80);

  Serial.printf("[ble] iBeacon started — UUID=%s  major=%u  minor=%u  tx=%d\n",
                IBEACON_UUID, IBEACON_MAJOR, IBEACON_MINOR, IBEACON_TX_POWER);
  Serial.printf("[ble-scan] passive discovery ready — %us on demand, max %u devices\n",
                (unsigned)BLE_SCAN_SECONDS, (unsigned)MAX_BLE_ENTRIES);
}

void on_ble_scan_complete(BLEScanResults results) {
  (void)results;
  g_bleScanCallbackPending = true;
}

void begin_ble_scan() {
  if (!g_bleScan || !g_bleAdvertising || !g_radio.beginBleScan()) {
    g_radio.cancelBleScan();
    Serial.println("[ble-scan] unavailable");
    return;
  }

  g_bleAdvertising->stop();
  digitalWrite(PIN_BLE_LED, HIGH);
  g_bleScan->clearResults();
  if (!g_bleScan->start(BLE_SCAN_SECONDS, on_ble_scan_complete, false)) {
    g_radio.cancelBleScan();
    g_bleAdvertising->start();
    digitalWrite(PIN_BLE_LED, LOW);
    Serial.println("[ble-scan] failed to start; iBeacon resumed");
    return;
  }
  Serial.printf("[ble-scan] started — passive, %us; iBeacon paused\n",
                (unsigned)BLE_SCAN_SECONDS);
}

void complete_ble_scan() {
  g_bleScanCallbackPending = false;
  if (!g_bleScan) return;

  // A callback caused by OTA preemption is stale. Clear it without leaving OTA.
  if (!g_radio.finishBleScan()) {
    g_bleScan->clearResults();
    return;
  }

  BLEScanResults results = g_bleScan->getResults();
  g_bleEntryCount = (size_t)min(results.getCount(), (int)MAX_BLE_ENTRIES);
  for (size_t i = 0; i < g_bleEntryCount; i++) {
    BLEAdvertisedDevice device = results.getDevice(i);
    g_bleEntries[i].name = device.haveName()
        ? String(device.getName().c_str())
        : String();
    g_bleEntries[i].address = String(device.getAddress().toString().c_str());
    g_bleEntries[i].rssi = device.haveRSSI() ? device.getRSSI() : -127;
    g_bleEntries[i].company = -1;
    if (device.haveManufacturerData()) {
      std::string data = device.getManufacturerData();
      if (data.size() >= 2) {
        g_bleEntries[i].company =
            (uint8_t)data[0] | ((uint16_t)(uint8_t)data[1] << 8);
      }
    }
  }
  g_bleScan->clearResults();
  g_lastScanStart = millis();  // let the AP settle before the next WiFi sweep

  if (!g_otaActive && g_bleAdvertising) {
    g_bleAdvertising->start();
  }
  digitalWrite(PIN_BLE_LED, LOW);
  Serial.printf("[ble-scan] complete — %u devices; iBeacon resumed\n",
                (unsigned)g_bleEntryCount);
}

// ─── OTA ────────────────────────────────────────────────────────────────────
void setup_ota() {
#ifndef OTA_PASSWORD
  Serial.println("[ota] DISABLED — no OTA_PASSWORD in secrets.h.");
  Serial.println("[ota] Refusing unauthenticated updates on an open AP.");
  return;
#else
  // A placeholder password is worse than no password: the AP is open and the
  // placeholder is published in include/secrets.example.h for anyone to read.
  // A board that boots with "change-me" is a board anyone in radio range can
  // reflash, so this is a build failure rather than a runtime warning.
  static_assert(!ota_password_equals(OTA_PASSWORD, "change-me"),
                "OTA_PASSWORD is still the placeholder from "
                "include/secrets.example.h — set a real one, or comment the "
                "#define out to build with OTA disabled.");
  static_assert(ota_password_length(OTA_PASSWORD) >= 8,
                "OTA_PASSWORD must be at least 8 characters — it is the only "
                "thing protecting firmware upload on an open access point.");

  ArduinoOTA.setHostname("jesse-scanner");
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    g_otaActive = true;
    bool bleWasScanning = g_radio.state() == RadioState::BleScanning;
    g_radio.beginOta();

    if (g_scanInProgress) {
      WiFi.scanDelete();
      g_scanInProgress = false;
    }
    if (bleWasScanning && g_bleScan) {
      g_bleScan->stop();
    }
    if (g_bleAdvertising) {
      g_bleAdvertising->stop();
    }
    digitalWrite(PIN_BLE_LED, LOW);
    Serial.println("\n[ota] upload started — radio surveys suspended");
  });

  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static int lastPercent = -1;
    int percent = total ? (int)((done * 100UL) / total) : 0;
    if (percent != lastPercent && percent % 10 == 0) {
      lastPercent = percent;
      Serial.printf("[ota] %d%%\n", percent);
    }
    digitalWrite(PIN_WIFI_LED, (done / 16384) & 1);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("[ota] done — rebooting into the new firmware");
    digitalWrite(PIN_WIFI_LED, LOW);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    g_otaActive = false;
    g_radio.finishOta();
    if (g_bleAdvertising) {
      g_bleAdvertising->start();
    }
    const char* message = "unknown";
    switch (error) {
      case OTA_AUTH_ERROR:    message = "auth failed"; break;
      case OTA_BEGIN_ERROR:   message = "begin failed"; break;
      case OTA_CONNECT_ERROR: message = "connect failed"; break;
      case OTA_RECEIVE_ERROR: message = "receive failed"; break;
      case OTA_END_ERROR:     message = "end failed"; break;
    }
    Serial.printf("[ota] ERROR: %s — scanner and beacon resumed\n", message);
  });

  ArduinoOTA.begin();
  Serial.printf("[ota] ready, password set — %s", WiFi.softAPIP().toString().c_str());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" and %s", WiFi.localIP().toString().c_str());
  }
  Serial.println(" (pio run -e ota -t upload)");
#endif
}

// ─── WiFi scan ──────────────────────────────────────────────────────────────
void start_wifi_scan() {
  if (!g_radio.wifiScanAllowed()) return;
  WiFi.scanNetworks(true, true);
  g_scanInProgress = true;
  g_lastScanStart = millis();
}

// ─── Heatmap capture ────────────────────────────────────────────────────────
// Drop the oldest sample, append the newest. 16 snapshots × 6 APs keeps the
// heap impact under 10 KB and gives a useful walkaround record.
static void shift_oldest_heatmap_sample() {
  for (size_t i = 1; i < g_heatmapCount; i++) {
    g_heatmapSamples[i - 1] = g_heatmapSamples[i];
  }
  if (g_heatmapCount > 0) g_heatmapCount--;
}

void capture_heatmap_snapshot(const String& tag) {
  g_heatmapSnapshotPending = false;
  if (g_otaActive) return;
  if (g_entryCount == 0) {
    Serial.println("[heatmap] snapshot dropped — the sweep found no networks");
    return;
  }

  if (g_heatmapCount >= MAX_HEATMAP_SAMPLES) {
    shift_oldest_heatmap_sample();
  }
  HeatmapSample sample;
  sample.tag = tag.length() ? tag : String("spot");
  sample.timestamp = millis();
  sample.rowCount = (uint8_t)min<size_t>(MAX_HEATMAP_ROWS_PER_SAMPLE, g_entryCount);

  // Rank the strongest APs. Every field comes from g_entries, which was filled
  // while the scan result was still alive; WiFi.BSSIDstr() would return nothing
  // here, because the sweep is deleted the moment it completes.
  struct Ranked { int32_t rssi; size_t index; };
  Ranked ranked[MAX_ENTRIES];
  for (size_t i = 0; i < g_entryCount; i++) {
    ranked[i] = { g_entries[i].rssi, i };
  }
  for (size_t i = 0; i < g_entryCount; i++) {
    for (size_t j = i + 1; j < g_entryCount; j++) {
      if (ranked[j].rssi > ranked[i].rssi) {
        Ranked swap = ranked[i];
        ranked[i] = ranked[j];
        ranked[j] = swap;
      }
    }
  }
  for (uint8_t i = 0; i < sample.rowCount; i++) {
    size_t src = ranked[i].index;
    sample.rows[i].ssid    = g_entries[src].ssid;
    sample.rows[i].channel = g_entries[src].channel;
    sample.rows[i].rssi    = g_entries[src].rssi;
    sample.rows[i].bssid   = g_entries[src].bssid;
  }
  g_heatmapSamples[g_heatmapCount++] = sample;
  g_heatmapSnapshotAt = millis();
  Serial.printf("[heatmap] captured \"%s\" — %u rows; %u samples in RAM\n",
                sample.tag.c_str(), (unsigned)sample.rowCount,
                (unsigned)g_heatmapCount);
}

SnapshotRequest schedule_heatmap_snapshot(const String& tag) {
  if (g_otaActive) return SnapshotRequest::RejectedOta;
  // Refuse presses that arrive during a BLE scan: the radio is unavailable.
  if (g_radio.state() == RadioState::BleRequested ||
      g_radio.state() == RadioState::BleScanning) {
    Serial.println("[heatmap] snapshot refused — BLE scan in progress");
    return SnapshotRequest::RejectedBleBusy;
  }
  const unsigned long now = millis();
  // g_heatmapSnapshotAt is 0 until the first capture, so guard against the
  // cooldown firing on a press made in the first two seconds after boot.
  if (g_heatmapSnapshotAt &&
      now - g_heatmapSnapshotAt < HEATMAP_SNAPSHOT_COOLDOWN_MS) {
    Serial.println("[heatmap] snapshot refused — cooldown");
    return SnapshotRequest::RejectedCooldown;
  }
  g_heatmapPendingTag = tag;
  g_heatmapPendingAt = now;
  g_heatmapSnapshotPending = true;
  // Demand one sweep that completes from here on. A sweep already in flight
  // counts, because it lands after the press. Otherwise start one now.
  g_heatmapPendingAfterScan = g_scanCompletedCount + 1;
  if (!g_scanInProgress) {
    start_wifi_scan();
  }
  return SnapshotRequest::Scheduled;
}

// ─── HTTP handlers ──────────────────────────────────────────────────────────
void handle_root() {
  server.send(200, "text/html", INDEX_HTML);
}

// SSIDs and BLE names are arbitrary bytes. Escape them before JSON encoding.
static String json_escape(const String& input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    switch (c) {
      case '"':  output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\n': output += "\\n";  break;
      case '\r': output += "\\r";  break;
      case '\t': output += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)(uint8_t)c);
          output += escaped;
        } else {
          output += c;
        }
    }
  }
  return output;
}

void handle_scan_json() {
  String body = "{\"entries\":[";
  for (size_t i = 0; i < g_entryCount; i++) {
    if (i) body += ",";
    body += "{\"ssid\":\"" + json_escape(g_entries[i].ssid) + "\",";
    body += "\"rssi\":"      + String(g_entries[i].rssi) + ",";
    body += "\"channel\":"   + String(g_entries[i].channel) + ",";
    body += "\"encrypted\":" + String(g_entries[i].encrypted ? "true" : "false") + "}";
  }
  body += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

void handle_ble_scan() {
  server.sendHeader("Cache-Control", "no-store");
  if (!g_bleScan || !g_bleAdvertising) {
    server.send(503, "application/json", "{\"accepted\":false,\"error\":\"BLE unavailable\"}");
    return;
  }
  if (g_otaActive) {
    server.send(409, "application/json", "{\"accepted\":false,\"error\":\"OTA active\"}");
    return;
  }
  if (g_radio.requestBleScan()) {
    server.send(202, "application/json", "{\"accepted\":true,\"scanning\":true}");
  } else {
    server.send(202, "application/json", "{\"accepted\":false,\"scanning\":true}");
  }
}

void handle_ble_json() {
  bool scanning = g_radio.state() == RadioState::BleRequested ||
                  g_radio.state() == RadioState::BleScanning;
  String body = "{\"scanning\":" + String(scanning ? "true" : "false") + ",\"entries\":[";
  for (size_t i = 0; i < g_bleEntryCount; i++) {
    if (i) body += ",";
    body += "{\"name\":\"" + json_escape(g_bleEntries[i].name) + "\",";
    body += "\"address\":\"" + json_escape(g_bleEntries[i].address) + "\",";
    body += "\"rssi\":" + String(g_bleEntries[i].rssi) + ",";
    body += "\"company\":" + String(g_bleEntries[i].company) + "}";
  }
  body += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

// ─── Heatmap HTTP ───────────────────────────────────────────────────────────
void handle_heatmap_scan() {
  server.sendHeader("Cache-Control", "no-store");
  String tag = server.arg("tag");
  tag.trim();
  if (tag.length() > 24) tag = tag.substring(0, 24);
  switch (schedule_heatmap_snapshot(tag)) {
    case SnapshotRequest::Scheduled:
      server.send(202, "application/json", "{\"accepted\":true,\"tag\":\"" +
                                    json_escape(tag) + "\"}");
      return;
    case SnapshotRequest::RejectedOta:
      server.send(409, "application/json",
                  "{\"accepted\":false,\"error\":\"firmware update in progress\"}");
      return;
    case SnapshotRequest::RejectedBleBusy:
      server.send(409, "application/json",
                  "{\"accepted\":false,\"error\":\"Bluetooth scan is using the radio\"}");
      return;
    case SnapshotRequest::RejectedCooldown: {
      const unsigned long elapsed = millis() - g_heatmapSnapshotAt;
      const unsigned long remaining =
          elapsed < HEATMAP_SNAPSHOT_COOLDOWN_MS
              ? HEATMAP_SNAPSHOT_COOLDOWN_MS - elapsed
              : 0;
      server.send(429, "application/json",
                  "{\"accepted\":false,\"error\":\"too soon after the last sample\","
                  "\"retry_after_ms\":" + String(remaining) + "}");
      return;
    }
  }
}

void handle_heatmap_json() {
  // Newest sample first. Newest entries appear at the top of the table.
  String body = "{\"entries\":[";
  bool firstRow = true;
  unsigned long now = millis();
  for (size_t sampleIdx = g_heatmapCount; sampleIdx > 0; sampleIdx--) {
    const HeatmapSample& sample = g_heatmapSamples[sampleIdx - 1];
    unsigned long ageSeconds = (now - sample.timestamp) / 1000UL;
    for (uint8_t rowIdx = 0; rowIdx < sample.rowCount; rowIdx++) {
      if (!firstRow) body += ",";
      firstRow = false;
      const HeatmapRow& row = sample.rows[rowIdx];
      body += "{\"tag\":\"" + json_escape(sample.tag) + "\",";
      body += "\"age\":" + String(ageSeconds) + ",";
      body += "\"ssid\":\"" + json_escape(row.ssid) + "\",";
      body += "\"channel\":" + String(row.channel) + ",";
      body += "\"rssi\":" + String(row.rssi) + "}";
    }
  }
  body += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

void handle_heatmap_csv() {
  // RFC 4180 encoding lives in heatmap_csv.h, which tests/test_heatmap_csv.cpp
  // compiles and round-trips through a parser. Do not inline it again here.
  String csv = heatmap::build_csv<String>(g_heatmapSamples, g_heatmapCount, millis());
  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"jesse-heatmap.csv\"");
  server.send(200, "text/csv", csv);
}

void handle_heatmap_clear() {
  for (size_t i = 0; i < g_heatmapCount; i++) {
    g_heatmapSamples[i].tag = String();
    g_heatmapSamples[i].timestamp = 0;
    g_heatmapSamples[i].rowCount = 0;
    for (uint8_t r = 0; r < MAX_HEATMAP_ROWS_PER_SAMPLE; r++) {
      g_heatmapSamples[i].rows[r].ssid = String();
      g_heatmapSamples[i].rows[r].bssid = String();
      g_heatmapSamples[i].rows[r].channel = 0;
      g_heatmapSamples[i].rows[r].rssi = 0;
    }
  }
  g_heatmapCount = 0;
  g_heatmapSnapshotPending = false;
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"cleared\":true}");
}
