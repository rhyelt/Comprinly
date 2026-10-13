#!/bin/sh
C=${CLY:-./cly}
T=$(mktemp -d)
bad=0
chk() {
    printf "bits 64\n$1\n" > $T/p.cly
    $C bare $T/p.cly -flat -m64 -q -out $T/p.bin 2>/dev/null
    got=$(od -An -tx1 -v $T/p.bin | tr -d ' \n')
    [ "$got" = "$2" ] || { echo "FAIL '$1' wanted $2 got $got"; bad=$((bad+1)); }
}
chk 'cmp ebp, -1' '83fdff'
chk 'xor ebp, ebp' '31ed'
chk 'test ebp, ebp' '85ed'
chk 'and ebp, 15' '83e50f'
chk 'add ebp, 1' '83c501'
chk 'neg ebp' 'f7dd'
chk 'inc ebp' 'ffc5'
chk 'lea ebp, [rax+1]' '8d6801'
chk 'mov ebp, 5' 'bd05000000'
chk 'sub esp, 16' '4883ec10'
chk 'mov ebp, esp' '4889e5'
chk 'mov eax, [ebp-8]' '8b45f8'
chk 'push ebp' '55'
chk 'lea esp, [esp+8]' '488d642408'
rm -rf $T
[ $bad -eq 0 ] && echo "promotion rules ok" || { echo "$bad failed"; exit 1; }
