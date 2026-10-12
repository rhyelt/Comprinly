import itertools
A = ['8b', '16b', '4h', '8h', '2s', '4s', '2d']
def v(n, a): return 'v%d.%s' % (n, a)

def three():
    L = []
    names = 'shadd uhadd sqadd uqadd srhadd urhadd shsub uhsub sqsub uqsub cmgt cmhi cmge cmhs sshl ushl sqshl uqshl srshl urshl sqrshl uqrshl smax umax smin umin sabd uabd saba uaba add sub cmtst cmeq mla mls mul pmul smaxp umaxp sminp uminp sqdmulh sqrdmulh addp'.split()
    for n in names:
        for a in A:
            L.append('%s %s, %s, %s' % (n, v(1, a), v(2, a), v(3, a)))
    for n in 'fmaxnm fminnm fmla fmls fadd fsub fmulx fcmeq fmax fmin frecps frsqrts fmaxnmp fminnmp faddp fabd fmul fcmge fcmgt facge facgt fmaxp fminp fdiv'.split():
        for a in ['2s', '4s', '2d', '4h', '8h']:
            L.append('%s %s, %s, %s' % (n, v(1, a), v(2, a), v(3, a)))
    for n in 'and bic orr orn eor bsl bit bif'.split():
        for a in ['8b', '16b']:
            L.append('%s %s, %s, %s' % (n, v(4, a), v(5, a), v(6, a)))
    for n in 'add sub cmeq cmgt cmge cmhi cmhs cmtst sshl ushl sqadd uqadd sqsub uqsub srshl urshl sqshl uqshl sqrshl uqrshl'.split():
        L.append('%s d1, d2, d3' % n)
    for n in 'sqdmulh sqrdmulh'.split():
        L.append('%s h1, h2, h3' % n); L.append('%s s1, s2, s3' % n)
    for n in 'fmulx fcmeq fcmge fcmgt facge facgt frecps frsqrts fabd'.split():
        L.append('%s s1, s2, s3' % n); L.append('%s d1, d2, d3' % n)
    return L

def two():
    L = []
    for n in 'rev64 cls clz abs neg sqabs sqneg suqadd usqadd'.split():
        for a in ['8b', '16b', '4h', '8h', '2s', '4s', '2d']:
            L.append('%s %s, %s' % (n, v(1, a), v(2, a)))
    for n in 'rev32 rev16 cnt not mvn rbit'.split():
        for a in ['8b', '16b', '4h', '8h']:
            L.append('%s %s, %s' % (n, v(1, a), v(2, a)))
    for n in 'saddlp uaddlp sadalp uadalp'.split():
        for a, b in [('4h', '8b'), ('8h', '16b'), ('2s', '4h'), ('4s', '8h'), ('1d', '2s'), ('2d', '4s')]:
            L.append('%s %s, %s' % (n, v(1, a), v(2, b)))
    for n in 'xtn xtn2 sqxtn sqxtn2 uqxtn uqxtn2 sqxtun sqxtun2'.split():
        for a, b in [('8b', '8h'), ('4h', '4s'), ('2s', '2d'), ('16b', '8h'), ('8h', '4s'), ('4s', '2d')]:
            L.append('%s %s, %s' % (n, v(1, a), v(2, b)))
    for n in 'frintn frintm frintp frintz frinta frintx frinti fabs fneg fsqrt frecpe frsqrte fcvtns fcvtms fcvtas fcvtps fcvtzs fcvtnu fcvtmu fcvtau fcvtpu fcvtzu scvtf ucvtf'.split():
        for a in ['2s', '4s', '2d', '4h', '8h']:
            L.append('%s %s, %s' % (n, v(1, a), v(2, a)))
    for n in 'cmgt cmeq cmlt cmge cmle'.split():
        for a in A:
            L.append('%s %s, %s, #0' % (n, v(1, a), v(2, a)))
    for n in 'fcmgt fcmeq fcmlt fcmge fcmle'.split():
        for a in ['2s', '4s', '2d', '4h', '8h']:
            L.append('%s %s, %s, #0.0' % (n, v(1, a), v(2, a)))
    for n in 'abs neg cmgt cmeq cmlt cmge cmle sqabs sqneg suqadd usqadd'.split():
        L.append('%s d1, d2' % n) if n in ('abs', 'neg', 'sqabs', 'sqneg', 'suqadd', 'usqadd') else L.append('%s d1, d2, #0' % n)
    L += ['shll v1.8h, v2.8b, #8', 'shll2 v1.4s, v2.8h, #16', 'shll v1.2d, v2.2s, #32', 'fcvtn v1.4h, v2.4s', 'fcvtn2 v1.8h, v2.4s', 'fcvtn v1.2s, v2.2d', 'fcvtl v1.4s, v2.4h', 'fcvtl2 v1.2d, v2.4s', 'fcvtxn v1.2s, v2.2d', 'frecpe s1, s2', 'frecpx d1, d2', 'urecpe v1.4s, v2.4s', 'ursqrte v1.2s, v2.2s']
    return L

