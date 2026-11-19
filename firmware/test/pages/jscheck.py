"""Syntax-checks every script in every page of pages.h with node --check.

  python jscheck.py ../../netmon/src/hw/pages.h
"""
import os, re, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_pages import extract

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..', 'netmon', 'src', 'hw', 'pages.h')
bad = 0
with tempfile.TemporaryDirectory() as d:
    for k, v in extract(path).items():
        for i, m in enumerate(re.finditer(r'<script>(.*?)</script>', v, re.S)):
            fn = os.path.join(d, '%s_%d.js' % (k, i))
            open(fn, 'w', encoding='utf-8').write(m.group(1))
            r = subprocess.run(['node', '--check', fn], capture_output=True, text=True)
            if r.returncode:
                bad += 1
                print(k, r.stderr[:600])
        print(k, len(v.encode()), 'bytes')
sys.exit(1 if bad else 0)
