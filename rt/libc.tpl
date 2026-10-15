;;@ exit _exit
@func exit 1
64|    mov eax, 231
64|    syscall
32|    mov ebx, edi
32|    mov eax, 252
32|    int 0x80
;;@ abort
@func abort 0
64|    mov edi, 134
64|    mov eax, 231
64|    syscall
32|    mov ebx, 134
32|    mov eax, 252
32|    int 0x80
;;@ write
@func write 3
64|    mov eax, 1
64|    syscall
32|    mov ebx, edi
32|    mov ecx, esi
32|    mov eax, 4
32|    int 0x80
    cmp @a, -4095
    jb .ok
    mov @a, -1
.ok:
@ret
;;@ read
@func read 3
64|    xor eax, eax
64|    syscall
32|    mov ebx, edi
32|    mov ecx, esi
32|    mov eax, 3
32|    int 0x80
    cmp @a, -4095
    jb .ok
    mov @a, -1
.ok:
@ret
;;@ getpid
@func getpid 0
64|    mov eax, 39
64|    syscall
32|    mov eax, 20
32|    int 0x80
@ret
;;@ strlen
@func strlen 1
    xor @a, @a
.l: cmp byte [@di+@a], 0
    je .d
    inc @a
    jmp .l
.d:
@ret
;;@ strcpy
@func strcpy 2
    mov @a, @di
.l: mov cl, [@si]
    mov [@di], cl
    inc @si
    inc @di
    test cl, cl
    jnz .l
@ret
;;@ strncpy
@func strncpy 3
    mov @a, @di
.l: test @d, @d
    jz .d
    mov cl, [@si]
    mov [@di], cl
    inc @di
    dec @d
    test cl, cl
    jz .pad
    inc @si
    jmp .l
.pad:
    test @d, @d
    jz .d
    mov byte [@di], 0
    inc @di
    dec @d
    jmp .pad
.d:
@ret
;;@ strcat
@func strcat 2
    mov @a, @di
.f: cmp byte [@di], 0
    je .c
    inc @di
    jmp .f
.c: mov cl, [@si]
    mov [@di], cl
    inc @si
    inc @di
    test cl, cl
    jnz .c
@ret
;;@ strncat
@func strncat 3
    mov @a, @di
.f: cmp byte [@di], 0
    je .c
    inc @di
    jmp .f
.c: test @d, @d
    jz .e
    mov cl, [@si]
    test cl, cl
    jz .e
    mov [@di], cl
    inc @si
    inc @di
    dec @d
    jmp .c
.e: mov byte [@di], 0
@ret
;;@ strcmp
@func strcmp 2
.l: movzx eax, byte [@di]
    movzx ecx, byte [@si]
    sub eax, ecx
    jne .d
    test ecx, ecx
    jz .d
    inc @di
    inc @si
    jmp .l
.d:
@ret
;;@ strncmp
@func strncmp 3
.l: test @d, @d
    jz .z
    movzx eax, byte [@di]
    movzx ecx, byte [@si]
    sub eax, ecx
    jne .d
    test ecx, ecx
    jz .d
    inc @di
    inc @si
    dec @d
    jmp .l
.z: xor eax, eax
.d:
@ret
;;@ strchr
@func strchr 2
    mov edx, esi
.l: mov cl, [@di]
    cmp cl, dl
    je .f
    test cl, cl
    jz .n
    inc @di
    jmp .l
.f: mov @a, @di
    jmp .d
.n: xor eax, eax
.d:
@ret
;;@ strrchr
@func strrchr 2
    mov edx, esi
    xor eax, eax
.l: mov cl, [@di]
    cmp cl, dl
    jne .n
    mov @a, @di
.n: test cl, cl
    jz .d
    inc @di
    jmp .l
.d:
@ret
;;@ strstr
@func strstr 2 b
.o: mov @a, @di
    mov @b, @si
.i: mov cl, [@b]
    test cl, cl
    jz .hit
    cmp cl, [@a]
    jne .nx
    inc @a
    inc @b
    jmp .i
