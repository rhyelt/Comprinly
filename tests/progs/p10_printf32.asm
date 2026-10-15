bits 32
extern printf
global main
section .data
fmt db "n=%d s=%s c=%c x=%x", 10, 0
sstr db "abc", 0
section .text
main:
    push ebp
    mov ebp, esp
    push 255
    push 'q'
    push sstr
    push 99
    push fmt
    call printf
    add esp, 20
    mov eax, 0
    pop ebp
    ret
