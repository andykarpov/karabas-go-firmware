# Karabas USB HID gamepad mapper

This small web tool will help you to poll a USB gamepad and map it's buttons to a virtual MD2 gamepad.
After the setup process you'll be able to download the INI file needed by Karabas Go (and mini) to use this gamepad properly.

## How to

1. Open https://andykarpov.github.io/karabas-go-firmware/hidmapper/ in Google Chrome, MS Edge or Opera (WebHID is needed)
2. Click a "Connect Gamepad"
3. In the browser's popup please find and choose your gamepad
4. Don't touch the pad for a second while it learns the idle state. The page also tells whether
   the firmware maps this pad by default, without an INI ("test with the default map").
5. Press "Record all" (or "Record" for one button) and press the buttons when asked. "+" adds
   another source to a button, for example the stick and the hat for the directions.
6. Test the mapping on the screen. It is checked the same way the firmware does.
7. Download the INI file (`<vid>_<pid>.ini`) and put it on the SD card under /drivers/.
   "Open .INI" loads an existing driver to edit it.
8. Enjoy your USB HID gamepad on Karabas

## Linux tips and tricks 

To fix linux permissions to the hidraw device from web browser, please do the following:

Create file /etc/udev/rules.d/99-webhid.rules:

`sudo nano /etc/udev/rules.d/99-webhid.rules`

with the content:

```
KERNEL=="hidraw*", SUBSYSTEM=="hidraw", MODE="0666", TAG+="uaccess"
```

Then apply newly created rules:

`sudo udevadm control --reload-rules && sudo udevadm trigger`

## Development

The page is built and published to GitHub Pages by `.github/workflows/hidmapper.yml` on every
push to master that changes `tools/hidmapper/`.

```
cd tools/hidmapper
npm ci
npm run dev           # local server
npx vitest run        # tests
npm run build         # dist/index.html, a single self-contained file
```

`src/services/JoyMap.ts`, `HidLearn.ts` and `AutoMap.ts` are ports of `hid_joy_map`, `hid_learn`
and `hid_auto_map` in `lib/KarabasCommon`. Change both sides together; the tests use the same
cases.
