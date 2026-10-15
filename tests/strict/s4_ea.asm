bits 64
section .text
lbl: mov rax, [rbx+rcx*3]
    add eax, 100000000000
    mov al, 300
    jmp short far_away
    times 200 nop
far_away: ret
