# Showcase assets

## What the images show

The screenshots use the actual `INDEX_HTML` literal in `src/ui_page.h`. Playwright opens that HTML in Chromium and fulfills the firmware API requests with the explicit synthetic fixtures in `showcase/fixtures.json`.

- No ESP32 is contacted and no real radio scan is performed.
- No credentials, real SSIDs, or real device addresses are read.
- Every screenshot carries a visible **SCREENSHOT DEMO / SYNTHETIC RADIO DATA** annotation outside the application shell.
- The cover combines the phone screenshot and a labeled crop of the channel screenshot. It is not a hardware photograph.
- Viewport height is fitted to the complete UI; screenshots are normal browser captures, not stitched pages.
- `showcase/capture-manifest.json` records the exact source and fixture hashes, browser version, dimensions, sizes, and hashes of the generated images.
- Fixture download tests verify browser wiring only. The Python fixture CSV generator is not the C++ firmware exporter; passing it does not close the firmware CSV defects.

## Files

| Asset | Purpose |
| --- | --- |
| `images/cover.png` | README lead image, 1600 × 900 |
| `images/networks.png` | Wi-Fi survey tab |
| `images/channels.png` | Channel-count analyzer |
| `images/bluetooth.png` | BLE discovery tab |
| `images/heatmap.png` | Tagged signal log and export controls |
| `images/phone.png` | Networks tab at a 390 CSS-pixel phone width |
| `showcase/cover.html` | Editable fixed-canvas cover composition |
| `showcase/index.html` | Responsive local feature-gallery preview |

Open `docs/showcase/index.html` directly in a browser to review the presentation. GitHub will show that HTML as source; the README uses normal relative image links for its rendered gallery.

## Reproduce

From the repository root:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r tools/requirements-showcase.txt
python3 -m playwright install chromium

python3 tests/browser_ui.py
python3 tools/capture_showcase.py
```

The browser regression checks truthful copy, the count-based channel view, tag-input sizing, and tab layout at phone/tablet/desktop widths. The capture harness also drives BLE scan, tag/log, CSV-download, and clear/cancel UI events against its synthetic API.

These optional tools are for development and documentation. They are not included in the firmware and do not change its memory footprint.

## Before replacing these with hardware screenshots

1. Obtain permission to show any third-party network/device information, or use a controlled lab with intentionally public names and identifiers.
2. Do not capture the secret header, serial credentials, home station SSID, or real location tags.
3. Verify the exact flashed firmware and perform the workflow on the board.
4. Replace the synthetic-data caption only with a precise description of what was actually tested.
5. Recheck each image in the rendered README after any GitHub visibility change. Raw-file availability alone does not prove the README images rendered correctly.
