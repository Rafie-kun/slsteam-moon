"""Rebuild ti.pkl (typeinfos and vtables from RELATIVE relocations) for rev.PATH.

Run from this directory after pointing rev.PATH at the client to analyse.
rtti.py/mapnames.py/xref.py load ti.pkl.
"""
import pickle, re, sys
sys.path.insert(0, '.')
from rtti import *

inv = {}
for va, tgt in rel.items():
    inv.setdefault(tgt, []).append(va)

def cstr(va):
    o = e.v2o(va)
    return d[o:d.index(b'\0', o)].decode('latin1')

ti = {}
for va, tgt in rel.items():
    try:
        s = cstr(tgt)
    except Exception:
        continue
    if re.match(r'^\d+[A-Za-z_]', s) and len(s) < 80 and (va - 8) in rel:
        ti[va - 8] = s
vt = {}
for t, name in ti.items():
    for va in inv.get(t, []):
        vt.setdefault(name, []).append(va + 8)
pickle.dump((ti, vt), open('ti.pkl', 'wb'))
print('typeinfos', len(ti), 'classes', len(vt))
