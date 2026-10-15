;;@ -vfmt
__cly_vfmt:
    push @b
    push @si
    push @di
    push @bp
    mov @bp, @sp
    sub @sp, @FRAME
    mov @L1, @di
    mov @L2, @si
    mov @L4, @c
    xor eax, eax
    mov @L3, @a
    mov @a, [@c]
    mov @L11, @a
    mov @a, [@c+@W]
    mov @L12, @a
    mov @a, [@c+@W*2]
    mov @L13, @a
    mov @si, @d
.loop:
    movzx eax, byte [@si]
    inc @si
    test eax, eax
    jz .end
    cmp eax, '%'
    je .spec
    call .emit
    jmp .loop
.end:
    mov @a, @L3
    mov @sp, @bp
    pop @bp
    pop @di
    pop @si
    pop @b
    ret
.emit:
    push @c
    push @d
    mov @c, @L3
    cmp @c, @L2
    jae .eskip
    mov @d, @L1
    mov [@d+@c], al
.eskip:
    inc @c
    mov @L3, @c
    pop @d
    pop @c
    ret
.vaint:
    mov @c, @L11
    cmp @c, 5
    jae .vaov
    mov @d, @L4
    mov @a, [@d+@c*@W+@W*3]
    inc @c
    mov @L11, @c
    ret
.vaov:
    mov @d, @L13
    mov @a, [@d]
    add @d, @W
    mov @L13, @d
    ret
.vadbl:
    mov @c, @L12
    cmp @c, 8
    jae .vdov
    mov @d, @L4
    movsd xmm0, [@d+@c*8+@W*8]
    inc @c
    mov @L12, @c
    ret
.vdov:
    mov @d, @L13
    movsd xmm0, [@d]
    add @d, 8
    mov @L13, @d
    ret
.utoa:
    mov @L14, @c
    lea @b, [@bp-@W*14]
.ul:
    xor edx, edx
    div @P @L14
    cmp dl, 9
    jbe .ud
    add dl, @L10
.ud:
    add dl, '0'
    dec @b
    mov [@b], dl
    test @a, @a
    jnz .ul
    lea @c, [@bp-@W*14]
    sub @c, @b
    ret
.spec:
    xor eax, eax
    mov @L5, @a
    mov @L6, @a
    mov @L8, @a
    mov @L9, @a
    mov @L10, @a
    mov @a, -1
    mov @L7, @a
.fl:
    movzx eax, byte [@si]
    cmp al, '-'
    jne .f1
    or @P @L5, 1
    jmp .fn
.f1:
    cmp al, '0'
    jne .f2
    or @P @L5, 2
    jmp .fn
.f2:
    cmp al, '+'
    jne .f3
    or @P @L5, 4
    jmp .fn
.f3:
    cmp al, ' '
    jne .f4
    or @P @L5, 8
    jmp .fn
.f4:
    cmp al, '#'
    jne .wd
    or @P @L5, 16
.fn:
    inc @si
    jmp .fl
.wd:
    cmp al, '*'
    jne .wdig
    inc @si
    call .vaint
    test eax, eax
    jns .ws
    neg eax
    or @P @L5, 1
.ws:
    mov @L6, @a
    jmp .pr
.wdig:
    xor ecx, ecx
.wl:
    movzx eax, byte [@si]
    sub eax, '0'
    cmp eax, 9
    ja .wdn
    imul ecx, ecx, 10
    add ecx, eax
    inc @si
    jmp .wl
.wdn:
    mov @L6, @c
.pr:
    cmp byte [@si], '.'
    jne .len
    inc @si
    cmp byte [@si], '*'
    jne .pdig
    inc @si
    call .vaint
    test eax, eax
    jns .ps
    mov @a, -1
.ps:
    mov @L7, @a
    jmp .len
.pdig:
    xor ecx, ecx
.pl:
    movzx eax, byte [@si]
    sub eax, '0'
    cmp eax, 9
    ja .pdn
    imul ecx, ecx, 10
    add ecx, eax
    inc @si
    jmp .pl
.pdn:
    mov @L7, @c
.len:
    movzx eax, byte [@si]
    cmp al, 'l'
    je .lg
    cmp al, 'z'
    je .lg
    cmp al, 'j'
    je .lg
    cmp al, 't'
    je .lg
    cmp al, 'q'
    je .lg
    cmp al, 'h'
    je .sk
    cmp al, 'L'
    je .sk
    jmp .conv
.lg:
    or @P @L5, 32
.sk:
    inc @si
    jmp .len
.conv:
    movzx eax, byte [@si]
    inc @si
    cmp al, 'd'
    je .cd
    cmp al, 'i'
    je .cd
    cmp al, 'u'
    je .cu
    cmp al, 'x'
    je .cx
    cmp al, 'X'
    je .cX
    cmp al, 'o'
    je .co
    cmp al, 'c'
    je .cc
    cmp al, 's'
    je .cs
    cmp al, 'p'
    je .cp
    cmp al, 'f'
    je .cf
    cmp al, 'F'
    je .cf
    cmp al, 'g'
    je .cf
    cmp al, 'G'
    je .cf
    cmp al, 'e'
    je .cf
    cmp al, 'E'
    je .cf
    cmp al, '%'
    je .lit
    test al, al
    jz .end
    push @a
    mov eax, '%'
    call .emit
    pop @a
