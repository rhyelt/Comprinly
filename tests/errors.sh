#!/bin/sh
C=${CLY:-./cly}
T=$(mktemp -d)
bad=0
chk() {
    cpu=$1; src=$2; want=$3
    printf "$src" > $T/e.cly
    out=$($C linux $T/e.cly -cpu $cpu -q -out $T/o 2>&1)
    rc=$?
    case "$out" in *"$want"*) ;; *) echo "FAIL [$cpu] wanted '$want' got: $out"; bad=$((bad+1));; esac
    [ $rc -eq 1 ] || { echo "FAIL [$cpu] exit code $rc for '$want'"; bad=$((bad+1)); }
}
chk arm64 'start:\n mov x0, 70000000000000000000\n' 'too large'
chk x86-64 'start:\n mov eax, 0x1ffffffffffffffff\n' 'too large'
chk arm64 'start:\n mvo x0, 1\n' 'did you mean'
chk rv64 'start:\n addd a0, a0, a1\n' 'did you mean'
chk arm64 'start:\n jmp start\n' 'x86 instruction'
chk x86-64 'start:\n ldr x0, [x1]\n' 'ARM64 instruction'
chk rv64 'start:\n ldr x0, [x1]\n' 'ARM64 instruction'
chk x86-64 '%%macro m 1\n nop\n%%endmacro\nstart:\n m\n' 'takes 1 parameter'
chk x86-64 '%%macro m 1\n nop\n%%endmacro\nstart:\n m 1, 2\n' 'takes 1 parameter'
chk x86-64 '%%if 1\nstart: ret\n' 'never closed'
chk x86-64 'start:\nstart:\n ret\n' 'first defined at'
chk x86-64 'start:\n mov eax, msgg\nmsg db 1\n' 'did you mean `msg'
chk arm64 'start:\n adr x0, msgg\nmsg: db 1\n' 'did you mean `msg'
chk x86-64 'start:\n jmp short away\n times 300 nop\naway: ret\n' 'bytes away'
chk arm64 'start:\n add x0, x1\n' 'expected 3 to 4 operands'
chk arm64 'start:\n ldr x0, [x1, 5000000]\n' 'out of range'
chk rv64 'start:\n addi a0, a0, 5000\n' 'does not fit'
chk rv64 'start:\n lw a0, 4(a1\n' 'junk'
chk x86-64 'start:\n db "abc\n' 'unterminated'
chk x86-64 'start:\n mov eax, 1/0\n' 'division by zero'
wchk() {
    cpu=$1; src=$2; want=$3; k=${4:-linux}
    printf "$src" > $T/e.cly
    out=$($C $k $T/e.cly -cpu $cpu -out $T/o 2>&1)
    case "$out" in *"$want"*) ;; *) echo "FAIL warn [$cpu] wanted '$want' got: $out"; bad=$((bad+1));; esac
}
gchk() {
    src=$1; want=$2
    printf "$src" > $T/e.cly
    out=$($C linux $T/e.cly -q -out $T/o 2>&1)
    case "$out" in *"$want"*) ;; *) echo "FAIL guess wanted '$want' got: $out"; bad=$((bad+1));; esac
}
wchk arm64 'start:\n mov x8, 999\n svc 0\n' 'not a Linux system call'
wchk arm64 'start:\n mov x8, 93\n svc 1\n' 'ignores the `svc'"'"' number'
wchk rv64 'start:\n li a7, 500\n ecall\n' 'not a Linux system call'
wchk x86-64 'SYS equ 400\nstart:\n mov eax, SYS\n syscall\n' 'not a Linux system call'
wchk x86-64 'start:\n int 0x80\n' 'int 0x80'
gchk 'start:\n mov w0, 1\n mov eax, 2\n' 'chosen from `w0'"'"
gchk 'start:\n mov rax, 1\n add x0, x1, x2\n' 'ARM64 register'
gchk 'start:\n li a0, 1\n mov eax, 2\n' 'x86 instruction'
rm -rf $T
if [ $bad -eq 0 ]; then echo "error messages ok"; else echo "$bad error tests failed"; exit 1; fi
