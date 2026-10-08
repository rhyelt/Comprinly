import subprocess, sys, re, os
CLY = os.environ.get('CLY', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'cly'))
DAT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', 'insns.dat')
W = '/tmp/cmpdat'
os.makedirs(W, exist_ok=True)
BITS = os.environ.get('BITS', '64')
NV = int(os.environ.get('NV', '6'))
ONLY = os.environ.get('ONLY')
SKIPF = {'AMXTILE', 'AMXINT8', 'AMXBF16', 'SM2', 'IA64', 'NEVER', 'FUTURE', 'OBSOLETE'}
SZN = {1: 'byte', 2: 'word', 4: 'dword', 8: 'qword', 10: 'tword', 16: 'oword', 32: 'yword', 64: 'zword'}
if BITS == '64':
    MEMS = ['[rax]', '[rax+0x40]', '[rbx+rcx*4+0x7f]', '[rbp-8]', '[rsp+0x80]', '[r12]', '[r13+r14*8+0x1234]', '[rel lbl]', '[rsi-0x100]', '[r15+0x12345]', '[abs 0x1234]', '[eax+4]']
    G = {8: ['al', 'cl', 'bl', 'dh', 'sil', 'r9b', 'r15b', 'spl'], 16: ['ax', 'cx', 'si', 'bp', 'r10w', 'r8w'],
         32: ['eax', 'ecx', 'ebx', 'edi', 'ebp', 'r8d', 'r13d', 'esp'], 64: ['rax', 'rcx', 'rbx', 'rdi', 'rbp', 'r8', 'r13', 'rsp', 'r15']}
    XR = [1, 2, 3, 7, 9, 12, 15, 5]
    CR = ['cr0', 'cr2', 'cr3', 'cr8']
else:
    MEMS = ['[eax]', '[eax+0x40]', '[ebx+ecx*4+0x7f]', '[ebp-8]', '[esp+0x80]', '[0x1234]', '[esi-0x100]', '[edi+0x12345]']
    G = {8: ['al', 'cl', 'bl', 'dh'], 16: ['ax', 'cx', 'si', 'bp'], 32: ['eax', 'ecx', 'ebx', 'edi', 'ebp'], 64: []}
    XR = [1, 2, 3, 7, 5, 6, 4, 0]
    CR = ['cr0', 'cr2', 'cr3', 'cr4']


def mem(sz, i, forcesz=False):
    m = MEMS[i % len(MEMS)]
    return (SZN[sz] + ' ' + m) if sz and (i % 2 == 0 or forcesz) else m


