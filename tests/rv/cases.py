import random
R = random.Random(2024)

def xr(compr=None):
    if compr is None:
        compr = R.random() < 0.45
    if compr:
        return 'x%d' % R.randint(8, 15) if R.random() < 0.5 else R.choice(['s0', 's1', 'a0', 'a1', 'a2', 'a3', 'a4', 'a5', 'fp'])
    return R.choice(['x%d' % R.randint(0, 31), 'zero', 'ra', 'sp', 'gp', 'tp', 't0', 't1', 't2', 's2', 's11', 'a6', 'a7', 't3', 't6'])

def xr_nz():
    while True:
        r = xr()
        if r not in ('zero', 'x0'): return r

def fr(compr=None):
    if compr is None:
        compr = R.random() < 0.4
    if compr:
        return R.choice(['f8', 'f9', 'f10', 'f15', 'fs0', 'fs1', 'fa0', 'fa5'])
    return R.choice(['f%d' % R.randint(0, 31), 'ft0', 'ft11', 'fs2', 'fs11', 'fa7', 'ft5'])

def imm12():
    return R.choice([0, 1, -1, 2, 31, -32, 32, 63, 64, 2047, -2048, 100, -100, 17, 255, 256, R.randint(-2048, 2047)])

def rtype(rv64):
    L = []
    ops = ['add', 'sub', 'sll', 'slt', 'sltu', 'xor', 'srl', 'sra', 'or', 'and', 'mul', 'mulh', 'mulhsu', 'mulhu', 'div', 'divu', 'rem', 'remu',
           'andn', 'orn', 'xnor', 'max', 'maxu', 'min', 'minu', 'rol', 'ror', 'sh1add', 'sh2add', 'sh3add', 'bclr', 'bext', 'binv', 'bset',
           'clmul', 'clmulr', 'clmulh', 'czero.eqz', 'czero.nez']
    if rv64:
        ops += ['addw', 'subw', 'sllw', 'srlw', 'sraw', 'mulw', 'divw', 'divuw', 'remw', 'remuw', 'rolw', 'rorw', 'add.uw', 'sh1add.uw', 'sh2add.uw', 'sh3add.uw']
    for op in ops:
        for _ in range(8):
            L.append('%s %s, %s, %s' % (op, xr(), xr(), xr()))
        L.append('%s a0, a0, a1' % op)
        L.append('%s a0, a1, a0' % op)
        L.append('%s x8, x8, x9' % op)
        L.append('%s sp, sp, a0' % op)
    for op in ['clz', 'ctz', 'cpop', 'sext.b', 'sext.h', 'orc.b', 'rev8', 'zext.h', 'zext.b', 'mv', 'not', 'neg', 'seqz', 'snez', 'sltz', 'sgtz'] + (['clzw', 'ctzw', 'cpopw', 'sext.w', 'zext.w', 'negw'] if rv64 else []):
        for _ in range(4):
            L.append('%s %s, %s' % (op, xr(), xr()))
        L.append('%s a0, a1' % op)
        L.append('%s x9, x8' % op)
    return L

def itype(rv64):
    L = []
    for op in ['addi', 'slti', 'sltiu', 'xori', 'ori', 'andi'] + (['addiw'] if rv64 else []):
        for _ in range(14):
            L.append('%s %s, %s, %d' % (op, xr(), xr(), imm12()))
        for im in [0, 1, -1, 31, -32, 32, -33, 2047, -2048, 4, 16, -16, 496, -512, 8, 1020, 1024, 4095]:
            if -2048 <= im <= 2047:
                L.append('%s a0, a0, %d' % (op, im))
                L.append('%s sp, sp, %d' % (op, im))
                L.append('%s x9, sp, %d' % (op, im))
                L.append('%s zero, zero, %d' % (op, im))
    sh = 63 if rv64 else 31
    for op in ['slli', 'srli', 'srai', 'rori', 'bclri', 'bexti', 'binvi', 'bseti']:
        for s in [0, 1, 5, 31, sh, R.randint(0, sh)]:
            L.append('%s %s, %s, %d' % (op, xr(), xr(), s))
            L.append('%s a0, a0, %d' % (op, s))
            L.append('%s x9, x9, %d' % (op, s))
    if rv64:
        for op in ['slliw', 'srliw', 'sraiw', 'roriw']:
            for s in [0, 1, 5, 31]:
                L.append('%s %s, %s, %d' % (op, xr(), xr(), s))
        for s in [0, 1, 31, 32, 63]:
            L.append('slli.uw %s, %s, %d' % (xr(), xr(), s))
    for op in ['lui', 'auipc']:
        for v in [0, 1, 31, 32, 0x12345, 0xfffff, 0x80000, 0x7ffff, 2, 5, 0xfffe0, 0xfffff]:
            L.append('%s %s, %d' % (op, xr(), v))
            L.append('%s a0, 0x%x' % (op, v))
            L.append('%s s1, %d' % (op, v))
    return L

