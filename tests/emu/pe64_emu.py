import sys, struct, lief
from unicorn import *
from unicorn.x86_const import *
pe = lief.parse(sys.argv[1])
stdin = bytearray(sys.argv[2].encode()) if len(sys.argv) > 2 else bytearray()
mu = Uc(UC_ARCH_X86, UC_MODE_64)
base = pe.optional_header.imagebase
size = (pe.optional_header.sizeof_image + 0xfff) & ~0xfff
mu.mem_map(base, size)
for s in pe.sections:
    mu.mem_write(base + s.virtual_address, bytes(s.content))
mu.mem_map(0x200000, 0x100000)
mu.mem_map(0x70000000, 0x1000)
mu.mem_map(0x80000000, 0x1000000)
fake = {}
out = bytearray()
for imp in pe.imports:
    for e in imp.entries:
        addr = 0x70000000 + 16 * len(fake)
        fake[addr] = e.name
        mu.mem_write(base + e.iat_address if e.iat_address < base else e.iat_address, struct.pack('<Q', addr))
done = []
valloc = [0x80000000]
R = lambda r: mu.reg_read(r)
def code(uc, addr, sz, ud):
    if addr not in fake:
        return
    nm = fake[addr]
    rsp = R(UC_X86_REG_RSP)
    ret = struct.unpack('<Q', uc.mem_read(rsp, 8))[0]
    rcx, rdx, r8, r9 = R(UC_X86_REG_RCX), R(UC_X86_REG_RDX), R(UC_X86_REG_R8), R(UC_X86_REG_R9)
    if (rsp + 8) % 16 != 0:
        print('misaligned stack at', nm, hex(rsp)); uc.emu_stop(); return
    rv = 1
    if nm == 'GetStdHandle':
        rv = 100 + ((rcx & 0xffffffff) - 0xfffffff6 if False else (0xffffffff - (rcx & 0xffffffff) - 9) & 0xff)
    elif nm == 'WriteFile':
        out.extend(uc.mem_read(rdx, r8)); uc.mem_write(r9, struct.pack('<I', r8)); rv = 1
    elif nm == 'ReadFile':
        n = min(r8, len(stdin)); uc.mem_write(rdx, bytes(stdin[:n])); del stdin[:n]; uc.mem_write(r9, struct.pack('<I', n)); rv = 1
    elif nm == 'ExitProcess':
        done.append(rcx & 0xffffffff); uc.emu_stop(); return
    elif nm == 'VirtualAlloc':
        rv = valloc[0]; valloc[0] += (rdx + 0xfff) & ~0xfff
    elif nm == 'GetCurrentProcessId':
        rv = 1234
    elif nm == 'GetSystemTimeAsFileTime':
        uc.mem_write(rcx, struct.pack('<Q', 116444736000000000 + 1700000000 * 10000000 + 5000000)); rv = 0
    else:
        print('unhandled', nm); uc.emu_stop(); return
    uc.reg_write(UC_X86_REG_RAX, rv)
    uc.reg_write(UC_X86_REG_RSP, rsp + 8)
    uc.reg_write(UC_X86_REG_RIP, ret)
mu.hook_add(UC_HOOK_CODE, code, None, 0x70000000, 0x70001000)
mu.reg_write(UC_X86_REG_RSP, 0x2F0008)
try:
    mu.emu_start(base + pe.optional_header.addressof_entrypoint, 0, count=2000000)
except UcError as e:
    print('uc error', e, hex(R(UC_X86_REG_RIP)))
print('stdout:', bytes(out), 'exit', done)
