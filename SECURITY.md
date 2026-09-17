# Security

This is a radio survey tool. It is deliberately passive, and it is deliberately
open on its own access point. Both of those are design decisions, and this file
states them plainly so you can decide whether they suit your situation.

## The trusted environment this is built for

**One person, holding the board, surveying premises they are responsible for,
for the length of a walk.** That is the whole intended threat model. The board
is powered on to take readings and powered off afterwards. It is not built to
be left running unattended, mounted in a public space, or joined to a network
you do not control.

Inside that environment the design is appropriate. Outside it, read the rest of
this file before deploying anything.

## What is exposed, and to whom

The board creates an **open Wi-Fi access point** named `jesse-scanner`
(`192.168.4.1`) with no passphrase, because typing one on a phone while holding
a board and reading a wall is friction in the only moment the tool is used.

Anyone in radio range can join that AP, and every HTTP endpoint is
**unauthenticated**:

| Endpoint | Method | Effect if a stranger calls it |
|---|---|---|
| `/` | GET | Serves the interface. |
| `/scan.json` | GET | Reads the current Wi-Fi sweep and radio state. |
| `/ble.json` | GET | Reads the last BLE discovery result. |
| `/ble/scan` | POST | **Starts a 5-second BLE scan**, pausing the iBeacon. |
| `/heatmap.json`, `/heatmap.csv` | GET | Reads or downloads the survey log. |
| `/heatmap/scan` | POST | **Captures a new snapshot** into the log. |
| `/heatmap/clear` | POST | **Erases the entire survey log.** |

So the realistic worst case from an unauthenticated caller is: they read your
survey, they make the radio busy, or they destroy readings you have not
exported yet. The log lives only in RAM and there is no persistence to corrupt,
no filesystem to write, and no credential stored on the board except the OTA
password.

**Optional station mode makes this worse and is off by default.** If you define
`WIFI_STA_SSID` / `WIFI_STA_PASS`, the same unauthenticated server becomes
reachable from every host on that network, not just from clients of the board's
own AP. Only enable it on a network you control.

## What is protected

**Firmware upload is the one thing that is authenticated.** OTA is the only
endpoint that can persistently change the board, so `OTA_PASSWORD` is enforced
at compile time: the build fails if it is still the `change-me` placeholder from
`include/secrets.example.h`, or if it is shorter than 8 characters. A board
cannot boot with a password that was never really set. To build with OTA
switched off entirely, comment the `#define` out.

Without that, an open AP plus an unauthenticated OTA service would let anyone in
radio range flash arbitrary firmware onto the board. That is why it is a build
error and not a warning.

## What this tool does not do

- **It does not capture traffic.** It reads what access points and BLE devices
  broadcast about themselves — SSID, BSSID, channel, RSSI, advertised name,
  manufacturer ID. No payloads, no packets, no contents.
- **It does not transmit anything but its own AP and its own iBeacon.** There is
  no deauthentication, no injection, no probe flooding, no jamming. BLE
  discovery is passive: it listens, it does not solicit scan responses and it
  does not connect.
- **It does not phone home.** There is no cloud service, no telemetry, no
  analytics and no outbound request of any kind. Data leaves the board only when
  you fetch it over the local AP.

## If you want to deploy it more widely

Do these before the environment above stops describing your situation:

1. **Put a passphrase on the AP** (`AP_PASS` in `src/main.cpp`), or gate the
   mutating endpoints behind a token. `/heatmap/clear` is the one that destroys
   work.
2. **Leave station mode off** unless you control the joined network.
3. **Never publish a built firmware binary.** `OTA_PASSWORD` and any station
   credentials are compiled into the image; the fact that `include/secrets.h` is
   gitignored does not help once the `.bin` is shared.
4. **Export before you reboot.** The survey log is RAM only and does not survive
   a reset.

## Legal and responsible use

Surveying the radio environment is not neutral everywhere. Scan only where you
have permission, follow local radio and privacy rules, and remember that a
survey log tied to named places is a record of other people's networks and
devices. This repository publishes one real capture with the identifier columns
pseudonymised for exactly that reason — see `docs/SHOWCASE.md`.

## Reporting a problem

Open an issue on the repository. This is a personal hardware project, not a
product with a support commitment: there is no SLA and no embargo process, and
the honest expectation is best-effort in evenings. Please do not report findings
that depend on already having physical access to the board — that is inside the
threat model above, not outside it.
