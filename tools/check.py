#!/usr/bin/env python3
"""Host protocol tests; never connects to a router or a serial device."""
import hashlib
import gzip
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sources = [ROOT / 'components/modem/modem.c', ROOT / 'components/modem/at.c', ROOT / 'tests/test_modem.c']
with tempfile.TemporaryDirectory(prefix='sdwan-esp32-host-') as temp:
    exe = Path(temp) / 'test-modem'
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-g', '-O1', '--coverage', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    pending={'pass':False,'scope':'New host check started; not yet verified'}
    (ROOT/'host-validation.json').write_text(json.dumps(pending)+'\n')
    subprocess.run(['gcc', *flags, '-I', str(ROOT / 'components/modem/include'), *map(str, sources), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)],cwd=temp,text=True,capture_output=True,check=True)
    print(result.stdout, end='')
    subprocess.run(['gcov','-j','-b',*[str(p) for p in Path(temp).glob('*.gcno')]],cwd=temp,check=True,capture_output=True)
    coverage={}
    for p in Path(temp).glob('*.gcov.json.gz'):
        data=json.loads(gzip.decompress(p.read_bytes()))
        for item in data['files']:
            name=Path(item['file'])
            if name.parent.name!='modem':continue
            lines=item['lines'];funcs=item['functions']
            coverage[name.name]={'lines_hit':sum(x['count']>0 for x in lines),'lines_total':len(lines),
                                 'functions_hit':sum(x['execution_count']>0 for x in funcs),'functions_total':len(funcs),
                                 'branches_hit':sum(b['count']>0 for x in lines for b in x.get('branches',[])),
                                 'branches_total':sum(len(x.get('branches',[])) for x in lines)}
    report = {'pass': True, 'scope': 'Host C protocol tests with AddressSanitizer and UndefinedBehaviorSanitizer',
              'output': result.stdout.strip(), 'firmware_built': False, 'hardware_validated': False,'core_coverage':coverage,
              'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [*sources, ROOT/'components/modem/include/modem.h']}}
    (ROOT / 'host-validation.json').write_text(json.dumps(report, indent=2) + '\n')
