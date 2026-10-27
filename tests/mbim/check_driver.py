"""Actual mbim_usb.c against deterministic USB HAL, never a real USB host."""
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parent;PROJECT=ROOT.parents[1]
sources=[PROJECT/'main/mbim_usb.c',PROJECT/'components/mbim/mbim.c',PROJECT/'components/modem/modem.c',ROOT/'test_usb_driver.c']
report={'pass':False,'physical_usb_tested':False,'scope':'Actual firmware USB driver with a deterministic HAL, ASan/UBSan'}
target=ROOT/'driver-validation.json';target.write_text(json.dumps(report)+'\n')
with tempfile.TemporaryDirectory(prefix='sdwan-mbim-driver-') as tmp:
    folder=Path(tmp);exe=folder/'test-usb'
    flags=['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-g','-O1','--coverage','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    includes=[ROOT/'stubs',PROJECT/'main',PROJECT/'components/mbim/include',PROJECT/'components/modem/include']
    args=['gcc',*flags]
    for path in includes:args+=['-I',str(path)]
    subprocess.run([*args,*map(str,sources),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],cwd=folder,text=True,capture_output=True,check=True)
    subprocess.run(['gcov','-j','-b',str(folder/'test-usb-mbim_usb.gcno')],cwd=folder,capture_output=True,check=True)
    coverage={}
    for p in folder.glob('*.gcov.json.gz'):
        for f in json.loads(gzip.decompress(p.read_bytes()))['files']:
            if Path(f['file'])!=sources[0]:continue
            lines=f['lines'];funcs=f['functions']
            coverage={'lines_hit':sum(x['count']>0 for x in lines),'lines_total':len(lines),
                      'functions_hit':sum(x['execution_count']>0 for x in funcs),'functions_total':len(funcs),
                      'branches_hit':sum(b['count']>0 for x in lines for b in x.get('branches',[])),
                      'branches_total':sum(len(x.get('branches',[])) for x in lines),
                      'uncovered_lines':[x['line_number'] for x in lines if not x['count']]}
    inputs=[*sources,PROJECT/'main/mbim_usb.h',*sorted((ROOT/'stubs').rglob('*.h'))]
    report.update(pass_=True,test_output=result.stdout.strip(),coverage=coverage,source_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs})
    report.pop('pass_');report['pass']=True;target.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
