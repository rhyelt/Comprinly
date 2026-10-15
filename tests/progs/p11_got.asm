default rel
section .data
msg db "got call", 0
section .text
extern puts
global main
main:
    push rax
    lea rdi, [msg]
    call [rel puts wrt ..got]
    pop rax
    xor eax, eax
    ret
section .note.GNU-stack noexec
