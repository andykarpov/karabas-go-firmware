#pragma once

// APP task only. Status updates have no byte acknowledgement semantics.
void printer_link_begin();
void printer_link_tick();
void printer_link_reset(); // discard protocol state when changing the FPGA core
