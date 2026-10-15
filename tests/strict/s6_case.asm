bits 64
section .text
global _start
_start:
    MOV EAX, 60
    xor edi, edi
    SYSCALL
Label: nop
    jmp label
