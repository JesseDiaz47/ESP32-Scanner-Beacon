import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "main.cpp").read_text()
UI_PATH = ROOT / "src" / "ui_page.h"
UI_SOURCE = UI_PATH.read_text() if UI_PATH.exists() else ""
# The heatmap storage types and the CSV encoder live here so that
# tests/test_heatmap_csv.cpp compiles the shipped code instead of a copy.
CSV_SOURCE = (ROOT / "src" / "heatmap_csv.h").read_text()
ALL_SOURCE = SOURCE + "\n" + UI_SOURCE


def embedded_html() -> str:
    match = re.search(
        r'const char INDEX_HTML\[\] PROGMEM = R"rawliteral\((.*?)\)rawliteral";',
        UI_SOURCE,
        re.DOTALL,
    )
    if not match:
        raise AssertionError("INDEX_HTML raw literal was not found in src/ui_page.h")
    return match.group(1)


class HeatmapContract(unittest.TestCase):
    def test_phone_ui_has_heatmap_tab(self):
        html = embedded_html()
        self.assertIn('data-tab="heatmap"', html)
        self.assertIn('id="panel-heatmap"', html)

    def test_heatmap_endpoint_is_pollable(self):
        self.assertIn('server.on("/heatmap.json"', SOURCE)
        self.assertIn('server.on("/heatmap/scan"', SOURCE)
        self.assertIn('server.on("/heatmap.csv"', SOURCE)
        self.assertIn('server.on("/heatmap/clear"', SOURCE)
        html = embedded_html()
        self.assertIn("fetch('/heatmap.json'", html)
        self.assertIn("/heatmap/scan", html)
        self.assertIn("/heatmap.csv", html)
        self.assertIn("/heatmap/clear", html)

    def test_heatmap_buffer_has_bounded_storage(self):
        match = re.search(r"constexpr\s+size_t\s+MAX_SAMPLES\s*=\s*(\d+);", CSV_SOURCE)
        self.assertIsNotNone(match)
        assert match is not None
        self.assertLessEqual(int(match.group(1)), 128)
        self.assertIn("MAX_HEATMAP_SAMPLES         = heatmap::MAX_SAMPLES", SOURCE)

    def test_heatmap_sample_stores_bssid_channel_rssi_timestamp(self):
        self.assertIn("struct Row", CSV_SOURCE)
        self.assertIn("struct Sample", CSV_SOURCE)
        for field in ("ssid", "bssid", "channel", "rssi", "timestamp", "rowCount", "tag"):
            self.assertIn(field, CSV_SOURCE)
        self.assertIn("Row<Str>      rows[MAX_ROWS_PER_SAMPLE];", CSV_SOURCE)
        # The firmware uses those same definitions rather than its own.
        self.assertIn("using HeatmapRow    = heatmap::Row<String>;", SOURCE)
        self.assertIn("using HeatmapSample = heatmap::Sample<String>;", SOURCE)

    def test_bssid_is_copied_before_the_scan_result_is_freed(self):
        """WiFi.BSSIDstr(i) is only valid until WiFi.scanDelete(), and the
        heatmap snapshot runs long after the sweep completes. The copy must
        therefore happen inside the completion loop, above the delete."""
        copy_at = SOURCE.index("g_entries[i].bssid     = WiFi.BSSIDstr(i);")
        delete_at = SOURCE.index("WiFi.scanDelete();  // every field above")
        self.assertLess(copy_at, delete_at)

        snapshot = SOURCE[SOURCE.index("void capture_heatmap_snapshot(const String& tag) {") :]
        snapshot = snapshot[: snapshot.index("\n}\n")]
        self.assertIn("sample.rows[i].bssid   = g_entries[src].bssid;", snapshot)
        # Comments in that function explain why WiFi.BSSIDstr() is wrong here,
        # so assert against code only.
        code = "\n".join(
            line for line in snapshot.splitlines() if not line.lstrip().startswith("//")
        )
        self.assertNotIn("WiFi.BSSIDstr", code)

    def test_csv_export_goes_through_the_tested_encoder(self):
        export = SOURCE[SOURCE.index("void handle_heatmap_csv() {") :]
        export = export[: export.index("\n}\n")]
        self.assertIn("heatmap::build_csv<String>", export)
        # No second encoder to drift from, and no JSON escaping in a .csv.
        self.assertNotIn("json_escape", export)
        self.assertNotIn("epoch_ms", SOURCE)
        self.assertIn('CSV_HEADER[] = "tag,boot_ms,age_s,ssid,bssid,channel,rssi', CSV_SOURCE)

    def test_csv_quotes_fields_and_defuses_spreadsheet_formulas(self):
        self.assertIn("must_quote", CSV_SOURCE)
        self.assertIn("triggers_formula", CSV_SOURCE)
        # RFC 4180 doubles an embedded quote; it never backslash-escapes one.
        self.assertIn("if (text[i] == '\"') append_char(out, '\"');", CSV_SOURCE)
        self.assertNotIn("\\\\\"", CSV_SOURCE)

    def test_a_tag_press_waits_for_a_sweep_that_started_after_it(self):
        self.assertIn("g_scanCompletedCount++", SOURCE)
        self.assertIn("g_heatmapPendingAfterScan = g_scanCompletedCount + 1;", SOURCE)
        self.assertIn("g_scanCompletedCount >= g_heatmapPendingAfterScan", SOURCE)

    def test_a_refused_snapshot_is_reported_as_refused(self):
        self.assertIn("enum class SnapshotRequest", SOURCE)
        for state in ("Scheduled", "RejectedOta", "RejectedBleBusy", "RejectedCooldown"):
            self.assertIn(state, SOURCE)
        self.assertIn("SnapshotRequest schedule_heatmap_snapshot", SOURCE)

        handler = SOURCE[SOURCE.index("void handle_heatmap_scan() {") :]
        handler = handler[: handler.index("\n}\n")]
        self.assertIn("switch (schedule_heatmap_snapshot(tag))", handler)
        self.assertIn("retry_after_ms", handler)
        # Exactly one branch may claim success.
        self.assertEqual(handler.count('\\"accepted\\":true'), 1)
        self.assertEqual(handler.count('\\"accepted\\":false'), 3)

    def test_ota_refuses_a_placeholder_or_short_password(self):
        self.assertIn('static_assert(!ota_password_equals(OTA_PASSWORD, "change-me")', SOURCE)
        self.assertIn("static_assert(ota_password_length(OTA_PASSWORD) >= 8", SOURCE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
