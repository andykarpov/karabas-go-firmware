#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern setup_t hw_setup;

void load_setup();
// writes the values of the OSD Settings screens that differ from hw_setup into karabas.ini,
// keeping the rest of the file, then sets hw_setup to s
bool save_setup(const setup_t *s);

#ifdef __cplusplus
}
#endif
