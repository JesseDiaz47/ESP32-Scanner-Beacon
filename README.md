# ESP32 Scanner Beacon

**A local Wi-Fi survey, BLE discovery tool, and iBeacon — one ESP32, one browser.**

![ESP32 Scanner Beacon: actual phone UI and channel view, shown with synthetic demo data](docs/images/cover.png)

Connect your phone to `jesse-scanner`, open `http://192.168.4.1`, and inspect nearby radio activity without a cloud account or a companion app. The board hosts the interface; your phone is the display.

**Platform:** classic ESP32 / ESP32-WROOM-32 · Arduino · PlatformIO  
**Status:** experimental, pre-release. The heatmap and CSV path has [known correctness issues](docs/RELEASE_CHECKLIST.md) that must be resolved before a public release.

## See it in action

These are **screenshots of the real embedded web interface**, rendered in Chromium with synthetic API fixtures. Network names, device addresses, signal levels, and location tags are demonstration data — not observations from a real survey. The screenshots demonstrate the interface, not end-to-end hardware validation.

<table>
<tr>
<td width="50%" valign="top">
<h3>01 · Nearby networks</h3>
<p>Sort nearby networks by signal strength. See the SSID, channel, encryption indicator, and RSSI in one view.</p>
<a href="docs/images/networks.png"><img src="docs/images/networks.png" alt="Networks tab showing synthetic SSIDs sorted strongest first" width="100%"></a>
</td>
<td width="50%" valign="top">
<h3>02 · Channel analyzer</h3>
<p>See where observed networks cluster. Bar height and color represent network count, not measured airtime or interference.</p>
<a href="docs/images/channels.png"><img src="docs/images/channels.png" alt="Channel analyzer showing synthetic network counts on 2.4 GHz channels" width="100%"></a>
</td>
</tr>
<tr>
<td width="50%" valign="top">
<h3>03 · Bluetooth discovery</h3>
<p>Request a five-second passive BLE scan. View advertised names, addresses, RSSI, and manufacturer IDs. The iBeacon pauses during discovery.</p>
<a href="docs/images/bluetooth.png"><img src="docs/images/bluetooth.png" alt="Bluetooth tab showing six fictional BLE advertisers with signal levels and manufacturer IDs" width="100%"></a>
</td>
<td width="50%" valign="top">
<h3>04 · Walk-around signal log</h3>
<p>Tag a spot and collect up to six network readings per snapshot. Inspect the log and use the CSV export control. Experimental; see the release checklist.</p>
<a href="docs/images/heatmap.png"><img src="docs/images/heatmap.png" alt="Heatmap tab showing synthetic Studio and Porch signal samples, tagging controls, and CSV download" width="100%"></a>
</td>
</tr>
</table>

[Full phone screenshot](docs/images/phone.png) · [Screenshot provenance and reproduction](docs/SHOWCASE.md)

## What the board does

| Capability | Implementation |
| --- | --- |
| Wi-Fi survey | Up to 64 results, including hidden SSIDs; displayed strongest-first. Background sweeps are scheduled at roughly 10-second intervals when the radio is available. The page polls every 3 seconds. |
| Channel view | Counts observed networks in channel bins 1–14. Available channels depend on the chip's regulatory configuration; this is not a spectrum analyzer. |
| BLE discovery | On-demand, five-second **passive BLE** scan, with up to 32 stored devices. A manufacturer ID identifies an advertised field, not a verified product identity. |
| iBeacon | Non-connectable advertising between BLE discovery sessions. UUID, major, minor, and advertised measured-power byte are configured in `src/main.cpp`. |
| Signal log | Up to 16 tagged snapshots × 6 network rows, stored in RAM. Oldest snapshots roll off; rebooting clears the log. |
| OTA support | Password-configured ArduinoOTA with two app partitions. An actual OTA transfer still needs release verification. |
| Browser UI | Self-contained HTML/CSS/JavaScript served from firmware. No external fonts, JavaScript libraries, or cloud APIs. |

### One radio, coordinated jobs

The classic ESP32 shares its 2.4 GHz radio between Wi-Fi and BLE. Channel sweeps can briefly interrupt the access point. The UI retains the previous table during failed polls. BLE discovery waits for the current Wi-Fi sweep, pauses iBeacon advertising, and resumes it afterward. OTA is designed to suspend surveys during an update.

The board is **not radio-silent**: it runs an access point and advertises an iBeacon. Wi-Fi scans currently use Arduino's active-scan default; only BLE discovery explicitly selects passive scanning. There is no deauthentication, jamming, or raw-frame injection feature.

## Hardware

- A classic ESP32 development board with **4 MB flash**, such as an ESP32-WROOM-32 DevKit.
- A USB data cable for the initial flash and a suitable USB power supply for use.
- A phone, tablet, or laptop with Wi-Fi and a browser.

