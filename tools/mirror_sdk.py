#!/usr/bin/env python3
"""Put a pinned SDK build cache on WSL's Linux filesystem. Project sources stay on C:."""
import os
import sys
import json
import subprocess
from build import ROOT,PROJECT,IDF,USB,TOOLS,PINS

def mirror(source,destination,pin):
    if not (destination/'.git').exists():
        destination.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['git','clone','--shared','--no-checkout',str(source),str(destination)],check=True)
        subprocess.run(['git','checkout','--detach',pin],cwd=destination,check=True)
    assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=destination,text=True).strip()==pin

def main():
    soak=PROJECT/'runtime/overlay-lab/runtime/network-soak.json'
    if soak.exists() and json.loads(soak.read_text()).get('status') in ('preparing','running','restoring'):
        raise SystemExit('SDK cache preparation deferred while soak is active')
    os.nice(19);native=TOOLS/'sources';mirror(IDF,native/'esp-idf',PINS['idf'])
    for line in subprocess.check_output(['git','config','--file','.gitmodules','--get-regexp','path'],cwd=IDF,text=True).splitlines():
        path=line.split(None,1)[1];source=IDF/path
        if not (source/'.git').exists():continue
        pin=subprocess.check_output(['git','ls-tree','HEAD',path],cwd=IDF,text=True).split()[2]
        mirror(source,native/'esp-idf'/path,pin)
    mirror(USB,native/'esp-usb',PINS['usb'])
    tiny=native/'esp-usb/device/esp_tinyusb/tinyusb'
    mirror(USB/'device/esp_tinyusb/tinyusb',tiny,PINS['tinyusb'])
    patch=ROOT/'tinyusb-linux-rndis.patch';dirty=subprocess.check_output(['git','diff','--','src/device/usbd.h','src/class/cdc/cdc_device.c'],cwd=tiny)
    if not dirty:subprocess.run(['git','apply',str(patch)],cwd=tiny,check=True)
    else:assert dirty==patch.read_bytes()
    env=dict(os.environ,IDF_TOOLS_PATH=str(TOOLS),IDF_PATH=str(native/'esp-idf'))
    subprocess.run([sys.executable,str(native/'esp-idf/tools/idf_tools.py'),'install','--targets','esp32s3'],env=env,check=True)
    print('Pinned native SDK cache ready; firmware sources remain in '+str(ROOT))

if __name__=='__main__':main()
