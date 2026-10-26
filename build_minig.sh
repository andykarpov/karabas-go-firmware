#!/bin/sh

export PATH=$HOME/.platformio/penv/bin:$PATH

echo "Building MCU sources"

BUILD_VER=`date +%y%m%d%H`
echo "Build version: $BUILD_VER"

# MINI:
pio run -e minig -t clean
export PLATFORMIO_BUILD_FLAGS="-DBUILD_VER=$BUILD_VER"
pio run -e minig
cp .pio/build/minig/firmware.uf2 ../../karabas-mini/karabas-mini/firmware/firmware_g.uf2
cp src/karabas.ini ../../karabas-mini/karabas-mini/firmware/karabas.ini

pio run -e minig -t clean

echo "Done"

