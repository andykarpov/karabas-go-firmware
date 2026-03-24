#include <Arduino.h>
#include <GyverMenu.h>
#include <GyverMenuExt.h>
#include "types.h"

void CoreItem(gm::Builder& b, core_osd_t& osd_item, void (*cb)(core_osd_t&) = nullptr) {
    if (!b.menu.beginWidget()) return;

    bool render = false;

    switch (b.getAction()) {
        case gm::Builder::Action::Refresh:
            render = true;
            break;

        case gm::Builder::Action::Set:
            b.menu.toggle();
            render = true;
            break;

        case gm::Builder::Action::SetUp:
        case gm::Builder::Action::Right:
            if (osd_item.options_len) {
                osd_item.val = (osd_item.val < osd_item.options_len-1) ? osd_item.val+1 : 0;
                if (cb) cb(osd_item);
                b.change();
                render = true;
            }
            break;

        case gm::Builder::Action::SetDown:
        case gm::Builder::Action::Left:
            if (osd_item.options_len) {
                osd_item.val = (osd_item.val > 0) ? osd_item.val-1 : osd_item.options_len-1;
                if (cb) cb(osd_item);
                b.change();
                render = true;
            }
            break;

        default: break;
    }

    if (render && b.beginRender()) {
        b.menu.print(osd_item.name);
        b.menu.print(osd_item.hotkey);
        if (b.menu.isActive()) b.menu.print(':');
        if (osd_item.options_len > 0) {
            b.menu.pad(b.menu.left - strlen(osd_item.options[osd_item.val].name) + 2);
            b.menu.print('<');
            b.menu.print(osd_item.options[osd_item.val].name);
            b.menu.print('>');
        } else {
            b.menu.pad(b.menu.left);
        }
    }
}