def misc():
    L = []
    for n in 'addv smaxv umaxv sminv uminv'.split():
        for a, b in [('b', '8b'), ('b', '16b'), ('h', '4h'), ('h', '8h'), ('s', '4s')]:
            L.append('%s %s1, v2.%s' % (n, a, b))
    for n in 'saddlv uaddlv'.split():
        for a, b in [('h', '8b'), ('h', '16b'), ('s', '4h'), ('s', '8h'), ('d', '4s')]:
            L.append('%s %s1, v2.%s' % (n, a, b))
    for n in 'fmaxv fminv fmaxnmv fminnmv'.split():
        L.append('%s s1, v2.4s' % n)
    for n in 'uzp1 uzp2 trn1 trn2 zip1 zip2'.split():
        for a in A:
            L.append('%s %s, %s, %s' % (n, v(1, a), v(2, a), v(3, a)))
    for i in range(0, 16, 5):
        L.append('ext v1.16b, v2.16b, v3.16b, #%d' % i)
    for i in range(0, 8, 3):
        L.append('ext v1.8b, v2.8b, v3.8b, #%d' % i)
    for n in 'tbl tbx'.split():
        for k in range(1, 5):
            regs = ', '.join('v%d.16b' % (4 + j) for j in range(k))
            L.append('%s v1.16b, {%s}, v9.16b' % (n, regs))
            L.append('%s v1.8b, {%s}, v9.8b' % (n, regs))
    for n in 'saddl uaddl saddw uaddw ssubl usubl ssubw usubw sabal uabal sabdl uabdl smlal umlal smlsl umlsl smull umull'.split():
        for a, b in [('8h', '8b'), ('4s', '4h'), ('2d', '2s')]:
            if n[-1] == 'w' or n in ('saddw', 'uaddw', 'ssubw', 'usubw'):
                L.append('%s v1.%s, v2.%s, v3.%s' % (n, a, a, b))
            else:
                L.append('%s v1.%s, v2.%s, v3.%s' % (n, a, b, b))
    for n in 'saddl2 uaddl2 smull2 umull2 sabal2 smlal2'.split():
        for a, b in [('8h', '16b'), ('4s', '8h'), ('2d', '4s')]:
            L.append('%s v1.%s, v2.%s, v3.%s' % (n, a, b, b))
    for n in 'addhn raddhn subhn rsubhn'.split():
        for a, b in [('8b', '8h'), ('4h', '4s'), ('2s', '2d')]:
            L.append('%s v1.%s, v2.%s, v3.%s' % (n, a, b, b))
        L.append('%s2 v1.16b, v2.8h, v3.8h' % n)
    for n in 'sqdmull sqdmlal sqdmlsl'.split():
        L += ['%s v1.4s, v2.4h, v3.4h' % n, '%s v1.2d, v2.2s, v3.2s' % n, '%s2 v1.4s, v2.8h, v3.8h' % n]
    L += ['pmull v1.8h, v2.8b, v3.8b', 'pmull2 v1.8h, v2.16b, v3.16b', 'pmull v1.1q, v2.1d, v3.1d', 'pmull2 v1.1q, v2.2d, v3.2d']
    return L

