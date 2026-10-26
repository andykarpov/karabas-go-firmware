#include "main.h"
#include "esp_at_parse.h"
#include "periph/esp_update.h"

// AT+CIUPDATE: +CIPUPDATE:1..4, then OK after the download (about 26 s in all on a module
// tried), and the module restarts into the new firmware: its boot log at 74880 baud, "ready".
// Then AT until it answers, ATE0 (the echo is on again after the restart) and AT+GMR for the
// new version. ERROR, or an error stage, leaves the old firmware. AT+RESTORE restarts the
// module the same way after its OK.

enum { S_IDLE, S_UPDATE, S_RESTART, S_PING, S_ECHO, S_VERSION };

#define UPDATE_TIMEOUT_MS 300000
#define RESTART_WAIT_MS 2000  // the boot log is not read
#define PING_TIMEOUT_MS 30000 // after the OK of the update
#define PING_MS 500

static uint8_t step = S_IDLE;
static uint8_t result = ESP_UPD_IDLE;
static const char *message;
static uint8_t stage;       // KG_ESP_UPD_*
static int error_stage;     // a negative +CIPUPDATE
static ElapsedTimer stage_timer;
static ElapsedTimer restart_timer;
static char version[16];
static bool restore;        // AT+RESTORE, not the update

static const char *const stage_text[] = {
  "Asking the update server...",
  "Server found",
  "Connected to the server",
  "Got the new version",
  "Downloading the firmware...",
  "The module restarts...",
};

static void set_stage(uint8_t s) {
  stage = s;
  stage_timer.reset();
  message = stage_text[s];
}

static void on_update(const char *line) {
  int st;
  if (!kg_esp_parse_ciupdate(line, &st)) return;
  if (st < 0) error_stage = st;
  else if (st >= 1 && st <= KG_ESP_UPD_DOWNLOAD && st > stage) set_stage(st);
}

static void on_version(const char *line) { kg_esp_parse_version(line, version, sizeof(version)); }

static void finish(uint8_t r, const char *m) {
  step = S_IDLE;
  result = r;
  message = m;
  if (r == ESP_UPD_OK) stage = KG_ESP_UPD_DONE;
  d_printf("ESP update: %s %s", m, r == ESP_UPD_OK ? version : ""); d_println();
}

static void go(uint8_t s) {
  step = s;
  switch (s) {
    case S_UPDATE:
      if (restore) esp_at_start("AT+RESTORE", 5000, nullptr);
      else esp_at_start("AT+CIUPDATE", UPDATE_TIMEOUT_MS, on_update);
      break;
    case S_RESTART: set_stage(KG_ESP_UPD_RESTART); restart_timer.reset(); break;
    case S_PING:    esp_at_start("AT", PING_MS, nullptr); break;
    case S_ECHO:    esp_at_start("ATE0", 1000, nullptr); break;
    case S_VERSION: version[0] = '\0'; esp_at_start("AT+GMR", 2000, on_version); break;
  }
}

static void start(bool r) {
  restore = r;
  version[0] = '\0';
  error_stage = 0;
  result = ESP_UPD_BUSY;
  set_stage(KG_ESP_UPD_SENT);
  if (restore) message = "Erasing the settings...";
  go(S_UPDATE);
}

void esp_update_start() { start(false); }
void esp_restore_start() { start(true); }

void esp_update_cancel() {
  step = S_IDLE;
  result = ESP_UPD_IDLE;
  message = nullptr;
}

uint8_t esp_update_poll() {
  if (step == S_IDLE) return result;
  if (step == S_RESTART) {
    if (restart_timer.elapsed() >= RESTART_WAIT_MS) go(S_PING);
    return result;
  }
  uint8_t r = esp_at_poll();
  if (r == ESP_AT_BUSY) return result;
  switch (step) {
    case S_UPDATE:
      if (r == ESP_AT_OK && error_stage == 0) go(S_RESTART);
      else if (r == ESP_AT_TIMEOUT || r == ESP_AT_IDLE) finish(ESP_UPD_ERROR, "No answer from the ESP");
      else finish(ESP_UPD_ERROR, restore ? "The module did not erase them" : "Update failed, the firmware is unchanged");
      break;
    case S_PING:
      if (r == ESP_AT_OK) go(S_ECHO);
      else if (restart_timer.elapsed() >= PING_TIMEOUT_MS) finish(ESP_UPD_ERROR, "The module did not come back");
      else go(S_PING);
      break;
    case S_ECHO:
      go(S_VERSION);
      break;
    case S_VERSION:
      finish(ESP_UPD_OK, restore ? "The settings are erased" : "The module is updated");
      break;
  }
  return result;
}

uint8_t esp_update_progress() {
  return kg_esp_update_progress(stage, stage_timer.elapsed());
}

const char *esp_update_message() {
  return message;
}

const char *esp_update_version() {
  return version;
}
