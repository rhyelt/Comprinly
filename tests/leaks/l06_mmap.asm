; expect 1
global _start
section .data
msg: db "mapped", 10
section .text
_start:
    mov eax, 9
    xor edi, edi
    mov esi, 4096
    mov edx, 3
    mov r10d, 0x22
    mov r8, -1
    xor r9d, r9d
    syscall
    mov rbx, rax
    mov byte [rbx], 65
    mov eax, 1
    mov edi, 1
    mov esi, msg
    mov edx, 7
    syscall
    mov eax, 60
    xor edi, edi
    syscall
