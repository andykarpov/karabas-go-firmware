#include <Arduino.h>
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "hid_app.h"
#include "main.h"
#include "host/usbh.h"
#include "host/hub.h"
#include "ps2kbd.h"
#include "SegaController.h"
#include "hardware/sync.h"
extern "C" {
bool usb_root_connected(void); // usb_root.c
void usb_root_drop(void);
}

hid_keyboard_report_t usb_keyboard_report;
hid_mouse_report_t usb_mouse_report;
uint16_t joyUSB[MAX_USB_JOYSTICKS];
uint8_t joyUSB_len;

// keyboard report is written by core1 (usb host) and read by core0 (osd, hotkeys)
// a plain hardware spinlock (irqs off for an 8-byte copy): it never waits through FreeRTOS,
// unlike pico mutexes, whose wakeups from irq context go through the FreeRTOS timer task
static spin_lock_t *kbd_report_lock = spin_lock_init(spin_lock_claim_unused(true));
static hid_keyboard_report_t kbd_report_shared = {0};

void hid_kbd_report_sync()
{
  hid_kbd_report_get(&usb_keyboard_report);
}

void hid_kbd_report_get(hid_keyboard_report_t *report)
{
  uint32_t save = spin_lock_blocking(kbd_report_lock);
  *report = kbd_report_shared;
  spin_unlock(kbd_report_lock, save);
}

typedef struct {
  tusb_desc_device_t desc_device;
  uint16_t manufacturer[32];
  uint16_t product[48];
  uint16_t serial[16];
  bool mounted;
} dev_info_t;

// CFG_TUH_DEVICE_MAX is defined by tusb_config header
dev_info_t dev_info[CFG_TUH_DEVICE_MAX] = { 0 };

bool hid_has_driver(uint16_t vid, uint16_t pid)
{
  if (joy_drivers_len > 0) {
    for (uint8_t i = 0; i < joy_drivers_len; i++) {
      if (joy_drivers[i].vid == vid && joy_drivers[i].pid == pid) {
        return true;
      }
    }
  }
  return false;
}

hid_joy_config_t hid_load_driver(uint16_t vid, uint16_t pid)
{
  hid_joy_config_t cfg;
  if (joy_drivers_len > 0) {
    for (uint8_t i = 0; i < joy_drivers_len; i++) {
      if (joy_drivers[i].vid == vid && joy_drivers[i].pid == pid) {
        return joy_drivers[i];
      }
    }
  }
  return cfg; 
}

void print_device_descriptor(tuh_xfer_t *xfer)
{
  if (XFER_RESULT_SUCCESS != xfer->result) {
    d_printf("Failed to get device descriptor\r\n");
    return;
  }
  uint8_t const daddr = xfer->daddr;
  dev_info_t *dev = &dev_info[daddr - 1];
  tusb_desc_device_t *desc = &dev->desc_device;
  d_printf("Device %u: ID %04x:%04x\r\n", daddr, desc->idVendor, desc->idProduct);
  d_printf("Device Descriptor:\r\n");
  d_printf("  bLength             %u\r\n"     , desc->bLength);
  d_printf("  bDescriptorType     %u\r\n"     , desc->bDescriptorType);
  d_printf("  bcdUSB              %04x\r\n"   , desc->bcdUSB);
  d_printf("  bDeviceClass        %u\r\n"     , desc->bDeviceClass);
  d_printf("  bDeviceSubClass     %u\r\n"     , desc->bDeviceSubClass);
  d_printf("  bDeviceProtocol     %u\r\n"     , desc->bDeviceProtocol);
  d_printf("  bMaxPacketSize0     %u\r\n"     , desc->bMaxPacketSize0);
  d_printf("  idVendor            0x%04x\r\n" , desc->idVendor);
  d_printf("  idProduct           0x%04x\r\n" , desc->idProduct);
  d_printf("  bcdDevice           %04x\r\n"   , desc->bcdDevice);
  d_printf("  iManufacturer       %u     "    , desc->iManufacturer);  
}

