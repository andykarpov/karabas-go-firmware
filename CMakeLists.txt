# Karabas Go RP2040 firmware: C11 on the pico-sdk, FreeRTOS SMP, TinyUSB, Pico-PIO-USB.
# One uf2 per board: karabas_go (HW_ID 1, .kg1), karabas_mini (2, .kg2), karabas_minig (3, .kg3).
#
#   cmake -S . -B build -G Ninja && ninja -C build
#
# The pico-sdk and the FreeRTOS kernel come from PICO_SDK_PATH / FREERTOS_KERNEL_PATH (the
# environment or -D), or are fetched at the pinned versions below. The host tests are a
# separate project: test/CMakeLists.txt.
cmake_minimum_required(VERSION 3.13)

# pinned: the firmware is written and checked against these
set(KG_PICO_SDK_TAG 2.2.0)          # TinyUSB 0.18.0
set(KG_FREERTOS_TAG V11.2.0)

if (NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_FETCH_FROM_GIT ON)
  set(PICO_SDK_FETCH_FROM_GIT_TAG ${KG_PICO_SDK_TAG})
endif()
if (NOT FREERTOS_KERNEL_PATH AND NOT DEFINED ENV{FREERTOS_KERNEL_PATH})
  include(FetchContent)
  FetchContent_Declare(freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG ${KG_FREERTOS_TAG}
    GIT_SHALLOW ON)
  FetchContent_Populate(freertos_kernel)
  set(FREERTOS_KERNEL_PATH ${freertos_kernel_SOURCE_DIR})
endif()

set(PICO_BOARD pico)
# TinyUSB of the pico-sdk on FreeRTOS, its PIO-USB host driver with our copy of Pico-PIO-USB
# (upstream 0.7.3 with our diagnostics)
set(TINYUSB_OPT_OS OPT_OS_FREERTOS)
set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/Pico-PIO-USB CACHE PATH "Pico-PIO-USB sources")

include(cmake/pico_sdk_import.cmake)
include(cmake/FreeRTOS_Kernel_import.cmake)

project(karabas_go_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# BUILD_VER: the date stamp of a release (build*.sh, CI), shown in About
set(BUILD_VER "" CACHE STRING "firmware version")

file(GLOB_RECURSE KG_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/src/*.c
  ${CMAKE_CURRENT_LIST_DIR}/lib/KarabasCommon/src/*.c)
foreach(dir FatFs FT812 I2cDevices OSD RTC SdSpi Spi0Bus tusb_xinput)
  file(GLOB dir_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/lib/${dir}/*.c)
  list(APPEND KG_SOURCES ${dir_sources})
endforeach()

set(KG_INCLUDES
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/lib/KarabasCommon/src
  ${CMAKE_CURRENT_LIST_DIR}/lib/MsTime)
foreach(dir FatFs FT812 I2cDevices OSD RTC SdSpi Spi0Bus tusb_xinput)
  list(APPEND KG_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/lib/${dir})
endforeach()

function(karabas_firmware target hw_id)
  add_executable(${target} ${KG_SOURCES})
  target_include_directories(${target} PRIVATE ${KG_INCLUDES})
  target_compile_definitions(${target} PRIVATE
    HW_ID=${hw_id}
    # loop rates, worst loop gaps, stack headroom and the flash cache every 10 s (debug=1)
    DIAG_STATS=1
    # one XInput interface per USB device (lib/tusb_xinput)
    CFG_TUH_XINPUT=1
    # the pico-sdk's stdio is not used: the log is src/diag/debug_out on the CDC
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
  )
  if (BUILD_VER)
    target_compile_definitions(${target} PRIVATE BUILD_VER=${BUILD_VER})
  endif()
  # -Wno-format-truncation: snprintf into the OSD lines and paths cuts them on purpose
  target_compile_options(${target} PRIVATE -Wall -Wno-format-truncation -Os)
  target_link_options(${target} PRIVATE
    # panics are kept for the next boot's log (src/diag/hang_diag)
    -Wl,--wrap=panic
    # newlib's reentrancy per task (src/platform/newlib.c)
    -Wl,--wrap=__getreent)
  target_link_libraries(${target} PRIVATE
    pico_stdlib
    pico_unique_id
    pico_util
    hardware_pio
    hardware_dma
    hardware_spi
    hardware_i2c
    hardware_divider
    hardware_watchdog
    hardware_clocks
    tinyusb_device
    tinyusb_host
    tinyusb_pico_pio_usb
    FreeRTOS-Kernel
    # pvPortMalloc on newlib's malloc: one heap, chip_free_heap() counts all of it
    FreeRTOS-Kernel-Heap3
  )
  pico_enable_stdio_usb(${target} 0)
  pico_enable_stdio_uart(${target} 0)
  pico_add_extra_outputs(${target})
endfunction()

karabas_firmware(karabas_go 1)
karabas_firmware(karabas_mini 2)
karabas_firmware(karabas_minig 3)
