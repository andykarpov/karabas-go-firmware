#!/bin/sh

export PATH=$HOME/.platformio/penv/bin:$PATH

echo "Building MCU sources"

BUILD_VER=`date +%y%m%d%H`
echo "Build version: $BUILD_VER"

# MINI: normal version
pio run -t clean
export PLATFORMIO_BUILD_FLAGS="-DHW_ID=2 -DDEBUG=0 -DFT_OSD=0 -DBUILD_VER=$BUILD_VER"
pio run
cp .pio/build/pico/firmware.uf2 ../../karabas-mini/karabas-mini/firmware/firmware.uf2

# MINI: debug version
pio run -t clean
export PLATFORMIO_BUILD_FLAGS="-DHW_ID=2 -DDEBUG=1 -DFT_OSD=0 -DBUILD_VER=$BUILD_VER"
pio run
cp .pio/build/pico/firmware.uf2 ../../karabas-mini/karabas-mini/firmware/debug/firmware.uf2

# MINI: ft812 version
pio run -t clean
export PLATFORMIO_BUILD_FLAGS="-DHW_ID=2 -DDEBUG=0 -DFT_OSD=1 -DBUILD_VER=$BUILD_VER"
pio run
cp .pio/build/pico/firmware.uf2 ../../karabas-mini/karabas-mini/firmware/ft812/firmware.uf2

pio run -t clean

echo "Done"