// Invoked when device is mounted (configured)
void tuh_mount_cb(uint8_t daddr) {
  d_printf("Device attached, address = %d\r\n", daddr);

  dev_info_t *dev = &dev_info[daddr - 1];
  dev->mounted = true;

  // Get Device Descriptor
  tuh_descriptor_get_device(daddr, &dev->desc_device, 18, print_device_descriptor, 0);
}

void tuh_umount_cb(uint8_t daddr) {
  d_printf("Device removed, address = %d\r\n", daddr);
  dev_info_t *dev = &dev_info[daddr - 1];
  dev->mounted = false;
}

// Invoked during enumeration, once the device has an address and sent its device descriptor
void tuh_enum_descriptor_device_cb(uint8_t daddr, const tusb_desc_device_t *desc_device) {
  d_printf("Enumerating device %u: ID %04x:%04x, class %02x\r\n", daddr, desc_device->idVendor, desc_device->idProduct, desc_device->bDeviceClass);
}

// USB host watchdog. TinyUSB gives up on a device after 3 failed enumeration attempts, and
// the hub driver stops quietly when one of its control requests fails (port power, status
// polling): the hub stays configured, but a device plugged into it is never seen. PIO-USB
// reports a new attach only after the lines went SE0, so nothing recovers until a replug.
// When no device comes up, the root port is dropped as if it was unplugged: PIO-USB sees the
// device (or the hub of the Mini boards) again on the next frame and TinyUSB starts anew.
#define USB_WD_PERIOD_MS 1000
#define USB_WD_CHECKS 4 // suspicious checks in a row before a drop, doubled after each failed drop
#define USB_WD_HUB_PORTS 4 // ports scanned on a hub (the scan stops at the first one that fails)

static struct {
  uint8_t hub_addr;     // hub being scanned, 0 = no scan running
  uint8_t port;         // port whose status is requested
  bool busy;            // a status request is in flight
  bool bad;             // a port has a device tinyusb does not know about, or no power
  bool done;            // the last scan completed
  uint32_t request_ms;  // when the request in flight was sent
} usb_hub_scan;

static uint8_t usb_hub_status_buf[4];

static bool usb_hub_scan_request();

static bool usb_hub_has_device(uint8_t hub_addr, uint8_t port) {
  for (uint8_t daddr = 1; daddr <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; daddr++) {
    if (daddr == hub_addr || !tuh_connected(daddr)) continue;
    tuh_bus_info_t bus;
    if (tuh_bus_info_get(daddr, &bus) && bus.hub_addr == hub_addr && bus.hub_port == port) return true;
  }
  return false;
}

static void usb_hub_scan_cb(tuh_xfer_t *xfer) {
  usb_hub_scan.busy = false;
  uint8_t port = usb_hub_scan.port;
  if (xfer->result != XFER_RESULT_SUCCESS) {
    // no such port (a stall): the scan is over
    usb_hub_scan.done = true;
    usb_hub_scan.hub_addr = 0;
    return;
  }
  uint16_t status = usb_hub_status_buf[0] | (usb_hub_status_buf[1] << 8); // wPortStatus
  bool connected = status & (1u << 0);
  bool powered = status & (1u << 8);
  if (!powered || (connected && !usb_hub_has_device(xfer->daddr, port))) usb_hub_scan.bad = true;
  if (port < USB_WD_HUB_PORTS) {
    usb_hub_scan.port = port + 1;
    if (usb_hub_scan_request()) return;
  }
  usb_hub_scan.done = true;
  usb_hub_scan.hub_addr = 0;
}

// GET_PORT_STATUS with our own buffer: hub_port_get_status() would overwrite the port status
// that the hub driver keeps between the steps of its own status handling
static bool usb_hub_scan_request() {
  tusb_control_request_t const request = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_OTHER,
      .type      = TUSB_REQ_TYPE_CLASS,
      .direction = TUSB_DIR_IN
    },
    .bRequest = HUB_REQUEST_GET_STATUS,
    .wValue   = 0,
    .wIndex   = usb_hub_scan.port,
    .wLength  = 4
  };
  tuh_xfer_t xfer = {
    .daddr       = usb_hub_scan.hub_addr,
    .ep_addr     = 0,
    .setup       = &request,
    .buffer      = usb_hub_status_buf,
    .complete_cb = usb_hub_scan_cb,
    .user_data   = 0
  };
  usb_hub_scan.busy = true;
  usb_hub_scan.request_ms = millis();
  if (!tuh_control_xfer(&xfer)) {
    usb_hub_scan.busy = false; // control pipe busy
    return false;
  }
  return true;
}

