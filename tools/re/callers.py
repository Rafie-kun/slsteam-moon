"""callers.py {32|64} VA... : appelants directs (call rel32) de chaque fonction, par fonction englobante.
32 : VA du client i386 (rev32), 64 : VA du client x86_64 (rev)."""
import sys, re, struct, collections
sys.path.insert(0, '.')
if sys.argv[1] == '32':
    import rev32; e = rev32.Elf32(); mask = 0xffffffff
else:
    import rev; e = rev.Elf(); mask = 0xffffffffffffffff
t = e.sec['.text']; lo = t[4]; hi = lo + t[5]; d = e.d
targets = {int(a, 16) for a in sys.argv[2:]}
res = collections.defaultdict(list)
for m in re.finditer(rb'\xe8', d[lo:hi]):
    i = lo + m.start()
    if i + 5 > hi: continue
    rel = struct.unpack_from('<i', d, i + 1)[0]
    va = e.o2v(i)
    tgt = (va + 5 + rel) & mask
    if tgt in targets: res[tgt].append(e.func_of(va))
for tg in sorted(targets):
    fs = res[tg]
    print(hex(tg), len(fs), 'appels ;', sorted({hex(f) for f in fs})[:12])
