bits 64
default rel
section .data
align 8
__cly_brk dq 0
__cly_brkbase dq 0
section .text
__cly_syscall:
    push rdx
    cmp eax, 2
    je .open
    cmp eax, 9
    je .mmap
    cmp eax, 12
    je .brk
    cmp eax, 35
    je .nanosleep
    cmp eax, 96
    je .gtod
    cmp eax, 201
    je .time
    cmp eax, 228
    je .cgt
    cmp eax, __cly_nsys
    jae .bad
    lea rcx, [__cly_sysmap]
    movzx eax, word [rcx+rax*2]
    cmp eax, 0xFFFF
    je .bad
    call .do
    jmp .out
.do:
    or eax, 0x2000000
    db 0x0f, 0x05
    jnc .done
    neg rax
.done:
    ret
.bad:
    mov rax, -38
    jmp .out
.open:
    mov ecx, esi
    and ecx, 3
    test esi, 0x40
    jz .o1
    or ecx, 0x200
.o1:
    test esi, 0x200
    jz .o2
    or ecx, 0x400
.o2:
    test esi, 0x400
    jz .o3
    or ecx, 8
.o3:
    test esi, 0x80
    jz .o4
    or ecx, 0x800
.o4:
    test esi, 0x800
    jz .o5
    or ecx, 4
.o5:
    mov esi, ecx
    mov eax, 5
    call .do
    jmp .out
.mmap:
    test r10d, 0x20
    jz .m1
    and r10d, -33
    or r10d, 0x1000
.m1:
    mov eax, 197
    call .do
    jmp .out
.brk:
    mov rax, [__cly_brkbase]
    test rax, rax
    jnz .br1
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    xor edi, edi
    mov esi, 0x10000000
    mov edx, 3
    mov r10d, 0x1002
    mov r8, -1
    xor r9d, r9d
    mov eax, 197
    call .do
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    cmp rax, -4096
    ja .brbad
    mov [__cly_brkbase], rax
    mov [__cly_brk], rax
.br1:
    test rdi, rdi
    jz .br2
    mov rcx, [__cly_brkbase]
    cmp rdi, rcx
    jb .br2
    add rcx, 0x10000000
    cmp rdi, rcx
    ja .br2
    mov [__cly_brk], rdi
.br2:
    mov rax, [__cly_brk]
    jmp .out
.brbad:
    mov rax, -12
    jmp .out
.nanosleep:
    mov rax, [rdi+8]
    xor edx, edx
    mov ecx, 1000
    div rcx
    push rdi
    push rsi
    push r10
    push r8
    push rax
    push qword [rdi]
    mov r8, rsp
    xor edi, edi
    xor esi, esi
    xor edx, edx
    xor r10d, r10d
    mov eax, 93
    call .do
    add rsp, 16
    pop r8
    pop r10
    pop rsi
    pop rdi
    xor eax, eax
    jmp .out
.tod:
    push rdi
    push rsi
    sub rsp, 16
    mov rdi, rsp
    xor esi, esi
    mov eax, 116
    call .do
    mov rax, [rsp]
    mov ecx, [rsp+8]
    add rsp, 16
    pop rsi
    pop rdi
    ret
.gtod:
    call .tod
    test rdi, rdi
    jz .gd1
    mov [rdi], rax
    mov [rdi+8], rcx
.gd1:
    xor eax, eax
    jmp .out
.time:
    call .tod
    test rdi, rdi
    jz .out
    mov [rdi], rax
    jmp .out
.cgt:
    call .tod
    imul rcx, rcx, 1000
    mov [rsi], rax
    mov [rsi+8], rcx
    xor eax, eax
.out:
    pop rdx
    ret
