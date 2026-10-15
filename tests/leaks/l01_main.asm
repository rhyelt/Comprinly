; expect 1
default rel
extern malloc, printf
global main
section .data
fmt: db "value=%d", 10, 0
section .text
main:
    push rbx
    mov edi, 64
    call malloc
    mov rbx, rax
    mov dword [rbx], 42
    mov esi, [rbx]
    lea rdi, [fmt]
    xor eax, eax
    call printf
    xor eax, eax
    pop rbx
    ret
