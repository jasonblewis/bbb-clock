#!/usr/bin/env bash
# Off-device test for leds-off.sh. Builds a fake /sys/class/leds tree and checks
# the script detaches every trigger and drives every LED dark. No device needed.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
script="$here/leds-off.sh"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Mirror the real device: onboard USR0-3, the MMC activity LED, and the WiFi
# dongle LED, each with an active trigger and (some) lit.
make_led() { # name trigger brightness
  mkdir -p "$tmp/$1"
  printf '%s\n' "$2" > "$tmp/$1/trigger"
  printf '%s\n' "$3" > "$tmp/$1/brightness"
}
make_led "beaglebone:green:usr0" heartbeat 0
make_led "beaglebone:green:usr1" mmc0      0
make_led "beaglebone:green:usr2" cpu0      1
make_led "beaglebone:green:usr3" none      0
make_led "mmc0::"                mmc0      0
make_led "ath9k_htc-phy0"        phy0tpt   255

# A stray non-LED directory (no trigger/brightness) must not break the sweep.
mkdir -p "$tmp/not-an-led"

LEDS_DIR="$tmp" "$script"

fail=0
for led in beaglebone:green:usr0 beaglebone:green:usr1 beaglebone:green:usr2 \
           beaglebone:green:usr3 "mmc0::" ath9k_htc-phy0; do
  t="$(cat "$tmp/$led/trigger")"
  b="$(cat "$tmp/$led/brightness")"
  [ "$t" = "none" ] || { echo "FAIL: $led trigger=$t (want none)"; fail=1; }
  [ "$b" = "0" ]    || { echo "FAIL: $led brightness=$b (want 0)"; fail=1; }
done

if [ "$fail" -eq 0 ]; then
  echo "PASS: every LED trigger detached and brightness zeroed"
else
  exit 1
fi