def elem():
    L = []
    for a, mx in [('b', 16), ('h', 8), ('s', 4), ('d', 2)]:
        for i in range(mx):
            for t in {'b': ['8b', '16b'], 'h': ['4h', '8h'], 's': ['2s', '4s'], 'd': ['2d']}[a]:
                L.append('dup v1.%s, v2.%s[%d]' % (t, a, i))
            L.append('dup %s1, v2.%s[%d]' % (a, a, i))
            L.append('ins v1.%s[%d], v2.%s[%d]' % (a, i, a, (i * 3) % mx))
            L.append('mov v1.%s[%d], v2.%s[%d]' % (a, i, a, (i * 5) % mx))
            L.append('ins v1.%s[%d], %s2' % (a, i, 'x' if a == 'd' else 'w'))
            L.append('mov v1.%s[%d], %s2' % (a, i, 'x' if a == 'd' else 'w'))
            if a == 'd':
                L.append('umov x1, v2.d[%d]' % i); L.append('mov x1, v2.d[%d]' % i)
            else:
                L.append('umov w1, v2.%s[%d]' % (a, i))
            if a == 's':
                L.append('mov w1, v2.s[%d]' % i); L.append('smov x1, v2.s[%d]' % i)
            if a in 'bh':
                L.append('smov w1, v2.%s[%d]' % (a, i)); L.append('smov x1, v2.%s[%d]' % (a, i))
    for t, g in [('8b', 'w'), ('16b', 'w'), ('4h', 'w'), ('8h', 'w'), ('2s', 'w'), ('4s', 'w'), ('2d', 'x')]:
        L.append('dup v1.%s, %s2' % (t, g))
    L += ['mov v1.16b, v2.16b', 'mov v1.8b, v2.8b', 'mov v31.16b, v0.16b']
    L += ['dup v1.1d, v2.d[0]', 'dup v1.4s, v2.s[4]', 'umov w1, v2.d[0]', 'ins v1.s[4], w2']
    L += ['sxtl v1.8h, v2.8b', 'sxtl2 v1.8h, v2.16b', 'uxtl v1.4s, v2.4h', 'uxtl2 v1.4s, v2.8h', 'sxtl v1.2d, v2.2s', 'uxtl2 v1.2d, v2.4s']
    L += ['fmov v1.d[1], x2', 'fmov x1, v2.d[1]']
    for n in ['addp d1, v2.2d', 'faddp s1, v2.2s', 'faddp d1, v2.2d', 'faddp h1, v2.2h', 'fmaxp s1, v2.2s', 'fmaxp d1, v2.2d', 'fminp s1, v2.2s', 'fmaxnmp d1, v2.2d', 'fminnmp s1, v2.2s', 'fminnmp h1, v2.2h', 'fmaxp h1, v2.2h']:
        L.append(n)
    return L

def immv():
    L = []
    for q in ['8b', '16b']:
        for i in [0, 1, 0x7f, 0x80, 0xff]:
            L.append('movi v1.%s, #%d' % (q, i))
    for t in ['4h', '8h']:
        for n in ['movi', 'mvni', 'orr', 'bic']:
            for i in [0, 1, 0x55, 0xff]:
                for sh in ['', ', lsl #0', ', lsl #8']:
                    L.append('%s v1.%s, #%d%s' % (n, t, i, sh))
    for t in ['2s', '4s']:
        for n in ['movi', 'mvni', 'orr', 'bic']:
            for i in [0, 1, 0x55, 0xff]:
                for sh in ['', ', lsl #0', ', lsl #8', ', lsl #16', ', lsl #24']:
                    L.append('%s v1.%s, #%d%s' % (n, t, i, sh))
        for n in ['movi', 'mvni']:
            for i in [0, 1, 0xff]:
                for sh in [8, 16]:
                    L.append('%s v1.%s, #%d, msl #%d' % (n, t, i, sh))
    L += ['movi v1.2d, #0xff00ff00ff00ff00', 'movi v1.2d, #0', 'movi d1, #0xffffffffffffffff', 'movi d1, #0xff', 'movi v1.2d, #0xffffffffffffffff']
    for t in ['2s', '4s', '2d', '4h', '8h']:
        for f in ['1.0', '0.5', '-2.5', '31.0', '0.125', '-0.25']:
            L.append('fmov v1.%s, #%s' % (t, f))
    L += ['fmov s1, #1.0', 'fmov d1, #-1.5', 'fmov h1, #2.0']
    return L

