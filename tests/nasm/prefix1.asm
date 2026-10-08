bits 32
lock add [eax], ebx
rep movsb
repe cmpsb
repne scasb
repz stosw
repnz lodsd
a16 mov eax, [bx]
a32 mov eax, [eax]
o16 mov eax, ebx
o32 mov ax, bx
mov eax, [es:ebx]
mov eax, [fs:ebx+4]
es mov eax, [ebx]
cs lodsb
rep movsd
lock inc dword [eax]
xacquire lock add [eax], ebx
xrelease mov [eax], ebx
bnd jmp eax
bnd ret
fs mov eax, [0]
mov eax, [gs:0]
movsb
movsw
movsd
cmpsd
a16 movsb
a16 rep movsb
rep a16 movsb
o16 pushad
pushaw
pusha
pushad
popa
popad
pushfd
popfw
pushf
popf
cbw
cwde
cwd
cdq
o16 cbw
cdqe
