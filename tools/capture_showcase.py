"""Capture the real embedded UI against a chosen fixture set.

The browser harness verifies UI presentation only, not ESP32 handler behavior.

  python3 tools/capture_showcase.py                               # synthetic
  python3 tools/capture_showcase.py --fixtures live-fixtures.json # real capture

With the default fixtures no board, nearby network, secret header, or external
service is read. With live-fixtures.json the rows come from tools/capture_live.py,
which did read a real board; the per-section *_real flags in that file drive the
on-image annotation so a screenshot always states what produced its data.
"""
from __future__ import annotations

import argparse
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


# A real capture puts 39 networks in one table. Fitting the viewport to the
# whole shell then yields a ~3700px ribbon that reads as a sliver in the README
# gallery, so shots are capped — cut just under a row boundary, never mid-row.
MAX_SHOT_HEIGHT = 900
MAX_PHONE_HEIGHT = 844


def fitted_height(page, max_height):
    """Full shell height when it fits, otherwise the last row boundary above the cap."""
    bottom = page.locator(".shell").evaluate("el => el.getBoundingClientRect().bottom")
    full = math.ceil(bottom + 24)
    if full <= max_height:
        return full
    cut = page.evaluate("""limit => {
      let best = 0;
      for (const row of document.querySelectorAll('.shell tr')) {
        if (row.offsetParent === null) continue;          // hidden tab
        const edge = row.getBoundingClientRect().bottom;
        if (edge <= limit && edge > best) best = edge;
      }
      return best;
    }""", max_height)
    return math.ceil(cut) if cut else max_height


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixtures", default="fixtures.json",
                        help="Fixture file in docs/showcase. Use live-fixtures.json "
                             "for a real board capture.")
    args = parser.parse_args()
    fixtures_path = SHOWCASE / args.fixtures

    source = (ROOT / "src/ui_page.h").read_text()
    match = re.search(r'R"rawliteral\((.*?)\)rawliteral";', source, re.S)
    assert match is not None, "Cannot find the firmware UI literal"
    html = match.group(1)
    fixtures = json.loads(fixtures_path.read_text())
    # Absent flags mean the old all-synthetic fixture file.
    real = {section: bool(fixtures.get(f"{section}_real"))
            for section in ("wifi", "ble", "heatmap")}
    MEDIA.mkdir(parents=True, exist_ok=True)
    records = []
    errors = []
    requests = []
    # sample_seq continues past the static fixtures' ids so a tagged snapshot
    # is counted as a new sample rather than merging into an existing one.
    state = {"scan_until": 0.0, "heatmap": list(fixtures["heatmap"]),
             "sample_seq": max((row["sample"] for row in fixtures["heatmap"]), default=-1)}

    def demo_bssid(ssid: str) -> str:
        """Deterministic, obviously-synthetic BSSID for fixture rows. The 02:
        prefix marks a locally administered address, so it cannot collide with a
        real vendor OUI."""
        digest = hashlib.sha256(ssid.encode("utf-8")).hexdigest()
        return ":".join(["02"] + [digest[i:i + 2] for i in range(0, 10, 2)]).upper()

    def serve(route):
        url = urlparse(route.request.url)
        requests.append((route.request.method, url.path))
        if url.hostname != "scanner.test":
            raise AssertionError(f"Unexpected external request: {url.hostname}")
        if url.path == "/":
            route.fulfill(body=html, content_type="text/html")
        elif url.path == "/scan.json":
            scanning = time.monotonic() < state["scan_until"]
            route.fulfill(json={
                "entries": fixtures["wifi"],
                "radio": "ble-scan" if scanning else "ready",
                "sweeping": False,
                "advertising": not scanning,
                "sweeps": 6,
            })
        elif url.path == "/ble.json":
            scanning = time.monotonic() < state["scan_until"]
            route.fulfill(json={"scanning": scanning, "advertising": not scanning,
                                "entries": fixtures["ble"]})
        elif url.path == "/ble/scan" and route.request.method == "POST":
            state["scan_until"] = time.monotonic() + 0.35
            route.fulfill(status=202, json={"accepted": True, "scanning": True})
        elif url.path == "/heatmap.json":
            route.fulfill(json={"entries": state["heatmap"]})
        elif url.path == "/heatmap/scan" and route.request.method == "POST":
            tag = parse_qs(url.query).get("tag", ["spot"])[0]
            state["sample_seq"] += 1
            rows = [{"tag": tag, "sample": state["sample_seq"], "age": 0,
                     "bssid": demo_bssid(row["ssid"]),
                     **{key: row[key] for key in ("ssid", "channel", "rssi")}}
                    for row in fixtures["wifi"][:6]]
            state["heatmap"] = (rows + state["heatmap"])[:96]
            route.fulfill(status=202, json={"accepted": True, "tag": tag})
        elif url.path == "/heatmap/clear" and route.request.method == "POST":
            state["heatmap"] = []
            route.fulfill(json={"cleared": True})
        elif url.path == "/heatmap.csv":
            # This is fixture output, deliberately NOT a test of firmware CSV.
            text = io.StringIO()
            writer = csv.writer(text)
            writer.writerow(["tag", "boot_ms", "age_s", "ssid", "bssid", "channel", "rssi"])
            for row in state["heatmap"]:
                writer.writerow([row["tag"], 100000 - row["age"] * 1000, row["age"], row["ssid"],
                                 row.get("bssid", ""), row["channel"], row["rssi"]])
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
        page.wait_for_function(
            f"document.getElementById('count').textContent === '{len(fixtures['wifi'])}'")
        assert page.locator("#strongest").inner_text() == str(max(row["rssi"] for row in fixtures["wifi"]))
        assert page.locator("#busyChannel").inner_text() == str(Counter(row["channel"] for row in fixtures["wifi"]).most_common(1)[0][0])
        assert page.locator("#rows tr").count() == len(fixtures["wifi"])
        # Visible annotation is outside .shell. Product layout/data are untouched.
        # The text is per-tab: a real Wi-Fi table and a mock heatmap must not
        # sit under the same caption.
        captured = fixtures.get("captured_utc", "")
        def notice_for(tab):
            section = {"wifi": "wifi", "channels": "wifi",
                       "ble": "ble", "heatmap": "heatmap"}[tab]
            if real[section]:
                when = f"  /  {captured}" if captured else ""
                return f"LIVE CAPTURE  /  REAL RADIO DATA FROM THE BOARD{when}"
            return "SCREENSHOT DEMO  /  SYNTHETIC RADIO DATA"

        page.evaluate("""() => {
          const notice = document.createElement('div');
          notice.id = 'screenshot-notice';
          notice.style.cssText = 'max-width:760px;margin:0 auto 14px;padding:9px 10px;border:1px solid #29444f;border-radius:7px;color:#8faeb9;background:#0d1c24;font:10px/1.4 ui-monospace,monospace;letter-spacing:.07em;text-align:center';
          document.body.prepend(notice);
        }""")

        for name, filename in (("wifi", "networks.png"), ("channels", "channels.png"), ("ble", "bluetooth.png"), ("heatmap", "heatmap.png")):
            page.locator(f'[data-tab="{name}"]').click()
            page.evaluate("text => document.getElementById('screenshot-notice').textContent = text",
                          notice_for(name))
            if name == "ble":
                page.wait_for_function(
                    f"document.querySelectorAll('#bleRows tr').length === {len(fixtures['ble'])}")
                page.locator("#bleScanButton").click()
                page.wait_for_function("document.getElementById('radioState').textContent === 'BLE scan'")
                page.wait_for_function("!document.getElementById('bleScanButton').disabled")
                assert ("POST", "/ble/scan") in requests
            if name == "heatmap":
                # Samples counts snapshots, not rows; Unique APs counts BSSIDs,
                # not SSIDs. Both used to be read off the row list.
                snapshots = len({row["sample"] for row in fixtures["heatmap"]})
                radios = len({row["bssid"] for row in fixtures["heatmap"]})
                page.wait_for_function(
                    f"document.getElementById('heatmapSamples').textContent === '{snapshots}'")
                assert page.locator("#heatmapUnique").inner_text() == str(radios)
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
            height = fitted_height(page, MAX_SHOT_HEIGHT)
            page.set_viewport_size({"width": 780, "height": height})
            page.screenshot(path=str(MEDIA / filename), animations="disabled")
            records.append({"file": filename, "viewport": [780, height], "pixels": [1560, height * 2], "tab": name})

        # A phone view proves this is the same responsive app, not a desktop mock.
        page.set_viewport_size({"width": 390, "height": 844})
        page.locator('[data-tab="wifi"]').click()
        page.evaluate("text => document.getElementById('screenshot-notice').textContent = text",
                      notice_for("wifi"))
        assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
        phone_height = fitted_height(page, MAX_PHONE_HEIGHT)
        page.set_viewport_size({"width": 390, "height": phone_height})
        page.screenshot(path=str(MEDIA / "phone.png"), animations="disabled")
        records.append({"file": "phone.png", "viewport": [390, phone_height], "pixels": [780, phone_height * 2], "tab": "wifi"})

        # Real DOM events, synthetic backend: don't mistake this for board QA.
        page.locator('[data-tab="heatmap"]').click()
        page.locator("#heatmapTag").fill("Bench")
        page.locator("#heatmapSnapshot").click()
        page.wait_for_function("document.getElementById('heatmapLatest').textContent === 'Bench'")
        tagged_rows = len(fixtures["heatmap"]) + 6
        assert len(state["heatmap"]) == tagged_rows
        with page.expect_download() as download_info:
            page.locator("#heatmapExport").click()
        download = download_info.value
        downloaded = download.path()
        assert downloaded is not None
        rows = list(csv.reader(Path(downloaded).read_text().splitlines()))
        assert len(rows) == tagged_rows + 1 and all(len(row) == 7 for row in rows)
        page.once("dialog", lambda dialog: dialog.dismiss())
        page.locator("#heatmapClear").click()
        assert len(state["heatmap"]) == tagged_rows
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
        "provenance": "Actual src/ui_page.h rendered by Chromium against "
                      f"{args.fixtures}, with a per-tab annotation naming the data "
                      "source. Rendering the real UI is not hardware verification; "
                      "the rows are real only where real[] says so.",
        "fixtures_file": args.fixtures,
        "data_is_real": real,
        "captured_utc": fixtures.get("captured_utc"),
        "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
        "fixtures_sha256": hashlib.sha256(fixtures_path.read_bytes()).hexdigest(),
        "browser_version": browser_version,
        "assets": records,
        "checks": ["tabs", "network sorting and counts", "BLE button and scan state", "tag and log UI", "CSV download UI with fixture output", "clear cancel and confirm", "no browser errors", "no screenshot overflow"],
    }
    (SHOWCASE / "capture-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"assets": len(records), "total_bytes": sum(item["bytes"] for item in records), "browser_errors": errors, "manifest": str(SHOWCASE / "capture-manifest.json")}, indent=2))


if __name__ == "__main__":
    main()