// one check: true if the usb host looks stuck. A device counts as working once all its
// interfaces are configured (tuh_mount_cb): tuh_mounted() turns true right after
// SET_CONFIGURATION, before the class drivers set up their interfaces.
static bool usb_host_check() {
  bool device_pending = false; // has an address but is not mounted
  for (uint8_t daddr = 1; daddr <= CFG_TUH_DEVICE_MAX; daddr++) {
    if (dev_info[daddr - 1].mounted) return false;
    if (tuh_connected(daddr)) device_pending = true;
  }
  if (device_pending) return true;
  uint8_t hub_addr = 0;
  for (uint8_t daddr = CFG_TUH_DEVICE_MAX + 1; daddr <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; daddr++) {
    if (tuh_mounted(daddr)) hub_addr = daddr;
  }
  if (hub_addr == 0) return true; // something on the root port, and nothing works
  // a hub and nothing behind it: fine if its ports are empty. The result of the last scan
  // counts, and so does a scan that could not start (control pipe busy) or never completes.
  bool bad;
  if (usb_hub_scan.busy) {
    bad = millis() - usb_hub_scan.request_ms > USB_WD_PERIOD_MS;
    if (!bad) return false;
    usb_hub_scan.busy = false; // give up on it, the next check sends a new one
    return true;
  }
  bad = usb_hub_scan.done ? usb_hub_scan.bad : usb_hub_scan.hub_addr != 0;
  usb_hub_scan.hub_addr = hub_addr;
  usb_hub_scan.port = 1;
  usb_hub_scan.bad = false;
  usb_hub_scan.done = false;
  usb_hub_scan_request();
  return bad;
}

void usb_host_watchdog() {
  static uint32_t last_ms = 0;
  static uint8_t suspicious = 0;
  static uint8_t failed_drops = 0; // drops in a row after which nothing came up
  uint32_t now = millis();
  if (now - last_ms < USB_WD_PERIOD_MS) return;
  last_ms = now;
  if (!usb_root_connected()) {
    suspicious = 0;
    return;
  }
  if (!usb_host_check()) {
    suspicious = 0;
    for (uint8_t i = 0; i < CFG_TUH_DEVICE_MAX; i++) {
      if (dev_info[i].mounted) failed_drops = 0;
    }
    return;
  }
  if (++suspicious < (USB_WD_CHECKS << failed_drops)) return;
  suspicious = 0;
  if (failed_drops < 4) failed_drops++;
  d_println("USB host: no device came up, re-enumerating");
  usb_hub_scan = {};
  usb_root_drop();
}

// Invoked when device is mounted (configured)
void tuh_hid_mount_cb (uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  d_printf("HID device mounted, address = %d, instance = %d\r\n", dev_addr, instance);

  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  d_printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  hid_info[instance].vid = vid;
  hid_info[instance].pid = pid;
  hid_info[instance].mounted = true;
  hid_info[instance].has_driver = false;

  d_printf("HID VID/PID = %04x/%04x\r\n", vid, pid);

  if ( itf_protocol == HID_ITF_PROTOCOL_NONE )
  {
    hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
    d_printf("HID has %u reports \r\n", hid_info[instance].report_count);
    hid_info[instance].has_driver = hid_has_driver(vid, pid);
    if (hid_info[instance].has_driver) {
      hid_info[instance].joy_config = hid_load_driver(vid, pid);
    }
  }
  // Receive report
  tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_set_protocol_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t protocol)
{
  d_printf("HID device set protocol complete, address = %d, instance = %d, protocol = %d\r\n", dev_addr, instance, protocol);
}

