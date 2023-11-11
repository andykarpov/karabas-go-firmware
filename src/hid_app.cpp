#include <Arduino.h>
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "hid_app.h"
#include "main.h"

// Invoked when device is mounted (configured)
void tuh_hid_mount_cb (uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  (void)desc_report;
  (void)desc_len;

  Serial.printf("HID device mounted, address = %d\r\n", dev_addr);

  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  Serial.printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  if ( itf_protocol == HID_ITF_PROTOCOL_NONE )
  {
    hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info,
    MAX_REPORT, desc_report, desc_len);
    Serial.printf("HID has %u reports \r\n", hid_info[instance].report_count);
  }

  // Receive report
  tuh_hid_receive_report(dev_addr, instance);
}

/// Invoked when device is unmounted (bus reset/unplugged)
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  Serial.printf("HID device unmounted, address = %d\r\n", dev_addr);
}

//#################

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  //Serial.print('.');

  switch (itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      //TU_LOG2("HID receive boot keyboard report\r\n");
      process_kbd_report( (hid_keyboard_report_t const*) report );
    break;

    case HID_ITF_PROTOCOL_MOUSE:
      //TU_LOG2("HID receive boot mouse report\r\n");
      process_mouse_report( (hid_mouse_report_t const*) report );
    break;

    default:
      // Generic report requires matching ReportID and contents with previous parsed report info
      process_generic_report(dev_addr, instance, report, len);
    break;
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    //printf("Error: cannot request to receive report\r\n");
    Serial.println("Error: cannot request to receive report");
  }
}

static void process_kbd_report(hid_keyboard_report_t const *report)
{
  static hid_keyboard_report_t prev_report = {0};

  uint8_t change = report->modifier ^ prev_report.modifier;
  if (change != 0) {
    spi_queue(CMD_USB_KBD, 0, report->modifier);
    for(uint8_t i=0; i<6; i++) {
      spi_queue(CMD_USB_KBD, i+1, report->keycode[i]);
    }
    Serial.printf("Keyboard M: %d B: %d %d %d %d %d %d", 
      report->modifier, 
      report->keycode[0], 
      report->keycode[1],
      report->keycode[2],
      report->keycode[3],
      report->keycode[4],
      report->keycode[5]
    );
    Serial.println();
    prev_report = *report;
  }
}

static void process_mouse_report(hid_mouse_report_t const * report) {

  static hid_mouse_report_t prev_report = {0};

  bool change = ((report->x ^ prev_report.x) || 
                 (report->y ^ prev_report.y) || 
                 (report->buttons ^ prev_report.buttons) ||
                 (report->wheel ^ prev_report.wheel));
  if (change) {
    spi_queue(CMD_USB_MOUSE, 0, report->x);
    spi_queue(CMD_USB_MOUSE, 1, report->y);
    spi_queue(CMD_USB_MOUSE, 2, report->wheel);
    spi_queue(CMD_USB_MOUSE, 3, report->buttons);
    Serial.printf("Mouse X: %d, Y: %d, Z: %d, B: %d", report->x, report->y, report->wheel, report->buttons);
    Serial.println();
    prev_report = *report;
  }
}

static void process_gamepad_report(hid_gamepad_report_t const * report) {
  static hid_gamepad_report_t prev_report = { 0 };

  uint8_t hat_changed_mask = report->hat ^ prev_report.hat;
  uint8_t buttons_changed_mask = report->buttons ^ prev_report.buttons;
  if ( (hat_changed_mask & report->hat) || (buttons_changed_mask & report->buttons)) {
    spi_queue(CMD_USB_GAMEPAD, 0, report->hat);
    spi_queue(CMD_USB_GAMEPAD, 1, (uint8_t)report->buttons);
    Serial.printf("Gamepad Hat: %d, Buttons: %d", report->hat, report->buttons);
    Serial.println();
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

  prev_report = *report;

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
    Serial.println("Couldn't find the report info for this report !");
    return;
  }

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
        //TU_LOG1("HID receive keyboard report\r\n");
        //Serial.println("HID receive keyboard report");
        // Assume keyboard follow boot report layout
        process_kbd_report( (hid_keyboard_report_t const*) report );
      break;
      case HID_USAGE_DESKTOP_MOUSE:
        //TU_LOG1("HID receive mouse report\r\n");
        //Serial.println("HID receive mouse report");
        // Assume mouse follow boot report layout
        process_mouse_report( (hid_mouse_report_t const*) report );
      break;
      case HID_USAGE_DESKTOP_GAMEPAD:
        //TU_LOG1("HID receive gamepad report\r\n");
        //Serial.println("HID receive gamepad report");
        // Assume gamepad follow boot report layout
        process_gamepad_report( (hid_gamepad_report_t const*) report );
      break;
      default: break;
    }
  }
}

