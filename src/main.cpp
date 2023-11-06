#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <PioSPI.h>
#include <SparkFun_PCA9536_Arduino_Library.h>
#include <DS3231.h>
#include "SdFat.h"
#include "Adafruit_TinyUSB.h"
#include "pio_usb.h"

#define HOST_PIN_DP   25 // pin 37 // gpio 25  

#define PIN_MCU_SPI_SCK 22 // pin 35
#define PIN_MCU_SPI_CS 21 // pin 34
#define PIN_MCU_SPI_RX 20 // pin 32
#define PIN_MCU_SPI_TX 23 // pin 31

#define PIN_SD_SPI_SCK 9 // 10 // pin 12
#define PIN_SD_SPI_CS 8 // 9 // pin 11
#define PIN_SD_SPI_RX 11 // 8 // pin 14
#define PIN_SD_SPI_TX 10 // 11 // pin 13
#define SD_CS_PIN PIN_SD_SPI_CS

#define PIN_I2C_SDA 12 // pin 15
#define PIN_I2C_SCL 13 // pin 16

#define PIN_JOY_SCK 5 // pin 7
#define PIN_JOY_DATA 6 // pin 8
#define PIN_JOY_LOAD 4 // pin 6
#define PIN_JOY_P7 1 // pin 3

#define PIN_CONF_INIT_B 3 // pin 5
#define PIN_CONF_PRG_B 16 // pin 27
#define PIN_CONF_IO1 18 // pin 29
#define PIN_CONF_CLK 17 // pin 28
#define PIN_CONF_DONE 19 // pin 30

#define PIN_EXT_BTN1 0
#define PIN_EXT_BTN2 1
#define PIN_EXT_LED1 2
#define PIN_EXT_LED2 3

PioSPI sd_spi(PIN_SD_SPI_TX, PIN_SD_SPI_RX, PIN_SD_SPI_SCK, PIN_SD_SPI_CS, SPI_MODE0, SD_SCK_MHZ(20));

class MySpiClass : public SdSpiBaseClass {
 public:
  // Activate SPI hardware with correct speed and mode.
  void activate() {
    sd_spi.beginTransaction(m_spiSettings);
  }
  // Initialize the SPI bus.
  void begin(SdSpiConfig config) {
    (void)config;
    sd_spi.begin();
  }
  // Deactivate SPI hardware.
  void deactivate() {
    sd_spi.endTransaction();
  }
  // Receive a byte.
  uint8_t receive() {
    return sd_spi.transfer(0XFF);
  }
  // Receive multiple bytes.
  // Replace this function if your board has multiple byte receive.
  uint8_t receive(uint8_t* buf, size_t count) {
    for (size_t i = 0; i < count; i++) {
      buf[i] = sd_spi.transfer(0XFF);
    }
    return 0;
  }
  // Send a byte.
  void send(uint8_t data) {
    sd_spi.transfer(data);
  }
  // Send multiple bytes.
  // Replace this function if your board has multiple byte send.
  void send(const uint8_t* buf, size_t count) {
    for (size_t i = 0; i < count; i++) {
      sd_spi.transfer(buf[i]);
    }
  }
  // Save SPISettings for new max SCK frequency
  void setSckSpeed(uint32_t maxSck) {
    m_spiSettings = SPISettings(maxSck, MSBFIRST, SPI_MODE0);
  }

 private:
  SPISettings m_spiSettings;
} mySpi;

#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(10), &mySpi)

SdFs sd;
FsFile file;

#define MAX_REPORT  4

static uint8_t const keycode2ascii[128][2] =  { HID_KEYCODE_TO_ASCII };

// Each HID instance can has multiple reports
static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
}hid_info[CFG_TUH_HID];

typedef struct {
  tusb_desc_device_t desc_device;
  uint16_t manufacturer[32];
  uint16_t product[48];
  uint16_t serial[16];
  bool mounted;
} dev_info_t;

// CFG_TUH_DEVICE_MAX is defined by tusb_config header
dev_info_t dev_info[CFG_TUH_DEVICE_MAX] = { 0 };

PCA9536 extender;
DS3231 rtc_clock;

static void process_kbd_report(hid_keyboard_report_t const *report);
static void process_mouse_report(hid_mouse_report_t const * report);
static void process_gamepad_report(hid_gamepad_report_t const * report);
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

// Language ID: English
#define LANGUAGE_ID 0x0409

// USB Host object
Adafruit_USBH_Host USBHost;

// holding device descriptor
tusb_desc_device_t desc_device;

