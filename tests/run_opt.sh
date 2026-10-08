#!/bin/sh
cd "$(dirname "$0")/.." || exit 1
fail=0
T=${TMPDIR:-/tmp}/cly_opt.$$
mkdir -p "$T"
for f in tests/opt/*.cly; do
    n=$(basename "$f" .cly)
    [ "$n" = lib ] && continue
    for m in -m64 -m32; do
        ./cly linux "$f" $m -q -O0 -out "$T/a0" 2>"$T/e0"; r0=$?
        ./cly linux "$f" $m -q -out "$T/a1" 2>"$T/e1"; r1=$?
        if [ $r0 != 0 ] || [ $r1 != 0 ]; then
            if [ $m = -m32 ] && [ $r0 = $r1 ]; then continue; fi
            echo "$n$m: build failed ($r0/$r1): $(head -1 "$T/e0") $(head -1 "$T/e1")"; fail=1; continue
        fi
        "$T/a0" >"$T/o0" 2>&1; c0=$?
        "$T/a1" >"$T/o1" 2>&1; c1=$?
        if [ $c0 = $c1 ] && cmp -s "$T/o0" "$T/o1"; then echo "$n$m: ok"; else echo "$n$m: DIFFERENT"; fail=1; fi
    done
done
rm -rf "$T"
exit $fail
