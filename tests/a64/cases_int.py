import random

R = random.Random(12345)

def x(n): return 'x%d' % n
def w(n): return 'w%d' % n

def rx(zr=False, sp=False):
    c = list(range(0, 31))
    if zr: c.append('xzr')
    if sp: c.append('sp')
    v = R.choice(c)
    return x(v) if isinstance(v, int) else v

def rw(zr=False, sp=False):
    c = list(range(0, 31))
    if zr: c.append('wzr')
    if sp: c.append('wsp')
    v = R.choice(c)
    return w(v) if isinstance(v, int) else v

def rr(is64, zr=False, sp=False):
    return rx(zr, sp) if is64 else rw(zr, sp)

def regs_edge(is64, zr=False, sp=False):
    base = [0, 1, 7, 15, 28, 30]
    out = [(x(i) if is64 else w(i)) for i in base]
    if zr: out.append('xzr' if is64 else 'wzr')
    if sp: out.append('sp' if is64 else 'wsp')
    return out

def addsub():
    L = []
    for mn in ['add', 'adds', 'sub', 'subs']:
        S = mn.endswith('s')
        for is64 in (True, False):
            for imm in [0, 1, 7, 255, 256, 4095, 4096, 0xfff000, -1, -4095, -4096, 5000, 0x1000000]:
                for sh in ['', ', lsl #0', ', lsl #12']:
                    if imm in (4096, 0xfff000, -4096, 5000, 0x1000000) and sh:
                        continue
                    d = rr(is64, zr=S, sp=not S)
                    n = rr(is64, sp=True)
                    L.append('%s %s, %s, #%d%s' % (mn, d, n, imm, sh))
            for k in range(40):
                d = rr(is64, zr=True, sp=False)
                n = rr(is64, zr=True)
                m = rr(is64, zr=True)
                sh = R.choice(['', ', lsl #%d' % R.randrange(0, 64 if is64 else 32), ', lsr #%d' % R.randrange(0, 64 if is64 else 32), ', asr #%d' % R.randrange(0, 64 if is64 else 32)])
                L.append('%s %s, %s, %s%s' % (mn, d, n, m, sh))
            for k in range(40):
                d = rr(is64, zr=S, sp=not S)
                n = rr(is64, sp=True)
                if is64:
                    kind = R.choice(['uxtb', 'uxth', 'uxtw', 'uxtx', 'sxtb', 'sxth', 'sxtw', 'sxtx', 'lsl'])
                    m = rw(zr=True) if kind in ('uxtb', 'uxth', 'uxtw', 'sxtb', 'sxth', 'sxtw') else rx(zr=True)
                else:
                    kind = R.choice(['uxtb', 'uxth', 'uxtw', 'sxtb', 'sxth', 'sxtw', 'lsl'])
                    m = rw(zr=True)
                amt = R.choice(['', ' #%d' % R.randrange(0, 5)])
                if kind == 'lsl' and not amt: amt = ' #%d' % R.randrange(0, 5)
                L.append('%s %s, %s, %s, %s%s' % (mn, d, n, m, kind, amt))
            L.append('%s %s, sp, %s' % (mn, rr(is64, zr=S), rr(is64, zr=True)))
            L.append('%s %s, %s, %s' % (mn, 'sp' if is64 else 'wsp', rr(is64), rr(is64)) if not S else '%s %s, %s, #1' % (mn, rr(is64), rr(is64)))
    for mn in ['cmp', 'cmn']:
        for is64 in (True, False):
            for imm in [0, 1, 4095, -1, -4095, 4096, 0xfff000]:
                L.append('%s %s, #%d' % (mn, rr(is64, sp=True), imm))
            for k in range(20):
                sh = R.choice(['', ', lsl #%d' % R.randrange(0, 32), ', asr #%d' % R.randrange(0, 32)])
                L.append('%s %s, %s%s' % (mn, rr(is64, zr=True), rr(is64, zr=True), sh))
            L.append('%s %s, %s, uxtb #2' % (mn, rr(is64, sp=True), rw(zr=True)))
            L.append('%s sp, %s' % (mn, rr(is64)) if is64 else '%s wsp, %s' % (mn, rr(is64)))
    for mn in ['neg', 'negs']:
        for is64 in (True, False):
            for k in range(8):
                sh = R.choice(['', ', lsl #%d' % R.randrange(0, 32), ', asr #%d' % R.randrange(0, 32)])
                L.append('%s %s, %s%s' % (mn, rr(is64, zr=True), rr(is64, zr=True), sh))
    return L

