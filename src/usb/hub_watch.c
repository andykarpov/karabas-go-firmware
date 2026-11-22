#include "main.h"
#include "usb/hub_watch.h"
#include <string.h>
#include "tusb.h"
#include "host/hcd.h"
#include "host/hub.h"
#include "host/usbh_pvt.h"
#include "hardware/structs/timer.h"

// Where TinyUSB 0.18 stops for good (src/host/usbh.c, hub.c of the pico-sdk):
// - process_enumeration() tries a failed transfer 3 times, then ends the enumeration: the device
//   stays on an enabled hub port, and nothing starts it again but a new connection (a replug);
// - a transfer the device NAKs for ever (PIO-USB tries again every frame) never ends, and a
//   TU_ASSERT in the enumeration returns without ending it: _dev0.enumerating stays set and the
//   attach of every other device waits behind it;
// - a failed control transfer of the hub driver (TU_ASSERT in its callbacks) or a port change it
//   does not handle leaves its status pipe not armed: no more port events from that hub.
// The firmware cannot change the pico-sdk, so it follows the ports from outside, through
// -Wl,--wrap (CMakeLists.txt): hcd_event_handler (the attach / remove events of the hub driver),
// hub_port_get_status (called by usbh.c only when the enumeration of a hub port starts), hub_open /
// hub_close (the hubs and their status endpoint). The calls inside one object file are not wrapped,
// so the hub driver's own calls of hub_port_get_status are not seen, as wanted.
//
// The restart of an enumeration does what the hub driver does for a new connection: a remove (the
// half enumerated device and its address go), a port reset, and the attach once the reset was
// taken; an attach for the port being enumerated makes usbh.c drop that enumeration and start
// again. The hub status pipe is stopped for it, as the hub driver keeps it stopped through an
// enumeration: else the hub driver would clear the reset change the enumeration waits for. The own
// control transfers are sent only with the event queue empty, from the USB task, between the
// tuh_task() calls; a refused one (the control pipe busy) is tried again in the next step. The
// queue is not empty for good while an attach waits behind a stopped enumeration (usbh.c puts it
// back), so a queue that stayed so for HW_QUEUE_STEPS steps does not hold the steps back.

void __real_hcd_event_handler(hcd_event_t const *event, bool in_isr);
bool __real_hub_port_get_status(uint8_t hub_addr, uint8_t hub_port, void *resp, tuh_xfer_cb_t complete_cb,
                                uintptr_t user_data);
bool __real_hub_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_interface_t const *itf_desc, uint16_t max_len);
void __real_hub_close(uint8_t dev_addr);

#define HW_PORTS 7        // ports followed per hub (the FE1.1s has 4)
#define HW_ENUM_MS 5000   // from the start of an enumeration to the mount
#define HW_TRIES 3        // enumerations started again per connection
#define HW_PIPE_MS 3000   // a hub status pipe not armed while none of its ports enumerates
#define HW_STEP_MS 100
#define HW_BUSY_STEPS 20  // steps a refused control transfer is tried again
#define HW_QUEUE_STEPS 3  // steps with events queued: a deferred attach, not one to handle first
#define HUB_ADDR0 (CFG_TUH_DEVICE_MAX + 1) // the addresses of the hubs follow the devices

typedef enum { PORT_EMPTY = 0, PORT_ENUM, PORT_MOUNTED, PORT_FAILED } port_state_t;
static const char *const port_names[] = {"empty", "enumerating", "mounted", "failed"};

typedef struct {
  uint8_t state;   // port_state_t
  uint8_t tries;   // enumerations started again for this connection
  uint8_t daddr;   // its device, once known
  uint32_t since;  // ms: the attach or the enumeration start
} port_t;

typedef struct {
  uint8_t ep_in;      // its status endpoint; 0: no hub at this address
  bool stopped;       // the status pipe stopped by a job
  uint32_t armed_ms;  // the last step its status pipe was armed (or a port of it enumerated)
  port_t port[HW_PORTS + 1];
} hub_t;

static hub_t hubs[CFG_TUH_HUB];

// the hub port whose enumeration started last (hub 0: the root port)
static volatile uint8_t enum_hub, enum_port;

