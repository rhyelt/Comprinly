#!/bin/sh
cd "$(dirname "$0")/.." || exit 1
T=${TMPDIR:-/tmp}/cly_xl.$$
mkdir -p "$T"
bad=0
./cly linux tests/xlink/a64.cly -obj -q -out "$T/a.o" && llvm-mc -triple=aarch64 -filetype=obj tests/xlink/a64_b.s -o "$T/b.o" && ld.lld -o "$T/x" "$T/a.o" "$T/b.o" -e start && python3 tests/emu/elf_emu.py "$T/x"
[ $? = 42 ] || { echo "xlink arm64 FAIL"; bad=1; }
./cly linux tests/xlink/rv.cly -obj -q -out "$T/r.o" && llvm-mc -triple=riscv64 -mattr=+c,+d,+f -target-abi=lp64d -filetype=obj tests/xlink/rv_b.s -o "$T/rb.o" && ld.lld -o "$T/rx" "$T/r.o" "$T/rb.o" -e start && python3 tests/emu/elf_emu.py "$T/rx"
[ $? = 42 ] || { echo "xlink riscv FAIL"; bad=1; }
rm -rf "$T"
exit $bad
