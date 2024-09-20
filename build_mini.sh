#!/bin/sh

export PATH=$HOME/.platformio/penv/bin:$PATH

echo "Building MCU sources"

BUILD_VER=`date +%y%m%d%H`
echo "Build version: $BUILD_VER"

# MINI:
pio run -t clean
export PLATFORMIO_BUILD_FLAGS="-DHW_ID=2 -DBUILD_VER=$BUILD_VER"
pio run
cp .pio/build/pico/firmware.uf2 ../../karabas-mini/karabas-mini/firmware/firmware.uf2
cp src/karabas.ini ../../karabas-mini/karabas-mini/cores/karabas.ini

pio run -t clean

echo "Done"

