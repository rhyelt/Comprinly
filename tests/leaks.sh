#!/bin/bash
cd "$(dirname "$0")/.."
C=./cly
fail=0
T=$(mktemp -d)
for f in tests/leaks/l*.asm; do
  n=$(basename $f .asm)
  exp=$(head -1 $f | sed 's/; expect //')
  flag=""
  grep -q "bits 32" $f && flag="-m32"
  $C linux $f $flag -out $T/$n -fixsrc >/dev/null 2>$T/$n.err
  got=$(grep -c "potential memory leak" $T/$n.err)
  if [ "$got" != "$exp" ]; then echo "$n: WARN COUNT got=$got expected=$exp"; fail=1; head -5 $T/$n.err; continue; fi
  if [ "$exp" = "0" ]; then echo "$n: ok (no warning)"; continue; fi
  if [ ! -x $T/${n}_fixed ]; then echo "$n: NO FIXED BUILD"; fail=1; continue; fi
  a=$($T/$n </dev/null 2>&1; echo "rc=$?")
  b=$($T/${n}_fixed </dev/null 2>&1; echo "rc=$?")
  if [ "$a" != "$b" ]; then echo "$n: FIXED OUTPUT DIFFERS"; echo "$a"; echo "$b"; fail=1; continue; fi
  $C linux $T/${n}_fixed.asm $flag -out $T/${n}_f2 >/dev/null 2>$T/$n.err2
  if grep -q "potential memory leak" $T/$n.err2; then echo "$n: FIX STILL LEAKS"; cat $T/$n.err2 | head -5; fail=1; continue; fi
  echo "$n: ok (warned, fixed)"
done
rm -rf $T
exit $fail
