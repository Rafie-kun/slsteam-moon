"""rosette.py OFF32... : pour chaque fonction i386 (offset fichier donné par scan.py), les chaînes qu'elle
référence (base PIC GOT) puis, dans le client x86_64, les fonctions qui référencent les mêmes
chaînes (décompte par fonction). Sert à retrouver l'homologue 64 bits d'un locator i386.
Cibles : rev32.P32 et rev.PATH."""
import sys, re, struct, collections
sys.path.insert(0, '.')
import rev32
import xref
e32 = rev32.Elf32(); d32 = e32.d
e64 = xref.e; d64 = e64.d
got = e32.sec['.got.plt'][3] if '.got.plt' in e32.sec else e32.sec['.got'][3]
ro_lo = min(v for v, o, f, fl in e32.loads); ro_hi = max(v + f for v, o, f, fl in e32.loads)

def pic_base(fva, code):
    """GOT d'une fonction PIC i386 : retaddr(call thunk) + imm de l'add qui suit."""
    for m in re.finditer(rb'\xe8....(?:\x81[\xc0-\xc7]|\x05)', code[:80], re.S):
        i = m.start(); ret = fva + i + 5
        j = i + 5
        j += 2 if code[j] == 0x81 else 1
        return (ret + struct.unpack_from('<i', code, j)[0]) & 0xffffffff
    return got

def strings32(fva):
    end = e32.func_end(fva); o = e32.v2o(fva); code = d32[o:o + (end - fva)]
    base = pic_base(fva, code)
    out = []
    for m in re.finditer(rb'\x8d[\x80-\xbf]', code):
        i = m.start()
        if (code[i + 1] & 7) == 4 or i + 6 > len(code): continue
        disp = struct.unpack_from('<i', code, i + 2)[0]
        for a in ((base + disp) & 0xffffffff, (got + disp) & 0xffffffff):
            try: off = e32.v2o(a)
            except ValueError: continue
            t = d32.find(b'\x00', off)
            s = d32[off:t]
            if 4 <= len(s) <= 200 and all(32 <= c < 127 or c in (9, 10) for c in s):
                out.append(s.decode()); break
    return list(dict.fromkeys(out))

for arg in sys.argv[1:]:
    fva = e32.o2v(int(arg, 16)); f = e32.func_of(fva)
    print('=== i386', hex(f), 'size', hex(e32.func_end(f) - f))
    ss = strings32(f)
    tally = collections.Counter()
    for s in ss:
        b = s.encode() + b'\x00'
        fns = set()
        for m in re.finditer(re.escape(b), d64):
            st = m.start()
            if st > 0 and d64[st - 1] != 0: continue   # chaîne entière seulement
            va = e64.o2v(st)
            if va is None: continue
            for x in xref.xrefs(va): fns.add(e64.func_of(x))
        for fn in fns: tally[fn] += 1
        print('  ', repr(s[:100]), '->', [hex(x) for x in sorted(fns)][:4])
    print('  x64 candidates:', [(hex(a), n, hex(e64.func_end(a) - a)) for a, n in tally.most_common(4)])
