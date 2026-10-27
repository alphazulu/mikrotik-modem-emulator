#!/usr/bin/env python3
"""Fetch pinned firmware dependencies into private directories. Does not flash or access routers."""
import json
import os
from pathlib import Path
import subprocess
import sys
from build import ROOT,PROJECT,DEPS,IDF,USB,TOOLS,PINS

def run(*args,cwd=None,env=None):
    subprocess.run(list(map(str,args)),cwd=cwd,env=env,check=True)

def checkout(path,url,commit):
    if not path.exists():
        path.mkdir(parents=True);run('git','init',path)
        run('git','remote','add','origin',url,cwd=path)
        run('git','fetch','--depth','1','origin',commit,cwd=path)
        run('git','checkout','--detach',commit,cwd=path)
    actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=path,text=True).strip()
    if actual!=commit:raise SystemExit('Existing dependency has another revision: '+str(path))

def main():
    soak=PROJECT/'runtime/overlay-lab/runtime/network-soak.json'
    if soak.exists() and json.loads(soak.read_text()).get('status') in ('preparing','running','restoring'):
        raise SystemExit('Bootstrap deferred while SD-WAN soak is active')
    os.nice(19);DEPS.mkdir(exist_ok=True);TOOLS.mkdir(parents=True,exist_ok=True)
    checkout(IDF,'https://github.com/espressif/esp-idf.git',PINS['idf'])
    modules=['components/esp_wifi/lib','components/esp_phy/lib','components/esp_coex/lib','components/lwip/lwip',
             'components/mbedtls/mbedtls','components/heap/tlsf','components/bootloader/subproject/components/micro-ecc/micro-ecc',
             'components/mqtt/esp-mqtt','components/json/cJSON','components/unity/unity','components/cmock/CMock',
             'components/protobuf-c/protobuf-c','components/spiffs/spiffs']
    run('git','submodule','update','--init','--depth','1','--jobs','1',*modules,cwd=IDF)
    checkout(USB,'https://github.com/espressif/esp-usb.git',PINS['usb'])
    tiny=USB/'device/esp_tinyusb/tinyusb'
    if not (tiny/'.git').exists():run('git','submodule','update','--init','--depth','1',cwd=USB)
    checkout(tiny,'https://github.com/espressif/tinyusb.git',PINS['tinyusb'])
    patch=ROOT/'tinyusb-linux-rndis.patch'
    dirty=subprocess.check_output(['git','diff','--','src/device/usbd.h','src/class/cdc/cdc_device.c'],cwd=tiny)
    if not dirty:
        run('git','apply','--check',patch,cwd=tiny);run('git','apply',patch,cwd=tiny)
    elif dirty!=patch.read_bytes():raise SystemExit('Unexpected TinyUSB descriptor patch; not overwriting it')
    env=dict(os.environ,IDF_PATH=str(IDF),IDF_TOOLS_PATH=str(TOOLS))
    run(sys.executable,IDF/'tools/idf_tools.py','install','--targets','esp32s3',env=env)
    run(sys.executable,IDF/'tools/idf_tools.py','install-python-env',env=env)
    python=TOOLS/'python_env/idf5.5_py3.12_env/bin/python'
    run(python,'-m','pip','install','cmake==3.30.9','ninja==1.11.1.4',env=env)
    print('Pinned ESP32-S3 environment ready; no device accessed')

if __name__=='__main__':main()
