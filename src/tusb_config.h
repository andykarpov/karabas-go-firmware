#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// CFG_TUSB_MCU and CFG_TUSB_OS (OPT_OS_FREERTOS, TINYUSB_OPT_OS in CMakeLists.txt) come from
// the pico-sdk build. The device is on the native USB port, the host on PIO-USB.

#define BOARD_TUD_RHPORT            0
#define BOARD_TUH_RHPORT            1

#define CFG_TUSB_DEBUG              0
#define CFG_TUSB_MEM_ALIGN          TU_ATTR_ALIGNED(4)

//--------------------------------------------------------------------+
// Device: CDC (the log, the serial bridge) + MSC (an SD card, src/storage/msc)
//--------------------------------------------------------------------+
#define CFG_TUD_ENABLED             1
#define CFG_TUD_ENDPOINT0_SIZE      64

#define CFG_TUD_CDC                 1
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      1024
#define CFG_TUD_CDC_EP_BUFSIZE      64

#define CFG_TUD_MSC                 1
// the read / write callbacks get up to 4 KB (8 sectors) at a time
#define CFG_TUD_MSC_EP_BUFSIZE      4096

//--------------------------------------------------------------------+
// Host on PIO-USB
//--------------------------------------------------------------------+
#define CFG_TUH_ENABLED             1
#define CFG_TUH_RPI_PIO_USB         1

// 3 hubs (the Mini G one, an external one, a keyboard's: the wired Apple keyboards have a hub
// inside), 8 devices, 16 HID interfaces. Pico-PIO-USB has 32 endpoints in all
// (PIO_USB_EP_POOL_CNT, a 32-bit mask): a device takes one for control plus one per interrupt /
// bulk endpoint. The full config descriptor and each HID report descriptor are read into the
// enumeration buffer: a longer config descriptor fails the device, a longer report descriptor
// is skipped.
#define CFG_TUH_ENUMERATION_BUFSIZE 1024
#define CFG_TUH_HUB                 3
#define CFG_TUH_DEVICE_MAX          8
#define CFG_TUH_HID                 16
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64

// A USB-UART adapter for the core serial (periph/usb_uart): one, the first plugged (a second
// one gets no driver and takes no endpoints). 3 endpoints of the pool (FTDI, CP210x, CH34x:
// control + bulk in / out), 4 for CDC ACM (+ the notification one). 8N1 at 115200 on mount,
// then the speed of the core; DTR and RTS on.
#define CFG_TUH_CDC                 1
#define CFG_TUH_CDC_FTDI            1
#define CFG_TUH_CDC_CP210X          1
#define CFG_TUH_CDC_CH34X           1
#define CFG_TUH_CDC_LINE_CONTROL_ON_ENUM 0x03
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM { 115200, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8 }

#endif