No external display or additional sensor is required. The configured target is `esp32dev`; ESP32-S2/S3/C3 and other variants are not claimed as tested targets.

| Pin | Purpose |
| --- | --- |
| GPIO 2 | Wi-Fi scan heartbeat: approximately 120 ms when a sweep completes. Onboard LED availability/polarity depends on the board. |
| GPIO 4 | Optional BLE status output: heartbeat while advertising, solid during discovery. Use an appropriate resistor if attaching an external LED. |

## Quick start

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) or the PlatformIO extension for VS Code.

```sh
git clone https://github.com/JesseDiaz47/esp32-scanner-beacon.git
cd esp32-scanner-beacon

# Build for the configured classic ESP32 target.
pio run -e esp32dev

# Connect the intended board over USB, then upload.
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

If more than one serial device is attached, inspect `pio device list` and add `--upload-port YOUR_PORT` to the upload command. Flashing replaces the firmware on the selected board.

1. Power the board.
2. Join the open Wi-Fi network **`jesse-scanner`**.
3. If your phone warns that the network has no internet, choose to stay connected.
4. Open **http://192.168.4.1** in the browser. Use HTTP, not HTTPS.
5. Start with **Networks**, then try **Channels** or **Bluetooth**.

The access point is not an internet connection. It does not automatically open a captive-portal window. Keep the pinned `espressif32@6.8.1` platform and `min_spiffs.csv` partition layout unless you are deliberately validating a toolchain or partition change.

### Optional: password-configured OTA

The firmware builds without a secret header; OTA is disabled when `OTA_PASSWORD` is not defined.

```sh
cp include/secrets.example.h include/secrets.h
```

Edit the local `include/secrets.h` and replace `change-me` with a strong, unique, non-empty password **before flashing**. The file is ignored by Git. Optional `WIFI_STA_SSID` and `WIFI_STA_PASS` definitions let the board join a trusted Wi-Fi network too.

After an initial USB flash with the OTA configuration, join the board's access point and set the shell's `OTA_PASSWORD` environment variable to the same value, without recording it in shell history. Then run:

```sh
pio run -e ota -t upload
```

The default OTA target is `192.168.4.1`. For an explicitly configured station connection, use `--upload-port BOARD_LAN_IP`. The OTA password does **not** protect the browser interface and is not equivalent to encrypted transport or signed firmware. Never publish a firmware binary built with your personal credentials embedded in it.

## Boundaries worth knowing

- **2.4 GHz Wi-Fi only.** No 5 GHz or 6 GHz survey.
- **Signal readings, not distance.** RSSI varies with antennas, orientation, people, and walls. It is not a calibrated range measurement.
- **A tagged log, not a floor-plan heatmap.** There is no GPS, map interpolation, or coverage overlay.
- **Network count, not channel utilization.** The channel view cannot measure throughput, airtime, noise, or non-Wi-Fi interference.
- **Volatile storage.** Download useful readings before rebooting. CSV correctness and BSSID capture require the fixes listed in the [release checklist](docs/RELEASE_CHECKLIST.md).
- **An open local interface.** Anyone who can reach the HTTP server can read results, trigger scans, and clear the log. Do not expose it to an untrusted LAN or the internet. “Local” does not mean authenticated.
- **Use responsibly.** Survey only where you have permission, follow local radio/privacy rules, and avoid publishing other people's SSIDs, BLE addresses, or location history.

## Development and checks

```sh
# Source-contract checks (not hardware integration tests).
python3 -m unittest discover -s tests -p 'test_*.py' -v

# Host-side tests of the real radio coordinator.
mkdir -p .test-bin
c++ -std=c++11 -Wall -Wextra -Werror -Isrc \
  tests/test_radio_coordinator.cpp -o .test-bin/radio
.test-bin/radio

# Firmware compilation.
pio run -e esp32dev
```

For real-browser presentation checks and screenshot regeneration, follow [docs/SHOWCASE.md](docs/SHOWCASE.md). The older `tests/test_heatmap_handlers.cpp` copies handler logic and prints a success message without validating the complete CSV; it is **not** an acceptance test for the firmware export.

```text
src/main.cpp                Radio setup, surveys, HTTP handlers, OTA
src/radio_coordinator.h      Host-testable radio state machine
src/ui_page.h                The actual embedded browser interface
include/secrets.example.h    Safe configuration template
platformio.ini              Pinned toolchain and flash partitions
tests/                      Source contracts, radio tests, browser checks
docs/images/                Feature screenshots and cover artwork
tools/capture_showcase.py    Reproducible synthetic-data screenshot harness
```

## Release status and license

This is a pre-release project, not yet a validated public release. Read the [release checklist](docs/RELEASE_CHECKLIST.md) for correctness issues, verification gaps, and the distinction between browser fixtures and board testing.

A distribution license has not yet been selected. No open-source license is implied by this README.