// JOB_STOP / JOB_SCAN stop the status pipe, and the next step sends the transfer: a status the
// abort came too late for is handled by tuh_task() in between
typedef enum { JOB_NONE, JOB_REMOVE, JOB_STOP, JOB_RESET, JOB_RESET_WAIT, JOB_SCAN, JOB_SCAN_SEND, JOB_SCAN_WAIT } job_step_t;
static struct {
  uint8_t step;   // job_step_t
  uint8_t hub, port;
  uint8_t busy;   // steps a control transfer was refused
  bool manual;    // asked for by AT+KARABAS=usbhub
} job;
static hub_port_status_response_t scan_status;

// AT requests from APP (port 0: scan)
static volatile bool req;
static volatile uint8_t req_hub, req_port;
// AT test: the next start of a hub port enumeration fails, as a TU_ASSERT in it would
static volatile bool stall_next;

static hub_t *hub_get(uint8_t addr) {
  if (addr < HUB_ADDR0 || addr >= HUB_ADDR0 + CFG_TUH_HUB) return NULL;
  hub_t *h = &hubs[addr - HUB_ADDR0];
  return h->ep_in ? h : NULL;
}

static port_t *port_get(uint8_t addr, uint8_t port) {
  hub_t *h = hub_get(addr);
  return (h && port >= 1 && port <= HW_PORTS) ? &h->port[port] : NULL;
}

static void post(uint8_t event_id, uint8_t hub_addr, uint8_t port) {
  hcd_event_t ev;
  memset(&ev, 0, sizeof(ev));
  ev.rhport = usbh_get_rhport(hub_addr);
  ev.event_id = event_id;
  ev.connection.hub_addr = hub_addr;
  ev.connection.hub_port = port;
  __real_hcd_event_handler(&ev, false);
}

// the status pipe of a hub: stopped for a job, armed again after it
static void pipe_stop(uint8_t addr) {
  hub_t *h = hub_get(addr);
  if (!h) return;
  if (usbh_edpt_busy(addr, h->ep_in)) tuh_edpt_abort_xfer(addr, h->ep_in);
  h->stopped = true;
}

static bool pipe_armed(uint8_t addr) {
  hub_t *h = hub_get(addr);
  return h && usbh_edpt_busy(addr, h->ep_in);
}

static void pipe_arm(uint8_t addr) {
  hub_t *h = hub_get(addr);
  if (!h) return;
  h->stopped = false;
  h->armed_ms = ms_now();
  if (!usbh_edpt_busy(addr, h->ep_in)) hub_edpt_status_xfer(addr);
}

// the attach and remove events the hub driver sends (task context); the root port's come from
// the SOF irq
static void port_event(bool attach, uint8_t hub_addr, uint8_t port) {
  port_t *p = port_get(hub_addr, port);
  if (!p) return;
  uint32_t now = ms_now();
  if (attach) {
    d_printf("USB hub %u port %u: connected at %lu ms\r\n", hub_addr, port, (unsigned long) now);
    memset(p, 0, sizeof(*p));
    p->state = PORT_ENUM;
    p->since = now;
  } else {
    if (p->state != PORT_EMPTY) d_printf("USB hub %u port %u: disconnected at %lu ms\r\n", hub_addr, port, (unsigned long) now);
    memset(p, 0, sizeof(*p));
  }
}

void __not_in_flash_func(__wrap_hcd_event_handler)(hcd_event_t const *event, bool in_isr) {
  if (event->event_id == HCD_EVENT_DEVICE_ATTACH || event->event_id == HCD_EVENT_DEVICE_REMOVE) {
    if (event->connection.hub_addr == 0) {
      if (event->event_id == HCD_EVENT_DEVICE_ATTACH) enum_hub = 0;
    } else if (!in_isr) {
      port_event(event->event_id == HCD_EVENT_DEVICE_ATTACH, event->connection.hub_addr, event->connection.hub_port);
    }
  }
  __real_hcd_event_handler(event, in_isr);
}

