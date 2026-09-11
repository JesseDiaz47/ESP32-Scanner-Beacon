// esp32-scanner-beacon
// One board, two roles:
//   1. WiFi AP-mode scanner — phone connects to "jesse-scanner", sees live SSID table at 192.168.4.1
//   2. BLE iBeacon — broadcasts Estimote's dev UUID so any BLE scanner app sees proximity dots
//
// LEDs:
//   GPIO 2  = onboard blue LED — flashes ~120ms each time a WiFi scan completes
//   GPIO 4  = any free GPIO — toggles at ~5Hz while the BLE beacon is alive
//
// Power: NULLLAB 1200mAh LiPo module -> VIN. ~2-3 hours with both radios active.
// Board: ESP32-WROOM-32 DevKit V1, 30-pin.
//
// Legal note: passive WiFi scanning is legal everywhere. BLE advertising is legal
//             everywhere. DO NOT add deauth/jamming — that is illegal.
//
// Known behavior (not a bug): the ESP32 has ONE radio shared by the AP and the
// scanner. Each scan hops all 14 channels for ~2s, during which the AP stops
// beaconing and the phone's HTTP fetch may fail. The page catches that and keeps
// showing the last table, so it looks like a brief pause rather than an error.

#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <BLEUtils.h>
#include <BLEUUID.h>

// ─── Pin map ────────────────────────────────────────────────────────────────
constexpr uint8_t PIN_WIFI_LED = 2;   // onboard blue LED — scan heartbeat
constexpr uint8_t PIN_BLE_LED  = 4;   // any free GPIO — BLE heartbeat

// ─── WiFi AP ────────────────────────────────────────────────────────────────
// NOTE: IPAddress is not a literal type on this core, so these cannot be
// constexpr. File-scope const objects are fine — built before setup() runs.
constexpr char AP_SSID[] = "jesse-scanner";
constexpr char AP_PASS[] = "";              // open network for ease of field use
const IPAddress AP_IP  (192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);  // subnet mask, NOT a second address

WebServer server(80);

// ─── Scan state ─────────────────────────────────────────────────────────────
struct ScanEntry {
  String  ssid;
  int32_t rssi;
  uint8_t channel;
  bool    encrypted;
};
constexpr size_t MAX_ENTRIES = 32;
ScanEntry g_entries[MAX_ENTRIES];
size_t        g_entryCount     = 0;
bool          g_scanInProgress = false;
unsigned long g_lastScanStart  = 0;
unsigned long g_wifiLedOnAt    = 0;     // 0 = LED is off
constexpr unsigned long SCAN_INTERVAL_MS = 10000;  // scan every 10s
constexpr unsigned long LED_FLASH_MS     = 120;

// ─── BLE iBeacon ────────────────────────────────────────────────────────────
// Estimote's public development UUID — every BLE scanner app recognizes it.
constexpr char     IBEACON_UUID[]   = "B9407F30-F5F8-466E-AFF9-25565B57FE6D";
constexpr uint16_t IBEACON_MAJOR    = 1;
constexpr uint16_t IBEACON_MINOR    = 1;
constexpr int8_t   IBEACON_TX_POWER = -59;  // calibrated RSSI at 1m

