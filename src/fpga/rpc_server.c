// The services of the firmware for the calls of a core over link v2 RPC (target 08,
// docs/link_v2.md 5.9). A request is served where link_v2_service() finds it (the APP loop), one at
// a time; the core waits for the response. A core start (CORES LAUNCH) waits for rpc_server_tick().
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "types.h"
#include "main.h"
#include "fpga/link_api.h"
#include "fpga/rpc_server.h"
#include "storage/settings.h"
#include "fpga/core_list.h"
#include "storage/msc.h"
#include "rtc.h"
#include "fpga/core_file.h"
#include "usb/hid_app.h"
#include "usb/hid_driver.h"
#include "apps/pad_learn.h"
#include "hid_learn.h"
#include "periph/matrix.h"
#include "ini_read.h"
#include "ini_update.h"
#include "matrix_style.h"
#include <stdlib.h>

enum { SVC_SYS = 0, SVC_CFG = 1, SVC_CORES = 2, SVC_HID = 3 };
enum { ST_OK = 0, ST_UNKNOWN = 1, ST_ARGS = 2, ST_NOT_FOUND = 3, ST_IO = 4, ST_DENIED = 5, ST_BUSY = 6 };
#define RPC_SERVICES ((1 << SVC_SYS) | (1 << SVC_CFG) | (1 << SVC_CORES) | (1 << SVC_HID))

static link_rpc_req_t req;
static uint8_t out[LINK_RPC_MAX];
static char launch_file[33] = "";

// a menu core: the boot core, or a core that asked for CORES in its HELLO (a test build of the menu
// under another name, started by AT+KARABAS=load, which reboots into boot.kg for a boot type)
static bool menu_core(void) {
  return core.type == CORE_TYPE_BOOT || (link_rpc_services() & (1 << SVC_CORES)) != 0;
}

bool rpc_boot_menu(void) {
  return core.type == CORE_TYPE_BOOT && link_has(LINK_CAP_RPC);
}

// the NUL separated strings of a payload: returns the k-th, NULL past the end
static const char *arg(const link_rpc_req_t *r, int k) {
  uint16_t p = 0;
  for (int i = 0; i < k; i++) {
    while (p < r->len && r->data[p]) p++;
    if (p >= r->len) return NULL;
    p++;
  }
  return p < r->len ? (const char *) r->data + p : NULL;
}

// appends a string with its NUL; false when it does not fit
static bool put_str(uint16_t *n, const char *s) {
  size_t l = strlen(s) + 1;
  if (*n + l > sizeof(out)) return false;
  memcpy(out + *n, s, l);
  *n += l;
  return true;
}

static uint8_t sys(uint8_t fn, uint16_t *n) {
  switch (fn) {
    case 0: { // INFO: the firmware build (8), board, RPC version, the services (2)
      static const char fw_build[] = STRINGIZE_VALUE_OF(BUILD_VER);
      memset(out, 0, 8);
      memcpy(out, fw_build, strlen(fw_build) < 8 ? strlen(fw_build) : 8);
      out[8] = HW_ID;
      out[9] = 1;
      out[10] = RPC_SERVICES & 0xFF;
      out[11] = RPC_SERVICES >> 8;
      *n = 12;
      return ST_OK;
    }
    case 1: { // TIME_GET: year (2), month, day, hour, minute, second, weekday
      if (!rtc_has_clock()) return ST_NOT_FOUND;
      uint16_t y = 2000 + rtc_get_year();
      out[0] = y & 0xFF; out[1] = y >> 8;
      out[2] = rtc_get_month(); out[3] = rtc_get_day();
      out[4] = rtc_get_hour(); out[5] = rtc_get_minute(); out[6] = rtc_get_second();
      out[7] = rtc_get_week();
      *n = 8;
      return ST_OK;
    }
    case 2: { // TIME_SET: as TIME_GET
      if (req.len < 8) return ST_ARGS;
      if (!rtc_has_clock()) return ST_NOT_FOUND;
      uint16_t y = req.data[0] | (req.data[1] << 8);
      if (y < 2000 || y > 2099) return ST_ARGS;
      rtc_set_year(y - 2000);
      rtc_set_month(req.data[2]); rtc_set_day(req.data[3]);
      rtc_set_hour(req.data[4]); rtc_set_minute(req.data[5]); rtc_set_second(req.data[6]);
      rtc_set_week(req.data[7]);
      rtc_save();
      return ST_OK;
    }
    default:
      return ST_UNKNOWN;
  }
}

// CFG: file 0 = karabas.ini (a menu core only). GET takes several section / key pairs (one read of
// the file), SET several section / key / value triples (one rewrite), SAVE takes the settings into
// the firmware (load_setup, the matrix as saved), PREVIEW shows [matrix] values on the LED matrix
// without writing them
#define CFG_ITEMS 40
static kg_ini_item_t cfg_items[CFG_ITEMS];

