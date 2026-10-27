#!/usr/bin/env python3
"""Build pinned ESP32-S3 firmware with one low-priority compiler; never flashes."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROJECT = next((p for p in ROOT.parents if (p/'runtime/overlay-lab/runtime/network-soak.json').exists()), ROOT)
DEPS = ROOT / '.deps'
IDF = DEPS / 'esp-idf'
USB = DEPS / 'esp-usb'
TOOLS = Path(os.environ.get('MODEM_EMU_TOOLS', str(Path.home()/'.cache/mikrotik-modem-emulator')))
PINS = {'idf': '30aaf64524299d3bde422ca9a2848090d1bc5d0f', 'usb': '68bf7f9613dbb1a31094fdb903209ae3e4bf388d', 'tinyusb': '894ea01409e0407a7dbe0ee29b2d58a4691f9046'}

def main():
    p=argparse.ArgumentParser();p.add_argument('--profile',choices=['hilink','at','mbim'],default='hilink')
    p.add_argument('--native-sdk',action='store_true',help='Use the Linux SDK cache created by mirror_sdk.py')
    args=p.parse_args();idf=TOOLS/'sources/esp-idf' if args.native_sdk else IDF;usb=TOOLS/'sources/esp-usb' if args.native_sdk else USB
    owner=(ROOT/'build.lock').open('a+')
    try:fcntl.flock(owner,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BlockingIOError:raise SystemExit('Another ESP32 build owns the source/cache; do not duplicate it')
    soak=PROJECT/'runtime/overlay-lab/runtime/network-soak.json'
    if soak.exists() and json.loads(soak.read_text()).get('status') in ('preparing','running','restoring'):
        raise SystemExit('Build deferred: an existing SD-WAN soak is active. Do not add load to its host.')
    for key,folder in [('idf',idf),('usb',usb),('tinyusb',usb/'device/esp_tinyusb/tinyusb')]:
        actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=folder,text=True).strip()
        if actual!=PINS[key]:raise SystemExit('Dependency pin mismatch: '+key)
    h=usb/'device/esp_tinyusb/tinyusb/src/device/usbd.h'
    if '#if defined(CONFIG_EMU_LINUX_RNDIS) && CONFIG_EMU_LINUX_RNDIS' not in h.read_text():
        raise SystemExit('Apply the recorded tinyusb-linux-rndis.patch before building')
    inputs=[p for folder in ('main','components') for p in (ROOT/folder).rglob('*') if p.is_file()]
    inputs += [ROOT/name for name in ('CMakeLists.txt','sdkconfig.defaults','sdkconfig.at','sdkconfig.mbim','partitions.csv','tinyusb-linux-rndis.patch')]
    before={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    env=dict(os.environ,IDF_PATH=str(idf),IDF_TOOLS_PATH=str(TOOLS),EMU_USB_PATH=str(usb),CMAKE_BUILD_PARALLEL_LEVEL='1',IDF_SKIP_CHECK_SUBMODULES='1')
    exported=subprocess.check_output([sys.executable,str(idf/'tools/idf_tools.py'),'export','--format','key-value'],env=env,text=True)
    for line in exported.splitlines():
        if '=' in line:
            key,value=line.split('=',1);env[key]=value.replace('$PATH',os.environ['PATH'])
    python=Path(env['IDF_PYTHON_ENV_PATH'])/'bin/python'
    defaults=str(ROOT/'sdkconfig.defaults')
    if args.profile=='at':defaults+=';'+str(ROOT/'sdkconfig.at')
    if args.profile=='mbim':defaults+=';'+str(ROOT/'sdkconfig.mbim')
    build=TOOLS/'projects'/hashlib.sha256(str(ROOT).encode()).hexdigest()[:12]/('build-'+('native-' if args.native_sdk else '')+args.profile);build.mkdir(parents=True,exist_ok=True)
    # This cache is generated solely from tracked defaults, never user overrides.
    # Existing sdkconfig otherwise wins over changed defaults on reconfigure.
    (build/'sdkconfig').unlink(missing_ok=True)
    argv=[str(python),str(idf/'tools/idf.py'),'-B',str(build),'-DIDF_TARGET=esp32s3',
          '-DSDKCONFIG='+str(build/'sdkconfig'),'-DSDKCONFIG_DEFAULTS='+defaults,'reconfigure']
    os.nice(19)
    subprocess.run(argv,cwd=ROOT,env=env,check=True)
    subprocess.run(['cmake','--build',str(build),'--parallel','1'],cwd=ROOT,env=env,check=True)
    after={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    if before!=after:raise SystemExit('Firmware input changed during build; artifacts are not accepted')
    dist=ROOT/'dist'/args.profile;dist.mkdir(parents=True,exist_ok=True)
    files={'sdwan_esp32_lte.bin':build/'sdwan_esp32_lte.bin','sdwan_esp32_lte.elf':build/'sdwan_esp32_lte.elf',
           'bootloader.bin':build/'bootloader/bootloader.bin','partition-table.bin':build/'partition_table/partition-table.bin'}
    hashes={}
    for name,source in files.items():
        data=source.read_bytes();(dist/name).write_bytes(data);hashes[name]={'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}
    source={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('main','components') for p in (ROOT/folder).rglob('*') if p.is_file()}
    report={'built':True,'profile':args.profile,'target':'esp32s3','pins':PINS,'source_sha256':source,'input_sha256':after,
            'sdkconfig_sha256':hashlib.sha256((build/'sdkconfig').read_bytes()).hexdigest(),
            'flash_metadata':json.loads((build/'flasher_args.json').read_text())['flash_files'],
            'tinyusb_patch_sha256':hashlib.sha256((ROOT/'tinyusb-linux-rndis.patch').read_bytes()).hexdigest(),
            'hardware_validated':False,'automatic_routeros_apn_delivery_validated':False,'files':hashes}
    (dist/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'built':True,'profile':args.profile,'output':str(dist),'hardware_validated':False}))

if __name__=='__main__':main()
