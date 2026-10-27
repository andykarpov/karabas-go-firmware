#!/bin/sh
# Release build of the Karabas Mini firmware into the sibling repo (CI does the same on master).
# Needs the earlephilhower toolchain on PATH (or PICO_TOOLCHAIN_PATH); the pico-sdk and FreeRTOS
# come from PICO_SDK_PATH / FREERTOS_KERNEL_PATH or are fetched.
set -e

BUILD_VER=`date +%y%m%d%H`
echo "Build version: $BUILD_VER"

rm -rf build-release
cmake -S . -B build-release -G Ninja -DBUILD_VER=$BUILD_VER
ninja -C build-release karabas_mini
cp build-release/karabas_mini.uf2 ../../karabas-mini/karabas-mini/firmware/firmware.uf2
cp src/karabas.ini ../../karabas-mini/karabas-mini/firmware/karabas.ini
rm -rf build-release

echo "Done"
