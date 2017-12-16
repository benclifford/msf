#!/bin/bash

# configures GPIO pin and then launches the reader
# run this as root.

echo 25 > /sys/class/gpio/export
echo in > /sys/class/gpio/gpio25/direction
echo both > /sys/class/gpio/gpio25/edge

./shift
