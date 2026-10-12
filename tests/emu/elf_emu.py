import sys, struct
from unicorn import *

def run(path, stdin=b'', limit=2000000, trace=False):
    d = open(path, 'rb').read()
    if d[:4] != b'\x7fELF':
        raise SystemExit('not elf')
    is64 = d[4] == 2
    mach = struct.unpack_from('<H', d, 18)[0]
    if is64:
        entry, phoff = struct.unpack_from('<QQ', d, 24)
        phentsize, phnum = struct.unpack_from('<HH', d, 54)
    else:
        entry, phoff = struct.unpack_from('<II', d, 24)[0], struct.unpack_from('<I', d, 28)[0]
        phentsize, phnum = struct.unpack_from('<HH', d, 42)
    if mach == 183:
        from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X8, UC_ARM64_REG_SP
        mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        R = [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2]
        NR = UC_ARM64_REG_X8
        SP = UC_ARM64_REG_SP
        sysh = 'intr'
        from unicorn.arm64_const import UC_ARM64_REG_CPACR_EL1
    else:
        from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A7, UC_RISCV_REG_SP
        mu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV64 if is64 else UC_MODE_RISCV32)
        R = [UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2]
        NR = UC_RISCV_REG_A7
        SP = UC_RISCV_REG_SP
    segs = []
    for i in range(phnum):
        o = phoff + i * phentsize
        if is64:
            t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', d, o)
        else:
            t, off, va, pa, fs, ms, fl, al = struct.unpack_from('<IIIIIIII', d, o)
        if t != 1: continue
        segs.append((va, off, fs, ms))
    for va, off, fs, ms in segs:
        lo = va & ~0xfff
        hi = (va + ms + 0xfff) & ~0xfff
        try: mu.mem_map(lo, hi - lo)
        except UcError: pass
        mu.mem_write(va, d[off:off + fs])
    stack = 0x7ff00000
    mu.mem_map(stack - 0x100000, 0x100000)
    mu.reg_write(SP, stack - 0x1000)
    heap = 0x60000000
    mu.mem_map(heap, 0x1000000)
    state = dict(brk=heap, out=bytearray(), code=None, inp=bytearray(stdin), n=0)
    if mach == 183:
        mu.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)

    def syscall(uc):
        nr = uc.reg_read(NR)
        a0, a1, a2 = [uc.reg_read(r) for r in R]
        if nr == 64:
            if a0 in (1, 2):
                state['out'] += bytes(uc.mem_read(a1, a2))
            uc.reg_write(R[0], a2)
        elif nr == 63:
            n = min(a2, len(state['inp']))
            uc.mem_write(a1, bytes(state['inp'][:n]))
            del state['inp'][:n]
            uc.reg_write(R[0], n)
        elif nr in (93, 94):
            state['code'] = a0 & 0xff
            uc.emu_stop()
        elif nr == 214:
            if a0 == 0: uc.reg_write(R[0], state['brk'])
            else:
                state['brk'] = a0
                uc.reg_write(R[0], a0)
        else:
            uc.reg_write(R[0], (-38) & 0xffffffffffffffff)

    if mach == 183:
        def intr(uc, no, ud):
            syscall(uc)
        mu.hook_add(UC_HOOK_INTR, intr)
    else:
        def intr(uc, no, ud):
            syscall(uc)
        mu.hook_add(UC_HOOK_INTR, intr)
    def cnt(uc, a, s, ud):
        state['n'] += 1
        if state['n'] > limit:
            uc.emu_stop()
    mu.hook_add(UC_HOOK_CODE, cnt)
    try:
        mu.emu_start(entry, 0xffffffffffff, count=limit * 2)
    except UcError as e:
        state['err'] = str(e)
    return state

if __name__ == '__main__':
    st = run(sys.argv[1], sys.argv[2].encode() if len(sys.argv) > 2 else b'')
    sys.stdout.write(st['out'].decode('latin1'))
    if 'err' in st: sys.stderr.write('emu error: %s\n' % st['err'])
    sys.exit(st['code'] if st['code'] is not None else 99)
