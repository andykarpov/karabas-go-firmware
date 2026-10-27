
#pragma once

#include "pio_usb_configuration.h"
#include "usb_definitions.h"

#ifdef __cplusplus
 extern "C" {
#endif

// Host functions
usb_device_t *pio_usb_host_init(const pio_usb_configuration_t *c);
int pio_usb_host_add_port(uint8_t pin_dp, PIO_USB_PINOUT pinout);
void pio_usb_host_task(void);
void pio_usb_host_stop(void);
void pio_usb_host_restart(void);
bool pio_usb_host_programs_loaded(void); // karabas: false if a PIO program did not fit
uint32_t pio_usb_host_get_frame_number(void);

// karabas: counters for the hang diagnostics (DIAG line, root port events), written on core1
typedef struct {
  volatile uint32_t connects;     // the root port went from SE0 to an idle line
  volatile uint32_t disconnects;  // SE0 on the root port at a SOF
  volatile uint32_t rx_overflows; // a packet longer than usb_rx_buffer, dropped
  volatile uint32_t max_sof_gap_us;   // the longest time between two frames (1000 if on time)
  volatile uint32_t max_frame_us;     // the longest frame handler
} pio_usb_diag_t;
extern pio_usb_diag_t pio_usb_diag;
// karabas: the root port 0 as the host sees it: bit 0 initialized, 1 connected, 2 suspended,
// 3 full speed, 4..5 the line state (PORT_PIN_*: 0 SE0, 1 full speed idle, 2 low speed idle, 3 SE1)
uint8_t pio_usb_host_root_state(void);

// Call this every 1ms when skip_alarm_pool is true.
void pio_usb_host_frame(void);

// Device functions
usb_device_t *pio_usb_device_init(const pio_usb_configuration_t *c,
                                  const usb_descriptor_buffers_t *buffers);
void pio_usb_device_task(void);

// Common functions
endpoint_t *pio_usb_get_endpoint(usb_device_t *device, uint8_t idx);
int pio_usb_get_in_data(endpoint_t *ep, uint8_t *buffer, uint8_t len);
int pio_usb_set_out_data(endpoint_t *ep, const uint8_t *buffer, uint8_t len);

#ifdef __cplusplus
 }
#endif
