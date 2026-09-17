// Host tests for the heatmap CSV exporter.
//
// This file compiles src/heatmap_csv.h — the same code the firmware ships —
// against a std::string-backed String stand-in, then reads the output back
// with an independently written RFC 4180 parser. The predecessor
// (tests/test_heatmap_handlers.cpp) kept its own copy of the encoder and a
// 128-byte String, so it printed "2 tests passed" while the shipped encoder
// emitted JSON escaping into a .csv and silently truncated long exports.
//
// Build and run:
//   clang++ -std=c++17 -Wall -Wextra -Werror tests/test_heatmap_csv.cpp \
//     -o .test-bin/heatmap_csv && .test-bin/heatmap_csv

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ─── Arduino String stand-in ────────────────────────────────────────────────
// Only the surface heatmap_csv.h actually uses, and no fixed buffer: a
// truncating mock cannot detect a truncation bug.
class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}

  size_t      length() const { return s_.size(); }
  const char* c_str()  const { return s_.c_str(); }
  void        reserve(size_t n) { s_.reserve(n); }

  String& operator+=(const char* s)   { s_ += s;    return *this; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }

  const std::string& str() const { return s_; }

 private:
  std::string s_;
};

#include "../src/heatmap_csv.h"

using Sample = heatmap::Sample<String>;

// ─── Assertions ─────────────────────────────────────────────────────────────
static int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      ++g_failures;                                                       \
      std::printf("    FAIL line %d: %s\n", __LINE__, #cond);             \
    }                                                                     \
  } while (0)

#define CHECK_EQ(actual, expected)                                        \
  do {                                                                    \
    const std::string a_ = (actual);                                      \
    const std::string e_ = (expected);                                    \
    if (a_ != e_) {                                                       \
      ++g_failures;                                                       \
      std::printf("    FAIL line %d:\n      expected [%s]\n      actual   [%s]\n", \
                  __LINE__, e_.c_str(), a_.c_str());                      \
    }                                                                     \
  } while (0)

// ─── An independent RFC 4180 reader ────────────────────────────────────────
// Written from the spec rather than from the encoder: sharing a helper between
// the two would make the round-trip prove nothing.
static std::vector<std::vector<std::string>> parse_csv(const std::string& text) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool quoted = false;
  bool field_started_quoted = false;

  for (size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < text.size() && text[i + 1] == '"') {
          field += '"';  // an escaped quote
          i++;
        } else {
          quoted = false;  // closing quote
        }
      } else {
        field += c;
      }
      continue;
    }
    if (c == '"' && field.empty() && !field_started_quoted) {
      quoted = true;
      field_started_quoted = true;
    } else if (c == ',') {
      row.push_back(field);
      field.clear();
      field_started_quoted = false;
    } else if (c == '\n') {
      row.push_back(field);
      field.clear();
      field_started_quoted = false;
      rows.push_back(row);
      row.clear();
    } else if (c == '\r') {
      // CRLF line ending: the LF closes the record.
    } else {
      field += c;
    }
  }
  if (!field.empty() || !row.empty()) {
    row.push_back(field);
    rows.push_back(row);
  }
  return rows;
}

// ─── Fixtures ───────────────────────────────────────────────────────────────
static Sample one_row_sample(const char* tag, const char* ssid,
                             const char* bssid, unsigned long timestamp) {
  Sample sample;
  sample.tag = tag;
  sample.timestamp = timestamp;
  sample.rowCount = 1;
  sample.rows[0].ssid = ssid;
  sample.rows[0].bssid = bssid;
  sample.rows[0].channel = 6;
  sample.rows[0].rssi = -84;
  return sample;
}

static std::string csv_of(const Sample* samples, size_t count, unsigned long now) {
  return heatmap::build_csv<String>(samples, count, now).str();
}

// ─── Tests ──────────────────────────────────────────────────────────────────
static void test_header_names_the_seven_columns() {
  const std::string csv = csv_of(nullptr, 0, 1000);
  CHECK_EQ(csv, "tag,boot_ms,age_s,ssid,bssid,channel,rssi\n");

  const auto rows = parse_csv(csv);
  CHECK(rows.size() == 1);
  CHECK(rows[0].size() == 7);
  // epoch_ms was a lie: the value is millis() since boot, not Unix time.
  CHECK_EQ(rows[0][1], "boot_ms");
}

