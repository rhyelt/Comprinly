bits 64
default rel
extern __cly_image_end
section .data
align 8
__cly_cur dq 0
__cly_heap dq 0
__cly_brk dq 0
__cly_brkbase dq 0
__cly_shift dd 0
__cly_ready dd 0
section .text
__cly_init:
    push rax
    push rcx
    push rdx
    push rdi
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
    mov qword [__cly_cur], 0
    pop rdi
    pop rdx
    pop rcx
    pop rax
    ret
__cly_putc:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
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
    mov rdi, [__cly_cur]
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
    mov [0xB8000+rdi*2], ax
    inc rdi
    jmp .chk
.nl:
    mov rax, rdi
    xor edx, edx
    mov ecx, 80
    div rcx
    sub rdi, rdx
    add rdi, 80
    jmp .chk
.cr:
    mov rax, rdi
    xor edx, edx
    mov ecx, 80
    div rcx
    sub rdi, rdx
    jmp .chk
.bs:
    test rdi, rdi
    jz .chk
    dec rdi
    mov word [0xB8000+rdi*2], 0x0720
    jmp .chk
.tab:
    add rdi, 8
    and rdi, -8
.chk:
    cmp rdi, 2000
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
    mov [__cly_cur], rdi
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
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret
__cly_getc:
    push rbx
    push rcx
    push rdx
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
    lea rbx, [__cly_ktab_s]
    movzx eax, byte [rbx+rcx]
    jmp .have
.norm:
    lea rbx, [__cly_ktab]
    movzx eax, byte [rbx+rcx]
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
    pop rdx
    pop rcx
    pop rbx
    ret
__cly_heapptr:
    mov rax, [__cly_heap]
    test rax, rax
    jnz .r
    mov rax, __cly_image_end
    add rax, 4095
    and rax, -4096
    cmp rax, 0x100000
    jae .r
    mov eax, 0x100000
.r:
    ret
__cly_alloc:
    call __cly_heapptr
    mov rbx, rax
    mov rdi, rax
    mov rcx, rdx
    xor eax, eax
    cld
    rep stosb
    lea rax, [rbx+rdx]
    mov [__cly_heap], rax
    mov rax, rbx
    ret
__cly_syscall:
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    cmp dword [__cly_ready], 0
    jne .go
    push rax
    call __cly_init
    pop rax
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
    mov rax, -38
    jmp .out
.zero:
    xor eax, eax
    jmp .out
.getpid:
    mov eax, 1
    jmp .out
.notty:
    mov rax, -25
    jmp .out
.ebadf:
    mov rax, -9
    jmp .out
.write:
    cmp edi, 1
    jb .ebadf
    cmp edi, 2
    ja .ebadf
    mov rcx, rdx
    mov rbx, rdx
    test rcx, rcx
    jz .wdone
.wl:
    mov al, [rsi]
    call __cly_putc
    inc rsi
    dec rcx
    jnz .wl
.wdone:
    mov rax, rbx
    jmp .out
.read:
    test edi, edi
    jnz .ebadf
    xor ebx, ebx
    test rdx, rdx
    jz .rdone
.rl:
    call __cly_getc
    cmp al, 8
    je .rbs
    cmp al, 127
    je .rbs
    mov [rsi+rbx], al
    inc rbx
    call __cly_putc
    cmp al, 10
    je .rdone
    cmp rbx, rdx
    jb .rl
    jmp .rdone
.rbs:
    test rbx, rbx
    jz .rl
    dec rbx
    mov al, 8
    call __cly_putc
    mov al, 32
    call __cly_putc
    mov al, 8
    call __cly_putc
    jmp .rl
.rdone:
    mov rax, rbx
    jmp .out
.mmap:
    mov rdx, rsi
    add rdx, 4095
    and rdx, -4096
    call __cly_alloc
    jmp .out
.brk:
    cmp qword [__cly_brkbase], 0
    jne .b1
    mov edx, 0x1000000
    call __cly_alloc
    mov [__cly_brkbase], rax
    mov [__cly_brk], rax
.b1:
    test rdi, rdi
    jz .b2
    cmp rdi, [__cly_brkbase]
    jb .b2
    mov rax, [__cly_brkbase]
    add rax, 0x1000000
    cmp rdi, rax
    ja .b2
    mov [__cly_brk], rdi
.b2:
    mov rax, [__cly_brk]
    jmp .out
.sleep:
    mov rax, [rdi+8]
    xor edx, edx
    mov ecx, 1000000
    div rcx
    mov rbx, rax
    mov rax, [rdi]
    imul rax, rax, 1000
    add rbx, rax
    imul rbx, rbx, 1000
.s1:
    test rbx, rbx
    jz .zero
    out 0x80, al
    dec rbx
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
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    ret