def inst(tok, i, ctx):
    mods = tok.split('|')
    base = mods[0]
    mods = mods[1:]
    if 'to' in mods:
        return None
    k = i
    if base in ('reg8', 'reg16', 'reg32', 'reg64'):
        l = G[int(base[3:])]
        return l[k % len(l)] if l else None
    if base == 'reg32na':
        return G[32][k % len(G[32])]
    if base.startswith('rm') and base[2:].isdigit():
        n = int(base[2:])
        if n == 64 and not G[64]:
            return None
        if i % 2 == 0:
            l = G[n]
            return l[k % len(l)]
        return mem(n // 8, i, ctx.get('sized'))
    if base == 'mem':
        if 'far' in mods:
            return 'far ' + mem(0, i)
        return mem(0, i)
    m = re.fullmatch(r'mem(8|16|32|64|80|128|256|512)', base)
    if m:
        s = int(m.group(1)) // 8
        if 'far' in mods:
            return None
        return mem(s, i)
    if base == 'mem_offs':
        return '[qword 0x1122334455]' if BITS == '64' else '[0x1234]'
    if base == 'xmmreg':
        return 'xmm%d' % XR[k % 8]
    if base == 'mmxreg':
        return 'mm%d' % [1, 2, 3, 7][k % 4]
    if base == 'xmmrm':
        return 'xmm%d' % XR[k % 8] if i % 2 == 0 else mem(0, i)
    m = re.fullmatch(r'xmmrm(16|32|64|128)', base)
    if m:
        return 'xmm%d' % XR[k % 8] if i % 2 == 0 else mem(int(m.group(1)) // 8, i)
    if base == 'mmxrm' or base == 'mmxrm64':
        return 'mm%d' % [1, 2, 3, 7][k % 4] if i % 2 == 0 else mem(8, i)
    if base == 'xmm0':
        return 'xmm0'
    if base == 'fpureg':
        return 'st%d' % [1, 2, 3, 7][k % 4]
    if base == 'fpu0':
        return 'st0'
    if base == 'bndreg':
        return 'bnd%d' % [0, 1, 2, 3][k % 4]
    if base == 'imm8':
        return ['5', '0x7f', '200', '0'][k % 4]
    if base == 'imm16':
        return ['5', '0x1234', '0xffff'][k % 3]
    if base == 'imm32':
        return ['5', '0x12345678', '-1', '0x80000000'][k % 4]
    if base == 'imm64':
        if 'near' in mods:
            return 'lbl'
        return ['5', '0x123456789abcdef0', '-1'][k % 3]
    if base == 'imm':
        if 'near' in mods or 'short' in mods:
            return ('near ' if 'near' in mods else 'short ') + 'lbl'
        return ['5', '0x12', '-1', '0x12345678', '300'][k % 5]
    if base == 'sbyteword' or base == 'sbyteword16' or base == 'sbytedword' or base == 'sbytedword32' or base == 'sbytedword64':
        return ['5', '-1', '127', '-128'][k % 4]
    if base == 'sdword':
        return ['-5', '0x7fffffff', '-0x80000000'][k % 3]
    if base == 'udword':
        return ['5', '0xffffffff', '0x80000000'][k % 3]
    if base == 'unity':
        return '1'
    m = re.fullmatch(r'reg_(al|cl|dx|ax|eax|rax|ecx|rcx|edx|cx|es|cs|ss|ds|fs|gs)', base)
    if m:
        return m.group(1)
    if base == 'reg_sreg':
        return ['es', 'cs', 'ss', 'ds', 'fs', 'gs'][k % 6]
    if base == 'reg_creg':
        return CR[k % 4]
    if base == 'reg_dreg':
        return 'dr%d' % [0, 3, 6, 7][k % 4]
    if base == 'reg_treg':
        return 'tr%d' % [3, 4, 6, 7][k % 4]
    return None


def main():
    lines = []
    tags = []
    seen = set()
    for l in open(DAT):
        if l.startswith(';') or not l.strip():
            continue
        m = re.match(r'^(\S+)\s+(\S+)\s+\[([^\]]*)\]\s*(\S*)', l)
        if not m:
            continue
        mn, ops, enc, flags = m.groups()
        fs = set(flags.split(','))
        if re.search(r'vex\.|xop\.|evex\.', enc):
            continue
        if fs & SKIPF:
            continue
        if BITS == '64' and 'NOLONG' in fs:
            continue
        if BITS != '64' and ('LONG' in fs or 'X86_64' in fs) and 'NOLONG' not in fs:
            continue
        if 'resb' in enc or re.match(r'^(RES|D[BWDQTOYZ]$|INCBIN|EQU)', mn, re.I):
            continue
        if ONLY and not re.search(ONLY, mn, re.I):
            continue
        oplist = [] if ops == 'void' else ops.split(',')
        mns = [mn[:-2] + c for c in ('o', 'ae', 'z', 'ne', 'be', 'a', 's', 'np', 'l', 'ge', 'le', 'g')] if mn.endswith('cc') else [mn]
        for mn in mns:
          for i in range(NV):
            res = []
            ok = True
            for k, t in enumerate(oplist):
                o = inst(t, i + k, {})
                if o is None:
                    ok = False
                    break
                res.append(o)
            if not ok:
                break
            line = mn.lower() + (' ' + ', '.join(res) if res else '')
            if line not in seen:
                seen.add(line)
                lines.append(line)
                tags.append(enc.strip() + ' ' + flags)
    print('variants', len(lines))
    return lines, tags


def run(tool, ls):
    idx = list(range(len(ls)))
    failed = set()
    while True:
        src = 'bits %s\nlbl:\n' % BITS + '\n'.join(ls[i] for i in idx) + '\n'
        open(W + '/a.asm', 'w').write(src)
        if tool == 'n':
            r = subprocess.run(['nasm', '-f', 'bin', '-l', W + '/a.lst', W + '/a.asm', '-o', W + '/a.bin'], capture_output=True, text=True)
        else:
            r = subprocess.run([CLY, 'linux', W + '/a.asm', '-flat', '-q', '-out', W + '/a.bin', '-l', W + '/a.lst'], capture_output=True, text=True)
        pat = r'a\.asm:(\d+): (?:error|invalid operand)'
        errs = set(int(m.group(1)) - 3 for m in re.finditer(pat, r.stderr))
        if not errs:
            if r.returncode != 0 and not os.path.exists(W + '/a.lst'):
                print(r.stderr[:300])
            break
        for k in errs:
            if 0 <= k < len(idx):
                failed.add(idx[k])
        idx = [idx[k] for k in range(len(idx)) if k not in errs]
    res = {}
    for l in open(W + '/a.lst').read().splitlines():
        m = re.match(r'\s*(\d+)\s+([0-9A-F]{8})\s+([0-9A-F]+)(-?)\s+(.*)$', l)
        if m:
            ln = int(m.group(1)) - 3
            if 0 <= ln < len(idx):
                res.setdefault(idx[ln], '')
                res[idx[ln]] += m.group(3)
    return res, failed


if __name__ == '__main__':
    lines, tags = main()
    nres, nfail = run('n', lines)
    cres, cfail = run('c', lines)
    bad = rej = 0
    shown = 0
    lim = int(os.environ.get('SHOW', '60'))
    for i, l in enumerate(lines):
        if i not in nres:
            continue
        if i in cfail:
            rej += 1
            if shown < lim:
                print('CLY REJECTS:', l, '| nasm', nres[i], '|', tags[i])
                shown += 1
            continue
        if cres.get(i) != nres[i]:
            bad += 1
            if shown < lim:
                print('DIFF:', l, '| nasm', nres[i], '| cly', cres.get(i), '|', tags[i])
                shown += 1
    extra = sum(1 for i in cres if i not in nres)
    print('nasm accepted', len(nres), 'diffs', bad, 'cly rejects', rej, 'cly accepts nasm rejects', extra)