.nx:
    cmp byte [@di], 0
    je .no
    inc @di
    jmp .o
.hit:
    mov @a, @di
    jmp .d
.no:
    xor eax, eax
.d:
@ret
;;@ strdup
;;> malloc strlen memcpy
@func strdup 1 b
    mov @b, @di
    @call strlen 1
    inc @a
    push @a
    mov @di, @a
    @call malloc 1
    pop @d
    test @a, @a
    jz .d
    push @a
    mov @di, @a
    mov @si, @b
    @call memcpy 3
    pop @a
.d:
@ret
;;@ memcpy
@func memcpy 3
    mov @a, @di
    mov @c, @d
    rep movsb
@ret
;;@ memmove
@func memmove 3
    mov @a, @di
    mov @c, @d
    cmp @di, @si
    jbe .fw
    lea @di, [@di+@c-1]
    lea @si, [@si+@c-1]
    std
    rep movsb
    cld
    jmp .d
.fw:
    rep movsb
.d:
@ret
;;@ memset
@func memset 3
    push @di
    mov @a, @si
    mov @c, @d
    rep stosb
    pop @a
@ret
;;@ memcmp
@func memcmp 3
    xor eax, eax
.l: test @d, @d
    jz .d
    movzx eax, byte [@di]
    movzx ecx, byte [@si]
    sub eax, ecx
    jne .d
    inc @di
    inc @si
    dec @d
    jmp .l
.d:
@ret
;;@ atoi atol
@func atoi 1
    xor eax, eax
    xor ecx, ecx
.sp: movzx edx, byte [@di]
    cmp edx, ' '
    je .sk
    cmp edx, 9
    jb .sg
    cmp edx, 13
    ja .sg
.sk: inc @di
    jmp .sp
.sg: cmp edx, '-'
    jne .pl
    mov ecx, 1
    inc @di
    jmp .dg
.pl: cmp edx, '+'
    jne .dg
    inc @di
.dg: movzx edx, byte [@di]
    sub edx, '0'
    cmp edx, 9
    ja .dn
    lea @a, [@a+@a*4]
    lea @a, [@d+@a*2]
    inc @di
    jmp .dg
.dn: test ecx, ecx
    jz .d
    neg @a
.d:
@ret
;;@ strtol strtoll strtoul strtoull
@func strtol 3 b
    mov @b, @d
    xor eax, eax
    xor ecx, ecx
.sp: movzx edx, byte [@di]
    cmp edx, ' '
    je .sk
    cmp edx, 9
    jb .sg
    cmp edx, 13
    ja .sg
.sk: inc @di
    jmp .sp
.sg: cmp edx, '-'
    jne .pl
    mov ecx, 1
    inc @di
    jmp .pf
.pl: cmp edx, '+'
    jne .pf
    inc @di
.pf: cmp @b, 16
    je .x16
    test @b, @b
    jne .dg
    cmp byte [@di], '0'
    jne .s10
    mov dl, [@di+1]
    or dl, 32
    cmp dl, 'x'
    je .x16
    mov @b, 8
    jmp .dg
.s10: mov @b, 10
    jmp .dg
.x16: mov @b, 16
    cmp byte [@di], '0'
    jne .dg
    mov dl, [@di+1]
    or dl, 32
    cmp dl, 'x'
    jne .dg
    add @di, 2
.dg: movzx edx, byte [@di]
    cmp edx, '0'
    jb .dn
    cmp edx, '9'
    jbe .num
    or edx, 32
    cmp edx, 'a'
    jb .dn
    cmp edx, 'z'
    ja .dn
    sub edx, 'a' - 10
    jmp .chk
.num: sub edx, '0'
.chk: cmp @d, @b
    jae .dn
    imul @a, @b
    add @a, @d
    inc @di
    jmp .dg
.dn: test ecx, ecx
    jz .e
    neg @a
.e: test @si, @si
    jz .r
    mov [@si], @di
.r:
@ret
;;@ abs
@func abs 1
    mov eax, edi
    neg eax
    cmovl eax, edi
