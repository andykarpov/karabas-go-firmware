#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "PioSpi.h"
#include <SparkFun_PCA9536_Arduino_Library.h>
#include "SdFat.h"
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "elapsed.h"
#include <RTC.h>
#include <OSD.h>
#include <SegaController.h>
#include "hid_app.h"
#include "main.h"

PioSpi spi(PIN_SD_SPI_RX, PIN_SD_SPI_SCK, PIN_SD_SPI_TX);
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(60), &spi)
SPISettings settingsA(SD_SCK_MHZ(10), MSBFIRST, SPI_MODE0); // MCU SPI settings

PCA9536 extender;
Elapsed my_timer;
RTC zxrtc;
SdFs sd;
FsFile file;
SegaController sega;
OSD zxosd;

static queue_t spi_event_queue;

hid_keyboard_report_t usb_keyboard_report;
hid_mouse_report_t usb_mouse_report;

// the setup function runs once when you press reset or power the board
void setup()
{
  queue_init(&spi_event_queue, sizeof(queue_spi_t), 64);

  // SPI0 to FPGA
  SPI.setSCK(PIN_MCU_SPI_SCK);
  SPI.setRX(PIN_MCU_SPI_RX);
  SPI.setTX(PIN_MCU_SPI_TX);
  SPI.setCS(PIN_MCU_SPI_CS);  
  SPI.begin(true);

  // FPGA bitstream loader
  pinMode(PIN_CONF_INIT_B, INPUT_PULLUP);
  pinMode(PIN_CONF_PRG_B, OUTPUT);
  pinMode(PIN_CONF_DONE, INPUT);
  pinMode(PIN_CONF_CLK, OUTPUT);
  pinMode(PIN_CONF_IO1, OUTPUT);

  // I2C
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.begin();

  while ( !Serial ) delay(10);   // wait for native usb

  Serial.begin(115200);
  Serial.println("Karabas Go RP2040 firmware");

  if (extender.begin() == false) {
    halt("PCA9536 unavailable. Please check soldering.");
  }
  Serial.println("I2C PCA9536 extender detected");

  extender.pinMode(PIN_EXT_BTN1, INPUT_PULLUP);
  extender.pinMode(PIN_EXT_BTN2, INPUT_PULLUP);
  extender.pinMode(PIN_EXT_LED1, OUTPUT);
  extender.pinMode(PIN_EXT_LED2, OUTPUT);

  zxrtc.begin(spi_send, on_time);
  sega.begin(PIN_JOY_SCK, PIN_JOY_LOAD, PIN_JOY_DATA, PIN_JOY_P7);
  zxosd.begin(spi_send);

  // SD
  Serial.print("Mounting SD card... ");
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
  }
  Serial.println("Done");

  //sd.ls(&Serial, LS_SIZE);

  fpga_configure("boot.kg1");

  delay(100);

  Serial.println("OSD test");
  zxosd.setPos(0,0);
  zxosd.setColor(OSD::COLOR_WHITE, OSD::COLOR_BLACK);
  zxosd.print("Hello Karabas Go");
}

uint8_t counter = 0;

void loop()
{
  zxrtc.handle();

  if (my_timer.elapsed_millis() >= 100) {
    counter++;
    extender.digitalWrite(2, counter %2 == 0);
    extender.digitalWrite(3, counter %2 != 0);
    my_timer.reset();

    zxosd.setPos(0,2);
    zxosd.setColor(OSD::COLOR_MAGENTA_I, OSD::COLOR_BLACK);
    zxosd.print("Counter: ");
    zxosd.setColor(OSD::COLOR_YELLOW_I, OSD::COLOR_BLACK);
    zxosd.print(counter);
    zxosd.print("     ");

    zxosd.setPos(0,4);
    zxosd.setColor(OSD::COLOR_GREEN_I, OSD::COLOR_BLACK);
    zxosd.print("Time: ");
    zxosd.setColor(OSD::COLOR_CYAN_I, OSD::COLOR_BLACK);
    zxosd.printf("%02d:%02d:%02d", zxrtc.getHour(), zxrtc.getMinute(), zxrtc.getSecond());
    zxosd.print("     ");

    zxosd.setPos(0,6);
    zxosd.setColor(OSD::COLOR_BLUE_I, OSD::COLOR_BLACK);
    zxosd.print("Kb: ");
    zxosd.setColor(OSD::COLOR_YELLOW_I, OSD::COLOR_BLACK);
    zxosd.printf("%02x %02x %02x %02x %02x %02x %02x"
    , usb_keyboard_report.modifier
    , usb_keyboard_report.keycode[0]
    , usb_keyboard_report.keycode[1]
    , usb_keyboard_report.keycode[2]
    , usb_keyboard_report.keycode[3]
    , usb_keyboard_report.keycode[4]
    , usb_keyboard_report.keycode[5]
    );
    zxosd.print("             ");
  }

  static bool prev_btn1 = HIGH;
  static bool prev_btn2 = HIGH;
  bool btn1 = extender.digitalRead(0);
  bool btn2 = extender.digitalRead(1);

  if (prev_btn1 != btn1) {
    Serial.print("Button 1: "); Serial.println((btn1 == LOW) ? "on" : "off");
    prev_btn1 = btn1;
    spi_send(CMD_BTN, 0, !btn1);
  }

  if (prev_btn2 != btn2) {
    Serial.print("Button 2: "); Serial.println((btn2 == LOW) ? "on" : "off");
    prev_btn2 = btn2;
    spi_send(CMD_BTN, 1, !btn2);
  }

  // joy reading
  uint16_t joyL = sega.getState(true);
  uint16_t joyR = sega.getState(false);
  static uint16_t prevJoyL = 0;
  static uint16_t prevJoyR = 0;

  if (joyL != prevJoyL) {
    Serial.printf("SEGA L: %u", joyL); Serial.println();
    prevJoyL = joyL;
    spi_send(CMD_JOYSTICK, 0, static_cast<uint8_t>(joyL & 0x00FF));
    spi_send(CMD_JOYSTICK, 1, static_cast<uint8_t>(joyL & 0xFF00) >> 8);
  }
  if (joyR != prevJoyR) {
    Serial.printf("SEGA R: %u", joyR); Serial.println();
    prevJoyR = joyR;
    spi_send(CMD_JOYSTICK, 2, static_cast<uint8_t>(joyR & 0x00FF));
    spi_send(CMD_JOYSTICK, 3, static_cast<uint8_t>(joyR & 0xFF00) >> 8);
  }

  queue_spi_t packet;
	while (queue_try_remove(&spi_event_queue, &packet)) {
    spi_send(packet.cmd, packet.addr, packet.data);
  }
}