.lit:
    call .emit
    jmp .loop
.cd:
    call .vaint
64|    test byte @L5, 32
64|    jnz .dl
64|    movsxd rax, eax
.dl:
    test @a, @a
    jns .dpos
    neg @a
    mov @P @L8, '-'
    jmp .dgo
.dpos:
    test byte @L5, 4
    jz .dsp
    mov @P @L8, '+'
    jmp .dgo
.dsp:
    test byte @L5, 8
    jz .dgo
    mov @P @L8, ' '
.dgo:
    mov ecx, 10
    jmp .num
.cu:
    call .vaint
64|    test byte @L5, 32
64|    jnz .ul2
64|    mov eax, eax
.ul2:
    mov ecx, 10
    jmp .num
.cx:
    call .vaint
64|    test byte @L5, 32
64|    jnz .xl
64|    mov eax, eax
.xl:
    mov @P @L10, 39
    test byte @L5, 16
    jz .cx1
    test @a, @a
    jz .cx1
    mov @P @L8, 0x7830
.cx1:
    mov ecx, 16
    jmp .num
.cX:
    call .vaint
64|    test byte @L5, 32
64|    jnz .Xl
64|    mov eax, eax
.Xl:
    mov @P @L10, 7
    test byte @L5, 16
    jz .cX1
    test @a, @a
    jz .cX1
    mov @P @L8, 0x5830
.cX1:
    mov ecx, 16
    jmp .num
.co:
    call .vaint
64|    test byte @L5, 32
64|    jnz .ol
64|    mov eax, eax
.ol:
    mov ecx, 8
    jmp .num
.num:
    cmp @P @L7, 0
    jne .nu
    test @a, @a
    jnz .nu
    lea @b, [@buf]
    xor ecx, ecx
    jmp .np
.nu:
    call .utoa
.np:
    mov @a, @L7
    test @a, @a
    js .nz
    and @P @L5, -3
    sub @a, @c
    jle .nz
    mov @L9, @a
.nz:
    jmp .parts
.cc:
    call .vaint
    mov [@buf], al
    lea @b, [@buf]
    mov ecx, 1
    and @P @L5, -3
    jmp .parts
.cs:
    call .vaint
    mov @b, @a
    test @b, @b
    jnz .sv
    mov @b, __cly_fmt_null
.sv:
    xor ecx, ecx
    mov @d, @L7
.sl:
    cmp @c, @d
    jae .sd
    cmp byte [@b+@c], 0
    je .sd
    inc @c
    jmp .sl
.sd:
    and @P @L5, -3
    jmp .parts
.cp:
    call .vaint
    test @a, @a
    jnz .pn
    mov @b, __cly_fmt_nil
    mov ecx, 5
    and @P @L5, -3
    jmp .parts
.pn:
    mov @P @L10, 39
    mov @P @L8, 0x7830
    mov ecx, 16
    jmp .num
.cf:
    mov @T1, eax
    call .vadbl
    movsd [@scr], xmm0
    mov eax, [@scr+4]
    test eax, eax
    jns .fp
    mov @P @L8, '-'
    and eax, 0x7fffffff
    mov [@scr+4], eax
    jmp .fa
.fp:
    test byte @L5, 4
    jz .fp1
    mov @P @L8, '+'
    jmp .fa
.fp1:
    test byte @L5, 8
    jz .fa
    mov @P @L8, ' '
.fa:
    mov eax, [@scr+4]
    mov edx, eax
    and edx, 0x7ff00000
    cmp edx, 0x7ff00000
    jne .fnum
    and eax, 0xfffff
    or eax, [@scr]
    mov @b, __cly_fmt_inf
    jz .fi
    mov @b, __cly_fmt_nan
.fi:
    mov ecx, 3
    and @P @L5, -3
    jmp .parts
.fnum:
    mov eax, @T1
    or eax, 32
    cmp eax, 'e'
    je .qs
    cmp eax, 'g'
    je .qs
    movsd xmm0, [@scr]
    mov @a, @L7
    test @a, @a
    jns .pp1
    mov @a, 6
.pp1:
    cmp @a, 30
    jbe .pp2
    mov @a, 30
.pp2:
    mov @L7, @a
    ucomisd xmm0, [__cly_fmt_lim]
    jb .inr
    movsd xmm0, [__cly_fmt_lim]
.inr:
    cvttsd2si @a, xmm0
    cvtsi2sd xmm1, @a
    subsd xmm0, xmm1
    mov @L14, @a
    lea @di, [@buf]
    mov @c, @L7
    movsd xmm2, [__cly_fmt_ten]
.fg:
    test @c, @c
    jz .fgd
    mulsd xmm0, xmm2
    cvttsd2si @a, xmm0
    cvtsi2sd xmm1, @a
    subsd xmm0, xmm1
    add al, '0'
    mov [@di], al
    inc @di
    dec @c
    jmp .fg
