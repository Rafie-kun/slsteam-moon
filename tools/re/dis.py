"""dis.py VA [N] : désassemble N instructions (défaut 60) de la fonction x86_64 à VA (objdump)."""
import sys,subprocess; sys.path.insert(0,'.')
import rev
e=rev.Elf(); f=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 60
o=e.v2o(f); open('/tmp/f.bin','wb').write(e.d[o:e.func_end(f)])
out=subprocess.run(['objdump','-D','-b','binary','-mi386:x86-64','-Mintel','--adjust-vma=%d'%f,'/tmp/f.bin'],capture_output=True,text=True).stdout
lines=[l for l in out.splitlines() if '\t' in l and not l.strip().startswith('...')]
for l in lines[:n]: print(l[:118])
