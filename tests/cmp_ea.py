import subprocess, re, os, sys
CLY = '/home/claude/componly/cly'
W = '/tmp/cmpea'
os.makedirs(W, exist_ok=True)
E32 = ['eax','ecx','edx','ebx','esp','ebp','esi','edi']
E16 = ['bx+si','bx+di','bp+si','bp+di','si','di','bp','bx','bx+si+4','bx+di-1','bp+si+0x1234','si+0x80','di-0x80','bp+0','bp+di+127','bp-129']
eas = []
for r in E32:
    eas += ['[%s]'%r, '[%s+8]'%r, '[%s-8]'%r, '[%s+0x80]'%r, '[%s-0x81]'%r, '[%s+0x12345678]'%r, '[%s*2]'%r, '[%s*4+0x10]'%r, '[%s*8]'%r, '[%s*3]'%r, '[%s*5+4]'%r, '[%s*9]'%r]
    for b in E32:
        eas += ['[%s+%s]'%(b,r), '[%s+%s*2+4]'%(b,r), '[%s+%s*4-0x100]'%(b,r), '[%s+%s*8]'%(b,r)]
eas += ['[0x1234]','[lab]','[lab+4]','[lab+eax]','[eax+lab]','[ecx*4+lab]','[lab+eax*2+ebx]','[-1]','[es:eax]','[fs:0]','[ss:esp+4]','[cs:ebx+ecx]','[gs:lab]','[ds:ebp]','[byte eax+4]','[dword eax+4]','[byte eax]','[dword eax]','[nosplit eax*2]','[eax+4*ecx]','[4*ecx+eax]','[ecx+ecx]','[eax+eax]','[eax+eax+eax]','[eax*2+eax]','[ebp+ebp]','[ebp*1]','[esp+esp]','[eax+esp]','[esp+eax]','[ebp+eax]','[eax+ebp]','[eax-eax]','[lab-lab2+eax]','[lab2-lab]','[$]','[$+4]','[$$]','[0x7fffffff]','[0xffffffff]','[eax+0xffffffff]','[abs 0x10]','[rel lab]','[abs lab]','[a16 bx]','[a32 eax]','[word 0x1234]','[dword 0x1234]','[eax+(3+4)*2]','[eax+bl]']
eas16 = ['[%s]'%x for x in E16] + ['[0x1234]','[lab]','[lab+bx]','[bx+lab]','[bx+si+lab]','[es:bx]','[cs:bp+si]','[a32 eax]','[a16 bx]','[eax]','[eax+ebx*2]','[byte bx+4]','[word bx+4]','[bp+0]','[bp]','[si+si]','[bx+bx]','[bx+bx+bx]','[bx*2]','[si*1]']
ctx = ['mov eax, dword %s','lea ebx, %s','mov byte %s, 5','add dword %s, 0x1234','add word %s, 0x12','mov ax, %s','mov %s, ecx','fld qword %s','cmp byte %s, 0x7f','movzx eax, byte %s','mov eax, %s']
def run(bits, lines):
    idx = list(range(len(lines)))
    nres = {}
    while True:
        src = 'bits %d\n'%bits + '\n'.join(lines[i] for i in idx) + '\nlab: db 1\nlab2: db 2\n'
        open(W+'/n.asm','w').write(src)
        r = subprocess.run(['nasm','-f','bin','-l',W+'/n.lst',W+'/n.asm','-o',W+'/n.bin'],capture_output=True,text=True)
        errs = set(int(m.group(1))-2 for m in re.finditer(r'n\.asm:(\d+): error',r.stderr))
        if not errs: break
        idx = [idx[k] for k in range(len(idx)) if k not in errs]
    nb = open(W+'/n.bin','rb').read()
    for l in open(W+'/n.lst').read().splitlines():
        m = re.match(r'\s*(\d+)\s+([0-9A-F]{8})\s+([0-9A-F\[\]]+)(-?)\s+(.*)$', l)
        if m:
            ln = int(m.group(1))-2
            if 0 <= ln < len(idx):
                off = int(m.group(2),16)
                n = len(m.group(3).replace('[','').replace(']',''))//2
                nres[idx[ln]] = nres.get(idx[ln],'') + nb[off:off+n].hex().upper()
    idx = list(range(len(lines)))
    cres = {}
    failed = set()
    while True:
        src = 'bits %d\n'%bits + '\n'.join(lines[i] for i in idx) + '\nlab: db 1\nlab2: db 2\n'
        open(W+'/c.asm','w').write(src)
        r = subprocess.run([CLY,'linux',W+'/c.asm','-flat','-q','-out',W+'/c.bin','-l',W+'/c.lst'],capture_output=True,text=True)
        errs = set(int(m.group(1))-2 for m in re.finditer(r'c\.asm:(\d+): error',r.stderr))
        if r.returncode == 0 and not errs: break
        if not errs: print(r.stderr[:300]); break
        for k in errs:
            if 0 <= k < len(idx): failed.add(idx[k])
        idx = [idx[k] for k in range(len(idx)) if k not in errs]
    cb = open(W+'/c.bin','rb').read()
    for l in open(W+'/c.lst').read().splitlines():
        m = re.match(r'\s*(\d+)\s+([0-9A-F]{8})\s+([0-9A-F]+)(-?)\s+(.*)$', l)
        if m:
            ln = int(m.group(1))-2
            if 0 <= ln < len(idx):
                off = int(m.group(2),16)
                n = len(m.group(3))//2
                cres[idx[ln]] = cres.get(idx[ln],'') + cb[off:off+n].hex().upper()
    return nres, cres, failed
tot = 0; bad = 0; rej = 0
for bits, lst in ((32, eas), (16, eas16), (32, eas16), (16, eas)):
    lines = [c % e for e in lst for c in ctx]
    nres, cres, failed = run(bits, lines)
    seen = set()
    for i, l in enumerate(lines):
        if i not in nres: continue
        tot += 1
        e = lst[i // len(ctx)]
        if i in failed:
            rej += 1
            if (e, 'r') not in seen:
                seen.add((e, 'r')); print('bits', bits, 'CLY REJECTS', l, nres[i])
            continue
        if nres[i] != cres.get(i):
            bad += 1
            if (e, 'd') not in seen:
                seen.add((e, 'd')); print('bits', bits, 'DIFF', l, 'nasm', nres[i], 'cly', cres.get(i))
print('total', tot, 'diff', bad, 'rejects', rej)
