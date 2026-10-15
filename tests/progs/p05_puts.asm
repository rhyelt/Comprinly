section .data
s1 db "first line", 0
s2 db "second line", 0
section .text
global main
extern puts, putchar
main:
    sub rsp, 8
    mov rdi, s1
    call puts
    mov rdi, s2
    call puts
    mov edi, 'A'
    call putchar
    mov edi, 10
    call putchar
    add rsp, 8
    xor eax, eax
    ret