def mem(rv64):
    L = []
    ld = ['lb', 'lh', 'lw', 'lbu', 'lhu'] + (['ld', 'lwu'] if rv64 else [])
    st = ['sb', 'sh', 'sw'] + (['sd'] if rv64 else [])
    offs = [0, 4, 8, 124, 128, 252, 256, 508, 512, -4, -8, 2047, -2048, 1, 3, 100, 60, 61, 120, 248, 504, 255, 16, 24, 32]
    for op in ld:
        for o in offs:
            L.append('%s %s, %d(%s)' % (op, xr(), o, xr()))
            L.append('%s a0, %d(sp)' % (op, o))
            L.append('%s a1, %d(a2)' % (op, o))
            L.append('%s s0, %d(s1)' % (op, o))
        L.append('%s a0, (a1)' % op)
        L.append('%s a0, [a1]' % op)
        L.append('%s a0, [a1 + 8]' % op)
        L.append('%s a0, [sp - 8]' % op)
        L.append('%s a0, [a1, 12]' % op)
    for op in st:
        for o in offs:
            L.append('%s %s, %d(%s)' % (op, xr(), o, xr()))
            L.append('%s a0, %d(sp)' % (op, o))
            L.append('%s a1, %d(a2)' % (op, o))
            L.append('%s s0, %d(s1)' % (op, o))
        L.append('%s a0, (a1)' % op)
    for op in ['flw', 'fld']:
        for o in offs:
            L.append('%s %s, %d(%s)' % (op, fr(), o, xr()))
            L.append('%s fa0, %d(sp)' % (op, o))
            L.append('%s fs0, %d(s1)' % (op, o))
    for op in ['fsw', 'fsd']:
        for o in offs:
            L.append('%s %s, %d(%s)' % (op, fr(), o, xr()))
            L.append('%s fa0, %d(sp)' % (op, o))
            L.append('%s fs0, %d(s1)' % (op, o))
    return L

def branches():
    L = []
    for op in ['beq', 'bne', 'blt', 'bge', 'bltu', 'bgeu', 'bgt', 'ble', 'bgtu', 'bleu']:
        for t in ['L1', 'L2']:
            L.append('%s %s, %s, %s' % (op, xr(), xr(), t))
    for op in ['beqz', 'bnez', 'blez', 'bgez', 'bltz', 'bgtz']:
        for t in ['L1', 'L2']:
            for _ in range(3):
                L.append('%s %s, %s' % (op, xr(), t))
            L.append('%s a0, %s' % (op, t))
            L.append('%s s0, %s' % (op, t))
    for t in ['L1', 'L2']:
        L.append('j %s' % t)
        L.append('jal %s' % t)
        L.append('jal ra, %s' % t)
        L.append('jal t0, %s' % t)
        L.append('jal zero, %s' % t)
    for _ in range(6):
        L.append('jr %s' % xr())
        L.append('jalr %s' % xr())
        L.append('jalr %s, %s' % (xr(), xr()))
        L.append('jalr %s, %d(%s)' % (xr(), imm12(), xr()))
        L.append('jalr %s, %s, %d' % (xr(), xr(), imm12()))
    L.append('jr ra')
    L.append('ret')
    L.append('jalr ra')
    L.append('jalr a0')
    L.append('jalr zero, 0(ra)')
    L.append('jalr ra, 0(a5)')
    return L

