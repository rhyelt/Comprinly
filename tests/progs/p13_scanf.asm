default rel
extern printf, scanf
global main
section .data
fi: db "%d %d", 0
fms: db "%s %c", 0
ff: db "%lf", 0
fo1: db "sum=%d prod=%d", 10, 0
fo2: db "word=[%s] c=[%c]", 10, 0
fo3: db "dbl=%.3f %6.2f|%-8.1f|", 10, 0
section .bss
a resd 1
b resd 1
w resb 32
chv resb 1
d resq 1
section .text
main:
    push rbp
    mov rbp, rsp
    lea rdi, [fi]
    lea rsi, [a]
    lea rdx, [b]
    xor eax, eax
    call scanf
    mov esi, [a]
    add esi, [b]
    mov edx, [a]
    imul edx, [b]
    lea rdi, [fo1]
    xor eax, eax
    call printf
    lea rdi, [fms]
    lea rsi, [w]
    lea rdx, [chv]
    xor eax, eax
    call scanf
    lea rdi, [fo2]
    lea rsi, [w]
    movzx edx, byte [chv]
    xor eax, eax
    call printf
    lea rdi, [ff]
    lea rsi, [d]
    xor eax, eax
    call scanf
    movsd xmm0, [d]
    movsd xmm1, xmm0
    addsd xmm1, xmm0
    movsd xmm2, xmm1
    lea rdi, [fo3]
    mov eax, 3
    call printf
    xor eax, eax
    pop rbp
    ret