/// Invoked when device is unmounted (bus reset/unplugged)
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  hid_info[instance].mounted = false;
  
  // reset joy data
  get_joy_num(dev_addr, instance);
  for (uint8_t i=0; i<MAX_USB_JOYSTICKS; i++) {
    joyUSB[i] = 0;
  }
  
  d_printf("HID device unmounted, address = %d, instance = %d\r\n", dev_addr, instance);
}

//#################

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  //d_print('.');

  switch (itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      if (len > 0) {
        //d_printf("HID protocol keyboard, len=%d", len); d_println();
        if (len == 8) {
          process_kbd_report( dev_addr, instance, (hid_keyboard_report_t const*) report, len );
        } else {
          // nkro
          process_kbd_report_ext( dev_addr, instance, (hid_keyboard_report_ext_t const*) report, len );
        }
      }
    break;

    case HID_ITF_PROTOCOL_MOUSE:
        if (len > 0) {
          //d_printf("HID protocol mouse, len=%d", len); d_println();
          if (len == 3 || len == 4) {
            process_mouse_report( dev_addr, instance, (hid_mouse_report_t const*) report, len );
          } else if (len == 8) {
            process_mouse_report_ext(dev_addr, instance, (hid_mouse_report_ext_t const*) report, len);
          }
        }
    break;

    default:
      // Generic report requires matching ReportID and contents with previous parsed report info
      //d_printf("HID protocol generic, len=%d", len); d_println();
      process_generic_report(dev_addr, instance, report, len);
    break;
  }

  // continue to request to receive report
  tuh_hid_receive_report(dev_addr, instance);
}

static void process_kbd_report(uint8_t dev_addr, uint8_t instance, hid_keyboard_report_t const *report, uint16_t len)
{
  static hid_keyboard_report_t prev_report = {0};

  uint8_t change = ((report->modifier ^ prev_report.modifier) || (report->keycode[0] ^ prev_report.keycode[0]) 
    || (report->keycode[1] ^ prev_report.keycode[1])
    || (report->keycode[2] ^ prev_report.keycode[2])
    || (report->keycode[3] ^ prev_report.keycode[3])
    || (report->keycode[4] ^ prev_report.keycode[4])
    || (report->keycode[5] ^ prev_report.keycode[5]));
  if (change != 0) {
    uint32_t save = spin_lock_blocking(kbd_report_lock);
    kbd_report_shared = *report;
    spin_unlock(kbd_report_lock, save);
    spi_queue(CMD_USB_KBD, 0, report->modifier);
    for(uint8_t i=0; i<6; i++) {
      spi_queue(CMD_USB_KBD, i+1, report->keycode[i]);
    }
    kb_usb_receive(report);
    d_printf("HID Keyboard: %02x %02x %02x %02x %02x %02x %02x", 
      report->modifier, 
      report->keycode[0], 
      report->keycode[1],
      report->keycode[2],
      report->keycode[3],
      report->keycode[4],
      report->keycode[5]
    );
    d_println();
    prev_report = *report;
  }
}

static void convert_nkro_to_6kro(hid_keyboard_report_ext_t const *nkro_report, hid_keyboard_report_t *report)
{
  report->modifier = nkro_report->mod;
  uint8_t idx = 0;
  for (uint8_t code = 0x04; code < 0xA4; code++) {
      if (idx < 6 && ((code >> 3) < 16) && nkro_report->bits[code >> 3] > 0 && (nkro_report->bits[code >> 3] & (1 << (code & 7)))) {
        report->keycode[idx] = code;
        idx++;
    }
  }
}

static void convert_nkro_to_6kro2(hid_keyboard_report_ext_t const *nkro_report, hid_keyboard_report_t *report)
{
  report->modifier = nkro_report->mod;
  uint8_t idx = 0;
  for (uint8_t b = 0; b < 16; b++) {
    if (nkro_report->bits[b] == 0) continue;
    uint8_t bits = nkro_report->bits[b];
    uint8_t code = b << 3;
    for(uint8_t i = 0; i<8; i++) {
      if (bitRead(bits, i) == 1 && idx < 6) {
        report->keycode[idx] = code + i;
        idx++;
      }
    }
  }
}

