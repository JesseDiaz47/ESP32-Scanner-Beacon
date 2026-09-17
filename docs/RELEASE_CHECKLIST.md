# Public-release checklist

Review date: 2026-09-15. Hardware pass added 2026-09-16. Correctness blockers
fixed 2026-09-16. This is a pre-release checklist, not a security certification.

**Recommendation: the repository is ready to be made public.** The four
correctness blockers are fixed and covered by host tests that fail against the
previous implementation; the project is Apache-2.0 licensed; the published
capture carries pseudonymised identifier columns, including the BLE addresses
that do not rotate on their own; and `SECURITY.md` states the trusted
environment and what the open AP exposes.

The device acceptance pass below is **not** a release gate. The README calls the
project experimental and pre-release, and this checklist is published alongside
it, so what has and has not been exercised on hardware is a matter of record
rather than a hidden claim. Those items gate calling the tool *finished*, not
publishing it. Visibility is the owner's to change.

> **The board now runs current `main`.** Flashed over USB on 2026-09-16 at
> commit `5ededa8` (1,652,320 bytes written, hash verified, hard reset). The
> revision skew that invalidated the earlier partial pass is closed: the
> firmware under test is the firmware in the repo. Everything still unticked
> below was simply not exercised yet.

## Correctness blockers

- [x] **CSV encoding repaired and tested through the production path.**
      `src/heatmap_csv.h` now holds the encoder and the heatmap storage types,
      and it implements RFC 4180: a field containing a comma, a double quote,
      CR or LF is wrapped in quotes and embedded quotes are doubled. Untrusted
      SSIDs and tags that begin with `= + - @ TAB CR` are prefixed with an
      apostrophe so a spreadsheet treats them as text; numeric columns are not
      guarded, so a `-84` dBm reading keeps its minus sign.
      `tests/test_heatmap_csv.cpp` compiles that same header and reads the
      output back with a separately written RFC 4180 parser: 12 tests, including
      a comma-bearing SSID of the shape that actually occurs on the air (a
      business advertising a phone number, written with a reserved 555 number),
      quotes, embedded newlines, `millis()` rollover, a hidden SSID, and a full
      16 x 6 buffer. Re-run against the previous encoder, the suite reports 18
      failed assertions — it is a real regression test, not a restatement.
- [x] **BSSID is preserved before the scan results are deleted.**
      `ScanEntry` gained a `bssid` field, filled inside the scan-completion loop
      while `WiFi.BSSIDstr(i)` is still valid; `WiFi.scanDelete()` now runs
      strictly after every field is copied out, and
      `capture_heatmap_snapshot()` reads the cached value. Pinned by
      `test_bssid_is_copied_before_the_scan_result_is_freed`, which asserts the
      source order and that the snapshot never calls `WiFi.BSSIDstr` itself.
- [x] **A tag press now captures a fresh sweep, and says so accurately.**
      `g_scanCompletedCount` is a sweep generation counter;
      `schedule_heatmap_snapshot()` records `g_scanCompletedCount + 1` and the
      capture waits for it, so a press is always answered by a sweep that
      completed after the press rather than by whatever sat in `g_entries` (up
      to `SCAN_INTERVAL_MS` stale). A press with no sweep in flight starts one.
      A press that cannot be served is dropped after
      `HEATMAP_PENDING_TIMEOUT_MS` rather than stranded. The handler now
      switches on a `SnapshotRequest` result and returns `202 accepted:true`,
      `409` for OTA or BLE contention, or `429` with `retry_after_ms` for
      cooldown — exactly one branch may claim success, which the contract test
      asserts. The phone UI reads the flag and reports the board's reason
      instead of always printing "Tagged".
- [x] **The misleading heatmap test is gone.**
      `tests/test_heatmap_handlers.cpp` was deleted. It kept a private copy of
      the encoder, used a 128-byte `String` that silently truncated, and its two
      called functions only printed output — while its two functions with real
      assertions were defined *after* `main()` and never ran at all (one of
      those assertions was also simply wrong). Its replacement compiles the
      shipped header and asserts on parsed output.

