section .data
fmt: db "%d squared is %d", 10, 0
section .text
extern printf
global main
main:
    push rbx
    mov ebx, 1
.next:
    mov esi, ebx
    mov edx, ebx
    imul edx, ebx
    mov rdi, fmt
    xor eax, eax
    call printf wrt ..plt
    inc ebx
    cmp ebx, 6
    jle .next
    pop rbx
    xor eax, eax
    ret
