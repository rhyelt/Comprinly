import random, subprocess, struct, sys, os
from unicorn import *
from unicorn.x86_const import *
CLY=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'cly')
W='/tmp/cly_ofz32'
R64=['eax','ebx','ecx','edx','esi','edi']
R32={x:x for x in R64}
R16={'eax':'ax','ebx':'bx','ecx':'cx','edx':'dx','esi':'si','edi':'di'}
R8={'eax':'al','ebx':'bl','ecx':'cl','edx':'dl','esi':'dl','edi':'bl'}
CC=['e','ne','z','nz','b','ae','c','nc','be','a','s','ns','o','no','p','np','l','ge','le','g']
def reg(): return random.choice(R64)
def imm():
    return random.choice(['0','0','0','1','-1','1','2','5','255','-128','0x7fffffff','100','0x12345'])
def gen_body(nlab, funcs):
    out=[]
    labs=['L%d'%i for i in range(nlab)]
    placed=0
    n=random.randint(8,60)
    for _ in range(n):
        k=random.random()
        r=reg(); r2=reg()
        if k<0.12: out.append('mov %s, %s'%(random.choice([r,R32[r]]), imm()))
        elif k<0.2: out.append('mov %s, 0'%random.choice([r,R32[r],R16[r]]))
        elif k<0.3: out.append('%s %s, %s'%(random.choice(['add','sub']), random.choice([r,R32[r]]), random.choice(['1','-1','1','2','0'])))
        elif k<0.36: out.append('cmp %s, %s'%(random.choice([r,R32[r],R16[r],R8[r]]), random.choice(['0','0','1','-1'])))
        elif k<0.41: out.append('%s %s, %s'%(random.choice(['and','or','xor','test','cmp','adc','sbb']), r, r2))
        elif k<0.45: out.append('%s %s'%(random.choice(['inc','dec','neg','not']), random.choice([r,R32[r]])))
        elif k<0.5: out.append('set%s %s'%(random.choice(CC), R8[r]))
        elif k<0.54: out.append('cmov%s %s, %s'%(random.choice(CC), r, r2))
        elif k<0.6:
            out.append('pushfd')
            out.append('pop edx')
            out.append('and edx, -17')
            out.append('mov [ebp+%d], edx'%(8*random.randint(8,12)))
        elif k<0.66 and placed<len(labs):
            out.append('j%s %s'%(random.choice(CC), random.choice(labs[placed:])))
        elif k<0.7 and placed<len(labs):
            out.append('jmp %s'%random.choice(labs[placed:]))
            if random.random()<0.5: out.append('mov eax, 77')
        elif k<0.76 and placed<len(labs):
            out.append(labs[placed]+':'); placed+=1
            if random.random()<0.2 and placed<len(labs):
                out.append('jmp %s'%random.choice(labs[placed:]))
        elif k<0.82:
            off=8*random.randint(0,3)
            out.append('mov [ebp+%d], %s'%(off,r))
            if random.random()<0.6: out.append('mov %s, [ebp+%d]'%(r,off))
        elif k<0.86:
            out.append('mov %s, %s'%(r,r2))
            if random.random()<0.5: out.append('mov %s, %s'%(r2,r))
        elif k<0.9 and funcs:
            out.append('call %s'%random.choice(funcs))
            if random.random()<0.3: out.append('ret_placeholder')
        elif k<0.94:
            out.append('shl %s, %d'%(r, random.randint(0,3)))
        elif k<0.97:
            out.append('lea %s, [%s+%d]'%(r2,r,random.randint(-9,9)))
        else:
            out.append('push %s'%r); out.append('pop %s'%r2)
    while placed<len(labs):
        out.append(labs[placed]+':'); placed+=1
    return out
def gen():
    nf=random.randint(0,4)
    funcs=['f%d'%i for i in range(nf)]
    body=gen_body(random.randint(0,5), funcs)
    body=[b for b in body if b!='ret_placeholder']
    src=['start:']
    for r in R64+['r11','r13']:
        pass
    src+=body
    src+=['mov ebp, eax','mov eax, 1','xor ebx, ebx','int 0x80']
    for f in funcs:
        src.append(f+':')
        k=random.random()
        if k<0.3: src+=['inc eax','add ecx, 2','ret']
        elif k<0.5: src+=['mov edx, 5','mov ebx, 0','ret']
        elif k<0.7: src+=['cmp edx, 0','jne .z','mov esi, 1','.z:','ret']
        elif k<0.85: src+=['call f%d'%random.randint(0,nf-1) if False else 'add esi, edi','ret']
        else: src+=['mov eax, 3','jmp %s_t'%f,'%s_t:'%f,'ret']
    return '\n'.join(src)+'\n'
def load_elf(path):
    d=open(path,'rb').read()
    entry=struct.unpack_from('<I',d,24)[0]
    phoff=struct.unpack_from('<I',d,28)[0]
    phentsize,phnum=struct.unpack_from('<HH',d,42)
    segs=[]
    for i in range(phnum):
        t,off,va,pa,fs,ms,fl,al=struct.unpack_from('<8I',d,phoff+i*phentsize)
        if t==1: segs.append((va,d[off:off+fs],ms,fl))
    return entry,segs
def run(path):
    entry,segs=load_elf(path)
    mu=Uc(UC_ARCH_X86,UC_MODE_32)
    for va,data,ms,fl in segs:
        base=va&~0xfff; end=(va+ms+0xfff)&~0xfff
        try: mu.mem_map(base,end-base)
        except UcError: pass
        mu.mem_write(va,data)
    mu.mem_map(0x7f0000,0x10000)
    mu.mem_map(0x7e0000,0x10000)
    mu.reg_write(UC_X86_REG_EBP,0x7e0000)
    mu.reg_write(UC_X86_REG_ESP,0x7f8000)
    st={}
    def intr(uc,n,ud):
        regs=[UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESP]
        st['regs']=[uc.reg_read(r) for r in regs]
        st['mem']=bytes(uc.mem_read(0x7e0000,0x100))
        uc.emu_stop()
    mu.hook_add(UC_HOOK_INTR,intr)
    try: mu.emu_start(entry,0,count=20000)
    except UcError as e: st['err']=str(e)
    return st
os.makedirs(W, exist_ok=True)
N=int(sys.argv[2]) if len(sys.argv)>2 else 300
random.seed(int(sys.argv[1]) if len(sys.argv)>1 else 1)
bad=0; shrunk=0; tot0=tot1=0
for it in range(N):
    src=gen()
    open(W+'/p.cly','w').write(src)
    r0=subprocess.run([CLY,'linux',W+'/p.cly','-q','-m32','-O0','-out',W+'/p0'],capture_output=True,text=True)
    r1=subprocess.run([CLY,'linux',W+'/p.cly','-q','-m32','-out',W+'/p1'],capture_output=True,text=True)
    if r0.returncode!=r1.returncode or (r0.returncode and r0.stderr.splitlines()[:1]!=r1.stderr.splitlines()[:1]):
        print('BUILD DIFF',it,r0.stderr[:200],'|',r1.stderr[:200]); open(W+'/bad_%d.cly'%it,'w').write(src); bad+=1; continue
    if r0.returncode: continue
    a=run(W+'/p0'); b=run(W+'/p1')
    tot0+=os.path.getsize(W+'/p0'); tot1+=os.path.getsize(W+'/p1')
    if a!=b:
        print('MISMATCH',it,{k:(a.get(k)==b.get(k)) for k in a}); open(W+'/bad_%d.cly'%it,'w').write(src); bad+=1
print('done bad=',bad,'size',tot0,'->',tot1)