.fgd:
    movsd xmm1, [__cly_fmt_half]
    ucomisd xmm0, xmm1
    jb .rdone
    ja .up
    mov @c, @L7
    test @c, @c
    jz .tie0
    test byte [@di-1], 1
    jz .rdone
    jmp .up
.tie0:
    test byte @L14, 1
    jz .rdone
.up:
    mov @c, @L7
    mov @b, @di
.cy:
    test @c, @c
    jz .cyip
    dec @b
    cmp byte [@b], '9'
    jne .cyinc
    mov byte [@b], '0'
    dec @c
    jmp .cy
.cyinc:
    inc byte [@b]
    jmp .rdone
.cyip:
    inc @P @L14
.rdone:
    mov @a, @L14
    mov ecx, 10
    call .utoa
    lea @di, [@buf+64]
    push @si
    mov @si, @b
    rep movsb
    pop @si
    cmp @P @L7, 0
    jne .dot
    test byte @L5, 16
    jz .nodot
.dot:
    mov byte [@di], '.'
    inc @di
    mov @c, @L7
    push @si
    lea @si, [@buf]
    rep movsb
    pop @si
.nodot:
    lea @b, [@buf+64]
    mov @c, @di
    sub @c, @b
    jmp .parts
.parts:
    mov @a, @L8
    xor edx, edx
    test al, al
    jz .pl0
    inc edx
    test ah, ah
    jz .pl0
    inc edx
.pl0:
    lea @a, [@d+@c]
    add @a, @L9
    mov @d, @L6
    sub @d, @a
    jns .pk
    xor edx, edx
.pk:
    mov @a, @L5
    test al, 1
    jnz .nz2
    test al, 2
    jz .nz2
    add @L9, @d
    xor edx, edx
.nz2:
    mov @L14, @d
    test byte @L5, 1
    jnz .pfx
    call .spaces
.pfx:
    mov @a, @L8
    test al, al
    jz .zr
    call .emit
    mov @a, @L8
    shr eax, 8
    test al, al
    jz .zr
    call .emit
.zr:
    mov @d, @L9
.zl:
    test @d, @d
    jle .bd
    mov eax, '0'
    call .emit
    dec @d
    jmp .zl
.bd:
    test @c, @c
    jz .tr
    movzx eax, byte [@b]
    call .emit
    inc @b
    dec @c
    jmp .bd
.tr:
    test byte @L5, 1
    jz .loop
    call .spaces
    jmp .loop
.qs:
    mov @a, @L7
    test @a, @a
    jns .qp1
    mov @a, 6
.qp1:
    cmp @a, 40
    jbe .qp2
    mov @a, 40
.qp2:
    mov edx, @T1
    or edx, 32
    cmp edx, 'g'
    jne .qe0
    test @a, @a
    jnz .qg1
    mov @a, 1
.qg1:
    mov @T2, @a
    jmp .qd
.qe0:
    inc @a
    mov @T2, @a
.qd:
    movsd xmm0, [@scr]
    xorpd xmm1, xmm1
    ucomisd xmm0, xmm1
    jne .qnz
    mov dword @T4, 0
    lea @di, [@buf]
    mov @c, @T2
    mov al, '0'
    rep stosb
    jmp .qfmt
.qnz:
    mov word @T5, 0x077f
    fldcw @T5
    fldlg2
    fld qword [@scr]
    fyl2x
    frndint
    fistp dword @T4
    mov eax, @T2
    cmp eax, 18
    ja .qold
    mov word @T5, 0x037f
    fldcw @T5
.qre:
    mov eax, @T2
    dec eax
    sub eax, @T4
    cmp eax, 27
    jg .qold
    cmp eax, -27
    jl .qold
    mov edx, eax
    test eax, eax
    jns .qk1
    neg eax
.qk1:
    call .qpw
    fld qword [@scr]
    test edx, edx
    js .qk2
    fmul st0, st1
    jmp .qk3
.qk2:
    fdiv st0, st1
.qk3:
    fstp st1
    frndint
    mov eax, @T2
    call .qpw
    fcomi st0, st1
    jb .qrg
    je .qeq
    mov eax, @T2
    dec eax
    call .qpw
    fcomip st0, st2
    ja .qrl
    fstp st0
    mov eax, @T2
    dec eax
    call .qpw
    mov word @T5, 0x0f7f
    fldcw @T5
    lea @di, [@buf]
    mov @c, @T2
.qil:
    fld st1
    fdiv st0, st1
    fistp dword @T3
    mov eax, @T3
    add eax, '0'
    mov [@di], al
    inc @di
    fild dword @T3
    fmul st0, st1
    fchs
    faddp st2, st0
    fdiv qword [__cly_fmt_ten]
    dec @c
    jnz .qil
    fstp st0
    fstp st0
    jmp .qrd
.qrg:
    fstp st0
    fstp st0
    inc dword @T4
    jmp .qre
.qrl:
    fstp st0
    fstp st0
    dec dword @T4
    jmp .qre
.qeq:
    fstp st0
    fstp st0
    lea @di, [@buf]
    mov @c, @T2
    mov al, '0'
    rep stosb
    mov byte [@buf], '1'
    inc dword @T4
    jmp .qrd
