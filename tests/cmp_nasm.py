import subprocess, sys, re, itertools, os, random
CLY = os.environ.get('CLY', '/home/claude/componly/cly')
BITS = os.environ.get('BITS', '32')
W = '/tmp/cmpwork' + BITS
os.makedirs(W, exist_ok=True)
dump = subprocess.run([CLY, '--dump-insns'], capture_output=True, text=True).stdout.splitlines()
ents = []
for l in dump:
    p = [x.strip() for x in l.split(' | ')]
    if len(p) < 3 or 'xv:' in p[1]: continue
    h = p[0].split(' ')
    mn = h[0]
    specs = h[1].split(',') if len(h) > 1 and h[1] else []
    ents.append((mn, specs, p[1], int(p[2])))
R8 = ['al', 'cl', 'bl', 'dh']
R16 = ['ax', 'cx', 'si', 'bp']
R32 = ['eax', 'ecx', 'ebx', 'edi', 'ebp']
MEMS = ['[ebx]', '[eax+ecx*4+0x10]', '[0x1234]', '[ebp-8]', '[esp+4]', '[esi+edi]', '[ebx+0x12345]']
SZ = {1: 'byte', 2: 'word', 4: 'dword', 8: 'qword', 10: 'tword', 16: 'oword', 32: 'yword'}
def mem(sz, i):
    m = MEMS[i % len(MEMS)]
    return (SZ[sz] + ' ' + m) if sz else m
def inst(spec, i):
    if spec in ('r8', 'rr8'): return R8[i % 4]
    if spec in ('r16', 'rr16'): return R16[i % 4]
    if spec in ('r32', 'rr32'): return R32[i % 5]
    if spec == 'rm8': return R8[i % 4] if i % 2 == 0 else mem(1, i)
    if spec == 'rm16': return R16[i % 4] if i % 2 == 0 else mem(2, i)
    if spec == 'rm32': return R32[i % 5] if i % 2 == 0 else mem(4, i)
    if spec == 'r32m16': return R32[i % 5] if i % 2 == 0 else mem(2, i)
    if spec == 'r32m8': return R32[i % 5] if i % 2 == 0 else mem(1, i)
    if spec == 'm': return mem(0, i)
    if spec == 'm8': return mem(1, i)
    if spec == 'm16': return mem(2, i)
    if spec == 'm32': return mem(4, i)
    if spec == 'm64': return mem(8, i)
    if spec == 'm80': return mem(10, i)
    if spec == 'm128': return mem(16, i)
    if spec == 'm256': return mem(32, i)
    if spec == 'mfar': return 'far ' + mem(0, i)
    if spec == 'mo8': return 'byte [0x1234]'
    if spec == 'mo16': return 'word [0x1234]'
    if spec == 'mo32': return 'dword [0x1234]'
    if spec == 'imm8': return ['5', '0x7f', '200'][i % 3]
    if spec == 'imm16': return ['0x1234', '5'][i % 2]
    if spec == 'imm32': return ['0x12345678', '5'][i % 2]
    if spec == 'sb16': return ['5', '-1', '0x1234'][i % 3]
    if spec == 'sb32': return ['5', '-1', '0x12345678'][i % 3]
    if spec == 'one': return '1'
    if spec in ('al', 'cl', 'dx', 'ax', 'eax', 'es', 'cs', 'ss', 'ds', 'fs', 'gs'): return spec
    if spec == 'sreg': return ['ds', 'es', 'fs', 'gs', 'ss'][i % 5]
    if spec == 'cr': return ['cr0', 'cr2', 'cr3', 'cr4'][i % 4]
    if spec == 'dr': return ['dr0', 'dr3', 'dr7'][i % 3]
    if spec == 'tr': return ['tr3', 'tr6'][i % 2]
    if spec in ('mm', 'mmr'): return 'mm%d' % (i % 8)
    if spec == 'mm32': return 'mm%d' % (i % 8) if i % 2 == 0 else mem(4, i)
    if spec == 'mm64': return 'mm%d' % (i % 8) if i % 2 == 0 else mem(8, i)
    if spec in ('xmm', 'xmmr'): return 'xmm%d' % ((i * 3 + 1) % 8)
    if spec == 'xm16': return 'xmm%d' % (i % 8) if i % 2 == 0 else mem(2, i)
    if spec == 'xm32': return 'xmm%d' % (i % 8) if i % 2 == 0 else mem(4, i)
    if spec == 'xm64': return 'xmm%d' % (i % 8) if i % 2 == 0 else mem(8, i)
    if spec == 'xm128': return 'xmm%d' % (i % 8) if i % 2 == 0 else mem(16, i)
    if spec in ('xmmv', 'xmmi'): return 'xmm%d' % ((i * 5 + 2) % 8)
    if spec in ('ymm', 'ymmr'): return 'ymm%d' % ((i * 3 + 1) % 8)
    if spec in ('ymmv', 'ymmi'): return 'ymm%d' % ((i * 5 + 2) % 8)
    if spec == 'ym256': return 'ymm%d' % (i % 8) if i % 2 == 0 else mem(32, i)
    if spec == 'r32v': return R32[(i + 2) % 5]
    if spec == 'xm8': return 'xmm%d' % (i % 8) if i % 2 == 0 else mem(1, i)
    if spec == 'vm32x': return ['[eax+xmm1*4]', '[ebx+xmm2*2+0x10]', '[xmm3]'][i % 3]
    if spec == 'vm32y': return ['[eax+ymm1*4]', '[ebx+ymm2*8+0x10]', '[ymm3*2]'][i % 3]
    if spec == 'xmm0': return 'xmm0'
    if spec == 'st0': return 'st0'
    if spec == 'sti': return 'st%d' % (1 + i % 7)
    if spec in ('rel8', 'rel'): return '$+0x20'
    if spec == 'far': return '0x10:0x1234'
    return None
