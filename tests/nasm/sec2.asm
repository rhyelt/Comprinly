bits 16
org 0x7c00
start:
  cli
  xor ax, ax
  mov ds, ax
  mov si, msg
.l: lodsb
  or al, al
  jz .d
  mov ah, 0x0e
  int 0x10
  jmp .l
.d: hlt
  jmp .d
msg db "Hello", 0
times 510-($-$$) db 0
dw 0xaa55
