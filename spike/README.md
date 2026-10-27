# pico-sdk spike

Phase 0 of `docs/rewrite_plan.md`: the firmware's USB setup on the plain pico-sdk, to see that it runs without arduino-pico, Adafruit TinyUSB and `patches/`.

- **FreeRTOS SMP** V11.2.0. The pico-sdk locks block a task through the port's interop, instead of a `wfe` loop per core.
- **TinyUSB 0.18.0** of the pico-sdk 2.2.0, on `OPT_OS_FREERTOS`:
  - device on the native port: CDC for the log, MSC with an 8 KB RAM disk, under the firmware's ids (2e8a:000a, "Karabas" / "Go", the flash id as serial);
  - host on PIO-USB: `lib/Pico-PIO-USB` of the firmware, with its diagnostics and the SE0 fix of #57.
- **Tasks**, as in the firmware:
  - `USBD` (core0) runs `tud_task()`, blocking on TinyUSB's queue;
  - `USB` (core1) runs `tuh_task()`;
  - `APP` (core0) never blocks: it sends the log to the CDC with no `__usb_mutex`, prints a `DIAG` line every 5 s and blinks LED1.
- **Mini G and Mini only** (`-DKG_HW_ID=3`, the default, or 2). No FPGA, SD card or I2C.

## Build

Needs the pico-sdk 2.2.0 (with its `lib/tinyusb` submodule), FreeRTOS-Kernel V11.2.0, cmake, ninja and an arm-none-eabi gcc. The arduino-pico one from PlatformIO works: `newlib_locks.c` gives its newlib the locks that arduino-pico brings otherwise.

```
python3 -m venv ~/pico/venv && ~/pico/venv/bin/pip install cmake ninja
git clone -b 2.2.0 https://github.com/raspberrypi/pico-sdk.git ~/pico/pico-sdk
git -C ~/pico/pico-sdk submodule update --init lib/tinyusb
git clone -b V11.2.0 https://github.com/FreeRTOS/FreeRTOS-Kernel.git ~/pico/FreeRTOS-Kernel

export PATH=~/pico/venv/bin:~/.platformio/packages/toolchain-rp2040-earlephilhower/bin:$PATH
export PICO_SDK_PATH=~/pico/pico-sdk FREERTOS_KERNEL_PATH=~/pico/FreeRTOS-Kernel
cmake -S spike -B spike/build -G Ninja
ninja -C spike/build
```

The first configure builds picotool from source, into `spike/build/_deps`.

## Flash

The firmware and the spike both reboot into BOOTSEL on a 1200 baud touch:

```
stty -F /dev/ttyACM0 1200
# wait for the RPI-RP2 drive, then
~/.platformio/packages/tool-picotool-rp2040-earlephilhower/picotool load -x spike/build/kg_spike.uf2
```

To go back: `pio run -e minig -t upload` (the same 1200 baud touch).

## What to check

Open the port (`pio device monitor`). The boot log waits in an 8 KB buffer until a terminal opens the port.

- **Boot.** `USB host: started` with the free spinlocks and `PIO programs loaded`, then `USB device N mounted` / `HID N.M mounted` for the hub, the keyboard and the gamepads.
- **Keys.** A boot keyboard logs its keys (`key N.M: mod .. keys ..`).
- **DIAG** every 5 s:
  - `usb sof` should be 1000/s, with a max gap of about 1000 us;
  - `disc` / `own` should stay 0;
  - also the root port and its pins, `hid reports`, the free heap and stacks.
- **Load.** Press `f` in the terminal (or BTN1): APP floods the CDC while the keyboard is in use. With arduino-pico this case stopped core0 (adafruit/Adafruit_TinyUSB_Arduino#293). Expect no hang, no lost keys and an even SOF.
- **MSC.** The RAM disk mounts on the PC, with `README.TXT`.
- **Crashes.** A crash (assert, stack overflow, panic) reboots, and the next boot logs `Last boot crashed: ...`.

Keys: `f` flood on / off, `s` DIAG now, `b` BOOTSEL.
