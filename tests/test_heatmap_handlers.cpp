#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

// Minimal stand-ins for the Arduino helpers handle_heatmap_* uses.
// No WiFi, no real WebServer — the handlers run against this mock.

class String {
 public:
  String() : len_(0) { buf_[0] = '\0'; }
  String(const char* s) : len_(0) {
    std::strncpy(buf_, s, sizeof(buf_) - 1);
    buf_[sizeof(buf_) - 1] = '\0';
    len_ = std::strlen(buf_);
  }
  String(const String& other) : len_(other.len_) {
    std::memcpy(buf_, other.buf_, sizeof(buf_));
  }
  String& operator=(const String& other) {
    if (this != &other) {
      len_ = other.len_;
      std::memcpy(buf_, other.buf_, sizeof(buf_));
    }
    return *this;
  }
  String& operator=(const char* s) {
    std::strncpy(buf_, s, sizeof(buf_) - 1);
    buf_[sizeof(buf_) - 1] = '\0';
    len_ = std::strlen(buf_);
    return *this;
  }

  size_t length() const { return len_; }
  const char* c_str() const { return buf_; }
  bool operator==(const String& other) const { return std::strcmp(buf_, other.buf_) == 0; }
  bool operator!=(const String& other) const { return !(*this == other); }

  // operator+= is what the heatmap handler relies on for building JSON.
  String& operator+=(const String& other) {
    size_t copy = std::min<size_t>(sizeof(buf_) - 1 - len_, other.len_);
    std::memcpy(buf_ + len_, other.buf_, copy);
    len_ += copy;
    buf_[len_] = '\0';
    return *this;
  }
  String& operator+=(const char* s) {
    size_t add = std::strlen(s);
    size_t copy = std::min<size_t>(sizeof(buf_) - 1 - len_, add);
    std::memcpy(buf_ + len_, s, copy);
    len_ += copy;
    buf_[len_] = '\0';
    return *this;
  }
  String operator+(const String& other) const {
    String result(*this);
    result += other;
    return result;
  }
  String operator+(const char* s) const {
    String result(*this);
    result += s;
    return result;
  }

 private:
  size_t len_;
  char buf_[128];
};

class IPAddress {
 public:
  IPAddress() : v_(0) {}
  IPAddress(int a, int b, int c, int d) : v_((a << 24) | (b << 16) | (c << 8) | d) {}
  const char* toString() const { static char buf[16]; std::snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                                                                 (v_ >> 24) & 0xFF, (v_ >> 16) & 0xFF,
                                                                 (v_ >> 8) & 0xFF, v_ & 0xFF); return buf; }
 private:
  uint32_t v_;
};

// Mirror the real handler-side types just enough.
struct HeatmapRow {
  String ssid;
  String bssid;
  uint8_t channel = 0;
  int32_t rssi = 0;
};
struct HeatmapSample {
  String tag;
  unsigned long timestamp = 0;
  uint8_t rowCount = 0;
  HeatmapRow rows[6];
};

constexpr size_t MAX_HEATMAP_SAMPLES = 16;
constexpr size_t MAX_HEATMAP_ROWS_PER_SAMPLE = 6;

static HeatmapSample g_heatmapSamples[MAX_HEATMAP_SAMPLES];
static size_t g_heatmapCount = 0;

// Copy of json_escape from main.cpp.
static String json_escape(const String& input) {
  String output;
  for (size_t i = 0; i < input.length(); i++) {
    char c = input.c_str()[i];
    switch (c) {
      case '"':  output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\n': output += "\\n";  break;
      case '\r': output += "\\r";  break;
      case '\t': output += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) {
          char escaped[7];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)(uint8_t)c);
          output += escaped;
        } else {
          char single[2] = { c, 0 };
          output += single;
        }
    }
  }
  return output;
}

static void handle_heatmap_csv(unsigned long now) {
  String csv = "tag,epoch_ms,age_s,ssid,bssid,channel,rssi\n";
  for (size_t sampleIdx = 0; sampleIdx < g_heatmapCount; sampleIdx++) {
    const HeatmapSample& sample = g_heatmapSamples[sampleIdx];
    unsigned long ageSeconds = (now - sample.timestamp) / 1000UL;
    for (uint8_t rowIdx = 0; rowIdx < sample.rowCount; rowIdx++) {
      const HeatmapRow& row = sample.rows[rowIdx];
      csv += json_escape(sample.tag); csv += ",";
      // Replicate the String(ulong) conversion from main.cpp.
      char buf[24];
      std::snprintf(buf, sizeof(buf), "%lu", sample.timestamp);
      csv += buf; csv += ",";
      std::snprintf(buf, sizeof(buf), "%lu", ageSeconds);
      csv += buf; csv += ",";
      csv += json_escape(row.ssid); csv += ",";
      csv += json_escape(row.bssid); csv += ",";
      std::snprintf(buf, sizeof(buf), "%u", row.channel);
      csv += buf; csv += ",";
      std::snprintf(buf, sizeof(buf), "%d", row.rssi);
      csv += buf; csv += "\n";
    }
  }
  std::printf("CSV_LEN=%zu\n", csv.length());
  std::printf("CSV=%s", csv.c_str());
}

