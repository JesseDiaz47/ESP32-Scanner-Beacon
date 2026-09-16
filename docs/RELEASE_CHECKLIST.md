# Public-release checklist

Review date: 2026-09-15. Hardware pass added 2026-09-16. This is a pre-release checklist, not a security certification.

**Recommendation: keep the repository private until the correctness blockers below are resolved and a license is selected.** The Wi-Fi and Bluetooth screenshots now carry real data from a running board (see the hardware pass below). The walk-around log is still synthetic, and no OTA transfer has been performed.

> **Flashed firmware is one revision behind `main`.** The board serves a UI literal that predates the copy corrections in `48c4716`: subtitle still reads `Passive 2.4 GHz field survey`, the channel legend still reads `Bar height = AP count · color = strongest signal`, and the tag input is still 13 px with no `min-height`. The JSON endpoints are byte-identical between the two revisions, so the captured data in `docs/data/` is unaffected — but **reflash before any acceptance pass**, or the thing under test is not the thing in the repo.

## Correctness blockers

- [ ] **Repair CSV encoding and add a real parser round-trip test.** `handle_heatmap_csv()` uses JSON escaping for comma-separated fields. A comma creates extra columns; quotes are backslash-escaped rather than CSV-quoted. Also decide how to handle spreadsheet formulas in untrusted SSIDs/tags. Test the actual production encoding path, not a copied implementation.
- [ ] **Preserve BSSID before deleting scan results.** The main loop calls `WiFi.scanDelete()` before `capture_heatmap_snapshot()` reads `WiFi.BSSIDstr(src)`. Store each BSSID alongside its scan entry while the scan data still exists, then snapshot the cached value.
- [ ] **Make a tag request capture a fresh, accepted result.** With existing scan entries and no sweep in flight, scheduling currently does not start a fresh sweep. The handler returns `accepted:true` even when scheduling rejects a request because of cooldown or BLE activity. Make the API report rejection accurately and prove the snapshot corresponds to the intended sweep.
- [ ] **Replace misleading heatmap test success.** `tests/test_heatmap_handlers.cpp` duplicates handler code, uses a 128-byte mock `String`, and the two functions called by `main()` do not assert CSV correctness. Its printed “2 tests passed” is not acceptance evidence.

## Security and disclosure decisions

- [ ] **Choose and add a root license.** MIT is a reasonable option for a small reusable firmware project, but the owner must select it. No license has been added by assumption.
- [x] **Initial reachable-history pattern review completed.** All 5 reachable commits and 16 unique text blobs (118,621 bytes) were reviewed; the object totals were independently reconciled against Git. Findings were configuration values, example placeholders, synthetic test identifiers, and commit identity metadata; no live credential was confirmed. No secret header or binary asset appeared in those reachable trees. This is a heuristic review, not a guarantee: unreachable/hidden refs and ignored local files were out of scope. Repeat the check against the exact publication commit.
- [ ] **Decide on the open AP and unauthenticated HTTP boundary.** Reading results, triggering BLE/heatmap requests, and clearing the log do not require authentication. Optional station mode exposes that same server to the joined network. Document the intended trusted environment; consider AP protection before wider distribution.
- [ ] **Reject weak OTA configuration.** A missing `OTA_PASSWORD` disables OTA, but a defined empty/default `change-me` value is not rejected. Require a non-empty, non-placeholder value before enabling the service.
- [ ] **Do not publish personal firmware binaries.** Secrets compiled into a binary are still disclosed even if the header is ignored by Git.

## Accuracy and usability follow-ups

- [x] Correct the subtitle: Wi-Fi uses active-scan defaults; the project is not a passive-only radio tool.
- [x] Correct the channel caption: both bar height and color are based on network count, not RSSI.
- [x] Size the tag input to 16 px text and a 44 px minimum height to avoid iOS focus zoom and improve touch use.
- [ ] Review heatmap metric names: “Samples” counts network rows, while “Unique APs” currently deduplicates SSIDs, not BSSIDs. Distinct APs can share an SSID.
- [ ] Make “Live” / “iBeacon broadcasting again” reflect actual radio state and successful polls, rather than relying on optimistic UI text.
- [ ] Clarify the `epoch_ms` CSV header: its value is `millis()` since boot, not Unix epoch time.

