#!/bin/bash
cd "$(dirname "$0")/.."
fail=0
T=$(mktemp -d)
for f in tests/strict/*.asm; do
  n=$(basename $f .asm)
  nasm -f elf64 $f -o $T/n.o 2>/dev/null; nr=$?
  ./cly linux $f -obj -nasm -q -out $T/c.o 2>/dev/null; cr=$?
  if [ $nr -eq 0 ] && [ $cr -eq 0 ]; then
    if cmp -s <(objcopy -O binary -j .text $T/n.o /dev/stdout) <(objcopy -O binary -j .text $T/c.o /dev/stdout); then echo "$n: ok"; else echo "$n: CODE DIFFERS"; fail=1; fi
  elif [ $nr -ne 0 ] && [ $cr -ne 0 ]; then echo "$n: ok (both reject)"
  else echo "$n: MISMATCH nasm=$nr cly=$cr"; fail=1; fi
done
for f in tests/nasm/*.asm; do
  n=$(basename $f .asm)
  nasm -f bin $f -o $T/n.bin 2>/dev/null; nr=$?
  ./cly linux $f -flat -nasm -q -out $T/c.bin 2>/dev/null; cr=$?
  if [ $nr -eq 0 ] && [ $cr -eq 0 ]; then cmp -s $T/n.bin $T/c.bin || { echo "$n: DIFF"; fail=1; }
  elif [ $nr -eq 0 ]; then echo "$n: CLY REJECTS"; fail=1; fi
done
rm -rf $T
exit $fail
