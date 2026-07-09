#!/bin/sh
# Turn off every status LED so the clock display is the only lit thing in the
# room: the BBB onboard USR0-3 LEDs, the MMC/SD activity LED, and the USB WiFi
# dongle LED. Fired ~30s after boot by leds-off.timer, which keeps the LEDs
# available as a boot-progress signal for the first 30s (issue #12).
#
# We sweep every /sys/class/leds/* entry rather than naming LEDs, so it stays
# correct across kernel/adapter changes (e.g. the WiFi LED name is chipset-
# specific: ath9k_htc-phy0 on this device's Atheros AR9271).
#
# LEDS_DIR overrides the sysfs LED root for off-device testing.

LEDS_DIR="${LEDS_DIR:-/sys/class/leds}"

for led in "$LEDS_DIR"/*; do
    [ -d "$led" ] || continue
    # Detach any kernel trigger (heartbeat, mmc0, cpu0, phy0tpt, ...) first so
    # nothing re-lights the LED, then force it dark.
    [ -w "$led/trigger" ]    && echo none > "$led/trigger"    2>/dev/null
    [ -w "$led/brightness" ] && echo 0    > "$led/brightness" 2>/dev/null
done

exit 0