.qpw:
    fld1
    fld qword [__cly_fmt_ten]
.qwl:
    test eax, eax
    jz .qwd
    test al, 1
    jz .qws
    fmul st1, st0
.qws:
    fmul st0, st0
    shr eax, 1
    jmp .qwl
.qwd:
    fstp st0
    ret
.qold:
    mov eax, @T4
    mov edx, eax
    test eax, eax
    jns .qp3
    neg eax
.qp3:
    call .qpw
    fld qword [@scr]
    test edx, edx
    js .qmu
    fdiv st0, st1
    jmp .qpf
.qmu:
    fmul st0, st1
.qpf:
    fstp st1
    fld qword [__cly_fmt_ten]
    fcomip st0, st1
    jbe .qdv
    fld1
    fcomip st0, st1
    ja .qm10
    jmp .qdg0
.qdv:
    fdiv qword [__cly_fmt_ten]
    inc dword @T4
    jmp .qdg0
.qm10:
    fmul qword [__cly_fmt_ten]
    dec dword @T4
.qdg0:
    mov word @T5, 0x0f7f
    fldcw @T5
    lea @di, [@buf]
    mov @c, @T2
.qdg:
    fld st0
    fistp dword @T3
    mov eax, @T3
    add eax, '0'
    mov [@di], al
    inc @di
    fisub dword @T3
    dec @c
    jz .qlast
    fmul qword [__cly_fmt_ten]
    jmp .qdg
.qlast:
    fld qword [__cly_fmt_half]
    fcomip st0, st1
    fstp st0
    jb .qup
    ja .qrd
    test byte [@di-1], 1
    jz .qrd
.qup:
    mov @c, @T2
    mov @b, @di
.qcy:
    dec @b
    cmp byte [@b], '9'
    jne .qinc
    mov byte [@b], '0'
    dec @c
    jnz .qcy
    mov byte [@buf], '1'
    inc dword @T4
    jmp .qrd
.qinc:
    inc byte [@b]
.qrd:
    mov word @T5, 0x037f
    fldcw @T5
.qfmt:
    lea @di, [@buf+64]
    mov edx, @T1
    or edx, 32
    cmp edx, 'g'
    je .qg
    mov al, [@buf]
    mov [@di], al
    inc @di
    mov @c, @T2
    dec @c
    jnz .qe1
    test byte @L5, 16
    jz .qexp
.qe1:
    mov byte [@di], '.'
    inc @di
    push @si
    lea @si, [@buf+1]
    rep movsb
    pop @si
.qexp:
    mov eax, @T1
    and eax, 0x20
    or eax, 0x45
    mov [@di], al
    inc @di
    mov eax, @T4
    mov byte [@di], '+'
    test eax, eax
    jns .qx1
    mov byte [@di], '-'
    neg eax
.qx1:
    inc @di
    cmp eax, 100
    jb .qx2
    xor edx, edx
    mov ecx, 100
    div ecx
    add al, '0'
    mov [@di], al
    inc @di
    mov eax, edx
.qx2:
    xor edx, edx
    mov ecx, 10
    div ecx
    add al, '0'
    mov [@di], al
    add dl, '0'
    mov [@di+1], dl
    add @di, 2
.qdone:
    lea @b, [@buf+64]
    mov @c, @di
    sub @c, @b
    jmp .parts
.qg:
    mov @c, @T2
    test byte @L5, 16
    jnz .qg0
.qgt:
    cmp @c, 1
    jbe .qg0
    cmp byte [@buf+@c-1], '0'
    jne .qg0
    dec @c
    jmp .qgt
.qg0:
    mov @T3, @c
    mov eax, @T4
    cmp eax, -4
    jl .qgs
    cmp eax, @T2
    jge .qgs
    test eax, eax
    js .qgn
    mov edx, eax
    inc edx
    xor ecx, ecx
.qgi:
    cmp ecx, edx
    jae .qgfr
    mov al, '0'
    cmp ecx, @T3
    jae .qgz
    mov al, [@buf+@c]
.qgz:
    mov [@di], al
    inc @di
    inc ecx
    jmp .qgi
.qgfr:
    cmp edx, @T3
    jb .qgdot
    test byte @L5, 16
    jz .qdone
.qgdot:
    mov byte [@di], '.'
    inc @di
.qgfl:
    cmp ecx, @T3
    jae .qdone
    mov al, [@buf+@c]
    mov [@di], al
    inc @di
    inc ecx
    jmp .qgfl
.qgn:
    mov byte [@di], '0'
    mov byte [@di+1], '.'
    add @di, 2
    neg eax
    dec eax
.qgn1:
    test eax, eax
    jz .qgn2
    mov byte [@di], '0'
    inc @di
    dec eax
    jmp .qgn1
.qgn2:
    xor ecx, ecx
.qgn3:
    cmp ecx, @T3
    jae .qdone
    mov al, [@buf+@c]
    mov [@di], al
    inc @di
    inc ecx
    jmp .qgn3
.qgs:
    mov al, [@buf]
    mov [@di], al
    inc @di
    mov ecx, 1
    cmp ecx, @T3
    jb .qgsd
    test byte @L5, 16
    jz .qexp
