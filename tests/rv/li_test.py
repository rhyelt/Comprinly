import sys, random
sys.path.insert(0, 'tests/emu')
import flat

R = random.Random(99)

def vals(rv64):
    v = [0, 1, -1, 2047, -2048, 2048, -2049, 0x7ff, 0x800, 0xfff, 0x1000, 0x7fffffff, -0x80000000, 0x80000000, 0xffffffff, 0x12345678, 0xdeadbeef, 0x7ffff800, 0x7ffff7ff]
    if rv64:
        v += [0x100000000, 0x7fffffffffffffff, -0x8000000000000000, 0x8000000000000000 - 1, 0x123456789abcdef0, 0xffffffff00000000, 0xfffffffff0000000, 0x00000000fffff000, 0x0000ffff00000000, 1 << 40, 1 << 62, (1 << 63), ~(1 << 40), ~(1 << 62), (1 << 32) - 1, (1 << 33) - 1, (1 << 48) - 1, 0xffff0000ffff0000, 0x00ff00ff00ff00ff, 0x7fffffff00000000, 0x800, 0x800000000, 0xfff00000, 0x1fffff000, -0x1000, 0xffffffffffff, 0x1234000000000000]
        for _ in range(300):
            k = R.choice([8, 16, 24, 32, 40, 48, 63, 64])
            x = R.getrandbits(k)
            if R.random() < 0.3: x <<= R.randint(0, 20)
            if R.random() < 0.3: x = -x
            x = ((x + (1 << 63)) % (1 << 64)) - (1 << 63)
            v.append(x)
    else:
        for _ in range(100):
            v.append(R.getrandbits(R.choice([8, 12, 16, 20, 32])))
        v += [-x for x in v[-20:]]
    return v

def norm(x, bits):
    return x & ((1 << bits) - 1)

def sim(arch, code):
    if opts_c: pass
    M = (1 << 64) - 1 if arch == 'rv64' else (1 << 32) - 1
    bits = 64 if arch == 'rv64' else 32
    r = [0] * 32
    def sx(v, b): return (v & ((1 << b) - 1)) - ((v & (1 << (b - 1))) << 1)
    for i in range(0, len(code) - 3, 4):
        w = int.from_bytes(code[i:i + 4], 'little')
        op = w & 0x7f; rd = (w >> 7) & 31; f3 = (w >> 12) & 7; rs1 = (w >> 15) & 31; imm = sx(w >> 20, 12)
        if w == 0x13: continue
        if op == 0x37: r[rd] = sx(w & 0xfffff000, 32) & M
        elif op == 0x13 and f3 == 0: r[rd] = (r[rs1] + imm) & M
        elif op == 0x13 and f3 == 1 and (w >> 26) == 0: r[rd] = (r[rs1] << ((w >> 20) & 63)) & M
        elif op == 0x13 and f3 == 1 and (w >> 26) == 0x0a: r[rd] = r[rs1] | (1 << ((w >> 20) & 63))
        elif op == 0x13 and f3 == 1 and (w >> 26) == 0x12: r[rd] = r[rs1] & ~(1 << ((w >> 20) & 63)) & M
        elif op == 0x13 and f3 == 5 and (w >> 26) == 0: r[rd] = (r[rs1] >> ((w >> 20) & 63))
        elif op == 0x1b and f3 == 0: r[rd] = sx((r[rs1] + imm) & 0xffffffff, 32) & M
        elif op == 0x1b and f3 == 1 and (w >> 25) == 4: r[rd] = ((r[rs1] & 0xffffffff) << ((w >> 20) & 63)) & M
        elif op == 0x3b and f3 == 0 and (w >> 25) == 4: r[rd] = (r[rs1] & 0xffffffff) + r[(w >> 20) & 31] & M
        else: raise RuntimeError('sim: unknown %08x' % w)
        r[0] = 0
    return r, None, None

opts_c = 0
bad = 0
n = 0
for arch in ['rv64', 'rv32']:
    for opts in [(), ('-O0',)]:
        for cpu in [arch + 'imafdc', arch + 'imafd', arch + 'imafd_zba_zbs']:
            vs = vals(arch == 'rv64')
            bits = 64 if arch == 'rv64' else 32
            for i in range(0, len(vs), 25):
                chunk = vs[i:i + 25]
                src = 'cpu %s\n' % cpu
                for j, x in enumerate(chunk):
                    xx = x
                    if bits == 32: xx = (x & 0xffffffff)
                    if bits == 64 and xx >= (1 << 63): xx -= (1 << 64)
                    if bits == 32 and xx >= (1 << 31): xx -= (1 << 32)
                    src += 'li x%d, %d\n' % (5 + j, xx)
                src += 'nop\n'
                code = flat.build(src, cpu, opts)
                regs, e, mu = (sim(arch, code) if 'zb' in cpu else flat.run(arch, code))
                if e: print('EMU ERR', e, arch, cpu, opts, i); open('/tmp/li_fail.cly','w').write(src); bad += 1; continue
                for j, x in enumerate(chunk):
                    n += 1
                    if regs[5 + j] != norm(x, bits):
                        bad += 1
                        print('BAD', arch, cpu, opts, hex(x), hex(regs[5 + j]))
print('li tests', n, 'bad', bad)