## Required device acceptance pass

Partially executed 2026-09-16 against **firmware one revision behind `main`** (UI copy only; endpoints identical). Everything below that is still unticked was not performed.

- [x] **Board boots and brings up every subsystem.** Hard reset over DTR/RTS; serial logged the AP (`SSID=jesse-scanner IP=192.168.4.1`), a 30-byte iBeacon payload within the 31-byte cap, passive BLE discovery ready, OTA service started with a password set, and the HTTP server listening.
- [x] **A client joined the AP and the server served the page.** Laptop associated, took `192.168.4.2` by DHCP, `GET /` returned `200` and 22,281 bytes. The served body was diffed against `src/ui_page.h` — this is how the revision skew above was found.
- [x] **Multiple Wi-Fi sweeps observed.** Serial logged 24 and 21 networks; six HTTP-sampled sweeps returned 28, 30, 26, 27, 30 and 23. Sweep-to-sweep variance from one fixed spot is real and is now documented in the README.
- [x] **`MAX_ENTRIES = 64` confirmed adequate.** Highest single sweep was 30, leaving the headroom the 32 → 64 change was made for.
- [x] **BLE discovery ran to completion.** `POST /ble/scan` accepted, `/ble.json` returned `scanning:false` with 28 devices after the 5 s window, and the iBeacon resumed.
- [ ] Load the exact flashed UI from a **phone** joined to the board's AP. *(Laptop only so far, and against the older literal.)*
- [ ] Verify the UI retains/replaces results sensibly across sweeps from the browser, not just the API.
- [ ] Observe iBeacon advertising resume **from a second device** rather than from the board's own state.
- [ ] Tag two known spots; verify freshness, RSSI, channel, BSSID, retained history, and rejected requests.
- [ ] Download CSV and parse it, including controlled comma/quote/newline names and safe spreadsheet handling.
- [ ] Verify RAM rollover, clear/cancel behavior, and the documented reboot loss of the log.
- [ ] Perform one authenticated OTA transfer and an interrupted/error-path test, with USB recovery available.

## Executed host evidence

| Check | Result | What it establishes |
| --- | --- | --- |
| Python source contracts | 10 passed | Expected source/interface contracts exist; not live behavior. |
| Real `RadioCoordinator` C++ tests | 4 passed with `-Wall -Wextra -Werror` | Host-side lifecycle and OTA preemption state behavior. |
| `tests/browser_ui.py` | Passed | Corrected labels, channel count/color, phone input sizing, and 12 tab/viewport combinations in Chromium. |
| `tools/capture_showcase.py` | Passed, no browser errors | UI event wiring with synthetic APIs: tabs, BLE start/finish, tag/log, fixture CSV download, clear cancel/confirm. |
| Configured firmware build | Passed | 69,188 bytes RAM; 1,641,821 bytes flash, within the selected app slot. |
| `tools/capture_live.py` against a live board | Passed | Six real sweeps + one real BLE scan pulled over HTTP; wrote `docs/data/` and `live-fixtures.json`. Exercises the board's JSON handlers, not its CSV exporter. |
| `tools/capture_showcase.py --fixtures live-fixtures.json` | Passed, no browser errors | Real rows rendered through the real UI literal, annotated per tab by data source. |
| Clean copied source without `include/secrets.h` | Passed | 67,324 bytes RAM; 1,614,669 bytes flash. A new user can compile without personal credentials. |

The CSV download performed by the screenshot harness is generated by the Python fixture server. It is **not a test of `handle_heatmap_csv()`** and does not close the CSV blocker.

## Publication sequence

1. Fix and test the blockers; choose the license.
2. Complete the device acceptance pass, or explicitly narrow the release's supported features.
3. Review the actual diff and generated media; keep credentials and built firmware out of the commit.
4. Integrate the feature branch with the owner's approval. The feature implementation was ahead of GitHub `main` when this pass began.
5. Push and change visibility only with explicit approval.
6. Read back the repository visibility and inspect the rendered README from a signed-out browser, including every image.
