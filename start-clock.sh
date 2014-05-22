#!/bin/bash

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
