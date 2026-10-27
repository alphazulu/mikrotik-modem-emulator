#!/usr/bin/env python3
"""Exercise actual portable Windows USB control response, without any hardware."""
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sources=[ROOT/'main/usb_os.c',ROOT/'tests/test_usb_os.c']
target=ROOT/'usb-host-validation.json'
target.write_text(json.dumps({'pass':False,'scope':'New USB host check started'})+'\n')
with tempfile.TemporaryDirectory(prefix='sdwan-esp-usb-') as temp:
    exe=Path(temp)/'test-usb-os'
    flags=['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-g','-O1','--coverage',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(['gcc',*flags,'-I',str(ROOT/'main'),*map(str,sources),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],cwd=temp,text=True,capture_output=True,check=True)
    subprocess.run(['gcov','-j','-b',*[str(p) for p in Path(temp).glob('*.gcno')]],cwd=temp,check=True,capture_output=True)
    coverage={}
    for p in Path(temp).glob('*.gcov.json.gz'):
        for item in json.loads(gzip.decompress(p.read_bytes()))['files']:
            if Path(item['file'])!=ROOT/'main/usb_os.c':continue
            lines=item['lines'];funcs=item['functions']
            coverage={'lines_hit':sum(x['count']>0 for x in lines),'lines_total':len(lines),
                      'functions_hit':sum(x['execution_count']>0 for x in funcs),'functions_total':len(funcs),
                      'branches_hit':sum(b['count']>0 for x in lines for b in x.get('branches',[])),
                      'branches_total':sum(len(x.get('branches',[])) for x in lines)}
    assert coverage and coverage['lines_hit']==coverage['lines_total'] and coverage['functions_hit']==coverage['functions_total']
    report={'pass':True,'scope':'Portable USB BOS/MS OS 2.0 request policy with ASan/UBSan; not physical enumeration',
            'output':result.stdout.strip(),'coverage':coverage,'hardware_validated':False,
            'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*sources,ROOT/'main/usb_os.h']}}
    target.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
