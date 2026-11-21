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
#include "apps/app_core_browser.h"
#include "rtc.h"
#include "fpga/core_file.h"

enum { SVC_SYS = 0, SVC_CFG = 1, SVC_CORES = 2 };
enum { ST_OK = 0, ST_UNKNOWN = 1, ST_ARGS = 2, ST_NOT_FOUND = 3, ST_IO = 4, ST_DENIED = 5, ST_BUSY = 6 };
#define RPC_SERVICES ((1 << SVC_SYS) | (1 << SVC_CFG) | (1 << SVC_CORES))

static link_rpc_req_t req;
static uint8_t out[LINK_RPC_MAX];
static char launch_file[33] = "";

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
  return p <= r->len ? (const char *) r->data + p : NULL;
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

// CFG: file 0 = karabas.ini (the boot core only); SET writes the line at once, SAVE takes the
// settings into the firmware (load_setup)
static uint8_t cfg(uint8_t fn, uint16_t *n) {
  const char *file = arg(&req, 0), *section = arg(&req, 1), *key = arg(&req, 2);
  if (!file || file[0] != '0' || file[1] != 0) return ST_DENIED; // the own ini of a core: later (FS)
  if (core.type != CORE_TYPE_BOOT) return ST_DENIED;
  switch (fn) {
    case 0: { // GET section, key -> value
      if (!section || !key) return ST_ARGS;
      char v[128];
      if (!settings_ini_get(section, key, v, sizeof(v))) return ST_NOT_FOUND;
      put_str(n, v);
      return ST_OK;
    }
    case 1: { // SET section, key, value
      const char *value = arg(&req, 3);
      if (!section || !key || !value) return ST_ARGS;
      return settings_ini_set(section, key, value) ? ST_OK : ST_IO;
    }
    case 2: // SAVE
      load_setup();
      return ST_OK;
    default:
      return ST_UNKNOWN;
  }
}

static uint8_t cores_svc(uint8_t fn, uint16_t *n) {
  if (core.type != CORE_TYPE_BOOT) return ST_DENIED;
  switch (fn) {
    case 0: // SCAN -> count (2), in the order of [setup] core_sort
      app_core_browser_read_list();
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
    case 3: { // AUTOLOAD -> timeout (s), file name ("" = none)
      out[0] = hw_setup.autoload_enabled ? hw_setup.autoload_timeout : 0;
      *n = 1;
      const char *file = "";
      for (uint8_t i = 0; i < cores_len; i++)
        if (hw_setup.autoload_enabled && strcmp(cores[i].id, hw_setup.autoload_core) == 0) file = cores[i].filename;
      put_str(n, file);
      return ST_OK;
    }
    default:
      return ST_UNKNOWN;
  }
}

static void on_request(void) {
  if (!link_rpc_read(&req)) return;
  uint16_t n = 0;
  uint8_t st;
  switch (req.svc) {
    case SVC_SYS: st = sys(req.fn, &n); break;
    case SVC_CFG: st = cfg(req.fn, &n); break;
    case SVC_CORES: st = cores_svc(req.fn, &n); break;
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
  if (!launch_file[0]) return;
  char f[sizeof(launch_file)];
  strcpy(f, launch_file);
  launch_file[0] = 0;
  d_printf("RPC: the boot menu starts %s", f); d_println();
  app_core_browser_start_file(f);
}
