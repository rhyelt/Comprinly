#!/bin/bash
cd "$(dirname "$0")/.."
C=./cly
fail=0
T=$(mktemp -d)
for f in tests/progs/p*.asm; do
  n=$(basename $f .asm)
  flag=""
  grep -q "bits 32" $f && flag="-m32"
  if ! $C linux $f -q $flag -out $T/$n 2>$T/$n.err; then echo "$n: BUILD FAIL: $(head -2 $T/$n.err | tr '\n' ' ')"; fail=1; continue; fi
  if [ -f tests/progs/$n.in ]; then inp=tests/progs/$n.in; else printf 'line one\nline two\n' > $T/in; inp=$T/in; fi
  ($T/$n < $inp; echo "rc=$?") > $T/$n.got 2>&1
  if cmp -s $T/$n.got tests/progs/$n.exp; then echo "$n: ok ($(stat -c %s $T/$n) bytes)"; else echo "$n: WRONG OUTPUT"; fail=1; fi
done
rm -rf $T
exit $fail