// the setup function runs once when you press reset or power the board
void setup()
{
  pinMode(PIN_SD_SPI_CS, OUTPUT_12MA); digitalWrite(PIN_SD_SPI_CS, HIGH);
  pinMode(PIN_SD_SPI_SCK, OUTPUT_12MA); digitalWrite(PIN_SD_SPI_SCK, HIGH);
  pinMode(PIN_SD_SPI_RX, INPUT_PULLUP);
  pinMode(PIN_SD_SPI_TX, OUTPUT_12MA); digitalWrite(PIN_SD_SPI_TX, HIGH);

  while ( !Serial ) delay(10);   // wait for native usb

  Serial.begin(115200);
  Serial.println("Karabas Go rp2040 core");

  // SPI0 to FPGA
  SPI.setSCK(PIN_MCU_SPI_SCK);
  SPI.setRX(PIN_MCU_SPI_RX);
  SPI.setTX(PIN_MCU_SPI_TX);
  SPI.setCS(PIN_MCU_SPI_CS);
  SPI.begin(PIN_MCU_SPI_CS);

  // I2C
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.begin();

  if (extender.begin() == false)
  {
    Serial.println("PCA9536 not detected. Please check wiring. Freezing...");
    while (true)
    ;
  }

  extender.pinMode(PIN_EXT_BTN1, INPUT_PULLUP);
  extender.pinMode(PIN_EXT_BTN2, INPUT_PULLUP);
  extender.pinMode(PIN_EXT_LED1, OUTPUT);
  extender.pinMode(PIN_EXT_LED2, OUTPUT);

  // SD
  Serial.println("Mounting SD card");
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorPrint(&Serial);
  }

  sd.ls(&Serial, LS_SIZE);

  if (!sd.exists("karabas.bit")) {
    Serial.println("karabas.bit does not exists on SD card");
  }

  if (!file.open("karabas.bit", FILE_READ)) {
    Serial.println("Unable to open file to read");
  }

  Serial.println("Reading karabas.bit");
  file.rewind();

  int i = 0;
  bool blink = false;

  while (file.available()) {
    char line[512];
    size_t n = file.readBytes(line, sizeof(line));
    i +=n;
    if (i % 4096 == 0) {
      blink = !blink;
      extender.digitalWrite(PIN_EXT_LED1, blink);
      extender.digitalWrite(PIN_EXT_LED2, blink);
    }
  }
  file.close();

  Serial.print("Total "); Serial.print(i, DEC); Serial.println(" bytes");
  Serial.flush();

  Serial.println("Done");
}

uint8_t counter = 0;

void loop()
{
  Serial.print("Counter: "); Serial.println(counter);
  counter++;

  extender.digitalWrite(2, counter %2 == 0);
  extender.digitalWrite(3, counter %2 != 0);

  if (extender.digitalRead(0) == LOW) {
    Serial.println("Pushed button 0");
    delay(100);
  }
  if (extender.digitalRead(1) == LOW) {
    Serial.println("Pushed button 1");
    delay(100);
  }

  bool century;
  bool h12flag;
  bool pmflag;

  Serial.print("RTC: ");
  Serial.print(rtc_clock.getYear(), DEC); Serial.print(" ");
  Serial.print(rtc_clock.getMonth(century), DEC); Serial.print(" ");
	Serial.print(rtc_clock.getDate(), DEC); Serial.print(" ");
	Serial.print(rtc_clock.getDoW(), DEC); Serial.print(" ");
	Serial.print(rtc_clock.getHour(h12flag, pmflag), DEC);
	Serial.print(" "); Serial.print(rtc_clock.getMinute(), DEC);
	Serial.print(" "); Serial.println(rtc_clock.getSecond(), DEC);

  delay(1000);
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
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
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
  Serial.flush();
}

//--------------------------------------------------------------------+
// TinyUSB Host callbacks
//--------------------------------------------------------------------+

void print_device_descriptor(tuh_xfer_t *xfer);

void utf16_to_utf8(uint16_t *temp_buf, size_t buf_len);

void print_lsusb(void) {
  bool no_device = true;
  for (uint8_t daddr = 1; daddr < CFG_TUH_DEVICE_MAX + 1; daddr++) {
    // TODO can use tuh_mounted(daddr), but tinyusb has an bug
    // use local connected flag instead
    dev_info_t *dev = &dev_info[daddr - 1];
    if (dev->mounted) {
      Serial.printf("Device %u: ID %04x:%04x %s %s\r\n", daddr,
                    dev->desc_device.idVendor, dev->desc_device.idProduct,
                    (char *) dev->manufacturer, (char *) dev->product);

      no_device = false;
    }
  }

  if (no_device) {
    Serial.println("No device connected (except hub)");
  }
}

// Invoked when device is mounted (configured)
void tuh_mount_cb(uint8_t daddr) {
  Serial.printf("Device attached, address = %d\r\n", daddr);

  dev_info_t *dev = &dev_info[daddr - 1];
  dev->mounted = true;

  // Get Device Descriptor
  tuh_descriptor_get_device(daddr, &dev->desc_device, 18, print_device_descriptor, 0);
}

/// Invoked when device is unmounted (bus reset/unplugged)
void tuh_umount_cb(uint8_t daddr) {
  Serial.printf("Device removed, address = %d\r\n", daddr);
  dev_info_t *dev = &dev_info[daddr - 1];
  dev->mounted = false;

  // print device summary
  print_lsusb();
}