static void process_kbd_report_ext(uint8_t dev_addr, uint8_t instance, hid_keyboard_report_ext_t const *report, uint16_t len)
{
  hid_keyboard_report_t std_report = {0};
  convert_nkro_to_6kro2(report, &std_report);
  process_kbd_report(dev_addr, instance, &std_report, 8);
}

static void process_mouse_report(uint8_t dev_addr, uint8_t instance, hid_mouse_report_t const * report, uint16_t len) {

  static hid_mouse_report_t prev_report = {0};

  bool change = ((report->x ^ prev_report.x) || 
                 (report->y ^ prev_report.y) || 
                 (report->buttons ^ prev_report.buttons) ||
                 (report->wheel ^ prev_report.wheel));
  if (change) {
    //d_print("Mouse protocol len="); d_println(len);
    usb_mouse_report = *report;
    spi_queue(CMD_USB_MOUSE, 0, report->x);
    spi_queue(CMD_USB_MOUSE, 1, report->y);
    spi_queue(CMD_USB_MOUSE, 2, report->wheel);
    spi_queue(CMD_USB_MOUSE, 3, report->buttons);
    d_printf("HID Mouse XYZB: %02x %02x %02x %02x", report->x, report->y, report->wheel, report->buttons);
    d_println();
    prev_report = *report;
    prev_report.wheel = 0;
  }
}

static void process_mouse_report_ext(uint8_t dev_addr, uint8_t instance, hid_mouse_report_ext_t const * report, uint16_t len) {

  static hid_mouse_report_ext_t prev_report = {0};

  bool change = ((report->x ^ prev_report.x) || 
                 (report->y ^ prev_report.y) || 
                 (report->buttons ^ prev_report.buttons) ||
                 (report->wheel ^ prev_report.wheel) || 
                 (report->pan ^ prev_report.pan));
  if (change) {
    //d_print("Mouse protocol len="); d_println(len);
    //usb_mouse_report = *report;
    spi_queue(CMD_USB_MOUSE, 0, report->x);
    spi_queue(CMD_USB_MOUSE, 1, report->y);
    spi_queue(CMD_USB_MOUSE, 2, report->wheel);
    spi_queue(CMD_USB_MOUSE, 3, report->buttons);
    // to avoid stuck on previous wheel position - let's send a wheel to 0 after an event
    d_printf("HID Mouse XYZBP: %04x %04x %02x %02x %02x", report->x, report->y, report->wheel, report->buttons, report->pan);
    d_println();

    if (report->wheel != 0) {
      spi_queue(CMD_USB_MOUSE, 2, 0);
      d_printf("HID Mouse XYZBP: %04x %04x %02x %02x %02x", report->x, report->y, 0, report->buttons, report->pan);
      d_println();
    }
    prev_report = *report;
    prev_report.wheel = 0;
  }
}

static void process_gamepad_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  static hid_gamepad_report_t prev_report = { 0 };
  const hid_gamepad_report_t* r;

  r = (hid_gamepad_report_t const*) report;

  uint8_t hat_changed = r->hat ^ prev_report.hat;
  uint32_t buttons_changed = r->buttons ^ prev_report.buttons;
  int8_t rx_changed = r->rx ^ prev_report.rx;
  int8_t ry_changed = r->ry ^ prev_report.ry;
  int8_t rz_changed = r->rz ^ prev_report.rz;
  int8_t x_changed = r->x ^ prev_report.x;
  int8_t y_changed = r->y ^ prev_report.y;
  int8_t z_changed = r->z ^ prev_report.z;

  if ( (hat_changed) || (buttons_changed) || (rx_changed) || (ry_changed) || (rz_changed) || (x_changed) || (y_changed) || (z_changed) ) {
    spi_queue(CMD_USB_GAMEPAD, 0, r->hat);
    spi_queue(CMD_USB_GAMEPAD, 1, (uint8_t)r->buttons);
    d_printf("HID Gamepad Hat: %02x, Buttons: %08x, XYZ %02x %02x %02x, RXRYRZ %02x %02x %02x", r->hat, r->buttons, r->rx, r->ry, r->rz, r->x, r->y, r->z);
    d_println();
    // report->hat && GAMEPAD_HAT_CENTERED; // none
    // report->hat && GAMEPAD_HAT_UP; // up
    // report->hat && GAMEPAD_HAT_DOWN; // down
    // report->hat && GAMEPAD_HAT_LEFT; // left
    // report->hat && GAMEPAD_HAT_RIGHT; // right
    // report->hat && GAMEPAD_HAT_UP_LEFT; // up + left
    // report->hat && GAMEPAD_HAT_UP_RIGHT; // up + right
    // report->hat && GAMEPAD_HAT_DOWN_LEFT; // down + left
    // report->hat && GAMEPAD_HAT_DOWN_RIGHT; // down + right

    // report->buttons && GAMEPAD_BUTTON_A
    // report->buttons && GAMEPAD_BUTTON_B
    // report->buttons && GAMEPAD_BUTTON_C
    // report->buttons && GAMEPAD_BUTTON_X
    // report->buttons && GAMEPAD_BUTTON_Y
    // report->buttons && GAMEPAD_BUTTON_Z
  }

  prev_report = *r;

}

