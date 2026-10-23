// Root port access for the usb host watchdog (hid_app.cpp). pio_usb_ll.h does not build as C++.
#include <stdbool.h>
#include <stdint.h>
#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "hardware/sync.h"

#define USB_ROOT_IDX 0 // tinyusb rhport 1

bool usb_root_connected(void) {
  root_port_t *root = PIO_USB_ROOT_PORT(USB_ROOT_IDX);
  return root->initialized && root->connected;
}

// drop the device on the root port as if it was unplugged (usb host task, core1)
void usb_root_drop(void) {
  root_port_t *root = PIO_USB_ROOT_PORT(USB_ROOT_IDX);
  // the SOF irq, which owns these fields, runs on this core. The remove event is queued here
  // and not by the next frame: that frame sees the device again, and the port handles the
  // connect bit before the disconnect bit, so the remove would abort the new enumeration.
  uint32_t save = save_and_disable_interrupts();
  root->connected = false;
  root->suspended = true;
  root->ints |= PIO_USB_INTS_DISCONNECT_BITS;
  pio_usb_host_irq_handler(USB_ROOT_IDX);
  restore_interrupts(save);
}