// usbh.c calls it when the enumeration of a hub port starts (after the 450 ms debounce)
bool __wrap_hub_port_get_status(uint8_t hub_addr, uint8_t hub_port, void *resp, tuh_xfer_cb_t complete_cb,
                                uintptr_t user_data) {
  bool ok;
  if (stall_next && hub_port != 0) {
    stall_next = false;
    d_printf("USB hub %u port %u: enumeration stopped (AT test)\r\n", hub_addr, hub_port);
    ok = false; // enum_new_device() returns with _dev0.enumerating set
  } else {
    ok = __real_hub_port_get_status(hub_addr, hub_port, resp, complete_cb, user_data);
  }
  if (hub_port != 0) {
    enum_hub = hub_addr;
    enum_port = hub_port;
    port_t *p = port_get(hub_addr, hub_port);
    if (p) {
      if (p->state != PORT_ENUM) p->daddr = 0;
      p->state = PORT_ENUM;
      p->since = ms_now();
    }
    if (!ok) d_printf("USB hub %u port %u: the enumeration could not start\r\n", hub_addr, hub_port);
  }
  return ok;
}

bool __wrap_hub_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
  bool ok = __real_hub_open(rhport, dev_addr, itf_desc, max_len);
  if (ok && dev_addr >= HUB_ADDR0 && dev_addr < HUB_ADDR0 + CFG_TUH_HUB) {
    hub_t *h = &hubs[dev_addr - HUB_ADDR0];
    memset(h, 0, sizeof(*h));
    // hub_open checked it: the interrupt endpoint right after the interface
    tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *) tu_desc_next(itf_desc);
    h->ep_in = ep->bEndpointAddress;
    h->armed_ms = ms_now();
    // a hub on a port of another hub: its port gets mounted when the hub is
    port_t *parent = enum_hub ? port_get(enum_hub, enum_port) : NULL;
    if (parent && parent->state == PORT_ENUM) parent->daddr = dev_addr;
  }
  return ok;
}

void __wrap_hub_close(uint8_t dev_addr) {
  __real_hub_close(dev_addr);
  if (dev_addr >= HUB_ADDR0 && dev_addr < HUB_ADDR0 + CFG_TUH_HUB) {
    memset(&hubs[dev_addr - HUB_ADDR0], 0, sizeof(hub_t));
    if (job.step != JOB_NONE && job.hub == dev_addr) job.step = JOB_NONE;
    if (enum_hub == dev_addr) enum_hub = 0;
  }
}

void hub_watch_mounted(uint8_t daddr) {
  port_t *p = enum_hub ? port_get(enum_hub, enum_port) : NULL;
  if (!p || p->state != PORT_ENUM) return;
  p->state = PORT_MOUNTED;
  p->daddr = daddr;
  // the time from the start of the enumeration: the margin of HW_ENUM_MS
  d_printf("USB hub %u port %u: device %u mounted in %lu ms", enum_hub, enum_port, daddr, (unsigned long) (ms_now() - p->since));
  if (p->tries) d_printf(" after %u restarts", p->tries);
  d_printf("\r\n");
}

// ---- the jobs: one at a time, a step per HW_STEP_MS

static void kick_start(uint8_t hub_addr, uint8_t port, bool manual) {
  port_t *p = port_get(hub_addr, port);
  if (!p) return;
  if (!manual) {
    p->tries++;
    d_printf("USB hub %u port %u: no device %lu ms after the %s, enumeration again (%u/%u)\r\n", hub_addr, port,
             (unsigned long) (ms_now() - p->since), p->tries > 1 ? "restart" : "connection", p->tries, HW_TRIES);
  } else {
    d_printf("USB hub %u port %u: enumeration again (AT)\r\n", hub_addr, port);
  }
  job.step = JOB_REMOVE;
  job.hub = hub_addr;
  job.port = port;
  job.busy = 0;
  job.manual = manual;
}

static void job_end(void) {
  if (hub_get(job.hub) && hub_get(job.hub)->stopped) pipe_arm(job.hub);
  job.step = JOB_NONE;
}

// a control transfer of a job refused: the next step tries it again, for a while
static void job_busy(const char *what) {
  if (++job.busy < HW_BUSY_STEPS) return;
  d_printf("USB hub %u port %u: %s refused (control pipe busy)\r\n", job.hub, job.port, what);
  port_t *p = port_get(job.hub, job.port);
  if (p && p->state == PORT_ENUM) p->since = ms_now(); // the next try after HW_ENUM_MS
  job_end();
}

