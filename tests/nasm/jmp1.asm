bits 32
start:
  jmp short a
  jmp a
  jmp near a
a: jz b
  jnz short b
  jmp b
  call c
  call b
  loop a
  jcxz b
  jecxz b
  times 200 nop
b: jmp start
  jz start
  jmp short $
  jmp $+2
  jmp $-1
c: ret
  call near c
  call far [c]
  jmp far [c]
  jmp 0x10:0x12345678
  call 0x10:0x1234
  jmp dword 0x10:0x1234
  jmp word 0x10:0x1234
  jmp [c]
  jmp eax
  call [eax+4]
  call word [eax]
  jmp dword [eax]
  ja d
  times 130 nop
d: jbe d
  jb d
  jae e
e:
