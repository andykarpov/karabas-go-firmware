#pragma once

// Included after pio_usb_ll.h. Keep this inlined in the RAM-resident SOF path.
static inline __attribute__((always_inline)) void
pio_usb_host_out_result(endpoint_t *ep, uint8_t pid, uint16_t length) {
  if (pid == USB_PID_ACK) {
    ep->out_errors = 0;
    pio_usb_ll_transfer_continue(ep, length);
  } else if (pid == USB_PID_NAK) {
    // Normal device backpressure is not an error; try on a later USB frame.
    ep->out_errors = 0;
  } else if (pid == USB_PID_STALL) {
    // A halted endpoint needs explicit recovery, not retries.
    pio_usb_ll_transfer_complete(ep, PIO_USB_INTS_ENDPOINT_STALLED_BITS);
  } else if ((ep->attr & 3) != 2 || ++ep->out_errors >= 3) {
    pio_usb_ll_transfer_complete(ep, PIO_USB_INTS_ENDPOINT_ERROR_BITS);
  }
  // For bulk OUT, the first two missing/corrupt handshakes leave the transfer
  // active. The next SOF sends the SAME packet with the SAME DATA0/DATA1 PID:
  // do not advance app_buf, actual_len or data_id until ACK. If the printer
  // received the packet but its ACK was lost, its toggle rejects the duplicate.
  // Retrying the entire application transfer instead would duplicate data.
  // Three consecutive failures still reach the caller; do not hide a bad link.
  // Control, interrupt and isochronous endpoints keep their previous behavior.
}