static void reset_done(tuh_xfer_t *xfer) {
  if (job.step != JOB_RESET_WAIT) return;
  port_t *p = port_get(job.hub, job.port);
  if (xfer->result != XFER_RESULT_SUCCESS || !p) {
    d_printf("USB hub %u port %u: port reset failed\r\n", job.hub, job.port);
    if (p) p->since = ms_now();
    job_end();
    return;
  }
  // as connection_port_reset_complete() of the hub driver; the status pipe stays stopped through
  // the enumeration (usbh.c arms it at its end)
  p->state = PORT_ENUM;
  p->daddr = 0;
  p->since = ms_now();
  hub_get(job.hub)->stopped = false;
  post(HCD_EVENT_DEVICE_ATTACH, job.hub, job.port);
  job.step = JOB_NONE;
}

static void scan_done(tuh_xfer_t *xfer) {
  if (job.step != JOB_SCAN_WAIT) return;
  // a port the hub does not have stalls: the scan is over
  if (xfer->result != XFER_RESULT_SUCCESS) {
    job.port = HW_PORTS + 1;
  } else {
    port_t *p = port_get(job.hub, job.port);
    if (p && scan_status.status.connection && p->state != PORT_MOUNTED && p->state != PORT_ENUM) {
      d_printf("USB hub %u port %u: connected, no device (status %04x change %04x)\r\n", job.hub, job.port,
               scan_status.status.value, scan_status.change.value);
      if (p->state == PORT_EMPTY) p->tries = 0;
      if (p->state != PORT_FAILED) {
        // the next steps start its enumeration again
        p->state = PORT_ENUM;
        p->daddr = 0;
        p->since = ms_now() - HW_ENUM_MS;
      }
    }
    job.port++;
  }
  job.step = JOB_SCAN_SEND;
  job.busy = 0;
}

static void job_step(void) {
  switch (job.step) {
  case JOB_REMOVE:
    // a transfer of an enumeration at address 0 that never ends (a device that NAKs for ever)
    tuh_edpt_abort_xfer(0, 0);
    // the half enumerated device: its address, endpoints and a transfer on them
    post(HCD_EVENT_DEVICE_REMOVE, job.hub, job.port); // usbh.c arms the status pipe after it
    job.step = JOB_STOP;
    break;
  case JOB_STOP:
    pipe_stop(job.hub);
    job.step = JOB_RESET;
    break;
  case JOB_RESET:
    // armed again by the hub driver after a status that came in before the abort
    if (pipe_armed(job.hub)) { job.step = JOB_STOP; job_busy("port reset"); break; }
    if (!hub_port_reset(job.hub, job.port, reset_done, 0)) { job_busy("port reset"); break; }
    job.step = JOB_RESET_WAIT;
    break;
  case JOB_SCAN:
    pipe_stop(job.hub);
    job.step = JOB_SCAN_SEND;
    break;
  case JOB_SCAN_SEND:
    if (pipe_armed(job.hub)) { job.step = JOB_SCAN; job_busy("port status"); break; }
    if (job.port > HW_PORTS) {
      d_printf("USB hub %u: ports looked at, status pipe armed\r\n", job.hub);
      job_end();
      break;
    }
    {
      port_t *p = port_get(job.hub, job.port);
      if (p && (p->state == PORT_MOUNTED || p->state == PORT_ENUM)) { job.port++; break; }
    }
    if (!__real_hub_port_get_status(job.hub, job.port, &scan_status, scan_done, 0)) { job_busy("port status"); break; }
    job.step = JOB_SCAN_WAIT;
    break;
  default:
    break; // waiting for a callback
  }
}

static void scan_start(uint8_t hub_addr, bool manual) {
  if (manual) d_printf("USB hub %u: looking at the ports (AT)\r\n", hub_addr);
  job.step = JOB_SCAN;
  job.hub = hub_addr;
  job.port = 1;
  job.busy = 0;
  job.manual = manual;
}

static bool port_enumerating(port_t *p) {
  if (p->state != PORT_ENUM) return false;
  if (p->daddr && tuh_mounted(p->daddr)) { p->state = PORT_MOUNTED; return false; } // a hub
  return true;
}

