; expect 0
default rel
extern malloc, printf
global main
section .bss
buf: resq 1
section .text
main:
    sub rsp, 8
    mov edi, 100
    call malloc
    mov [buf], rax
    mov rax, [buf]
    mov byte [rax], 1
    add rsp, 8
    xor eax, eax
    ret
