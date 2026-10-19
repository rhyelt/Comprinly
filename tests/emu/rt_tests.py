import sys, subprocess, os, tempfile
sys.path.insert(0, os.path.dirname(__file__))
import elf_emu

A64 = 'cpu arm64\nsection .text\nglobal start\nstart:\n'
A64X = '    mov x8, #93\n    svc #0\n'
RV = 'cpu rv64\nsection .text\nglobal start\nstart:\n'
RVX = '    li a7, 93\n    ecall\n'

T = []
def t(name, src, want, opts=()):
    T.append((name, src, want, opts))

t('a64 far b.cond', A64 + '    mov x0, #5\n    cmp x0, #5\n    b.eq far\n    mov x0, #1\n    b done\n    times 300000 nop\nfar:\n    mov x0, #42\ndone:\n' + A64X, 42)
t('a64 far cbz', A64 + '    mov x0, #0\n    cbz x0, far\n    mov x0, #1\n    b done\n    times 300000 nop\nfar:\n    mov x0, #7\ndone:\n' + A64X, 7)
t('a64 far cbnz', A64 + '    mov x0, #3\n    cbnz x0, far\n    mov x0, #1\n    b done\n    times 300000 nop\nfar:\n    mov x0, #8\ndone:\n' + A64X, 8)
t('a64 far tbz', A64 + '    mov x0, #0\n    tbz x0, #3, far\n    mov x0, #1\n    b done\n    times 20000 nop\nfar:\n    mov x0, #9\ndone:\n' + A64X, 9)
t('a64 far tbnz', A64 + '    mov x0, #8\n    tbnz x0, #3, far\n    mov x0, #1\n    b done\n    times 20000 nop\nfar:\n    mov x0, #10\ndone:\n' + A64X, 10)
t('a64 not taken far', A64 + '    mov x0, #4\n    cmp x0, #5\n    b.eq far\n    mov x0, #11\n    b done\n    times 300000 nop\nfar:\n    mov x0, #42\ndone:\n' + A64X, 11)
t('a64 symbols', A64 + '    adr x1, val\n    ldr x0, [x1]\n    mov x2, val\n    ldr x2, [x2]\n    add x0, x0, x2\n    ldr x3, =0x1000000000\n    lsr x3, x3, #32\n    add x0, x0, x3\n    ldr x4, [val]\n    add x0, x0, x4\n' + A64X + 'section .data\nval: dq 10\n', 46)
t('a64 store symbol', A64 + '    mov x1, #77\n    str x1, [slot]\n    ldr x0, [slot]\n' + A64X + 'section .bss\nalignb 8\nslot: resq 1\n', 77)
t('a64 local labels', A64 + '    mov x0, #0\n    mov x1, #5\n1:  add x0, x0, #2\n    sub x1, x1, #1\n    cbnz x1, 1b\n    b 2f\n    mov x0, #99\n2:  mov x1, #3\n1:  add x0, x0, #1\n    subs x1, x1, #1\n    b.ne 1b\n' + A64X, 13)
t('a64 mov imm', A64 + '    mov x0, #0x123456789abcdef0\n    lsr x0, x0, #56\n' + A64X, 18)
t('a64 mov neg', A64 + '    mov x0, #-5\n    add x0, x0, #10\n' + A64X, 5)
t('a64 call', A64 + '    mov x0, #4\n    bl dbl\n    bl dbl\n    b done\ndbl:\n    add x0, x0, x0\n    ret\ndone:\n' + A64X, 16)
t('a64 stack', A64 + '    mov x0, #21\n    sub sp, sp, #16\n    str x0, [sp]\n    ldr x1, [sp], #16\n    add x0, x1, x1\n' + A64X, 42)
t('a64 -O0 same', A64 + '    mov x0, #6\n    add x0, x0, #1\n' + A64X, 7, ('-O0',))

