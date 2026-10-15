default rel
global _start
section .data
v1: dd 1.5, 2.5, 3.5, 4.5
v2: dd 10.0, 20.0, 30.0, 40.0
align 16
res: times 4 dd 0
fmt: db "ok", 10
section .text
_start:
    movups xmm0, [v1]
    movups xmm1, [v2]
    addps xmm0, xmm1
    movups [res], xmm0
    cvttss2si eax, [res]
    cmp eax, 11
    jne .bad
    mov eax, 1
    mov edi, 1
    lea rsi, [fmt]
    mov edx, 3
    syscall
    mov eax, 60
    xor edi, edi
    syscall
.bad:
    mov eax, 60
    mov edi, 1
    syscall