.qgsd:
    mov byte [@di], '.'
    inc @di
.qgsl:
    cmp ecx, @T3
    jae .qexp
    mov al, [@buf+@c]
    mov [@di], al
    inc @di
    inc ecx
    jmp .qgsl
.spaces:
    mov @d, @L14
.sl2:
    test @d, @d
    jle .sd2
    mov eax, ' '
    call .emit
    dec @d
    jmp .sl2
.sd2:
    ret
@data
__cly_fmt_null: db "(null)", 0
__cly_fmt_nil: db "(nil)", 0
__cly_fmt_inf: db "inf", 0
__cly_fmt_nan: db "nan", 0
__cly_fmt_ten: dq 10.0
__cly_fmt_half: dq 0.5
__cly_fmt_lim: dq @FLIM
;;@ -vscanf
;;> getchar
__cly_vscanf:
    push @b
    push @si
    push @di
    push @bp
    mov @bp, @sp
    sub @sp, @FR2
    xor eax, eax
    mov @L1, @a
    mov @L11, @a
    mov @L2, @c
    mov @a, [@c]
    mov @L3, @a
    mov @a, [@c+@W]
    mov @L4, @a
    mov @a, [@c+@W*2]
    mov @L5, @a
.loop:
    movzx eax, byte [@si]
    inc @si
    test eax, eax
    jz .end
    cmp al, '%'
    je .conv
    cmp al, ' '
    je .ws
    lea ecx, [eax-9]
    cmp ecx, 4
    jbe .ws
    mov @L8, @a
    call .getc
    cmp eax, @L8
    jne .mis
    jmp .loop
.ws:
    call .skipws
    jmp .loop
.mis:
    cmp eax, -1
    je .end
    mov [__cly_unget], eax
.end:
    mov @a, @L1
    test @a, @a
    jnz .ret
    cmp @P @L11, 0
    je .ret
    mov @a, -1
.ret:
    mov @sp, @bp
    pop @bp
    pop @di
    pop @si
    pop @b
    ret
.getc:
    push @si
    push @di
    @call getchar 0
    pop @di
    pop @si
    ret
.skipws:
    call .getc
    cmp eax, -1
    je .sw0
    cmp eax, ' '
    je .skipws
    lea ecx, [eax-9]
    cmp ecx, 4
    jbe .skipws
    mov [__cly_unget], eax
    ret
.sw0:
    mov @P @L11, 1
    ret
.vaint:
    mov @c, @L3
    cmp @c, 5
    jae .vaov
    mov @d, @L2
    mov @a, [@d+@c*@W+@W*3]
    inc @c
    mov @L3, @c
    ret
.vaov:
    mov @d, @L5
    mov @a, [@d]
    add @d, @W
    mov @L5, @d
    ret
.conv:
    xor eax, eax
    mov @L6, @a
    mov @L7, @a
    cmp byte [@si], '*'
    jne .cw
    or @P @L6, 2
    inc @si
.cw:
    movzx eax, byte [@si]
    sub eax, '0'
    cmp eax, 9
    ja .cl
    imul ecx, @L7, 10
    add ecx, eax
    mov @L7, @c
    inc @si
    jmp .cw
.cl:
    movzx eax, byte [@si]
    cmp al, 'l'
    je .cll
    cmp al, 'z'
    je .cll
    cmp al, 'h'
    je .clh
    cmp al, 'L'
    je .clh
    jmp .cc
.cll:
    or @P @L6, 1
.clh:
    inc @si
    jmp .cl
.cc:
    movzx eax, byte [@si]
    inc @si
    cmp al, 'd'
    je .cd
    cmp al, 'i'
    je .cd
    cmp al, 'u'
    je .cd
    cmp al, 'x'
    je .cx
    cmp al, 'X'
    je .cx
    cmp al, 's'
    je .cs
    cmp al, 'c'
    je .ch
    cmp al, 'f'
    je .cf
    cmp al, 'e'
    je .cf
    cmp al, 'g'
    je .cf
    cmp al, '%'
    je .pc
    jmp .end
.pc:
    call .skipws
    call .getc
    cmp al, '%'
    jne .mis
    jmp .loop
.cd:
    xor ebx, ebx
    mov @a, 10
    mov @L12, @a
    jmp .int
.cx:
    xor ebx, ebx
    mov @a, 16
    mov @L12, @a
.int:
    call .skipws
    xor eax, eax
    mov @L9, @a
    mov @L10, @a
    call .getc
    cmp eax, -1
    je .eof
    cmp al, '-'
    jne .ip
    mov @P @L9, 1
    call .getc
    jmp .id
.ip:
    cmp al, '+'
    jne .id
    call .getc
.id:
    mov ecx, eax
    sub ecx, '0'
    cmp ecx, 9
    jbe .ok1
    cmp @P @L12, 16
    jne .idn
    mov ecx, eax
    or ecx, 32
    sub ecx, 'a'
    cmp ecx, 5
    ja .idn
    add ecx, 10
.ok1:
    imul @b, @L12
    add @b, @c
    inc @P @L10
    call .getc
    jmp .id
.idn:
    cmp eax, -1
    je .idn2
    mov [__cly_unget], eax
