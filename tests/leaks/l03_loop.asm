; expect 1
default rel
extern malloc, printf
global main
section .data
fmt: db "i=%d", 10, 0
section .text
main:
    push rbx
    push r12
    xor r12d, r12d
.again:
    mov edi, 32
    call malloc
    mov rbx, rax
    mov [rbx], r12d
    mov esi, [rbx]
    lea rdi, [fmt]
    xor eax, eax
    call printf
    inc r12d
    cmp r12d, 3
    jl .again
    xor eax, eax
    pop r12
    pop rbx
    ret
