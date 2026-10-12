import sys, random, subprocess, os, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'emu'))
import elf_emu

CLY = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'cly')
seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
count = int(sys.argv[2]) if len(sys.argv) > 2 else 300
R = random.Random(seed)
A_REGS = ['x%d' % i for i in list(range(0, 8)) + list(range(9, 16))]
V_REGS = ['a0', 'a1', 'a2', 'a3', 'a4', 'a5', 'a6', 't0', 't1', 't2', 't3', 's2', 's3']

def a64_body(nb):
    out = []
    regs = A_REGS
    for k in range(nb):
        out.append('L%d:' % k)
        for _ in range(R.randint(0, 6)):
            a, b, c = R.choice(regs), R.choice(regs), R.choice(regs)
            t = R.randint(0, 22)
            if t == 0: out.append(' mov %s, %s' % (a, b))
            elif t == 1: out.append(' mov %s, %s' % (a, a))
            elif t == 2: out.append(' mov %s, %d' % (a, R.randint(-300, 70000)))
            elif t == 3: out.append(' add %s, %s, %d' % (a, b, R.randint(0, 99)))
            elif t == 4: out.append(' add %s, %s, 0' % (a, a))
            elif t == 5: out.append(' eor %s, %s, %s' % (a, b, c))
            elif t == 6: out.append(' mul %s, %s, %s' % (a, b, c))
            elif t == 7:
                out.append(' cmp %s, 0' % a); out.append(' b.%s L%d' % (R.choice(['eq', 'ne']), R.randint(k + 1, nb)))
            elif t == 8:
                out.append(' cmp %s, %s' % (a, b)); out.append(' b.%s L%d' % (R.choice(['eq', 'ne', 'lt', 'hi', 'ge', 'ls']), R.randint(k + 1, nb)))
            elif t == 9: out.append(' cbz %s, L%d' % (a, R.randint(k + 1, nb)))
            elif t == 10: out.append(' cbnz %s, L%d' % (a, R.randint(k + 1, nb)))
            elif t == 11: out.append(' b L%d' % R.randint(k + 1, nb)); 
            elif t == 12:
                off = R.choice([8, 16, 24])
                out.append(' str %s, [sp, %d]' % (a, off)); out.append(' ldr %s, [sp, %d]' % (b, off))
            elif t == 13: out.append(' ldr %s, [sp, %d]' % (a, R.choice([8, 16, 24])))
            elif t == 14: out.append(' str %s, [sp, %d]' % (a, R.choice([8, 16, 24])))
            elif t == 15:
                out.append(' mov %s, %d' % (a, R.randint(0, 500))); out.append(' mov %s, %d' % (a, R.randint(0, 500)))
            elif t == 16:
                out.append(' cmp %s, 0' % a); out.append(' b.%s L%d' % (R.choice(['eq', 'ne']), R.randint(k + 1, nb))); out.append(' csel %s, %s, %s, %s' % (b, c, a, R.choice(['eq', 'ne'])))
            elif t == 17:
                out.append(' tst %s, %s' % (a, a)); out.append(' b.%s L%d' % (R.choice(['eq', 'ne']), R.randint(k + 1, nb)))
            elif t == 18:
                tgt = R.randint(k + 1, nb)
                out.append(' b.%s L%d' % (R.choice(['eq', 'ne']), tgt)); out.append(' b L%d' % R.randint(k + 1, nb)); out.append('L%d_x:' % k if False else '')
            elif t == 19: out.append(' tbz %s, %d, L%d' % (a, R.randint(0, 5), R.randint(k + 1, nb)))
            elif t == 20: out.append(' mov %s, %s' % (a, b)); out.append(' ldr %s, [sp, 8]' % a)
            elif t == 21: out.append(' sub %s, %s, %s' % (a, b, c))
            else: out.append(' lsl %s, %s, %d' % (a, b, R.randint(0, 5)))
    out.append('L%d:' % nb)
    return out

