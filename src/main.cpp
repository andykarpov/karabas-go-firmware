#include <Arduino.h>
#include "Adafruit_SPIFlash.h"
#include <ArduinoJson.h>

// pio-usb is required for rp2040 host
#include "pio_usb.h"
#define HOST_PIN_DP   36   // Pin used as D+ for host, D- = D+ + 1

#define PIN_MCU_SPI_SCK 35
#define PIN_MCU_SPI_CS 34
#define PIN_MCU_SPI_RX 32
#define PIN_MCU_SPI_TX 31

#define PIN_SD_SPI_SCK 12
#define PIN_SD_SPI_CS 11
#define PIN_SD_SPI_RX 14
#define PIN_SD_SPI_TX 13
//#define SD_CHIP_SELECT_PIN PIN_SD_SPI_CS

#define PIN_I2C_SDA 15
#define PIN_I2C_SCL 16

#define PIN_JOY_SCK 7
#define PIN_JOY_DATA 8
#define PIN_JOY_LOAD 6
#define PIN_JOY_P7 3

#define PIN_CONF_INIT_B 5
#define PIN_CONF_PRG_B 27
#define PIN_CONF_IO1 29
#define PIN_CONF_CLK 28
#define PIN_CONF_DONE 30

#include <SPI.h>
#include <Wire.h>
#include "SdFat.h"
#include "sdios.h"

SdFs sd;
typedef FsFile file_t;
#define SPI_CLOCK SD_SCK_MHZ(50)
#define SD_CONFIG SdSpiConfig(PIN_SD_SPI_CS, DEDICATED_SPI, SPI_CLOCK)
FsFile bitstream;
FsFile config;

#include "Adafruit_TinyUSB.h"

#define MAX_REPORT  4

static uint8_t const keycode2ascii[128][2] =  { HID_KEYCODE_TO_ASCII };

// Each HID instance can has multiple reports
static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
}hid_info[CFG_TUH_HID];

static void process_kbd_report(hid_keyboard_report_t const *report);
static void process_mouse_report(hid_mouse_report_t const * report);
static void process_gamepad_report(hid_gamepad_report_t const * report);
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

// USB Host object
Adafruit_USBH_Host USBHost;

// holding device descriptor
tusb_desc_device_t desc_device;

// the setup function runs once when you press reset or power the board
void setup()
{
  Serial.begin(115200);
  Serial.println("Karabas Go rp2040 core");

  // SPI1 to FPGA
  SPI1.setSCK(PIN_MCU_SPI_SCK);
  SPI1.setRX(PIN_MCU_SPI_RX);
  SPI1.setTX(PIN_MCU_SPI_TX);
  SPI1.setCS(PIN_MCU_SPI_CS);
  SPI1.begin(PIN_MCU_SPI_CS);

  // SPI for SD
  SPI.setSCK(PIN_SD_SPI_SCK);
  SPI.setRX(PIN_SD_SPI_RX);
  SPI.setTX(PIN_SD_SPI_TX);
  SPI.setCS(PIN_SD_SPI_CS);
  SPI.begin();

  if (!sd.begin(SD_CONFIG)) {
    Serial.println("Error init SD card");
  }

  if (!sd.exists("karabas/boot.bit")) {
    Serial.println("File karabas/boot.bit does not exists");
  }

  if (bitstream.open("karabas/boot.bit", FILE_READ)) {
    while(bitstream.available()) {
      // TODO: config spartan
      char c = bitstream.read();
      Serial.print(c);
    }
    bitstream.close();
  }

  // I2C
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.begin();

  //delay(2000);
}

uint8_t counter = 0;

void loop()
{
  delay(100);
  Serial.println(counter);
  counter++;
}