def system(rv64):
    L = ['ecall', 'ebreak', 'mret', 'sret', 'wfi', 'fence', 'fence.i', 'fence rw, rw', 'fence r, r', 'fence w, w', 'fence iorw, iorw', 'fence ow, ir', 'fence.tso', 'pause', 'unimp', 'nop',
         'sfence.vma', 'sfence.vma a0', 'sfence.vma a0, a1', 'sfence.vma zero, a1']
    csrs = ['mstatus', 'mepc', 'mtvec', 'satp', 'cycle', 'time', 'instret', 'fcsr', 'frm', 'fflags', 'mhartid', 'mscratch', 'sstatus', '0x300', '0xc00', 'mcause', 'misa', 'pmpcfg0', 'pmpaddr3', 'hpmcounter3']
    for c in csrs:
        for op in ['csrrw', 'csrrs', 'csrrc']:
            L.append('%s %s, %s, %s' % (op, xr(), c, xr()))
        for op in ['csrrwi', 'csrrsi', 'csrrci']:
            L.append('%s %s, %s, %d' % (op, xr(), c, R.randint(0, 31)))
        L.append('csrr %s, %s' % (xr(), c))
        L.append('csrw %s, %s' % (c, xr()))
        L.append('csrs %s, %s' % (c, xr()))
        L.append('csrc %s, %s' % (c, xr()))
        L.append('csrwi %s, %d' % (c, R.randint(0, 31)))
        L.append('csrsi %s, %d' % (c, R.randint(0, 31)))
        L.append('csrci %s, %d' % (c, R.randint(0, 31)))
    for op in ['rdcycle', 'rdtime', 'rdinstret', 'frcsr', 'frrm', 'frflags']:
        L.append('%s %s' % (op, xr()))
    for op in ['fscsr', 'fsrm', 'fsflags']:
        L.append('%s %s' % (op, xr()))
        L.append('%s %s, %s' % (op, xr(), xr()))
    if not rv64:
        for op in ['rdcycleh', 'rdtimeh', 'rdinstreth']:
            L.append('%s %s' % (op, xr()))
    return L

def atomics(rv64):
    L = []
    ws = ['w'] + (['d'] if rv64 else [])
    for w in ws:
        for sfx in ['', '.aq', '.rl', '.aqrl']:
            L.append('lr.%s%s %s, (%s)' % (w, sfx, xr(), xr()))
            L.append('lr.%s%s %s, 0(%s)' % (w, sfx, xr(), xr()))
            L.append('sc.%s%s %s, %s, (%s)' % (w, sfx, xr(), xr(), xr()))
            for op in ['amoswap', 'amoadd', 'amoxor', 'amoand', 'amoor', 'amomin', 'amomax', 'amominu', 'amomaxu']:
                L.append('%s.%s%s %s, %s, (%s)' % (op, w, sfx, xr(), xr(), xr()))
    return L

def fp(rv64):
    L = []
    rms = ['', ', rne', ', rtz', ', rdn', ', rup', ', rmm', ', dyn']
    for t in 'sd':
        for op in ['fadd', 'fsub', 'fmul', 'fdiv']:
            for rm in rms:
                L.append('%s.%s %s, %s, %s%s' % (op, t, fr(), fr(), fr(), rm))
        for rm in rms:
            L.append('fsqrt.%s %s, %s%s' % (t, fr(), fr(), rm))
        for op in ['fmadd', 'fmsub', 'fnmsub', 'fnmadd']:
            for rm in rms[:3]:
                L.append('%s.%s %s, %s, %s, %s%s' % (op, t, fr(), fr(), fr(), fr(), rm))
        for op in ['fsgnj', 'fsgnjn', 'fsgnjx', 'fmin', 'fmax']:
            L.append('%s.%s %s, %s, %s' % (op, t, fr(), fr(), fr()))
        for op in ['feq', 'flt', 'fle', 'fgt', 'fge']:
            L.append('%s.%s %s, %s, %s' % (op, t, xr(), fr(), fr()))
        L.append('fclass.%s %s, %s' % (t, xr(), fr()))
        for op in ['fmv', 'fabs', 'fneg']:
            L.append('%s.%s %s, %s' % (op, t, fr(), fr()))
    for rm in rms:
        L.append('fcvt.s.d %s, %s%s' % (fr(), fr(), rm))
    L.append('fcvt.d.s %s, %s' % (fr(), fr()))
    for t in 'sd':
        for i in ['w', 'wu'] + (['l', 'lu'] if rv64 else []):
            for rm in rms:
                L.append('fcvt.%s.%s %s, %s%s' % (i, t, xr(), fr(), rm))
            if t == 's':
                for rm in rms:
                    L.append('fcvt.s.%s %s, %s%s' % (i, fr(), xr(), rm))
            else:
                if i in ('w', 'wu'):
                    L.append('fcvt.d.%s %s, %s' % (i, fr(), xr()))
                else:
                    for rm in rms:
                        L.append('fcvt.d.%s %s, %s%s' % (i, fr(), xr(), rm))
    L.append('fmv.x.w %s, %s' % (xr(), fr()))
    L.append('fmv.w.x %s, %s' % (fr(), xr()))
    if rv64:
        L.append('fmv.x.d %s, %s' % (xr(), fr()))
        L.append('fmv.d.x %s, %s' % (fr(), xr()))
    return L

