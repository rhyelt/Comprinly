.text
.globl helper
helper:
    addi a0, a0, 2
    ret
.data
.globl counter
.balign 8
counter: .quad 10
