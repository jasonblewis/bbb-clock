#!/bin/bash

# if not found, export the port
if ! grep -q gpio-20 /sys/kernel/debug/gpio 
then
    echo 20 > /sys/class/gpio/export
fi
echo out > /sys/class/gpio/gpio20/direction
echo 1 > /sys/class/gpio/gpio20/value
