import re,sys
data=open(sys.argv[1],'rb').read()
def compile_pat(p):
    toks=p.split()
    return re.compile(b''.join(b'.' if t in('?','??') else re.escape(bytes([int(t,16)])) for t in toks), re.S)
def count(p):
    r=compile_pat(p); n=0; pos=0; first=None
    while True:
        m=r.search(data,pos)
        if not m: break
        n+=1; first=first or m.start(); pos=m.start()+1
        if n>50: break
    return n,first
src=open(sys.argv[2]).read()
# take x86 branch: text after '#else' up to '#endif' within each Pattern_t, else whole
out=[]
for m in re.finditer(r'"([A-Za-z:_]+)",\s*(.*?)\n\t*\};', src, re.S):
    name,body=m.group(1),m.group(2)
    if '#else' in body: body=body.split('#else',1)[1]
    body=body.split('#endif')[0]
    s=re.search(r'"((?:[0-9A-F?]{1,2} ?)+)"',body)
    if not s: continue
    out.append((name,s.group(1).strip()))
for name,p in out:
    n,f=count(p)
    print(f"{n:>3} {hex(f) if f is not None else '-':>10}  {name}  [{p}]")