## Security and disclosure decisions

- [x] **Root license chosen and added.** Apache License 2.0, selected by the
      owner. `LICENSE` holds the canonical text (201 lines, including
      `END OF TERMS AND CONDITIONS` and the appendix, with the appendix's
      `[yyyy] [name of copyright owner]` placeholder left verbatim as the
      license intends). `NOTICE` carries the actual copyright, which is the
      convention Apache 2.0 expects redistributors to preserve. Re-read
      `gh repo view --json licenseInfo` after the publication commit to confirm
      GitHub detects it.
- [x] **The real capture is published with pseudonymised identifiers.**
      `docs/data/` and the screenshots carry a real capture: real sweep counts,
      channel distribution, RSSI spread, hidden-network count, BLE company IDs
      and BLE addresses, all exactly as observed. What is no longer published is
      the human-readable identifier columns. The Wi-Fi `ssid` column and the BLE
      `name` column are replaced with stable pseudonyms (`AP-01`..`AP-35`,
      `Device-1`..`Device-4`), so the same network keeps the same label across
      all six sweeps and the data stays internally consistent.
      The reason: an SSID set is a location fingerprint even without GPS —
      matchable against public wardriving databases — and the captured set
      included the owner's own surname on the strongest access point, several
      neighbours' surnames, and a business phone number. It was also
      third-party data. One BLE device name carried the owner's initials.
      Mapping tables were not committed. `docs/data/capture-manifest.json`
      records the substitution and its file hashes were recomputed.
      **The BLE `address` column is now handled too.** Classified by the two
      most significant bits per Core spec Vol 6 Part B 1.3: 18 of 28 are
      resolvable or non-resolvable private addresses, which rotate on their own
      and identify nothing, and those are published exactly as captured. The
      other 10 are static-random or public — the firmware does not record the
      advertisement's address-type flag, so the two cannot be told apart — and
      each is replaced with a deterministic stand-in that preserves its address
      class, keeping the published distribution truthful. Four of the ten were
      the named devices, one of which was the owner's own wearable. An earlier
      pass mis-classified these using the Ethernet locally-administered bit,
      which does not apply to BLE; the corrected count is recorded here because
      the README's "overwhelmingly rotating" claim depends on it.

- [x] **Initial reachable-history pattern review completed.** All 5 reachable
      commits and 16 unique text blobs (118,621 bytes) were reviewed; the object
      totals were independently reconciled against Git. Findings were
      configuration values, example placeholders, synthetic test identifiers,
      and commit identity metadata; no live credential was confirmed. No secret
      header or binary asset appeared in those reachable trees. This is a
      heuristic review, not a guarantee: unreachable/hidden refs and ignored
      local files were out of scope. Repeat the check against the exact
      publication commit.
- [x] **The open AP and unauthenticated HTTP boundary is documented.**
      `SECURITY.md` states the trusted environment the tool is built for — one
      person, holding the board, surveying premises they are responsible for,
      for the length of a walk — and tabulates every endpoint with what a
      stranger in radio range can do by calling it. The realistic worst case is
      named rather than glossed: read the survey, occupy the radio, or erase an
      unexported log. It records that station mode widens that exposure to a
      whole network and is off by default, that OTA is the one authenticated
      path and why its password is a compile-time error, and what the tool
      deliberately cannot do (no traffic capture, no injection, no outbound
      request). It closes with the four changes to make before deploying more
      widely, `/heatmap/clear` being the one that destroys work.
- [x] **Weak OTA configuration is rejected.** `setup_ota()` fails the *build*
      rather than warning at runtime: `static_assert` rejects the `change-me`
      placeholder from `include/secrets.example.h` and any password shorter than
      8 characters. Verified three ways by building a clean copy of the source —
      the verbatim example header fails with the placeholder message, a 5-
      character password fails with the length message, and a 13-character
      password builds. Commenting the `#define` out still builds with OTA
      disabled. `include/secrets.example.h` documents all of this.
