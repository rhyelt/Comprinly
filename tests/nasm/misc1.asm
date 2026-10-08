bits 32
mov eax, 1
mov ax, 1
mov al, 1
mov eax, 0x12345678
mov [eax], byte 1
mov byte [eax], 1
mov word [eax], 1
mov dword [eax], 1
mov eax, ebx
add eax, 1
add eax, 127
add eax, 128
add eax, -128
add eax, -129
add ax, 1
add ax, 0x1234
add al, 1
add byte [eax], 1
add dword [eax], byte 1
add dword [eax], strict byte 1
add dword [eax], 0x1234
push 1
push 0x1234
push dword 1
push word 1
push eax
push ax
push es
push fs
pop gs
push dword [eax]
push word [eax]
pop dword [eax]
imul eax, ebx
imul eax, ebx, 5
imul eax, ebx, 500
imul eax, [ebx], 5
imul eax, 5
imul eax, 500
imul ebx
imul byte [eax]
shl eax, 1
shl eax, 2
shl eax, cl
shl byte [eax], 1
sar word [ebx], cl
rol eax, 1
test eax, eax
test eax, 1
test al, 1
test byte [eax], 1
test [eax], eax
xchg eax, ebx
xchg ebx, eax
xchg eax, eax
xchg ax, bx
nop
pause
xchg [eax], ebx
lea eax, [ebx+ecx*2+5]
lea eax, [eax+eax*2]
lea eax, [eax*4]
movzx eax, byte [ebx]
movzx eax, bl
movzx eax, word [ebx]
movzx ax, bl
movsx eax, bx
movsx eax, byte [ebx]
cmovz eax, ebx
cmovne eax, [ebx]
setz al
setnz byte [eax]
bt eax, 5
bt [eax], ebx
bts eax, ebx
btr dword [eax], 5
btc ax, 3
bsf eax, ebx
bsr eax, [ebx]
shld eax, ebx, 5
shld eax, ebx, cl
shrd [eax], ebx, 3
cmpxchg [eax], ebx
xadd [eax], bl
bswap eax
enter 8, 0
leave
ret
ret 8
retn
retf
retf 4
int 3
int 0x80
into
iret
iretd
iretw
in al, 0x60
in eax, dx
out dx, al
out 0x60, eax
insb
outsd
cpuid
rdtsc
rdmsr
wrmsr
lgdt [eax]
lidt [eax]
sgdt [eax]
mov eax, cr0
mov cr3, eax
mov eax, dr7
mov dr0, ebx
mov ds, ax
mov es, [eax]
mov ax, ds
mov [eax], cs
mov eax, ds
hlt
cli
sti
clc
stc
cmc
cld
std
lahf
sahf
xlat
xlatb
daa
das
aaa
aas
aam
aad
aam 5
aad 5
bound eax, [ebx]
arpl [eax], bx
lds eax, [ebx]
les ax, [ebx]
lfs eax, [ebx]
lar eax, bx
lsl eax, [ebx]
verr ax
verw [eax]
ltr ax
lldt bx
sldt eax
str [eax]
smsw ax
lmsw bx
invlpg [eax]
wbinvd
invd
clts
rsm
ud2
