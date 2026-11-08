#include "main.h"
#include "at_args.h"
#include "file_name.h"
#include "at_cmd.h"
#include "periph/at_control.h"
#include "periph/serial_bridge.h"
#include "apps/app_core.h"
#include "apps/app_core_browser.h"
#include "fpga/core_file.h"
#include "input/input.h"
#include <ctype.h>
#include <strings.h>

// a move report for 10 ms (the rate of a USB mouse); a change of the buttons for longer, cores
// poll them
#define MOUSE_STEP_US 10000
#define MOUSE_BUTTON_US 60000
#define PAD_PRESS_US 100000
#define PAD_RELEASE_US 40000
#define SYNC_TIMEOUT_MS 30000

static uint8_t mouse_held = 0;
static uint16_t pad_held[2] = {0, 0};
static bool sync_waiting = false;
static uint32_t sync_start;

static const char *after(const char *arg, const char *prefix) {
  size_t n = strlen(prefix);
  return strncasecmp(arg, prefix, n) == 0 ? arg + n : NULL;
}

static void reply(bool ok) {
  serial_host_print(ok ? "OK\r\n" : "ERROR\r\n");
}

// ---- the mouse and the pads ----

static bool at_mouse(const char *s) {
  kg_at_mouse_t m;
  if (!kg_at_mouse_parse(s, &m)) return false;
  uint8_t buttons = m.buttons == KG_AT_MOUSE_KEEP ? mouse_held : (uint8_t) m.buttons;
  int steps = kg_at_mouse_steps(m.dx, m.dy);
  if (hid_ext_inject_free() < steps) return false;
  int dx = m.dx, dy = m.dy;
  for (int i = 0; i < steps; i++) {
    int8_t x = kg_at_mouse_step(&dx), y = kg_at_mouse_step(&dy);
    bool first = i == 0;
    hid_mouse_inject(x, y, first ? m.wheel : 0, buttons,
                     first && buttons != mouse_held ? MOUSE_BUTTON_US : MOUSE_STEP_US);
  }
  mouse_held = buttons;
  return true;
}

static bool at_click(const char *name) {
  uint8_t mask;
  if (!kg_at_mouse_button(name, &mask) || hid_ext_inject_free() < 2) return false;
  hid_mouse_inject(0, 0, 0, mouse_held | mask, MOUSE_BUTTON_US);
  hid_mouse_inject(0, 0, 0, mouse_held, MOUSE_BUTTON_US);
  return true;
}

// <port>:<buttons> [<buttons>...]: pressed and released one after the other; hold: one, kept down
static bool at_pad(const char *s, bool hold) {
  if ((s[0] != '0' && s[0] != '1') || s[1] != ':') return false;
  uint8_t port = s[0] - '0';
  s += 2;
  uint16_t words[KG_AT_MAX_ARG];
  char name[KG_AT_MAX_ARG + 1];
  int n = 0;
  while (*s) {
    while (*s == ' ') s++;
    if (!*s) break;
    size_t len = strcspn(s, " ");
    memcpy(name, s, len);
    name[len] = '\0';
    if (!kg_at_pad_parse(name, &words[n])) return false;
    n++;
    s += len;
  }
  if (n == 0 || (hold && n != 1) || hid_ext_inject_free() < 2 * n) return false;
  for (int i = 0; i < n; i++) {
    hid_pad_inject(port, words[i], PAD_PRESS_US);
    if (!hold) hid_pad_inject(port, 0, PAD_RELEASE_US);
  }
  pad_held[port] = hold ? words[0] : 0;
  return true;
}

void at_control_release() {
  if (mouse_held) {
    mouse_held = 0;
    hid_mouse_inject(0, 0, 0, 0, MOUSE_BUTTON_US);
  }
  for (uint8_t i = 0; i < 2; i++) {
    if (pad_held[i]) {
      pad_held[i] = 0;
      hid_pad_inject(i, 0, PAD_RELEASE_US);
    }
  }
}

void at_control_tick() {
  if (!sync_waiting) return;
  bool idle = hid_inject_idle();
  if (!idle && ms_since(sync_start) < SYNC_TIMEOUT_MS) return;
  sync_waiting = false;
  reply(idle);
}

// ---- the cores ----