// core1's setup
void setup1() {
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  tuh_init(1);
}

// core1's loop
void loop1()
{
   tuh_task();
}

/**
 * @brief Halt the program execution with message
 * 
 * @param msg 
 */
void halt(const char* msg) {
  Serial.println(msg);
  Serial.flush();
  bool blink = false;
  while(true) {
      blink = !blink;
      extender.digitalWrite(PIN_EXT_LED1, blink);
      extender.digitalWrite(PIN_EXT_LED2, blink);
      delay(100);
  }
}

/**
 * @brief FPGA bitstream uploader
 * 
 * @param filename 
 */
void fpga_configure(const char* filename) {

  Serial.print("Configuring FPGA by "); Serial.println(filename);

  if (!file.open(filename, FILE_READ)) {
    halt("Unable to open bitstream file to read");
  }

  // get bitstream size
  file.seek(80);
  uint8_t buf[4] = {0};
  uint32_t length = 0;
  file.readBytes(buf, sizeof(buf));
  length = buf[3] + buf[2]*256 + buf[1]*256*256 + buf[0]*256*256*256;
  Serial.print("Bitstream size: "); Serial.println(length, DEC);

  // seek to bitstream start
  file.seek(1024);

  // pulse PROG_B
  digitalWrite(PIN_CONF_PRG_B, HIGH);
  digitalWrite(PIN_CONF_PRG_B, LOW);
  digitalWrite(PIN_CONF_PRG_B, HIGH);

  // wait for INIT_B = 0
  delay(10);

  my_timer.reset();

  int i = 0;
  bool blink = false;
  char line[128];
  uint8_t n;

  digitalWrite(PIN_CONF_CLK, LOW);

  while ((n = file.read(line, sizeof(line) < length ? sizeof(line) : length ))) {
    i += n;
    length -=n;

    for (uint8_t s=0; s<n; s++) {
      uint8_t c = line[s];
      for (uint8_t j=0; j<8; ++j) {
        // Set bit of data
        gpio_put(PIN_CONF_IO1, (c & (1<<(7-j))) ? HIGH : LOW);
        // Latch bit of data by CCLK impulse
        gpio_put(PIN_CONF_CLK, HIGH);
        gpio_put(PIN_CONF_CLK, LOW);
      }
    }

    if (i % 8192 == 0) {
      blink = !blink;
      extender.digitalWrite(PIN_EXT_LED1, blink);
      extender.digitalWrite(PIN_EXT_LED2, blink);
    }
  }
  file.close();

  Serial.print(i, DEC); Serial.println(" bytes done");
  Serial.print("Elapsed time: "); Serial.print(my_timer.elapsed_millis(), DEC); Serial.println(" ms");
  Serial.flush();

  Serial.print("Waiting for CONF_DONE... ");
  while(digitalRead(PIN_CONF_DONE) == LOW) {
    delay(10);
  }
  Serial.println("Done");
}

void spi_queue(uint8_t cmd, uint8_t addr, uint8_t data) {
  queue_spi_t packet;
  packet.cmd = cmd;
  packet.addr = addr;
  packet.data = data;
  queue_try_add(&spi_event_queue, &packet);
}

/**
 * @brief send a custom message to MCU via hw SPI
 * 
 * @param cmd command
 * @param addr address
 * @param data data
 */
void spi_send(uint8_t cmd, uint8_t addr, uint8_t data) {
  //Serial.printf("SPI %d %d %d", cmd, addr, data); Serial.println();
  SPI.beginTransaction(settingsA);
  digitalWrite(PIN_MCU_SPI_CS, LOW);
  uint8_t rx_cmd = SPI.transfer(cmd);
  uint8_t rx_addr = SPI.transfer(addr);
  uint8_t rx_data = SPI.transfer(data);
  digitalWrite(PIN_MCU_SPI_CS, HIGH);
  SPI.endTransaction();
  if (rx_cmd > 0) {
    process_in_cmd(rx_cmd, rx_addr, rx_data);
  }
}

/**
 * @brief Process incoming command from FPGA
 * 
 * @param cmd command
 * @param addr address
 * @param data data
 */
void process_in_cmd(uint8_t cmd, uint8_t addr, uint8_t data) {
  // TODO
  // init request
  // etc
}

/**
 * @brief RTC callback
 * 
 */
void on_time() {
  // TODO 
  // display the current time via OSD
}