.idn2:
    cmp @P @L10, 0
    je .end
    cmp @P @L9, 0
    je .st
    neg @b
.st:
    test byte @L6, 2
    jnz .loop
    call .vaint
    mov @d, @a
    test byte @L6, 1
    jnz .st64
    mov [@d], ebx
    jmp .cnt
.st64:
    mov [@d], @b
.cnt:
    inc @P @L1
    jmp .loop
.eof:
    mov @P @L11, 1
    jmp .end
.cs:
    call .skipws
    test byte @L6, 2
    jnz .cs0
    call .vaint
    mov @di, @a
.cs0:
    xor ebx, ebx
.csl:
    call .getc
    cmp eax, -1
    je .cse
    cmp eax, ' '
    je .csu
    lea ecx, [eax-9]
    cmp ecx, 4
    jbe .csu
    test byte @L6, 2
    jnz .csn
    mov [@di], al
    inc @di
.csn:
    inc ebx
    cmp @b, @L7
    jne .csl
    cmp @P @L7, 0
    je .csl
    jmp .csd
.cse:
    mov @P @L11, 1
    jmp .csd
.csu:
    mov [__cly_unget], eax
.csd:
    test ebx, ebx
    jz .end
    test byte @L6, 2
    jnz .loop
    mov byte [@di], 0
    jmp .cnt
.ch:
    call .getc
    cmp eax, -1
    je .eof
    test byte @L6, 2
    jnz .loop
    mov ebx, eax
    call .vaint
    mov [@a], bl
    jmp .cnt
.cf:
    call .skipws
    pxor xmm0, xmm0
    movsd xmm2, [__cly_sc_ten]
    xor ebx, ebx
    xor eax, eax
    mov @L9, @a
    mov @L10, @a
    call .getc
    cmp eax, -1
    je .eof
    cmp al, '-'
    jne .fp
    mov @P @L9, 1
    call .getc
    jmp .fd
.fp:
    cmp al, '+'
    jne .fd
    call .getc
.fd:
    mov ecx, eax
    sub ecx, '0'
    cmp ecx, 9
    ja .fdot
    cvtsi2sd xmm1, ecx
    mulsd xmm0, xmm2
    addsd xmm0, xmm1
    inc @P @L10
    call .getc
    jmp .fd
.fdot:
    cmp al, '.'
    jne .fend
    pxor xmm3, xmm3
    movsd xmm4, [__cly_sc_one]
    call .getc
.ff:
    mov ecx, eax
    sub ecx, '0'
    cmp ecx, 9
    ja .ffd
    cvtsi2sd xmm1, ecx
    mulsd xmm3, xmm2
    addsd xmm3, xmm1
    mulsd xmm4, xmm2
    inc @P @L10
    call .getc
    jmp .ff
.ffd:
    divsd xmm3, xmm4
    addsd xmm0, xmm3
.fend:
    cmp eax, -1
    je .fe2
    mov [__cly_unget], eax
.fe2:
    cmp @P @L10, 0
    je .end
    cmp @P @L9, 0
    je .fst
    mulsd xmm0, [__cly_sc_neg]
.fst:
    test byte @L6, 2
    jnz .loop
    movsd [@bp-@W*16], xmm0
    call .vaint
    mov @d, @a
    movsd xmm0, [@bp-@W*16]
    test byte @L6, 1
    jnz .fst64
    cvtsd2ss xmm0, xmm0
    movss [@d], xmm0
    jmp .cnt
.fst64:
    movsd [@d], xmm0
    jmp .cnt
@data
__cly_sc_ten: dq 10.0
__cly_sc_one: dq 1.0
__cly_sc_neg: dq -1.0
;;@ scanf
;;> vscanf getchar
@func scanf 1 b bp
@mkva 1
    mov @bp, @sp
    mov @si, @di
    mov @c, @bp
    call __cly_vscanf
@endva
@ret
;;@ sqrt
@func sqrt 0
32|    movsd xmm0, [esp+20]
    sqrtsd xmm0, xmm0
@fret
;;@ fabs
@func fabs 0
32|    movsd xmm0, [esp+20]
    sub @sp, 8
    movsd [@sp], xmm0
    and dword [@sp+4], 0x7fffffff
    movsd xmm0, [@sp]
    add @sp, 8
@fret
;;@ floor
@func floor 0
32|    movsd xmm0, [esp+20]
    roundsd xmm0, xmm0, 9
@fret
;;@ ceil
@func ceil 0
32|    movsd xmm0, [esp+20]
    roundsd xmm0, xmm0, 10
