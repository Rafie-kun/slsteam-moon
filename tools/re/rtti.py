from rev import *
import struct,re,bisect
e=Elf()
d=e.d
drr=e.sec['.data.rel.ro']
# relocations: in a PIE .so the pointers in .data.rel.ro are R_X86_64_RELATIVE with addend in .rela.dyn; raw bytes are 0.
def parse_rela():
    s=e.sec['.rela.dyn']; off=s[4]; n=s[5]//24
    m={}
    for i in range(n):
        o,info,add=struct.unpack_from('<QQq',d,off+i*24)
        if info&0xffffffff==8: m[o]=add
    return m
rel=parse_rela()
print('relative relocs',len(rel))
def qword(va):
    return rel.get(va)
