bits 16
start: mov ax, start
dw start, end - start, $ - start, $$ - start
end:
db "hello", 0
l2: dw l2, l3, l3 - l2
l3: dd l2, l3
equ1 equ 5
equ2 equ equ1 * 2 + 1
db equ1, equ2
%define X 5
db X
times 10 - ($ - $$) % 10 db 0
align 16
db 1
align 16, db 0x90
db 2
alignb 8
dw 3
align 4, nop
db 0x90
times 100 - ($-$$) db 0x90
resb 3
dd $
