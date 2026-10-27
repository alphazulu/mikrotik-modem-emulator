# Dependencies

The original application C/Python source is maintained separately from vendor
SDKs. This draft does not assign a new license to the owner's original source.

Build dependencies are pinned to Espressif ESP-IDF5.5.2, esp-usb and TinyUSB commits
listed in tools/build.py. Vendor dependencies and their licenses remain in the
upstream checkouts. tinyusb-linux-rndis.patch records a local descriptor/class
binding change; the upstream license notices remain in the patched sources.

libmbim is an independent host-test decoder, not firmware code or a runtime
library for the device. pyserial and pyelftools are host tooling dependencies.
No vendor SDK tree, libmbim binary, RouterOS image, license or originalflash is
bundled in this source draft. A public license decision is separate from validation.