@ret
;;@ labs llabs
@func labs 1
    mov @a, @di
    neg @a
    cmovl @a, @di
@ret
;;@ toupper
@func toupper 1
    mov eax, edi
    lea ecx, [eax-'a']
    cmp ecx, 25
    ja .d
    sub eax, 32
.d:
@ret
;;@ tolower
@func tolower 1
    mov eax, edi
    lea ecx, [eax-'A']
    cmp ecx, 25
    ja .d
    add eax, 32
.d:
@ret
;;@ isdigit
@func isdigit 1
    lea ecx, [edi-'0']
    xor eax, eax
    cmp ecx, 9
    setbe al
@ret
;;@ isalpha
@func isalpha 1
    mov ecx, edi
    or ecx, 32
    sub ecx, 'a'
    xor eax, eax
    cmp ecx, 25
    setbe al
@ret
;;@ isalnum
@func isalnum 1
    lea ecx, [edi-'0']
    cmp ecx, 9
    jbe .y
    mov ecx, edi
    or ecx, 32
    sub ecx, 'a'
    cmp ecx, 25
    jbe .y
    xor eax, eax
    jmp .d
.y: mov eax, 1
.d:
@ret
;;@ isspace
@func isspace 1
    xor eax, eax
    cmp edi, ' '
    je .y
    lea ecx, [edi-9]
    cmp ecx, 4
    ja .d
.y: mov eax, 1
.d:
@ret
;;@ isupper
@func isupper 1
    lea ecx, [edi-'A']
    xor eax, eax
    cmp ecx, 25
    setbe al
@ret
;;@ islower
@func islower 1
    lea ecx, [edi-'a']
    xor eax, eax
    cmp ecx, 25
    setbe al
@ret
;;@ rand
@func rand 0
    mov eax, [__cly_seed]
    imul eax, eax, 1103515245
    add eax, 12345
    mov [__cly_seed], eax
    shr eax, 16
    and eax, 0x7fff
@ret
@data
__cly_seed: dd 1
;;@ srand
;;> rand
@func srand 1
    mov [__cly_seed], edi
@ret
;;@ time
@func time 1
64|    mov eax, 201
64|    syscall
32|    mov ebx, edi
32|    mov eax, 13
32|    int 0x80
@ret
;;@ usleep
@func usleep 1
    mov eax, edi
    xor edx, edx
    mov ecx, 1000000
    div ecx
    mov [__cly_ts], @a
    imul edx, edx, 1000
    mov [__cly_ts+@W], @d
64|    mov edi, __cly_ts
64|    xor esi, esi
64|    mov eax, 35
64|    syscall
32|    mov ebx, __cly_ts
32|    xor ecx, ecx
32|    mov eax, 162
32|    int 0x80
    xor eax, eax
@ret
@bss
__cly_ts: @RES 2
;;@ sleep
;;> usleep
@func sleep 1
    imul edi, edi, 1000000
    @call usleep 1
    xor eax, eax
@ret
;;@ -brkmap
@func brkmap 1
    mov @si, @di
64|    xor edi, edi
64|    mov edx, 3
64|    mov r10d, 0x22
64|    mov r8, -1
64|    xor r9d, r9d
64|    mov eax, 9
64|    syscall
32|    xor ebx, ebx
32|    mov ecx, esi
32|    mov edx, 3
32|    mov esi, 0x22
32|    mov edi, -1
32|    xor ebp, ebp
32|    mov eax, 192
32|    int 0x80
    cmp @a, -4095
    jb .ok
    xor eax, eax
.ok:
@ret
;;@ malloc
;;> brkmap
@func malloc 1 b
    test @di, @di
    jnz .nz
    mov edi, 1
.nz: lea @di, [@di+@G-1]
    and @di, -@G
    lea @b, [__cly_freelist]
.fl: mov @c, [@b]
    test @c, @c
    jz .bump
    cmp [@c], @di
    jae .take
    lea @b, [@c+@H]
    jmp .fl
