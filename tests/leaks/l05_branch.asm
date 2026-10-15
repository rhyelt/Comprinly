; expect 1
default rel
extern malloc, free, puts
global main
section .data
a: db "alloc", 0
b: db "early", 0
section .text
main:
    push rbx
    mov edi, 100
    call malloc
    mov rbx, rax
    test rbx, rbx
    jz .out
    lea rdi, [a]
    call puts
    cmp byte [rbx], 0
    je .early
    mov rdi, rbx
    call free
    jmp .out
.early:
    lea rdi, [b]
    call puts
.out:
    xor eax, eax
    pop rbx
    ret
