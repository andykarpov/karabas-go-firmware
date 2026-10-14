#!/bin/sh

export PATH=$HOME/.platformio/penv/bin:$PATH

echo "Building MCU sources"

BUILD_VER=`date +%y%m%d%H`
echo "Build version: $BUILD_VER"

# MINI (FreeRTOS):
pio run -e mini_rtos -t clean
export PLATFORMIO_BUILD_FLAGS="-DBUILD_VER=$BUILD_VER"
pio run -e mini_rtos
cp .pio/build/mini_rtos/firmware.uf2 ../../karabas-mini/karabas-mini/firmware/firmware_rtos.uf2
cp src/karabas.ini ../../karabas-mini/karabas-mini/cores/karabas.ini
cp src/karabas.ini ../../karabas-mini/karabas-mini/firmware/karabas.ini

pio run -e mini_rtos -t clean

echo "Done"