// ─── HTML page (the phone UI) ───────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>jesse-scanner</title>
<style>
  body { font: 14px/1.4 -apple-system, system-ui, sans-serif; margin: 0; padding: 12px;
         background: #111; color: #eee; }
  h1   { font-size: 18px; margin: 0 0 8px; color: #6cf; }
  .meta { color: #888; font-size: 12px; margin-bottom: 12px; }
  table { width: 100%; border-collapse: collapse; }
  th, td { padding: 6px 8px; text-align: left; border-bottom: 1px solid #222; }
  th     { color: #888; font-weight: normal; font-size: 12px; text-transform: uppercase; }
  td.rssi { font-family: ui-monospace, Menlo, monospace; text-align: right; width: 90px;
            white-space: nowrap; }
  td.enc  { text-align: center; width: 30px; }
  .bar    { display: inline-block; width: 4px; margin-right: 1px; background: #6cf;
            vertical-align: baseline; }
  .empty  { color: #555; font-style: italic; padding: 24px; text-align: center; }
  .live   { color: #6cf; }
  .live::before { content: "\25CF"; margin-right: 4px; animation: pulse 1.5s infinite; }
  @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.3; } }
</style>
</head><body>
<h1>jesse-scanner</h1>
<div class="meta">
  <span class="live">live</span> &middot; <span id="count">0</span> networks &middot;
  strongest first &middot; refreshes every 3s (scan every 10s)
</div>
<table>
  <thead><tr><th>SSID</th><th>CH</th><th class="enc">LOCK</th><th class="rssi">RSSI</th></tr></thead>
  <tbody id="rows"><tr><td colspan="4" class="empty">scanning&hellip;</td></tr></tbody>
</table>
<script>
function emptyRow(body, msg) {
  var tr = body.insertRow();
  var td = tr.insertCell();
  td.colSpan = 4;
  td.className = 'empty';
  td.textContent = msg;
}
async function tick() {
  try {
    var r = await fetch('/scan.json', { cache: 'no-store' });
    var d = await r.json();
    var list = d.entries.slice().sort(function (a, b) { return b.rssi - a.rssi; });
    document.getElementById('count').textContent = list.length;
    var body = document.getElementById('rows');
    body.textContent = '';
    if (!list.length) { emptyRow(body, 'no networks in range'); return; }
    // Built with DOM nodes, not innerHTML: an SSID is attacker-chosen text and
    // must never be parsed as markup.
    list.forEach(function (e) {
      var tr = body.insertRow();
      var tdS = tr.insertCell();
      if (e.ssid) {
        tdS.textContent = e.ssid;
      } else {
        tdS.textContent = '(hidden)';
        tdS.style.color = '#666';
      }
      tr.insertCell().textContent = e.channel;
      var tdE = tr.insertCell();
      tdE.className = 'enc';
      tdE.textContent = e.encrypted ? '●' : '';
      var tdR = tr.insertCell();
      tdR.className = 'rssi';
      var bars = Math.max(0, Math.min(5, Math.floor((e.rssi + 100) / 12)));
      for (var i = 0; i < bars; i++) {
        var s = document.createElement('span');
        s.className = 'bar';
        s.style.height = (i * 3 + 4) + 'px';
        tdR.appendChild(s);
      }
      tdR.appendChild(document.createTextNode(' ' + e.rssi));
    });
  } catch (err) {
    // A scan sweep knocks the AP off-channel for ~2s; keep the last table.
  }
}
tick();
setInterval(tick, 3000);
</script>
</body></html>
)rawliteral";

// ─── Forward declarations ───────────────────────────────────────────────────
void setup_wifi_ap();
void setup_ble_beacon();
void start_scan();
void handle_scan_json();
void handle_root();

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
  setup_ble_beacon();

  server.on("/", HTTP_GET, handle_root);
  server.on("/scan.json", HTTP_GET, handle_scan_json);
  server.begin();
  Serial.println("[http] listening on http://192.168.4.1/");

  start_scan();  // don't make the page sit on "scanning..." for a full interval
}

void loop() {
  server.handleClient();

  if (!g_scanInProgress && (millis() - g_lastScanStart > SCAN_INTERVAL_MS)) {
    start_scan();
  }

  // Drain completed scan results when they become available.
  if (g_scanInProgress) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      g_entryCount = (size_t)min(n, (int)MAX_ENTRIES);
      for (size_t i = 0; i < g_entryCount; i++) {
        g_entries[i].ssid      = WiFi.SSID(i);
        g_entries[i].rssi      = WiFi.RSSI(i);
        g_entries[i].channel   = WiFi.channel(i);
        g_entries[i].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      }
      WiFi.scanDelete();
      g_scanInProgress = false;
      Serial.printf("[scan] %u networks\n", (unsigned)g_entryCount);
      digitalWrite(PIN_WIFI_LED, HIGH);
      g_wifiLedOnAt = millis();
    } else if (n == WIFI_SCAN_FAILED) {
      Serial.println("[scan] failed");
      g_scanInProgress = false;
    }
    // n == WIFI_SCAN_RUNNING (-1) -> keep waiting
  }

  // End the scan flash. Without this the LED latches on after the first scan.
  if (g_wifiLedOnAt && (millis() - g_wifiLedOnAt > LED_FLASH_MS)) {
    digitalWrite(PIN_WIFI_LED, LOW);
    g_wifiLedOnAt = 0;
  }

  // ── BLE heartbeat — toggle every ~100ms so the beacon is visibly alive ───
  static unsigned long lastBeat  = 0;
  static uint8_t       beatState = 0;
  if (millis() - lastBeat > 100) {
    lastBeat = millis();
    digitalWrite(PIN_BLE_LED, beatState);
    beatState = !beatState;
  }

  yield();  // let the WiFi/BLE background tasks run
}

// ─── WiFi AP setup ──────────────────────────────────────────────────────────
void setup_wifi_ap() {
  // AP for the phone + STA so scanNetworks() has an interface to scan with.
  WiFi.mode(WIFI_AP_STA);
  // Configure before starting, so the AP comes up once on the right subnet.
  if (!WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK)) {
    Serial.println("[ap] softAPConfig FAILED");
  }
  if (!WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 4)) {  // channel 1, visible, 4 clients
    Serial.println("[ap] softAP FAILED");
    return;
  }
  Serial.printf("[ap] SSID=%s  IP=%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

// ─── BLE iBeacon setup ──────────────────────────────────────────────────────
// Parse "B9407F30-F5F8-466E-AFF9-25565B57FE6D" into 16 bytes in WRITTEN order.
// We deliberately do NOT use BLEUUID::getNative(), which stores uuid128 in
// reverse byte order — memcpy'ing that straight into the packet broadcasts the
// UUID backwards, and no scanner would match the UUID above.
static bool parse_uuid128(const char* s, uint8_t out[16]) {
  int nib = 0;
  for (const char* p = s; *p; ++p) {
    if (*p == '-') continue;
    if (nib >= 32) return false;
    uint8_t v;
    if      (*p >= '0' && *p <= '9') v = *p - '0';
    else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
    else return false;
    if (nib % 2 == 0) out[nib / 2]  = v << 4;
    else              out[nib / 2] |= v;
    nib++;
  }
  return nib == 32;
}

// Apple iBeacon advertisement, exactly 30 of the 31 permitted payload bytes:
//   [02 01 06]                        flags AD structure                    3
//   [1A FF 4C 00 02 15] [16B UUID]
//     [2B major] [2B minor] [1B tx]   manufacturer-specific AD structure   27
// setManufacturerData() prepends the [length][0xFF] pair itself, so we hand it
// only the 25 bytes from the Apple company ID onward.
void setup_ble_beacon() {
  uint8_t uuid[16];
  if (!parse_uuid128(IBEACON_UUID, uuid)) {
    Serial.println("[ble] bad IBEACON_UUID — beacon not started");
    return;
  }

  BLEDevice::init("");
  BLEAdvertising* adv = BLEDevice::getAdvertising();

  uint8_t md[25];
  size_t k = 0;
  md[k++] = 0x4C; md[k++] = 0x00;                 // Apple company ID, little-endian
  md[k++] = 0x02; md[k++] = 0x15;                 // iBeacon type, 21 bytes follow
  memcpy(&md[k], uuid, 16); k += 16;              // proximity UUID, big-endian
  md[k++] = (IBEACON_MAJOR >> 8) & 0xFF;
  md[k++] =  IBEACON_MAJOR       & 0xFF;
  md[k++] = (IBEACON_MINOR >> 8) & 0xFF;
  md[k++] =  IBEACON_MINOR       & 0xFF;
  md[k++] = (uint8_t)IBEACON_TX_POWER;
  // Every byte is assigned: a short-filled buffer would leak stack garbage
  // into the packet and push it over the 31-byte cap.
  static_assert(sizeof(md) == 25, "iBeacon manufacturer payload must be 25 bytes");

  BLEAdvertisementData advData;
  advData.setFlags(0x06);  // LE General Discoverable + BR/EDR not supported
  advData.setManufacturerData(std::string((const char*)md, sizeof(md)));

  std::string payload = advData.getPayload();
  Serial.printf("[ble] adv payload %u bytes (cap 31): ", (unsigned)payload.size());
  for (size_t i = 0; i < payload.size(); i++) Serial.printf("%02X", (uint8_t)payload[i]);
  Serial.println();
  if (payload.size() > 31) {
    Serial.println("[ble] payload OVER 31 bytes — the controller will reject it");
  }

  adv->setAdvertisementData(advData);
  adv->setAdvertisementType(ADV_TYPE_NONCONN_IND);  // non-connectable beacon
  adv->setMinInterval(160);  // 100ms (units of 0.625ms)
  adv->setMaxInterval(320);  // 200ms
  adv->start();

  Serial.printf("[ble] iBeacon started — UUID=%s  major=%u  minor=%u  tx=%d\n",
                IBEACON_UUID, IBEACON_MAJOR, IBEACON_MINOR, IBEACON_TX_POWER);
}

// ─── WiFi scan ──────────────────────────────────────────────────────────────
void start_scan() {
  WiFi.scanNetworks(true, true);  // async so the web server keeps serving
  g_scanInProgress = true;
  g_lastScanStart  = millis();
}

// ─── HTTP handlers ──────────────────────────────────────────────────────────
void handle_root() {
  server.send(200, "text/html", INDEX_HTML);
}

// SSIDs are arbitrary bytes. An unescaped quote or backslash in one name breaks
// JSON.parse() on the phone and blanks the whole table.
static String json_escape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(uint8_t)c);
          out += buf;
        } else {
          out += c;  // UTF-8 multibyte passes through unchanged
        }
    }
  }
  return out;
}

void handle_scan_json() {
  String body = "{\"entries\":[";
  for (size_t i = 0; i < g_entryCount; i++) {
    if (i) body += ",";
    body += "{\"ssid\":\"" + json_escape(g_entries[i].ssid) + "\",";
    body += "\"rssi\":"    + String(g_entries[i].rssi) + ",";
    body += "\"channel\":" + String(g_entries[i].channel) + ",";
    body += "\"encrypted\":" + String(g_entries[i].encrypted ? "true" : "false") + "}";
  }
  body += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}
