#pragma once
// RSSI-heatmap storage types and the CSV exporter.
//
// Both the firmware and the host tests include this file: src/main.cpp
// instantiates it with Arduino's String, tests/test_heatmap_csv.cpp with a
// std::string-backed stand-in. There is deliberately no second copy of the
// encoder for a test to drift from — a green test against a duplicated
// encoder is exactly what let a JSON-escaped "CSV" ship.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace heatmap {

// 16 samples x 6 APs = 96 rows, roughly 10 KB of heap once the String bodies
// are counted. This board has no PSRAM, so the bound is deliberate.
constexpr size_t MAX_SAMPLES = 16;
constexpr size_t MAX_ROWS_PER_SAMPLE = 6;

template <typename Str>
struct Row {
  Str     ssid;
  Str     bssid;
  uint8_t channel = 0;
  int32_t rssi    = 0;
};

template <typename Str>
struct Sample {
  Str           tag;
  unsigned long timestamp = 0;  // millis() at capture — not wall-clock time
  uint8_t       rowCount  = 0;
  Row<Str>      rows[MAX_ROWS_PER_SAMPLE];
};

// Appending a bare char is not portable across String implementations, so
// every character append goes through a NUL-terminated pair.
template <typename Str>
inline void append_char(Str& out, char c) {
  const char pair[2] = { c, '\0' };
  out += pair;
}

template <typename Str>
inline void append_ulong(Str& out, unsigned long value) {
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%lu", value);
  out += buffer;
}

template <typename Str>
inline void append_long(Str& out, long value) {
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%ld", value);
  out += buffer;
}

// A spreadsheet evaluates a cell whose text begins with one of these even when
// the field is correctly quoted. SSIDs come from strangers' routers and tags
// come from whatever is typed on the phone, so a leading trigger is defused
// with an apostrophe, which Excel, Sheets and LibreOffice all read as "the
// rest of this cell is literal text". Numeric columns are written by
// append_long() and never pass through here, so a -84 dBm reading keeps its
// minus sign.
inline bool triggers_formula(char c) {
  return c == '=' || c == '+' || c == '-' || c == '@' || c == '\t' || c == '\r';
}

// RFC 4180 2.5-2.7: a field carrying a comma, a double quote, CR or LF is
// wrapped in double quotes and every embedded quote is doubled. This is what
// the previous encoder got wrong — it backslash-escaped quotes, which no CSV
// parser accepts, and left commas bare, which silently shifts every later
// column on that row.
template <typename Str>
Str escape_field(const Str& input) {
  const char*  text   = input.c_str();
  const size_t length = input.length();

  const bool guard = length > 0 && triggers_formula(text[0]);
  bool must_quote = guard;
  for (size_t i = 0; i < length && !must_quote; i++) {
    const char c = text[i];
    must_quote = (c == ',' || c == '"' || c == '\n' || c == '\r');
  }

  Str out;
  if (!must_quote) {
    out += text;
    return out;
  }
  append_char(out, '"');
  if (guard) append_char(out, '\'');
  for (size_t i = 0; i < length; i++) {
    if (text[i] == '"') append_char(out, '"');  // RFC 4180: double it
    append_char(out, text[i]);
  }
  append_char(out, '"');
  return out;
}

// boot_ms, not epoch_ms: the value is millis() since power-on. Nothing on this
// board knows the wall-clock time.
constexpr char CSV_HEADER[] = "tag,boot_ms,age_s,ssid,bssid,channel,rssi\n";

// `now` is a parameter rather than a millis() call so the host tests can pin
// ages exactly. Unsigned subtraction keeps age correct across the ~49-day
// millis() rollover.
template <typename Str>
Str build_csv(const Sample<Str>* samples, size_t count, unsigned long now) {
  Str csv;
  csv.reserve(sizeof(CSV_HEADER) + count * MAX_ROWS_PER_SAMPLE * 80);
  csv += CSV_HEADER;
  for (size_t sampleIdx = 0; sampleIdx < count; sampleIdx++) {
    const Sample<Str>& sample = samples[sampleIdx];
    const unsigned long ageSeconds = (now - sample.timestamp) / 1000UL;
    const Str tag = escape_field(sample.tag);
    for (uint8_t rowIdx = 0; rowIdx < sample.rowCount; rowIdx++) {
      const Row<Str>& row = sample.rows[rowIdx];
      csv += tag;                                     append_char(csv, ',');
      append_ulong(csv, sample.timestamp);            append_char(csv, ',');
      append_ulong(csv, ageSeconds);                  append_char(csv, ',');
      csv += escape_field(row.ssid);                  append_char(csv, ',');
      csv += escape_field(row.bssid);                 append_char(csv, ',');
      append_ulong(csv, (unsigned long)row.channel);  append_char(csv, ',');
      append_long(csv, (long)row.rssi);
      append_char(csv, '\n');
    }
  }
  return csv;
}

}  // namespace heatmap