def lis():
    L = []
    for v in [0, 1, -1, 31, -32, 32, -33, 2047, -2048, 2048, -2049, 4095, 4096, 0x12345, 0x7fffffff, -0x80000000, 0x800, 0x7ff, 0xfff, 0x1000, 0xfffff000, 0x80000000, 0x12345678, -0x12345678, 0x7ffff800, 0x7ffff7ff, 0xffffffff]:
        L.append('li %s, %d' % (xr(), v))
        L.append('li a0, %d' % v)
        L.append('li s1, 0x%x' % (v & 0xffffffff) if v >= 0 else 'li s1, %d' % v)
    return L

def build(rv64):
    return rtype(rv64) + itype(rv64) + mem(rv64) + branches() + system(rv64) + atomics(rv64) + fp(rv64) + lis()

def zext(rv64):
    L = []
    X = ['a0', 'a1', 'x5', 's11', 'zero', 't6', 'sp', 'ra']
    Fr = ['fa0', 'ft1', 'fs11', 'f31', 'f0']
    def r3(n):
        for i, a in enumerate(X):
            L.append('%s %s, %s, %s' % (n, a, X[(i + 1) % 8], X[(i + 3) % 8]))
    def r2(n):
        for i, a in enumerate(X):
            L.append('%s %s, %s' % (n, a, X[(i + 2) % 8]))
    for n in ['pack', 'packh', 'xperm4', 'xperm8']: r3(n)
    r2('brev8')
    if rv64:
        r3('packw')
        for n in ['aes64ds', 'aes64dsm', 'aes64es', 'aes64esm', 'aes64ks2']: r3(n)
        r2('aes64im')
        for k in range(11): L.append('aes64ks1i a0, a1, %d' % k)
        for n in ['sha512sig0', 'sha512sig1', 'sha512sum0', 'sha512sum1']: r2(n)
    else:
        r2('zip'); r2('unzip')
    for n in ['sha256sig0', 'sha256sig1', 'sha256sum0', 'sha256sum1', 'sm3p0', 'sm3p1']: r2(n)
    for n in ['sm4ed', 'sm4ks']:
        for b in range(4): L.append('%s a0, a1, a2, %d' % (n, b)); L.append('%s t6, s11, x5, %d' % (n, b))
    for n in ['cbo.clean', 'cbo.flush', 'cbo.inval', 'cbo.zero']:
        for r in ['a0', 'x5', 's11', 'sp']: L.append('%s (%s)' % (n, r))
    for n in ['prefetch.i', 'prefetch.r', 'prefetch.w']:
        for o in [0, 32, -32, 2016, -2048]: L.append('%s %d(a0)' % (n, o))
    for n in ['sinval.vma', 'hinval.vvma', 'hinval.gvma', 'hfence.vvma', 'hfence.gvma']:
        L += ['%s a0, a1' % n, '%s zero, zero' % n, '%s t6, x5' % n]
    L += ['sfence.w.inval', 'sfence.inval.ir', 'wrs.nto', 'wrs.sto', 'ntl.p1', 'ntl.pall', 'ntl.s1', 'ntl.all']
    for n in ['hlv.b', 'hlv.bu', 'hlv.h', 'hlv.hu', 'hlvx.hu', 'hlv.w', 'hlvx.wu'] + (['hlv.wu', 'hlv.d'] if rv64 else []):
        L += ['%s a0, (a1)' % n, '%s t6, (s11)' % n]
    for n in ['hsv.b', 'hsv.h', 'hsv.w'] + (['hsv.d'] if rv64 else []):
        L += ['%s a0, (a1)' % n, '%s t6, (s11)' % n]
    for n in ['fadd', 'fsub', 'fmul', 'fdiv']:
        for rm in ['', ', rne', ', rtz', ', rdn', ', rup', ', rmm', ', dyn']:
            L.append('%s.h fa0, fa1, fa2%s' % (n, rm))
    for rm in ['', ', rtz']:
        L.append('fsqrt.h fa0, fa1%s' % rm)
        for n in ['fmadd', 'fmsub', 'fnmsub', 'fnmadd']: L.append('%s.h fa0, fa1, fa2, fa3%s' % (n, rm))
    for n in ['fsgnj', 'fsgnjn', 'fsgnjx', 'fmin', 'fmax']: L.append('%s.h fa0, fa1, fa2' % n)
    for n in ['feq', 'flt', 'fle']: L += ['%s.h a0, fa1, fa2' % n, '%s.h t6, f31, f0' % n]
    L += ['fclass.h a0, fa1', 'fmv.x.h a0, fa1', 'fmv.h.x fa0, a1', 'flh fa0, 4(a0)', 'fsh fa0, -4(a0)', 'flh fa0, 2047(sp)', 'fsh f31, -2048(s11)']
    L += ['fmv.h fa0, fa1', 'fabs.h fa0, fa1', 'fneg.h fa0, fa1']
    L += ['fcvt.s.h fa0, fa1', 'fcvt.h.s fa0, fa1', 'fcvt.h.s fa0, fa1, rtz', 'fcvt.d.h fa0, fa1', 'fcvt.h.d fa0, fa1', 'fcvt.h.d fa0, fa1, rdn']
    for n in ['fcvt.w.h', 'fcvt.wu.h'] + (['fcvt.l.h', 'fcvt.lu.h'] if rv64 else []):
        L += ['%s a0, fa1' % n, '%s a0, fa1, rtz' % n]
    for n in ['fcvt.h.w', 'fcvt.h.wu'] + (['fcvt.h.l', 'fcvt.h.lu'] if rv64 else []):
        L += ['%s fa0, a1' % n, '%s fa0, a1, rup' % n]
    return L