def rv_body(nb):
    out = []
    regs = V_REGS
    for k in range(nb):
        out.append('L%d:' % k)
        for _ in range(R.randint(0, 6)):
            a, b, c = R.choice(regs), R.choice(regs), R.choice(regs)
            t = R.randint(0, 20)
            tg = lambda: 'L%d' % R.randint(k + 1, nb)
            if t == 0: out.append(' mv %s, %s' % (a, b))
            elif t == 1: out.append(' mv %s, %s' % (a, a))
            elif t == 2: out.append(' li %s, %d' % (a, R.randint(-300, 70000)))
            elif t == 3: out.append(' addi %s, %s, %d' % (a, b, R.randint(-50, 99)))
            elif t == 4: out.append(' addi %s, %s, 0' % (a, a))
            elif t == 5: out.append(' xor %s, %s, %s' % (a, b, c))
            elif t == 6: out.append(' mul %s, %s, %s' % (a, b, c))
            elif t == 7: out.append(' %s %s, %s, %s' % (R.choice(['beq', 'bne', 'blt', 'bge', 'bltu', 'bgeu']), a, b, tg()))
            elif t == 8: out.append(' %s %s, %s' % (R.choice(['beqz', 'bnez', 'bltz', 'bgez', 'blez', 'bgtz']), a, tg()))
            elif t == 9: out.append(' j %s' % tg())
            elif t == 10:
                off = R.choice([8, 16, 24])
                out.append(' sd %s, %d(sp)' % (a, off)); out.append(' ld %s, %d(sp)' % (b, off))
            elif t == 11: out.append(' ld %s, %d(sp)' % (a, R.choice([8, 16, 24])))
            elif t == 12: out.append(' sd %s, %d(sp)' % (a, R.choice([8, 16, 24])))
            elif t == 13:
                out.append(' li %s, %d' % (a, R.randint(0, 500))); out.append(' li %s, %d' % (a, R.randint(0, 500)))
            elif t == 14: out.append(' %s %s, %s, %s' % (R.choice(['bgt', 'ble', 'bgtu', 'bleu']), a, b, tg()))
            elif t == 15:
                out.append(' %s %s, %s, %s' % (R.choice(['beq', 'bne']), a, b, tg())); out.append(' j %s' % tg())
            elif t == 16: out.append(' mv %s, %s' % (a, b)); out.append(' ld %s, 8(sp)' % a)
            elif t == 17: out.append(' sub %s, %s, %s' % (a, b, c))
            elif t == 18: out.append(' slli %s, %s, %d' % (a, b, R.randint(0, 5)))
            elif t == 19: out.append(' slt %s, %s, %s' % (a, b, c))
            else: out.append(' add %s, %s, %s' % (a, b, c))
    out.append('L%d:' % nb)
    return out

def build(arch):
    nb = R.randint(2, 7)
    if arch == 'a64':
        regs = A_REGS
        s = ['cpu arm64', 'start:', ' mov x29, sp']
        for i, r in enumerate(regs): s.append(' mov %s, %d' % (r, 100 + 7 * i))
        s += a64_body(nb)
        s.append(' adr x16, buf')
        for i, r in enumerate(regs): s.append(' str %s, [x16, %d]' % (r, 8 * i))
        s += [' mov x0, 1', ' mov x1, x16', ' mov x2, %d' % (8 * len(regs)), ' mov x8, 64', ' svc 0', ' mov x0, 0', ' mov x8, 93', ' svc 0']
    else:
        regs = V_REGS
        s = ['cpu rv64', 'start:']
        for i, r in enumerate(regs): s.append(' li %s, %d' % (r, 100 + 7 * i))
        s += rv_body(nb)
        s.append(' la s11, buf')
        for i, r in enumerate(regs): s.append(' sd %s, %d(s11)' % (r, 8 * i))
        s += [' li a0, 1', ' mv a1, s11', ' li a2, %d' % (8 * len(regs)), ' li a7, 64', ' ecall', ' li a0, 0', ' li a7, 93', ' ecall']
    s += ['section .bss', 'buf: resb 256']
    return '\n'.join(x for x in s if x != '') + '\n'

def run(src, extra):
    d = tempfile.mkdtemp()
    p = os.path.join(d, 't.cly'); o = os.path.join(d, 't')
    open(p, 'w').write(src)
    r = subprocess.run([CLY, 'linux', p, '-q', '-w', '-out', o] + extra, capture_output=True)
    if r.returncode: return None, r.stderr.decode('latin1')
    st = elf_emu.run(o, limit=20000)
    global SZ
    SZ = os.path.getsize(o)
    return (bytes(st['out']), st['code'], st.get('err')), None

SZ = 0
bad = 0
smaller = 0
tot0 = tot1 = 0
for n in range(count):
    arch = 'a64' if n % 2 == 0 else 'rv64'
    src = build(arch)
    a, e1 = run(src, ['-O0'])
    s0 = SZ
    b, e2 = run(src, [])
    tot0 += s0; tot1 += SZ
    if a is None or b is None:
        bad += 1
        print('BUILD FAIL', arch, e1 or e2); open('/tmp/nxfail_%d.cly' % bad, 'w').write(src); continue
    if a != b:
        bad += 1
        print('MISMATCH', arch); open('/tmp/nxfail_%d.cly' % bad, 'w').write(src)
print('nx opt fuzz: programs %d bad %d, bytes %d -> %d' % (count, bad, tot0, tot1))
sys.exit(1 if bad else 0)
