"""Capture a real survey from the board and write it to docs/data.

Unlike tools/capture_showcase.py, this contacts an actual ESP32 running this
firmware and records what its radio observed. Everything it writes is a real
measurement, including real SSIDs and real BLE addresses.

  python3 tools/capture_live.py --sweeps 6 --tags desk,kitchen,garage

The board's AP knocks itself off-channel for ~2s on every sweep, so every
request retries. /heatmap.csv is fetched from the firmware itself, which means
this also exercises the real C++ exporter rather than a Python stand-in.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import platform
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "docs/data"
SHOWCASE = ROOT / "docs/showcase"


def request(host: str, path: str, method: str = "GET", tries: int = 8):
    """One request, retried around the radio dropping the AP mid-sweep."""
    last = None
    for attempt in range(tries):
        try:
            req = urllib.request.Request(f"http://{host}{path}", method=method)
            with urllib.request.urlopen(req, timeout=12) as response:
                return response.read().decode("utf-8", "replace")
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            last = exc
            time.sleep(1.5 * (attempt + 1))
    raise SystemExit(f"{method} {path} failed after {tries} tries: {last}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.4.1")
    parser.add_argument("--sweeps", type=int, default=6,
                        help="Wi-Fi sweeps to record, ~11s apart")
    parser.add_argument("--tags", default="",
                        help="Comma-separated heatmap spots to tag, in walk order")
    parser.add_argument("--tag-wait", type=float, default=14.0,
                        help="Seconds to wait at each spot before tagging")
    args = parser.parse_args()

    DATA.mkdir(parents=True, exist_ok=True)
    started = dt.datetime.now(dt.timezone.utc)

    # ── Wi-Fi: several sweeps, so the record shows real RSSI drift ──────────
    sweeps = []
    for index in range(args.sweeps):
        if index:
            time.sleep(11)
        entries = json.loads(request(args.host, "/scan.json"))["entries"]
        sweeps.append(entries)
        print(f"[wifi] sweep {index + 1}/{args.sweeps}: {len(entries)} networks")

    with (DATA / "wifi-survey.csv").open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["sweep", "ssid", "hidden", "channel", "encrypted", "rssi"])
        for index, entries in enumerate(sweeps, 1):
            for entry in entries:
                writer.writerow([index, entry["ssid"], not entry["ssid"],
                                 entry["channel"], entry["encrypted"], entry["rssi"]])

    # Strongest observation per network, for the rendered screenshots.
    best: dict[tuple[str, int], dict] = {}
    for entries in sweeps:
        for entry in entries:
            key = (entry["ssid"], entry["channel"])
            if key not in best or entry["rssi"] > best[key]["rssi"]:
                best[key] = entry
    wifi = sorted(best.values(), key=lambda e: -e["rssi"])

    # ── BLE: one on-demand passive scan ─────────────────────────────────────
    request(args.host, "/ble/scan", method="POST")
    print("[ble] passive scan requested; waiting")
    time.sleep(9)
    ble_payload = json.loads(request(args.host, "/ble.json"))
    ble = sorted(ble_payload["entries"], key=lambda e: -e["rssi"])
    print(f"[ble] {len(ble)} devices")

    with (DATA / "ble-devices.csv").open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["address", "name", "company_id", "rssi"])
        for entry in ble:
            writer.writerow([entry["address"], entry["name"],
                             entry["company"], entry["rssi"]])

    # ── Heatmap: tag each spot, then pull the firmware's own CSV ────────────
    tags = [tag.strip() for tag in args.tags.split(",") if tag.strip()]
    for tag in tags:
        print(f"[heatmap] move to '{tag}' — tagging in {args.tag_wait:.0f}s")
        time.sleep(args.tag_wait)
        request(args.host, "/heatmap/scan?" + urllib.parse.urlencode({"tag": tag}),
                method="POST")
        time.sleep(3)
        print(f"[heatmap] tagged '{tag}'")

    heatmap = json.loads(request(args.host, "/heatmap.json"))["entries"] if tags else []
    heatmap_csv = request(args.host, "/heatmap.csv") if tags else ""
    if heatmap_csv:
        (DATA / "heatmap.csv").write_text(heatmap_csv)

    # No walk was performed. Carry the synthetic rows through unchanged rather
    # than shipping an empty tab, and keep them labelled as the mock they are.
    heatmap_is_real = bool(tags)
    if heatmap_is_real:
        print(f"[heatmap] {len(heatmap)} real rows across {len({r['tag'] for r in heatmap})} spots")
    else:
        heatmap = json.loads((SHOWCASE / "fixtures.json").read_text())["heatmap"]
        print(f"[heatmap] no --tags given; carrying {len(heatmap)} MOCK rows through")

    # ── Fixture file, same shape capture_showcase.py already consumes ───────
    fixtures = {
        "notice": "Wi-Fi and BLE are a REAL capture from an ESP32 running this "
                  "firmware: real SSIDs, real BLE addresses, real RSSI. The "
                  + ("heatmap rows are real too." if heatmap_is_real else
                     "heatmap rows are MOCK demo data — no walk was performed."),
        "wifi_real": True,
        "ble_real": True,
        "heatmap_real": heatmap_is_real,
        "captured_utc": started.isoformat(timespec="seconds"),
        "wifi": [{k: e[k] for k in ("ssid", "rssi", "channel", "encrypted")} for e in wifi],
        "ble": [{k: e[k] for k in ("name", "address", "rssi", "company")} for e in ble],
        "heatmap": [{k: r[k] for k in ("tag", "age", "ssid", "channel", "rssi")} for r in heatmap],
    }
    SHOWCASE.mkdir(parents=True, exist_ok=True)
    (SHOWCASE / "live-fixtures.json").write_text(json.dumps(fixtures, indent=2) + "\n")

    channels = Counter(e["channel"] for e in wifi)
    manifest = {
        "provenance": "Live capture from an ESP32-WROOM-32 running this firmware. "
                      "Real radio observations, not synthetic fixtures.",
        "captured_utc": started.isoformat(timespec="seconds"),
        "host": args.host,
        "captured_by": f"{platform.system()} {platform.release()}",
        "wifi": {
            "sweeps": len(sweeps),
            "per_sweep_counts": [len(s) for s in sweeps],
            "unique_networks": len(wifi),
            "hidden": sum(1 for e in wifi if not e["ssid"]),
            "open": sum(1 for e in wifi if not e["encrypted"]),
            "strongest_rssi": max(e["rssi"] for e in wifi),
            "weakest_rssi": min(e["rssi"] for e in wifi),
            "busiest_channel": channels.most_common(1)[0][0],
            "channel_counts": dict(sorted(channels.items())),
        },
        "ble": {
            "devices": len(ble),
            "named": sum(1 for e in ble if e["name"]),
            "with_company_id": sum(1 for e in ble if e["company"] >= 0),
            "company_counts": dict(Counter(e["company"] for e in ble).most_common()),
            "strongest_rssi": max((e["rssi"] for e in ble), default=None),
        },
        "heatmap": {
            "real": heatmap_is_real,
            "note": None if heatmap_is_real else
                    "MOCK rows carried over from fixtures.json; no walk performed.",
            "tags": sorted({r["tag"] for r in heatmap}),
            "rows": len(heatmap),
            "csv_bytes": len(heatmap_csv),
            "csv_sha256": hashlib.sha256(heatmap_csv.encode()).hexdigest() if heatmap_csv else None,
        },
        "files": {},
    }
    for name in ("wifi-survey.csv", "ble-devices.csv", "heatmap.csv"):
        path = DATA / name
        if path.exists():
            manifest["files"][name] = {
                "bytes": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
    (DATA / "capture-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"[done] wrote {DATA.relative_to(ROOT)}/ and {(SHOWCASE / 'live-fixtures.json').relative_to(ROOT)}")


if __name__ == "__main__":
    main()
