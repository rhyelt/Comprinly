; expect 1
bits 32
extern malloc, printf
global main
section .data
fmt: db "v=%d", 10, 0
section .text
main:
    push ebp
    mov ebp, esp
    push dword 32
    call malloc
    add esp, 4
    mov [ebp-4], eax
    mov dword [eax], 9
    push dword [eax]
    push fmt
    call printf
    add esp, 8
    xor eax, eax
    pop ebp
    ret
