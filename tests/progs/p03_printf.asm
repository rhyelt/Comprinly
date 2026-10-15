default rel
bits 64
extern printf
extern exit
global main

section .rodata
fmt1: db "Hello %s, you are %d years old", 10, 0
fmt2: db "hex=%x HEX=%X char=%c unsigned=%u long=%ld pct=%%", 10, 0
fmt3: db "[%5d] [%-5d] [%05d] [%s]", 10, 0
name: db "World", 0

section .text
main:
    push rbp
    mov rbp, rsp
    lea rdi, [fmt1]
    lea rsi, [name]
    mov edx, 42
    xor eax, eax
    call printf
    lea rdi, [fmt2]
    mov esi, 255
    mov edx, 255
    mov ecx, 'Z'
    mov r8d, -1
    mov r9, -123456789012
    xor eax, eax
    call printf
    lea rdi, [fmt3]
    mov esi, 42
    mov edx, 42
    mov ecx, 42
    lea r8, [name]
    xor eax, eax
    call printf
    mov eax, 7
    pop rbp
    ret
