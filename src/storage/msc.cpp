#include <Arduino.h>
#include "main.h"
#include "Adafruit_TinyUSB.h"
#include "hardware/watchdog.h"

// Expose an SD card as USB mass storage (AT+KARABAS=msc / msc2, the msc / msc2 button actions):
// the card of the MCU, or the second one (the slot of the FPGA, storage/sd2, through the boot core)

// watchdog scratch 0..3 survive a watchdog reboot (the bootrom uses 4..7)
#define MSC_REQUEST_MAGIC 0x4B47534D  // "MSGK": the first card
#define MSC2_REQUEST_MAGIC 0x4B47324D // "M2GK": the second card
#define MSC_FAILED_REBOOT_MS 5000     // no second card: the screen says so, then back to the menu
#define MSC_EJECT_REBOOT_MS 5000      // after an eject, at the latest

static Adafruit_USBD_MSC usb_msc;
static bool expose_msc = false;
static uint8_t card = 1;
static bool failed = false;
static uint32_t failed_timer = 0;
// The reboot after an eject waits until the host lets the device go (unmounted or suspended:
// Windows "Safely Remove" ends with the port disabled), or MSC_EJECT_REBOOT_MS (Linux, the
// Eject of the drive in Explorer). Gone at once, the device hung the Windows 10 removal (msc2).
static volatile bool ejected = false;   // set by the USBD task
static volatile uint32_t eject_ms = 0;
static volatile bool host_gone = false; // the host let the device go after the eject (USBD task)
static bool msc_blink = false;
static uint32_t msc_blink_timer = 0;

int32_t msc_read_cb_sd (uint32_t lba, void* buffer, uint32_t bufsize);
int32_t msc_write_cb_sd (uint32_t lba, uint8_t* buffer, uint32_t bufsize);
void msc_flush_cb_sd (void);
bool msc_start_stop_cb_sd(uint8_t power_condition, bool start, bool load_eject);

void msc_init() {
#if ENABLE_MSC
  // the PC asks the name when USB comes up, before msc_start(): the request is still in scratch 0
  usb_msc.setID(0, "Karabas", watchdog_hw->scratch[0] == MSC2_REQUEST_MAGIC ? "SD Card 2" : "SD Card", "1.0");
  usb_msc.setReadWriteCallback(0, msc_read_cb_sd, msc_write_cb_sd, msc_flush_cb_sd);
  usb_msc.setStartStopCallback(0, msc_start_stop_cb_sd);
  usb_msc.setUnitReady(0, false);
  usb_msc.begin();
#endif
}

void msc_reboot_to_msc(uint8_t sd_card) {
  bk_flush();
  matrix_icon(MATRIX_ICON_MSC);
  watchdog_hw->scratch[0] = sd_card == 2 ? MSC2_REQUEST_MAGIC : MSC_REQUEST_MAGIC;
  chip_reboot();
}

void msc_check_boot() {
  uint32_t request = watchdog_hw->scratch[0];
  watchdog_hw->scratch[0] = 0;
  card = request == MSC2_REQUEST_MAGIC ? 2 : 1;
#if ENABLE_MSC
  // the first card has the boot core, which the second one needs too
  expose_msc = (request == MSC_REQUEST_MAGIC || request == MSC2_REQUEST_MAGIC) && has_sd;
#endif
}

void msc_start() {
#if ENABLE_MSC
  if (!expose_msc) return;
  matrix_icon(MATRIX_ICON_MSC);
  ejected = false;
  uint32_t block_count;
  if (card == 2) {
    if (!sd2_begin()) {
      d_println("MSC: no second SD card");
      failed = true;
      failed_timer = ms_now();
      return;
    }
    block_count = sd2_sector_count();
  } else {
    block_count = sd_card_sectors(sd1_card());
  }
  usb_msc.setCapacity(0, block_count, 512);
  usb_msc.setUnitReady(0, true);
#endif
}

bool msc_active() {
  return expose_msc;
}

uint8_t msc_card() {
  return card;
}

bool msc_failed() {
  return failed;
}

// returns true while in msc mode (the rest of the main loop is skipped)
bool msc_tick() {
  // if eject event registered - reboot rp2040
  if (ejected && (host_gone || ms_now() - eject_ms >= MSC_EJECT_REBOOT_MS)) {
    d_println(host_gone ? "MSC: the host let the device go, reboot" : "MSC: reboot after the eject");
    d_flush();
    chip_reboot();
  }
  if (failed && ms_since(failed_timer) >= MSC_FAILED_REBOOT_MS) {
    chip_reboot();
  }

#if ENABLE_MSC
  if (expose_msc) {

    if (ms_since(msc_blink_timer) >= 200) {
          msc_blink = !msc_blink;
        led_write(1, msc_blink);
        msc_blink_timer = ms_now();
    }

    // do not process other loop things until exit from msc mode
    return true;
  }
  if (ejected) return true; // waits for the reboot
#endif
  return false;
}

#if ENABLE_MSC
// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_cb_sd (uint32_t lba, void* buffer, uint32_t bufsize)
{
  if (!expose_msc) return -1; // ejected: the card is being handed back to the firmware
  bool rc;
  if (card == 2) rc = sd2_read(lba, (uint8_t*) buffer, bufsize/512);
  else rc = sd_card_read(sd1_card(), lba, (uint8_t*) buffer, bufsize/512);
  return rc ? bufsize : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and 
// return number of written bytes (must be multiple of block size)
int32_t msc_write_cb_sd (uint32_t lba, uint8_t* buffer, uint32_t bufsize)
{
  if (!expose_msc) return -1;
  bool rc;
  if (card == 2) rc = sd2_write(lba, buffer, bufsize/512);
  else rc = sd_card_write(sd1_card(), lba, buffer, bufsize/512);
  return rc ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_cb_sd (void)
{
  if (card == 2) sd2_sync();
  else sd_card_sync(sd1_card());
}

bool msc_start_stop_cb_sd(uint8_t power_condition, bool start, bool load_eject) {
  d_printf("MSC: start/stop, power condition %u, start %u, eject %u", power_condition, start, load_eject);
  d_println();

  if ( load_eject )
  {
    if (start)
    {
      // load disk storage
    }else
    {
      // unload disk storage
      if (expose_msc) {
        // sd_card keeps a multi-sector read / write open between the calls: end it, or
        // the card is still in it after the reboot and the bitstream read of the boot core
        // stops half way (CONF_DONE timeout). The read / write callbacks run in this task too.
        expose_msc = false;
        usb_msc.setUnitReady(0, false);
        if (card == 2) sd2_sync();
        else sd_card_sync(sd1_card());
        eject_ms = ms_now();
        ejected = true;
      }
    }
  }

  return true;
}

// TinyUSB, in the USBD task: the host let the device go
extern "C" void tud_umount_cb(void) {
  d_println("USB: unmounted by the host");
  if (ejected) host_gone = true;
}

extern "C" void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  d_println("USB: suspended by the host");
  if (ejected) host_gone = true;
}
#endif

/////////