- [ ] **Do not publish personal firmware binaries.** Secrets compiled into a
      binary are still disclosed even if the header is ignored by Git.

## Accuracy and usability follow-ups

- [x] Correct the subtitle: Wi-Fi uses active-scan defaults; the project is not
      a passive-only radio tool.
- [x] Correct the channel caption: both bar height and color are based on
      network count, not RSSI.
- [x] Size the tag input to 16 px text and a 44 px minimum height to avoid iOS
      focus zoom and improve touch use.
- [x] Clarify the CSV time column: the header is now `boot_ms`, not `epoch_ms`,
      because the value is `millis()` since power-on. Nothing on this board
      knows wall-clock time. `tools/capture_showcase.py` emits the same header.
- [x] **Heatmap metrics now mean what they say.** "Samples" counts distinct
      snapshots rather than the flattened row list, and "Unique APs"
      deduplicates BSSIDs rather than SSIDs — a mesh or extender puts one SSID
      on several radios, and every hidden network shared a single name bucket.
      Neither was fixable before this round, because `/heatmap.json` carried no
      BSSID; it now emits `bssid` and a `sample` id per row. The status line
      distinguishes samples from readings. Pinned by
      `test_unique_ap_count_deduplicates_radios_not_names`.
- [x] **"Live" and the iBeacon text now reflect the actual radio.** `/scan.json`
      reports `radio` (`ready` / `ble-pending` / `ble-scan` / `ota`), `sweeping`
      and `advertising`, and the badge is driven by that poll: it shows
      `Sweeping`, `BLE scan`, `BLE queued`, `Updating`, or `No signal` after
      three consecutive missed polls. Previously it printed `Live` whenever a
      BLE scan was not running, which was equally true of a stalled poll and of
      a board that had stopped answering. Advertising is no longer assumed
      either: every start/stop goes through `start_advertising()` /
      `stop_advertising()`, which record what the radio was actually commanded
      to do, and the UI reports that flag instead of asserting the beacon "is
      broadcasting again". Pinned by `test_status_badge_reports_real_radio_state`
      and `test_advertising_state_is_tracked_not_assumed`.

## Required device acceptance pass

Partially executed 2026-09-16. Boot bring-up has been re-verified against
current `main` after the reflash; the other ticked items were exercised on the
prior build in code paths this round did not change (Wi-Fi sweeps, BLE
discovery, `MAX_ENTRIES`). Every unticked item below still needs a real run,
and all of them can now be run directly against the repo's firmware.

- [x] **Board boots and brings up every subsystem — re-verified on current
      `main`.** Hard reset over DTR/RTS after the 2026-09-16 flash; serial
      logged the AP (`SSID=jesse-scanner IP=192.168.4.1`), the 30-byte iBeacon
      payload within the 31-byte cap and byte-identical to the predicted frame
      (`0201061AFF4C000215B9407F30F5F8466EAFF925565B57FE6D00010001C5`), passive
      BLE discovery ready, `[ota] ready, password set` — which is the new
      compile-time password guard surviving a real build and boot — the HTTP
      server listening, and three sweeps at 14/15/17 networks. Lower counts
      than the earlier pass's 24-30 from a different spot and time of day; the
      sweep-to-sweep variance the README documents.
- [x] **A client joined the AP and the server served the page.** Laptop
      associated, took `192.168.4.2` by DHCP, `GET /` returned `200` and 22,281
      bytes. The served body was diffed against `src/ui_page.h` — this is how
      the revision skew was found.
- [x] **Multiple Wi-Fi sweeps observed.** Serial logged 24 and 21 networks; six
      HTTP-sampled sweeps returned 28, 30, 26, 27, 30 and 23. Sweep-to-sweep
      variance from one fixed spot is real and is documented in the README.
- [x] **`MAX_ENTRIES = 64` confirmed adequate.** Highest single sweep was 30,
      leaving the headroom the 32 -> 64 change was made for.
- [x] **BLE discovery ran to completion.** `POST /ble/scan` accepted,
      `/ble.json` returned `scanning:false` with 28 devices after the 5 s
      window, and the iBeacon resumed.
