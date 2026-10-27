# Перенос в QEMU CHR/x86

Статус: анализ существующего кода; перенос пока не выполнен. Исправный RNDIS
профиль сохраняется. Новые runtime API/endpoints не требуются.

## Что использовать непосредственно

- components/mbim: MBIM BasicConnect, DeviceServices, fakeSIM/registration,
  IPConfiguration layout, MS BaseStationsInfo, ATDS Signal и NTB16 session0.
- components/modem: AT parser, staged/active profile, generation fence,
  отложенная активация/таймаут и data blackhole. Валидатор Wi-Fi не подходит
  обычному APN интернет-провайдера без адаптации политики профиля.
- Проверки настоящего C-кода под ASAN/UBSAN, независимый libmbim decoder и тесты
  повреждённых сообщений, ограничений буфера, очереди, OPEN/CLOSE и ZLP.

Предпочтение: небольшая C ABI-обёртка и ctypes поверх того же C-кода. Не переписывать
вторую независимую реализацию MBIM в Python и не требовать libmbim в runtime.
libmbim используется только как независимый декодер в тестах.

## Что адаптировать в существующем Python usbredir эмуляторе

| Файл | Изменение |
|---|---|
| descriptors.py | opt-in profile=mbim, interface02/0e, data alternate0/1, endpoint contract |
| device.py | MBIM SEND/GET_ENCAPSULATED, response notifications, NCM negotiation, session reset |
| device.py bulk path | NTB16/rawIPv4 вместо RNDIS Ethernet wrappers; bounded RX/TX |
| emulator.py | выбор профиля и startup IPv4/backend configuration, без новых API |
| state.py | связать существующие fault-injection controls с состояниями общего ядра |
| uplink.py | сохранить AF_PACKET/socket L2; при MBIM добавить IPv4↔Ethernet/ARP adapter |

ESP-specific main/main.c (Wi-Fi, NAPT, UART, NVS) и TinyUSB main/mbim_usb.c не
переносятся в QEMU. Их USB-контракт/тестовые сценарии служат эталоном адаптера.

## Важная разница адресации

У ESP USB-facing адрес192.0.2.2 за собственным NAT. QEMU bridge использует внешний
ISP. Нельзя просто выдавать ESP-адрес/шлюз и объявлять, что IP работает.
Нужна явная адресация из изолированной fixture либо отдельный DHCP client к ISP
backend. Эмулятор не становится DHCP-сервером. При статике исключить пересечение
с реальным DHCP-пулом ISP. APN выбирает синтетический backend, не Wi-Fi SSID.

## Приёмка переноса

Сначала probe → только разрешённый decision.path, server ready → QEMU TCP-client.
Проверить actual QMP USB, RouterOS hardware/LTE, APN и monitor, двусторонние кадры,
ICMP/TCP. Затем reset/reopen, ошибки профиля, blackhole, CFUN-equivalent, свежую
SLA-приёмку, failover/failback и очистку только affected conntrack.

USB ID не подбирать вслепую. CellMonitor/SINR считать рабочими только после
фактического вывода на конкретном RouterOS. На x86 L6 сохранить qcow2 IDE
format=qcow2,bus=0,unit=0,media=disk. Изменения lab.py только opt-in edge.
Не запускать новые VM/сборки поверх здорового текущего48h SD-WAN soak.
