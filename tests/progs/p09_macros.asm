%define SYS_WRITE 1
%define SYS_EXIT 60
%macro print 2
    mov rax, SYS_WRITE
    mov rdi, 1
    mov rsi, %1
    mov rdx, %2
    syscall
%endmacro
%macro exit 1
    mov rax, SYS_EXIT
    mov rdi, %1
    syscall
%endmacro
struc point
    .x: resd 1
    .y: resd 1
endstruc
section .data
a: db "alpha", 10
alen equ $ - a
b: db "beta", 10
blen equ $ - b
pt: istruc point
    at point.x, dd 3
    at point.y, dd 4
iend
pad: times 8 db 0
section .text
global _start
_start:
    print a, alen
    print b, blen
    mov eax, [pt + point.x]
    add eax, [pt + point.y]
    exit rax
