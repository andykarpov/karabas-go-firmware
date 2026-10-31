#include "main.h"
#include "tusb.h"
#include "hardware/watchdog.h"

// Expose an SD card as USB mass storage (AT+KARABAS=msc / msc2, the msc / msc2 button actions):
// the card of the MCU, or the second one (the slot of the FPGA, storage/sd2, through the boot core)

// watchdog scratch 0..3 survive a watchdog reboot (the bootrom uses 4..7)
#define MSC_REQUEST_MAGIC 0x4B47534D  // "MSGK": the first card
#define MSC2_REQUEST_MAGIC 0x4B47324D // "M2GK": the second card
#define MSC_FAILED_REBOOT_MS 5000     // no second card: the screen says so, then back to the menu
#define MSC_EJECT_REBOOT_MS 5000      // after an eject, at the latest

static bool expose_msc = false;
// read by the USB stack (USBD task): the SD card is ready for the host, its size and USB name
static volatile bool unit_ready = false;
static uint32_t block_count = 0;
static const char *product = "SD Card";
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

void msc_init() {
  // the PC asks the name when USB comes up, before msc_start(): the request is still in scratch 0
  if (watchdog_hw->scratch[0] == MSC2_REQUEST_MAGIC) product = "SD Card 2";
}

void msc_reboot_to_msc(uint8_t sd_card) {
  bk_flush();
  core_eeprom_flush();
  sd1_sync_before_reboot();
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
  unit_ready = true;
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

// The TinyUSB MSC callbacks, in the USBD task. The interface is there in normal mode too, with
// no medium (the unit is not ready), as with Adafruit's MSC class before.

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
  (void) lun;
  memcpy(vendor_id, "Karabas", 7);
  memcpy(product_id, product, strlen(product));
  memcpy(product_rev, "1.0", 3);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  if (!unit_ready) tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00); // medium not present
  return unit_ready;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *count, uint16_t *size) {
  (void) lun;
  *count = block_count;
  *size = 512;
}

// the SCSI commands TinyUSB does not answer itself
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
  (void) scsi_cmd; (void) buffer; (void) bufsize;
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00); // invalid command
  return -1;
}

// READ10: the sectors from lba, bufsize bytes (up to CFG_TUD_MSC_EP_BUFSIZE, whole sectors)
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  (void) lun; (void) offset;
  if (!expose_msc) return -1; // ejected: the card is being handed back to the firmware
  bool rc;
  if (card == 2) rc = sd2_read(lba, (uint8_t*) buffer, bufsize/512);
  else rc = sd_card_read(sd1_card(), lba, (uint8_t*) buffer, bufsize/512);
  return rc ? (int32_t) bufsize : -1;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  (void) lun; (void) offset;
  if (!expose_msc) return -1;
  bool rc;
  if (card == 2) rc = sd2_write(lba, buffer, bufsize/512);
  else rc = sd_card_write(sd1_card(), lba, buffer, bufsize/512);
  return rc ? (int32_t) bufsize : -1;
}

// the host has the status of the WRITE10
void tud_msc_write10_complete_cb(uint8_t lun) {
  (void) lun;
  if (card == 2) sd2_sync();
  else sd_card_sync(sd1_card());
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  (void) lun;
  d_printf("MSC: start/stop, power condition %u, start %u, eject %u", power_condition, start, load_eject);
  d_println();
  if (load_eject && !start && expose_msc) {
    // sd_card keeps a multi-sector read / write open between the calls: end it, or
    // the card is still in it after the reboot and the bitstream read of the boot core
    // stops half way (CONF_DONE timeout). The read / write callbacks run in this task too.
    expose_msc = false;
    unit_ready = false;
    if (card == 2) sd2_sync();
    else sd_card_sync(sd1_card());
    eject_ms = ms_now();
    ejected = true;
  }
  return true;
}

// the host let the device go
void tud_umount_cb(void) {
  d_println("USB: unmounted by the host");
  if (ejected) host_gone = true;
}

void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  d_println("USB: suspended by the host");
  if (ejected) host_gone = true;
}