.take:
    mov @d, [@c+@H]
    mov [@b], @d
    lea @a, [@c+@H]
    jmp .out
.bump:
    mov @a, [__cly_hcur]
    lea @d, [@a+@di+@H]
    cmp @d, [__cly_hend]
    jbe .have
    lea @d, [@di+@H+4095]
    and @d, -4096
    cmp @d, 1048576
    jae .big
    mov @d, 1048576
.big:
    push @di
    push @d
    mov @di, @d
    @call brkmap 1
    pop @d
    pop @di
    test @a, @a
    jz .out
    mov [__cly_hcur], @a
    lea @c, [@a+@d]
    mov [__cly_hend], @c
.have:
    mov @a, [__cly_hcur]
    mov [@a], @di
    lea @d, [@a+@di+@H]
    mov [__cly_hcur], @d
    add @a, @H
.out:
@ret
@bss
__cly_hcur: @RES 1
__cly_hend: @RES 1
__cly_freelist: @RES 1
;;@ free
@func free 1
    test @di, @di
    jz .d
    sub @di, @H
    mov @a, [__cly_freelist]
    mov [@di+@H], @a
    mov [__cly_freelist], @di
.d:
@ret
;;@ calloc
;;> malloc memset
@func calloc 2 b
    imul @di, @si
    mov @b, @di
    @call malloc 1
    test @a, @a
    jz .d
    mov @di, @a
    xor esi, esi
    mov @d, @b
    push @a
    @call memset 3
    pop @a
.d:
@ret
;;@ realloc
;;> malloc free memcpy
@func realloc 2 b
    test @di, @di
    jnz .have
    mov @di, @si
    @call malloc 1
    jmp .d
.have:
    mov @b, @di
    mov @a, [@di-@H]
    cmp @a, @si
    jae .same
    mov @di, @si
    @call malloc 1
    test @a, @a
    jz .d
    push @a
    mov @di, @a
    mov @si, @b
    mov @d, [@b-@H]
    @call memcpy 3
    mov @di, @b
    @call free 1
    pop @a
    jmp .d
.same:
    mov @a, @b
.d:
@ret
;;@ putchar
;;> write
@func putchar 1
    mov eax, edi
    mov [__cly_chbuf], al
    mov edi, 1
    mov esi, __cly_chbuf
    mov edx, 1
    @call write 3
    movzx eax, byte [__cly_chbuf]
@ret
@bss
__cly_chbuf: @RES 1
;;@ puts
;;> write strlen
@func puts 1 b
    mov @b, @di
    @call strlen 1
    mov @d, @a
    mov @si, @b
    mov edi, 1
    @call write 3
    mov edi, 1
    mov esi, __cly_nl
    mov edx, 1
    @call write 3
    mov eax, 1
@ret
@data
__cly_nl: db 10
;;@ =stdio_files stdin stdout stderr
@data
stdin: @D __cly_f0
stdout: @D __cly_f1
stderr: @D __cly_f2
__cly_f0: @D 0
__cly_f1: @D 1
__cly_f2: @D 2
;;@ fputs
;;> write strlen
@func fputs 2 b
    mov @b, @si
    @call strlen 1
    mov @d, @a
    mov @si, @di
    mov @di, [@b]
    @call write 3
    mov eax, 1
@ret
;;@ fputc putc
;;> write
@func fputc 2
    mov eax, edi
    mov [__cly_fcbuf], al
    mov edi, [@si]
    mov esi, __cly_fcbuf
    mov edx, 1
    @call write 3
    movzx eax, byte [__cly_fcbuf]
@ret
@bss
__cly_fcbuf: @RES 1
;;@ fflush fclose
@func fflush 1
    xor eax, eax
@ret
;;@ fwrite
;;> write
@func fwrite 4 b
    mov @b, @d
    imul @si, @d
    mov @a, @di
    mov @di, [@c]
    mov @d, @si
    mov @si, @a
    @call write 3
    mov @a, @b
@ret
;;@ getchar fgetc getc
;;> read
@func getchar 0
    mov eax, [__cly_unget]
    cmp eax, -1
    je .rd
    mov dword [__cly_unget], -1
    jmp .d