static void test_plain_row_round_trips() {
  const Sample sample = one_row_sample("porch", "HouseNet", "AA:BB:CC:DD:EE:01", 100000UL);
  const auto rows = parse_csv(csv_of(&sample, 1, 107500UL));

  CHECK(rows.size() == 2);
  CHECK(rows[1].size() == 7);
  CHECK_EQ(rows[1][0], "porch");
  CHECK_EQ(rows[1][1], "100000");
  CHECK_EQ(rows[1][2], "7");  // 7500 ms of age, truncated to whole seconds
  CHECK_EQ(rows[1][3], "HouseNet");
  CHECK_EQ(rows[1][4], "AA:BB:CC:DD:EE:01");
  CHECK_EQ(rows[1][5], "6");
  CHECK_EQ(rows[1][6], "-84");
}

// The blocker this file exists for: a comma in an SSID used to shift every
// later column on the row. Businesses really do advertise a phone number and a
// suite as their SSID; this fixture keeps that shape using the reserved 555
// range rather than a number captured off the air.
static void test_comma_in_ssid_keeps_the_column_count() {
  const Sample sample = one_row_sample("spot", "Call (555) 010-1234, Suite B",
                                       "AA:BB:CC:DD:EE:02", 5000UL);
  const std::string csv = csv_of(&sample, 1, 5000UL);
  const auto rows = parse_csv(csv);

  CHECK(rows.size() == 2);
  CHECK(rows[1].size() == 7);
  CHECK_EQ(rows[1][3], "Call (555) 010-1234, Suite B");
  CHECK_EQ(rows[1][6], "-84");
}

static void test_quotes_are_doubled_not_backslashed() {
  const Sample sample = one_row_sample("spot", "The \"Free\" WiFi",
                                       "AA:BB:CC:DD:EE:03", 0UL);
  const std::string csv = csv_of(&sample, 1, 0UL);

  // The old encoder emitted \" here, which no CSV parser accepts.
  CHECK(csv.find('\\') == std::string::npos);
  CHECK(csv.find("\"\"Free\"\"") != std::string::npos);

  const auto rows = parse_csv(csv);
  CHECK(rows.size() == 2);
  CHECK(rows[1].size() == 7);
  CHECK_EQ(rows[1][3], "The \"Free\" WiFi");
}

static void test_newline_in_ssid_does_not_add_a_record() {
  const Sample sample = one_row_sample("spot", "line1\nline2\r\nline3",
                                       "AA:BB:CC:DD:EE:04", 0UL);
  const auto rows = parse_csv(csv_of(&sample, 1, 0UL));

  CHECK(rows.size() == 2);  // header + exactly one data record
  CHECK(rows[1].size() == 7);
  // RFC 4180 2.6: a line break inside a quoted field is data. The bytes come
  // back exactly as they went in, CR included.
  CHECK_EQ(rows[1][3], "line1\nline2\r\nline3");
}

static void test_tag_is_escaped_as_well_as_ssid() {
  Sample sample = one_row_sample("bay 3, \"north\"", "HouseNet",
                                 "AA:BB:CC:DD:EE:05", 0UL);
  const auto rows = parse_csv(csv_of(&sample, 1, 0UL));

  CHECK(rows.size() == 2);
  CHECK(rows[1].size() == 7);
  CHECK_EQ(rows[1][0], "bay 3, \"north\"");
}

// A hostile SSID must not become a live formula when the export is opened in
// a spreadsheet.
static void test_formula_triggers_are_defused() {
  const char* hostile[] = {
    "=HYPERLINK(\"http://evil\",\"click\")",
    "+1234",
    "@SUM(A1:A9)",
    "-2+3",
  };
  for (const char* ssid : hostile) {
    const Sample sample = one_row_sample("spot", ssid, "AA:BB:CC:DD:EE:06", 0UL);
    const auto rows = parse_csv(csv_of(&sample, 1, 0UL));

    CHECK(rows.size() == 2);
    CHECK(rows[1].size() == 7);
    const std::string& cell = rows[1][3];
    CHECK(!cell.empty());
    CHECK_EQ(cell.substr(0, 1), "'");             // guarded
    CHECK_EQ(cell.substr(1), std::string(ssid));  // and otherwise intact
  }
}

// The guard applies to text fields only. RSSI is negative on every real
// reading and must keep its minus sign.
static void test_numeric_columns_are_not_guarded() {
  Sample sample = one_row_sample("spot", "HouseNet", "AA:BB:CC:DD:EE:07", 0UL);
  sample.rows[0].rssi = -21;
  const auto rows = parse_csv(csv_of(&sample, 1, 0UL));

  CHECK(rows.size() == 2);
  CHECK_EQ(rows[1][6], "-21");
  CHECK(rows[1][6].find('\'') == std::string::npos);
}

static void test_hidden_ssid_becomes_an_empty_field() {
  const Sample sample = one_row_sample("spot", "", "AA:BB:CC:DD:EE:08", 0UL);
  const auto rows = parse_csv(csv_of(&sample, 1, 0UL));

  CHECK(rows.size() == 2);
  CHECK(rows[1].size() == 7);
  CHECK_EQ(rows[1][3], "");
  CHECK_EQ(rows[1][4], "AA:BB:CC:DD:EE:08");
}