// core1's setup
void setup1() {
  while ( !Serial ) delay(10);   // wait for native usb
  Serial.println("Core1 setup to run TinyUSB host with pio-usb");

  // Check for CPU frequency, must be multiple of 120Mhz for bit-banging USB
  uint32_t cpu_hz = clock_get_hz(clk_sys);
  if ( cpu_hz != 120000000UL && cpu_hz != 240000000UL ) {
    while ( !Serial ) delay(10);   // wait for native usb
    Serial.printf("Error: CPU Clock = %u, PIO USB require CPU clock must be multiple of 120 Mhz\r\n", cpu_hz);
    Serial.printf("Change your CPU Clock to either 120 or 240 Mhz in Menu->CPU Speed \r\n", cpu_hz);
    while(1) delay(1);
  }

  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  USBHost.configure_pio_usb(1, &pio_cfg);

  // run host stack on controller (rhport) 1
  // Note: For rp2040 pico-pio-usb, calling USBHost.begin() on core1 will have most of the
  // host bit-banging processing works done in core1 to free up core0 for other works
  USBHost.begin(1);
}

// core1's loop
void loop1()
{
  USBHost.task();
}

//--------------------------------------------------------------------+
// TinyUSB Host callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted (configured)
void tuh_hid_mount_cb (uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  Serial.printf("Device attached, address = %d\r\n", dev_addr);

  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  Serial.printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);

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
  Serial.printf("Device removed, address = %d\r\n", dev_addr);
}

//#################

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

Serial.print('.');

  switch (itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      //TU_LOG2("HID receive boot keyboard report\r\n");
      Serial.println("HID receive boot keyboard report");
      process_kbd_report( (hid_keyboard_report_t const*) report );
    break;

    case HID_ITF_PROTOCOL_MOUSE:
      //TU_LOG2("HID receive boot mouse report\r\n");
      Serial.println("HID receive boot mouse report");
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

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
  for(uint8_t i=0; i<6; i++)
  {
    if (report->keycode[i] == keycode)  return true;
  }

  return false;
}

static void process_kbd_report(hid_keyboard_report_t const *report)
{
  static hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released

  //------------- example code ignore control (non-printable) key affects -------------//
  for(uint8_t i=0; i<6; i++)
  {
    if ( report->keycode[i] )
    {
      if ( find_key_in_report(&prev_report, report->keycode[i]) )
      {
        // exist in previous report means the current key is holding
      } else
      {
        // not existed in previous report means the current key is pressed
        bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
        uint8_t ch = keycode2ascii[report->keycode[i]][is_shift ? 1 : 0];
        Serial.print(ch);
        if ( ch == '\r' ) Serial.print('\n'); // added new line for enter key

        fflush(stdout); // flush right away, else nanolib will wait for newline
      }
    }
    // TODO example skips key released
  }

  prev_report = *report;
}

static void process_mouse_report(hid_mouse_report_t const * report) {
  static hid_mouse_report_t prev_report = { 0 };

  uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
  /*if ( button_changed_mask & report->buttons)
  {
    printf(" %c%c%c ",
       report->buttons & MOUSE_BUTTON_LEFT   ? 'L' : '-',
       report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-',
       report->buttons & MOUSE_BUTTON_RIGHT  ? 'R' : '-');
  }*/

  prev_report = *report;

  Serial.printf("X: %d, Y: %d, Z: %d, B: %d", report->x, report->y, report->wheel, report->buttons);
}

static void process_gamepad_report(hid_gamepad_report_t const * report) {
  static hid_gamepad_report_t prev_report = { 0 };

  uint8_t hat_changed_mask = report->hat ^ prev_report.hat;
  uint8_t buttons_changed_mask = report->buttons ^ prev_report.buttons;
  if ( (hat_changed_mask & report->hat) || (buttons_changed_mask & report->buttons)) {
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
        Serial.println("HID receive keyboard report");
        // Assume keyboard follow boot report layout
        process_kbd_report( (hid_keyboard_report_t const*) report );
      break;
      case HID_USAGE_DESKTOP_MOUSE:
        //TU_LOG1("HID receive mouse report\r\n");
        Serial.println("HID receive mouse report");
        // Assume mouse follow boot report layout
        process_mouse_report( (hid_mouse_report_t const*) report );
      break;
      case HID_USAGE_DESKTOP_GAMEPAD:
        //TU_LOG1("HID receive gamepad report\r\n");
        Serial.println("HID receive gamepad report");
        // Assume gamepad follow boot report layout
        process_gamepad_report( (hid_gamepad_report_t const*) report );
      break;
      default: break;
    }
  }
}