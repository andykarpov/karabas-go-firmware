#!/bin/sh

export PATH=$HOME/.platformio/penv/bin:$PATH

echo "Building MCU sources"

BUILD_VER=`date +%y%m%d%H`
echo "Build version: $BUILD_VER"

# MINI:
pio run -e mini -t clean
export PLATFORMIO_BUILD_FLAGS="-DBUILD_VER=$BUILD_VER"
pio run -e mini
cp .pio/build/mini/firmware.uf2 ../../karabas-mini/karabas-mini/firmware/firmware.uf2
cp src/karabas.ini ../../karabas-mini/karabas-mini/cores/karabas.ini
cp src/karabas.ini ../../karabas-mini/karabas-mini/firmware/karabas.ini

pio run -e mini -t clean

echo "Done"

