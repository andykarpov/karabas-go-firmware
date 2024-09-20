#!/bin/sh

export PATH=$HOME/.platformio/penv/bin:$PATH

echo "Building MCU sources"

BUILD_VER=`date +%y%m%d%H`
echo "Build version: $BUILD_VER"

# GO: 
pio run -t clean
export PLATFORMIO_BUILD_FLAGS="-DHW_ID=1 -DBUILD_VER=$BUILD_VER"
pio run
cp .pio/build/pico/firmware.uf2 ../karabas-go/firmware/firmware.uf2
cp src/karabas.ini ../karabas-go/firmware/karabas.ini
cp src/karabas.ini ../karabas-go/cores/karabas.ini

pio run -t clean

echo "Done"
