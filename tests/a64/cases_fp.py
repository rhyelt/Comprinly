import random
R = random.Random(4242)
def f(t): return '%s%d' % (t, R.randint(0, 31))
def g(is64): return ('x%d' if is64 else 'w%d') % R.randint(0, 30)

def arith():
    L = []
    for t in 'sdh':
        for mn in ['fadd', 'fsub', 'fmul', 'fdiv', 'fmax', 'fmin', 'fmaxnm', 'fminnm', 'fnmul']:
            L.append('%s %s, %s, %s' % (mn, f(t), f(t), f(t)))
        for mn in ['fabs', 'fneg', 'fsqrt', 'frintn', 'frintp', 'frintm', 'frintz', 'frinta', 'frintx', 'frinti', 'fmov']:
            L.append('%s %s, %s' % (mn, f(t), f(t)))
        for mn in ['fmadd', 'fmsub', 'fnmadd', 'fnmsub']:
            L.append('%s %s, %s, %s, %s' % (mn, f(t), f(t), f(t), f(t)))
        for mn in ['fcmp', 'fcmpe']:
            L.append('%s %s, %s' % (mn, f(t), f(t)))
            L.append('%s %s, #0.0' % (mn, f(t)))
            L.append('%s %s, #0' % (mn, f(t)))
        for mn in ['fccmp', 'fccmpe']:
            for cc in ['eq', 'ne', 'hs', 'lo', 'mi', 'vs', 'ge', 'lt', 'gt', 'le', 'al']:
                L.append('%s %s, %s, #%d, %s' % (mn, f(t), f(t), R.randint(0, 15), cc))
        for cc in ['eq', 'ne', 'cs', 'cc', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le', 'al']:
            L.append('fcsel %s, %s, %s, %s' % (f(t), f(t), f(t), cc))
        for imm in ['0.0', '1.0', '2.0', '-1.0', '0.5', '31.0', '-0.125', '1.9375', '0.25', '-31.0', '3.0', '10.0', '0.1', '32.0']:
            L.append('fmov %s, #%s' % (f(t), imm))
    for a, b in [('s', 'd'), ('d', 's'), ('h', 's'), ('s', 'h'), ('h', 'd'), ('d', 'h')]:
        L.append('fcvt %s, %s' % (f(a), f(b)))
    L.append('fmov d3, x4')
    L.append('fmov x4, d3')
    L.append('fmov s3, w4')
    L.append('fmov w4, s3')
    L.append('fmov h3, w4')
    L.append('fmov w4, h3')
    L.append('fmov h3, x4')
    L.append('fmov x4, h3')
    L.append('fmov v3.d[1], x4')
    L.append('fmov x4, v3.d[1]')
    L.append('fmov d3, xzr')
    L.append('fmov s3, wzr')
    L.append('fjcvtzs w3, d4')
    return L

def cvt():
    L = []
    for t in 'sdh':
        for is64 in (True, False):
            for mn in ['scvtf', 'ucvtf']:
                L.append('%s %s, %s' % (mn, f(t), g(is64)))
                for fb in [1, 3, 32 if not is64 else 63, 64 if is64 else 32]:
                    L.append('%s %s, %s, #%d' % (mn, f(t), g(is64), fb))
            for mn in ['fcvtns', 'fcvtnu', 'fcvtps', 'fcvtpu', 'fcvtms', 'fcvtmu', 'fcvtzs', 'fcvtzu', 'fcvtas', 'fcvtau']:
                L.append('%s %s, %s' % (mn, g(is64), f(t)))
            for mn in ['fcvtzs', 'fcvtzu']:
                for fb in [1, 5, 32]:
                    L.append('%s %s, %s, #%d' % (mn, g(is64), f(t), fb))
    return L

def build():
    return arith() + cvt()
