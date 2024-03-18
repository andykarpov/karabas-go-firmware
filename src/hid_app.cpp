#include <Arduino.h>
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "hid_app.h"
#include "main.h"
#include "host/usbh.h"

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

  if ( itf_protocol == HID_ITF_PROTOCOL_NONE )
  {
    hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info,
    MAX_REPORT, desc_report, desc_len);
    d_printf("HID has %u reports \r\n", hid_info[instance].report_count);
  }

  // Receive report
  tuh_hid_receive_report(dev_addr, instance);
}

/// Invoked when device is unmounted (bus reset/unplugged)
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
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
      process_kbd_report( dev_addr, instance, (hid_keyboard_report_t const*) report, len );
    break;

    case HID_ITF_PROTOCOL_MOUSE:
        if (len == 3 || len == 4) {
          process_mouse_report( dev_addr, instance, (hid_mouse_report_t const*) report, len );
        } else if (len == 8) {
          process_mouse_report_ext(dev_addr, instance, (hid_mouse_report_ext_t const*) report, len);
        }
      //process_mouse_report( dev_addr, instance, (hid_mouse_report_t const*) report, len );
    break;

    default:
      // Generic report requires matching ReportID and contents with previous parsed report info
      process_generic_report(dev_addr, instance, report, len);
    break;
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    d_println("Error: cannot request to receive report");
  }
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
    usb_keyboard_report = *report;
    spi_queue(CMD_USB_KBD, 0, report->modifier);
    for(uint8_t i=0; i<6; i++) {
      spi_queue(CMD_USB_KBD, i+1, report->keycode[i]);
    }
    d_printf("Keyboard M: %d B: %d %d %d %d %d %d", 
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
    d_printf("Mouse X: %d, Y: %d, Z: %d, B: %d", report->x, report->y, report->wheel, report->buttons);
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
    d_printf("Mouse X: %d, Y: %d, Z: %d, B: %d, Pan: %d", report->x, report->y, report->wheel, report->buttons, report->pan);
    d_println();

    if (report->wheel != 0) {
      spi_queue(CMD_USB_MOUSE, 2, report->wheel);
      d_printf("Mouse X: %d, Y: %d, Z: %d, B: %d, Pan: %d", report->x, report->y, 0, report->buttons, report->pan);
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
    d_printf("Gamepad Hat: %d, Buttons: %d, XYZ %d %d %d, RXRYRZ %d %d %d", r->hat, r->buttons, r->rx, r->ry, r->rz, r->x, r->y, r->z);
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

    d_printf("Joystick: %d %d %d %d Buttons %d %d %d %d", r->axis[0], r->axis[1], r->axis[2], r->axis[3], r->buttons[0], r->buttons[1], r->buttons[2], r->buttons[3]);
    d_println();
  }

  prev_report = *r;

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
    d_println("Couldn't find the report info for this report !");
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
        process_kbd_report( dev_addr, instance, (hid_keyboard_report_t const*) report, len );
      break;
      case HID_USAGE_DESKTOP_MOUSE:
        if (len == 3 || len == 4) {
          process_mouse_report( dev_addr, instance, (hid_mouse_report_t const*) report, len );
        } else if (len == 8) {
          process_mouse_report_ext(dev_addr, instance, (hid_mouse_report_ext_t const*) report, len);
        }
      break;
      case HID_USAGE_DESKTOP_JOYSTICK:
        process_joystick_report( dev_addr, instance, report, len );
      break;
      case HID_USAGE_DESKTOP_GAMEPAD:
        process_gamepad_report( dev_addr, instance, report, len);
      break;
      default: break;
    }
  }
}

