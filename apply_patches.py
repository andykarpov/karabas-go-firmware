from os.path import join, isfile

Import("env")

LIBDEPS_DIR = env['PROJECT_LIBDEPS_DIR']
FRAMEWORK_DIR = env.PioPlatform().get_package_dir("framework-arduinopico")

patches = [
    [join("patches", "1-adafruit-usbh-host.patch"), join(FRAMEWORK_DIR, "libraries", "Adafruit_TinyUSB_Arduino", "src", "arduino", "Adafruit_USBH_Host.cpp")],
#    [join("patches", "1-adafruit-usbh-host.patch"), join(LIBDEPS_DIR, "pico", "Adafruit TinyUSB Library", "src", "arduino", "Adafruit_USBH_Host.cpp")],
#    [join("patches", "2-adafruit-sdfatconfig.patch"), join(LIBDEPS_DIR, "pico", "SdFat - Adafruit Fork", "src", "SdFatConfig.h")],
]

for patch in patches:

    patch_file = patch[0]
    original_file = patch[1]
    patchflag_path = original_file + ".patching-done"

    # patch file only if we didn't do it before
    if not isfile(patchflag_path):
        print(f"Patching {original_file} with {patch_file}")
        assert isfile(original_file) and isfile(patch_file)
        env.Execute("patch \"%s\" < %s" % (original_file, patch_file))
        def _touch(path):
            with open(path, "w") as fp:
                fp.write("")
        env.Execute(lambda *args, **kwargs: _touch(patchflag_path))
