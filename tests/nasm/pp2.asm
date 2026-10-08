bits 32
%macro proc 1
 %push proc
 %define %$name %1
 %1:
 push ebp
 mov ebp, esp
 %assign %$argoff 8
%endmacro
%macro endproc 0
 %ifnctx proc
  %error wrong ctx
 %endif
 pop ebp
 ret
 %pop
%endmacro
proc foo
 nop
endproc
proc bar
 nop
endproc
%define a1 1
%define a2 2
%ifdef a1
 %ifdef a2
  db 12
 %else
  db 10
 %endif
%else
 db 0
%endif
%if a1 + a2 == 3
db 33
%endif
%ifndef zzz
db 44
%endif
%ifdef zzz
db 1
%elifdef a1
db 55
%endif
%xdefine v1 a1
%undef a1
%define a1 9
db v1, a1
%macro cc 2
 %push c
 %repl d
 %pop
%endmacro
%macro lm 1
 %push lm
 %define %$v %1
 db %$v
 %pop
%endmacro
lm 5
lm 6
%macro twice 1
 %rep 2
  %1
 %endrep
%endmacro
twice nop
twice {db 1, 2}
%macro withargs 3
 %local x
 db %1, %2, %3
%endmacro
%define ADD3(a,b,c) ((a)+(b)+(c))
db ADD3(1,2,3)
%define VA(a, ...) a, __VA_ARGS__
db VA(1, 2, 3)
%define vv(...) __VA_ARGS__
db vv(7,8)
%define cnt(x) %strlen(x)
db cnt("abcd")
%define STR(x) #x
%assign k 3
db k * k
%define t1 3
%define t2 t1 + 1
db t2 * 2
%defalias al1 a1
db al1
%define m(x) x
db m(m(4))
%assign q 1
%rep 3
 %assign q q * 2
%endrep
db q
%macro mm 1-*
 %rep %0
  db %1
  %rotate 1
 %endrep
%endmacro
mm 9, 8, 7
%macro mm2 2-3 +
 db %0
%endmacro
mm2 1, 2
mm2 1, 2, 3
%macro neg1 1
 %if %1 < 0
  db 1
 %else
  db 0
 %endif
%endmacro
neg1 -5
neg1 5
%macro lbl 0
%%top:
 nop
 jmp %%top
%endmacro
lbl
lbl
%macro dd2 1-2 0
 dd %1, %2
%endmacro
dd2 5
dd2 5, 6
%ifidni ABC, abc
db 1
%endif
%if 'a' == 97
db 2
%endif
%if (1 << 3) == 8
db 3
%endif
%ifnidn a, b
db 4
%endif
%ifempty
db 5
%endif
%ifenv NOSUCHVAR
db 6
%endif
%warning test warning
db __LINE__
%line 100+1 foo.asm
db __LINE__
db __FILE__
%assign big 0x7fffffff
dd big
%define sq(x) (x)*(x)
dd sq(1+2)
%rep 0
db 99
%endrep
%macro e0 0
%endmacro
e0
db 0xAA
%macro cond 1
 %ifnum %1
  db %1
 %else
  db 0
 %endif
%endmacro
cond 5
cond abc
