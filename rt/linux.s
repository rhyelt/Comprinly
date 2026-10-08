bits 32
section .text
__cly_syscall:
    push ebx
    push ecx
    push esi
    push edi
    push ebp
    cmp eax, __cly_nsys
    jae .bad
    movzx eax, word [__cly_sysmap+eax*2]
    cmp eax, 0xFFFF
    je .bad
    mov ebx, edi
    mov ecx, esi
    mov esi, [__cly_vregs+8]
    mov ebp, [__cly_vregs+4]
    mov edi, [__cly_vregs]
    cmp eax, 192
    jne .go
    shr ebp, 12
.go:
    int 0x80
    jmp .out
.bad:
    mov eax, -38
.out:
    pop ebp
    pop edi
    pop esi
    pop ecx
    pop ebx
    ret
