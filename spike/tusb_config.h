#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// CFG_TUSB_MCU and CFG_TUSB_OS (OPT_OS_FREERTOS, TINYUSB_OPT_OS in CMakeLists.txt) come from
// the pico-sdk build. The device is on the native USB port, the host on PIO-USB.

#define BOARD_TUD_RHPORT            0
#define BOARD_TUH_RHPORT            1

//--------------------------------------------------------------------+
// Device: CDC (log) + MSC (a RAM disk), as the firmware's CDC + MSC
//--------------------------------------------------------------------+
#define CFG_TUD_ENABLED             1
#define CFG_TUD_ENDPOINT0_SIZE      64

#define CFG_TUD_CDC                 1
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      1024
#define CFG_TUD_CDC_EP_BUFSIZE      64

#define CFG_TUD_MSC                 1
#define CFG_TUD_MSC_EP_BUFSIZE      4096 // the firmware's patches/6

//--------------------------------------------------------------------+
// Host on PIO-USB: the firmware's limits ([rp2040] in platformio.ini)
//--------------------------------------------------------------------+
#define CFG_TUH_ENABLED             1
#define CFG_TUH_RPI_PIO_USB         1

#define CFG_TUH_ENUMERATION_BUFSIZE 1024
#define CFG_TUH_HUB                 3
#define CFG_TUH_DEVICE_MAX          8
#define CFG_TUH_HID                 16
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64

#endif
