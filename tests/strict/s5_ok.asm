bits 64
section .data
msg db "hi", 10
section .text
global _start
extern puts
_start:
    mov eax, 1
    mov edi, 1
    lea rsi, [rel msg]
    mov edx, 3
    syscall
    xor edi, edi
    mov eax, 231
    syscall
