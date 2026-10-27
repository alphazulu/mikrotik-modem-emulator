# Hardware validation summary — draft

Tested hardware: ESP32-S3 rev0.2, flash16MiB/PSRAM8MiB, RB750Gr3, RouterOS7.11.2.
MBIM dev2 image:730064 bytes, SHA256
`a0dc01272be4ed028f463f669bd45d75611db232f51330ba2907264b9e002cb2`.
The image identity remains sdwan_esp32_lte for compatibility with validated builds.

PASS: actualUSB/LTE, automaticAPN after reset, ICMP/HTTP throughLTE, network scan,
model/SIM/operator/cell/RF monitor fields,3reset recovery cycles, liveUSB data
blackhole and recovery, CFUN0/1 Wi-Fi disconnect/recovery, invalidAPN, unknownSSID,
wrongkey and return to the originalAPN. Original key/bridge were not changed.

Synthetic: SIM/operator, BSSID-derived cell identities, virtualB3/EARFCN,
Wi-Fi RSSI mapped to virtualRSRP, RSRQ=-10, timingAdvance0. No LTE radio exists.
CellMonitor is Unsupported and SINR is not displayed on the tested RouterOS.

USB power reset with independent UART power did not show a detach; not a passed
hotplug test. Full ESP SD-WAN integration and physical hotplug acceptance remain
open. One earlier public-ping test had4/5 replies, retained as failed; subsequent
3reset functional cycles each had local/public5/5 and HTTP. No zero-loss guarantee.

Raw hardware evidence is intentionally private. This summary contains no owner
router serial, MAC, Wi-Fi credentials, router export, originalflash or disk/license.
This source draft is not a final accepted release.
