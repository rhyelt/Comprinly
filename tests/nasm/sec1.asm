bits 32
org 0x100
section .text
start:
  mov eax, msg
  mov ebx, [counter]
  jmp done
section .data
msg db "hi", 0
val dd 5
section .bss
counter resd 1
buf resb 16
section .text
done: ret
  mov eax, counter
  mov ecx, buf
section .data
more db 1,2,3
