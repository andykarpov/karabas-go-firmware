#!/bin/sh

export PATH=$HOME/.platformio/penv/bin:$PATH

echo "Building MCU sources"

BUILD_VER=`date +%y%m%d%H`
echo "Build version: $BUILD_VER"

# normal version
pio run -t clean
export PLATFORMIO_BUILD_FLAGS="-DDEBUG=0 -DFT_OSD=0 -DBUILD_VER=$BUILD_VER"
pio run
cp .pio/build/pico/firmware.uf2 ../karabas-go/firmware/firmware.uf2

#debug version
pio run -t clean
export PLATFORMIO_BUILD_FLAGS="-DDEBUG=1 -DFT_OSD=0 -DBUILD_VER=$BUILD_VER"
pio run
cp .pio/build/pico/firmware.uf2 ../karabas-go/firmware/debug/firmware.uf2

#ft812 version
pio run -t clean
export PLATFORMIO_BUILD_FLAGS="-DDEBUG=0 -DFT_OSD=1 -DBUILD_VER=$BUILD_VER"
pio run
cp .pio/build/pico/firmware.uf2 ../karabas-go/firmware/ft812/firmware.uf2

pio run -t clean

echo "Done"

