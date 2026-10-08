bits 32
global _start
extern foo
_start:
  mov eax, 1
  jmp l2
l2: times 3 nop
  align 8
  nop
  align 4, db 0xcc
  db 1
  alignb 4
  nop
x equ 5
y equ x * 2
z equ $ - _start
  dd x, y, z
  mov eax, [rel here]
here: dd 1
  default rel
  mov eax, [here]
  default abs
  mov eax, [here]
  cpu 386
  cpu pentium
  cpu all
  [bits 16]
  mov ax, 1
  [bits 32]
  use16
  mov ax, 1
  use32
  mov eax, 1
