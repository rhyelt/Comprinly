bits 32
global _start
section .data
    msg db 'Hello 32', 0xA
    len equ $-msg
section .text
_start:
    mov eax, 4
    mov ebx, 1
    mov ecx, msg
    mov edx, len
    int 0x80
    mov eax, 1
    xor ebx, ebx
    int 0x80
