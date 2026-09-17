// Copy to include/secrets.h and fill in. secrets.h is gitignored.
//
// OTA_PASSWORD is the only thing standing between "jesse-scanner" being an open
// AP and anyone in radio range flashing arbitrary firmware onto the board.
//
// setup_ota() enforces two rules at COMPILE TIME, so a board cannot boot with a
// password that was never really set:
//   1. it may not be the placeholder below, and
//   2. it must be at least 8 characters.
// To build with OTA switched off instead, comment the #define out entirely —
// the firmware then refuses to start the OTA service at all, which is the safe
// default for a board you only ever flash over USB.
#pragma once

#define OTA_PASSWORD "change-me"

// OPTIONAL: also join a normal WiFi network as a station, so OTA works from a
// machine that stays on your home network instead of joining the board's AP.
// Leave both undefined for AP-only operation (the right default in the field).
// #define WIFI_STA_SSID "your-ssid"
// #define WIFI_STA_PASS "your-pass"