.rd:
    mov @c, [__cly_ssrc]
    test @c, @c
    jz .rd0
    movzx eax, byte [@c]
    test eax, eax
    jz .eof
    inc @c
    mov [__cly_ssrc], @c
    jmp .d
.rd0:
    xor edi, edi
    mov esi, __cly_gcbuf
    mov edx, 1
    @call read 3
    cmp @a, 1
    jne .eof
    movzx eax, byte [__cly_gcbuf]
    jmp .d
.eof:
    mov eax, -1
.d:
@ret
@bss
__cly_gcbuf: @RES 1
@data
__cly_unget: dd -1
__cly_ssrc: @D 0
;;@ fgets
;;> getchar
@func fgets 3 b
    mov @b, @di
    mov @c, @si
    dec @c
    jle .none
.l: test @c, @c
    jz .end
    push @c
    push @di
    @call getchar 0
    pop @di
    pop @c
    cmp eax, -1
    je .eof
    mov [@di], al
    inc @di
    dec @c
    cmp al, 10
    jne .l
.end:
    mov byte [@di], 0
    mov @a, @b
    jmp .d
.eof:
    cmp @di, @b
    je .none
    jmp .end
.none:
    xor eax, eax
.d:
@ret
;;@ gets
;;> getchar
@func gets 1 b
    mov @b, @di
.l: push @di
    @call getchar 0
    pop @di
    cmp eax, -1
    je .eof
    cmp al, 10
    je .end
    mov [@di], al
    inc @di
    jmp .l
.end:
    mov byte [@di], 0
    mov @a, @b
    jmp .d
.eof:
    cmp @di, @b
    je .none
    jmp .end
.none:
    xor eax, eax
.d:
@ret
;;@ printf
;;> vfmt write
@func printf 1 b bp
@mkva 1
    mov @bp, @sp
    mov @X1, @di
    xor edi, edi
    xor esi, esi
    mov @d, @X1
    mov @c, @bp
    call __cly_vfmt
    mov @X2, @a
    lea @c, [@a+16]
    and @c, -16
    sub @sp, @c
    mov @di, @sp
    mov @si, @X2
    mov @d, @X1
    mov @c, @bp
    call __cly_vfmt
    mov @si, @sp
    mov edi, 1
    mov @d, @X2
    @call write 3
    mov @a, @X2
@endva
@ret
;;@ fprintf
;;> vfmt write
@func fprintf 2 b bp
@mkva 2
    mov @bp, @sp
    mov edi, [@di]
    mov @X3, @di
    mov @X1, @si
    xor edi, edi
    xor esi, esi
    mov @d, @X1
    mov @c, @bp
    call __cly_vfmt
    mov @X2, @a
    lea @c, [@a+16]
    and @c, -16
    sub @sp, @c
    mov @di, @sp
    mov @si, @X2
    mov @d, @X1
    mov @c, @bp
    call __cly_vfmt
    mov @si, @sp
    mov @di, @X3
    mov @d, @X2
    @call write 3
    mov @a, @X2
@endva
@ret
;;@ sprintf
;;> vfmt
@func sprintf 2 b bp
@mkva 2
    mov @bp, @sp
    mov @X1, @di
    mov @d, @si
    or @si, -1
    mov @c, @bp
    call __cly_vfmt
    mov @di, @X1
    mov byte [@di+@a], 0
@endva
@ret
;;@ snprintf
;;> vfmt
@func snprintf 3 b bp
@mkva 3
    mov @bp, @sp
    mov @X1, @di
    mov @b, @si
    dec @si
    jns .lim
    xor esi, esi
.lim:
    mov @c, @bp
    call __cly_vfmt
    mov @X2, @a
    test @b, @b
    jz .d
    mov @di, @X1
    mov @c, @b
    dec @c
    cmp @a, @c
    cmova @a, @c
    mov byte [@di+@a], 0
.d:
    mov @a, @X2
@endva
@ret
