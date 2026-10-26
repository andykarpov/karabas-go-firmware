#include <Arduino.h>
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "hid_app.h"
#include "hid_driver.h"
#include "hid_joy_map.h"
#include "hid_auto_map.h"
#include "crc32.h"
#include "input/usb_joy.h"
#include "main.h"
#include "host/usbh.h"
#include "ps2kbd.h"
#include "SegaController.h"
#include "hardware/sync.h"

hid_keyboard_report_t usb_keyboard_report;
hid_mouse_report_t usb_mouse_report;

#define MAX_REPORT  4 // Max USB reports per instance

// Each HID instance can has multiple reports. Indexed by the TinyUSB HID index, which is unique
// across devices. Used by core1; vid, pid, driver and auto_map are written under usb_host_lock()
// and read by APP (usb_pads_get).
static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
  uint16_t vid;
  uint16_t pid;
  const hid_joy_config_t *driver; // drivers/*.ini of the device
  uint32_t driver_gen; // hid_drivers_gen when driver was looked up
  hid_auto_map_t *auto_map; // default map from the report descriptor, used when there is no driver
} hid_info[CFG_TUH_HID];

// a few default maps, enough for the two joystick ports and a spare
#define MAX_AUTO_MAPS 4
static hid_auto_map_t auto_maps[MAX_AUTO_MAPS];
static bool auto_map_used[MAX_AUTO_MAPS];

typedef struct
{
  uint8_t id;
  uint8_t buttons; /**< buttons mask for currently pressed buttons in the mouse. */
  int16_t  x;       /**< Current delta x movement of the mouse. */
  int16_t  y;       /**< Current delta y movement on the mouse. */
  int8_t  wheel;   /**< Current delta wheel movement on the mouse. */
  int8_t  pan;     // using AC Pan
} hid_mouse_report_ext_t;

typedef struct
{
  uint8_t id;
  uint8_t mod;
  uint8_t bits[16];
} hid_keyboard_report_ext_t;

static void process_kbd_report(uint8_t dev_addr, uint8_t instance, hid_keyboard_report_t const* report, uint16_t len);
static void process_kbd_report_ext(uint8_t dev_addr, uint8_t instance, hid_keyboard_report_ext_t const* report, uint16_t len);
static void process_mouse_report(uint8_t dev_addr, uint8_t instance, hid_mouse_report_t const* report, uint16_t len);
static void process_mouse_report_ext(uint8_t dev_addr, uint8_t instance, hid_mouse_report_ext_t const* report, uint16_t len);
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);
static void process_joy_report(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t const* report, uint16_t len);
static void dump_hid_report(uint8_t instance, uint8_t const* report, uint16_t len);
static void dump_hid_descriptor(uint8_t instance, uint8_t const* desc, uint16_t len);
static hid_auto_map_t *auto_map_build(uint8_t const* desc, uint16_t len);
static void auto_map_free(hid_auto_map_t *m);
static void capture_feed(uint8_t instance, uint8_t report_id, uint8_t const* report, uint16_t len);

// keyboard report is written by core1 (usb host) and read by core0 (osd, hotkeys),
// under usb_host_lock()
static hid_keyboard_report_t kbd_report_shared = {0};

void hid_kbd_report_sync()
{
  hid_kbd_report_get(&usb_keyboard_report);
}

void hid_kbd_report_get(hid_keyboard_report_t *report)
{
  uint32_t save = spin_lock_blocking(usb_host_lock());
  *report = kbd_report_shared;
  spin_unlock(usb_host_lock(), save);
}

// the mouse moves since the last hid_mouse_take(): written by core1, read by APP, under usb_host_lock()
static hid_mouse_move_t mouse_shared = {0};

static void mouse_add(int dx, int dy, int wheel, uint8_t buttons)
{
  uint32_t save = spin_lock_blocking(usb_host_lock());
  mouse_shared.dx += dx;
  mouse_shared.dy += dy;
  mouse_shared.wheel += wheel;
  mouse_shared.pressed |= buttons & ~mouse_shared.buttons;
  mouse_shared.buttons = buttons;
  mouse_shared.changed = true;
  spin_unlock(usb_host_lock(), save);
}

