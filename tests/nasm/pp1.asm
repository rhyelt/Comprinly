bits 32
%define A 5
%define B(x) x+1
%define C(x,y) ((x)*(y))
%xdefine D A+1
%define A 7
db A, B(2), C(3,4), D
%undef A
%ifdef A
db 1
%else
db 2
%endif
%assign n 10
%assign n n+1
db n
%macro m1 0
 db 1
%endmacro
m1
%macro m2 1
 db %1
%endmacro
m2 5
%macro m3 2-3 9
 db %1, %2, %3
%endmacro
m3 1, 2
m3 1, 2, 3
%macro m4 1+
 db %1
%endmacro
m4 1, 2, 3
%macro m6 1-*
 %rep %0
  db %1
  %rotate 1
 %endrep
%endmacro
m6 1,2,3,4
%macro m7 0
 %%l: jmp %%l
%endmacro
m7
m7
%if 1 == 1
db 11
%elif 2
db 12
%else
db 13
%endif
%rep 3
db 7
%endrep
%if 1+1 == 2 && 3 > 2
db 99
%endif
%ifidn abc, abc
db 1
%endif
%ifnum 5
db 2
%endif
%ifstr "x"
db 3
%endif
%ifid foo
db 4
%endif
%ifmacro m1
db 5
%endif
%ifctx foo
db 6
%else
db 7
%endif
%push foo
%define %$x 5
db %$x
%pop
%strlen sl "hello"
db sl
%substr ss "hello" 2, 3
db ss
%defstr ds abc
db ds
%strcat sc "ab", "cd"
db sc
%deftok dt1 'ab'
%define q(x) %str(x)
db q(hello)
%macro m8 1
 %ifidn %1, one
  db 1
 %elifidn %1, two
  db 2
 %else
  db 3
 %endif
%endmacro
m8 one
m8 two
m8 three
%define s1(a, b) a + b
db s1(1, 2)
%define cat(a,b) a %+ b
%define foo1 77
db cat(foo, 1)
%macro m9 1
 %assign i 0
 %rep %1
  db i
  %assign i i+1
 %endrep
%endmacro
m9 4
%macro m10 1-2 5
 db %1, %2
%endmacro
m10 1
m10 1, 2
%macro m11 0-1 ,
 db %0
%endmacro
m11
m11 a
%macro m12 3
 db %1+%2+%3
%endmacro
m12 1,2,3
%macro m13 1
 %1 eax, ebx
%endmacro
m13 add
m13 mov
%idefine IFoo 4
db ifoo, IFOO
%define x1 x2
%define x2 3
db x1
%define EMPTY
db 1 EMPTY
%macro m14 2
 db %1, %2
%endmacro
m14 {1,2}, 3
%macro mcc 1
 j%1 $
%endmacro
mcc z
mcc nz
%macro mcd 1
 %if %1
  db 1
 %endif
%endmacro
mcd 1
mcd 0
%define MAC(a) a
%define MAC2 MAC(5)
db MAC2
%macro retm 0
 %exitrep
%endmacro
%rep 10
 db 1
 %exitrep
%endrep
db __LINE__
db __BITS__
%if __NASM_MAJOR__ >= 2
db 8
%endif
