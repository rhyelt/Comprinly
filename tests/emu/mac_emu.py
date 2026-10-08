import sys, struct
from unicorn import *
from unicorn.x86_const import *
d = open(sys.argv[1], 'rb').read()
magic, cpu, sub, ft, ncmds, sz, fl = struct.unpack_from('<7I', d, 0)
off = 28
mu = Uc(UC_ARCH_X86, UC_MODE_32)
entry = None
for i in range(ncmds):
    cmd, cs = struct.unpack_from('<2I', d, off)
    if cmd == 1:
        name = d[off + 8:off + 24].rstrip(b'\0')
        vmaddr, vmsize, fo, fs, mp, ip, ns, fl2 = struct.unpack_from('<8I', d, off + 24)
        if vmsize and name != b'__PAGEZERO':
            mu.mem_map(vmaddr, vmsize)
            mu.mem_write(vmaddr, d[fo:fo + fs])
    elif cmd == 5:
        regs = struct.unpack_from('<16I', d, off + 16)
        entry = regs[10]
    off += cs
mu.mem_map(0xB0000000, 0x100000)
out = bytearray(); done = []
def intr(uc, n, ud):
    if n != 0x80: return
    esp = uc.reg_read(UC_X86_REG_ESP)
    eax = uc.reg_read(UC_X86_REG_EAX)
    a = [struct.unpack('<I', uc.mem_read(esp + 4 + 4 * i, 4))[0] for i in range(7)]
    fl = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
    if eax == 4:
        out.extend(uc.mem_read(a[1], a[2])); uc.reg_write(UC_X86_REG_EAX, a[2])
    elif eax == 1:
        done.append(a[0]); uc.emu_stop()
    elif eax == 197:
        uc.reg_write(UC_X86_REG_EAX, 0x30000000)
    else:
        print('syscall', eax, a)
        uc.reg_write(UC_X86_REG_EAX, 0)
    uc.reg_write(UC_X86_REG_EFLAGS, fl)
mu.hook_add(UC_HOOK_INTR, intr)
mu.reg_write(UC_X86_REG_ESP, 0xB00F0000)
try:
    mu.emu_start(entry, 0, count=1000000)
except UcError as e:
    print('uc error', e, hex(mu.reg_read(UC_X86_REG_EIP)))
print('stdout:', bytes(out), 'exit', done)
