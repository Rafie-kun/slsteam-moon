"""strings64.py VA... : chaînes (lea rip+disp vers des chaînes ASCII) référencées par chaque fonction x86_64."""
import sys, re, struct
sys.path.insert(0, '.')
import xref
e = xref.e; d = e.d
for arg in sys.argv[1:]:
    f = int(arg, 16); end = e.func_end(f); o = e.v2o(f)
    code = d[o:o + end - f]; out = []
    for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', code):
        i = m.start(); disp = struct.unpack_from('<i', code, i + 3)[0]
        a = f + i + 7 + disp
        try: off = e.v2o(a)
        except Exception: continue
        t = d.find(b'\x00', off); s = d[off:t]
        if 4 <= len(s) <= 160 and all(32 <= c < 127 or c in (9, 10) for c in s): out.append(s.decode())
    print('===', hex(f), hex(end - f)); [print('   ', repr(s)) for s in dict.fromkeys(out)]