// the [matrix] values of a payload over a copy of the settings
static void matrix_values(setup_t *s, int from) {
  for (int k = from; arg(&req, k) && arg(&req, k + 1) && arg(&req, k + 2); k += 3) {
    const char *section = arg(&req, k), *key = arg(&req, k + 1), *v = arg(&req, k + 2);
    if (strcmp(section, "matrix") != 0) continue;
    if (!strcmp(key, "enabled")) s->matrix_enabled = v[0] == '1';
    else if (!strcmp(key, "brightness")) { unsigned b = strtoul(v, 0, 10); if (b <= 16) s->matrix_brightness = b; }
    else if (!strcmp(key, "meter_peak")) s->matrix_style.meter_peak = v[0] == '1';
    else for (uint8_t i = 0; i < KG_MX_OPT_COUNT; i++) {
      if (strcmp(key, kg_mx_opt_key(i)) != 0) continue;
      int o = kg_mx_opt_parse(i, v);
      if (o >= 0) s->matrix_style.opt[i] = o;
    }
  }
}

static uint8_t cfg(uint8_t fn, uint16_t *n) {
  const char *file = arg(&req, 0);
  if (!file || file[0] != '0' || file[1] != 0) return ST_DENIED; // the own ini of a core: later (FS)
  if (!menu_core()) return ST_DENIED;
  switch (fn) {
    case 0: { // GET (section, key)... -> per pair: 1 + value, or 0 + "" (not in the file)
      if (!arg(&req, 2)) return ST_ARGS;
      size_t len;
      char *text = settings_ini_text(&len);
      if (!text) return ST_IO;
      uint8_t st = ST_OK;
      for (int k = 1; arg(&req, k) && arg(&req, k + 1); k += 2) {
        char v[128];
        bool found = kg_ini_get(text, len, arg(&req, k), arg(&req, k + 1), v, sizeof(v));
        if (*n + 1 > sizeof(out)) { st = ST_ARGS; break; }
        out[(*n)++] = found ? 1 : 0;
        if (!put_str(n, found ? v : "")) { st = ST_ARGS; break; }
      }
      free(text);
      return st;
    }
    case 1: { // SET (section, key, value)...
      uint8_t count = 0;
      for (int k = 1; arg(&req, k) && arg(&req, k + 1) && arg(&req, k + 2); k += 3) {
        if (count == CFG_ITEMS) return ST_ARGS;
        cfg_items[count++] = (kg_ini_item_t) { arg(&req, k), arg(&req, k + 1), arg(&req, k + 2), false };
      }
      if (!count) return ST_ARGS;
      return settings_ini_set_items(cfg_items, count) ? ST_OK : ST_IO;
    }
    case 2: // SAVE
      load_setup();
      matrix_setup(&hw_setup);
      matrix_preview(MATRIX_PREVIEW_NONE);
      return ST_OK;
    case 3: { // PREVIEW mode ("none", "clock", "meter"), (section, key, value)...: none of them = back
      const char *mode = arg(&req, 1);
      if (!mode) return ST_ARGS;
      setup_t s = hw_setup;
      matrix_values(&s, 2);
      matrix_setup(&s);
      matrix_preview(!strcmp(mode, "clock") ? MATRIX_PREVIEW_CLOCK : !strcmp(mode, "meter") ? MATRIX_PREVIEW_METER : MATRIX_PREVIEW_NONE);
      return ST_OK;
    }
    default:
      return ST_UNKNOWN;
  }
}

static uint8_t cores_svc(uint8_t fn, uint16_t *n) {
  if (!menu_core()) return ST_DENIED;
  switch (fn) {
    case 0: // SCAN -> count (2), in the order of [setup] core_sort
      core_list_read();
      out[0] = cores_len; out[1] = 0;
      *n = 2;
      return ST_OK;
    case 1: { // ITEM index (2) -> file name, name, build, id, flags (NUL separated)
      if (req.len < 2) return ST_ARGS;
      uint16_t i = req.data[0] | (req.data[1] << 8);
      if (i >= cores_len) return ST_NOT_FOUND;
      const core_list_item_t *c = &cores[i];
      char flags[4];
      snprintf(flags, sizeof(flags), "%u", (unsigned) ((c->visible ? 1 : 0) | (c->type << 1)));
      put_str(n, c->filename); put_str(n, c->name); put_str(n, c->build); put_str(n, c->id); put_str(n, flags);
      return ST_OK;
    }
    case 2: { // LAUNCH file name: started by rpc_server_tick (the core is replaced, no response read)
      const char *f = arg(&req, 0);
      if (!f || !f[0] || strlen(f) >= sizeof(launch_file) || !core_file_is_core(f)) return ST_ARGS;
      strcpy(launch_file, f);
      return ST_OK;
    }
    case 3: { // AUTOLOAD -> timeout (s), file name ("" = none): once after the MCU start, not after a
              // return to the menu (the user asked for the menu)
      static bool given = false;
      bool on = hw_setup.autoload_enabled && !given;
      given = true;
      out[0] = on ? hw_setup.autoload_timeout : 0;
      *n = 1;
      const char *file = "";
      for (uint8_t i = 0; i < cores_len; i++)
        if (on && strcmp(cores[i].id, hw_setup.autoload_core) == 0) file = cores[i].filename;
      put_str(n, file);
      return ST_OK;
    }
    default:
      return ST_UNKNOWN;
  }
}

