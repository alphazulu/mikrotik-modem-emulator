# Настройка Wi-Fi через модемный профиль

Статус MBIM dev2: автоматическая передача APN/SSID/пароля с hEX RouterOS7.11.2
подтверждена, включая3 reset с восстановлением интернета. RNDIS не имеет такого
стандартного APN management, AT-chat в тесте7.11.2 оказался unsupported.
Подробные ограничения: [MODEM-SCANNER-VALIDATION.md](MODEM-SCANNER-VALIDATION.md).

## Единая схема параметров

| Поле RouterOS/модема | Значение в эмуляторе |
|---|---|
| APN profile `name` | Локальное имя внутри RouterOS; на ESP не передаётся |
| `apn=wifi` | Выбор Wi-Fi профиля эмулятора |
| `user` | SSID, 1–32 UTF-8 байта |
| `password` | Wi-Fi ключ: 8–63 печатных ASCII или 64 hex |
| `authentication=pap/chap` | В этом тестовом модеме означает защищённую Wi-Fi сеть и передачу key; Wi-Fi использует WPA2+, а не PAP/CHAP |
| `authentication=none`, пустой key | Явно открытая Wi-Fi сеть |

PAP/CHAP не шифруют пароль Wi-Fi на USB канале в этой схеме. Это перенос полей
профиля, не эмуляция сотовой аутентификации или Wi-Fi Enterprise/EAP.
WPA3-only и Enterprise не входят в первый профиль; WPA2-compatible сеть требуется.

Для SSID со сложными символами есть второй формат: `apn=wifi.<BASE32-SSID>`,
`user=""`, password — отдельно. Base32 без `=` и без пробелов, регистр допускается
любой, неиспользуемые биты должны быть нулевыми. Даже 32 байта дают label длиной
52 символа; пароль не включается в APN или SSID. Две формы одновременно запрещены.

## Стандартные AT-команды первого прототипа

```text
ATE0
AT+CGDCONT=1,"IP","wifi"
AT+CGAUTH=1,1,"DemoAP","DEMO-ONLY-KEY"
AT+CGACT=1,1
AT+CGACT?
AT+CGATT?
```

Это синтетический пример. CID пока только 1, PDP type только `IP` (IPv4).
CGDCONT/CGAUTH изменяют staged profile в RAM. CGACT=1 применяет профиль целиком;
внешний ответ OK выдаётся после получения Wi-Fi IP, иначе CME ERROR/timeout.
Повторная активация того же профиля не увеличивает generation и не перезапускает
исправное Wi-Fi соединение. Новое поколение/CFUN/SIM отменяет ожидающую активацию.

CGACT=0 закрывает data session, CFUN=0 выключает радио и вызывает Wi-Fi disconnect.
SIM/registration/CSQ — синтетическое состояние; CSQ не подменяется Wi-Fi RSSI.
При data blackhole `AT+EMUGATE=1` Wi-Fi/USB/session остаются поднятыми, а Internet
data gate закрыт. ARP/DHCP и локальные услуги сохраняются для наблюдения отказа.

Ответы не содержат исходную команду; ATE1 подтверждается, но echo остаётся выключен,
чтобы credential-bearing команды не дублировались. CGAUTH? не возвращает key/SSID.
Неизвестные/повреждённые/слишком длинные команды — ERROR, без применения их части.

`AT&W` явно сохраняет **active** профиль в NVS; повтор совпадающего сохранения
не делает новую запись. Это NVS development-прошивки, без обещания шифрования flash.
Без AT&W после reboot требуется повторная настройка. `AT+EMUFORGET` удаляет только
профиль эмулятора и очищает credentials в RAM; не выполняет полное erase flash.

## Управление RouterOS через MBIM

MBIM Connect SET передаёт APN/Username/Password в общий обработчик профиля ESP.
Активация завершается после Wi-Fi IP; неуспех/timeout возвращает ошибку. USB IP
выдаётся RouterOS через IPConfiguration, не DHCP-client. Профиль IPv4.

Пример для **тестового** RouterOS7.11.2 с синтетическими SSID/key:

```routeros
/interface/lte/apn/add name=esp-wifi apn=wifi user="DemoAP" password="DEMO-ONLY-KEY" authentication=pap ip-type=ipv4 use-network-apn=no
/interface/lte/set lte1 apn-profiles=esp-wifi
```

Новые версии документации используют `apn-profile`; тест7.11.2 выполнялся с
`apn-profiles`. Проверять схему целевой версии. Параметры add-default-route,
distance и DNS задаются через этот APN-профиль. Не менять bridge ради установки.
В реальном окружении ключ вводится в WinBox/API через защищённый способ, не
сохраняется в публичном RSC/истории команд/репозитории.

Для RNDIS Hilink параметры APN управляют внутренним DHCP-client RouterOS,
но сами по себе не передают Wi-Fi credentials в ESP. Experimental CDC рядом с
RNDIS тоже не доказывает наличие поддержанного AT-канала.

## Инструмент для первого подключения

Для HiLink либо отдельной диагностики используется AT-handler через CH343 UART;
experimental CDC применяется только при фактически привязанном драйвере:

```text
python3 tools/configure.py --ssid DemoAP --dry-run
python3 tools/configure.py --port <ESP_UART_PORT> --ssid DemoAP
python3 tools/configure.py --port <ESP_UART_PORT> --ssid DemoAP --save
```

Key вводится скрыто через prompt, не передаётся в argv/имени файла и не печатается.
Сначала утилита проверяет ATI именно нашего эмулятора, затем отправляет credentials.
Без заданного порта сканирования/изменения чужих устройств нет. `--save` посылает
AT&W явно; без него настройки остаются RAM-only. Требуется pyserial.

Автоматическая APN-доставка проверена на MBIM dev2; это не доказывает поддержку
каждой версии RouterOS или каждого режима аутентификации.
После изменения SSID неправильно объявлять link healthy до нового IP и свежей
SLA; сохранённый старый адрес/поколение не должны подтверждать новый профиль.

Основание полей:
[RouterOS LTE/APN](https://manual.mikrotik.com/docs/mobile-networking/lte-5g/).
Руководство производителя сейчас маркировано 7.26; физическая проверка выполнена на7.11.2; CHR SD-WAN использует7.24.5.
