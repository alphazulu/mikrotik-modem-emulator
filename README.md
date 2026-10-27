# MikroTik modem emulator — ESP32-S3 source draft

Wi-Fi-backed USB modem emulation for RouterOS testbeds. The device presents MBIM
or RNDIS; the SD-WAN controller stays on MikroTik. No additional telemetry APIs,
HTTP endpoints or external fetch calls are needed for modem operation.

This local draft is NOT a completed release: physical hot USB unplug/replug and
end-to-end SD-WAN with ESP still need acceptance. CellMonitor returns Unsupported
and SINR was not displayed on the tested RB750Gr3/RouterOS7.11.2. Radio/SIM fields
are virtual; this is not a cellular radio or a hardware capacity benchmark.

Verified on the isolated hEX: realLTE interface, automatic MBIM APN-to-Wi-Fi,
ICMP/HTTP viaLTE, realWi-Fi network scan, monitor cell/RF fields and3reset recoveries.
UART data blackhole/recovery and CFUN radiooff/on passed; invalidAPN, unknownSSID
and wrongkey stopped old forwarding and recovered after restoring the originalAPN.
USB power reset did NOT detach this self-powered device while UART supplied power.
That outcome is not a successful hotplug test. Bridge was left unchanged.

## Build and host checks

Linux/WSL with Python3, GCC/gcov, Git, CMake, libmbim runtime and ESP-IDF prerequisites.
Python dependencies: python3 -m pip install -r requirements.txt.
Use Ubuntu's libmbim-glib4 or MODEM_LIBMBIM_PATH for independent decoding.

```sh
python3 tools/check.py
python3 tools/check_usb.py
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 tests/mbim/check.py
python3 tests/mbim/check_driver.py
python3 tools/bootstrap.py
python3 tools/mirror_sdk.py
python3 tools/build.py --native-sdk --profile mbim
python3 tools/verify.py --profiles mbim
```

Dependencies are pinned. SDK/tool caches stay outside source control. Builds are
single-job/low-priority and do not access hardware. A surrounding SD-WAN soak,
if present, prevents dependency/build operations. No deployment command runs
implicitly. Verify the chip/flash, back up original flash privately, verify image
hashes and perform a separate verify-flash before using a new board.

The tested board had16MiB flash/8MiB PSRAM; this image uses noPSRAM and the first4MiB
flash layout with a2MiB app partition. NativeUSB goes to hEX; a separateUART cable
serves flashing/control. Do not change eFuse or erase owner flash blindly.

MBIM APN: apn=wifi, user=SSID, password=Wi-Fi key, authentication=pap, ip-type=ipv4,
use-network-apn=no. Settings stay in RAM unless AT&W is explicitly requested.
See [APN mapping](APN-WIFI.md). The current firmware artifact name remains
sdwan_esp32_lte for continuity with the hardware-validated image.

See [portable core / QEMU reuse](docs/PORTABLE-CORE.md). The QEMU MBIM adapter is
not implemented here. Proprietary images, router exports, credentials, raw hardware
logs and full-flash backups are excluded from this source draft.
