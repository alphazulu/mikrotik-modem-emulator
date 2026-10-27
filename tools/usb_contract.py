#!/usr/bin/env python3
"""Inspect actual compiled ELF descriptors, not a physical USB enumeration."""
import argparse
import hashlib
import io
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parents[1]

def symbol(elf,name):
    symbols=elf.get_section_by_name('.symtab').get_symbol_by_name(name)
    if not symbols or len(symbols)!=1:raise ValueError('Unique USB descriptor symbol absent: '+name)
    s=symbols[0];sec=elf.get_section(s['st_shndx']);offset=s['st_value']-sec['sh_addr']
    return sec.data()[offset:offset+s['st_size']]

def inspect(profile):
    folder=ROOT/'dist'/profile;path=folder/'sdwan_esp32_lte.elf'
    elf=ELFFile(io.BytesIO(path.read_bytes()))
    assert elf['e_machine']=='EM_XTENSA';cfg=symbol(elf,'configuration');device=symbol(elf,'device')
    expected_id=bytes([0x3a,0x30,0,0x40]) if profile=='mbim' else bytes([0xd1,0x12,0xdb,0x14])
    assert len(device)==18 and device[8:12]==expected_id
    bos=os_set=b''
    if profile=='mbim':assert device[2:4]==bytes([0,2]) and device[12:14]==bytes([2,1])
    else:
        assert device[2:4]==bytes([0x10,2]) and device[12:14]==bytes([1,1])
        bos=symbol(elf,'emu_usb_bos');os_set=symbol(elf,'emu_ms_os_set')
        assert len(bos)==33 and bos[:5]==bytes([5,15,33,0,1])
        assert bos[5:9]==bytes([28,16,5,0])
        assert bos[9:25]==bytes.fromhex('df60ddd88945c74c9cd2659d9e648a9f')
        assert bos[25:]==bytes([0,0,3,6,46,0,0x20,0])
        assert len(os_set)==46 and os_set[:10]==bytes([10,0,0,0,0,0,3,6,46,0])
        assert os_set[10:18]==bytes([8,0,1,0,1,0,36,0])
        assert os_set[18:26]==bytes([8,0,2,0,0,0,28,0])
        assert os_set[26:]==bytes([20,0,3,0])+b'RNDIS\0\0\0'+b'5162001\0'
    assert cfg[0:2]==bytes([9,2]) and int.from_bytes(cfg[2:4],'little')==len(cfg)
    interfaces=[];endpoints=[];i=0
    while i<len(cfg):
        size=cfg[i];assert size>=2 and i+size<=len(cfg)
        row=cfg[i:i+size]
        if row[1]==4:
            interface={'number':row[2],'class':row[5],'subclass':row[6],'protocol':row[7]}
            if profile=='mbim':interface['alternate']=row[3]
            interfaces.append(interface)
        if row[1]==5:endpoints.append({'address':row[2],'type':row[3]&3,'packet_size':int.from_bytes(row[4:6],'little')})
        i+=size
    count=4 if profile=='at' else 2
    if profile=='mbim':
        assert cfg[4]==2 and len(cfg)==95
        assert interfaces==[{'number':0,'class':2,'subclass':14,'protocol':0,'alternate':0},
                            {'number':1,'class':10,'subclass':0,'protocol':2,'alternate':0},
                            {'number':1,'class':10,'subclass':0,'protocol':2,'alternate':1}]
        assert bytes([12,0x24,0x1b,0,1,0,4,0,0,0xdc,5,0x20]) in cfg
        assert bytes([8,0x24,0x1c,0,1,1,0xdc,5]) in cfg
    else:
        assert cfg[4]==len(interfaces)==count
        assert interfaces[:2]==[{'number':0,'class':2,'subclass':2,'protocol':255},{'number':1,'class':10,'subclass':0,'protocol':0}]
    if profile=='at':assert interfaces[2:]==[{'number':2,'class':2,'subclass':2,'protocol':1},{'number':3,'class':10,'subclass':0,'protocol':0}]
    assert len(endpoints)==(6 if profile=='at' else 3)
    assert len({r['address'] for r in endpoints})==len(endpoints)
    assert sum(bool(r['address']&128) for r in endpoints)+1<=5
    assert all(e['packet_size']==64 for e in endpoints if e['type']==2)
    image=(folder/'sdwan_esp32_lte.bin').read_bytes()
    for forbidden in [b'test-only-key',b'DEMO-ONLY-KEY',b'DemoAP']:
        assert forbidden not in image,'Test credentials embedded in firmware'
    result={'pass':True,'scope':'Actual ELF USB descriptor contract only, not USB host enumeration or LTE recognition',
            'profile':profile,'elf_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'firmware_sha256':hashlib.sha256(image).hexdigest(),'interfaces':interfaces,'endpoints':endpoints,
            'ms_os_20':{'bos_hex':bos.hex(),'descriptor_set_hex':os_set.hex(),'compatible_id':'RNDIS','subcompatible_id':'5162001','function_interface':0} if profile!='mbim' else None,
            'physical_usb_tested':False}
    (folder/'usb-contract.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--profile',choices=['hilink','at','mbim'],required=True);a=p.parse_args();print(json.dumps(inspect(a.profile),indent=2))