def cexpl(rv64):
    L = []
    for r in ['a0', 'a5', 's1', 'x8', 'x15', 't1', 'sp', 'ra', 'x31']:
        L += ['c.addi %s, 4' % r, 'c.addi %s, -32' % r, 'c.addi %s, 31' % r, 'c.li %s, 4' % r, 'c.li %s, -32' % r, 'c.lui %s, 4' % r, 'c.lui %s, -32' % r,
              'c.mv %s, a1' % r, 'c.add %s, a1' % r, 'c.slli %s, 3' % r, 'c.jr %s' % r, 'c.jalr %s' % r]
    for r in ['a0', 'a5', 's1', 'x8', 'x15']:
        for s in ['a1', 'a4', 's0', 'x9']:
            L += ['c.and %s, %s' % (r, s), 'c.or %s, %s' % (r, s), 'c.xor %s, %s' % (r, s), 'c.sub %s, %s' % (r, s)]
            L += ['c.lw %s, 4(%s)' % (r, s), 'c.sw %s, 8(%s)' % (r, s), 'c.fld fa0, 8(%s)' % s, 'c.fsd fa5, 16(%s)' % s]
            if rv64: L += ['c.ld %s, 8(%s)' % (r, s), 'c.sd %s, 16(%s)' % (r, s), 'c.addw %s, %s' % (r, s), 'c.subw %s, %s' % (r, s)]
            else: L += ['c.flw fa0, 4(%s)' % s, 'c.fsw fa5, 8(%s)' % s]
        L += ['c.andi %s, 3' % r, 'c.andi %s, -32' % r, 'c.srli %s, 3' % r, 'c.srai %s, 5' % r, 'c.beqz %s, 8' % r, 'c.bnez %s, -8' % r, 'c.beqz %s, 254' % r, 'c.bnez %s, -256' % r]
        L += ['c.addi4spn %s, sp, 16' % r, 'c.addi4spn %s, sp, 1020' % r]
    for off in [0, 4, 252]: L += ['c.lwsp a0, %d(sp)' % off, 'c.swsp a1, %d(sp)' % off]
    for off in [0, 8, 504]:
        L += ['c.fldsp fa0, %d(sp)' % off, 'c.fsdsp fa1, %d(sp)' % off]
        if rv64: L += ['c.ldsp a0, %d(sp)' % off, 'c.sdsp a1, %d(sp)' % off]
    if rv64: L += ['c.addiw a0, 3', 'c.addiw s1, -32']
    else: L += ['c.jal 8', 'c.jal -8']
    L += ['c.addi16sp sp, 16', 'c.addi16sp sp, -512', 'c.addi16sp sp, 496', 'c.j 8', 'c.j -8', 'c.j 2046', 'c.j -2048', 'c.nop', 'c.ebreak']
    return L
