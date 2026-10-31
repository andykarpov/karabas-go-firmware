// USB MIDI host class driver: TinyUSB 0.18 has only the device side. One device, the IN
// endpoint of its MIDI Streaming interface (bulk, or interrupt as some vendors have it);
// the OUT endpoint is not opened, so the driver costs one endpoint of the PIO-USB pool.
// The transfer is re-armed from its own callback: nothing runs in the idle USB loop.

#include "midi_host.h"
#include <string.h>
#include "config.h"
#include "usb_midi.h"
#include "fpga/link.h"
#include "diag/debug_out.h"
#include "storage/settings.h"

#define AUDIO_CLASS 1
#define AUDIO_SUBCLASS_AC 1
#define AUDIO_SUBCLASS_MS 3

static struct {
  uint8_t address, itf_ms, ep_in;
  uint16_t mps, vid, pid;
  bool mounted;
} dev;
static volatile uint32_t rx_bytes, sent_bytes, skipped_bytes;
static uint8_t seq; // addr of CMD_MIDI: counts the bytes, so the core sees a loss
CFG_TUSB_MEM_ALIGN static uint8_t rx_buf[64];

static bool midi_init(void) {
  memset(&dev, 0, sizeof(dev));
  return true;
}

static bool is_itf(const uint8_t *d, uint8_t subclass) {
  const tusb_desc_interface_t *itf = (const tusb_desc_interface_t *) d;
  return d[1] == TUSB_DESC_INTERFACE && d[0] >= sizeof(*itf) &&
         itf->bInterfaceClass == AUDIO_CLASS && itf->bInterfaceSubClass == subclass &&
         itf->bAlternateSetting == 0;
}

// a keyboard has an Audio Control interface (no endpoints) and the MIDI Streaming one.
// Without an IAD TinyUSB offers them one by one; with an IAD both at once, from AC.
static bool midi_open(uint8_t rhport, uint8_t address, const tusb_desc_interface_t *itf, uint16_t len) {
  (void) rhport;
  if (dev.address || !address || len < sizeof(*itf) || itf->bLength > len) return false;
  const uint8_t *bytes = (const uint8_t *) itf;
  uint16_t pos = 0;
  if (is_itf(bytes, AUDIO_SUBCLASS_AC)) {
    // look for the MS interface inside the association
    for (pos = itf->bLength; pos + 2 <= len; pos += bytes[pos]) {
      if (bytes[pos] < 2 || bytes[pos] > len - pos) return false;
      if (is_itf(bytes + pos, AUDIO_SUBCLASS_MS)) break;
    }
    if (pos + sizeof(*itf) > len) return false;
  } else if (!is_itf(bytes, AUDIO_SUBCLASS_MS)) {
    return false;
  }
  const tusb_desc_interface_t *ms = (const tusb_desc_interface_t *) (bytes + pos);

  tusb_desc_endpoint_t in = {0};
  for (pos += ms->bLength; pos + 2 <= len; pos += bytes[pos]) {
    const uint8_t size = bytes[pos], type = bytes[pos + 1];
    if (size < 2 || size > len - pos) return false;
    if (type == TUSB_DESC_INTERFACE || type == TUSB_DESC_INTERFACE_ASSOCIATION) break;
    if (type != TUSB_DESC_ENDPOINT || size < sizeof(tusb_desc_endpoint_t)) continue;
    tusb_desc_endpoint_t ep;
    memcpy(&ep, bytes + pos, sizeof(ep));
    const uint16_t mps = tu_edpt_packet_size(&ep);
    if ((ep.bEndpointAddress & 0x80) && !in.bEndpointAddress &&
        (ep.bmAttributes.xfer == TUSB_XFER_BULK || ep.bmAttributes.xfer == TUSB_XFER_INTERRUPT) &&
        mps >= 4 && mps <= sizeof(rx_buf)) {
      in = ep;
    }
  }
  if (!in.bEndpointAddress) {
    d_printf("USB MIDI: no usable IN endpoint, address %u", address); d_println();
    return false;
  }
  if (!tuh_edpt_open(address, &in)) {
    d_printf("USB MIDI: IN endpoint %02x did not open (endpoint pool full?)", in.bEndpointAddress); d_println();
    return false;
  }
  dev.address = address;
  dev.itf_ms = ms->bInterfaceNumber;
  dev.ep_in = in.bEndpointAddress;
  dev.mps = tu_edpt_packet_size(&in);
  tuh_vid_pid_get(address, &dev.vid, &dev.pid);
  return true;
}

static bool midi_read(void) {
  if (!usbh_edpt_claim(dev.address, dev.ep_in)) return false;
  if (usbh_edpt_xfer(dev.address, dev.ep_in, rx_buf, dev.mps)) return true;
  usbh_edpt_release(dev.address, dev.ep_in);
  return false;
}

static bool midi_set_config(uint8_t address, uint8_t itf_num) {
  if (address != dev.address) return false;
  // with an IAD this comes for the AC interface too: mount once, at the MS one
  if (itf_num == dev.itf_ms && !dev.mounted) {
    dev.mounted = true;
    d_printf("USB MIDI mounted %04x:%04x address %u interface %u IN %02x (%u bytes)",
             dev.vid, dev.pid, dev.address, dev.itf_ms, dev.ep_in, dev.mps); d_println();
    if (!midi_read()) { d_print("USB MIDI: IN transfer failed"); d_println(); }
  }
  usbh_driver_set_config_complete(address, itf_num);
  return true;
}

static void midi_close(uint8_t address) {
  if (address != dev.address) return;
  if (dev.mounted) { d_printf("USB MIDI disconnected, address %u", address); d_println(); }
  memset(&dev, 0, sizeof(dev));
}

static void midi_send(const uint8_t *bytes, uint8_t n) {
  // only cores that take MIDI get it (caps bit 4); others would see unknown frames
  const bool take = (link_core_caps() & CAPS_MIDI) != 0;
  for (uint8_t i = 0; i < n; i++) {
    if (take) {
      spi_queue(CMD_MIDI, seq++, bytes[i]);
      sent_bytes++;
    } else {
      skipped_bytes++;
    }
  }
}

static bool midi_xfer_cb(uint8_t address, uint8_t ep, xfer_result_t result, uint32_t length) {
  if (address != dev.address || ep != dev.ep_in) return false;
  if (result == XFER_RESULT_SUCCESS) {
    for (uint32_t i = 0; i + 4 <= length; i += 4) {
      uint8_t out[3];
      const uint8_t n = kg_usb_midi_packet(rx_buf + i, out);
      rx_bytes += n;
      midi_send(out, n);
    }
  }
  if (result == XFER_RESULT_STALLED) {
    // PIO-USB cannot clear a stall: reading again would only stall again
    d_print("USB MIDI: IN endpoint stalled, stopped"); d_println();
  } else if (dev.mounted && !midi_read()) {
    // an error reads again; a keyboard that went away is closed by TinyUSB
    d_print("USB MIDI: IN transfer failed"); d_println();
  }
  return true;
}

usbh_class_driver_t const usbh_midi_driver = {
  "MIDI", midi_init, NULL, midi_open, midi_set_config, midi_xfer_cb, midi_close
};

void midi_host_stats(midi_host_stats_t *stats) {
  stats->rx = rx_bytes;
  stats->sent = sent_bytes;
  stats->skipped = skipped_bytes;
  stats->mounted = dev.mounted;
}
