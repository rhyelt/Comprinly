bits 32
section .text align=16
a: nop
section .data align=8
b: db 1
section .text
c: nop
section code2 align=32 follows=.data
d: nop
section .bss
e: resb 7
section .text
 mov eax, a
 mov eax, b
 mov eax, c
 mov eax, d
 mov eax, e
 mov eax, $$
 mov eax, $
 mov eax, section.code2.start
 mov eax, section..text.start
 dd e - a
 dd d - b
