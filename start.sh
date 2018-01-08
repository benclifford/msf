#!/bin/bash

# configures GPIO pin and then launches the reader
# run this as root.

echo 12 > /sys/class/gpio/export
echo in > /sys/class/gpio/gpio12/direction
echo both > /sys/class/gpio/gpio12/edge

./shiftmsf