def bitmasks(is64):
    vals = []
    size = 64 if is64 else 32
    for esz in [2, 4, 8, 16, 32, 64]:
        if esz > size: continue
        for ones in range(1, esz):
            for rot in range(esz):
                e = ((1 << ones) - 1)
                e = ((e << rot) | (e >> (esz - rot))) & ((1 << esz) - 1)
                v = 0
                for i in range(0, size, esz):
                    v |= e << i
                vals.append(v)
    vals = sorted(set(vals))
    R.shuffle(vals)
    return vals

def logical():
    L = []
    for mn in ['and', 'orr', 'eor', 'ands', 'bic', 'orn', 'eon', 'bics']:
        for is64 in (True, False):
            bm = bitmasks(is64)
            for v in bm[:30]:
                d = rr(is64, zr=(mn in ('ands', 'bics')), sp=(mn not in ('ands', 'bics')))
                n = rr(is64, zr=True)
                L.append('%s %s, %s, #0x%x' % (mn, d, n, v))
            for v in [0, 1, 2, 3, 5, 0xffffffff, 0xffffffffffffffff, 0x12345678, 0x8000000000000001]:
                if not is64 and v > 0xffffffff: continue
                L.append('%s %s, %s, #0x%x' % (mn, rr(is64, zr=True), rr(is64, zr=True), v))
            for k in range(30):
                sh = R.choice(['', ', lsl #%d' % R.randrange(0, 32), ', lsr #%d' % R.randrange(0, 32), ', asr #%d' % R.randrange(0, 32), ', ror #%d' % R.randrange(0, 32)])
                L.append('%s %s, %s, %s%s' % (mn, rr(is64, zr=True), rr(is64, zr=True), rr(is64, zr=True), sh))
    for is64 in (True, False):
        for v in bitmasks(is64)[:20]:
            L.append('tst %s, #0x%x' % (rr(is64, zr=True), v))
        for k in range(10):
            sh = R.choice(['', ', lsl #%d' % R.randrange(0, 32), ', ror #%d' % R.randrange(0, 32)])
            L.append('tst %s, %s%s' % (rr(is64, zr=True), rr(is64, zr=True), sh))
            sh = R.choice(['', ', lsl #%d' % R.randrange(0, 32), ', asr #%d' % R.randrange(0, 32)])
            L.append('mvn %s, %s%s' % (rr(is64, zr=True), rr(is64, zr=True), sh))
    return L

def movs():
    L = []
    for mn in ['movz', 'movn', 'movk']:
        for is64 in (True, False):
            for imm in [0, 1, 0xffff, 0x1234, 0x8000]:
                for sh in ([0, 16, 32, 48] if is64 else [0, 16]):
                    L.append('%s %s, #%d, lsl #%d' % (mn, rr(is64, zr=True) if False else rr(is64), imm, sh))
                L.append('%s %s, #%d' % (mn, rr(is64), imm))
    for is64 in (True, False):
        for imm in [0, 1, 0xffff, 0x10000, 0x12340000, 0xffff0000, -1, -2, -0x10000, 0x8000, -0x8000, 0xff00ff00, 0x5555555555555555, 0x1234, 0xfffffffe, 0x7fffffff, 0x80000000, 0xffffffff]:
            if not is64 and imm > 0xffffffff: continue
            L.append('mov %s, #%d' % (rr(is64), imm))
        for imm in [0x100000000, 0xffff00000000, 0x1234000000000000, 0xfffffffffffffffe, 0x8000000000000000, 0xfffe000000000000, 0xffff000000000000, 0xffffffff00000000, 0xffffffffffff0000]:
            if not is64: continue
            L.append('mov %s, #0x%x' % (rr(is64), imm))
        for k in range(40):
            L.append('mov %s, %s' % (rr(is64, zr=True), rr(is64, zr=True)))
    L.append('mov sp, x3'); L.append('mov x3, sp'); L.append('mov wsp, w3'); L.append('mov w3, wsp'); L.append('mov sp, sp')
    for v in bitmasks(True)[:15]:
        L.append('mov x%d, #0x%x' % (R.randrange(31), v))
    for v in bitmasks(False)[:15]:
        L.append('mov w%d, #0x%x' % (R.randrange(31), v))
    return L

