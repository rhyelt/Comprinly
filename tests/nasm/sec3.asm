bits 32
struc point
 .x resd 1
 .y resd 1
 .name resb 8
endstruc
struc rec
 r_id: resw 1
 r_len: resb 1
 r_data: resb 3
endstruc
section .text
 mov eax, point.y
 mov ebx, [esi + point.x]
 mov ecx, point_size
 mov edx, rec_size
 mov al, [edi + r_len]
section .data
p1: istruc point
 at point.x, dd 5
 at point.y, dd 6
 at point.name, db "abc", 0
iend
p2: istruc rec
 at r_id, dw 1
 at r_len, db 2
 at r_data, db 3,4,5
iend
absolute 0x1000
abs1 resd 1
abs2 resw 2
section .text
 mov eax, abs2
 mov ebx, abs1
