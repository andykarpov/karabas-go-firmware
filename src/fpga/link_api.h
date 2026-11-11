#pragma once

// What the firmware says to the running core and hears from it, by meaning, not by frame.
// The services (input, loaders, image mounter, ports, RTC, matrix) use only this; the frames of
// link v1 (docs/protocol.md) are made in link_api.c on top of the transport of link.c. Link v2
// (docs/link_v2.md) will be a second implementation of the same calls.
//
// Context rules are the ones of the link: calls may come from any task (they take the bus lock),
// the link_hid_queue_*() calls also from core1 and from interrupts (they only queue).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- capabilities of the loaded core ----

typedef enum {
  LINK_CAP_BK,       // backup RAM: the core asks for its save, the firmware flushes it
  LINK_CAP_MIDI,     // the core takes the MIDI IN of a USB MIDI keyboard
  LINK_CAP_IMG_WP,   // the core knows write protected images
  LINK_CAP_BURST,    // fast data transfers (v1: bursts)
} link_cap_t;

bool link_has(link_cap_t cap);
const char *link_mode_name(void); // for the log: "burst" / "classic"

// a core load: before the bitstream (nothing of the old core reaches the new one, no fast
// transfers until the new core says so), after it (waits until the core answers, up to 100 ms;
// false: an old core that never answers), and when its configuration is done
void link_core_unload(void);
bool link_core_hello(void);
void link_core_ready(void);
void link_core_freq(uint8_t mhz); // the SPI clock of the core file, 0 = default

// lets a core that has something to say talk now (v1: a NOP frame)
void link_poll_once(void);

// ---- SYS ----

void link_sys_init(bool start);                    // true: configuration starts (busy), false: done
void link_sys_hw_setup(uint8_t key, uint8_t value); // LINK_HW_* below
void link_sys_build_ids(const char *core_build, const char *fw_build); // 8 characters each, 0 padded
void link_sys_setval(uint8_t index, uint8_t value); // an OSD switch
void link_sys_btn(uint8_t button, bool pressed);    // an on-board button with the action "core"
void link_sys_spi_control(uint8_t bits);            // FT812 / SD2 / ESP routing (CMD_SPI_CONTROL bits)

#define LINK_HW_BOARD 0
#define LINK_HW_DVI_ONLY 1

// the core asks to be loaded again (it runs in the dispatch task)
void link_sys_on_reload(void (*cb)(uint8_t core_id));

// ---- HID ----

// events of core1 (USB HID, PS/2, MIDI) for the INPUT task, which forwards them unless the OSD
// takes the keys
typedef enum {
  LINK_EV_KBD,   // idx 0 = modifiers, 1..6 = key codes
  LINK_EV_MOUSE, // idx 0 = x, 1 = y, 2 = wheel, 3 = buttons
  LINK_EV_PS2,   // idx 0 = set 2 byte, 1 = XT byte (bit 7 release)
  LINK_EV_MIDI,  // idx = byte counter, val = MIDI byte
} link_event_kind_t;

typedef struct {
  uint8_t kind; // link_event_kind_t
  uint8_t idx;
  uint8_t val;
} link_event_t;

void link_hid_queue(link_event_kind_t kind, uint8_t idx, uint8_t val);
bool link_hid_queue_pop(link_event_t *ev);
void link_hid_forward(const link_event_t *ev);   // to the core now
void link_hid_joy(uint8_t port, uint16_t bits);  // port 0 left, 1 right
void link_hid_matrix_btn(uint8_t button, bool pressed);

// PS/2 commands of the core to the keyboard (ED = LEDs, F0 = scancode set)
void link_hid_on_ps2_cmd(void (*cb)(uint8_t cmd, uint8_t val));

// ---- LOAD: ROMs, the file loader, the ioctl loader ----

typedef enum {
  LINK_LOAD_ROM,    // the ROMs of the core file
  LINK_LOAD_FILE,   // the file loader (fileloader cores)
  LINK_LOAD_IOCTL,  // an FL item
} link_load_kind_t;

