import struct,re,bisect
import os; P32=os.path.expanduser(os.environ.get('REV32_PATH','~/.local/share/Steam/ubuntu12_32/steamclient.so'))
class Elf32:
    def __init__(s,path=P32):
        s.d=d=open(path,'rb').read()
        (s.shoff,)=struct.unpack_from('<I',d,0x20)
        shentsize,shnum,shstrndx=struct.unpack_from('<HHH',d,0x2E)
        secs=[]
        for i in range(shnum):
            o=s.shoff+i*shentsize
            name,typ,flags,addr,off,size=struct.unpack_from('<IIIIII',d,o)
            secs.append([name,typ,flags,addr,off,size])
        stro=secs[shstrndx][4]; s.sec={}
        for sc in secs:
            n=d[stro+sc[0]:d.index(b'\0',stro+sc[0])].decode(); s.sec[n]=sc
        (phoff,)=struct.unpack_from('<I',d,0x1C); phentsize,phnum=struct.unpack_from('<HH',d,0x2A)
        s.loads=[]
        for i in range(phnum):
            typ,off,va,pa,fsz,msz,fl,al=struct.unpack_from('<IIIIIIII',d,phoff+i*phentsize)
            if typ==1: s.loads.append((va,off,fsz,fl))
        s._f=None
    def v2o(s,va):
        for v,o,f,_ in s.loads:
            if v<=va<v+f: return o+va-v
        raise ValueError(hex(va))
    def o2v(s,off):
        for v,o,f,_ in s.loads:
            if o<=off<o+f: return v+off-o
        raise ValueError(hex(off))
    def funcs(s):
        if s._f: return s._f
        h=s.sec['.eh_frame_hdr']; base=h[3]; off=h[4]; d=s.d
        (n,)=struct.unpack_from('<i',d,off+8); st=[]
        for i in range(n):
            a,b=struct.unpack_from('<ii',d,off+12+i*8); st.append((base+a)&0xffffffff)
        s._f=sorted(st); return s._f
    def func_of(s,va):
        f=s.funcs(); i=bisect.bisect_right(f,va)-1; return f[i] if i>=0 else None
    def func_end(s,va):
        f=s.funcs(); i=bisect.bisect_right(f,va); return f[i] if i<len(f) else None
    def find(s,pat):
        toks=pat.split()
        rx=re.compile(b''.join(b'.' if t in('?','??') else re.escape(bytes([int(t,16)])) for t in toks),re.S)
        t=s.sec['.text']; lo=t[4]; hi=t[4]+t[5]; out=[]; pos=lo
        while True:
            m=rx.search(s.d,pos,hi)
            if not m: break
            out.append(s.o2v(m.start())); pos=m.start()+1
        return out