t('rv far beq', RV + '    li a0, 5\n    li a1, 5\n    beq a0, a1, far\n    li a0, 1\n    j done\n    times 3000 nop\nfar:\n    li a0, 42\ndone:\n' + RVX, 42)
t('rv far bne', RV + '    li a0, 5\n    li a1, 6\n    bne a0, a1, far\n    li a0, 1\n    j done\n    times 3000 nop\nfar:\n    li a0, 43\ndone:\n' + RVX, 43)
t('rv far blt', RV + '    li a0, 5\n    li a1, 6\n    blt a0, a1, far\n    li a0, 1\n    j done\n    times 3000 nop\nfar:\n    li a0, 44\ndone:\n' + RVX, 44)
t('rv far bnez', RV + '    li a0, 5\n    bnez a0, far\n    li a0, 1\n    j done\n    times 3000 nop\nfar:\n    li a0, 45\ndone:\n' + RVX, 45)
t('rv far j', RV + '    j far\n    li a0, 1\n    j done\n    times 600000 nop\nfar:\n    li a0, 46\ndone:\n' + RVX, 46)
t('rv far j nc', RV + '    j far\n    li a0, 1\n    j done\n    times 300000 nop\nfar:\n    li a0, 47\ndone:\n' + RVX, 47, ('-cpu', 'rv64gc', '-O0'))
t('rv not taken', RV + '    li a0, 5\n    li a1, 6\n    beq a0, a1, far\n    li a0, 12\n    j done\n    times 3000 nop\nfar:\n    li a0, 1\ndone:\n' + RVX, 12)
t('rv call', RV + '    li a0, 4\n    call dbl\n    call dbl\n    j done\ndbl:\n    add a0, a0, a0\n    ret\ndone:\n' + RVX, 16)
t('rv tail', RV + '    li a0, 5\n    call outer\n    j done\nouter:\n    addi a0, a0, 1\n    tail inner\ninner:\n    addi a0, a0, 10\n    ret\ndone:\n' + RVX, 16)
t('rv la lw sw', RV + '    la t0, val\n    lw a0, 0(t0)\n    lw a1, val\n    add a0, a0, a1\n    li a2, 9\n    sw a2, slot, t1\n    lw a3, slot\n    add a0, a0, a3\n' + RVX + 'section .data\nval: dd 10\nsection .bss\nslot: resd 1\n', 29)
t('rv ld sd symbol', RV + '    li a1, 100\n    sd a1, slot, t0\n    ld a0, slot\n' + RVX + 'section .bss\nalignb 8\nslot: resq 1\n', 100)
t('rv li big', RV + '    li a0, 0x123456789abcdef0\n    srli a0, a0, 56\n' + RVX, 18)
t('rv li neg', RV + '    li a0, -5\n    addi a0, a0, 10\n' + RVX, 5)
t('rv li 32', RV + '    li a0, 0x12345678\n    srli a0, a0, 24\n' + RVX, 18)
t('rv local labels', RV + '    li a0, 0\n    li a1, 5\n1:  addi a0, a0, 2\n    addi a1, a1, -1\n    bnez a1, 1b\n    j 2f\n    li a0, 99\n2:  li a1, 3\n1:  addi a0, a0, 1\n    addi a1, a1, -1\n    bnez a1, 1b\n' + RVX, 13)
t('rv nc same', RV + '    li a0, 6\n    addi a0, a0, 1\n' + RVX, 7, ('-O0',))
t('rv32', 'cpu rv32\nsection .text\nglobal start\nstart:\n    li a0, 0x12345678\n    srli a0, a0, 24\n' + RVX, 18)
t('rv mul div', RV + '    li a0, 7\n    li a1, 6\n    mul a0, a0, a1\n    li a2, 5\n    divu a0, a0, a2\n    remu a3, a0, a2\n    add a0, a0, a3\n' + RVX, 11)
t('rv atomics', RV + '    la t0, val\n    li a1, 5\n    amoadd.w a2, a1, (t0)\n    lw a0, 0(t0)\n' + RVX + 'section .data\nval: dd 37\n', 42)

bad = 0
d = tempfile.mkdtemp()
for name, src, want, opts in T:
    p = os.path.join(d, 'x.cly')
    open(p, 'w').write(src)
    out = os.path.join(d, 'x.elf')
    r = subprocess.run(['./cly', 'linux', p, '-q', '-out', out] + list(opts), capture_output=True, text=True)
    if r.returncode:
        print('FAIL build', name, r.stderr.strip().split('\n')[0]); bad += 1; continue
    st = elf_emu.run(out, limit=3000000)
    got = st['code']
    if got != want:
        print('FAIL run', name, 'want', want, 'got', got, st.get('err', '')); bad += 1
print('runtime tests', len(T), 'bad', bad)
