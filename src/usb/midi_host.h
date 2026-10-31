#pragma once

#include "tusb.h"
#include "host/usbh_pvt.h"

#ifdef __cplusplus
extern "C" {
#endif

// USB MIDI keyboards (docs/usb_midi_plan.md): the IN endpoint of one MIDI Streaming
// interface, its events as CMD_MIDI frames to cores with CAPS_MIDI. USB task (core 1) only.
extern usbh_class_driver_t const usbh_midi_driver;

typedef struct {
  uint32_t rx;      // MIDI bytes taken from the device
  uint32_t sent;    // bytes queued to the core
  uint32_t skipped; // bytes not queued: the core has no CAPS_MIDI
  bool mounted;
} midi_host_stats_t;
void midi_host_stats(midi_host_stats_t *stats);

#ifdef __cplusplus
}
#endif
