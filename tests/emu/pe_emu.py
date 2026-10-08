import sys, struct, lief
from unicorn import *
from unicorn.x86_const import *
pe = lief.parse(sys.argv[1])
mu = Uc(UC_ARCH_X86, UC_MODE_32)
base = pe.optional_header.imagebase
size = (pe.optional_header.sizeof_image + 0xfff) & ~0xfff
mu.mem_map(base, size)
mu.mem_write(base, bytes(pe.get_content_from_virtual_address(0, pe.sizeof_headers) if False else b'\0'))
for s in pe.sections:
    data = bytes(s.content)
    mu.mem_write(base + s.virtual_address, data)
mu.mem_map(0x200000, 0x100000)
mu.mem_map(0x70000000, 0x1000)
fake = {}
out = bytearray()
for imp in pe.imports:
    for e in imp.entries:
        addr = 0x70000000 + 16 * len(fake)
        fake[addr] = e.name
        mu.mem_write(base + e.iat_address - (0 if e.iat_address < base else base) if False else (e.iat_address if e.iat_address >= base else base + e.iat_address), struct.pack('<I', addr))
done = []
def code(uc, addr, sz, ud):
    if addr in fake:
        nm = fake[addr]
        esp = uc.reg_read(UC_X86_REG_ESP)
        rd = lambda i: struct.unpack('<I', uc.mem_read(esp + 4 * i, 4))[0]
        ret = rd(0)
        if nm == 'GetStdHandle':
            uc.reg_write(UC_X86_REG_EAX, 100 + (rd(1) & 0xff)); uc.reg_write(UC_X86_REG_ESP, esp + 8)
        elif nm == 'WriteFile':
            buf, ln, pw = rd(2), rd(3), rd(4)
            out.extend(uc.mem_read(buf, ln)); uc.mem_write(pw, struct.pack('<I', ln))
            uc.reg_write(UC_X86_REG_EAX, 1); uc.reg_write(UC_X86_REG_ESP, esp + 24)
        elif nm == 'ExitProcess':
            done.append(rd(1)); uc.emu_stop(); return
        else:
            print('unhandled', nm); uc.emu_stop(); return
        uc.reg_write(UC_X86_REG_EIP, ret)
mu.hook_add(UC_HOOK_CODE, code, None, 0x70000000, 0x70001000)
mu.reg_write(UC_X86_REG_ESP, 0x2f0000)
try:
    mu.emu_start(base + pe.optional_header.addressof_entrypoint, 0, count=1000000)
except UcError as e:
    print('uc error', e, hex(mu.reg_read(UC_X86_REG_EIP)))
print('stdout:', bytes(out), 'exit', done)
