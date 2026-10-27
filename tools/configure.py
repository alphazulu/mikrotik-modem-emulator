#!/usr/bin/env python3
"""Configure only the identified emulator over its AT UART/CDC, never RouterOS API."""
import argparse
import base64
import getpass
import os
import sys
import time

def profile_commands(ssid, password, *, open_network=False):
    data=ssid.encode('utf-8')
    if not 1<=len(data)<=32 or any(c in ssid for c in '\0\r\n'):
        raise ValueError('SSID must contain 1..32 UTF-8 bytes and no NUL/newline')
    if open_network:
        if password:raise ValueError('Open network must have an empty key')
    else:
        valid=8<=len(password)<=63 and all(32<=ord(c)<=126 for c in password)
        valid=valid or len(password)==64 and all(c in '0123456789abcdefABCDEF' for c in password)
        if not valid:raise ValueError('Key must be 8..63 printable ASCII or 64 hexadecimal characters')
    # Avoid AT quote/encoding ambiguities. RouterOS MBIM mapping can use user=SSID directly.
    apn='wifi.'+base64.b32encode(data).decode('ascii').rstrip('=')
    escaped=password.replace('"','""')
    return ['AT+CGDCONT=1,"IP","'+apn+'"', 'AT+CGAUTH=1,'+('0' if open_network else '1')+',"","'+escaped+'"','AT+CGACT=1,1']

def exchange(port, command, *, activation=False):
    port.write((command+'\r').encode('utf-8'));port.flush()
    deadline=time.monotonic()+(35 if activation else 3);lines=bytearray()
    while time.monotonic()<deadline:
        byte=port.read(1)
        if not byte:continue
        lines.extend(byte)
        if len(lines)>2048:raise RuntimeError('Unexpected oversized response; contents withheld')
        if any(marker in lines for marker in (b'ESP-ROM:',b'assert failed:',b'Guru Meditation',b'Brownout detector')):
            raise RuntimeError('Emulator restarted or panicked during command; contents withheld')
        if lines.endswith(b'\r\nOK\r\n'):return bytes(lines)
        if lines.endswith(b'\r\nERROR\r\n') or b'+CME ERROR:' in lines:
            raise RuntimeError('Emulator rejected command; response contents withheld')
    raise RuntimeError('AT response timeout; no credential data printed')

def configure(port, ssid, password, *, open_network=False, save=False):
    commands=profile_commands(ssid,password,open_network=open_network)
    port.reset_input_buffer()
    def step(label,command,**kwargs):
        try:return exchange(port,command,**kwargs)
        except RuntimeError as error:raise RuntimeError(label+': '+str(error)) from None
    step('Disable echo','ATE0')
    identity=step('Check emulator identity','ATI')
    if b'SDWAN ESP32-S3 USB modem emulator' not in identity:
        raise RuntimeError('Serial device identity mismatch; credentials were not sent')
    for label,command in zip(('Stage APN','Stage authentication','Activate Wi-Fi'),commands):
        step(label,command,activation=command.startswith('AT+CGACT='))
    if save:step('Persist active profile','AT&W')

def open_port(serial_module, name):
    # CH343 DTR/RTS drive the ESP auto-reset circuit. Set them BEFORE open;
    # Serial(name,...) defaults can reset the device before the first ATE0.
    kwargs={'exclusive':True} if os.name=='posix' else {}
    port=serial_module.Serial(port=None,baudrate=115200,timeout=.2,write_timeout=2,**kwargs)
    try:
        port.dtr=False
        port.rts=False
        port.port=name
        port.open()
    except BaseException:
        port.close()
        raise
    return port

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port');p.add_argument('--ssid',required=True);p.add_argument('--open',action='store_true',dest='open_network')
    p.add_argument('--save',action='store_true',help='Explicitly persist active Wi-Fi credentials in ESP NVS')
    p.add_argument('--dry-run',action='store_true',help='Show redacted commands, without accessing any device')
    a=p.parse_args()
    if a.dry_run:
        commands=profile_commands(a.ssid,'' if a.open_network else 'DEMO-ONLY-KEY',open_network=a.open_network)
        print(commands[0]);print('AT+CGAUTH=1,<auth>,"","<redacted>"');print(commands[2]);print('AT&W' if a.save else 'RAM only; no persistence requested');return
    if not a.port:p.error('--port is required unless --dry-run is selected')
    password='' if a.open_network else getpass.getpass('Wi-Fi key (not logged): ')
    import serial
    with open_port(serial,a.port) as port:
        configure(port,a.ssid,password,open_network=a.open_network,save=a.save)
    print('Wi-Fi session activated; '+('saved explicitly in ESP NVS' if a.save else 'RAM only'))

if __name__=='__main__':
    try:main()
    except (OSError,RuntimeError,ValueError) as error:
        print(str(error),file=sys.stderr);raise SystemExit(1)
