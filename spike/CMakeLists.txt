# Karabas Go pico-sdk spike (docs/rewrite_plan.md, phase 0); see README.md.
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico)
set(KG_HW_ID 3 CACHE STRING "board: 2 Mini, 3 Mini G")
set(FIRMWARE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# TinyUSB of the pico-sdk on FreeRTOS, its PIO-USB host driver with our copy of Pico-PIO-USB
set(TINYUSB_OPT_OS OPT_OS_FREERTOS)
set(LOG 0)
# our copy (0.5.3 + fixes) by default; -DPICO_PIO_USB_PATH=... for upstream with pio-usb-0.7.3-diag.patch
set(PICO_PIO_USB_PATH ${FIRMWARE_ROOT}/lib/Pico-PIO-USB CACHE PATH "Pico-PIO-USB sources")

include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

project(kg_spike C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(kg_spike main.c usb_descriptors.c msc_disk.c newlib_locks.c)
target_include_directories(kg_spike PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(kg_spike PRIVATE
  HW_ID=${KG_HW_ID}
  PICO_PANIC_FUNCTION=spike_panic
)
# upstream Pico-PIO-USB only: put the whole usb host on one PIO block (0 or 1)
set(SPIKE_USB_PIO "" CACHE STRING "PIO block of the usb host, empty for the library default")
if (NOT SPIKE_USB_PIO STREQUAL "")
  target_compile_definitions(kg_spike PRIVATE SPIKE_USB_PIO=${SPIKE_USB_PIO})
endif()
target_compile_options(kg_spike PRIVATE -Wall)
target_link_libraries(kg_spike PRIVATE
  pico_stdlib
  pico_unique_id
  hardware_pio
  hardware_dma
  tinyusb_device
  tinyusb_host
  tinyusb_pico_pio_usb
  FreeRTOS-Kernel
  FreeRTOS-Kernel-Heap4
)
pico_enable_stdio_usb(kg_spike 0)
pico_enable_stdio_uart(kg_spike 0)
pico_add_extra_outputs(kg_spike)
