; expect 1
default rel
extern malloc, free, printf
global main
section .data
fmt: db "node=%d", 10, 0
section .text
make_node:
    push rbp
    mov rbp, rsp
    mov edi, 16
    call malloc
    mov dword [rax], 7
    pop rbp
    ret
main:
    push rbx
    call make_node
    mov rbx, rax
    mov esi, [rbx]
    lea rdi, [fmt]
    xor eax, eax
    call printf
    xor eax, eax
    pop rbx
    ret