static void test_age_is_correct_across_millis_rollover() {
  // timestamp near the top of the 32-bit millis() range, `now` past the wrap.
  const unsigned long before_wrap = 0xFFFFF000UL;
  const unsigned long now = before_wrap + 9000UL;  // wraps
  const Sample sample = one_row_sample("spot", "HouseNet", "AA:BB:CC:DD:EE:09",
                                       before_wrap);
  const auto rows = parse_csv(csv_of(&sample, 1, now));

  CHECK(rows.size() == 2);
  CHECK_EQ(rows[1][2], "9");
}

// The 128-byte mock String in the old test could not have caught this.
static void test_a_full_buffer_exports_every_row_untruncated() {
  Sample samples[heatmap::MAX_SAMPLES];
  for (size_t i = 0; i < heatmap::MAX_SAMPLES; i++) {
    char tag[32];
    std::snprintf(tag, sizeof(tag), "spot-%02zu", i);
    samples[i].tag = tag;
    samples[i].timestamp = (unsigned long)(i * 1000);
    samples[i].rowCount = (uint8_t)heatmap::MAX_ROWS_PER_SAMPLE;
    for (size_t r = 0; r < heatmap::MAX_ROWS_PER_SAMPLE; r++) {
      char ssid[64];
      char bssid[32];
      std::snprintf(ssid, sizeof(ssid), "Network with a long name %02zu-%02zu", i, r);
      std::snprintf(bssid, sizeof(bssid), "AA:BB:CC:%02zX:%02zX:01", i, r);
      samples[i].rows[r].ssid = ssid;
      samples[i].rows[r].bssid = bssid;
      samples[i].rows[r].channel = (uint8_t)(r + 1);
      samples[i].rows[r].rssi = -40 - (int32_t)r;
    }
  }

  const auto rows = parse_csv(csv_of(samples, heatmap::MAX_SAMPLES, 20000UL));
  CHECK(rows.size() == 1 + heatmap::MAX_SAMPLES * heatmap::MAX_ROWS_PER_SAMPLE);
  for (size_t i = 1; i < rows.size(); i++) {
    CHECK(rows[i].size() == 7);
  }
  // Last row intact end to end, which is what truncation would break first.
  const auto& last = rows.back();
  CHECK_EQ(last[3], "Network with a long name 15-05");
  CHECK_EQ(last[6], "-45");
}

static void test_rowcount_bounds_what_is_emitted() {
  Sample sample;
  sample.tag = "spot";
  sample.timestamp = 0;
  sample.rowCount = 2;  // only two of the six slots are live
  for (size_t r = 0; r < heatmap::MAX_ROWS_PER_SAMPLE; r++) {
    sample.rows[r].ssid = "Seeded";
    sample.rows[r].bssid = "AA:BB:CC:DD:EE:FF";
  }
  const auto rows = parse_csv(csv_of(&sample, 1, 0UL));
  CHECK(rows.size() == 3);  // header + 2
}

int main() {
  struct { const char* name; void (*fn)(); } tests[] = {
    { "header names the seven columns",        test_header_names_the_seven_columns },
    { "plain row round-trips",                 test_plain_row_round_trips },
    { "comma in SSID keeps column count",      test_comma_in_ssid_keeps_the_column_count },
    { "quotes doubled, never backslashed",     test_quotes_are_doubled_not_backslashed },
    { "newline in SSID adds no record",        test_newline_in_ssid_does_not_add_a_record },
    { "tag is escaped too",                    test_tag_is_escaped_as_well_as_ssid },
    { "formula triggers are defused",          test_formula_triggers_are_defused },
    { "numeric columns are not guarded",       test_numeric_columns_are_not_guarded },
    { "hidden SSID is an empty field",         test_hidden_ssid_becomes_an_empty_field },
    { "age is correct across rollover",        test_age_is_correct_across_millis_rollover },
    { "full buffer exports untruncated",       test_a_full_buffer_exports_every_row_untruncated },
    { "rowCount bounds the emitted rows",      test_rowcount_bounds_what_is_emitted },
  };

  const int total = (int)(sizeof(tests) / sizeof(tests[0]));
  for (const auto& t : tests) {
    const int before = g_failures;
    t.fn();
    std::printf("  %s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
  }

  if (g_failures) {
    std::printf("heatmap csv: %d assertion(s) failed across %d tests\n",
                g_failures, total);
    return 1;
  }
  std::printf("heatmap csv: %d tests passed\n", total);
  return 0;
}
