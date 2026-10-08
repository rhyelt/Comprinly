bits 32
extern __cly_image_end
section .data
__cly_cur dd 0
__cly_shift dd 0
__cly_ready dd 0
__cly_heap dd 0
__cly_brk dd 0
__cly_brkbase dd 0
section .text
__cly_init:
    pushad
    mov dword [__cly_ready], 1
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 1
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 3
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    mov edi, 0xB8000
    mov ecx, 1000
    mov eax, 0x07200720
    cld
    rep stosd
    mov dword [__cly_cur], 0
    popad
    ret
__cly_putc:
    pushad
    movzx ebx, al
    out 0xE9, al
    mov dx, 0x3FD
    mov ecx, 65536
.w:
    in al, dx
    cmp al, 0xFF
    je .vga
    test al, 0x20
    jnz .snd
    dec ecx
    jnz .w
    jmp .vga
.snd:
    mov dx, 0x3F8
    mov al, bl
    out dx, al
.vga:
    mov edi, [__cly_cur]
    cmp bl, 10
    je .nl
    cmp bl, 13
    je .cr
    cmp bl, 8
    je .bs
    cmp bl, 9
    je .tab
    mov al, bl
    mov ah, 7
    mov [0xB8000+edi*2], ax
    inc edi
    jmp .chk
.nl:
    mov eax, edi
    xor edx, edx
    mov ecx, 80
    div ecx
    sub edi, edx
    add edi, 80
    jmp .chk
.cr:
    mov eax, edi
    xor edx, edx
    mov ecx, 80
    div ecx
    sub edi, edx
    jmp .chk
.bs:
    test edi, edi
    jz .chk
    dec edi
    mov word [0xB8000+edi*2], 0x0720
    jmp .chk
.tab:
    add edi, 8
    and edi, -8
.chk:
    cmp edi, 2000
    jb .done
    mov esi, 0xB80A0
    mov edi, 0xB8000
    mov ecx, 960
    cld
    rep movsd
    mov ecx, 40
    mov eax, 0x07200720
    rep stosd
    mov edi, 1920
.done:
    mov [__cly_cur], edi
    mov dx, 0x3D4
    mov al, 0x0F
    out dx, al
    inc dx
    mov eax, edi
    out dx, al
    dec dx
    mov al, 0x0E
    out dx, al
    inc dx
    mov eax, edi
    shr eax, 8
    out dx, al
    popad
    ret
__cly_getc:
    push ebx
    push ecx
    push edx
.poll:
    mov dx, 0x3FD
    in al, dx
    cmp al, 0xFF
    je .kbd
    test al, 1
    jz .kbd
    mov dx, 0x3F8
    in al, dx
    movzx eax, al
    cmp al, 13
    jne .fin
    mov al, 10
    jmp .fin
.kbd:
    in al, 0x64
    test al, 1
    jz .poll
    in al, 0x60
    movzx ecx, al
    cmp cl, 0x2A
    je .shon
    cmp cl, 0x36
    je .shon
    cmp cl, 0xAA
    je .shoff
    cmp cl, 0xB6
    je .shoff
    test cl, 0x80
    jnz .poll
    cmp cl, 0x39
    ja .poll
    cmp dword [__cly_shift], 0
    je .norm
    movzx eax, byte [__cly_ktab_s+ecx]
    jmp .have
.norm:
    movzx eax, byte [__cly_ktab+ecx]
.have:
    test eax, eax
    jz .poll
    jmp .fin
.shon:
    mov dword [__cly_shift], 1
    jmp .poll
.shoff:
    mov dword [__cly_shift], 0
    jmp .poll
.fin:
    pop edx
    pop ecx
    pop ebx
    ret
__cly_heapptr:
    mov eax, [__cly_heap]
    test eax, eax
    jnz .r
    mov eax, __cly_image_end
    add eax, 4095
    and eax, -4096
    cmp eax, 0x100000
    jae .r
    mov eax, 0x100000
.r:
    ret
__cly_alloc:
    call __cly_heapptr
    mov ebx, eax
    mov edi, eax
    mov ecx, edx
    xor eax, eax
    cld
    rep stosb
    lea eax, [ebx+edx]
    mov [__cly_heap], eax
    mov eax, ebx
    ret
__cly_syscall:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    cmp dword [__cly_ready], 0
    jne .go
    push eax
    call __cly_init
    pop eax
.go:
    cmp eax, 1
    je .write
    cmp eax, 0
    je .read
    cmp eax, 9
    je .mmap
    cmp eax, 12
    je .brk
    cmp eax, 11
    je .zero
    cmp eax, 3
    je .zero
    cmp eax, 24
    je .zero
    cmp eax, 39
    je .getpid
    cmp eax, 35
    je .sleep
    cmp eax, 60
    je .exit
    cmp eax, 231
    je .exit
    cmp eax, 16
    je .notty
    mov eax, -38
    jmp .out
.zero:
    xor eax, eax
    jmp .out
.getpid:
    mov eax, 1
    jmp .out
.notty:
    mov eax, -25
    jmp .out
.ebadf:
    mov eax, -9
    jmp .out
.write:
    cmp edi, 1
    jb .ebadf
    cmp edi, 2
    ja .ebadf
    mov ecx, edx
    mov ebx, edx
    test ecx, ecx
    jz .wdone
.wl:
    mov al, [esi]
    call __cly_putc
    inc esi
    dec ecx
    jnz .wl
.wdone:
    mov eax, ebx
    jmp .out
.read:
    test edi, edi
    jnz .ebadf
    xor ebx, ebx
    test edx, edx
    jz .rdone
.rl:
    call __cly_getc
    cmp al, 8
    je .rbs
    cmp al, 127
    je .rbs
    mov [esi+ebx], al
    inc ebx
    call __cly_putc
    cmp al, 10
    je .rdone
    cmp ebx, edx
    jb .rl
    jmp .rdone
.rbs:
    test ebx, ebx
    jz .rl
    dec ebx
    mov al, 8
    call __cly_putc
    mov al, 32
    call __cly_putc
    mov al, 8
    call __cly_putc
    jmp .rl
.rdone:
    mov eax, ebx
    jmp .out
.mmap:
    mov edx, esi
    add edx, 4095
    and edx, -4096
    call __cly_alloc
    jmp .out
.brk:
    cmp dword [__cly_brkbase], 0
    jne .b1
    mov edx, 0x1000000
    call __cly_alloc
    mov [__cly_brkbase], eax
    mov [__cly_brk], eax
.b1:
    test edi, edi
    jz .b2
    cmp edi, [__cly_brkbase]
    jb .b2
    mov eax, [__cly_brkbase]
    add eax, 0x1000000
    cmp edi, eax
    ja .b2
    mov [__cly_brk], edi
.b2:
    mov eax, [__cly_brk]
    jmp .out
.sleep:
    mov eax, [edi+8]
    xor edx, edx
    mov ecx, 1000000
    div ecx
    mov ebx, eax
    mov eax, [edi]
    imul eax, eax, 1000
    add ebx, eax
    imul ebx, ebx, 1000
.s1:
    test ebx, ebx
    jz .zero
    out 0x80, al
    dec ebx
    jmp .s1
.exit:
    mov eax, edi
    out 0xF4, al
    mov dx, 0x604
    mov ax, 0x2000
    out dx, ax
    mov dx, 0xB004
    out dx, ax
    mov dx, 0x4004
    mov ax, 0x3400
    out dx, ax
    cli
.h:
    hlt
    jmp .h
.out:
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
