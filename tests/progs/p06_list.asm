default rel
extern malloc, free, printf
global main
section .data
fmt db "node %d", 10, 0
sumfmt db "sum=%d", 10, 0
section .text
main:
    push rbx
    push r12
    push r13
    xor r12, r12
    mov ebx, 1
.build:
    mov edi, 16
    call malloc
    mov [rax], ebx
    mov [rax+8], r12
    mov r12, rax
    inc ebx
    cmp ebx, 6
    jl .build
    xor r13d, r13d
    mov rbx, r12
.walk:
    test rbx, rbx
    jz .done
    mov esi, [rbx]
    add r13d, esi
    lea rdi, [fmt]
    xor eax, eax
    call printf
    mov rbx, [rbx+8]
    jmp .walk
.done:
    lea rdi, [sumfmt]
    mov esi, r13d
    xor eax, eax
    call printf
.free:
    test r12, r12
    jz .out
    mov rdi, r12
    mov r12, [r12+8]
    call free
    jmp .free
.out:
    pop r13
    pop r12
    pop rbx
    xor eax, eax
    ret
