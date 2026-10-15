; expect 0
default rel
extern malloc, free, printf
global main
section .data
fmt: db "value=%d", 10, 0
section .text
main:
    push rbx
    mov edi, 64
    call malloc
    test rax, rax
    jz .fail
    mov rbx, rax
    mov dword [rbx], 42
    mov esi, [rbx]
    lea rdi, [fmt]
    xor eax, eax
    call printf
    mov rdi, rbx
    call free
.fail:
    xor eax, eax
    pop rbx
    ret
