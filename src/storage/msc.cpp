#include "main.h"
#include "Adafruit_TinyUSB.h"
#include "hardware/watchdog.h"

// Expose the SD card as USB mass storage (AT+KARABAS=msc, the msc button action)

// watchdog scratch 0..3 survive a watchdog reboot (the bootrom uses 4..7)
#define MSC_REQUEST_MAGIC 0x4B47534D // "MSGK"

static Adafruit_USBD_MSC usb_msc;
static bool expose_msc = false;
static bool ejected = false;
static bool msc_blink = false;
static ElapsedTimer msc_blink_timer;

int32_t msc_read_cb_sd (uint32_t lba, void* buffer, uint32_t bufsize);
int32_t msc_write_cb_sd (uint32_t lba, uint8_t* buffer, uint32_t bufsize);
void msc_flush_cb_sd (void);
bool msc_start_stop_cb_sd(uint8_t power_condition, bool start, bool load_eject);

void msc_init() {
#if ENABLE_MSC
  usb_msc.setID(0, "Karabas", "SD Card", "1.0");
  usb_msc.setReadWriteCallback(0, msc_read_cb_sd, msc_write_cb_sd, msc_flush_cb_sd);
  usb_msc.setStartStopCallback(0, msc_start_stop_cb_sd);
  usb_msc.setUnitReady(0, false);
  usb_msc.begin();
#endif
}

void msc_reboot_to_msc() {
  bk_flush();
  watchdog_hw->scratch[0] = MSC_REQUEST_MAGIC;
  rp2040.reboot();
}

void msc_check_boot() {
  bool requested = (watchdog_hw->scratch[0] == MSC_REQUEST_MAGIC);
  watchdog_hw->scratch[0] = 0;
#if ENABLE_MSC
  expose_msc = requested && has_sd;
#endif
}

void msc_start() {
#if ENABLE_MSC
  if (!expose_msc) return;
  ejected = false;
  uint32_t block_count = sd1.card()->sectorCount();
  usb_msc.setCapacity(0, block_count, 512);
  usb_msc.setUnitReady(0, true);
#endif
}

bool msc_active() {
  return expose_msc;
}

// returns true while in msc mode (the rest of the main loop is skipped)
bool msc_tick() {
  // if eject event registered - reboot rp2040
  if (ejected) {
    ejected = false;
    rp2040.reboot();
  }

#if ENABLE_MSC
  if (expose_msc) {

    if (msc_blink_timer.elapsed() >= 200) {
          msc_blink = !msc_blink;
        led_write(1, msc_blink);
        msc_blink_timer.reset();
    }

    // do not process other loop things until exit from msc mode
    return true;
  }
#endif
  return false;
}

#if ENABLE_MSC
// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_cb_sd (uint32_t lba, void* buffer, uint32_t bufsize)
{
  bool rc;
  rc = sd1.card()->readSectors(lba, (uint8_t*) buffer, bufsize/512);
  return rc ? bufsize : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and 
// return number of written bytes (must be multiple of block size)
int32_t msc_write_cb_sd (uint32_t lba, uint8_t* buffer, uint32_t bufsize)
{
  bool rc;
  rc = sd1.card()->writeSectors(lba, buffer, bufsize/512);
  return rc ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_cb_sd (void)
{
  sd1.card()->syncDevice();
  //sd1.cacheClear();
}

bool msc_start_stop_cb_sd(uint8_t power_condition, bool start, bool load_eject) {
  //d_printf("Start stop msc %d %d %d", power_condition, start, load_eject);
  //d_println(); d_flush();
  (void) power_condition;

  if ( load_eject )
  {
    if (start)
    {
      // load disk storage
    }else
    {
      // unload disk storage
      if (expose_msc) {
        expose_msc = false;
        ejected = true;
      }
    }
  }

  return true;
}
#endif

/////////