// ioctl: slot, size and extension (lowercase, without the dot); the others ignore them
void link_load_start(link_load_kind_t kind, uint8_t slot, uint64_t size, const char *ext);
void link_load_data(link_load_kind_t kind, uint32_t addr, const uint8_t *buf, size_t len);
void link_load_end(link_load_kind_t kind);

// ---- IMG: the file mounter, backup RAM, physical floppy drives ----

#define LINK_SECTOR_SIZE 512

typedef struct {
  uint8_t slot;
  uint8_t op;          // IMG_REQ_READ / IMG_REQ_WRITE
  uint32_t lba;
  bool complete;       // the request arrived whole (v1: all four LBA bytes)
  uint32_t start_us;   // when it started
} link_img_req_t;

void link_img_begin(void);                                  // at start: take the requests of the core
void link_img_mount(uint8_t slot, uint64_t size, bool wp); // size 0: eject
void link_img_mount_phys(uint8_t slot, uint8_t drive);     // drive 0 = DS0, 1 = DS1 (after an eject)
void link_img_reset(void);                                  // forget a half-received request

bool link_img_waiting(void);                     // a request started or is waiting to be served
bool link_img_take(link_img_req_t *req);         // the waiting request (pulls a started one first)
void link_img_expire(uint32_t timeout_us);       // drop a request that started but never ended
void link_img_send_sector(const uint8_t *buf);   // the data of a read request
bool link_img_receive_sector(uint8_t *buf);      // the data of a write request; false: data lost
void link_img_done(uint8_t slot, uint8_t res);   // IMG_RES_*

void link_img_flush(uint8_t slot);               // backup RAM: write the dirty sectors now
void link_img_on_flush(void (*cb)(uint8_t slot, uint8_t result)); // IMG_RES_DONE / IMG_RES_ERROR
// the core asks for its save file: size in sectors, the img slot, the kind (0 = .sav)
void link_img_on_bk_mount(void (*cb)(uint32_t sectors, uint8_t slot, uint8_t kind));
// DSKCHG of a physical drive (1 = DS0, 2 = DS1) while the core selects it: 1 changed / no disk,
// 0 disk in, -1 no line
void link_img_on_fdd_chng(int (*cb)(uint8_t drive));

// ---- PORT: byte streams ----

void link_uart_put(uint8_t byte);                // USB serial -> the core UART
void link_esp_put(uint8_t byte);                 // the ESP8266 behind the fpga
void link_printer_status(uint8_t status);        // bit 7 BUSY

// the core UART: kind 0 = a byte at 115200, 1 = DLL, 2 = DLM, 3 = a byte at the DLL / DLM speed
void link_uart_on_rx(void (*cb)(uint8_t kind, uint8_t byte));
void link_esp_on_rx(void (*cb)(uint8_t byte));
void link_printer_on_data(void (*cb)(uint8_t byte));
void link_printer_on_status_request(void (*cb)(void));

// ---- RTC ----

void link_rtc_set(uint8_t reg, uint8_t value);
void link_rtc_on_write(void (*cb)(uint8_t reg, uint8_t value)); // the CPU of the core wrote one

// ---- AUX: the LED matrix and the audio meters of the core ----

typedef enum {
  LINK_AUX_MATRIX_CTL,
  LINK_AUX_MATRIX_PIXEL,
  LINK_AUX_MATRIX_BITPLAN,
  LINK_AUX_PEAK_L,
  LINK_AUX_PEAK_R,
  LINK_AUX_COUNT
} link_aux_kind_t;

void link_aux_on(link_aux_kind_t kind, void (*cb)(uint8_t a, uint8_t b));

// ---- OSD (the text OSD of v1) ----

void link_osd_reg(uint8_t reg, uint8_t value);

#ifdef __cplusplus
}
#endif
