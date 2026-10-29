import sys, struct
from unicorn import *
from unicorn.x86_const import UC_X86_INS_SYSCALL

def parse_elf(d):
    if d[:4] != b'\x7fELF':
        raise SystemExit('not elf')
    is64 = d[4] == 2
    etype, mach = struct.unpack_from('<HH', d, 16)
    if is64:
        entry, phoff = struct.unpack_from('<QQ', d, 24)
        phentsize, phnum = struct.unpack_from('<HH', d, 54)
    else:
        entry, phoff = struct.unpack_from('<II', d, 24)
        phentsize, phnum = struct.unpack_from('<HH', d, 42)
    segs = []
    dynamic = None
    for i in range(phnum):
        o = phoff + i * phentsize
        if is64:
            t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', d, o)
        else:
            t, off, va, pa, fs, ms, fl, al = struct.unpack_from('<IIIIIIII', d, o)
        if t == 1: segs.append((va, off, fs, ms))
        if t == 2: dynamic = (va, ms)
    return dict(d=d, is64=is64, etype=etype, mach=mach, entry=entry, segs=segs, dynamic=dynamic, bias=0)

def read_dyn(mu, m):
    ptr = 8 if m['is64'] else 4
    fmt = '<QQ' if m['is64'] else '<II'
    va, sz = m['dynamic']
    raw = bytes(mu.mem_read(m['bias'] + va, sz))
    tags = {}
    multi = {}
    for i in range(0, len(raw) - 2 * ptr + 1, 2 * ptr):
        t, v = struct.unpack_from(fmt, raw, i)
        if t == 0: break
        tags.setdefault(t, v)
        multi.setdefault(t, []).append(v)
    return tags, multi

