import random
R = random.Random(777)
def X(i): return 'x%d' % i
def W(i): return 'w%d' % i
def gp(is64, sp=False, zr=True):
    c = list(range(0, 31))
    v = R.choice(c + (['zr'] if zr else []))
    if v == 'zr': return 'xzr' if is64 else 'wzr'
    return X(v) if is64 else W(v)
def base():
    v = R.choice(list(range(0, 31)) + ['sp'])
    return 'sp' if v == 'sp' else X(v)

def ldst():
    L = []
    for mn, sz, is64 in [('ldr', 8, True), ('ldr', 4, False), ('str', 8, True), ('str', 4, False), ('ldrb', 1, False), ('strb', 1, False), ('ldrh', 2, False), ('strh', 2, False), ('ldrsb', 1, True), ('ldrsb', 1, False), ('ldrsh', 2, True), ('ldrsh', 2, False), ('ldrsw', 4, True)]:
        for off in [0, sz, sz * 5, sz * 4095, sz * 4096, 1, 3, -1, -8, -256, -257, 255, 256, 257]:
            if off == 0:
                L.append('%s %s, [%s]' % (mn, gp(is64), base()))
            else:
                L.append('%s %s, [%s, #%d]' % (mn, gp(is64), base(), off))
        L.append('%s %s, [%s, #%d]!' % (mn, gp(is64), base(), 8 if sz > 1 else 3))
        L.append('%s %s, [%s, #-%d]!' % (mn, gp(is64), base(), 16))
        L.append('%s %s, [%s], #%d' % (mn, gp(is64), base(), 8))
        L.append('%s %s, [%s], #-%d' % (mn, gp(is64), base(), 256))
        for ext in ['', ', lsl #0', ', lsl #%d' % {1: 0, 2: 1, 4: 2, 8: 3}[sz]]:
            L.append('%s %s, [%s, %s%s]' % (mn, gp(is64), base(), X(R.randint(0, 30)), ext))
        for e in ['uxtw', 'sxtw', 'sxtx']:
            for a in ['', ' #%d' % {1: 0, 2: 1, 4: 2, 8: 3}[sz]]:
                r = W(R.randint(0, 30)) if e != 'sxtx' else X(R.randint(0, 30))
                L.append('%s %s, [%s, %s, %s%s]' % (mn, gp(is64), base(), r, e, a))
        L.append('%s %s, [%s, xzr]' % (mn, gp(is64), base()))
        L.append('%s %s, [%s, %s, uxtw]' % (mn, gp(is64), base(), 'wzr'))
        L.append('%s %s, [%s, :lo12:L1]' % (mn, gp(is64), X(R.randint(0, 30))))
    for mn, sz, is64 in [('ldur', 8, True), ('ldur', 4, False), ('stur', 8, True), ('ldurb', 1, False), ('sturb', 1, False), ('ldurh', 2, False), ('sturh', 2, False), ('ldursb', 1, True), ('ldursh', 2, False), ('ldursw', 4, True)]:
        for off in [0, 1, -1, 255, -256, 7, -100]:
            L.append('%s %s, [%s, #%d]' % (mn, gp(is64), base(), off))
    for mn in ['ldr', 'ldrsw']:
        for is64 in (True, False):
            if mn == 'ldrsw' and not is64: continue
            L.append('%s %s, L1' % (mn, gp(is64)))
            L.append('%s %s, L2' % (mn, gp(is64)))
    L.append('ldr x3, [x4, #8]')
    L.append('ldr w3, [x4,#4]')
    L.append('ldr x3,[x4 ,#8 ]')
    L.append('ldr x3, [ x4 , x5 , lsl #3 ]')
    return L