void print_device_descriptor(tuh_xfer_t *xfer) {
  if (XFER_RESULT_SUCCESS != xfer->result) {
    Serial.printf("Failed to get device descriptor\r\n");
    return;
  }

  uint8_t const daddr = xfer->daddr;
  dev_info_t *dev = &dev_info[daddr - 1];
  tusb_desc_device_t *desc = &dev->desc_device;

  Serial.printf("Device %u: ID %04x:%04x\r\n", daddr, desc->idVendor, desc->idProduct);
  Serial.printf("Device Descriptor:\r\n");
  Serial.printf("  bLength             %u\r\n"     , desc->bLength);
  Serial.printf("  bDescriptorType     %u\r\n"     , desc->bDescriptorType);
  Serial.printf("  bcdUSB              %04x\r\n"   , desc->bcdUSB);
  Serial.printf("  bDeviceClass        %u\r\n"     , desc->bDeviceClass);
  Serial.printf("  bDeviceSubClass     %u\r\n"     , desc->bDeviceSubClass);
  Serial.printf("  bDeviceProtocol     %u\r\n"     , desc->bDeviceProtocol);
  Serial.printf("  bMaxPacketSize0     %u\r\n"     , desc->bMaxPacketSize0);
  Serial.printf("  idVendor            0x%04x\r\n" , desc->idVendor);
  Serial.printf("  idProduct           0x%04x\r\n" , desc->idProduct);
  Serial.printf("  bcdDevice           %04x\r\n"   , desc->bcdDevice);

  // Get String descriptor using Sync API
  Serial.printf("  iManufacturer       %u     ", desc->iManufacturer);
  if (XFER_RESULT_SUCCESS ==
      tuh_descriptor_get_manufacturer_string_sync(daddr, LANGUAGE_ID, dev->manufacturer, sizeof(dev->manufacturer))) {
    utf16_to_utf8(dev->manufacturer, sizeof(dev->manufacturer));
    Serial.printf((char *) dev->manufacturer);
  }
  Serial.printf("\r\n");

  Serial.printf("  iProduct            %u     ", desc->iProduct);
  if (XFER_RESULT_SUCCESS ==
      tuh_descriptor_get_product_string_sync(daddr, LANGUAGE_ID, dev->product, sizeof(dev->product))) {
    utf16_to_utf8(dev->product, sizeof(dev->product));
    Serial.printf((char *) dev->product);
  }
  Serial.printf("\r\n");

  Serial.printf("  iSerialNumber       %u     ", desc->iSerialNumber);
  if (XFER_RESULT_SUCCESS ==
      tuh_descriptor_get_serial_string_sync(daddr, LANGUAGE_ID, dev->serial, sizeof(dev->serial))) {
    utf16_to_utf8(dev->serial, sizeof(dev->serial));
    Serial.printf((char *) dev->serial);
  }
  Serial.printf("\r\n");

  Serial.printf("  bNumConfigurations  %u\r\n", desc->bNumConfigurations);

  // print device summary
  print_lsusb();
}

//--------------------------------------------------------------------+
// String Descriptor Helper
//--------------------------------------------------------------------+

static void _convert_utf16le_to_utf8(const uint16_t *utf16, size_t utf16_len, uint8_t *utf8, size_t utf8_len) {
  // TODO: Check for runover.
  (void) utf8_len;
  // Get the UTF-16 length out of the data itself.

  for (size_t i = 0; i < utf16_len; i++) {
    uint16_t chr = utf16[i];
    if (chr < 0x80) {
      *utf8++ = chr & 0xff;
    } else if (chr < 0x800) {
      *utf8++ = (uint8_t) (0xC0 | (chr >> 6 & 0x1F));
      *utf8++ = (uint8_t) (0x80 | (chr >> 0 & 0x3F));
    } else {
      // TODO: Verify surrogate.
      *utf8++ = (uint8_t) (0xE0 | (chr >> 12 & 0x0F));
      *utf8++ = (uint8_t) (0x80 | (chr >> 6 & 0x3F));
      *utf8++ = (uint8_t) (0x80 | (chr >> 0 & 0x3F));
    }
    // TODO: Handle UTF-16 code points that take two entries.
  }
}

// Count how many bytes a utf-16-le encoded string will take in utf-8.
static int _count_utf8_bytes(const uint16_t *buf, size_t len) {
  size_t total_bytes = 0;
  for (size_t i = 0; i < len; i++) {
    uint16_t chr = buf[i];
    if (chr < 0x80) {
      total_bytes += 1;
    } else if (chr < 0x800) {
      total_bytes += 2;
    } else {
      total_bytes += 3;
    }
    // TODO: Handle UTF-16 code points that take two entries.
  }
  return total_bytes;
}

void utf16_to_utf8(uint16_t *temp_buf, size_t buf_len) {
  size_t utf16_len = ((temp_buf[0] & 0xff) - 2) / sizeof(uint16_t);
  size_t utf8_len = _count_utf8_bytes(temp_buf + 1, utf16_len);

  _convert_utf16le_to_utf8(temp_buf + 1, utf16_len, (uint8_t *) temp_buf, buf_len);
  ((uint8_t *) temp_buf)[utf8_len] = '\0';
}

// Invoked when device is mounted (configured)
void tuh_hid_mount_cb (uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  (void)desc_report;
  (void)desc_len;

  Serial.printf("Device attached, address = %d\r\n", dev_addr);

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