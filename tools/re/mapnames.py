import sys,pickle,re,struct
sys.path.insert(0,'.')
from rtti import *
ti,vt=pickle.load(open('ti.pkl','rb'))
t=e.sec['.text']; tlo=t[3]; thi=t[3]+t[5]
ro=e.sec['.rodata']; rlo=ro[3]; rhi=ro[3]+ro[5]
def slots(vb,maxn=600):
    out=[]
    for k in range(maxn):
        v=rel.get(vb+8*k)
        if v is None or not (tlo<=v<thi): break
        out.append(v)
    return out
def strat(va):
    if not (rlo<=va<rhi): return None
    o=e.v2o(va); end=d.find(b'\0',o,o+80)
    if end<0: return None
    b=d[o:end]
    if len(b)<3 or not re.fullmatch(rb'[A-Za-z_][A-Za-z0-9_]*',b): return None
    return b.decode()
def names_for(fn):
    end=e.func_end(fn); out=[]
    code=d[e.v2o(fn):e.v2o(fn)+min(end-fn,0x200)]
    for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',code):
        i=m.start()
        if i+7>len(code): continue
        disp=struct.unpack_from('<i',code,i+3)[0]
        va=fn+i+7+disp
        s=strat(va)
        if s: out.append(s)
    return out
def table(mapname):
    vb=[v for v in vt[mapname] if (v-16) not in rel][0]
    sl=slots(vb)
    return {i:names_for(f) for i,f in enumerate(sl)}
if __name__=='__main__':
    pickle.dump({n:table(n) for n in ('14IClientUserMap','14IClientAppsMap','15IClientUtilsMap','20IClientAppManagerMap','17IClientFriendsMap','23IClientRemoteStorageMap','19IClientUserStatsMap','13IClientUGCMap')},open('maps.pkl','wb'))
    print('ok')
