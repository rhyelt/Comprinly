cases["macro1"]="""bits 32
%macro foo 2
 mov eax, %1
 mov ebx, %2
%endmacro
foo 1, 2
foo 3,4
"""
cases["macro_def"]="""bits 32
%macro bar 1-3 5, 6
 db %1, %2, %3
%endmacro
bar 1
bar 1,2
bar 1,2,3
"""
cases["macro_vararg"]="""bits 32
%macro v 1-*
%rep %0
 db %1
%rotate 1
%endrep
%endmacro
v 1,2,3,4
"""
cases["macro_greedy"]="""bits 32
%macro g 1+
 db %1
%endmacro
g "hello", 0, 1
"""
cases["macro_local"]="""bits 32
%macro l 0
%%top: dec eax
 jnz %%top
%endmacro
l
l
"""
cases["define"]="""bits 32
%define X 5
%define F(a,b) ((a)+(b)*2)
 mov eax, X
 mov ebx, F(3,4)
%undef X
%define X 7
 mov ecx, X
"""
cases["xdefine"]="""bits 32
%define A 1
%xdefine B A+1
%define A 10
 dd B
"""
cases["assign"]="""bits 32
%assign i 0
%rep 5
 dd i
%assign i i+1
%endrep
"""
cases["if"]="""bits 32
%define Z 3
%if Z > 2
 db 1
%elif Z == 2
 db 2
%else
 db 3
%endif
%ifdef Z
 db 4
%endif
%ifndef Q
 db 5
%endif
%ifnum Z
 db 6
%endif
%ifidn Z, 3
 db 7
%endif
"""
cases["strfuncs"]="""bits 32
%strlen L "hello"
%substr S "hello" 2,3
 dd L
 db S
%defstr D hello
 db D
%deftok T 'ab'
 dw T
"""
cases["concat"]="""bits 32
%define a b
%define xy 42
%define q(n) x %+ n
 dd q(y)
"""
cases["rep"]="""bits 32
%rep 3
 nop
%endrep
times 5 db 0x90
times 3*2 dw 1
"""
cases["struc"]="""bits 32
struc pt
 .x resd 1
 .y resd 1
endstruc
 mov eax, pt.y
 mov ebx, pt_size
 dd pt_size
"""
cases["istruc"]="""bits 32
struc s
 a: resb 1
 b: resw 1
endstruc
istruc s
 at a, db 1
 at b, dw 2
iend
"""
cases["equ"]="""bits 32
Len equ 5
 mov eax, Len
s: db "abc"
sl equ $ - s
 mov ebx, sl
"""
cases["data"]="""bits 32
db 1,2,3,"abc",'d',0
dw 1,2,"ab"
dd 1,-1,0x7fffffff,1.5
dq 1,2.5,-3.25
dt 1.5
do 1
dy 1
resb 4
resw 2
align 8
align 16, db 0x90
"""
cases["floats"]="""bits 32
dd 1.0, 1.5e3, -0.0, __Infinity__, __QNaN__
dq 1.0e-10, 3.14159265358979
dw __float16__(1.0)
dd __float32__(2.5)
"""
cases["strings"]="""bits 32
db `a\\nb\\x41\\u0042`
db "a", 'b', `c`
dw "ab", 'cd'
dd "abcd"
dq "abcdefgh"
"""
cases["expr"]="""bits 32
dd 1+2*3, (1+2)*3, 10/3, 10 % 3, 10//3, 10 %% 3, 1<<4, 256>>2, 5&3, 5|3, 5^3, ~5, -5, +5
dd 3 < 4, 3 > 4, 3 == 3, 3 != 3, 3 <= 3, 3 >= 4, 3 <=> 4
dd 1 && 0
dd 0x10, 0o17, 0b101, 10h, 17q, 101b, 0xFFp0
dd $$, $
dd (1 ? 2 : 3)
"""
cases["effaddr"]="""bits 32
 mov eax, [ebx]
 mov eax, [ebx+4]
 mov eax, [ebx+ecx*4+8]
 mov eax, [ecx*2]
 mov eax, [ebx*2+3]
 mov eax, [dword 5]
 mov eax, [esp]
 mov eax, [ebp]
 mov eax, [ebp+ecx]
 mov eax, [8+ebx]
 mov al, [es:ebx]
 mov al, [fs:0]
 mov eax, [ebx+ebx]
 lea eax, [ebx+ebx*2]
 lea eax, [eax*8]
"""
cases["effaddr64"]="""bits 64
 mov rax, [rbx]
 mov rax, [rbx+rcx*8+0x100]
 mov rax, [rel $+5]
 mov rax, [abs 0x1000]
 mov eax, [abs dword 0x1000]
 mov rax, [r12]
 mov rax, [r13]
 mov rax, [rsp+r8*2]
 lea rax, [rip+5]
 default rel
 mov rax, [foo]
foo: dq 0
 default abs
 mov rax, [foo]
"""
cases["jumps"]="""bits 32
l1: jmp l1
 jmp short l1
 jmp near l2
 jmp l2
 jz l1
 jnz near l2
 loop l1
 jcxz l1
 call l2
l2: ret
 times 200 nop
 jmp l1
"""
cases["sizes"]="""bits 32
 mov byte [eax], 1
 mov word [eax], 1
 mov dword [eax], 1
 add dword [eax], byte 1
 add eax, strict byte 1
 add eax, strict dword 1
 push dword 5
 push word 5
 push byte 5
 mov eax, strict dword 1
 imul eax, ebx, 5
 imul eax, [ebx], 300
"""
cases["seg"]="""bits 16
section .text
 mov ax, cs
 mov ds, ax
 jmp 0x7c0:0
 jmp far [bx]
 call far [bx]
 retf
 mov ax, [bx+si]
 mov ax, [bp+di+4]
"""
cases["org"]="""org 0x7c00
bits 16
start: mov si, msg
 jmp start
msg: db "hi",0
times 510-($-$$) db 0
dw 0xaa55
"""
cases["sections"]="""bits 32
section .text
 nop
section .data
 db 1
section .bss
 resb 4
section .text
 nop
"""
cases["absolute"]="""bits 32
absolute 0x100
a: resb 1
b: resd 1
section .text
 mov eax, a
 mov ebx, b
"""
cases["prefix"]="""bits 32
 lock add [eax], ebx
 rep movsb
 repe cmpsb
 repne scasb
 a16 mov eax, [bx]
 o16 mov eax, ebx
 cs mov eax, [ebx]
 xacquire lock add [eax], ebx
 bnd jmp eax
"""
cases["fpu"]="""bits 32
 fld dword [eax]
 fld qword [eax]
 fld tword [eax]
 fstp st1
 fadd st0, st1
 fadd st1, st0
 fsub st1, st0
 fsubp st1, st0
 fdivrp
 fxch
 fild word [eax]
 fistp qword [eax]
 fcomi st0, st1
 fcomip st0, st1
 fnstsw ax
 fldz
 fld1
"""
cases["sse"]="""bits 64
 movaps xmm0, xmm1
 movss xmm0, [rax]
 pshufd xmm0, xmm1, 0x1b
 vaddps ymm0, ymm1, ymm2
 vmovdqu ymm0, [rax]
 vpaddd zmm0 {k1}{z}, zmm1, zmm2
 vaddps zmm0, zmm1, [rax]{1to16}
 cvtsi2sd xmm0, rax
 movq xmm0, rax
"""
cases["cpu"]="""cpu 686
bits 32
 nop
"""
cases["times_expr"]="""bits 32
%assign n 3
times n db n
times n+1 nop
buf: times 16 db 0
len equ $-buf
dd len
"""
cases["include_guard"]="""bits 32
%ifndef GUARD
%define GUARD
 db 1
%endif
%ifndef GUARD
 db 2
%endif
"""
cases["ctx"]="""bits 32
%push ctx
%define %$v 5
 dd %$v
%pop
%push a
%push b
%define %$x 1
%define %$$x 2
 dd %$x, %$$x
%pop
%pop
"""
cases["predef"]="""bits 32
 dd __LINE__
 db __FILE__
 dd __NASM_MAJOR__
 dd __BITS__
%ifidn __OUTPUT_FORMAT__, bin
 db 1
%endif
"""
cases["macro_overload"]="""bits 32
%macro m 0
 db 0
%endmacro
%macro m 1
 db %1
%endmacro
%macro m 2
 db %1, %2
%endmacro
m
m 1
m 1,2
"""
cases["macro_cond"]="""bits 32
%macro j 3
 cmp %1, %2
 j%+3 skip
 nop
skip:
%endmacro
j eax, ebx, ne
"""
cases["macro_nargs"]="""bits 32
%macro q 1-2
%if %0 == 2
 db %2
%endif
 db %1
%endmacro
q 1
q 1, 2
"""
cases["cond_ops"]="""bits 32
%macro w 1
%ifstr %1
 db 1
%elifid %1
 db 2
%elifnum %1
 db 3
%else
 db 4
%endif
%endmacro
w "s"
w foo
w 5
w [eax]
"""
cases["line_cont"]="""bits 32
 mov eax, \\
   5
%define X \\
  7
 dd X
"""
cases["comments"]="""bits 32 ; c
 nop ; x
; full
 db ';', "a;b" ; after
"""
cases["local_labels"]="""bits 32
f1:
.a: nop
.b: jmp .a
f2:
.a: jmp .a
 jmp f1.b
"""
cases["extern_global"]="""bits 32
global f
extern g
f: call g
 ret
"""
cases["incbin"]="""bits 32
incbin "t.asm", 0, 4
"""
cases["warning_dir"]="""bits 32
[warning -all]
[bits 32]
[org 0]
[section .text]
 nop
"""
cases["mixed_case"]="""BITS 32
SECTION .TEXT
MOV EAX, EBX
Mov Eax, [Ebx+EcX*2]
"""
cases["seg_override"]="""bits 32
 mov eax, [cs:ebx]
 mov eax, [ss:esp]
 mov [gs:0x10], eax
"""
cases["dup"]="""bits 32
db 5 dup (1)
dw 3 dup (1,2)
"""
cases["exitrep"]="""bits 32
%assign i 0
%rep 10
%if i == 3
%exitrep
%endif
 db i
%assign i i+1
%endrep
"""
cases["indirect"]="""bits 32
%define foo 5
%define bar foo
 dd %[bar]
%define n 3
%define x3 77
 dd x%[n]
"""
cases["isdef"]="""bits 32
%define A
%if %isdef(A)
 db 1
%endif
%if %isdef(B)
 db 2
%endif
%ifmacro foo
 db 3
%endif
%macro foo 0
%endmacro
%ifmacro foo
 db 4
%endif
"""
cases["tokfuncs"]="""bits 32
%define L %strlen("abcd")
 dd L
%define S %substr("abcdef", 2, 3)
 db S
%define T %tok(abc)
 %assign x %sel(2, 10, 20, 30)
 dd x
"""
cases["pragma"]="""bits 32
%pragma ignore dummy
 nop
"""
cases["arg_local"]="""bits 32
f:
%push p
%stacksize flat
%arg a:dword, b:word
mov eax, [a]
 mov bx, [b]
%pop
"""
cases["errors_in_dead"]="""bits 32
%if 0
%error nope
bogus instruction
%endif
 nop
"""
cases["comment_blk"]="""bits 32
%comment
this is junk
%endcomment
 nop
"""
cases["unmacro"]="""bits 32
%macro m 0
 db 1
%endmacro
 m
%unmacro m 0
%define m db 2
 m
"""
cases["defalias"]="""bits 32
%define orig 9
%defalias al orig
 dd al
"""
cases["iftoken"]="""bits 32
%iftoken 5
 db 1
%endif
%ifempty
 db 2
%endif
%ifidni ABC, abc
 db 3
%endif
%ifnidn a, b
 db 4
%endif
%ifctx foo
 db 5
%else
 db 6
%endif
%ifnstr abc
 db 7
%endif
"""
cases["macro_params"]="""bits 32
%macro m 1-2+ 7
 db %1, %2
%endmacro
m 1
m 1, 2, 3
%macro n 2
 db %2, %1
%endmacro
n {1,2}, 3
%macro o 1
 db %{1:1}
%endmacro
"""
cases["macro_ctx"]="""bits 32
%macro pushctx 0
%push foo
%endmacro
%macro popctx 0
%pop
%endmacro
pushctx
%define %$a 5
 dd %$a
popctx
"""
cases["noexpand"]="""bits 32
%define a 1
%define b a
%define c b
 dd c
%xdefine d c
%define a 2
 dd d
 dd c
"""
cases["recursion"]="""bits 32
%define f(x) x+1
 dd f(f(1))
%define g(x,y) (x*y)
 dd g(2,3)
 dd g((1+1),(2))
"""
cases["fp_expr"]="""bits 32
dd 1.5 + 0
"""
cases["dq_expr"]="""bits 32
dq 0xFFFFFFFFFFFFFFFF
dq -1
dq 1<<63
dq 0x8000000000000000
dd 0xFFFFFFFF
dd -2147483648
dw 0xFFFF, -32768
db 255, -128
"""
cases["bigjmps"]="""bits 16
org 0x100
 jmp end
 times 130 nop
end: jcxz end
 jmp short end
 ret
"""
cases["rel_forward"]="""bits 64
 call f
 jmp f
 lea rax, [rel f]
 mov rax, [rel g]
f: ret
g: dq 0
"""
cases["imm_forms"]="""bits 64
 mov rax, 1
 mov rax, -1
 mov rax, 0xFFFFFFFF
 mov rax, 0x100000000
 mov eax, -1
 mov r8, 0x7FFFFFFF
 mov r8d, 5
 mov al, 255
 mov ax, -1
 add rax, 127
 add rax, 128
 add rax, -129
 and rax, 0xFF
 and eax, 0xFFFFFFF0
 push 1
 push -1
 push 1000
 push qword 1
 cmp qword [rax], 0
 cmp byte [rax], -1
 test eax, 1
 test al, 1
 xor rax, rax
 or r9, 1
"""
cases["shifts"]="""bits 32
 shl eax, 1
 shl eax, 2
 shl eax, cl
 sar byte [eax], 1
 rol ax, 3
 shld eax, ebx, 4
 shrd eax, ebx, cl
"""
cases["string_ops"]="""bits 32
 movsb
 movsw
 movsd
 cmpsb
 scasb
 lodsb
 stosb
 insb
 outsb
 rep stosd
 repne scasb
 movsb byte
"""
cases["misc_ins"]="""bits 32
 cpuid
 rdtsc
 xchg eax, ebx
 xchg ax, ax
 nop
 pause
 int3
 int 0x80
 into
 hlt
 cld
 std
 cli
 sti
 leave
 enter 8, 0
 cwde
 cdq
 cbw
 cwd
 bswap eax
 xadd [eax], ebx
 cmpxchg [eax], ebx
 cmpxchg8b [eax]
 popcnt eax, ebx
 bsf eax, ebx
 bt eax, 3
 bts [eax], ebx
 movzx eax, byte [ebx]
 movsx eax, word [ebx]
 setz al
 cmovne eax, ebx
 loopne $
 ud2
 sysenter
 lfence
 mfence
 prefetchnta [eax]
 xlat
 xlatb
"""
cases["mov_seg_cr"]="""bits 32
 mov eax, cr0
 mov cr3, eax
 mov eax, dr7
 mov ds, ax
 mov ax, ds
 mov [eax], ds
 lgdt [eax]
 lidt [eax]
 ltr ax
 smsw ax
 invlpg [eax]
 wrmsr
 rdmsr
"""
cases["mmx"]="""bits 32
 movq mm0, mm1
 movd mm0, eax
 paddb mm0, [eax]
 emms
 pshufw mm0, mm1, 5
"""
cases["bits_switch"]="""bits 16
 mov ax, 1
bits 32
 mov eax, 1
bits 64
 mov rax, 1
bits 16
 mov eax, 1
"""
cases["use_aliases"]="""use16
 mov ax, 1
use32
 mov eax, 1
"""
cases["org_times"]="""org 0x100
 db 1
 times 0x20-($-$$) db 0
 dw $
 dw $$
"""
cases["sectalign"]="""bits 32
section .text vstart=0x1000 align=16
 nop
 dd $
 dd $$
section .data vstart=0x2000 follows=.text
 dd $
"""
cases["org_section"]="""org 0x200
section .text
 dd $
section .data
 dd $
"""
cases["label_math"]="""bits 32
a: nop
b: nop
 dd b-a
 dd (b-a)*4
 db b-a
 dw b - $$
 mov eax, b-a
 mov eax, [b-a]
 mov eax, [a+4]
 mov eax, [a-4]
"""
cases["local_in_macro"]="""bits 32
%macro m 0
.x: nop
%endmacro
f: m
g: m
"""
cases["equ_fwd"]="""bits 32
 dd later
 mov eax, later2
later equ 5
later2 equ later*2
"""
cases["times_neg"]="""bits 32
 times -1 db 0
"""
cases["times_zero"]="""bits 32
 times 0 db 1
 nop
"""
cases["times_mac"]="""bits 32
%macro m 1
 db %1
%endmacro
 times 3 m 5
"""
cases["multi_label"]="""bits 32
a: b: c: nop
d:
e: nop
"""
cases["colon_opt"]="""bits 32
a nop
b db 1
c equ 3
d: equ 4
 dd c, d
"""
cases["string_esc"]="""bits 32
db `\\a\\b\\t\\n\\v\\f\\r\\e\\\\\\'\\"\\`\\?\\123\\x7f\\u263a\\U0001f600`
db 'single "double"'
db "double 'single'"
"""
cases["char_consts"]="""bits 32
 mov al, 'a'
 mov ax, 'ab'
 mov eax, 'abcd'
 mov eax, 'abc'
 mov eax, "a"
 cmp al, `\\n`
 dd 'abcd', 'a', 'ab'
 dw 'a'
"""
cases["numbers"]="""bits 32
dd 0x1F, 0X1f, 1Fh, 0h, 0fh, $0f, 0b11, 11b, 0y11, 11y, 0o17, 17o, 017, 0d17, 17d, 1_000, 0x_ff
dd 1e2
"""
cases["neg_zero_float"]="""bits 32
dd -0.0, 0.0, 1.0, 1e38, 1e39
dq 1e308, 1e-324
"""
cases["hexfloat"]="""bits 32
dd 0x1.8p1
dq 0x1p-3
"""