def run(path, stdin=b'', limit=2000000, trace=False, libs=()):
    mods = [parse_elf(open(path, 'rb').read())] + [parse_elf(open(p, 'rb').read()) for p in libs]
    main = mods[0]
    is64 = main['is64']
    mach = main['mach']
    if mach == 183:
        from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X8, UC_ARM64_REG_SP, UC_ARM64_REG_X30
        mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        R = [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2]
        NR = UC_ARM64_REG_X8
        SP = UC_ARM64_REG_SP
        LR = UC_ARM64_REG_X30
        from unicorn.arm64_const import UC_ARM64_REG_CPACR_EL1
    elif mach == 243:
        from unicorn.riscv_const import UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2, UC_RISCV_REG_A7, UC_RISCV_REG_SP, UC_RISCV_REG_RA
        mu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV64 if is64 else UC_MODE_RISCV32)
        R = [UC_RISCV_REG_A0, UC_RISCV_REG_A1, UC_RISCV_REG_A2]
        NR = UC_RISCV_REG_A7
        SP = UC_RISCV_REG_SP
        LR = UC_RISCV_REG_RA
    elif mach == 62:
        from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RSP
        mu = Uc(UC_ARCH_X86, UC_MODE_64)
        R = [UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX]
        NR = UC_X86_REG_RAX
        SP = UC_X86_REG_RSP
        LR = None
    else:
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
        mu = Uc(UC_ARCH_X86, UC_MODE_32)
        R = [UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX]
        NR = UC_X86_REG_EAX
        SP = UC_X86_REG_ESP
        LR = None
    nxt = 0x20000000
    for k, m in enumerate(mods):
        if m['etype'] == 3:
            m['bias'] = nxt
            nxt += 0x1000000
        for va, off, fs, ms in m['segs']:
            lo = (m['bias'] + va) & ~0xfff
            hi = (m['bias'] + va + ms + 0xfff) & ~0xfff
            try: mu.mem_map(lo, hi - lo)
            except UcError: pass
            mu.mem_write(m['bias'] + va, m['d'][off:off + fs])
    ptr = 8 if is64 else 4
    mask = (1 << (8 * ptr)) - 1
    if any(m['dynamic'] for m in mods):
        symtabs = []
        for m in mods:
            m['syms'] = {}
            m['deflist'] = []
            if not m['dynamic']:
                continue
            tags, multi = read_dyn(mu, m)
            m['tags'] = tags
            m['multi'] = multi
            strtab = m['bias'] + tags[5]
            symtab = m['bias'] + tags[6]
            nsyms = struct.unpack_from('<I', bytes(mu.mem_read(m['bias'] + tags[4] + 4, 4)))[0]
            m['symtab'] = symtab
            m['strtab'] = strtab
            esz = 24 if is64 else 16
            names = []
            for i in range(nsyms):
                raw = bytes(mu.mem_read(symtab + i * esz, esz))
                if is64:
                    nm, info, other, shn, val, size = struct.unpack('<IBBHQQ', raw)
                else:
                    nm, val, size, info, other, shn = struct.unpack('<IIIBBH', raw)
                s = b''
                a = strtab + nm
                while True:
                    c = bytes(mu.mem_read(a, 1))
                    if c == b'\0': break
                    s += c
                    a += 1
                names.append(s.decode())
                if shn != 0 and i:
                    m['syms'][s.decode()] = (m['bias'] + val if shn != 0xfff1 else val, size, info >> 4, info & 15, other & 3)
            m['names'] = names
        def lookup(name, skip_main=False):
            for k, m in enumerate(mods):
                if skip_main and k == 0: continue
                if name in m['syms']: return m['syms'][name]
            return None
        for m in mods:
            if not m['dynamic']: continue
            tags = m['tags']
            B = m['bias']
            esz = 24 if is64 else 16
            def sym_name(i):
                return m['names'][i]
            tables = []
            if 7 in tags: tables.append((B + tags[7], tags[8], tags[9] if 9 in tags else 24, True))
            if 17 in tags: tables.append((B + tags[17], tags[18], tags[19] if 19 in tags else 8, False))
            if 23 in tags: tables.append((B + tags[23], tags[2], 24 if is64 else 8, mach in (62, 183, 243)))
            for addr, size, ent, rela in tables:
                raw = bytes(mu.mem_read(addr, size))
                for i in range(0, size - ent + 1, ent):
                    if is64:
                        off, info = struct.unpack_from('<QQ', raw, i)
                        add = struct.unpack_from('<q', raw, i + 16)[0] if rela else 0
                        sym, typ = info >> 32, info & 0xffffffff
                    else:
                        off, info = struct.unpack_from('<II', raw, i)
                        add = struct.unpack_from('<i', raw, i + 8)[0] if rela else 0
                        sym, typ = info >> 8, info & 255
                    P = B + off
                    if not rela:
                        add = int.from_bytes(bytes(mu.mem_read(P, ptr)), 'little')
                        if add >= 1 << (8 * ptr - 1): add -= 1 << (8 * ptr)
                    S = 0
                    sz = 0
                    if sym:
                        nm = sym_name(sym)
                        r = lookup(nm, skip_main=(typ in (5, 1024, 4) and (mach, typ) in ((62, 5), (3, 5), (183, 1024), (243, 4))))
                        if r is None:
                            S = 0
                        else:
                            S = r[0]
                            sz = r[1]
                    rel = {62: 8, 3: 8, 183: 1027, 243: 3}[mach]
                    glob = {62: (6, 7), 3: (6, 7), 183: (1025, 1026), 243: (2, 5)}[mach]
                    cpy = {62: 5, 3: 5, 183: 1024, 243: 4}[mach]
                    ab = {62: 1, 3: 1, 183: 257, 243: 2}[mach]
                    if typ == rel: val = (B + add) & mask
                    elif typ in glob: val = S & mask
                    elif typ == ab: val = (S + add) & mask
                    elif typ == cpy:
                        mu.mem_write(P, bytes(mu.mem_read(S, sz)))
                        continue
                    elif typ == 0: continue
                    else:
                        raise SystemExit('unsupported dynamic reloc type %d in %s' % (typ, path))
                    mu.mem_write(P, val.to_bytes(ptr, 'little'))
    stack = 0x7ff00000
    mu.mem_map(stack - 0x100000, 0x100000)
    mu.reg_write(SP, stack - 0x1000)
    heap = 0x60000000
    mu.mem_map(heap, 0x1000000)
    halt = 0x7f000000
    mu.mem_map(halt, 0x1000)
    state = dict(brk=heap, out=bytearray(), code=None, inp=bytearray(stdin), n=0)
    if mach == 183:
        mu.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)

    def syscall(uc):
        nr = uc.reg_read(NR)
        a0, a1, a2 = [uc.reg_read(r) for r in R]
        if mach in (183, 243):
            WR, RD, EX, BRK = 64, 63, (93, 94), 214
        elif mach == 62:
            WR, RD, EX, BRK = 1, 0, (60, 231), 12
        else:
            WR, RD, EX, BRK = 4, 3, (1, 252), 45
        if nr == WR:
            if a0 in (1, 2):
                state['out'] += bytes(uc.mem_read(a1, a2))
            uc.reg_write(NR if mach in (62, 3) else R[0], a2)
        elif nr == RD:
            n = min(a2, len(state['inp']))
            uc.mem_write(a1, bytes(state['inp'][:n]))
            del state['inp'][:n]
            uc.reg_write(NR if mach in (62, 3) else R[0], n)
        elif nr in EX:
            state['code'] = a0 & 0xff
            uc.emu_stop()
        elif nr == BRK:
            if a0 == 0: uc.reg_write(NR if mach in (62, 3) else R[0], state['brk'])
            else:
                state['brk'] = a0
                uc.reg_write(NR if mach in (62, 3) else R[0], a0)
        else:
            uc.reg_write(NR if mach in (62, 3) else R[0], (-38) & mask)

    def intr(uc, no, ud):
        syscall(uc)
    mu.hook_add(UC_HOOK_INTR, intr)
    if mach == 62:
        def sysc(uc, ud):
            syscall(uc)
        mu.hook_add(UC_HOOK_INSN, sysc, None, 1, 0, UC_X86_INS_SYSCALL)
    def cnt(uc, a, s, ud):
        state['n'] += 1
        if state['n'] > limit:
            uc.emu_stop()
    mu.hook_add(UC_HOOK_CODE, cnt)

    def call(addr, args=()):
        sp = stack - 0x2000
        if LR is None:
            if is64:
                sp -= 8
                mu.mem_write(sp, halt.to_bytes(8, 'little'))
            else:
                sp -= 4
                mu.mem_write(sp, halt.to_bytes(4, 'little'))
        else:
            mu.reg_write(LR, halt)
        mu.reg_write(SP, sp)
        try:
            mu.emu_start(addr, halt, count=limit)
        except UcError as e:
            state['err'] = str(e)
        mu.reg_write(SP, stack - 0x1000)
    try:
        for m in reversed(mods[1:]):
            if not m['dynamic']: continue
            tags, multi = m['tags'], m['multi']
            if 12 in tags: call(m['bias'] + tags[12])
            if 25 in tags:
                n = tags.get(27, 0) // ptr
                for i in range(n):
                    fp = int.from_bytes(bytes(mu.mem_read(m['bias'] + tags[25] + i * ptr, ptr)), 'little')
                    call(fp)
        if mach == 3:
            sp = stack - 0x1000
            mu.reg_write(SP, sp)
        mu.emu_start(main['bias'] + main['entry'], 0xffffffffffff, count=limit * 2)
    except UcError as e:
        state['err'] = str(e)
    state['mu'] = mu
    state['mods'] = mods
    return state

if __name__ == '__main__':
    args = sys.argv[1:]
    libs = []
    rest = []
    i = 0
    while i < len(args):
        if args[i] == '-L' and i + 1 < len(args):
            libs.append(args[i + 1])
            i += 2
        else:
            rest.append(args[i])
            i += 1
    st = run(rest[0], rest[1].encode() if len(rest) > 1 else b'', libs=libs)
    sys.stdout.write(st['out'].decode('latin1'))
    if 'err' in st: sys.stderr.write('emu error: %s\n' % st['err'])
    sys.exit(st['code'] if st['code'] is not None else 99)
