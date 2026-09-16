"""Browser checks for the actual embedded UI; no ESP32 or radio is contacted.

Run: python3 tests/browser_ui.py
Requires: playwright + its Chromium browser. All API responses below are fixtures.
"""
import re
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]


def run():
    source = (ROOT / "src/ui_page.h").read_text()
    match = re.search(r'R"rawliteral\((.*?)\)rawliteral";', source, re.S)
    assert match is not None, "Embedded UI literal missing"
    html = match.group(1)
    errors = []
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 390, "height": 844})
        page.on("pageerror", lambda error: errors.append(str(error)))

        def serve(route):
            path = route.request.url.split("scanner.test", 1)[-1]
            if path == "/":
                route.fulfill(body=html, content_type="text/html")
            elif path == "/scan.json":
                route.fulfill(json={"entries": [
                    {"ssid": "Demo Lab", "channel": 6, "rssi": -42, "encrypted": True},
                    {"ssid": "Demo Guest", "channel": 6, "rssi": -69, "encrypted": False},
                ]})
            elif path == "/heatmap.json":
                route.fulfill(json={"entries": []})
            else:
                route.fulfill(json={"scanning": False, "entries": []})

        page.route("**/*", serve)
        page.goto("http://scanner.test/")
        page.wait_for_function("document.getElementById('count').textContent === '2'")
        assert page.locator(".subtitle").inner_text() == "Local 2.4 GHz field survey", (
            "WiFi scanNetworks(true, true) does not select passive scanning; don't claim it."
        )
        page.locator('[data-tab="channels"]').click()
        assert page.locator("#panel-channels .meta").inner_text() == "Bar height & color = network count", (
            "Channel color is based on count, not RSSI."
        )
        assert page.locator(".channel").count() == 14
        assert page.locator(".channel").nth(5).locator(".channel-count").inner_text() == "2"
        assert "warm" in (page.locator(".channel").nth(5).locator(".channel-bar").get_attribute("class") or "")
        page.locator('[data-tab="heatmap"]').click()
        size = page.locator("#heatmapTag").evaluate("el => parseFloat(getComputedStyle(el).fontSize)")
        assert size >= 16, f"Prevent iOS input-focus zoom; got {size}px"
        bounds = page.locator("#heatmapTag").bounding_box()
        assert bounds is not None
        height = bounds["height"]
        assert height >= 44, f"Tag input needs a usable touch target; got {height}px"
        for width in (390, 768, 1280):
            page.set_viewport_size({"width": width, "height": 844})
            for name in ("wifi", "channels", "ble", "heatmap"):
                page.locator(f'[data-tab="{name}"]').click()
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), (width, name)
                assert page.locator(".panel.active").count() == 1
        assert errors == [], errors
        browser.close()
    print("Browser UI: truthful labels, channel data, phone input, and 12 viewport/tab states passed.")


if __name__ == "__main__":
    run()
