bits 64
section .text
global _start
_start:
    mov eax, Foo
    call undefined_fn
foo: ret
