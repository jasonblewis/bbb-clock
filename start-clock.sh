#!/bin/bash

# LEGACY launcher. The clock now runs under clock.service, and the status LEDs
# are darkened ~30s after boot by leds-off.timer (issue #12) — NOT immediately as
# below. This inline LED-off block is superseded; kept only for the old path.

# turn off LEDs

for i in /sys/class/leds/beaglebone\:green\:usr*
do
    echo none > "$i"/trigger
    echo 0 > "$i"/brightness
done


# turn off wifi green LED
echo none >  /sys/class/leds/ath9k_htc-phy0/trigger

# start clock in a screen instance
exec /root/clock/clock -t 