@fret
;;@ sin
@func sin 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fsin
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ cos
@func cos 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fcos
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ tan
@func tan 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fptan
    fstp st0
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ atan
@func atan 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fld1
    fpatan
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ asin
@func asin 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fld st0
    fmul st0, st0
    fchs
    fld1
    faddp st1, st0
    fsqrt
    fpatan
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ acos
@func acos 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fld st0
    fmul st0, st0
    fchs
    fld1
    faddp st1, st0
    fsqrt
    fxch
    fpatan
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ log
@func log 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fstp st0
    fldln2
    fld qword [@sp]
    fyl2x
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ log10
@func log10 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fstp st0
    fldlg2
    fld qword [@sp]
    fyl2x
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ log2
@func log2 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fstp st0
    fld1
    fld qword [@sp]
    fyl2x
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ exp
@func exp 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fldl2e
    fmulp st1, st0
    fld st0
    frndint
    fxch
    fsub st0, st1
    f2xm1
    fld1
    faddp st1, st0
    fscale
    fstp st1
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ exp2
@func exp2 0
32|    movsd xmm0, [esp+20]
    sub @sp, 16
    movsd [@sp], xmm0
    fld qword [@sp]
    fld st0
    frndint
    fxch
    fsub st0, st1
    f2xm1
    fld1
    faddp st1, st0
    fscale
    fstp st1
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ atan2
@func atan2 0
32|    movsd xmm0, [esp+20]
32|    movsd xmm1, [esp+28]
    sub @sp, 16
    movsd [@sp], xmm0
    movsd [@sp+8], xmm1
    fld qword [@sp]
    fld qword [@sp+8]
    fpatan
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ fmod
@func fmod 0
32|    movsd xmm0, [esp+20]
32|    movsd xmm1, [esp+28]
    sub @sp, 16
    movsd [@sp], xmm0
    movsd [@sp+8], xmm1
    fld qword [@sp+8]
    fld qword [@sp]
.l: fprem
    fnstsw ax
    test ah, 4
    jnz .l
    fstp st1
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 16
@fret
;;@ hypot
@func hypot 0
32|    movsd xmm0, [esp+20]
32|    movsd xmm1, [esp+28]
    mulsd xmm0, xmm0
    mulsd xmm1, xmm1
    addsd xmm0, xmm1
    sqrtsd xmm0, xmm0
@fret
;;@ fmin
@func fmin 0
32|    movsd xmm0, [esp+20]
32|    movsd xmm1, [esp+28]
    minsd xmm0, xmm1
@fret
;;@ fmax
@func fmax 0
32|    movsd xmm0, [esp+20]
32|    movsd xmm1, [esp+28]
    maxsd xmm0, xmm1
@fret
;;@ trunc
@func trunc 0
32|    movsd xmm0, [esp+20]
    roundsd xmm0, xmm0, 11
@fret
;;@ round
@func round 0
32|    movsd xmm0, [esp+20]
    roundsd xmm1, xmm0, 11
    movapd xmm2, xmm0
    subsd xmm2, xmm1
    movsd xmm3, [__cly_m_half]
    ucomisd xmm2, xmm3
    jb .n1
    addsd xmm1, [__cly_m_one]
    jmp .d
.n1:
    movsd xmm3, [__cly_m_nhalf]
    ucomisd xmm3, xmm2
    jb .d
    subsd xmm1, [__cly_m_one]
.d:
    movapd xmm0, xmm1
@fret
@data
__cly_m_half: dq 0.5
__cly_m_nhalf: dq -0.5
__cly_m_one: dq 1.0
;;@ pow
@func pow 0
32|    movsd xmm0, [esp+20]
32|    movsd xmm1, [esp+28]
    sub @sp, 32
    movsd [@sp], xmm0
    movsd [@sp+8], xmm1
    mov dword [@sp+16], 0
    xorpd xmm2, xmm2
    ucomisd xmm1, xmm2
    jne .nz
    jp .nz
    fld1
    jmp .out
.nz:
    ucomisd xmm0, xmm2
    jne .xnz
    jp .xnz
    ucomisd xmm1, xmm2
    ja .zero
    fld1
    fldz
    fdivp st1, st0
    jmp .out
.zero:
    fldz
    jmp .out
.xnz:
    jnp .xo
    fld qword [@sp]
    jmp .out
.xo:
    ucomisd xmm0, xmm2
    ja .pos
    cvttsd2si eax, xmm1
    cvtsi2sd xmm3, eax
    ucomisd xmm3, xmm1
    jne .nan
    jp .nan
    test al, 1
    jz .even
    mov dword [@sp+16], 1
.even:
    mov eax, [@sp+4]
    and eax, 0x7fffffff
    mov [@sp+4], eax
.pos:
    fld qword [@sp+8]
    fld qword [@sp]
    fyl2x
    fld st0
    frndint
    fxch
    fsub st0, st1
    f2xm1
    fld1
    faddp st1, st0
    fscale
    fstp st1
    cmp dword [@sp+16], 0
    je .out
    fchs
    jmp .out
.nan:
    fldz
    fldz
    fdivp st1, st0
.out:
    fstp qword [@sp]
    movsd xmm0, [@sp]
    add @sp, 32
@fret
;;@ strtod
@func strtod 2 b
    sub @sp, 32
    mov [@sp], @di
    mov @P [@sp+16], 0
.sp:
    movzx eax, byte [@di]
    cmp eax, ' '
    je .sk
    cmp eax, 9
    jb .sg
    cmp eax, 13
    ja .sg
.sk:
    inc @di
    jmp .sp
.sg:
    cmp eax, '-'
    jne .pl
    mov @P [@sp+16], 1
    inc @di
    jmp .st
.pl:
    cmp eax, '+'
    jne .st
    inc @di
