import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "main.cpp").read_text()
UI_PATH = ROOT / "src" / "ui_page.h"
UI_SOURCE = UI_PATH.read_text() if UI_PATH.exists() else ""
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
        match = re.search(r"constexpr\s+size_t\s+MAX_HEATMAP_SAMPLES\s*=\s*(\d+);", SOURCE)
        self.assertIsNotNone(match)
        assert match is not None
        self.assertLessEqual(int(match.group(1)), 128)

    def test_heatmap_sample_stores_bssid_channel_rssi_timestamp(self):
        self.assertIn("struct HeatmapSample", SOURCE)
        self.assertIn("String", SOURCE)
        self.assertIn("HeatmapRow     rows[", SOURCE)
        self.assertIn("uint8_t", SOURCE)
        self.assertIn("struct HeatmapRow", SOURCE)
        self.assertIn("bssid", SOURCE)
        self.assertIn("channel", SOURCE)
        self.assertIn("rssi", SOURCE)
        self.assertIn("timestamp", SOURCE)
        self.assertIn("rowCount", SOURCE)
        self.assertIn("tag", SOURCE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
