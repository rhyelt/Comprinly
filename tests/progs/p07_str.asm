default rel
global main
extern strlen, strcpy, strcmp, memcpy, memset, puts, printf, atoi
section .data
src db "reverse me", 0
num db "1234", 0
fmt db "len=%d cmp=%d atoi=%d", 10, 0
section .bss
buf resb 64
tmp resb 64
section .text
main:
    push rbx
    push r12
    push r13
    lea rdi, [buf]
    lea rsi, [src]
    call strcpy
    lea rdi, [buf]
    call strlen
    mov rbx, rax
    lea rdi, [tmp]
    mov esi, 'x'
    mov edx, 63
    call memset
    lea rdi, [tmp]
    lea rsi, [buf]
    mov rdx, rbx
    call memcpy
    lea rdi, [tmp]
    call puts
    lea rdi, [num]
    call atoi
    mov r12d, eax
    lea rdi, [buf]
    lea rsi, [tmp]
    call strcmp
    xor edx, edx
    test eax, eax
    setg dl
    mov r13d, edx
    lea rdi, [fmt]
    mov esi, ebx
    mov edx, r13d
    mov ecx, r12d
    xor eax, eax
    call printf
    pop r13
    pop r12
    pop rbx
    xor eax, eax
    ret
