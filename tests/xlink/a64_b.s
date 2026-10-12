.text
.globl helper
helper:
    add x0, x0, #2
    ret
.data
.globl counter
.balign 8
counter: .quad 10
