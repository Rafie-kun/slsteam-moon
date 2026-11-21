import sys,re,struct
sys.path.insert(0,'.')
from rev import *
e=Elf(); d=e.d
t=e.sec['.text']; lo=t[4]; hi=t[4]+t[5]
code=d[lo:hi]
def xrefs(va):
    """RIP-relative lea/mov references to va"""
    out=[]
    for m in re.finditer(rb'[\x48\x4c\x8b\x89\x8d]',code): pass
    # scan for disp32 patterns: instruction ends at i+len; brute: for each position with opcode lea (48/4C 8D modrm 05..3D)
    for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',code):
        i=m.start()
        if i+7>len(code): continue
        disp=struct.unpack_from('<i',code,i+3)[0]
        a=e.o2v(lo+i)
        if a+7+disp==va: out.append(a)
    return out
def fn_info(a):
    f=e.func_of(a); return hex(f),hex(e.func_end(f)-f)
if __name__=='__main__':
    for s in sys.argv[1:]:
        va=int(s,16); xs=xrefs(va)
        print(hex(va),[ (hex(x),)+fn_info(x) for x in xs])
