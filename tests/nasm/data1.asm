bits 32
db 1, 2, 3, 'abc', "de", `f\n\t\x41\101é`, 0xff, -1, 255
dw 1, 0x1234, -1, 'ab', "abc", 65535
dd 1, 0x12345678, -1, 'abcd', "abcde", 1.5, 3.14159, 1e10, -0.0, __Infinity__, __QNaN__
dq 1, 0x123456789abcdef0, -1, 1.5, 3.14159265358979, 1e300, __Infinity__, 'abcdefgh'
dt 1.5, 3.14159265358979323846, -2.5e-10
do 1.5
dy 2.5
dh 1.5
dd __float32__(1.25)
dq __float64__(2.5)
dw __float16__(1.5)
dw __bfloat16__(1.5)
dt __float80m__(1.0), __float80e__(1.0)
dq __float64__(1.5)
dd 1.0e-45, 3.4028235e38, 1.17549435e-38
resb 4
db 1
times 5 db 7
times 3 dw 1, 2
times 2 times 2 db 9
db "x" 
dd 10 dup (1)
db 3 dup (1, 2)
dw 'a', 'b'
db 0xFF,0x7F
dd 0x7fffffff, 0xffffffff, 0x80000000
db 1+2*3, (1+2)*3, 10/3, 10%3, -10/3, -10%3, 10//3, 10%%3, -10//3, -10%%3
db 1<<4, 256>>4, 0xf0 & 0x3c, 0xf0 | 0x0f, 0xf0 ^ 0xff, ~0xf0 & 0xff
db 5 > 3, 1 && 0, 1 || 0, !5
db 3 ? 1 : 2
dd 0b1010, 0o17, 017, 0x1F, 1Fh, 1010b, 17o, 0q17, 0d99, 99d, 0xDEADBEEF, $$, $
db 'a' + 1
dd ilog2e(1000), ilog2f(1000), ilog2c(1000), ilog2w(1000)
dd seg 0
dw 1,2,3
dd 100000 * 100000
dq 100000 * 100000
dq 1<<40
dq -1
dq 0xffffffffffffffff
dd 3.0/2.0
db 1.0
