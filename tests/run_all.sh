#!/bin/sh
cd "$(dirname "$0")/.." || exit 1
fail=0
T=${TMPDIR:-/tmp}/cly_check.$$
mkdir -p "$T"
note() { printf '%-28s %s\n' "$1" "$2"; }

for ex in examples/*.cly; do
    n=$(basename "$ex" .cly)
    for k in linux windows mac bare; do
        for m in -m32 -m64; do
            if ./cly $k "$ex" $m -bin -q -out "$T/$n.$k" 2>"$T/err"; then :; else
                note "$n/$k$m" "FAIL: $(head -1 "$T/err")"; fail=1
            fi
        done
    done
done
if ./cly windows examples/print.cly -m32 -coff -q -out "$T/c.obj" && [ "$(head -c2 "$T/c.obj" | od -An -tx1 | tr -d ' ')" = "4c01" ]; then note "coff object" ok; else note "coff object" FAIL; fail=1; fi
if ./cly windows examples/print.cly -m64 -coff -q -out "$T/c.obj" && [ "$(head -c2 "$T/c.obj" | od -An -tx1 | tr -d ' ')" = "6486" ]; then note "coff64 object" ok; else note "coff64 object" FAIL; fail=1; fi
if ./cly linux examples/print.cly -m64 -obj -q -out "$T/e.o" && [ "$(head -c5 "$T/e.o" | od -An -tx1 | tr -d ' ')" = "7f454c4602" ]; then note "elf64 object" ok; else note "elf64 object" FAIL; fail=1; fi
note "examples build" "done"

if [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ]; then
    for ex in print fact; do
        ./cly linux examples/$ex.cly -m64 -q -out "$T/p" && "$T/p" >"$T/o" 2>/dev/null
        case $ex in print) want="Hello, World!";; fact) want=3628800;; esac
        if [ "$(cat "$T/o")" = "$want" ]; then note "linux64 run $ex" ok; else note "linux64 run $ex" FAIL; fail=1; fi
    done
    if ./cly linux examples/print.cly -m32 -q -out "$T/p" && "$T/p" >"$T/o" 2>/dev/null && [ "$(cat "$T/o")" = "Hello, World!" ]; then note "linux32 run" ok; else note "linux32 run" skipped; fi
else
    note "linux run" skipped
fi

if python3 -c "import unicorn" 2>/dev/null; then
    ./cly windows examples/print.cly -m32 -q -out "$T/w.exe"
    python3 tests/emu/pe_emu.py "$T/w.exe" | grep -q "Hello, World" && note "windows32 emu" ok || { note "windows32 emu" FAIL; fail=1; }
    ./cly windows examples/print.cly -m64 -q -out "$T/w.exe"
    python3 tests/emu/pe64_emu.py "$T/w.exe" | grep -q "Hello, World" && note "windows64 emu" ok || { note "windows64 emu" FAIL; fail=1; }
    ./cly mac examples/print.cly -m32 -q -out "$T/m"
    python3 tests/emu/mac_emu.py "$T/m" | grep -q "Hello, World" && note "mac32 emu" ok || { note "mac32 emu" FAIL; fail=1; }
    ./cly mac examples/print.cly -m64 -q -out "$T/m"
    python3 tests/emu/mac64_emu.py "$T/m" | grep -q "Hello, World" && note "mac64 emu" ok || { note "mac64 emu" FAIL; fail=1; }
    for m in -m32 -m64; do
        ./cly bare examples/print.cly $m -q -out "$T/b"
        python3 tests/emu/bare_emu.py "$T/b" | grep -q "Hello, World" && note "bare$m emu" ok || { note "bare$m emu" FAIL; fail=1; }
    done
else
    note "emulators" "skipped (no unicorn)"
fi

if [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ]; then
    out=$(sh tests/run_opt.sh 2>&1)
    if echo "$out" | grep -qE "DIFFERENT|failed"; then echo "$out" | grep -E "DIFFERENT|failed"; fail=1; else note "optimizer cases" ok; fi
fi
if python3 -c "import unicorn" 2>/dev/null; then
    r=$(python3 tests/opt_fuzz64.py 1 60 | tail -1); echo "$r" | grep -q "bad= 0" && note "optimizer fuzz 64" ok || { note "optimizer fuzz 64" "FAIL: $r"; fail=1; }
    r=$(python3 tests/opt_fuzz32.py 1 60 | tail -1); echo "$r" | grep -q "bad= 0" && note "optimizer fuzz 32" ok || { note "optimizer fuzz 32" "FAIL: $r"; fail=1; }
fi

if command -v nasm >/dev/null 2>&1; then
    out=$(sh tests/run_nasm_dir.sh 2>&1)
    if echo "$out" | grep -qE "DIFF|CLY REJECTS"; then echo "$out" | grep -E "DIFF|CLY REJECTS"; fail=1; else note "nasm compare" ok; fi
    python3 tests/cmp_ea.py | tail -1 | grep -q "diff 0" && note "ea compare" ok || { note "ea compare" FAIL; fail=1; }
    r=$(NV=2 SHOW=0 python3 tests/cmp_evex.py | tail -1)
    if echo "$r" | grep -qE "diffs 0 cly rejects 0 "; then note "avx compare" ok; else note "avx compare" "FAIL: $r"; fail=1; fi
else
    note "nasm compare" "skipped (no nasm)"
fi

rm -rf "$T"
[ $fail = 0 ] && echo "all good" || echo "failures"
exit $fail