static void process_joystick_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  static hid_joystick_report_t prev_report = { 0 };
  const hid_joystick_report_t* r;
  r = (hid_joystick_report_t const*) report;

  bool axis_changed = (r->axis[0] ^ prev_report.axis[0]) || (r->axis[1] ^ prev_report.axis[1]) || (r->axis[2] ^ prev_report.axis[2]) || (r->axis[3] ^ prev_report.axis[3]);
  bool buttons_changed = (r->buttons[0] ^ prev_report.buttons[0]) || (r->buttons[1] ^ prev_report.buttons[1]) || (r->buttons[2] ^ prev_report.buttons[2]) || (r->buttons[3] ^ prev_report.buttons[3]);

  if ( axis_changed || buttons_changed ) {
    spi_queue(CMD_USB_JOYSTICK, 0, r->axis[0]);
    spi_queue(CMD_USB_JOYSTICK, 1, r->axis[1]);
    spi_queue(CMD_USB_JOYSTICK, 2, r->axis[2]);
    spi_queue(CMD_USB_JOYSTICK, 3, r->axis[3]);
    spi_queue(CMD_USB_JOYSTICK, 5, r->buttons[0]);
    spi_queue(CMD_USB_JOYSTICK, 6, r->buttons[1]);
    spi_queue(CMD_USB_JOYSTICK, 7, r->buttons[2]);
    spi_queue(CMD_USB_JOYSTICK, 8, r->buttons[3]);

    d_printf("HID Joystick: %02x %02x %02x %02x Buttons: %02x %02x %02x %02x", r->axis[0], r->axis[1], r->axis[2], r->axis[3], r->buttons[0], r->buttons[1], r->buttons[2], r->buttons[3]);
    d_println();
  }

  prev_report = *r;

}

static uint8_t get_joy_num(uint8_t dev_addr, uint8_t instance)
{
  uint8_t res = 0;
  uint8_t cnt = 0;
  for (uint8_t i=0; i<CFG_TUH_HID; i++) {
    if (hid_info[i].mounted && hid_info[i].has_driver) {
      if (i == instance) {
        res = cnt;
      }
      cnt++;
    }
  }
  joyUSB_len = cnt;
  return res;
}

static void process_driver_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  //d_println("Driver report processing");
  uint16_t res = hid_joy_map_report(&hid_info[instance].joy_config, report, len, SC_CTL_ON);
  joyUSB[get_joy_num(dev_addr, instance)] = res;
}

