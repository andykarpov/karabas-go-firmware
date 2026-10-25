#include <Arduino.h>
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "xinput_host.h"
#include "xinput_joy_map.h"
#include "input/usb_joy.h"
#include "config.h"
#include "storage/settings.h"

void tuh_xinput_mount_cb(uint8_t address, uint8_t instance, const xinputh_interface_t *itf) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(address, &vid, &pid);
  d_printf("XInput mounted %04x:%04x address=%u instance=%u type=%u\r\n", vid, pid, address, instance, itf->type);
  if (itf->connected) usb_joy_set(address, instance, true, 1);
  tuh_xinput_receive_report(address, instance);
}

void tuh_xinput_umount_cb(uint8_t address, uint8_t instance) {
  usb_joy_remove(address, instance, true);
  d_printf("XInput unmounted address=%u instance=%u\r\n", address, instance);
}

void tuh_xinput_report_received_cb(uint8_t address, uint8_t instance, const xinputh_interface_t *itf, uint16_t len) {
  (void)len;
  if (itf->last_xfer_result == XFER_RESULT_SUCCESS) {
    if (!itf->connected) {
      usb_joy_remove(address, instance, true);
    } else if (itf->new_pad_data) {
      const auto &p = itf->pad;
      usb_joy_set(address, instance, true, xinput_joy_map(p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY));
      if (p.wButtons != 0 || p.bLeftTrigger > 30 || p.bRightTrigger > 30) {
        d_printf("XInput %u:%u buttons=%04x LT=%u RT=%u LX=%d LY=%d\r\n",
                 address, instance, p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY);
      }
    }
  }
  tuh_xinput_receive_report(address, instance);
}