bool hid_mouse_take(hid_mouse_move_t *m)
{
  uint32_t save = spin_lock_blocking(usb_host_lock());
  *m = mouse_shared;
  mouse_shared.dx = mouse_shared.dy = mouse_shared.wheel = 0;
  mouse_shared.pressed = 0;
  mouse_shared.changed = false;
  spin_unlock(usb_host_lock(), save);
  return m->changed;
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

// Invoked when device is mounted (configured)
void tuh_hid_mount_cb (uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  d_printf("HID device mounted, address = %d, instance = %d\r\n", dev_addr, instance);

  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  d_printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  uint32_t save = spin_lock_blocking(usb_host_lock());
  hid_info[instance].vid = vid;
  hid_info[instance].pid = pid;
  hid_info[instance].driver = nullptr;
  hid_info[instance].auto_map = nullptr;
  spin_unlock(usb_host_lock(), save);

  d_printf("HID VID/PID = %04x/%04x\r\n", vid, pid);

  if ( itf_protocol == HID_ITF_PROTOCOL_NONE )
  {
    hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
    d_printf("HID has %u reports \r\n", hid_info[instance].report_count);
    dump_hid_descriptor(instance, desc_report, desc_len);
    uint32_t gen = hid_drivers_gen;
    const hid_joy_config_t *driver = hid_driver_find(vid, pid);
    if (driver) d_printf("HID %04x:%04x uses its driver %s\r\n", vid, pid, hid_driver_file(driver));
    // the default map also for a pad with a driver: it takes over when the driver is forgotten
    hid_auto_map_t *map = auto_map_build(desc_report, desc_len);
    save = spin_lock_blocking(usb_host_lock());
    hid_info[instance].driver_gen = gen;
    hid_info[instance].driver = driver;
    hid_info[instance].auto_map = map;
    spin_unlock(usb_host_lock(), save);
    // take a joystick port right away, so the ports follow the order the pads were plugged in
    if (hid_info[instance].driver || hid_info[instance].auto_map) {
      usb_joy_set(dev_addr, instance, false, SC_CTL_ON);
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
  uint32_t save = spin_lock_blocking(usb_host_lock());
  hid_auto_map_t *map = hid_info[instance].auto_map;
  hid_info[instance].driver = nullptr;
  hid_info[instance].auto_map = nullptr;
  spin_unlock(usb_host_lock(), save);
  auto_map_free(map);
  usb_joy_remove(dev_addr, instance, false);

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
    uint32_t save = spin_lock_blocking(usb_host_lock());
    kbd_report_shared = *report;
    spin_unlock(usb_host_lock(), save);
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
  mouse_add(report->x, report->y, report->wheel, report->buttons);

  // the core adds the deltas of every report (MS_UPD): a report that repeats the last one is a
  // move too; only a report without a move and with the same buttons is dropped
  bool change = (report->x || report->y || report->wheel ||
                 (report->buttons ^ prev_report.buttons));
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
  mouse_add(report->x, report->y, report->wheel, report->buttons);

  // as in process_mouse_report: every report with a move goes to the core
  bool change = (report->x || report->y || report->wheel || report->pan ||
                 (report->buttons ^ prev_report.buttons));
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

static hid_auto_map_t *auto_map_build(uint8_t const* desc, uint16_t len)
{
  for (uint8_t i = 0; i < MAX_AUTO_MAPS; i++) {
    if (auto_map_used[i]) continue;
    if (!hid_auto_map_build(desc, len, &auto_maps[i])) return nullptr;
    auto_map_used[i] = true;
    d_printf("HID default map: %u fields\r\n", auto_maps[i].count);
    return &auto_maps[i];
  }
  d_printf("HID default map: no free slot\r\n");
  return nullptr;
}

static void auto_map_free(hid_auto_map_t *m)
{
  if (m) auto_map_used[m - auto_maps] = false;
}

// Raw reports of one pad for the OSD gamepad mapper. The pad is the first one whose report
// changes after hid_capture_start(); a report hash per instance tells the change. Under
// usb_host_lock().
static bool capture_on = false;
static uint8_t capture_instance;
static uint32_t capture_hash[CFG_TUH_HID];
static bool capture_hash_valid[CFG_TUH_HID];
static hid_capture_t capture;

void hid_capture_start()
{
  uint32_t save = spin_lock_blocking(usb_host_lock());
  capture_on = true;
  capture_instance = 0xff;
  memset(capture_hash_valid, 0, sizeof(capture_hash_valid));
  capture.seq = 0;
  spin_unlock(usb_host_lock(), save);
}

void hid_capture_stop()
{
  uint32_t save = spin_lock_blocking(usb_host_lock());
  capture_on = false;
  spin_unlock(usb_host_lock(), save);
}

bool hid_capture_get(hid_capture_t *out)
{
  uint32_t save = spin_lock_blocking(usb_host_lock());
  bool has = capture_on && capture.seq != 0;
  if (has) *out = capture;
  spin_unlock(usb_host_lock(), save);
  return has;
}

static void capture_feed(uint8_t instance, uint8_t report_id, uint8_t const* report, uint16_t len)
{
  if (!capture_on) return; // a plain read: a report missed while it starts does not matter
  if (len > sizeof(capture.data)) len = sizeof(capture.data);
  uint32_t hash = kg_crc32_final(kg_crc32_update(KG_CRC32_INIT, report, len)) ^ report_id;
  uint32_t save = spin_lock_blocking(usb_host_lock());
  if (capture_on && capture_instance == 0xff) {
    if (!capture_hash_valid[instance]) {
      capture_hash_valid[instance] = true;
      capture_hash[instance] = hash;
    } else if (capture_hash[instance] != hash) {
      capture_instance = instance;
    }
  }
  if (capture_on && capture_instance == instance) {
    capture.vid = hid_info[instance].vid;
    capture.pid = hid_info[instance].pid;
    capture.report_id = report_id;
    capture.len = len;
    memcpy(capture.data, report, len);
    capture.seq++;
  }
  spin_unlock(usb_host_lock(), save);
}

// a joystick report goes through the driver ini of the device if there is one, otherwise
// through the default map built from its report descriptor
static void process_joy_report(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t const* report, uint16_t len)
{
  dump_hid_report(instance, report, len);
  capture_feed(instance, report_id, report, len);
  uint32_t gen = hid_drivers_gen;
  if (hid_info[instance].driver_gen != gen) {
    // the drivers were (re)loaded after the device was mounted
    const hid_joy_config_t *driver = hid_driver_find(hid_info[instance].vid, hid_info[instance].pid);
    uint32_t save = spin_lock_blocking(usb_host_lock());
    hid_info[instance].driver_gen = gen;
    hid_info[instance].driver = driver;
    spin_unlock(usb_host_lock(), save);
  }
  const hid_joy_config_t *driver = hid_info[instance].driver;
  uint16_t res;
  if (driver) {
    // reports with another id (battery, vendor data) must not release the buttons
    if (!hid_joy_accepts_report(driver, report_id)) return;
    res = hid_joy_map_report(driver, report, len, SC_CTL_ON);
  } else if (hid_info[instance].auto_map && hid_auto_map_has_report(hid_info[instance].auto_map, report_id)) {
    res = hid_auto_map_report(hid_info[instance].auto_map, report_id, report, len, SC_CTL_ON);
  } else {
    return;
  }
  usb_joy_set(dev_addr, instance, false, res);
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
      default:
        // joystick, gamepad and the rest
        process_joy_report(dev_addr, instance, rpt_info->report_id, report, len);
    }
  } else if (rpt_info->usage_page >= 0xFF00) {
    // vendor usage pages: some pads have no desktop collection at all
    process_joy_report(dev_addr, instance, rpt_info->report_id, report, len);
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

static void dump_hid_descriptor(uint8_t instance, uint8_t const* desc, uint16_t len) {
  if (hw_setup.debug_hid) {
    d_printf("DESC %04x:%04x, %u bytes:", hid_info[instance].vid, hid_info[instance].pid, len);
    for (uint16_t j=0; j<len; j++) {
      if ((j & 15) == 0) d_println();
      d_printf("%02x ", desc[j]);
    }
    d_println();
  }
}

void usb_pads_get(usb_pad_t pads[2])
{
  usb_joy_port_t ports[2];
  usb_joy_ports(ports);
  for (uint8_t i = 0; i < 2; i++) {
    usb_pad_t &p = pads[i];
    p = {};
    if (!ports[i].address) continue;
    if (ports[i].xinput) {
      p.source = USB_PAD_XINPUT;
      tuh_vid_pid_get(ports[i].address, &p.vid, &p.pid);
      continue;
    }
    uint8_t n = ports[i].instance;
    if (n >= CFG_TUH_HID) continue;
    uint32_t save = spin_lock_blocking(usb_host_lock());
    p.vid = hid_info[n].vid;
    p.pid = hid_info[n].pid;
    const hid_joy_config_t *driver = hid_info[n].driver;
    bool map = hid_info[n].auto_map != nullptr;
    spin_unlock(usb_host_lock(), save);
    if (driver) {
      p.source = USB_PAD_DRIVER;
      p.file = hid_driver_file(driver);
    } else {
      p.source = map ? USB_PAD_DEFAULT : USB_PAD_NONE;
    }
  }
}
