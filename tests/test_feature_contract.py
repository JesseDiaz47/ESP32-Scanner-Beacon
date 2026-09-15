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


class FirmwareFeatureContract(unittest.TestCase):
    def test_phone_ui_has_wifi_channels_and_ble_views(self):
        html = embedded_html()
        for name in ("wifi", "channels", "ble"):
            self.assertIn(f'data-tab="{name}"', html)
            self.assertIn(f'id="panel-{name}"', html)

    def test_channel_analyzer_renders_every_24ghz_channel(self):
        html = embedded_html()
        self.assertIn('id="channelBars"', html)
        self.assertRegex(
            html,
            r"for\s*\(var channel\s*=\s*1;\s*channel\s*<=\s*14;\s*channel\+\+\)",
        )
        self.assertIn("strongest", html.lower())

    def test_ble_scan_is_user_triggered_and_pollable(self):
        html = embedded_html()
        self.assertIn('id="bleScanButton"', html)
        self.assertIn("fetch('/ble/scan', { method: 'POST' })", html)
        self.assertIn("fetch('/ble.json'", html)
        self.assertIn('server.on("/ble/scan", HTTP_POST, handle_ble_scan);', SOURCE)
        self.assertIn('server.on("/ble.json", HTTP_GET, handle_ble_json);', SOURCE)

    def test_ble_results_are_bounded_for_this_non_psram_board(self):
        match = re.search(r"constexpr size_t MAX_BLE_ENTRIES\s*=\s*(\d+);", SOURCE)
        self.assertIsNotNone(match)
        assert match is not None
        self.assertLessEqual(int(match.group(1)), 40)

    def test_radio_coordinator_controls_scan_scheduling(self):
        self.assertIn('#include "radio_coordinator.h"', SOURCE)
        self.assertIn('#include "ui_page.h"', SOURCE)
        self.assertIn("RadioCoordinator g_radio;", SOURCE)
        self.assertIn("g_radio.wifiScanAllowed()", SOURCE)

    def test_active_attack_primitives_are_absent(self):
        forbidden = (
            "esp_wifi_80211_tx",
            "ieee80211_raw_frame_sanity_check",
            "deauth_frame",
            "jammer",
        )
        lowered = ALL_SOURCE.lower()
        for token in forbidden:
            self.assertNotIn(token, lowered)


if __name__ == "__main__":
    unittest.main(verbosity=2)