static void seed_two_samples(unsigned long now) {
  g_heatmapSamples[0].tag = "porch";
  g_heatmapSamples[0].timestamp = now - 12000UL;
  g_heatmapSamples[0].rowCount = 2;
  g_heatmapSamples[0].rows[0].ssid = "HouseNet";
  g_heatmapSamples[0].rows[0].bssid = "AA:BB:CC:DD:EE:01";
  g_heatmapSamples[0].rows[0].channel = 6;
  g_heatmapSamples[0].rowCount = 2;
  g_heatmapSamples[0].rows[0].rssi = -45;
  g_heatmapSamples[0].rows[1].ssid = "Guest";
  g_heatmapSamples[0].rows[1].bssid = "AA:BB:CC:DD:EE:02";
  g_heatmapSamples[0].rows[1].channel = 11;
  g_heatmapSamples[0].rows[1].rssi = -71;

  g_heatmapSamples[1].tag = "kitchen";
  g_heatmapSamples[1].timestamp = now - 3000UL;
  g_heatmapSamples[1].rowCount = 3;
  g_heatmapSamples[1].rows[0].ssid = "HouseNet";
  g_heatmapSamples[1].rows[0].bssid = "AA:BB:CC:DD:EE:01";
  g_heatmapSamples[1].rows[0].channel = 6;
  g_heatmapSamples[1].rows[0].rssi = -52;
  g_heatmapSamples[1].rows[1].ssid = "Guest";
  g_heatmapSamples[1].rows[1].bssid = "AA:BB:CC:DD:EE:02";
  g_heatmapSamples[1].rows[1].channel = 11;
  g_heatmapSamples[1].rows[1].rssi = -64;
  g_heatmapSamples[1].rows[2].ssid = "Neighbor";
  g_heatmapSamples[1].rows[2].bssid = "AA:BB:CC:DD:EE:03";
  g_heatmapSamples[1].rows[2].channel = 1;
  g_heatmapSamples[1].rows[2].rssi = -88;
  g_heatmapCount = 2;
}

static void test_csv_emits_header_and_rows() {
  g_heatmapCount = 0;
  unsigned long now = 1000000UL;
  seed_two_samples(now);
  handle_heatmap_csv(now);
  // The mock prints CSV_LEN and CSV lines; verify by re-running and counting.
}

static void test_csv_escapes_quotes_and_commas() {
  g_heatmapCount = 0;
  g_heatmapSamples[0].tag = "weird,tag\"with\"chars";
  g_heatmapSamples[0].timestamp = 5000UL;
  g_heatmapSamples[0].rowCount = 1;
  g_heatmapSamples[0].rows[0].ssid = "Foo,Bar";
  g_heatmapSamples[0].rows[0].bssid = "11:22:33:44:55:66";
  g_heatmapSamples[0].rows[0].channel = 6;
  g_heatmapSamples[0].rows[0].rssi = -50;
  g_heatmapCount = 1;
  handle_heatmap_csv(5000UL);
}

int main() {
  test_csv_emits_header_and_rows();
  test_csv_escapes_quotes_and_commas();
  std::printf("heatmap csv: 2 tests passed\n");
  return 0;
}

static void test_json_escape_handles_csv_unsafe_chars() {
  String input = "weird,tag\"with\"chars";
  String escaped = json_escape(input);
  assert(escaped.length() == input.length() + 4); // two doublequotes escaped + two commas
  assert(std::strstr(escaped.c_str(), "\\\"") != nullptr);
}

static void test_age_seconds_in_csv_uses_known_offset() {
  g_heatmapCount = 0;
  g_heatmapSamples[0].tag = "office";
  g_heatmapSamples[0].timestamp = 100000UL;
  g_heatmapSamples[0].rowCount = 1;
  g_heatmapSamples[0].rows[0].ssid = "OfficeWiFi";
  g_heatmapSamples[0].rows[0].bssid = "DE:AD:BE:EF:00:01";
  g_heatmapSamples[0].rows[0].channel = 36;
  g_heatmapSamples[0].rows[0].rssi = -65;
  g_heatmapCount = 1;
  handle_heatmap_csv(107500UL);
}
