#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// AT+KARABAS commands that drive the board without its keyboard, menus and mouse (APP only, from
// serial_bridge). Each answers OK or ERROR, after its +KARABAS lines if it has any:
//   state - the running core, the OSD screen, the file items
//   load:<file or id> - start a core from any core; OK when it has started. The boot core: a reboot
//   reset - start the running core again (with clean memory)
//   osd / osd:<item> / osd:<item>=<value> - the menu items of the core: list, one, set
//   mount:<item>:<path> / eject:<item> - the image of a file item
//   mouse:<dx>,<dy>[,<buttons>[,<wheel>]] / click[:left|right|middle]
//   pad:<port>:<buttons> ... / padhold:<port>:<buttons>
//   debug:0|1 - the debug log until the next reboot
//   sync - OK once everything queued by key, type, mouse, click and pad has been delivered
// An item is the key or the name of the item (any case), or #<position>.

// true: arg was one of these commands, it has been answered (or sync waits)
bool at_control_command(const char *arg);
void at_control_tick();    // the answer of sync
void at_control_release(); // mouse buttons and pads up (AT+KARABAS=release)

#ifdef __cplusplus
}
#endif