// HID (a menu core): the USB pads of the two joystick ports and the learning of a pad
// (apps/pad_learn, as Setup > Gamepad of the FT812 menu did), driven and drawn by the core
static uint8_t hid_svc(uint8_t fn, uint16_t *n) {
  if (!menu_core()) return ST_DENIED;
  switch (fn) {
    case 0: { // PADS -> per port: source, VID (2), PID (2), driver file
      usb_pad_t pads[2];
      usb_pads_get(pads);
      for (uint8_t i = 0; i < 2; i++) {
        out[(*n)++] = pads[i].source;
        out[(*n)++] = pads[i].vid & 0xFF; out[(*n)++] = pads[i].vid >> 8;
        out[(*n)++] = pads[i].pid & 0xFF; out[(*n)++] = pads[i].pid >> 8;
        put_str(n, pads[i].source == USB_PAD_DRIVER && pads[i].file ? pads[i].file : "");
      }
      return ST_OK;
    }
    case 1: { // FORGET port: the driver ini of its pad becomes .bak
      if (req.len < 1 || req.data[0] > 1) return ST_ARGS;
      usb_pad_t pads[2];
      usb_pads_get(pads);
      return hid_driver_forget(pads[req.data[0]].vid, pads[req.data[0]].pid) ? ST_OK : ST_IO;
    }
    case 2: // LEARN_START
      pl_start();
      return ST_OK;
    case 3: { // LEARN_STATE -> state, button, last button, seconds left, VID (2), PID (2), mapped (2),
              // held (2), the rule of the last button, the saved file
      out[0] = pl_state(); out[1] = pl_btn(); out[2] = (uint8_t) pl_last_btn(); out[3] = (uint8_t) pl_press_left_s();
      out[4] = pl_vid() & 0xFF; out[5] = pl_vid() >> 8; out[6] = pl_pid() & 0xFF; out[7] = pl_pid() >> 8;
      uint16_t m = 0;
      for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) if (pl_mapped(i)) m |= 1 << i;
      out[8] = m & 0xFF; out[9] = m >> 8;
      uint16_t live = 0;
      for (uint8_t i = 0; i < HID_JOY_BUTTONS; i++) if (pl_live() & hid_joy_btns[i]) live |= 1 << i;
      out[10] = live & 0xFF; out[11] = live >> 8;
      *n = 12;
      char r[24] = "";
      if (pl_last_btn() >= 0 && pl_mapped(pl_last_btn())) hid_learn_format_rule(pl_rule(pl_last_btn()), r, sizeof(r));
      put_str(n, r);
      put_str(n, pl_saved_path());
      return ST_OK;
    }
    case 4: // LEARN_DO: 0 stop, 1 skip, 2 back, 3 save
      if (req.len < 1) return ST_ARGS;
      switch (req.data[0]) {
        case 0: pl_stop(); break;
        case 1: pl_skip(); break;
        case 2: pl_back(); break;
        case 3: pl_save(); break;
        default: return ST_ARGS;
      }
      return ST_OK;
    default:
      return ST_UNKNOWN;
  }
}

static void on_request(void) {
  if (!link_rpc_read(&req)) return;
  uint16_t n = 0;
  uint8_t st;
  // the PC has the card (USB mass storage): no file of it is read or written meanwhile
  if (msc_active() && (req.svc == SVC_CFG || req.svc == SVC_CORES || req.svc == SVC_HID)) st = ST_BUSY;
  else switch (req.svc) {
    case SVC_SYS: st = sys(req.fn, &n); break;
    case SVC_CFG: st = cfg(req.fn, &n); break;
    case SVC_CORES: st = cores_svc(req.fn, &n); break;
    case SVC_HID: st = hid_svc(req.fn, &n); break;
    default: st = ST_UNKNOWN;
  }
  if (hw_setup.debug_enabled) {
    d_printf("RPC %u: service %u function %u, %u bytes -> status %u, %u bytes", req.id, req.svc, req.fn, req.len, st, n);
    d_println();
  }
  if (launch_file[0]) return; // the core goes away: its response is not read
  link_rpc_respond(req.id, st, out, n);
}

void rpc_server_init(void) {
  link_rpc_on_request(on_request);
}

void rpc_server_tick(void) {
  if (pl_state() != PL_IDLE && menu_core()) pl_tick();   // the learning of a pad (HID LEARN_*)
  if (!launch_file[0]) return;
  char f[sizeof(launch_file)];
  strcpy(f, launch_file);
  launch_file[0] = 0;
  d_printf("RPC: the boot menu starts %s", f); d_println();
  core_list_start_file(f);
}
