from os.path import join, isfile

Import("env")

LIBDEPS_DIR = env['PROJECT_LIBDEPS_DIR']
patchflag_path = join(LIBDEPS_DIR, "pico", "Adafruit TinyUSB Library", "src", "arduino", "Adafruit_USBH_Host.cpp.patching-done")

# patch file only if we didn't do it before
if not isfile(patchflag_path):
    original_file = join(LIBDEPS_DIR, "pico", "Adafruit TinyUSB Library", "src", "arduino", "Adafruit_USBH_Host.cpp")
    patched_file = join("patches", "1-adafruit-usbh-host.patch")

    assert isfile(original_file) and isfile(patched_file)

    env.Execute("patch \"%s\" < %s" % (original_file, patched_file))
    # env.Execute("touch " + patchflag_path)

    def _touch(path):
        with open(path, "w") as fp:
            fp.write("")

    env.Execute(lambda *args, **kwargs: _touch(patchflag_path))
