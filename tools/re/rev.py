import struct, bisect, re, sys, subprocess
import os; PATH=os.path.expanduser(os.environ.get('REV_PATH','~/.local/share/Steam/steamrt64/steamclient.so'))
class Elf:
    def __init__(s,path=PATH):
        s.d=open(path,'rb').read()
        d=s.d
        (s.shoff,)=struct.unpack_from('<Q',d,0x28)
        shentsize,shnum,shstrndx=struct.unpack_from('<HHH',d,0x3A)
        secs=[]
        for i in range(shnum):
            o=s.shoff+i*shentsize
            name,typ,flags,addr,off,size=struct.unpack_from('<IIQQQQ',d,o)
            secs.append([name,typ,flags,addr,off,size])
        stro=secs[shstrndx][4]
        s.sec={}
        for sc in secs:
            n=d[stro+sc[0]:d.index(b'\0',stro+sc[0]+0)].decode()
            s.sec[n]=sc
        # program headers for vaddr<->offset
        (phoff,)=struct.unpack_from('<Q',d,0x20)
        phentsize,phnum=struct.unpack_from('<HH',d,0x36)
        s.loads=[]
        for i in range(phnum):
            o=phoff+i*phentsize
            typ,flags,off,va,pa,fsz,msz,al=struct.unpack_from('<IIQQQQQQ',d,o)
            if typ==1: s.loads.append((va,off,fsz,flags))
        s._funcs=None
    def v2o(s,va):
        for v,o,f,_ in s.loads:
            if v<=va<v+f: return o+va-v
        raise ValueError(hex(va))
    def o2v(s,off):
        for v,o,f,_ in s.loads:
            if o<=off<o+f: return v+off-o
        raise ValueError(hex(off))
    def funcs(s):
        if s._funcs: return s._funcs
        h=s.sec['.eh_frame_hdr']; base=h[3]; off=h[4]
        d=s.d
        ver,ep,fc,tb=d[off],d[off+1],d[off+2],d[off+3]
        assert ep==0x1b and fc==3 and tb==0x3b, (ep,fc,tb)
        (n,)=struct.unpack_from('<i',d,off+8)
        starts=[]
        for i in range(n):
            a,b=struct.unpack_from('<ii',d,off+12+i*8)
            starts.append(base+a)
        s._funcs=sorted(starts)
        return s._funcs
    def func_of(s,va):
        f=s.funcs(); i=bisect.bisect_right(f,va)-1
        return f[i] if i>=0 else None
    def func_end(s,va):
        f=s.funcs(); i=bisect.bisect_right(f,va)
        return f[i] if i<len(f) else None
    def find(s,pat,start=None,end=None):
        toks=pat.split()
        rx=re.compile(b''.join(b'.' if t in('?','??') else re.escape(bytes([int(t,16)])) for t in toks),re.S)
        t=s.sec['.text']; lo=t[4]; hi=t[4]+t[5]
        out=[];pos=lo
        while True:
            m=rx.search(s.d,pos,hi)
            if not m: break
            out.append(s.o2v(m.start())); pos=m.start()+1
        return out
    def disasm(s,va,length):
        start=va
        r=subprocess.run(['objdump','-d','-M','intel','--no-show-raw-insn','--start-address',hex(start),'--stop-address',hex(start+length),PATH],capture_output=True,text=True)
        return '\n'.join(l for l in r.stdout.split('\n') if l.startswith(' ') or '>:' in l)
if __name__=='__main__':
    e=Elf()
    print({k:(hex(v[3]),hex(v[5])) for k,v in e.sec.items() if k in('.text','.rodata','.eh_frame_hdr','.data.rel.ro','.plt')})
    print('funcs',len(e.funcs()))
