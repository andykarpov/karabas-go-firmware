#include <Arduino.h>
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "xinput_host.h"
#include "xinput_joy_map.h"
#include "xinput_app.h"
#include "usb_joy_slots.h"
#include "hardware/sync.h"
#include "config.h"
#include "storage/settings.h"

static UsbJoySlots<2> slots;
static spin_lock_t *joy_lock = spin_lock_init(spin_lock_claim_unused(true));

static void set_pad(uint8_t address, uint8_t instance, uint16_t buttons) {
  uint32_t save = spin_lock_blocking(joy_lock);
  slots.set(address, instance, true, buttons);
  spin_unlock(joy_lock, save);
}

static void remove_pad(uint8_t address, uint8_t instance) {
  uint32_t save = spin_lock_blocking(joy_lock);
  slots.remove(address, instance, true);
  spin_unlock(joy_lock, save);
}

void xinput_joystick_snapshot(uint16_t buttons[2]) {
  uint32_t save = spin_lock_blocking(joy_lock);
  slots.snapshot(buttons);
  spin_unlock(joy_lock, save);
}

extern "C" usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *count) {
  *count = 1;
  return &usbh_xinput_driver;
}

void tuh_xinput_mount_cb(uint8_t address, uint8_t instance, const xinputh_interface_t *itf) {
  uint16_t vid = 0, pid = 0;
  tuh_vid_pid_get(address, &vid, &pid);
  d_printf("XInput mounted %04x:%04x address=%u instance=%u type=%u\r\n", vid, pid, address, instance, itf->type);
  if (itf->connected) set_pad(address, instance, 1);
  tuh_xinput_receive_report(address, instance);
}

void tuh_xinput_umount_cb(uint8_t address, uint8_t instance) {
  remove_pad(address, instance);
  d_printf("XInput unmounted address=%u instance=%u\r\n", address, instance);
}

void tuh_xinput_report_received_cb(uint8_t address, uint8_t instance, const xinputh_interface_t *itf, uint16_t len) {
  (void)len;
  if (itf->last_xfer_result == XFER_RESULT_SUCCESS) {
    if (!itf->connected) {
      remove_pad(address, instance);
    } else if (itf->new_pad_data) {
      const auto &p = itf->pad;
      set_pad(address, instance,xinput_joy_map(p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY));
      d_printf("XInput %u:%u buttons=%04x LT=%u RT=%u LX=%d LY=%d\r\n",
               address, instance, p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY);
    }
  }
  tuh_xinput_receive_report(address, instance);
}
