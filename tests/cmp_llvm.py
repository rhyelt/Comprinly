import subprocess, sys, os, re, tempfile

CLY = os.environ.get('CLY', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'cly'))

CFG = {
    'a64': dict(triple='aarch64', mattr='+v8.7a,+lse,+crc,+fullfp16,+fp16fml,+rcpc,+rdm,+dotprod,+aes,+sha2,+sha3,+sm4,+bf16,+i8mm,+jsconv,+complxnum,+mte,+pauth,+lse2,+ls64,+sb,+ssbs,+predres,+specrestrict,+tlb-rmi,+flagm,+altnzcv,+ccpp,+fptoint,+rcpc-immo,+fp-armv8,+neon,+perfmon,+spe,+bti,+sve,+sve2,+cssc,+tme,+wfxt,+rcpc3,+xs,+mops,+hbc,+the,+rand', cpu='arm64', comment='//'),
    'rv64': dict(triple='riscv64', mattr='+m,+a,+f,+d,+zicsr,+zifencei,+zba,+zbb,+zbs,+zbc,+zicond,+c,+zfh,+zbkb,+zbkx,+zknd,+zkne,+zknh,+zksed,+zksh,+zicbom,+zicboz,+zicbop,+zawrs,+svinval,+h', cpu='riscv64', comment='#'),
    'rv64nc': dict(triple='riscv64', mattr='+m,+a,+f,+d,+zicsr,+zifencei,+zba,+zbb,+zbs,+zbc,+zicond', cpu='riscv64', comment='#'),
    'rv32': dict(triple='riscv32', mattr='+m,+a,+f,+d,+zicsr,+zifencei,+zba,+zbb,+zbs,+zbc,+zicond,+c,+zfh,+zbkb,+zbkx,+zknd,+zkne,+zknh,+zksed,+zksh,+zicbom,+zicboz,+zicbop,+zawrs,+svinval,+h', cpu='riscv32', comment='#'),
    'rv32nc': dict(triple='riscv32', mattr='+m,+a,+f,+d,+zicsr,+zifencei,+zba,+zbb,+zbs,+zbc,+zicond', cpu='riscv32', comment='#'),
}

def llvm_batch(arch, lines, labels=True):
    c = CFG[arch]
    src = []
    if labels:
        src.append('L1:\n nop')
    src.extend(lines)
    if labels:
        src.append('L2:\n nop')
    text = '\n'.join(src) + '\n'
    p = subprocess.run(['llvm-mc', '-triple=' + c['triple'], '-mattr=' + c['mattr'], '-show-encoding'], input=text, capture_output=True, text=True)
    return p.stdout, p.stderr, (1 if labels else 0)

def llvm_encode(arch, lines):
    keep = list(range(len(lines)))
    rejected = {}
    while True:
        cur = [lines[i] for i in keep]
        out, err, off = llvm_batch(arch, cur)
        errlines = set()
        for m in re.finditer(r'<stdin>:(\d+):\d+: error: (.*)', err):
            errlines.add(int(m.group(1)))
        if not errlines:
            break
        newkeep = []
        for k, i in enumerate(keep):
            ln = k + 3
            if ln in errlines:
                rejected[i] = 'llvm'
            else:
                newkeep.append(i)
        if len(newkeep) == len(keep):
            raise SystemExit('llvm error mapping failed: ' + err[:500])
        keep = newkeep
    c = CFG[arch]
    sizes = []
    for l in out.split('\n'):
        m = re.search(r'encoding: \[(.*?)\]', l)
        if m:
            sizes.append(len(m.group(1).split(',')))
    cur = [lines[i] for i in keep]
    src = ['L1:\n nop']
    for k, l in enumerate(cur):
        src.append('Q%d:' % k)
        src.append(l)
    src.append('L2:\n nop')
    d = tempfile.mkdtemp()
    sp = os.path.join(d, 'x.s')
    op = os.path.join(d, 'x.o')
    bp = os.path.join(d, 'x.bin')
    open(sp, 'w').write('\n'.join(src) + '\n')
    p = subprocess.run(['llvm-mc', '-triple=' + c['triple'], '-mattr=' + c['mattr'], '-filetype=obj', sp, '-o', op], capture_output=True, text=True)
    if p.returncode:
        raise SystemExit('llvm obj failed: ' + p.stderr[:500])
    subprocess.run(['llvm-objcopy', '-O', 'binary', '--only-section=.text', op, bp], check=True)
    data = open(bp, 'rb').read()
    nm = subprocess.run(['llvm-nm', op], capture_output=True, text=True).stdout
    offs = {}
    for l in nm.split('\n'):
        m = re.match(r'([0-9a-f]+) \w (\S+)', l)
        if m: offs[m.group(2)] = int(m.group(1), 16)
    first = offs.get('L1', 0)
    pts = [offs['Q%d' % k] for k in range(len(cur))] + [offs['L2']]
    enc = [data[first:pts[0]]] if cur else []
    enc = [data[0:pts[0]]]
    for k in range(len(cur)):
        enc.append(data[pts[k]:pts[k + 1]])
    enc.append(data[offs['L2']:])
    return keep, enc, rejected

