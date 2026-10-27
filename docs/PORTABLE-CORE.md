# Reuse by the QEMU CHR/x86 emulator

components/modem provides profile validation, AT staging, generation-fenced activation
and the IPv4 data gate. components/mbim provides bounded MBIM control messages,
BasicConnect/MS BaseStationsInfo/ATDS Signal and NTB16 rawIPv4 framing.
Neither portable component depends on ESP-IDF or TinyUSB. C11 host tests compile
the actual firmware C files under ASAN/UBSAN; independent libmbim decodes replies.

Reuse these components in the existing usbredir userspace emulator rather than
porting main/main.c or duplicating the protocol in Python. Add a thin C ABI/ctypes
adapter for emulator state and packet IO. main/mbim_usb.c is a TinyUSB-specific
adapter and must remain on ESP; its host harness defines useful transport cases.

The QEMU adapter should map APN activation to a selected synthetic ISP backend,
not require a physical Wi-Fi radio. Route/DNS data must come from that backend,
not blindly retain the ESP's fixed192.0.2.2 settings. Keep current RNDIS working.

First parity checks: actualUSB enumeration/LTE binding, APN delivery, monitor,
ICMP+TCP, bounded queues, NTB/ZLP, disconnect/reopen, invalid profile/timeouts,
SLA failover/failback and affected-only conntrack. Do not claim CellMonitor/SINR
from protocol fixtures; require actual RouterOS output per version.

No new runtime APIs or external telemetry are required. Existing host-side lab
fault controls remain test infrastructure; routing decisions stay in RouterOS.

This is a reuse design, not an implemented QEMU MBIM backend.