//--------------------------------------------------------------------+
// Generic Report
//--------------------------------------------------------------------+
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) dev_addr;

  uint8_t const rpt_count = hid_info[instance].report_count;
  tuh_hid_report_info_t* rpt_info_arr = hid_info[instance].report_info;
  tuh_hid_report_info_t* rpt_info = NULL;

  if ( rpt_count == 1 && rpt_info_arr[0].report_id == 0)
  {
    // Simple report without report ID as 1st byte
    rpt_info = &rpt_info_arr[0];
  }else
  {
    // Composite report, 1st byte is report ID, data starts from 2nd byte
    uint8_t const rpt_id = report[0];

    // Find report id in the arrray
    for(uint8_t i=0; i<rpt_count; i++)
    {
      if (rpt_id == rpt_info_arr[i].report_id )
      {
        rpt_info = &rpt_info_arr[i];
        break;
      }
    }

    report++;
    len--;
  }

  if (!rpt_info)
  {
    //printf("Couldn't find the report info for this report !\r\n");
    return;
  }

  //d_printf("Generic report usage page %u, usage %u\r\n", rpt_info->usage_page, rpt_info->usage);

  // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For examples:
  // - Keyboard                     : Desktop, Keyboard
  // - Mouse                        : Desktop, Mouse
  // - Gamepad                      : Desktop, Gamepad
  // - Consumer Control (Media Key) : Consumer, Consumer Control
  // - System Control (Power key)   : Desktop, System Control
  // - Generic (vendor)             : 0xFFxx, xx
  if ( rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP )
  {
    switch (rpt_info->usage)
    {
      case HID_USAGE_DESKTOP_KEYBOARD:
        //d_printf("HID usage desktop keyboard, len=%d", len); d_println();
        process_kbd_report( dev_addr, instance, (hid_keyboard_report_t const*) report, len );
      break;
      case HID_USAGE_DESKTOP_MOUSE:
        //d_printf("HID usage desktop mouse, len=%d", len); d_println();
        if (len == 3 || len == 4) {
          process_mouse_report( dev_addr, instance, (hid_mouse_report_t const*) report, len );
        } else if (len == 8) {
          process_mouse_report_ext(dev_addr, instance, (hid_mouse_report_ext_t const*) report, len);
        }
      break;
      case HID_USAGE_DESKTOP_JOYSTICK:
        //d_printf("HID usage desktop joystick, len=%d", len); d_println();
        dump_hid_report(instance, report, len);
        if (hid_info[instance].has_driver) {
          process_driver_report(dev_addr, instance, report, len);
        } else {
          // try find a driver
          hid_info[instance].has_driver = hid_has_driver(hid_info[instance].vid, hid_info[instance].pid);
          if (hid_info[instance].has_driver) {
            hid_info[instance].joy_config = hid_load_driver(hid_info[instance].vid, hid_info[instance].pid);
          } else {
            process_joystick_report( dev_addr, instance, report, len );
          }
        }
      break;
      case HID_USAGE_DESKTOP_GAMEPAD:
        //d_printf("HID usage desktop gamepad, len=%d", len); d_println();
        dump_hid_report(instance, report, len);
        if (hid_info[instance].has_driver) {
          process_driver_report(dev_addr, instance, report, len);
        } else {
          // try find a driver
          hid_info[instance].has_driver = hid_has_driver(hid_info[instance].vid, hid_info[instance].pid);
          if (hid_info[instance].has_driver) {
            hid_info[instance].joy_config = hid_load_driver(hid_info[instance].vid, hid_info[instance].pid);
          } else {
            process_gamepad_report( dev_addr, instance, report, len);
          }
        }
      break;
      default: 
        dump_hid_report(instance, report, len);
        if (hid_info[instance].has_driver) {
          process_driver_report(dev_addr, instance, report, len);
        } else {
          // try find a driver
          hid_info[instance].has_driver = hid_has_driver(hid_info[instance].vid, hid_info[instance].pid);
          if (hid_info[instance].has_driver) {
            hid_info[instance].joy_config = hid_load_driver(hid_info[instance].vid, hid_info[instance].pid);
          }
        }
    }
  }
}

static void dump_hid_report(uint8_t instance, uint8_t const* report, uint16_t len) {
  if (hw_setup.debug_hid) {
    d_printf("RAW %04x:%04x: ", hid_info[instance].vid, hid_info[instance].pid);
    for (uint16_t j=0; j<len; j++) {
      d_printf("%02x ", report[j]);
    }
    d_println();
  }
}