// the core with this file name, or else this id: its file and type
static bool core_find(const char *what, char *file, size_t size, uint8_t *type) {
  static DIR root;
  if (!has_sd || !sd_opendir(&root, "/")) return false;
  FILINFO fi;
  FIL *f;
  bool found = false;
  while (f_readdir(&root, &fi) == FR_OK && fi.fname[0]) {
    if ((fi.fattrib & AM_DIR) || !core_file_is_core(fi.fname)) continue;
    if ((f = sd_tmp_open(fi.fname, FA_READ)) == NULL) continue;
    core_file_t cf;
    core_list_item_t c;
    bool ok = core_file_open(f, fi.fname, &cf); // a .kg of other boards is left out
    if (ok) core_file_info(f, &cf, &c);
    sd_tmp_close();
    if (!ok || strlen(fi.fname) >= size) continue;
    bool name = strcasecmp(fi.fname, what) == 0;
    if (name || (!found && strcasecmp(c.id, what) == 0)) {
      strcpy(file, fi.fname);
      *type = c.type;
      found = true;
      if (name) break;
    }
  }
  sd_closedir(&root);
  return found;
}

static void reboot_with_ok() {
  reply(true);
  serial_host_flush();
  flush_and_reboot();
}

static void at_load(const char *what) {
  char file[sizeof(core.filename)];
  uint8_t type;
  if (!core_find(what, file, sizeof(file), &type)) { reply(false); return; }
  if (type == CORE_TYPE_BOOT) { reboot_with_ok(); return; }
  app_core_browser_start_file(file);
  reply(true);
}

static void at_reset() {
  if (core.type == CORE_TYPE_BOOT) { reboot_with_ok(); return; }
  app_core_leave_browser();
  do_reload();
  reply(true);
}

// ---- the menu items of the core ----

static bool item_usable(uint8_t i) {
  const core_osd_t *o = &core.osd[i];
  return o->type == CORE_OSD_TYPE_TRIGGER || core_osd_is_file(i) ||
         ((o->type == CORE_OSD_TYPE_SWITCH || o->type == CORE_OSD_TYPE_NSWITCH) && o->options_len > 0);
}

static int item_find(const char *s) {
  if (s[0] == '#') {
    int pos = atoi(s + 1);
    return isdigit((unsigned char) s[1]) && pos < core.osd_len && item_usable(pos) ? pos : -1;
  }
  for (uint8_t i = 0; i < core.osd_len; i++) {
    if (core.osd[i].key[0] && item_usable(i) && strcasecmp(core.osd[i].key, s) == 0) return i;
  }
  for (uint8_t i = 0; i < core.osd_len; i++) {
    if (item_usable(i) && strcasecmp(core.osd[i].name, s) == 0) return i;
  }
  return -1;
}

static const char *item_kind(uint8_t type) {
  switch (type) {
    case CORE_OSD_TYPE_SWITCH: return "switch";
    case CORE_OSD_TYPE_NSWITCH: return "nswitch";
    case CORE_OSD_TYPE_TRIGGER: return "trigger";
    case CORE_OSD_TYPE_FILEMOUNTER: return "image";
    default: return "loader";
  }
}

// +KARABAS:item;<pos>;<key>;<name>;<kind>;<enabled>;<value number>;<value>;<options or extensions>
static void print_item(uint8_t i) {
  const core_osd_t *o = &core.osd[i];
  char line[96];
  snprintf(line, sizeof(line), "+KARABAS:item;%u;%s;%s;%s;%u;", i, o->key, o->name, item_kind(o->type),
           core_osd_enabled(i) ? 1 : 0);
  serial_host_print(line);
  if (core_osd_is_file(i)) {
    const core_file_slot_t *fs = &file_slots[o->slot_id];
    serial_host_print("-;");
    if (fs->is_mounted) {
      if (fs->filename[0] != ':') { // ":phys0": a physical drive, not in a dir
        serial_host_print(fs->dir);
        if (strcmp(fs->dir, "/") != 0) serial_host_print("/");
      }
      serial_host_print(fs->filename);
    }
    serial_host_print(";");
    serial_host_print(fs->ext);
  } else if (o->type == CORE_OSD_TYPE_TRIGGER) {
    serial_host_print("-;;");
  } else {
    snprintf(line, sizeof(line), "%u;%s;", o->val, o->val < o->options_len ? o->options[o->val].name : "");
    serial_host_print(line);
    for (uint8_t n = 0; n < o->options_len; n++) {
      if (n) serial_host_print("|");
      serial_host_print(o->options[n].name);
    }
  }
  serial_host_print("\r\n");
}

static void redraw() {
  if (is_osd) osd_handle(true);
}

