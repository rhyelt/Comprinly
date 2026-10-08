bits 32
section .data
__cly_brk dd 0
__cly_brkbase dd 0
section .text
__cly_syscall:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    cmp eax, 2
    je .open
    cmp eax, 8
    je .lseek
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
    movzx eax, word [__cly_sysmap+eax*2]
    cmp eax, 0xFFFF
    je .bad
    push dword [__cly_vregs+4]
    push dword [__cly_vregs]
    push dword [__cly_vregs+8]
    push edx
    push esi
    push edi
    call .do
    add esp, 24
    jmp .out
.do:
    int 0x80
    jnc .done
    neg eax
.done:
    ret
.bad:
    mov eax, -38
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
    push edx
    push ecx
    push edi
    mov eax, 5
    call .do
    add esp, 12
    jmp .out
.lseek:
    mov ecx, esi
    sar ecx, 31
    push edx
    push ecx
    push esi
    push edi
    mov eax, 199
    call .do
    add esp, 16
    jmp .out
.mmap:
    mov ecx, [__cly_vregs+8]
    test ecx, 0x20
    jz .m1
    and ecx, -33
    or ecx, 0x1000
.m1:
    push 0
    push dword [__cly_vregs+4]
    push dword [__cly_vregs]
    push ecx
    push edx
    push esi
    push edi
    mov eax, 197
    call .do
    add esp, 28
    jmp .out
.brk:
    mov eax, [__cly_brkbase]
    test eax, eax
    jnz .br1
    push 0
    push 0
    push -1
    push 0x1002
    push 3
    push 0x10000000
    push 0
    mov eax, 197
    call .do
    add esp, 28
    cmp eax, -4096
    ja .brbad
    mov [__cly_brkbase], eax
    mov [__cly_brk], eax
.br1:
    test edi, edi
    jz .br2
    mov ecx, [__cly_brkbase]
    cmp edi, ecx
    jb .br2
    add ecx, 0x10000000
    cmp edi, ecx
    ja .br2
    mov [__cly_brk], edi
.br2:
    mov eax, [__cly_brk]
    jmp .out
.brbad:
    mov eax, -12
    jmp .out
.nanosleep:
    mov eax, [edi+8]
    xor edx, edx
    mov ecx, 1000
    div ecx
    push eax
    push dword [edi]
    mov eax, esp
    push eax
    push 0
    push 0
    push 0
    push 0
    mov eax, 93
    call .do
    add esp, 28
    xor eax, eax
    jmp .out
.gtod:
    sub esp, 8
    push 0
    lea eax, [esp+4]
    push eax
    mov eax, 116
    call .do
    add esp, 8
    pop eax
    pop ecx
    test edi, edi
    jz .gd1
    mov [edi], eax
    mov dword [edi+4], 0
    mov [edi+8], ecx
    mov dword [edi+12], 0
.gd1:
    xor eax, eax
    jmp .out
.time:
    sub esp, 8
    push 0
    lea eax, [esp+4]
    push eax
    mov eax, 116
    call .do
    add esp, 8
    pop eax
    pop ecx
    test edi, edi
    jz .out
    mov [edi], eax
    mov dword [edi+4], 0
    jmp .out
.cgt:
    sub esp, 8
    push 0
    lea eax, [esp+4]
    push eax
    mov eax, 116
    call .do
    add esp, 8
    pop eax
    pop ecx
    imul ecx, ecx, 1000
    mov [esi], eax
    mov dword [esi+4], 0
    mov [esi+8], ecx
    mov dword [esi+12], 0
    xor eax, eax
.out:
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
