"""Capture the real embedded UI with explicitly synthetic API fixtures.

No board, nearby network, secret header, or external service is read.
The browser harness verifies UI presentation only, not ESP32 handler behavior.
Run: python3 tools/capture_showcase.py
"""
from __future__ import annotations

import csv
import hashlib
import io
import json
import math
import re
import time
from collections import Counter
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
MEDIA = ROOT / "docs/images"
SHOWCASE = ROOT / "docs/showcase"


def main():
    source = (ROOT / "src/ui_page.h").read_text()
    match = re.search(r'R"rawliteral\((.*?)\)rawliteral";', source, re.S)
    assert match is not None, "Cannot find the firmware UI literal"
    html = match.group(1)
    fixtures = json.loads((SHOWCASE / "fixtures.json").read_text())
    MEDIA.mkdir(parents=True, exist_ok=True)
    records = []
    errors = []
    requests = []
    state = {"scan_until": 0.0, "heatmap": list(fixtures["heatmap"])}

    def serve(route):
        url = urlparse(route.request.url)
        requests.append((route.request.method, url.path))
        if url.hostname != "scanner.test":
            raise AssertionError(f"Unexpected external request: {url.hostname}")
        if url.path == "/":
            route.fulfill(body=html, content_type="text/html")
        elif url.path == "/scan.json":
            route.fulfill(json={"entries": fixtures["wifi"]})
        elif url.path == "/ble.json":
            route.fulfill(json={"scanning": time.monotonic() < state["scan_until"], "entries": fixtures["ble"]})
        elif url.path == "/ble/scan" and route.request.method == "POST":
            state["scan_until"] = time.monotonic() + 0.35
            route.fulfill(status=202, json={"accepted": True, "scanning": True})
        elif url.path == "/heatmap.json":
            route.fulfill(json={"entries": state["heatmap"]})
        elif url.path == "/heatmap/scan" and route.request.method == "POST":
            tag = parse_qs(url.query).get("tag", ["spot"])[0]
            rows = [{"tag": tag, "age": 0, **{key: row[key] for key in ("ssid", "channel", "rssi")}} for row in fixtures["wifi"][:6]]
            state["heatmap"] = (rows + state["heatmap"])[:96]
            route.fulfill(status=202, json={"accepted": True, "tag": tag})
        elif url.path == "/heatmap/clear" and route.request.method == "POST":
            state["heatmap"] = []
            route.fulfill(json={"cleared": True})
        elif url.path == "/heatmap.csv":
            # This is fixture output, deliberately NOT a test of firmware CSV.
            text = io.StringIO()
            writer = csv.writer(text)
            writer.writerow(["tag", "epoch_ms", "age_s", "ssid", "bssid", "channel", "rssi"])
            for row in state["heatmap"]:
                writer.writerow([row["tag"], 100000 - row["age"] * 1000, row["age"], row["ssid"], "", row["channel"], row["rssi"]])
            route.fulfill(body=text.getvalue(), content_type="text/csv", headers={"Content-Disposition": 'attachment; filename="demo-heatmap.csv"'})
        else:
            route.fulfill(status=404, body="Fixture route not found")

    with sync_playwright() as p:
        browser = p.chromium.launch()
        browser_version = browser.version
        context = browser.new_context(viewport={"width": 780, "height": 960}, device_scale_factor=2)
        context.route("**/*", serve)
        page = context.new_page()
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.on("console", lambda message: errors.append(message.text) if message.type == "error" else None)
        page.goto("http://scanner.test/")
        page.wait_for_function("document.getElementById('count').textContent === '12'")
        assert page.locator("#strongest").inner_text() == str(max(row["rssi"] for row in fixtures["wifi"]))
        assert page.locator("#busyChannel").inner_text() == str(Counter(row["channel"] for row in fixtures["wifi"]).most_common(1)[0][0])
        assert page.locator("#rows tr").count() == len(fixtures["wifi"])
        # Visible annotation is outside .shell. Product layout/data are untouched.
        page.evaluate("""() => {
          const notice = document.createElement('div');
          notice.id = 'screenshot-notice';
          notice.textContent = 'SCREENSHOT DEMO  /  SYNTHETIC RADIO DATA';
          notice.style.cssText = 'max-width:760px;margin:0 auto 14px;padding:9px 10px;border:1px solid #29444f;border-radius:7px;color:#8faeb9;background:#0d1c24;font:10px/1.4 ui-monospace,monospace;letter-spacing:.07em;text-align:center';
          document.body.prepend(notice);
        }""")

        for name, filename in (("wifi", "networks.png"), ("channels", "channels.png"), ("ble", "bluetooth.png"), ("heatmap", "heatmap.png")):
            page.locator(f'[data-tab="{name}"]').click()
            if name == "ble":
                page.wait_for_function("document.querySelectorAll('#bleRows tr').length === 6")
                page.locator("#bleScanButton").click()
                page.wait_for_function("document.getElementById('radioState').textContent === 'BLE scan'")
                page.wait_for_function("!document.getElementById('bleScanButton').disabled")
                assert ("POST", "/ble/scan") in requests
            if name == "heatmap":
                page.wait_for_function("document.getElementById('heatmapSamples').textContent === '12'")
                assert page.locator("#heatmapUnique").inner_text() == str(len({row["ssid"] for row in fixtures["heatmap"]}))
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
            bottom = page.locator('.shell').evaluate('el => el.getBoundingClientRect().bottom')
            height = math.ceil(bottom + 24)
            page.set_viewport_size({"width": 780, "height": height})
            page.screenshot(path=str(MEDIA / filename), animations="disabled")
            records.append({"file": filename, "viewport": [780, height], "pixels": [1560, height * 2], "tab": name})

        # A phone view proves this is the same responsive app, not a desktop mock.
        page.set_viewport_size({"width": 390, "height": 844})
        page.locator('[data-tab="wifi"]').click()
        assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
        phone_height = max(844, math.ceil(page.locator(".shell").evaluate("el => el.getBoundingClientRect().bottom") + 24))
        page.set_viewport_size({"width": 390, "height": phone_height})
        page.screenshot(path=str(MEDIA / "phone.png"), animations="disabled")
        records.append({"file": "phone.png", "viewport": [390, phone_height], "pixels": [780, phone_height * 2], "tab": "wifi"})

        # Real DOM events, synthetic backend: don't mistake this for board QA.
        page.locator('[data-tab="heatmap"]').click()
        page.locator("#heatmapTag").fill("Bench")
        page.locator("#heatmapSnapshot").click()
        page.wait_for_function("document.getElementById('heatmapLatest').textContent === 'Bench'")
        assert len(state["heatmap"]) == 18
        with page.expect_download() as download_info:
            page.locator("#heatmapExport").click()
        download = download_info.value
        downloaded = download.path()
        assert downloaded is not None
        rows = list(csv.reader(Path(downloaded).read_text().splitlines()))
        assert len(rows) == 19 and all(len(row) == 7 for row in rows)
        page.once("dialog", lambda dialog: dialog.dismiss())
        page.locator("#heatmapClear").click()
        assert len(state["heatmap"]) == 18
        page.once("dialog", lambda dialog: dialog.accept())
        page.locator("#heatmapClear").click()
        page.wait_for_function("document.getElementById('heatmapSamples').textContent === '0'")
        assert state["heatmap"] == []
        assert errors == [], errors
        context.close()

        cover = SHOWCASE / "cover.html"
        if cover.exists():
            art = browser.new_page(viewport={"width": 1600, "height": 900}, device_scale_factor=1)
            art.goto(cover.as_uri())
            art.evaluate("document.fonts.ready")
            assert art.locator("img").evaluate_all("images => images.every(img => img.complete && img.naturalWidth > 0)")
            assert art.evaluate("document.documentElement.scrollWidth === innerWidth && document.documentElement.scrollHeight === innerHeight")
            art.screenshot(path=str(MEDIA / "cover.png"), animations="disabled")
            records.append({"file": "cover.png", "viewport": [1600, 900], "pixels": [1600, 900], "tab": "composition"})
            art.close()
        browser.close()

    for record in records:
        asset = MEDIA / record["file"]
        record["sha256"] = hashlib.sha256(asset.read_bytes()).hexdigest()
        record["bytes"] = asset.stat().st_size
    manifest = {
        "provenance": "Actual src/ui_page.h rendered by Chromium with synthetic API fixtures and a visible demo annotation. Not hardware verification.",
        "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
        "fixtures_sha256": hashlib.sha256((SHOWCASE / "fixtures.json").read_bytes()).hexdigest(),
        "browser_version": browser_version,
        "assets": records,
        "checks": ["tabs", "network sorting and counts", "BLE button and scan state", "tag and log UI", "CSV download UI with fixture output", "clear cancel and confirm", "no browser errors", "no screenshot overflow"],
    }
    (SHOWCASE / "capture-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"assets": len(records), "total_bytes": sum(item["bytes"] for item in records), "browser_errors": errors, "manifest": str(SHOWCASE / "capture-manifest.json")}, indent=2))


if __name__ == "__main__":
    main()
