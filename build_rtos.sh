#!/bin/sh

export PATH=$HOME/.platformio/penv/bin:$PATH

echo "Building MCU sources"

BUILD_VER=`date +%y%m%d%H`
echo "Build version: $BUILD_VER"

# GO (FreeRTOS):
pio run -e go_rtos -t clean
export PLATFORMIO_BUILD_FLAGS="-DBUILD_VER=$BUILD_VER"
pio run -e go_rtos
cp .pio/build/go_rtos/firmware.uf2 ../karabas-go/firmware/firmware_rtos.uf2
cp src/karabas.ini ../karabas-go/firmware/karabas.ini
cp src/karabas.ini ../karabas-go/cores/karabas.ini

pio run -e go_rtos -t clean

echo "Done"
