#!/bin/sh
cd "$(dirname "$0")/.." || exit 1
T=${TMPDIR:-/tmp}/cly_log.$$
mkdir -p "$T"
fail=0
./cly linux examples/print.cly -m64 -q -log=$T/l.txt -out $T/p || fail=1
for k in preprocess assemble link write; do grep -q "\] $k" $T/l.txt || { echo "log missing $k"; fail=1; }
done
./cly linux examples/print.cly -m64 -q -out $T/p 2>$T/e || fail=1
[ -s $T/e ] && { echo "log output without -log"; fail=1; }
printf 'x db 1\n' > $T/i.inc
printf '%%include "i.inc"\nstart: mov eax,1\n' > $T/d.asm
./cly linux $T/d.asm -m32 -nolibc -noleak -q -MD -out $T/d || fail=1
grep -q "i.inc" $T/d.d || { echo "dep rule missing include"; fail=1; }
printf 'start: mov al, 300\n' > $T/w.asm
./cly linux $T/w.asm -nolibc -Werror -out $T/w >/dev/null 2>&1 && { echo "Werror did not fail"; fail=1; }
rm -rf "$T"
exit $fail
