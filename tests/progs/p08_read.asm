section .bss
buf resb 128
section .data
nl db 10
section .text
global _start
_start:
.loop:
    mov rax, 0
    mov rdi, 0
    mov rsi, buf
    mov rdx, 128
    syscall
    test rax, rax
    jle .exit
    mov rdx, rax
    mov rax, 1
    mov rdi, 1
    mov rsi, buf
    syscall
    jmp .loop
.exit:
    mov rax, 60
    mov rdi, 3
    syscall