lines = []
tags = []
for mn, specs, enc, lvl in ents:
    nvar = 3
    for i in range(nvar):
        ops = [inst(s, i + k) for k, s in enumerate(specs)]
        if any(o is None for o in ops): break
        line = mn + (' ' + ', '.join(ops) if ops else '')
        if line not in lines:
            lines.append(line)
            tags.append((mn, specs, enc))
print('variants', len(lines))
def run_nasm(ls):
    ls = list(ls)
    idx = list(range(len(ls)))
    while True:
        src = 'bits %s\n' % BITS + '\n'.join(ls[i] for i in idx) + '\n'
        open(W + '/n.asm', 'w').write(src)
        r = subprocess.run(['nasm', '-f', 'bin', '-l', W + '/n.lst', W + '/n.asm', '-o', W + '/n.bin'], capture_output=True, text=True)
        errs = set(int(m.group(1)) - 2 for m in re.finditer(r'n\.asm:(\d+): error', r.stderr))
        if not errs:
            break
        idx = [idx[k] for k in range(len(idx)) if k not in errs]
    res = {}
    lst = open(W + '/n.lst').read().splitlines()
    k = 0
    for l in lst:
        m = re.match(r'\s*(\d+)\s+([0-9A-F]{8})\s+([0-9A-F]+)(-?)\s+(.*)$', l)
        if m:
            ln = int(m.group(1)) - 2
            if ln >= 0 and ln < len(idx):
                res.setdefault(idx[ln], '')
                res[idx[ln]] += m.group(3)
    return res
def run_cly(ls):
    ls = list(ls)
    idx = list(range(len(ls)))
    failed = set()
    while True:
        src = 'bits %s\n' % BITS + '\n'.join(ls[i] for i in idx) + '\n'
        open(W + '/c.asm', 'w').write(src)
        r = subprocess.run([CLY, 'linux', W + '/c.asm', '-flat', '-q', '-out', W + '/c.bin', '-l', W + '/c.lst'], capture_output=True, text=True)
        errs = set(int(m.group(1)) - 2 for m in re.finditer(r'c\.asm:(\d+): error', r.stderr))
        if r.returncode == 0 and not errs: break
        if not errs:
            print(r.stderr[:500]); break
        for k in errs:
            if 0 <= k < len(idx): failed.add(idx[k])
        idx = [idx[k] for k in range(len(idx)) if k not in errs]
    res = {}
    for l in open(W + '/c.lst').read().splitlines():
        m = re.match(r'\s*(\d+)\s+([0-9A-F]{8})\s+([0-9A-F]+)(-?)\s+(.*)$', l)
        if m:
            ln = int(m.group(1)) - 2
            if 0 <= ln < len(idx):
                res.setdefault(idx[ln], '')
                res[idx[ln]] += m.group(3)
    return res, failed
nres = run_nasm(lines)
cres, cfail = run_cly(lines)
bad = 0
miss = 0
for i, l in enumerate(lines):
    if i not in nres: continue
    if i in cfail:
        miss += 1
        print('CLY REJECTS:', l, '| nasm', nres[i], '|', tags[i][2])
        continue
    if cres.get(i) != nres[i]:
        bad += 1
        print('DIFF:', l, '| nasm', nres[i], '| cly', cres.get(i), '|', tags[i][2])
print('nasm accepted', len(nres), 'diffs', bad, 'cly rejects', miss)
