#!/bin/bash
fail=0
for f in tests/nasm/*.asm; do
  n=$(basename $f .asm)
  nasm -f bin $f -o /tmp/nt_$n.bin 2>/tmp/nt_$n.nerr
  nr=$?
  ./cly linux $f -flat -q -out /tmp/ct_$n.bin 2>/tmp/ct_$n.cerr
  cr=$?
  if [ $nr -ne 0 ] && [ $cr -ne 0 ]; then echo "$n: both reject"; continue; fi
  if [ $nr -ne 0 ]; then echo "$n: nasm rejects ($(head -1 /tmp/nt_$n.nerr)), cly rc=$cr"; continue; fi
  if [ $cr -ne 0 ]; then echo "$n: CLY REJECTS: $(head -3 /tmp/ct_$n.cerr)"; fail=1; continue; fi
  if cmp -s /tmp/nt_$n.bin /tmp/ct_$n.bin; then echo "$n: ok"; else echo "$n: DIFF"; fail=1; fi
done
exit $fail
