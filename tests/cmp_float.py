import random, subprocess, os, struct
CLY='/home/claude/componly/cly'
W='/tmp/cmpfl'; os.makedirs(W,exist_ok=True)
random.seed(7)
vals=[]
for _ in range(400):
    k=random.choice(['n','big','small','tiny','int','half','tie'])
    if k=='n': v='%.*g'%(random.randint(1,17),random.uniform(-1000,1000))
    elif k=='big': v='%.*e'%(random.randint(0,17),random.uniform(1,9.99)*10**random.randint(30,308))
    elif k=='small': v='%.*e'%(random.randint(0,17),random.uniform(1,9.99)*10**-random.randint(30,308))
    elif k=='tiny': v='%.*e'%(random.randint(0,9),random.uniform(1,9.99)*10**-random.randint(38,48))
    elif k=='int': v='%d.0'%random.randint(0,2**40)
    elif k=='half': v='%.*e'%(random.randint(0,6),random.uniform(1,9.99)*10**random.randint(-8,5))
    else: v=repr(float(struct.unpack('<f',struct.pack('<I',random.randint(0,0x7f7fffff)))[0]))
    vals.append(v)
vals += ['0.0','1.0','0.5','3.4028235e38','3.4028236e38','3.4028234663852886e38','1.17549435e-38','1.401298464324817e-45','0.7e-45','0.69e-45','65504','65520','65519.99','6.1e-5','5.96e-8','3e-8','1e-5','16777217','16777216.5','9007199254740993','0.1','0.2','0.3','100','1e100','1e-100','123456789012345678901234567890']
lines=[]
for v in vals:
    for d in ('dd %s'%v,'dq %s'%v,'dt %s'%v,'dw __float16__(%s)'%v,'dw __bfloat16__(%s)'%v,'do %s'%v,'dh %s'%v):
        lines.append(d)
bad=0
chunk=300
for i in range(0,len(lines),chunk):
    part=lines[i:i+chunk]
    idx=list(range(len(part)))
    while True:
        open(W+'/a.asm','w').write('\n'.join(part[k] for k in idx)+'\n')
        r=subprocess.run(['nasm','-f','bin','-l',W+'/n.lst',W+'/a.asm','-o',W+'/n.bin'],capture_output=True,text=True)
        import re
        errs=set(int(m.group(1))-1 for m in re.finditer(r'a\.asm:(\d+): error',r.stderr))
        if not errs: break
        idx=[idx[k] for k in range(len(idx)) if k not in errs]
    for k in idx:
        open(W+'/b.asm','w').write(part[k]+'\n')
        subprocess.run(['nasm','-f','bin',W+'/b.asm','-o',W+'/n1.bin'],capture_output=True)
        r=subprocess.run([CLY,'linux',W+'/b.asm','-flat','-q','-out',W+'/c1.bin'],capture_output=True,text=True)
        a=open(W+'/n1.bin','rb').read(); b=open(W+'/c1.bin','rb').read() if r.returncode==0 else b'ERR'
        if a!=b:
            bad+=1
            if bad<25: print('DIFF',part[k],a.hex(),b.hex())
print('lines',len(lines),'bad',bad)
