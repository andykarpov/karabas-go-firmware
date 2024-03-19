#ifndef PS2_KBD_H
#define PS2_KBD_H

#include <Arduino.h>
#include "hid_app.h"

void kb_send(uint8_t byte);
void kb_maybe_send_e0(uint8_t key);
void kb_send_key(uint8_t key, bool state, uint8_t modifiers);
void kb_usb_receive(hid_keyboard_report_t const *report);

#endif