def cly_encode(arch, lines, opts=()):
    cpu = {'a64': 'arm64', 'rv64': 'riscv64', 'rv64nc': 'rv64imafd_zicsr_zifencei_zba_zbb_zbs_zbc_zicond', 'rv32': 'riscv32', 'rv32nc': 'rv32imafd_zicsr_zifencei_zba_zbb_zbs_zbc_zicond'}[arch]
    keep = list(range(len(lines)))
    rejected = {}
    d = tempfile.mkdtemp()
    while True:
        cur = [lines[i] for i in keep]
        src = ['cpu ' + cpu, 'L1:', ' nop'] + cur + ['L2:', ' nop']
        path = os.path.join(d, 't.cly')
        open(path, 'w').write('\n'.join(src) + '\n')
        outp = os.path.join(d, 't.bin')
        lst = os.path.join(d, 't.lst')
        p = subprocess.run([CLY, 'linux', path, '-flat', '-q', '-w', '-l', lst, '-out', outp] + list(opts), capture_output=True, text=True)
        if p.returncode == 0:
            break
        errlines = set()
        for m in re.finditer(r't\.cly:(\d+): error', p.stderr):
            errlines.add(int(m.group(1)))
        if not errlines:
            raise SystemExit('cly failed: ' + p.stderr[:800])
        newkeep = []
        for k, i in enumerate(keep):
            ln = k + 4
            if ln in errlines:
                rejected[i] = p.stderr
            else:
                newkeep.append(i)
        if len(newkeep) == len(keep):
            raise SystemExit('cly error mapping failed: ' + p.stderr[:800])
        keep = newkeep
    per = {}
    for l in open(lst).read().split('\n'):
        m = re.match(r'\s*(\d+) ([0-9A-F]{8}) ([0-9A-F]+)', l)
        if m and int(m.group(1)) not in per:
            per[int(m.group(1))] = int(m.group(2), 16)
    return keep, open(outp, 'rb').read(), rejected, per

def compare(arch, lines, show=40, opts=(), ignore_cly_reject=False):
    lkeep, lenc, lrej = llvm_encode(arch, lines)
    cur = [lines[i] for i in lkeep]
    ckeep, cbin, crej = cly_encode(arch, cur, opts)
    # llvm lines encoded; align by cly keep (subset of lkeep)
    # build llvm per-line encodings offsets with first nop
    pos = 4 if arch == 'a64' else 4
    # we need per-line byte offsets in llvm output: sum encoding lengths
    offs = []
    o = 0
    for e in lenc:
        offs.append(o)
        o += len(e)
    # llvm includes L1 nop as lenc[0]; instructions follow
    diffs = 0
    ok = 0
    cly_rej = 0
    bad = []
    # cly layout: only accepted lines emitted; llvm layout differs if cly rejects lines.
    if len(crej):
        # re-run llvm with only cly-accepted lines to keep layouts identical
        pass
    return lkeep, lenc, lrej, ckeep, cbin, crej

def run(arch, lines, show=40, opts=()):
    lkeep, lenc, lrej = llvm_encode(arch, lines)
    llines = [lines[i] for i in lkeep]
    ckeep, cbin, crej, per = cly_encode(arch, llines, opts)
    clines = [llines[i] for i in ckeep]
    lk2, lenc2, lr2 = llvm_encode(arch, clines)
    assert len(lk2) == len(clines), (len(lk2), len(clines))
    ck2, cbin2, cr2, per2 = cly_encode(arch, clines, opts)
    assert len(ck2) == len(clines), 'cly second pass rejected lines'
    starts = [per2[3]] + [per2[4 + k] for k in range(len(clines))] + [per2[4 + len(clines) + 1]]
    ends = starts[1:] + [len(cbin2)]
    diffs = 0
    for idx, line in enumerate(clines):
        e = lenc2[idx + 1]
        got = cbin2[starts[idx + 1]:ends[idx + 1]]
        if got != e:
            diffs += 1
            if diffs <= show:
                print('DIFF  %-44s llvm=%s cly=%s' % (line, e.hex(), got.hex()))
    res = 'identical' if diffs == 0 else 'different'
    return dict(total=len(lines), llvm_rejects=[lines[i] for i in lrej], cly_rejects=[(llines[i], crej[i]) for i in crej], compared=len(clines), diffs=diffs, result=res)

def report(arch, lines, show=40, opts=(), reject_show=25):
    r = run(arch, lines, show, opts)
    print('%s total=%d compared=%d diffs=%d llvm_rejects=%d cly_rejects=%d' % (arch, r['total'], r['compared'], r['diffs'], len(r['llvm_rejects']), len(r['cly_rejects'])))
    for l, e in r['cly_rejects'][:reject_show]:
        msg = re.findall(r'error: (.*)', e)
        print('  CLY REJECTS  %-44s %s' % (l, msg[0] if msg else ''))
    return r

def overaccept(arch, lines, opts=(), show=30):
    r = run(arch, lines, 0, opts)
    rej = r['llvm_rejects']
    if not rej:
        return []
    ck, cb, cr, _ = cly_encode(arch, rej, opts)
    acc = [rej[i] for i in ck]
    print('%s llvm rejects %d, cly accepts %d of them' % (arch, len(rej), len(acc)))
    for l in acc[:show]:
        print('  CLY ACCEPTS  ' + l)
    return acc
