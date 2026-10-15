import os
HERE=os.path.dirname(os.path.abspath(__file__))
CLY=os.path.join(HERE,"..","..","cly")
import tempfile
TMP=tempfile.mkdtemp()
os.chdir(TMP)
import subprocess,re,sys
def dump(f):
    out=[]
    s=subprocess.run(["readelf","-SsrW",f],capture_output=True,text=True).stdout
    secs=[]
    for l in s.splitlines():
        l=l.strip()
        m=re.match(r"\[\s*\d+\]\s+(\S+)\s+(\S+)\s+\S+\s+\S+\s+\S+\s+\S+\s+(\S*)\s+(\d+)\s+(\d+)\s+(\d+)",l)
        if m and m.group(1) not in ("",): secs.append((m.group(1),m.group(2),m.group(3)))
    syms=[]
    for l in s.splitlines():
        p=l.split()
        if len(p)>=8 and re.match(r"\d+:",p[0]): syms.append((p[7] if len(p)>7 else "",p[3],p[4],p[1]!="0000000000000000" and p[6]!="UND"))
    rel=[]
    for l in s.splitlines():
        p=l.split()
        if len(p)>=5 and re.match(r"[0-9a-f]{16}$",p[0]) and p[2].startswith("R_"): rel.append((p[0],p[2],p[4] if len(p)>4 else "",p[-1]))
    data=[]
    for sec,_,_ in secs:
        if sec in (".text",".data",".rodata"):
            r=subprocess.run(["objcopy","-O","binary","-j",sec,f,"/dev/stdout"],capture_output=True)
            data.append((sec,r.stdout.hex()))
    return sorted([x[0] for x in secs if not x[0].startswith(".note")]),sorted(syms),sorted(rel),data
cases={}
exec(open(os.path.join(HERE,"objcases.py")).read())
bad=0
for k,v in cases.items():
    open("o.asm","w").write(v)
    n=subprocess.run(["nasm","-f","elf64","o.asm","-o","n.o"],capture_output=True,text=True)
    c=subprocess.run([CLY,"linux","o.asm","-obj","-nasm","-q","-out","c.o"],capture_output=True,text=True)
    if n.returncode!=0 and c.returncode!=0: continue
    if n.returncode!=c.returncode:
        print(k,"STATUS nasm",n.returncode,n.stderr.splitlines()[:1],"cly",c.returncode,c.stderr.splitlines()[:1]); bad+=1; continue
    a=dump("n.o");b=dump("c.o")
    for i,nm in enumerate(["sections","symbols","relocs","data"]):
        if a[i]!=b[i]:
            print(k,nm,"\n  nasm",a[i],"\n  cly ",b[i]); bad+=1; break
print("cases",len(cases),"bad",bad)