- [ ] Load the exact flashed UI from a **phone** joined to the board's AP.
      *(Laptop only so far, and against a much older literal.)*
- [ ] Verify the UI retains/replaces results sensibly across sweeps from the
      browser, not just the API.
- [ ] Observe iBeacon advertising resume **from a second device** rather than
      from the board's own state.
- [ ] Tag two known spots; verify freshness, RSSI, channel, **a populated BSSID
      column**, retained history, and that a refused press now reports its
      reason in the UI (tag twice inside the 2 s cooldown, and once during a
      BLE scan).
- [ ] Download CSV **from the board** and parse it, including controlled
      comma/quote/newline SSIDs or tags. The host tests cover the encoder; only
      the board proves the transfer.
- [ ] Verify RAM rollover, clear/cancel behavior, and the documented reboot loss
      of the log.
- [ ] Perform one authenticated OTA transfer and an interrupted/error-path test,
      with USB recovery available.

## Executed host evidence

All commands below were run unpiped on 2026-09-16 after the fixes.

| Check | Result | What it establishes |
| --- | --- | --- |
| Python source contracts | 19 passed | Expected source/interface contracts exist, including BSSID copy order, single-success-branch reporting, the shared encoder, the OTA guards, real radio-state reporting, tracked advertising state, and BSSID-based AP counting. Not live behavior. |
| `tests/test_heatmap_csv.cpp` | 12 passed under both `-std=c++11` and `-std=c++17`, `-Wall -Wextra -Werror` | The shipped RFC 4180 encoder, round-tripped through an independently written parser. |
| Same suite vs. the pre-fix encoder | 18 assertions failed | The tests actually detect the bugs they describe. |
| Real `RadioCoordinator` C++ tests | 4 passed with `-Wall -Wextra -Werror` | Host-side lifecycle and OTA preemption state behavior. |
| `tests/browser_ui.py` | Passed | Corrected labels, channel count/color, phone input sizing, and 12 tab/viewport combinations in Chromium. |
| `tools/capture_showcase.py` (synthetic and `--fixtures live-fixtures.json`) | Passed, no browser errors | Real rows through the real UI literal, including the rewritten tag/log path and the corrected metrics. Its heatmap assertions were updated to the new semantics (snapshots, not rows; BSSIDs, not SSIDs) and the fixtures gained `sample` and synthetic `bssid` fields, without which the corrected metrics have nothing to count. |
| Configured firmware build | Passed | 70,220 bytes RAM (21.4%); 1,643,065 bytes flash (83.6%) in the selected app slot. The BSSID cache costs ~1 KB RAM. |
| Clean copied source without `include/secrets.h` | Passed | 1,615,925 bytes flash, OTA disabled. A new user can compile without personal credentials. |
| Clean copy with the verbatim example header | Build failed as designed | The placeholder OTA password cannot reach a board. |
| Clean copy with a 5-character password | Build failed as designed | The 8-character minimum is enforced. |
| Clean copy with a 13-character password | Passed | A real password builds. |

`tools/capture_live.py` remains the only harness that pulls from a real board,
and it exercises the JSON handlers. The CSV download performed by the screenshot
harness is generated by the Python fixture server and is **not** a test of the
firmware's exporter; `tests/test_heatmap_csv.cpp` is.

## Publication sequence

1. ~~Fix and test the blockers~~ — done. ~~Choose the license~~ — Apache-2.0.
2. ~~Decide the real-capture disclosure~~ — identifier columns pseudonymised,
   non-rotating BLE addresses included. ~~Write down the open-AP boundary~~ —
   `SECURITY.md`.
3. ~~Reflash the board~~ — done, current `main`. Complete the device acceptance
   pass when there is time on the board; it is not a gate on publishing.
4. Review the actual diff and generated media; keep credentials and built
   firmware out of the commit.
5. Push and change visibility only with explicit approval.
6. Read back the repository visibility and inspect the rendered README from a
   signed-out browser, including every image.
