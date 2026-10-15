cases["basic"]="""bits 64
section .text
global _start
_start: mov eax, 60
 xor edi, edi
 syscall
"""
cases["calls"]="""bits 64
extern puts, printf
global main:function
section .data
msg: db "hi",0
section .text
main: lea rdi,[rel msg]
 call puts wrt ..plt
 call printf
 mov rax,[rel puts wrt ..got]
 mov rax,[rel msg]
 mov rbx, msg
 mov ebx, msg
 ret
"""
cases["bss"]="""bits 64
section .bss
buf: resb 100
section .data
p: dq buf
section .text
global f
f: mov rax, buf
 mov eax, [buf]
 mov [rel buf], eax
"""
cases["common"]="""bits 64
common c 16
common d 8:8
extern e
global g
g: mov rax, [rel c]
"""
cases["types"]="""bits 64
global a:data, b:function, c:data 4, d:function 3
a: dd 1
b: nop
c: dd 2
d: nop
"""
cases["rodata"]="""bits 64
section .rodata
s: db "x"
section .text.foo exec
 nop
section .mydata write
 dd 1
"""
cases["extern_data"]="""bits 64
extern v
section .text
 mov eax,[rel v]
 mov rax,[v]
 mov rax, v
 cmp dword [v], 1
 add rax, v
"""
cases["got"]="""bits 64
extern v
 mov rax,[rel v wrt ..got]
 lea rax,[rel v wrt ..gotpc]
"""
cases["local"]="""bits 64
f: nop
.l: jmp .l
g: jmp f
 call g
 call f
"""
cases["data_relocs"]="""bits 64
extern x
section .data
a: dq x
b: dd x
c: dq a
d: dq x+8
e: dq a-b
"""
cases["bits32"]="""bits 32
extern x
section .text
 mov eax, x
 call x
 mov eax,[x]
 jmp x
"""
cases["align"]="""bits 64
section .text align=16
 nop
section .data align=32
 db 1
 align 8
 db 2
"""
cases["absrel"]="""bits 64
section .text
 mov rax,[0x1000]
 mov eax,[0x1000]
 mov rax,[abs 0x1000]
 mov rax,[fs:0]
 mov rax,[gs:rax]
"""
cases["tls"]="""bits 64
extern t
 mov rax,[fs:0]
 mov rax,[rel t wrt ..gottpoff]
"""
cases["sect_follow"]="""bits 64
section .a progbits alloc exec nowrite align=16
 nop
section .b nobits alloc write
 resb 4
"""
cases["times_data"]="""bits 64
section .data
 times 4 dq 0
 times 16 db 'a'
x: times 3 dd x
"""