def shifts():
    L = []
    for n in 'sshr ushr ssra usra srshr urshr srsra ursra sri'.split():
        for a, es in [('8b', 8), ('16b', 8), ('4h', 16), ('8h', 16), ('2s', 32), ('4s', 32), ('2d', 64)]:
            for s in [1, es // 2, es]:
                L.append('%s v1.%s, v2.%s, #%d' % (n, a, a, s))
        L.append('%s d1, d2, #5' % n); L.append('%s d1, d2, #64' % n)
    for n in 'shl sli sqshl uqshl sqshlu'.split():
        for a, es in [('8b', 8), ('16b', 8), ('4h', 16), ('8h', 16), ('2s', 32), ('4s', 32), ('2d', 64)]:
            for s in [0, 1, es - 1]:
                L.append('%s v1.%s, v2.%s, #%d' % (n, a, a, s))
        L.append('%s d1, d2, #5' % n); L.append('%s d1, d2, #63' % n)
        if n.endswith('shl') or n.endswith('shlu'):
            for r in 'bhs':
                L.append('%s %s1, %s2, #1' % (n, r, r))
    for n in 'shrn rshrn sqshrn sqrshrn sqshrun sqrshrun uqshrn uqrshrn'.split():
        for a, b, es in [('8b', '8h', 8), ('4h', '4s', 16), ('2s', '2d', 32)]:
            for s in [1, es]:
                L.append('%s v1.%s, v2.%s, #%d' % (n, a, b, s))
        for a, b, es in [('16b', '8h', 8), ('8h', '4s', 16), ('4s', '2d', 32)]:
            L.append('%s2 v1.%s, v2.%s, #%d' % (n, a, b, es))
        if n not in ('shrn', 'rshrn'):
            L += ['%s b1, h2, #3' % n, '%s h1, s2, #7' % n, '%s s1, d2, #9' % n]
    for n in 'sshll ushll'.split():
        for a, b, es in [('8h', '8b', 8), ('4s', '4h', 16), ('2d', '2s', 32)]:
            for s in [0, 1, es - 1]:
                L.append('%s v1.%s, v2.%s, #%d' % (n, a, b, s))
        for a, b, es in [('8h', '16b', 8), ('4s', '8h', 16), ('2d', '4s', 32)]:
            L.append('%s2 v1.%s, v2.%s, #%d' % (n, a, b, es - 1))
    for n in 'scvtf ucvtf fcvtzs fcvtzu'.split():
        for a, es in [('4h', 16), ('8h', 16), ('2s', 32), ('4s', 32), ('2d', 64)]:
            for s in [1, es // 2, es]:
                L.append('%s v1.%s, v2.%s, #%d' % (n, a, a, s))
        L += ['%s s1, s2, #3' % n, '%s d1, d2, #40' % n, '%s h1, h2, #4' % n]
    return L

def byel():
    L = []
    for n in 'mul mla mls sqdmulh sqrdmulh sqrdmlah sqrdmlsh'.split():
        for a, e in [('4h', 'h'), ('8h', 'h'), ('2s', 's'), ('4s', 's')]:
            for i in range(0, 8 if e == 'h' else 4):
                L.append('%s v1.%s, v2.%s, v3.%s[%d]' % (n, a, a, e, i))
            L.append('%s v1.%s, v2.%s, v15.%s[1]' % (n, a, a, e))
            L.append('%s v1.%s, v2.%s, v31.%s[1]' % (n, a, a, e))
    for n in 'sqdmulh sqrdmulh sqrdmlah sqrdmlsh'.split():
        L += ['%s h1, h2, v3.h[5]' % n, '%s s1, s2, v3.s[2]' % n, '%s s1, s2, v20.s[3]' % n]
    for n in 'fmla fmls fmul fmulx'.split():
        for a, e in [('4h', 'h'), ('8h', 'h'), ('2s', 's'), ('4s', 's'), ('2d', 'd')]:
            for i in range(0, {'h': 8, 's': 4, 'd': 2}[e]):
                L.append('%s v1.%s, v2.%s, v3.%s[%d]' % (n, a, a, e, i))
            L.append('%s v1.%s, v2.%s, v21.%s[1]' % (n, a, a, e))
        for r, e in [('h', 'h'), ('s', 's'), ('d', 'd')]:
            L.append('%s %s1, %s2, v3.%s[1]' % (n, r, r, e))
            L.append('%s %s1, %s2, v19.%s[1]' % (n, r, r, e))
    for n in 'smlal umlal smlsl umlsl smull umull sqdmlal sqdmlsl sqdmull'.split():
        for a, b, e in [('4s', '4h', 'h'), ('2d', '2s', 's')]:
            for i in range(0, 8 if e == 'h' else 4):
                L.append('%s v1.%s, v2.%s, v3.%s[%d]' % (n, a, b, e, i))
        for a, b, e in [('4s', '8h', 'h'), ('2d', '4s', 's')]:
            L.append('%s2 v1.%s, v2.%s, v3.%s[1]' % (n, a, b, e))
            L.append('%s2 v1.%s, v2.%s, v18.%s[1]' % (n, a, b, e))
        if n.startswith('sqd'):
            L += ['%s s1, h2, v3.h[7]' % n, '%s d1, s2, v3.s[3]' % n, '%s d1, s2, v19.s[3]' % n]
    return L

def ldst():
    L = []
    arrs = [('8b', 8), ('16b', 16), ('4h', 8), ('8h', 16), ('2s', 8), ('4s', 16), ('1d', 8), ('2d', 16)]
    for n, k in [('ld1', 4), ('st1', 4), ('ld2', 2), ('st2', 2), ('ld3', 3), ('st3', 3), ('ld4', 4), ('st4', 4)]:
        for a, b in arrs:
            cnts = [1, 2, 3, 4] if n[2] == '1' else [int(n[2])]
            for cn in cnts:
                lst = ', '.join('v%d.%s' % ((5 + i) % 32, a) for i in range(cn))
                L.append('%s {%s}, [x0]' % (n, lst))
                L.append('%s {%s}, [sp]' % (n, lst))
                L.append('%s {%s}, [x1], #%d' % (n, lst, cn * b))
                L.append('%s {%s}, [x1], x2' % (n, lst))
    L += ['ld1 {v30.16b, v31.16b, v0.16b}, [x3]', 'ld1 {v0.16b-v3.16b}, [x1]', 'ld2 {v31.8b, v0.8b}, [x1]']
    for n in ['ld1', 'ld2', 'ld3', 'ld4', 'st1', 'st2', 'st3', 'st4']:
        N = int(n[2])
        for e, mx, by in [('b', 16, 1), ('h', 8, 2), ('s', 4, 4), ('d', 2, 8)]:
            for i in range(mx):
                lst = ', '.join('v%d.%s' % (5 + j, e) for j in range(N))
                L.append('%s {%s}[%d], [x0]' % (n, lst, i))
                L.append('%s {%s}[%d], [x1], #%d' % (n, lst, i, N * by))
                L.append('%s {%s}[%d], [x1], x2' % (n, lst, i))
    for N in range(1, 5):
        for a, b in [('8b', 1), ('16b', 1), ('4h', 2), ('8h', 2), ('2s', 4), ('4s', 4), ('1d', 8), ('2d', 8)]:
            lst = ', '.join('v%d.%s' % (5 + j, a) for j in range(N))
            L.append('ld%dr {%s}, [x0]' % (N, lst))
            L.append('ld%dr {%s}, [x1], #%d' % (N, lst, N * b))
            L.append('ld%dr {%s}, [x1], x2' % (N, lst))
    return L

def crypto():
    L = []
    for n in 'aese aesd aesmc aesimc'.split(): L.append('%s v1.16b, v2.16b' % n)
    for n in 'sha1c sha1p sha1m'.split(): L.append('%s q1, s2, v3.4s' % n)
    L += ['sha1su0 v1.4s, v2.4s, v3.4s', 'sha256h q1, q2, v3.4s', 'sha256h2 q1, q2, v3.4s', 'sha256su1 v1.4s, v2.4s, v3.4s', 'sha1h s1, s2', 'sha1su1 v1.4s, v2.4s', 'sha256su0 v1.4s, v2.4s']
    L += ['sha512h q1, q2, v3.2d', 'sha512h2 q1, q2, v3.2d', 'sha512su1 v1.2d, v2.2d, v3.2d', 'rax1 v1.2d, v2.2d, v3.2d', 'sha512su0 v1.2d, v2.2d']
    L += ['sm3partw1 v1.4s, v2.4s, v3.4s', 'sm3partw2 v1.4s, v2.4s, v3.4s', 'sm3ss1 v1.4s, v2.4s, v3.4s, v4.4s', 'sm4e v1.4s, v2.4s', 'sm4ekey v1.4s, v2.4s, v3.4s']
    for n in 'sm3tt1a sm3tt1b sm3tt2a sm3tt2b'.split():
        for i in range(4): L.append('%s v1.4s, v2.4s, v3.s[%d]' % (n, i))
    L += ['eor3 v1.16b, v2.16b, v3.16b, v4.16b', 'bcax v1.16b, v2.16b, v3.16b, v4.16b', 'xar v1.2d, v2.2d, v3.2d, #0', 'xar v1.2d, v2.2d, v3.2d, #63']
    return L

def dots():
    L = []
    for n in 'sdot udot usdot'.split():
        for q, a in [(0, '8b'), (1, '16b')]:
            d = '2s' if not q else '4s'
            L.append('%s v1.%s, v2.%s, v3.%s' % (n, d, a, a))
            for i in range(4):
                L.append('%s v1.%s, v2.%s, v3.4b[%d]' % (n, d, a, i))
            L.append('%s v1.%s, v2.%s, v19.4b[3]' % (n, d, a))
    for q, a in [(0, '8b'), (1, '16b')]:
        d = '2s' if not q else '4s'
        L.append('sudot v1.%s, v2.%s, v3.4b[1]' % (d, a)); L.append('sudot v1.%s, v2.%s, v21.4b[2]' % (d, a))
    for q, a, d in [(0, '4h', '2s'), (1, '8h', '4s')]:
        L.append('bfdot v1.%s, v2.%s, v3.%s' % (d, a, a))
        for i in range(4): L.append('bfdot v1.%s, v2.%s, v3.2h[%d]' % (d, a, i))
        L.append('bfdot v1.%s, v2.%s, v23.2h[3]' % (d, a))
    L += ['smmla v1.4s, v2.16b, v3.16b', 'ummla v1.4s, v2.16b, v3.16b', 'usmmla v1.4s, v2.16b, v3.16b', 'bfmmla v1.4s, v2.8h, v3.8h']
    for n in ['bfmlalb', 'bfmlalt']:
        L.append('%s v1.4s, v2.8h, v3.8h' % n)
        for i in range(8): L.append('%s v1.4s, v2.8h, v3.h[%d]' % (n, i))
        L.append('%s v1.4s, v2.8h, v15.h[3]' % n)
    L += ['bfcvtn v1.4h, v2.4s', 'bfcvtn2 v1.8h, v2.4s', 'bfcvt h1, s2']
    for n in 'fmlal fmlsl fmlal2 fmlsl2'.split():
        L.append('%s v1.2s, v2.2h, v3.2h' % n); L.append('%s v1.4s, v2.4h, v3.4h' % n)
        for i in range(8):
            L.append('%s v1.2s, v2.2h, v3.h[%d]' % (n, i)); L.append('%s v1.4s, v2.4h, v7.h[%d]' % (n, i))
    for n in 'sqrdmlah sqrdmlsh'.split():
        for a in ['4h', '8h', '2s', '4s']: L.append('%s v1.%s, v2.%s, v3.%s' % (n, a, a, a))
        L += ['%s h1, h2, h3' % n, '%s s1, s2, s3' % n]
    for rot in [0, 90, 180, 270]:
        for a in ['4h', '8h', '2s', '4s', '2d']:
            L.append('fcmla v1.%s, v2.%s, v3.%s, #%d' % (a, a, a, rot))
        for i in range(2): L.append('fcmla v1.4s, v2.4s, v3.s[%d], #%d' % (i, rot))
        for i in range(4): L.append('fcmla v1.8h, v2.8h, v3.h[%d], #%d' % (i, rot))
        for i in range(2): L.append('fcmla v1.4h, v2.4h, v3.h[%d], #%d' % (i, rot))
        L.append('fcmla v1.8h, v2.8h, v19.h[3], #%d' % rot); L.append('fcmla v1.4s, v2.4s, v19.s[1], #%d' % rot)
    for rot in [90, 270]:
        for a in ['4h', '8h', '2s', '4s', '2d']:
            L.append('fcadd v1.%s, v2.%s, v3.%s, #%d' % (a, a, a, rot))
    return L