.st:
    xorpd xmm0, xmm0
    xor edx, edx
    mov @P [@sp+8], 0
    movsd xmm1, [__cly_sd_ten]
.ip:
    movzx eax, byte [@di]
    sub eax, '0'
    cmp eax, 9
    ja .dot
    mulsd xmm0, xmm1
    cvtsi2sd xmm2, eax
    addsd xmm0, xmm2
    inc @P [@sp+8]
    inc @di
    jmp .ip
.dot:
    cmp byte [@di], '.'
    jne .ex0
    inc @di
.fp:
    movzx eax, byte [@di]
    sub eax, '0'
    cmp eax, 9
    ja .ex0
    mulsd xmm0, xmm1
    cvtsi2sd xmm2, eax
    addsd xmm0, xmm2
    inc @P [@sp+8]
    dec edx
    inc @di
    jmp .fp
.ex0:
    cmp @P [@sp+8], 0
    je .none
    movzx eax, byte [@di]
    or eax, 32
    cmp eax, 'e'
    jne .sc
    lea @c, [@di+1]
    mov @P [@sp+24], 0
    movzx eax, byte [@c]
    cmp eax, '-'
    jne .ep
    mov @P [@sp+24], 1
    inc @c
    jmp .ed
.ep:
    cmp eax, '+'
    jne .ed
    inc @c
.ed:
    movzx eax, byte [@c]
    sub eax, '0'
    cmp eax, 9
    ja .sc
    xor ebx, ebx
.edl:
    movzx eax, byte [@c]
    sub eax, '0'
    cmp eax, 9
    ja .edn
    cmp ebx, 100000
    jge .eskip
    imul ebx, ebx, 10
    add ebx, eax
.eskip:
    inc @c
    jmp .edl
.edn:
    cmp @P [@sp+24], 0
    je .eap
    neg ebx
.eap:
    add edx, ebx
    mov @di, @c
.sc:
    test edx, edx
    jz .sgn
    mov eax, edx
    jns .pos
    neg eax
.pos:
    cmp eax, 400
    jbe .cl
    mov eax, 400
.cl:
    movsd xmm2, [__cly_sd_one]
.pl2:
    test eax, eax
    jz .pd
    mulsd xmm2, xmm1
    dec eax
    jmp .pl2
.pd:
    test edx, edx
    js .div
    mulsd xmm0, xmm2
    jmp .sgn
.div:
    divsd xmm0, xmm2
.sgn:
    cmp @P [@sp+16], 0
    je .fin
    mulsd xmm0, [__cly_sd_neg]
.fin:
    test @si, @si
    jz .out
    mov [@si], @di
    jmp .out
.none:
    xorpd xmm0, xmm0
    test @si, @si
    jz .out
    mov @a, [@sp]
    mov [@si], @a
.out:
    add @sp, 32
@fret
@data
__cly_sd_ten: dq 10.0
__cly_sd_one: dq 1.0
__cly_sd_neg: dq -1.0
;;@ atof
;;> strtod
@func atof 1
    xor esi, esi
    @call strtod 2
@ret
;;@ sscanf
;;> vscanf getchar
@func sscanf 2 b bp
@mkva 2
    mov @bp, @sp
    mov eax, [__cly_unget]
    push @a
    mov @a, [__cly_ssrc]
    push @a
    mov [__cly_ssrc], @di
    mov dword [__cly_unget], -1
    mov @c, @bp
    call __cly_vscanf
    pop @c
    mov [__cly_ssrc], @c
    pop @c
    mov [__cly_unget], ecx
@endva
@ret
;;@ qsort
@func qsort 4 b bp
    mov @bp, @sp
    sub @sp, @FR2
    and @sp, -16
    mov @L1, @di
    mov @L2, @si
    mov @L3, @d
    mov @L4, @c
    mov @a, @si
    shr @a, 1
    mov @L5, @a
.gl:
    cmp @P @L5, 0
    je .done
    mov @a, @L5
    mov @L6, @a
.il:
    mov @a, @L6
    cmp @a, @L2
    jae .gn
    mov @L7, @a
.jl:
    mov @a, @L7
    cmp @a, @L5
    jb .in
    mov @c, @a
    sub @c, @L5
    imul @c, @L3
    add @c, @L1
    mov @d, @a
    imul @d, @L3
    add @d, @L1
    mov @L8, @c
    mov @L9, @d
    mov @di, @c
    mov @si, @d
32|    push esi
32|    push edi
    call @P @L4
32|    add esp, 8
    test eax, eax
    jle .in
    mov @c, @L3
    mov @si, @L8
    mov @di, @L9
.sw:
    test @c, @c
    jz .sd
    mov al, [@si]
    mov dl, [@di]
    mov [@si], dl
    mov [@di], al
    inc @si
    inc @di
    dec @c
    jmp .sw
.sd:
    mov @a, @L7
    sub @a, @L5
    mov @L7, @a
    jmp .jl
.in:
    inc @P @L6
    jmp .il
.gn:
    mov @a, @L5
    shr @a, 1
    mov @L5, @a
    jmp .gl
.done:
    mov @sp, @bp
@ret
