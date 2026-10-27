import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from conformance import verify

ROOT=Path(__file__).resolve().parent
MODEM=ROOT.parents[1]/'components/modem'
MBIM=MODEM.parent/'mbim'
sources=[MBIM/'mbim.c',MODEM/'modem.c',ROOT/'test_mbim.c']
report={'pass':False,'hardware_validated':False,'firmware_integrated':False}
target=ROOT/'validation.json';target.write_text(json.dumps(report)+'\n')
with tempfile.TemporaryDirectory(prefix='sdwan-mbim-host-') as temp:
    folder=Path(temp);exe=folder/'test-mbim'
    flags=['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-g','-O1','--coverage','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(['gcc',*flags,'-I',str(MODEM/'include'),'-I',str(MBIM/'include'),*map(str,sources),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],cwd=folder,capture_output=True,text=True,check=True)
    decoded=verify(folder)
    subprocess.run(['gcov','-j','-b',str(folder/'test-mbim-mbim.gcno')],cwd=folder,check=True,capture_output=True)
    coverage={}
    for f in folder.glob('*.gcov.json.gz'):
        for item in json.loads(gzip.decompress(f.read_bytes()))['files']:
            if Path(item['file'])!=MBIM/'mbim.c':continue
            lines=item['lines'];funcs=item['functions']
            coverage={'lines_hit':sum(x['count']>0 for x in lines),'lines_total':len(lines),
                      'functions_hit':sum(x['execution_count']>0 for x in funcs),'functions_total':len(funcs),
                      'branches_hit':sum(b['count']>0 for x in lines for b in x.get('branches',[])),
                      'branches_total':sum(len(x.get('branches',[])) for x in lines),
                      'uncovered_lines':[x['line_number'] for x in lines if not x['count']]}
    fixtures=ROOT/'fixtures';fixtures.mkdir(exist_ok=True)
    for f in fixtures.glob('*'):f.unlink()
    for f in folder.glob('*.bin'):shutil.copy2(f,fixtures/f.name)
    for f in folder.glob('*.txt'):shutil.copy2(f,fixtures/f.name)
    report.update({'pass':True,'test_output':result.stdout.strip(),'coverage':coverage,'libmbim_decoded':decoded,
                   'source_sha256':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*sources,MBIM/'include/mbim.h',ROOT/'conformance.py']}})
    target.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k not in ('source_sha256','libmbim_decoded')},indent=2))