def pairs():
    L = []
    for mn in ['ldp', 'stp']:
        for is64, sz in ((True, 8), (False, 4)):
            for off in [0, sz, -sz, sz * 63, -sz * 64, sz * 64, -sz * 65, 1, sz * 2]:
                if off == 0:
                    L.append('%s %s, %s, [%s]' % (mn, gp(is64), gp(is64), base()))
                else:
                    L.append('%s %s, %s, [%s, #%d]' % (mn, gp(is64), gp(is64), base(), off))
            L.append('%s %s, %s, [%s, #%d]!' % (mn, gp(is64), gp(is64), base(), sz * 2))
            L.append('%s %s, %s, [%s], #%d' % (mn, gp(is64), gp(is64), base(), -sz * 2))
    for off in [0, 4, -256, 252, 256]:
        L.append('ldpsw %s, %s, [%s, #%d]' % (gp(True), gp(True), base(), off))
        L.append('ldnp %s, %s, [%s, #%d]' % (gp(True), gp(True), base(), off * 2))
        L.append('stnp %s, %s, [%s, #%d]' % (gp(False), gp(False), base(), off))
    L.append('ldpsw x1, x2, [x3], #8')
    L.append('ldpsw x1, x2, [x3, #8]!')
    return L

def excl():
    L = []
    for sfx, is64 in (('', True), ('', False), ('b', False), ('h', False)):
        for pre in ['ldxr', 'ldaxr', 'ldar', 'ldlar', 'ldapr']:
            if pre == 'ldapr' and sfx == '' and False: pass
            L.append('%s%s %s, [%s]' % (pre, sfx, gp(is64, zr=False), base()))
        for pre in ['stlr', 'stllr']:
            L.append('%s%s %s, [%s]' % (pre, sfx, gp(is64), base()))
        for pre in ['stxr', 'stlxr']:
            L.append('%s%s w%d, %s, [%s]' % (pre, sfx, R.randint(0, 30), gp(is64), base()))
    for is64 in (True, False):
        L.append('ldxp %s, %s, [%s]' % (gp(is64, zr=False), gp(is64, zr=False), base()))
        L.append('ldaxp %s, %s, [%s]' % (gp(is64, zr=False), gp(is64, zr=False), base()))
        L.append('stxp w%d, %s, %s, [%s]' % (R.randint(0, 30), gp(is64), gp(is64), base()))
        L.append('stlxp w%d, %s, %s, [%s]' % (R.randint(0, 30), gp(is64), gp(is64), base()))
    L.append('ldr x0, [x1, #8]')
    for sfx, is64 in (('', True), ('', False), ('b', False), ('h', False)):
        for pre in ['cas', 'casa', 'casal', 'casl']:
            L.append('%s%s %s, %s, [%s]' % (pre, sfx, gp(is64, zr=False), gp(is64, zr=False), base()))
        for op in ['add', 'clr', 'eor', 'set', 'smax', 'smin', 'umax', 'umin']:
            for ar in ['', 'a', 'l', 'al']:
                L.append('ld%s%s%s %s, %s, [%s]' % (op, ar, sfx, gp(is64), gp(is64), base()))
            for ar in ['', 'l']:
                L.append('st%s%s%s %s, [%s]' % (op, ar, sfx, gp(is64, zr=False), base()))
        for ar in ['', 'a', 'l', 'al']:
            L.append('swp%s%s %s, %s, [%s]' % (ar, sfx, gp(is64), gp(is64), base()))
    for is64 in (True, False):
        for ar in ['', 'a', 'l', 'al']:
            r = R.randrange(0, 30, 2)
            r2 = R.randrange(0, 30, 2)
            p = 'x' if is64 else 'w'
            L.append('casp%s %s%d, %s%d, %s%d, %s%d, [%s]' % (ar, p, r, p, r + 1, p, r2, p, r2 + 1, base()))
    return L

def prfm():
    L = []
    for op in ['pldl1keep', 'pldl1strm', 'pldl2keep', 'pldl3keep', 'plil1keep', 'pstl1keep', 'pstl3strm', '5', '31', '#7']:
        L.append('prfm %s, [%s]' % (op, base()))
        L.append('prfm %s, [%s, #%d]' % (op, base(), 8 * R.randint(0, 100)))
        L.append('prfm %s, [%s, %s, lsl #3]' % (op, base(), X(R.randint(0, 30))))
        L.append('prfum %s, [%s, #-%d]' % (op, base(), R.randint(1, 200)))
    L.append('prfm pldl1keep, L1')
    L.append('prfm pldl1keep, L2')
    return L

def build():
    return ldst() + pairs() + excl() + prfm()
