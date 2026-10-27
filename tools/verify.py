#!/usr/bin/env python3
"""Bind host checks and both compiled firmware variants. No USB/API/flash operations."""
import datetime
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
from build import ROOT,PROJECT,TOOLS,PINS

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--profiles',nargs='+',choices=['hilink','at','mbim'],default=['hilink','at','mbim']);args=parser.parse_args()
    report={'pass':False,'scope':'Host and compiled-artifact verification only','physical_usb_tested':False,
            'routeros_lte_tested':False,'automatic_apn_wifi_tested':False}
    target=ROOT/'firmware-validation.json';target.write_text(json.dumps(report)+'\n')
    host=json.loads((ROOT/'host-validation.json').read_text());assert host['pass']
    for name,digest in host['source_sha256'].items():assert sha(ROOT/name)==digest,'Stale host tests: '+name
    for data in host['core_coverage'].values():
        assert data['lines_hit']==data['lines_total'] and data['functions_hit']==data['functions_total']
    units=subprocess.run([sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py','-v'],cwd=ROOT,capture_output=True,text=True)
    (ROOT/'python-tests.log').write_text(units.stdout+units.stderr);assert units.returncode==0
    count=re.search(r'Ran (\d+) tests?',units.stderr)
    assert count
    report['host']=host;report['python_helper_tests_passed']=int(count[1])
    usb_host=json.loads((ROOT/'usb-host-validation.json').read_text());assert usb_host['pass']
    for name,digest in usb_host['source_sha256'].items():assert sha(ROOT/name)==digest,'Stale USB host tests: '+name
    report['usb_host']=usb_host
    if 'mbim' in args.profiles:
        mbim_host=json.loads((ROOT/'tests/mbim/validation.json').read_text());assert mbim_host['pass']
        for name,digest in mbim_host['source_sha256'].items():assert sha(Path(name))==digest,'Stale MBIM host check: '+name
        coverage=mbim_host['coverage'];assert coverage['lines_hit']==coverage['lines_total'] and coverage['functions_hit']==coverage['functions_total']
        report['mbim_host']=mbim_host
        driver_host=json.loads((ROOT/'tests/mbim/driver-validation.json').read_text());assert driver_host['pass']
        for name,digest in driver_host['source_sha256'].items():assert sha(Path(name))==digest,'Stale MBIM USB driver check: '+name
        report['mbim_usb_driver_host']=driver_host
    report['configure_helper_sha256']=sha(ROOT/'tools/configure.py');report['profiles']={}
    python=TOOLS/'python_env/idf5.5_py3.12_env/bin/python'
    for profile in args.profiles:
        folder=ROOT/'dist'/profile;build=TOOLS/'projects'/hashlib.sha256(str(ROOT).encode()).hexdigest()[:12]/('build-native-'+profile)
        data=json.loads((folder/'build-report.json').read_text());assert data['built'] and data['pins']==PINS
        for name,digest in data['source_sha256'].items():assert sha(ROOT/name)==digest,'Stale firmware source: '+name
        for name,digest in data['input_sha256'].items():assert sha(ROOT/name)==digest,'Stale build inputs: '+name
        for name,item in data['files'].items():assert sha(folder/name)==item['sha256']
        assert sha(build/'sdkconfig')==data['sdkconfig_sha256']
        config=(build/'sdkconfig').read_text()
        netmode='CONFIG_TINYUSB_NET_MODE_NONE=y' if profile=='mbim' else 'CONFIG_TINYUSB_NET_MODE_ECM_RNDIS=y'
        for line in ['CONFIG_EMU_LINUX_RNDIS=y',netmode,'CONFIG_LWIP_IPV4_NAPT=y','CONFIG_ESPTOOLPY_FLASHMODE_DIO=y','CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y','CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192']:
            assert line in config,line
        assert 'CONFIG_SPIRAM=y' not in config
        assert ('CONFIG_EMU_AT_USB=y' in config)==(profile=='at')
        assert ('CONFIG_EMU_MBIM=y' in config)==(profile=='mbim')
        if profile=='mbim':assert 'CONFIG_LWIP_DHCPS_STATIC_ENTRIES=y' in config
        checked=subprocess.run([str(python),str(ROOT/'tools/usb_contract.py'),'--profile',profile],capture_output=True,text=True)
        (ROOT/('usb-contract-'+profile+'.log')).write_text(checked.stdout+checked.stderr)
        if checked.returncode:raise RuntimeError('Compiled USB contract failed; see usb-contract-'+profile+'.log')
        contract=json.loads((folder/'usb-contract.json').read_text());assert contract['pass']
        assert contract['firmware_sha256']==data['files']['sdwan_esp32_lte.bin']['sha256']
        metadata=json.loads((build/'flasher_args.json').read_text())
        files={offset:Path(name).name for offset,name in metadata['flash_files'].items()}
        assert files=={'0x0':'bootloader.bin','0x8000':'partition-table.bin','0x10000':'sdwan_esp32_lte.bin'}
        flash={'profile':profile,'chip':'esp32s3','flash_settings':metadata['flash_settings'],'files':files,
               'sha256':{name:data['files'][name]['sha256'] for name in files.values()},
               'requires_chip_flash_identification_and_backup':True,'device_accessed':False}
        (folder/'flash-plan.json').write_text(json.dumps(flash,indent=2)+'\n')
        report['profiles'][profile]={'app_bytes':data['files']['sdwan_esp32_lte.bin']['bytes'],
              'image_sha256':contract['firmware_sha256'],'usb_contract':'dist/'+profile+'/usb-contract.json',
              'inputs_unchanged_during_build':True,'flash_plan':'dist/'+profile+'/flash-plan.json'}
    report['pass']=True;report['checked_at_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    target.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['pass','profiles','physical_usb_tested','automatic_apn_wifi_tested']},indent=2))

if __name__=='__main__':main()
