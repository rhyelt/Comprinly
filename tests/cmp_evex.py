import subprocess, sys, re, os
CLY = os.environ.get('CLY', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'cly'))
DAT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', 'insns.dat')
W = '/tmp/cmpevex'
os.makedirs(W, exist_ok=True)
NV = int(os.environ.get('NV', '6'))
ONLY = os.environ.get('ONLY')
SKIP = {'LONG', 'AMXTILE', 'AMXINT8', 'AMXBF16', 'SM2', 'X64'}
SZN = {1: 'byte', 2: 'word', 4: 'dword', 8: 'qword', 10: 'tword', 16: 'oword', 32: 'yword', 64: 'zword'}
MEMS = ['[eax]', '[eax+0x40]', '[ebx+ecx*4+0x7f]', '[ebp-8]', '[esp+0x80]', '[0x1234]', '[esi-0x100]', '[edi+0x12345]']
REGS = {'xmm': [1, 2, 3, 7, 17, 20, 31, 5], 'ymm': [1, 2, 3, 7, 17, 20, 31, 5], 'zmm': [1, 2, 3, 7, 17, 20, 31, 5]}
GP = {8: ['al', 'cl', 'bl', 'dh'], 16: ['ax', 'cx', 'si', 'bp'], 32: ['eax', 'ecx', 'ebx', 'edi', 'ebp']}


def mem(sz, i, bc=None):
    m = MEMS[i % len(MEMS)]
    if bc is not None:
        return m
    return (SZN[sz] + ' ' + m) if sz and i % 2 == 0 else m


def inst(tok, i, role, vl, nreg):
    star = tok.endswith('*')
    if star:
        tok = tok[:-1]
    parts = tok.split('|')
    base = parts[0]
    decs = parts[1:]
    out = None
    suffix = ''
    if role == 'v':
        rn = [1, 2, 3, 5, 6, 4][i % 6]
    else:
        rn = REGS['xmm'][i % 8]
    m = re.fullmatch(r'(xmm|ymm|zmm)reg', base)
    if m:
        out = '%s%d' % (m.group(1), rn)
    m = re.fullmatch(r'(xmm|ymm|zmm)rm(\d+)', base)
    if m:
        usemem = (i % 3 == 1)
        bsz = [d for d in decs if d.startswith('b')]
        if bsz and i % 3 == 2:
            esz = int(bsz[0][1:]) // 8
            cnt = int(m.group(2)) // 8 // esz
            out = mem(0, i, 1) + '{1to%d}' % cnt
        elif usemem:
            out = mem(int(m.group(2)) // 8, i)
        else:
            out = '%s%d' % (m.group(1), rn)
    m = re.fullmatch(r'([xyz])mem(32|64)', base)
    if m:
        c = {'x': 'xmm', 'y': 'ymm', 'z': 'zmm'}[m.group(1)]
        out = '[eax+%s%d*%d]' % (c, [2, 3, 4][i % 3], [1, 2, 4, 8][i % 4])
    if base == 'kreg':
        out = 'k%d' % [1, 2, 3, 7][i % 4]
    m = re.fullmatch(r'krm(8|16|32|64)', base)
    if m:
        out = 'k%d' % [1, 2, 3][i % 3] if i % 2 == 0 else mem(int(m.group(1)) // 8, i)
    m = re.fullmatch(r'mem(\d+)?', base)
    if m:
        out = mem(int(m.group(1)) // 8 if m.group(1) else 0, i)
    if base in ('reg8', 'reg16', 'reg32'):
        out = GP[int(base[3:])][i % 4]
    if base == 'rm64':
        out = mem(8, i)
    if base in ('rm8', 'rm16', 'rm32'):
        n = int(base[2:])
        out = GP[n][i % 4] if i % 2 == 0 else mem(n // 8, i)
    if base == 'imm32':
        out = ['5', '0x12345678', '-1'][i % 3]
    if base == 'imm8':
        out = ['5', '0x7f', '200', '0'][i % 4]
    if out is None:
        return None
    if 'mask' in decs and i % 2 == 0:
        suffix += '{k%d}' % [1, 2, 7][i % 3]
        if 'z' in decs and i % 4 == 0:
            suffix += '{z}'
    if '{1to' not in out:
        if 'er' in decs and i % 3 == 0:
            suffix += ', {%s}' % ['rn-sae', 'rd-sae', 'ru-sae', 'rz-sae'][i % 4]
        if 'sae' in decs and i % 3 == 0:
            suffix += ', {sae}'
    return out + suffix


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
        if 'vex.' not in enc and 'xop.' not in enc:
            continue
        if set(flags.split(',')) & SKIP:
            continue
        if ONLY and not re.search(ONLY, mn, re.I):
            continue
        om = re.match(r'^([a-z]*):', enc.strip())
        order = om.group(1) if om else ''
        oplist = [] if ops == 'void' else ops.split(',')
        for i in range(NV):
            res = []
            ok = True
            for k, t in enumerate(oplist):
                role = order[k] if k < len(order) else 'r'
                o = inst(t, i + k, role, 0, len(oplist))
                if o is None:
                    ok = False
                    break
                res.append(o)
            if not ok:
                break
            if oplist and oplist[1:2] and oplist[1].endswith('*') and i % 3 == 0 and re.match(r'^[xyz]', res[0]) and '[' not in res[0]:
                del res[1]
            line = mn.lower() + (' ' + ', '.join(res).replace('}, {', '}, {') if res else '')
            if line not in seen:
                seen.add(line)
                lines.append(line)
                tags.append(enc.strip())
    print('variants', len(lines))
    return lines, tags


def run(tool, ls):
    idx = list(range(len(ls)))
    failed = set()
    bits = os.environ.get('BITS', '32')
    while True:
        src = 'bits %s\n' % bits + '\n'.join(ls[i] for i in idx) + '\n'
        open(W + '/a.asm', 'w').write(src)
        if tool == 'n':
            r = subprocess.run(['nasm', '-f', 'bin', '-l', W + '/a.lst', W + '/a.asm', '-o', W + '/a.bin'], capture_output=True, text=True)
            pat = r'a\.asm:(\d+): (?:error|invalid operand)'
        else:
            r = subprocess.run([CLY, 'linux', W + '/a.asm', '-flat', '-q', '-out', W + '/a.bin', '-l', W + '/a.lst'], capture_output=True, text=True)
            pat = r'a\.asm:(\d+): (?:error|invalid operand)'
        errs = set(int(m.group(1)) - 2 for m in re.finditer(pat, r.stderr))
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
            ln = int(m.group(1)) - 2
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
    for i, l in enumerate(lines):
        if i not in nres:
            continue
        if i in cfail:
            rej += 1
            if shown < int(os.environ.get('SHOW', '60')):
                print('CLY REJECTS:', l, '| nasm', nres[i], '|', tags[i])
                shown += 1
            continue
        if cres.get(i) != nres[i]:
            bad += 1
            if shown < int(os.environ.get('SHOW', '60')):
                print('DIFF:', l, '| nasm', nres[i], '| cly', cres.get(i), '|', tags[i])
                shown += 1
    extra = sum(1 for i in cres if i not in nres)
    print('nasm accepted', len(nres), 'diffs', bad, 'cly rejects', rej, 'cly accepts nasm rejects', extra)
