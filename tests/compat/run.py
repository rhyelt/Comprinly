import os
HERE=os.path.dirname(os.path.abspath(__file__))
CLY=os.path.join(HERE,"..","..","cly")
import tempfile
TMP=tempfile.mkdtemp()
os.chdir(TMP)
import subprocess,sys,os,glob
def run(name,src,bits="bits 32\n"):
    open("t.asm","w").write(src)
    n=subprocess.run(["nasm","-f","bin","t.asm","-o","n.bin"],capture_output=True,text=True)
    c=subprocess.run([CLY,"linux","t.asm","-flat","-nasm","-q","-out","c.bin"],capture_output=True,text=True)
    if n.returncode==0 and c.returncode==0:
        a=open("n.bin","rb").read();b=open("c.bin","rb").read()
        if a==b: return None
        return "DIFF nasm=%s cly=%s"%(a[:40].hex(),b[:40].hex())
    if n.returncode!=0 and c.returncode!=0: return None
    if n.returncode==0: return "CLY REJECTS: "+c.stderr.splitlines()[0] if c.stderr else "CLY REJECTS"
    return "CLY ACCEPTS (nasm: %s)"%n.stderr.splitlines()[0]
cases={}
exec(open(os.path.join(HERE,"cases.py")).read())
bad=0
for k,v in cases.items():
    r=run(k,v)
    if r: print(k,":",r); bad+=1
print("cases",len(cases),"bad",bad)
