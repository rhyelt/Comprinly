; expect 1
default rel
extern malloc, exit, puts
global main
section .data
m: db "bye", 0
section .text
main:
    sub rsp, 8
    mov edi, 8
    call malloc
    mov r12, rax
    lea rdi, [m]
    call puts
    mov edi, 3
    call exit
