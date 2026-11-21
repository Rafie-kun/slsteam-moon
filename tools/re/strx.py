"""strx.py SUBSTR... : chaînes contenant SUBSTR (octets), leurs références lea rip+disp et la fonction englobante.
Usage : python3 strx.py 'config changed' ['autre chaîne' ...]   (cible : rev.PATH)"""
import sys, re
sys.path.insert(0, '.')
from rev import *
import xref
e = xref.e; d = e.d
for sub in sys.argv[1:]:
    b = sub.encode()
    print('==', sub)
    for m in re.finditer(re.escape(b), d):
        s = d.rfind(b'\x00', 0, m.start()) + 1
        t = d.find(b'\x00', m.start())
        va = e.o2v(s)
        if va is None: continue
        txt = d[s:t].decode('latin1')
        xs = xref.xrefs(va)
        fns = sorted({xref.fn_info(x) for x in xs})
        print(hex(va), repr(txt[:90]), len(xs), fns[:4])