static void step(void) {
  uint32_t now = ms_now();
  if (req) {
    if (job.step != JOB_NONE) return;
    req = false;
    if (!hub_get(req_hub)) return;
    if (req_port == 0) scan_start(req_hub, true);
    else kick_start(req_hub, req_port, true);
    return;
  }
  if (job.step != JOB_NONE) { job_step(); return; }

  // the port being enumerated goes first: while it hangs, the others wait behind it
  port_t *cur = enum_hub ? port_get(enum_hub, enum_port) : NULL;
  bool cur_waits = cur && port_enumerating(cur);
  for (uint8_t i = 0; i < CFG_TUH_HUB; i++) {
    hub_t *h = &hubs[i];
    uint8_t addr = HUB_ADDR0 + i;
    if (!h->ep_in) continue;
    if (!tuh_mounted(addr)) { h->armed_ms = now; continue; }
    bool enumerating = false;
    for (uint8_t n = 1; n <= HW_PORTS; n++) {
      port_t *p = &h->port[n];
      if (!port_enumerating(p)) continue;
      enumerating = true;
      if (now - p->since < HW_ENUM_MS) continue;
      if (cur_waits && p != cur) continue;
      if (p->tries >= HW_TRIES) {
        d_printf("USB hub %u port %u: no device after %u restarts, plug it in again\r\n", addr, n, p->tries);
        p->state = PORT_FAILED;
        continue;
      }
      kick_start(addr, n, false);
      return;
    }
    // the status pipe: armed while the hub waits for port changes
    if (h->stopped || enumerating || usbh_edpt_busy(addr, h->ep_in)) h->armed_ms = now;
    else if (now - h->armed_ms >= HW_PIPE_MS) {
      d_printf("USB hub %u: status pipe stopped for %lu ms\r\n", addr, (unsigned long) (now - h->armed_ms));
      scan_start(addr, false);
      return;
    }
  }
}

void __not_in_flash_func(hub_watch_tick)(void) { // the USB loop runs from RAM
  static uint32_t last;
  uint32_t t = timer_hw->timerawl;
  if (t - last < HW_STEP_MS * 1000u) return;
  last = t;
  // the events first, and an own transfer never between an event and its handling; an event that
  // stays is an attach put back behind a stopped enumeration
  static uint8_t queued;
  if (!tuh_task_event_ready()) queued = 0;
  else if (++queued < HW_QUEUE_STEPS) return;
  else queued = HW_QUEUE_STEPS;
  step();
}

// ---- AT+KARABAS=usbhub (APP; the state is core1's, a snapshot)

void hub_watch_print(void (*out)(const char *line)) {
  char line[96];
  for (uint8_t i = 0; i < CFG_TUH_HUB; i++) {
    hub_t *h = &hubs[i];
    uint8_t addr = HUB_ADDR0 + i;
    if (!h->ep_in) continue;
    snprintf(line, sizeof(line), "+KARABAS:usbhub;%u;%s;pipe %s\r\n", addr, tuh_mounted(addr) ? "mounted" : "enumerating",
             usbh_edpt_busy(addr, h->ep_in) ? "armed" : "not armed");
    out(line);
    for (uint8_t n = 1; n <= HW_PORTS; n++) {
      port_t *p = &h->port[n];
      if (p->state == PORT_EMPTY && !p->tries) continue;
      snprintf(line, sizeof(line), "+KARABAS:usbport;%u;%u;%s;%u;%u\r\n", addr, n, port_names[p->state], p->daddr, p->tries);
      out(line);
    }
  }
}

uint8_t hub_watch_first_hub(void) {
  for (uint8_t i = 0; i < CFG_TUH_HUB; i++) {
    if (hubs[i].ep_in) return HUB_ADDR0 + i;
  }
  return 0;
}

bool hub_watch_request_reset(uint8_t hub_addr, uint8_t port) {
  if (port < 1 || port > HW_PORTS || req) return false;
  req_hub = hub_addr;
  req_port = port;
  req = true;
  return true;
}

void hub_watch_request_stall(void) {
  stall_next = true;
}

bool hub_watch_request_scan(uint8_t hub_addr) {
  if (req) return false;
  req_hub = hub_addr;
  req_port = 0;
  req = true;
  return true;
}