def pcrel():
    L = []
    for r in range(0, 31, 3):
        L.append('adr x%d, L1' % r)
        L.append('adr x%d, L2' % r)
        L.append('adrp x%d, L1' % r)
    L += ['b L1', 'b L2', 'bl L1', 'bl L2', 'b.eq L1', 'b.ne L2', 'bne L1', 'beq L2', 'b.hs L1', 'b.lo L2', 'b.al L1', 'b.nv L2']
    for cc in ['eq', 'ne', 'cs', 'cc', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le', 'al', 'nv', 'hs', 'lo']:
        L.append('b.%s L1' % cc)
        L.append('b.%s L2' % cc)
    for mn in ['cbz', 'cbnz']:
        for is64 in (True, False):
            for lab in ('L1', 'L2'):
                L.append('%s %s, %s' % (mn, rr(is64, zr=True), lab))
    for mn in ['tbz', 'tbnz']:
        for b in [0, 1, 5, 31, 32, 63]:
            for lab in ('L1', 'L2'):
                reg = x(R.randrange(31)) if b >= 32 else R.choice([x(R.randrange(31)), w(R.randrange(31))])
                L.append('%s %s, #%d, %s' % (mn, reg, b, lab))
    for r in [0, 1, 17, 30]:
        L += ['br x%d' % r, 'blr x%d' % r, 'ret x%d' % r]
    L += ['ret', 'eret', 'drps']
    return L

def bitfield():
    L = []
    for is64 in (True, False):
        sz = 64 if is64 else 32
        for mn in ['sbfm', 'bfm', 'ubfm']:
            for k in range(10):
                L.append('%s %s, %s, #%d, #%d' % (mn, rr(is64, zr=True), rr(is64, zr=True), R.randrange(sz), R.randrange(sz)))
        for mn in ['sbfx', 'ubfx', 'sbfiz', 'ubfiz', 'bfi', 'bfxil']:
            for k in range(12):
                lsb = R.randrange(sz)
                wd = R.randrange(1, sz - lsb + 1)
                L.append('%s %s, %s, #%d, #%d' % (mn, rr(is64, zr=True), rr(is64, zr=True), lsb, wd))
        for k in range(8):
            lsb = R.randrange(sz)
            wd = R.randrange(1, sz - lsb + 1)
            L.append('bfc %s, #%d, #%d' % (rr(is64), lsb, wd))
        for mn in ['lsl', 'lsr', 'asr', 'ror']:
            for k in range(12):
                L.append('%s %s, %s, #%d' % (mn, rr(is64, zr=True), rr(is64, zr=True), R.randrange(sz)))
                L.append('%s %s, %s, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True), rr(is64, zr=True)))
        for mn in ['lslv', 'lsrv', 'asrv', 'rorv']:
            L.append('%s %s, %s, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True), rr(is64, zr=True)))
        for k in range(8):
            L.append('extr %s, %s, %s, #%d' % (rr(is64, zr=True), rr(is64, zr=True), rr(is64, zr=True), R.randrange(sz)))
    for mn in ['sxtb', 'sxth', 'sxtw']:
        for k in range(4):
            L.append('%s %s, %s' % (mn, rx(zr=True), rw(zr=True)))
    for mn in ['sxtb', 'sxth', 'uxtb', 'uxth']:
        for k in range(4):
            L.append('%s %s, %s' % (mn, rw(zr=True), rw(zr=True)))
    return L

def csel():
    L = []
    conds = ['eq', 'ne', 'cs', 'cc', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le', 'al', 'nv', 'hs', 'lo']
    for is64 in (True, False):
        for mn in ['csel', 'csinc', 'csinv', 'csneg']:
            for c in conds:
                L.append('%s %s, %s, %s, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True), rr(is64, zr=True), c))
        for mn in ['cset', 'csetm']:
            for c in conds:
                L.append('%s %s, %s' % (mn, rr(is64, zr=True), c))
        for mn in ['cinc', 'cinv', 'cneg']:
            for c in conds:
                L.append('%s %s, %s, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True), c))
        for mn in ['ccmp', 'ccmn']:
            for c in conds[:8]:
                L.append('%s %s, %s, #%d, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True), R.randrange(16), c))
                L.append('%s %s, #%d, #%d, %s' % (mn, rr(is64, zr=True), R.randrange(32), R.randrange(16), c))
        for mn in ['adc', 'adcs', 'sbc', 'sbcs']:
            for k in range(4):
                L.append('%s %s, %s, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True), rr(is64, zr=True)))
        for mn in ['ngc', 'ngcs']:
            L.append('%s %s, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True)))
    return L

def arith():
    L = []
    for is64 in (True, False):
        for mn in ['mul', 'mneg', 'udiv', 'sdiv']:
            for k in range(6):
                L.append('%s %s, %s, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True), rr(is64, zr=True)))
        for mn in ['madd', 'msub']:
            for k in range(6):
                L.append('%s %s, %s, %s, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True), rr(is64, zr=True), rr(is64, zr=True)))
        for mn in ['rbit', 'rev16', 'rev', 'clz', 'cls']:
            for k in range(4):
                L.append('%s %s, %s' % (mn, rr(is64, zr=True), rr(is64, zr=True)))
    for mn in ['rev32', 'rev64']:
        L.append('%s %s, %s' % (mn, rx(zr=True), rx(zr=True)))
    for mn in ['smull', 'umull', 'smnegl', 'umnegl']:
        for k in range(4):
            L.append('%s %s, %s, %s' % (mn, rx(zr=True), rw(zr=True), rw(zr=True)))
    for mn in ['smaddl', 'smsubl', 'umaddl', 'umsubl']:
        for k in range(4):
            L.append('%s %s, %s, %s, %s' % (mn, rx(zr=True), rw(zr=True), rw(zr=True), rx(zr=True)))
    for mn in ['smulh', 'umulh']:
        for k in range(4):
            L.append('%s %s, %s, %s' % (mn, rx(zr=True), rx(zr=True), rx(zr=True)))
    for mn in ['crc32b', 'crc32h', 'crc32w', 'crc32cb', 'crc32ch', 'crc32cw']:
        L.append('%s %s, %s, %s' % (mn, rw(zr=True), rw(zr=True), rw(zr=True)))
    for mn in ['crc32x', 'crc32cx']:
        L.append('%s %s, %s, %s' % (mn, rw(zr=True), rw(zr=True), rx(zr=True)))
    return L

def system():
    L = []
    for mn in ['svc', 'hvc', 'smc', 'brk', 'hlt']:
        for v in [0, 1, 0x80, 0xffff, 0x1234]:
            L.append('%s #%d' % (mn, v))
    L += ['dcps1', 'dcps2', 'dcps3', 'dcps1 #5']
    for mn in ['nop', 'yield', 'wfe', 'wfi', 'sev', 'sevl', 'dgh', 'esb', 'psb csync', 'csdb', 'paciasp', 'pacibsp', 'autiasp', 'autibsp', 'paciaz', 'pacibz', 'autiaz', 'autibz', 'xpaclri', 'pacia1716', 'pacib1716', 'autia1716', 'autib1716', 'tsb csync']:
        L.append(mn)
    L += ['hint #0', 'hint #7', 'hint #127', 'hint #34']
    for mn in ['dmb', 'dsb']:
        for o in ['sy', 'st', 'ld', 'ish', 'ishst', 'ishld', 'nsh', 'nshst', 'nshld', 'osh', 'oshst', 'oshld', '#0', '#15', '#4', '#8']:
            L.append('%s %s' % (mn, o))
    L += ['isb', 'isb sy', 'isb #3', 'clrex', 'clrex #5', 'clrex #15']
    from sysregs import SYSREGS
    for r in SYSREGS:
        L.append('mrs x%d, %s' % (R.randrange(31), r))
        L.append('msr %s, x%d' % (r, R.randrange(31)))
    L += ['mrs x0, s3_0_c15_c2_0', 'msr s3_1_c11_c0_3, x5', 'mrs x1, S2_3_C4_C5_6', 'mrs x2, S3_7_C15_C15_7']
    for p in ['spsel', 'daifset', 'daifclr', 'uao', 'pan', 'dit', 'ssbs', 'tco']:
        for v in [0, 1, 7, 15]:
            L.append('msr %s, #%d' % (p, v))
    L += ['sys #0, c7, c5, #0', 'sys #3, c7, c14, #1, x5', 'sysl x3, #1, c8, c3, #2']
    for o in ['zva', 'ivac', 'isw', 'cvac', 'csw', 'cvau', 'civac', 'cisw', 'cvap', 'cvadp']:
        L.append('dc %s, x%d' % (o, R.randrange(31)))
    L += ['ic ialluis', 'ic iallu', 'ic ivau, x4']
    for o in ['vmalle1is', 'alle1', 'alle2', 'alle3', 'vmalle1', 'alle1is', 'vmalls12e1', 'vmalls12e1is']:
        L.append('tlbi %s' % o)
    for o in ['vae1is', 'aside1is', 'vaae1is', 'vale1is', 'vaale1is', 'vae1', 'aside1', 'vaae1', 'vale1', 'vaale1', 'vae2is', 'vae3', 'ipas2e1is', 'ipas2le1']:
        L.append('tlbi %s, x%d' % (o, R.randrange(31)))
    for o in ['s1e1r', 's1e1w', 's1e0r', 's1e0w', 's1e2r', 's12e1r', 's12e0w', 's1e3w']:
        L.append('at %s, x%d' % (o, R.randrange(31)))
    return L

ALL = {}
def build():
    return dict(addsub=addsub(), logical=logical(), movs=movs(), pcrel=pcrel(), bitfield=bitfield(), csel=csel(), arith=arith(), system=system())
