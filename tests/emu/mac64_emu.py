import sys, struct
from unicorn import *
from unicorn.x86_const import *
d = open(sys.argv[1], 'rb').read()
magic, cpu, sub, ft, ncmds, sz, fl, _ = struct.unpack_from('<8I', d, 0)
off = 32
mu = Uc(UC_ARCH_X86, UC_MODE_64)
entry = None
for i in range(ncmds):
    cmd, cs = struct.unpack_from('<2I', d, off)
    if cmd == 0x19:
        name = d[off + 8:off + 24].rstrip(b'\0')
        vmaddr, vmsize, fo, fs = struct.unpack_from('<4Q', d, off + 24)
        if vmsize and name != b'__PAGEZERO':
            mu.mem_map(vmaddr, (vmsize + 4095) & ~4095)
            mu.mem_write(vmaddr, d[fo:fo + fs])
    elif cmd == 5:
        regs = struct.unpack_from('<21Q', d, off + 16)
        entry = regs[16]
    off += cs
mu.mem_map(0x7ff000000000, 0x100000)
out = bytearray(); done = []
def sc(uc, ud):
    n = uc.reg_read(UC_X86_REG_RAX)
    rdi = uc.reg_read(UC_X86_REG_RDI); rsi = uc.reg_read(UC_X86_REG_RSI); rdx = uc.reg_read(UC_X86_REG_RDX)
    if n == 0x2000004:
        out.extend(uc.mem_read(rsi, rdx)); uc.reg_write(UC_X86_REG_RAX, rdx)
    elif n == 0x2000001:
        done.append(rdi); uc.emu_stop()
    else:
        print('syscall', hex(n))
        uc.reg_write(UC_X86_REG_RAX, 0)
mu.hook_add(UC_HOOK_INSN, sc, None, 1, 0, UC_X86_INS_SYSCALL)
mu.reg_write(UC_X86_REG_RSP, 0x7ff0000f0000)
try:
    mu.emu_start(entry, 0, count=1000000)
except UcError as e:
    print('uc error', e, hex(mu.reg_read(UC_X86_REG_RIP)))
print('stdout:', bytes(out), 'exit', done)
