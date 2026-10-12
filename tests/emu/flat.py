from unicorn import *
import subprocess, os, tempfile, struct

CLY = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'cly')

def build(src, cpu, extra=()):
    d = tempfile.mkdtemp()
    p = os.path.join(d, 't.cly')
    o = os.path.join(d, 't.bin')
    open(p, 'w').write(src)
    r = subprocess.run([CLY, 'linux', p, '-flat', '-q', '-w', '-out', o] + list(extra), capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr)
    return open(o, 'rb').read()

def run(arch, code, base=0x10000, steps=100000, setup=None):
    if arch == 'a64':
        from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_SP, UC_ARM64_REG_CPACR_EL1
        mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        regs = [UC_ARM64_REG_X0 + i for i in range(29)]
        mu.mem_map(base, 0x100000)
        mu.mem_map(0x7ff00000, 0x100000)
        mu.reg_write(UC_ARM64_REG_SP, 0x7ff80000)
        mu.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)
        mu.mem_write(base, code)
        end = base + len(code)
    else:
        from unicorn import riscv_const as rc
        mu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV64 if arch == 'rv64' else UC_MODE_RISCV32)
        regs = [rc.UC_RISCV_REG_X0 + i for i in range(32)]
        mu.mem_map(base, 0x100000)
        mu.mem_map(0x7ff00000, 0x100000)
        mu.reg_write(rc.UC_RISCV_REG_SP, 0x7ff80000)
        mu.mem_write(base, code)
        end = base + len(code)
    if setup: setup(mu)
    try:
        mu.emu_start(base, end, count=steps)
    except UcError as e:
        return None, str(e), mu
    return [mu.reg_read(r) for r in regs], None, mu
