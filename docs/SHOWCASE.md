# Showcase assets

## What the images show

Every screenshot is the actual `INDEX_HTML` literal from `src/ui_page.h`, opened in Chromium by `tools/capture_showcase.py`. Only the data behind it varies, and there are two sources:

| Fixture set | Used for | What the rows are |
| --- | --- | --- |
| `showcase/live-fixtures.json` | Networks, Channels, Bluetooth | **Real.** Written by `tools/capture_live.py` from an ESP32-WROOM-32 running this firmware. Real SSIDs, real BLE addresses, real RSSI. |
| `showcase/fixtures.json` | Walk-around log | **Synthetic.** No walk has been performed. |

The current images in `images/` were rendered from `live-fixtures.json`, so the first three tabs are real and the heatmap tab carries the synthetic rows forward.

- Each screenshot is annotated **per tab**, outside the application shell: `LIVE CAPTURE / REAL RADIO DATA FROM THE BOARD` with the capture timestamp, or `SCREENSHOT DEMO / SYNTHETIC RADIO DATA`. A real table and a mock table never sit under the same caption.
- The cover combines the phone screenshot and a labeled crop of the channel screenshot. It is not a hardware photograph.
- Viewport height is fitted to the complete UI; screenshots are normal browser captures, not stitched pages.
- `showcase/capture-manifest.json` records which fixture file was used, a `data_is_real` flag per section, the capture timestamp, source and fixture hashes, browser version, and the dimensions, sizes and hashes of the generated images.
- **Rendering the real UI is not hardware acceptance testing.** The browser harness drives the DOM against a fixture server; it does not exercise the ESP32's C++ handlers. The one exception is `/heatmap.csv`, which `capture_live.py --tags` fetches from the firmware itself.

## Capturing real data

The board must be powered and broadcasting. Join its AP (`jesse-scanner`, `http://192.168.4.1`), then:

```sh
python3 tools/capture_live.py --sweeps 6
python3 tools/capture_showcase.py --fixtures live-fixtures.json
```

`capture_live.py` retries every request, because a Wi-Fi sweep takes the radio off the AP's channel for roughly two seconds and will otherwise drop the connection mid-request.

Add `--tags desk,kitchen,garage` to perform a walk. The tool prompts at each spot, tags it, and then downloads `/heatmap.csv` from the firmware — which is also the only part of this harness that tests the real C++ exporter. Without `--tags`, the synthetic heatmap rows are carried through unchanged and stay labelled as mock.

## Files

| Asset | Purpose |
| --- | --- |
| `images/cover.png` | README lead image, 1600 × 900 |
| `images/networks.png` | Wi-Fi survey tab — real capture |
| `images/channels.png` | Channel-count analyzer — real capture |
| `images/bluetooth.png` | BLE discovery tab — real capture |
| `images/heatmap.png` | Tagged signal log and export controls — synthetic |
| `images/phone.png` | Networks tab at a 390 CSS-pixel phone width — real capture |
| `showcase/live-fixtures.json` | Real rows from the last board capture |
| `showcase/fixtures.json` | Synthetic rows, kept for the heatmap and for board-free rendering |
| `showcase/cover.html` | Editable fixed-canvas cover composition |
| `showcase/index.html` | Responsive local feature-gallery preview |
| `data/wifi-survey.csv` | Every Wi-Fi observation, one row per network per sweep |
| `data/ble-devices.csv` | Every BLE advertiser from the scan |
| `data/capture-manifest.json` | Capture provenance, counts and file hashes |

Open `docs/showcase/index.html` directly in a browser to review the presentation. GitHub will show that HTML as source; the README uses normal relative image links for its rendered gallery.

## Reproduce the render without a board

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r tools/requirements-showcase.txt
python3 -m playwright install chromium

python3 tests/browser_ui.py
python3 tools/capture_showcase.py                   # synthetic fixtures
```

The browser regression checks truthful copy, the count-based channel view, tag-input sizing, and tab layout at phone/tablet/desktop widths.

These optional tools are for development and documentation. They are not included in the firmware and do not change its memory footprint.

## Privacy note

`data/` and the Wi-Fi and Bluetooth screenshots contain a real, unredacted capture from one location. That is deliberate: a survey tool documented with invented numbers demonstrates nothing. It is a single snapshot with no location tags, no traffic, and no repeated readings, and the BLE addresses are overwhelmingly the rotating, randomised kind. Publishing a *walk* — repeated readings tied to named places — is a different decision and has not been made here.

Before publishing a capture of your own, confirm you are comfortable with the names it contains, and never capture the secret header, serial credentials, or a home station SSID you did not intend to show.
