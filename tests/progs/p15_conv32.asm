bits 32
global main
extern printf, strtod, atof, sscanf, qsort, sin, pow, fmod
section .data
s1 db "  -12.5e2xyz", 0
s3 db "42 hello 2.5 z", 0
f_sc db "%d %s %lf %c", 0
f1 db "strtod %g rest=%s", 10, 0
f3 db "sscanf n=%d i=%d s=%s d=%g c=%c", 10, 0
f4 db "%d %d %d %d %d", 10, 0
f5 db "%e %g %.3e", 10, 0
f6 db "%.12g %.12g %.12g", 10, 0
arr dd 9, -3, 7, 100, 0
one dq 1.0
two dq 2.0
half dq 0.5
v1 dq 1234.5678
v2 dq 0.00001234
t1 dq 10.5
t2 dq 3.0
section .bss
endp resd 1
ival resd 1
dval resq 1
cval resb 1
sbuf resb 32
res resq 1
section .text
cmpi:
    mov eax, [esp+4]
    mov eax, [eax]
    mov ecx, [esp+8]
    sub eax, [ecx]
    ret
main:
    push ebp
    mov ebp, esp
    push endp
    push s1
    call strtod
    add esp, 8
    fstp qword [res]
    push dword [endp]
    push dword [res+4]
    push dword [res]
    push f1
    call printf
    add esp, 16
    push cval
    push dval
    push sbuf
    push ival
    push f_sc
    push s3
    call sscanf
    add esp, 24
    movzx ecx, byte [cval]
    push ecx
    push dword [dval+4]
    push dword [dval]
    push sbuf
    push dword [ival]
    push eax
    push f3
    call printf
    add esp, 28
    push cmpi
    push 4
    push 5
    push arr
    call qsort
    add esp, 16
    push dword [arr+16]
    push dword [arr+12]
    push dword [arr+8]
    push dword [arr+4]
    push dword [arr]
    push f4
    call printf
    add esp, 24
    push dword [v2+4]
    push dword [v2]
    push dword [v2+4]
    push dword [v2]
    push dword [v1+4]
    push dword [v1]
    push f5
    call printf
    add esp, 28
    push dword [one+4]
    push dword [one]
    call sin
    add esp, 8
    fstp qword [res]
    sub esp, 24
    mov eax, [res]
    mov [esp+4], eax
    mov eax, [res+4]
    mov [esp+8], eax
    push dword [half+4]
    push dword [half]
    push dword [two+4]
    push dword [two]
    call pow
    add esp, 16
    fstp qword [esp+12]
    push dword [t2+4]
    push dword [t2]
    push dword [t1+4]
    push dword [t1]
    call fmod
    add esp, 16
    fstp qword [esp+20]
    mov dword [esp], f6
    call printf
    add esp, 24
    xor eax, eax
    pop ebp
    ret