static void at_state() {
  static const char *const screens[] = {"main", "setup", "about", "core_browser", "file_loader", "pad_mapper",
                                        "rtc", "wifi", "settings", "ft_setup", "msc"};
  const char *type = core.type == CORE_TYPE_BOOT ? "boot" : core.type == CORE_TYPE_OSD ? "osd" :
                     core.type == CORE_TYPE_FILELOADER ? "fileloader" : "hidden";
  char line[160];
  snprintf(line, sizeof(line), "+KARABAS:core;%s;%s;%s;%s;%s\r\n", core.filename, core.id, core.name, core.build, type);
  serial_host_print(line);
  snprintf(line, sizeof(line), "+KARABAS:osd;%u;%s;%u\r\n", is_osd ? 1 : 0,
           osd_state < sizeof(screens) / sizeof(screens[0]) ? screens[osd_state] : "?",
           app_core_in_browser() ? 1 : 0);
  serial_host_print(line);
  for (uint8_t i = 0; i < core.osd_len; i++) {
    if (core_osd_is_file(i)) print_item(i);
  }
  reply(true);
}

static bool set_option(uint8_t pos, const char *value) {
  const core_osd_t *o = &core.osd[pos];
  int n = -1;
  if (isdigit((unsigned char) value[0])) {
    n = atoi(value);
  } else {
    for (uint8_t i = 0; i < o->options_len && n < 0; i++) {
      if (strcasecmp(o->options[i].name, value) == 0) n = i;
    }
  }
  if (n < 0 || n >= o->options_len) return false;
  core_osd_set(pos, n);
  if (core.osd_need_save) app_core_save(pos);
  return true;
}

// <item>, <item>=<value>
static bool at_osd(const char *s) {
  char name[KG_AT_MAX_ARG + 1];
  const char *eq = strchr(s, '=');
  size_t len = eq ? (size_t) (eq - s) : strlen(s);
  memcpy(name, s, len);
  name[len] = '\0';
  int pos = item_find(name);
  if (pos < 0) return false;
  if (!eq) { print_item(pos); return true; }
  if (core_osd_is_file(pos) || !core_osd_enabled(pos)) return false;
  if (core.osd[pos].type == CORE_OSD_TYPE_TRIGGER) {
    if (strcmp(eq + 1, "1") != 0) return false;
    core_trigger(pos);
  } else if (!set_option(pos, eq + 1)) {
    return false;
  }
  redraw();
  return true;
}

// <item>:<path>
static bool at_mount(const char *s) {
  char name[KG_AT_MAX_ARG + 1];
  const char *colon = strchr(s, ':');
  if (!colon) return false;
  memcpy(name, s, colon - s);
  name[colon - s] = '\0';
  int pos = item_find(name);
  if (pos < 0 || !app_core_mount_path(pos, colon + 1)) return false;
  redraw();
  return true;
}

static bool at_eject(const char *s) {
  int pos = item_find(s);
  if (pos < 0 || core.osd[pos].type != CORE_OSD_TYPE_FILEMOUNTER) return false;
  app_core_eject_item(pos);
  redraw();
  return true;
}

bool at_control_command(const char *arg) {
  const char *v;
  if (strcasecmp(arg, "state") == 0) at_state();
  else if (strcasecmp(arg, "reset") == 0) at_reset();
  else if ((v = after(arg, "load:"))) at_load(v);
  else if (strcasecmp(arg, "osd") == 0) {
    for (uint8_t i = 0; i < core.osd_len; i++) {
      if (item_usable(i)) print_item(i);
    }
    reply(true);
  }
  else if ((v = after(arg, "osd:"))) reply(at_osd(v));
  else if ((v = after(arg, "mount:"))) reply(at_mount(v));
  else if ((v = after(arg, "eject:"))) reply(at_eject(v));
  else if ((v = after(arg, "mouse:"))) reply(at_mouse(v));
  else if (strcasecmp(arg, "click") == 0) reply(at_click(""));
  else if ((v = after(arg, "click:"))) reply(at_click(v));
  else if ((v = after(arg, "pad:"))) reply(at_pad(v, false));
  else if ((v = after(arg, "padhold:"))) reply(at_pad(v, true));
  else if ((v = after(arg, "debug:"))) {
    bool ok = strcmp(v, "0") == 0 || strcmp(v, "1") == 0;
    if (ok) hw_setup.debug_enabled = v[0] == '1';
    reply(ok);
  }
  else if (strcasecmp(arg, "sync") == 0) {
    if (sync_waiting) { reply(false); return true; }
    sync_waiting = true;
    sync_start = ms_now();
  }
  else return false;
  return true;
}
