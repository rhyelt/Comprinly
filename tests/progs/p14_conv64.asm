global main
extern printf, strtod, atof, sscanf, qsort, sin, pow, exp, log, fmod
section .data
s1 db "  -12.5e2xyz", 0
s2 db "3.14159", 0
s3 db "42 hello 2.5 z", 0
f_sc db "%d %s %lf %c", 0
f1 db "strtod %g rest=%s", 10, 0
f2 db "atof %.10g", 10, 0
f3 db "sscanf n=%d i=%d s=%s d=%g c=%c", 10, 0
f4 db "%d %d %d %d %d %d %d", 10, 0
f5 db "%e %g %G %.3e %g", 10, 0
f6 db "%.12g %.12g %.12g %.12g %.12g", 10, 0
arr dd 9, -3, 7, 100, 0, 7, -50
two dq 2.0
half dq 0.5
one dq 1.0
ten dq 10.0
v1 dq 1234.5678
v2 dq 0.00001234
v3 dq 1.0e20
v4 dq 0.0
v5 dq 100000.0
t1 dq 10.5
t2 dq 3.0
section .bss
endp resq 1
ival resd 1
dval resq 1
cval resb 1
sbuf resb 32
section .text
cmpi:
    mov eax, [rdi]
    sub eax, [rsi]
    ret
main:
    push rbx
    lea rdi, [s1]
    lea rsi, [endp]
    call strtod
    lea rdi, [f1]
    mov rsi, [endp]
    mov eax, 1
    call printf
    lea rdi, [s2]
    call atof
    lea rdi, [f2]
    mov eax, 1
    call printf
    lea rdi, [s3]
    lea rsi, [f_sc]
    lea rdx, [ival]
    lea rcx, [sbuf]
    lea r8, [dval]
    lea r9, [cval]
    xor eax, eax
    call sscanf
    mov esi, eax
    lea rdi, [f3]
    mov edx, [ival]
    lea rcx, [sbuf]
    movsd xmm0, [dval]
    movzx r8d, byte [cval]
    mov eax, 1
    call printf
    lea rdi, [arr]
    mov esi, 7
    mov edx, 4
    lea rcx, [cmpi]
    call qsort
    lea rdi, [f4]
    mov esi, [arr]
    mov edx, [arr+4]
    mov ecx, [arr+8]
    mov r8d, [arr+12]
    mov r9d, [arr+16]
    sub rsp, 16
    mov eax, [arr+24]
    mov [rsp+8], rax
    mov eax, [arr+20]
    mov [rsp], rax
    xor eax, eax
    call printf
    add rsp, 16
    lea rdi, [f5]
    movsd xmm0, [v1]
    movsd xmm1, [v2]
    movsd xmm2, [v3]
    movsd xmm3, [v4]
    movsd xmm4, [v5]
    mov eax, 5
    call printf
    sub rsp, 48
    movsd xmm0, [one]
    call sin
    movsd [rsp], xmm0
    movsd xmm0, [two]
    movsd xmm1, [half]
    call pow
    movsd [rsp+8], xmm0
    movsd xmm0, [one]
    call exp
    movsd [rsp+16], xmm0
    movsd xmm0, [ten]
    call log
    movsd [rsp+24], xmm0
    movsd xmm0, [t1]
    movsd xmm1, [t2]
    call fmod
    movsd [rsp+32], xmm0
    lea rdi, [f6]
    movsd xmm0, [rsp]
    movsd xmm1, [rsp+8]
    movsd xmm2, [rsp+16]
    movsd xmm3, [rsp+24]
    movsd xmm4, [rsp+32]
    mov eax, 5
    call printf
    add rsp, 48
    xor eax, eax
    pop rbx
    ret
