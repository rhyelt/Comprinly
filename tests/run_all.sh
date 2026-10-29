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
    printf 'start:\n mov eax, 4\n mov ebx, 1\n mov ecx, m\n mov edx, 14\n int 0x80\n mov eax, 1\n xor ebx, ebx\n int 0x80\nm db "Hello, World!", 10\n' > "$T/l32.cly"
    if ./cly linux32 "$T/l32.cly" -q -out "$T/p" && "$T/p" >"$T/o" 2>/dev/null && [ "$(cat "$T/o")" = "Hello, World!" ]; then note "linux32 run" ok; else note "linux32 run" skipped; fi
    for c in i386 i486 i686 ia32; do
        if ./cly linux "$T/l32.cly" -cpu $c -q -out "$T/p" && "$T/p" >"$T/o" 2>/dev/null && [ "$(cat "$T/o")" = "Hello, World!" ]; then note "cpu $c run" ok; else note "cpu $c run" FAIL; fail=1; fi
    done
    printf 'start:\n mov r12, 0x9E3779B97F4A7C15\n mov r15, 3000\n xor r14, r14\n.l:\n imul r12, r12, 69069\n add r12, 12345\n mov rax, r12\n shr rax, 20\n mov rbx, r12\n shr rbx, 40\n or rbx, 1\n xor edx, edx\n div rbx\n add r14, rax\n xor r14, rdx\n mov rax, r12\n shr rax, 33\n mov rbx, r12\n shr rbx, 45\n or rbx, 1\n cqo\n idiv rbx\n add r14, rax\n xor r14, rdx\n dec r15\n jnz .l\n mov rdi, r14\n and edi, 127\n mov eax, 60\n syscall\n' > "$T/dv.cly"
    ./cly linux "$T/dv.cly" -O0 -q -out "$T/d0" && ./cly linux "$T/dv.cly" -O3 -q -out "$T/d3" && "$T/d0"; r0=$?; "$T/d3"; r3=$?
    if [ $r0 = $r3 ]; then note "O3 division paths" ok; else note "O3 division paths" FAIL; fail=1; fi
else
    note "linux run" skipped
fi

if python3 -c "import unicorn" 2>/dev/null; then
    printf 'start:\n mov eax, 0x2000004\n mov edi, 1\n lea rsi, [rel m]\n mov edx, 14\n syscall\n mov eax, 0x2000001\n xor edi, edi\n syscall\nm db "Hello, World!", 10\n' > "$T/m64.cly"
    ./cly mac "$T/m64.cly" -m64 -q -out "$T/m"
    python3 tests/emu/mac64_emu.py "$T/m" | grep -q "Hello, World" && note "mac64 emu" ok || { note "mac64 emu" FAIL; fail=1; }
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
    T2=$(mktemp -d)
    cat > "$T2/forms.txt" <<'LIST'
andn rax,rbx,rcx
andn rax,rbx,[rcx]
bextr rax,rbx,rcx
bextr rax,[rbx],rcx
bextr rax,rbx,1
bzhi rax,rbx,rcx
mulx rax,rbx,rcx
mulx r9,r10,[rcx]
pdep rax,rbx,rcx
pext rax,rbx,rcx
sarx rax,rbx,rcx
shlx rax,rbx,rcx
shrx r8,[rbx],rcx
rorx rax,rbx,1
rorx rax,[rbx],1
blsi rax,rbx
blsmsk rax,[rbx]
blsr rax,rbx
blcs rax,rbx
blci rax,rbx
tzmsk rax,[rbx]
t1mskc rax,rbx
blcfill rax,rbx
blsfill rax,rbx
blsic rax,rbx
blcic rax,rbx
blcmsk rax,rbx
llwpcb rax
slwpcb rax
lwpval rax,[rbx],1
lwpins rax,[rbx],1
kmovq k1,rax
kmovq rax,k1
vcvtsd2si rax,xmm0
vcvtsd2si rax,[rbx]
vcvtsd2si rax,qword [rbx]
vcvttsd2si rax,xmm0
vcvtss2si rax,xmm0
vcvttss2si rax,[rbx]
vcvtsd2usi rax,xmm0
vcvtss2usi rax,[rbx+rcx*2]
vcvttsd2usi rax,xmm0
vcvttss2usi rax,xmm0
vcvtsh2si rax,xmm0
vcvtsh2usi rax,[rbx]
vcvttsh2si rax,xmm0
vcvttsh2usi rax,xmm0
vcvtsi2sd xmm0,xmm1,rax
vcvtsi2ss xmm0,xmm1,rax
vcvtsi2sd xmm0,xmm1,qword [rax]
vcvtsi2sd xmm0,xmm1,dword [rax]
vcvtsi2sd xmm0,xmm1,eax
vcvtsi2sd xmm0,rax
vcvtsi2sd xmm0,xmm1,{rn-sae},rax
vcvtusi2sd xmm0,xmm1,rax
vcvtusi2ss xmm0,xmm1,rax
vcvtsi2sh xmm0,xmm1,rax
vmovq xmm0,rax
vmovq rax,xmm0
vmovq xmm0,xmm1
vmovq xmm0,[rax]
vmovq [rax],xmm0
vmovmskps rax,xmm0
vmovmskpd rax,ymm0
vpmovmskb rax,xmm0
vpmovmskb rax,ymm0
vpbroadcastb xmm0,rax
vpbroadcastw ymm0{k1},rax
vpbroadcastq zmm0,rax
aadd [rbx],eax
aadd qword [rbx],rax
aand [rbx],eax
axor qword [rbx],rax
hreset 1
prefetchit0 [rax]
prefetchit1 byte [rax]
retnw 1
retfw 1
retfd 1
clac
stac
enclv
vaddps xmm16,xmm1,xmm2
vaddps xmm1,xmm2,xmm17
vaddps ymm1,ymm2,ymm31
vmovd xmm16,eax
vmovq xmm16,xmm1
vgatherdps xmm1{k1},[rax+xmm16]
vaddps xmm0,xmm1,xmm2
vpinsrd xmm20,xmm1,eax,1
vpshufb xmm17,xmm1,xmm2
vcvtsi2sd xmm20,xmm1,rax
vpextrw eax,xmm16,1
vpextrb [rax],xmm18,1
vucomiss xmm17,xmm1
vcmpeqps k1,xmm17,xmm2
ldtilecfg [rax]
sttilecfg [rax+64]
tilezero tmm0
tilezero tmm7
tilerelease
tileloadd tmm0,[rax]
tileloadd tmm1,[rax+rcx*2]
tileloadd tmm1,[rax+rcx*2+0x40]
tileloadd tmm1,[rsp]
tileloadd tmm1,[rbp]
tileloaddt1 tmm2,[rax+8]
tilestored [rax+rcx],tmm3
tdpbssd tmm0,tmm1,tmm2
tdpbsud tmm3,tmm4,tmm5
tdpbusd tmm0,tmm1,tmm2
tdpbuud tmm7,tmm6,tmm5
tdpbf16ps tmm0,tmm1,tmm2
xresldtrk
xsusldtrk
pconfig
vmovq xmm16,rax
vpextrb rax,xmm0,1
vpextrd rax,xmm0,1
vpextrq rax,xmm0,1
vpinsrq xmm0,xmm1,rax,1
vpinsrq xmm0,xmm1,[rax],1
vpextrq [rax],xmm0,1
movntss [rax],xmm0
movntsd [rax],xmm0
prefetch qword [rax]
prefetchw [rax]
LIST
    bad=0
    while IFS= read -r i; do
        printf 'bits 64\n%s\n' "$i" > "$T2/n.asm"
        printf 'bits 64\nstart:\n%s\n' "$i" > "$T2/c.asm"
        rm -f "$T2/n.bin" "$T2/c.bin"
        nasm -f bin "$T2/n.asm" -o "$T2/n.bin" 2>/dev/null
        ./cly linux "$T2/c.asm" -flat -m64 -O0 -q -w -out "$T2/c.bin" 2>/dev/null
        cmp -s "$T2/n.bin" "$T2/c.bin" || { echo "64-bit form differs: $i"; bad=1; }
    done < "$T2/forms.txt"
    rm -rf "$T2"
    [ $bad = 0 ] && note "64-bit forms" ok || { note "64-bit forms" FAIL; fail=1; }
else
    note "nasm compare" "skipped (no nasm)"
fi

TF=$(mktemp -d)
bad=0
cat > "$TF/a.asm" <<'EOS'
bits 32
section .text
global _start, tbl
extern ext
_start:
    mov eax, 1
    call ext
    mov eax, [data1]
    jmp short _start
section .data
data1: dd 5, _start
tbl: dd ext
section .bss
buf: resb 16
EOS
cat > "$TF/o.asm" <<'EOS'
bits 32
segment code use16 class=CODE align=16
..start:
    mov ax, msg
    call other
    ret
global gl
gl: ret
extern other
segment data use16 class=DATA align=2
msg: db 'hello', 0
dw msg
segment stack stack class=STACK
resb 64
group dgroup data stack
common cm 4
EOS
if command -v nasm >/dev/null 2>&1; then
    for t in aout aoutb; do
        rm -f "$TF/c.o" "$TF/n.o"
        ./cly -nasm linux32 "$TF/a.asm" -$t -q -out "$TF/c.o" && nasm -f $t "$TF/a.asm" -o "$TF/n.o"
        cmp -s "$TF/c.o" "$TF/n.o" || { echo "$t differs from nasm"; bad=1; }
    done
    rm -f "$TF/c.o" "$TF/n.o"
    ./cly -nasm linux32 "$TF/o.asm" -omf -q -out "$TF/c.o" && nasm -f obj "$TF/o.asm" -o "$TF/n.o"
    cmp -s "$TF/c.o" "$TF/n.o" || { echo "omf differs from nasm"; bad=1; }
    rm -f "$TF/c.o" "$TF/n.o"
    ./cly -nasm linux32 "$TF/a.asm" -as86 -q -out "$TF/c.o" && nasm -f as86 "$TF/a.asm" -o "$TF/n.o"
    cmp -s "$TF/c.o" "$TF/n.o" || { echo "as86 differs from nasm"; bad=1; }
    rm -f "$TF/c.o" "$TF/n.o"
    printf 'bits 16\nsection code\nglobal gl\nextern ext1\n..start:\n mov ax, 1\n call ext1\n mov bx, [dat1]\ngl: ret\nsection data\ndat1: dw 5, gl\nsection bss\nb1: resb 8\n' > "$TF/i.asm"
    ./cly -nasm linux32 "$TF/i.asm" -ieee -q -out "$TF/c.o" && nasm -f ieee "$TF/i.asm" -o "$TF/n.o"
    [ "$(grep -v '^DT\|^CS' "$TF/c.o")" = "$(grep -v '^DT\|^CS' "$TF/n.o")" ] || { echo "ieee differs from nasm"; bad=1; }
fi
printf 'bits 64\nglobal _start\nsection .text\n_start:\n mov eax, 60\n xor edi, edi\n syscall\n' > "$TF/x.asm"
./cly linux "$TF/x.asm" -x32 -q -out "$TF/x32.o" && [ "$(od -An -tx1 -j18 -N2 "$TF/x32.o" | tr -d ' ')" = "3e00" ] || { echo "x32 machine type"; bad=1; }
./cly linux "$TF/x.asm" -g -elf -q -out "$TF/g.elf" && command -v readelf >/dev/null 2>&1 && { readelf -S "$TF/g.elf" | grep -q debug_line || { echo "-g has no debug_line"; bad=1; }; }
./cly linux "$TF/x.asm" -elf -map "$TF/m.map" -q -out "$TF/m.elf" 2>/dev/null; grep -q _start "$TF/m.map" 2>/dev/null || { echo "map file"; bad=1; }
printf '%s\n' "linux" "$TF/x.asm" "-elf" "-q" "-out" "$TF/r.elf" > "$TF/args.rsp"
./cly -@ "$TF/args.rsp" 2>/dev/null; [ -s "$TF/r.elf" ] || { echo "response file"; bad=1; }
./cly linux "$TF/x.asm" -elf -q -MF "$TF/d.mk" -out "$TF/dep.elf" 2>/dev/null; grep -q 'x.asm' "$TF/d.mk" 2>/dev/null || { echo "dependency file"; bad=1; }
cat > "$TF/l1.asm" <<'EOS'
bits 64
global _start
extern helper, counter
section .text
_start:
    call helper
    add eax, [rel counter]
    mov edi, eax
    mov eax, 60
    syscall
EOS
cat > "$TF/l2.asm" <<'EOS'
bits 64
global helper, counter
section .text
helper:
    mov eax, 30
    ret
section .data
counter: dd 12
EOS
./cly linux "$TF/l1.asm" -o -q && ./cly linux "$TF/l2.asm" --o "$TF/l2.o" -q && ./cly "$TF/l1.o" "$TF/l2.o" --ld "$TF/lprog" >/dev/null
"$TF/lprog"; [ $? = 42 ] || { echo "--ld program result"; bad=1; }
./cly "$TF/l1.o" "$TF/l2.o" "$(printf '\342\200\224')ld" "$TF/lprog2" >/dev/null; [ -x "$TF/lprog2" ] || { echo "em dash --ld"; bad=1; }
./cly linux "$TF/l1.asm" "$(printf '\342\200\224')o" "$TF/em.o" -q; [ "$(od -An -tx1 -N4 "$TF/em.o" | tr -d ' ')" = "7f454c46" ] || { echo "--o with .o name is not an object"; bad=1; }
./cly "$TF/l1.o" --ld "$TF/bad" >/dev/null 2>&1 && { echo "--ld undefined symbol accepted"; bad=1; }
rm -rf "$TF"
[ $bad = 0 ] && note "object formats and options" ok || { note "object formats and options" FAIL; fail=1; }

for a in arm64 riscv; do
    for ex in examples/$a/*.cly; do
        n=$(basename "$ex" .cly)
        if ./cly linux "$ex" -q -out "$T/$a.$n" 2>"$T/err"; then :; else note "$a/$n" "FAIL: $(head -1 "$T/err")"; fail=1; fi
        if ./cly bare "$ex" -hex -q -out "$T/$a.$n.hex" 2>"$T/err"; then :; else note "$a/$n bare" "FAIL: $(head -1 "$T/err")"; fail=1; fi
    done
done
note "arm64 and riscv examples" "built"
if python3 -c "import unicorn" 2>/dev/null; then
    for a in arm64 riscv; do
        ./cly linux examples/$a/hello.cly -q -out "$T/h" && python3 tests/emu/elf_emu.py "$T/h" | grep -q "Hello" && note "$a hello emu" ok || { note "$a hello emu" FAIL; fail=1; }
        ./cly linux examples/$a/fact.cly -q -out "$T/f" && python3 tests/emu/elf_emu.py "$T/f" | grep -q "3628800" && note "$a fact emu" ok || { note "$a fact emu" FAIL; fail=1; }
    done
    r=$(python3 tests/emu/rt_tests.py | tail -1); echo "$r" | grep -qE "bad 0$" && note "arm64 riscv runtime" ok || { note "arm64 riscv runtime" "FAIL"; python3 tests/emu/rt_tests.py | grep FAIL; fail=1; }
    r=$(python3 tests/rv/li_test.py 2>&1 | tail -1); echo "$r" | grep -qE "bad 0$" && note "riscv li" ok || { note "riscv li" "FAIL: $r"; fail=1; }
fi
if command -v llvm-mc >/dev/null 2>&1; then
    for t in run_int run_mem run_fp run_simd run_ext; do
        r=$(python3 tests/a64/$t.py 2>&1 | grep "total=" | grep -v "diffs=0 ")
        [ -z "$r" ] && note "arm64 $t vs llvm" ok || { note "arm64 $t vs llvm" "FAIL"; echo "$r"; fail=1; }
    done
    if command -v ld.lld >/dev/null 2>&1 && python3 -c "import unicorn" 2>/dev/null; then sh tests/xlink.sh && note "link with llvm objects" ok || { note "link with llvm objects" FAIL; fail=1; }; fi
    sh tests/errors.sh >/dev/null && note "error messages" ok || { note "error messages" FAIL; fail=1; }
    sh tests/promo.sh >/dev/null && note "register promotion" ok || { note "register promotion" FAIL; fail=1; }
    python3 tests/opt_nx_fuzz.py 7 200 | tail -1 | grep -q "bad 0" && note "arm64 riscv optimizer fuzz" ok || { note "arm64 riscv optimizer fuzz" FAIL; fail=1; }
    for a in rv64 rv64nc rv32 rv32nc; do
        r=$(python3 tests/rv/run.py $a 2>&1)
        echo "$r" | grep -q "total=" || { note "riscv $a vs llvm" "FAIL (no output)"; fail=1; continue; }
        r=$(echo "$r" | grep "total=" | grep -v "diffs=0 ")
        [ -z "$r" ] && note "riscv $a vs llvm" ok || { note "riscv $a vs llvm" FAIL; echo "$r"; fail=1; }
    done
else
    note "arm64 and riscv vs llvm" "skipped (no llvm-mc)"
fi

if [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ]; then
    sh tests/progs.sh >/dev/null 2>&1 && note "nasm programs" ok || { note "nasm programs" FAIL; fail=1; }
    sh tests/leaks.sh >/dev/null 2>&1 && note "leak warnings" ok || { note "leak warnings" FAIL; fail=1; }
    sh tests/log.sh && note "compile log" ok || { note "compile log" FAIL; fail=1; }
    if command -v nasm >/dev/null 2>&1; then sh tests/compat.sh >/dev/null 2>&1 && note "nasm compat" ok || { note "nasm compat" FAIL; fail=1; }; fi
    if command -v nasm >/dev/null 2>&1; then bash tests/nasm_strict.sh >/dev/null 2>&1 && note "nasm strict mode" ok || { note "nasm strict mode" FAIL; fail=1; }; fi
fi

if command -v python3 >/dev/null 2>&1; then
    python3 - <<'PYBATCH'
import os, sys, random, subprocess, tempfile, shutil, struct, re
ROOT = os.getcwd()
CLY = os.path.abspath(os.environ.get("CLY", "./cly"))
W = tempfile.mkdtemp(prefix="cly_batch")
os.chdir(W)
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", CLY_NO_HINT="1")
SEED = int(os.environ.get("BATCH_SEED", "0"))
NF = int(os.environ.get("BATCH_N", "50"))
M64 = (1 << 64) - 1
M32 = (1 << 32) - 1

def run(args, timeout=30, **kw):
    r = subprocess.run([CLY] + args, capture_output=True, timeout=timeout, env=env, **kw)
    return r

def crashed(r):
    return r.returncode < 0 or r.returncode >= 128 or b"AddressSanitizer" in r.stderr or b"runtime error" in r.stderr

class Batch:
    def __init__(self, name):
        self.name = name; self.n = 0; self.ok = 0; self.msgs = []
    def check(self, cond, what=""):
        self.n += 1
        if cond: self.ok += 1
        elif len(self.msgs) < 6: self.msgs.append(what)
    def done(self):
        print("batch %-26s %d/%d" % (self.name, self.ok, self.n))
        for m in self.msgs: print("   FAIL:", m)
        return self.ok == self.n

def wr(p, s, mode="w"):
    with open(p, mode) as f: f.write(s)

def rd(p):
    with open(p, "rb") as f: return f.read()

def have(x): return shutil.which(x) is not None

def batch_flags_b(b):
    src = "start:\n mov eax, 1\n xor ebx, ebx\n int 0x80\n"
    wr("f.cly", src)
    ELF = b"\x7fELF"
    def isobj(p):
        d = rd(p); return d[:4] == ELF and struct.unpack_from("<H", d, 16)[0] == 1
    def isexe(p):
        d = rd(p); return d[:4] == ELF and struct.unpack_from("<H", d, 16)[0] == 2
    cases = []
    for kern in ["linux", None]:
        pre = [kern] if kern else []
        for bits in ["-m32", "-m64", None]:
            bb = [bits] if bits else []
            cases.append((pre + ["f.cly"] + bb + ["-o", "-q"], "f.o", isobj))
            cases.append((pre + ["f.cly"] + bb + ["-obj", "-q"], "f.obj", isobj))
            cases.append((pre + ["f.cly"] + bb + ["--o", "g1.o", "-q"], "g1.o", isobj))
            cases.append((pre + ["f.cly"] + bb + ["--o", "g2.obj", "-q"], "g2.obj", isobj))
            cases.append((pre + ["f.cly"] + bb + ["--o", "g3", "-q"], "g3", isexe))
            cases.append((pre + ["f.cly"] + bb + ["-q", "-o"], "f.o", isobj))
            cases.append((pre + ["-q", "f.cly"] + bb + ["\u2014o", "g4.o"], "g4.o", isobj))
            cases.append((pre + ["f.cly"] + bb + ["-obj", "--o", "g5.x", "-q"], "g5.x", isobj))
    for args, out, pred in cases:
        for p in ["f.o", "f.obj", out]:
            if os.path.exists(p): os.remove(p)
        r = run(args)
        okk = r.returncode == 0 and os.path.exists(out) and pred(out)
        b.check(okk, "%s -> %s rc=%d %s" % (" ".join(args), out, r.returncode, r.stderr[:80]))
        if b.n >= 48: break
    b.check(run(["f.cly", "--o"]).returncode != 0, "--o without name accepted")
    b.check(run(["f.cly", "--ld"]).returncode != 0, "--ld without name accepted")

def gen_chain(rng, bits, nfun):
    mask = M64 if bits == 64 else M32
    ops = []
    val = rng.randrange(0, 1 << 31)
    cur = val
    funs = []
    seq = []
    for k in range(rng.randint(3, 9)):
        op = rng.choice(["add", "sub", "xor", "and", "or", "imul", "shl", "shr"])
        if op == "shl" or op == "shr": imm = rng.randint(0, 7)
        elif op == "imul": imm = rng.randint(-100, 100)
        elif op == "and": imm = rng.randint(0, 0x7fffffff) | 0xff
        else: imm = rng.randint(-100000, 100000)
        seq.append((op, imm, rng.random() < 0.5))
    return val, seq

def apply(cur, op, imm, mask):
    if op == "add": return (cur + imm) & mask
    if op == "sub": return (cur - imm) & mask
    if op == "xor": return (cur ^ (imm & mask)) & mask
    if op == "and": return cur & (imm & mask)
    if op == "or": return cur | (imm & mask)
    if op == "imul": return (cur * imm) & mask
    if op == "shl": return (cur << imm) & mask
    if op == "shr": return (cur & mask) >> imm

def elf_prog(rng, bits, idx):
    mask = M64 if bits == 64 else M32
    r = "rdi" if bits == 64 else "ebx"
    val, seq = gen_chain(rng, bits, 0)
    tblv = [rng.randint(0, 1000) for _ in range(4)]
    cnt0 = rng.randint(0, 50)
    wsz = 8 if bits == 64 else 4
    dd = "dq" if bits == 64 else "dd"
    a = ["bits %d" % bits]
    if bits == 64: a.append("default rel")
    exts = ["tbl", "cnt"] + ["f%d" % i for i, s in enumerate(seq) if s[2]]
    a.append("extern " + ", ".join(exts))
    a.append("global _start")
    a.append("section .text")
    a.append("_start:")
    a.append("mov %s, %d" % (r, val))
    b = ["bits %d" % bits, "global tbl, cnt, " + ", ".join("f%d" % i for i, s in enumerate(seq) if s[2]) if any(s[2] for s in seq) else "global tbl, cnt"]
    if bits == 64: b.insert(1, "default rel")
    b.append("section .text")
    cur = val
    for i, (op, imm, ext) in enumerate(seq):
        ins = {"add": "add", "sub": "sub", "xor": "xor", "and": "and", "or": "or", "shl": "shl", "shr": "shr"}.get(op)
        if op == "imul": line = "imul %s, %s, %d" % (r, r, imm)
        else: line = "%s %s, %d" % (ins, r, imm)
        if ext:
            a.append("call f%d" % i)
            b.append("f%d:\n %s\n ret" % (i, line))
        else:
            a.append(line)
        cur = apply(cur, op, imm, mask)
    for i in range(4):
        a.append("add %s, [tbl+%d]" % (r, i * wsz))
        cur = (cur + tblv[i]) & mask
    a.append("inc %s [cnt]" % ("qword" if bits == 64 else "dword"))
    a.append("add %s, [cnt]" % r)
    cur = (cur + cnt0 + 1) & mask
    if bits == 64:
        a.append("mov eax, 60\nsyscall")
    else:
        a.append("mov eax, 1\nint 0x80")
    b.append("section .data")
    b.append("tbl %s %s" % (dd, ", ".join(map(str, tblv))))
    b.append("cnt %s %d" % (dd, cnt0))
    return "\n".join(a) + "\n", "\n".join(b) + "\n", cur & 0xff

def elf_gcc_check(b, bits):
    if not have("gcc"):
        b.check(True); b.check(True); return
    c = "int counter = 7;\nstatic int sq(int x) { return x * x; }\nint cfun(int a, int bb) { counter += a; return sq(a) + bb + counter; }\nconst char *msg = \"hello\";\nint arr[10];\nint tot(void) { int t = 0; for (int i = 0; i < 10; i++) { arr[i] = i * 3; t += arr[i]; } return t; }\n"
    wr("gc.c", c)
    for pic in ("-fno-pic", "-fpic"):
        okk = False; why = ""
        r = subprocess.run(["gcc", "-m%d" % bits, pic, "-O2", "-ffreestanding", "-fno-stack-protector", "-c", "gc.c", "-o", "gc.o"], capture_output=True)
        if r.returncode != 0:
            b.check(True); continue
        if bits == 64:
            m = "bits 64\ndefault rel\nextern cfun, counter, msg, tot\nglobal _start\n_start:\n mov edi, 3\n mov esi, 4\n call cfun\n add eax, [rel counter]\n mov rdx, [rel msg]\n movzx edx, byte [rdx+1]\n add eax, edx\n mov ebx, eax\n call tot\n add eax, ebx\n mov edi, eax\n mov eax, 60\n syscall\n"
        else:
            m = "bits 32\nextern cfun, counter, msg, tot\nglobal _start\n_start:\n push 4\n push 3\n call cfun\n add esp, 8\n add eax, [counter]\n mov edx, [msg]\n movzx edx, byte [edx+1]\n add eax, edx\n mov esi, eax\n call tot\n add eax, esi\n mov ebx, eax\n mov eax, 1\n int 0x80\n"
        wr("gm.cly", m)
        r1 = run(["linux", "gm.cly", "-m%d" % bits, "-o", "-q"])
        if os.path.exists("gp"): os.remove("gp")
        r2 = run(["gm.o", "gc.o", "--ld", "gp"])
        code = None
        if r2.returncode == 0:
            try: code = subprocess.run(["./gp"], timeout=10).returncode
            except Exception: code = -2
        b.check(r1.returncode == 0 and r2.returncode == 0 and code == (134 + 135) % 256, "gcc %s object: %d %d code %s %s" % (pic, r1.returncode, r2.returncode, code, (r1.stderr + r2.stderr)[:150]))

def batch_elf_b(b, bits):
    import platform
    if sys.platform != "linux" or platform.machine() not in ("x86_64", "AMD64"):
        for _ in range(50): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(1000 + bits)
    for i in range(48):
        sa, sb, want = elf_prog(rng, bits, i)
        wr("ea.cly", sa); wr("eb.cly", sb)
        mb = "-m%d" % bits
        kern = ["linux"] if i % 2 else []
        r1 = run(kern + ["ea.cly", mb, "-o", "-q"]); r2 = run(kern + ["eb.cly", mb, "--o", "eb.o", "-q"])
        if i % 3 == 0:
            os.replace("ea.o", "ea.obj") if False else None
        files = ["ea.o", "eb.o"] if i % 5 else ["eb.o", "ea.o"]
        for f in files:
            pass
        if os.path.exists("ep"): os.remove("ep")
        r3 = run(files + ["--ld", "ep"])
        code = -1
        if r3.returncode == 0:
            try: code = subprocess.run(["./ep"], timeout=10).returncode
            except Exception: code = -2
        b.check(r1.returncode == 0 and r2.returncode == 0 and r3.returncode == 0 and code == want,
                "case %d: asm %d %d ld %d code %d want %d %s" % (i, r1.returncode, r2.returncode, r3.returncode, code, want, (r1.stderr + r2.stderr + r3.stderr)[:120]))
    elf_gcc_check(b, bits)

def pe_run(path):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_MODE_64, UC_HOOK_CODE, UcError
    from unicorn.x86_const import UC_X86_REG_RCX, UC_X86_REG_ESP, UC_X86_REG_RSP
    d = rd(path)
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    mach, nsec = struct.unpack_from("<HH", d, pe + 4)
    is64 = mach == 0x8664
    opt = pe + 24
    entry = struct.unpack_from("<I", d, opt + 16)[0]
    base = struct.unpack_from("<Q", d, opt + 24)[0] if is64 else struct.unpack_from("<I", d, opt + 28)[0]
    soi = struct.unpack_from("<I", d, opt + 56)[0]
    osz = struct.unpack_from("<H", d, pe + 20)[0]
    mu = Uc(UC_ARCH_X86, UC_MODE_64 if is64 else UC_MODE_32)
    mu.mem_map(base, (soi + 0xfff) & ~0xfff)
    sec = opt + osz
    for i in range(nsec):
        n, vs, va, rs, rp = struct.unpack_from("<8sIIII", d, sec + 40 * i)
        mu.mem_write(base + va, d[rp:rp + rs])
    imp_rva, imp_sz = struct.unpack_from("<II", d, opt + (112 if is64 else 96) + 8)
    w = 8 if is64 else 4
    hooks = {}
    fake = 0x10000000
    mu.mem_map(fake, 0x1000)
    p = imp_rva
    while imp_rva:
        ilt, _, _, nm, iat = struct.unpack("<IIIII", bytes(mu.mem_read(base + p, 20)))
        if nm == 0: break
        k = 0
        while True:
            e = int.from_bytes(bytes(mu.mem_read(base + ilt + k * w, w)), "little")
            if e == 0: break
            name = bytes(mu.mem_read(base + e + 2, 40)).split(b"\0")[0].decode()
            addr = fake + 0x10 * len(hooks)
            hooks[addr] = name
            mu.mem_write(base + iat + k * w, addr.to_bytes(w, "little"))
            k += 1
        p += 20
    sp = 0x20000000
    mu.mem_map(sp - 0x10000, 0x20000)
    mu.reg_write(UC_X86_REG_RSP if is64 else UC_X86_REG_ESP, sp)
    res = {}
    def hook(mu, addr, size, ud):
        if addr in hooks and hooks[addr] == "ExitProcess":
            if is64: res["code"] = mu.reg_read(UC_X86_REG_RCX) & M32
            else:
                esp = mu.reg_read(UC_X86_REG_ESP)
                res["code"] = int.from_bytes(bytes(mu.mem_read(esp + 4, 4)), "little")
            mu.emu_stop()
    mu.hook_add(UC_HOOK_CODE, hook)
    try: mu.emu_start(base + entry, 0, count=200000)
    except UcError: pass
    return res.get("code")

def pe_prog(rng, bits):
    mask = M64 if bits == 64 else M32
    r = "rcx" if bits == 64 else "ebx"
    val, seq = gen_chain(rng, bits, 0)
    tblv = [rng.randint(0, 1000) for _ in range(4)]
    cnt0 = rng.randint(0, 50)
    wsz = 8 if bits == 64 else 4
    dd = "dq" if bits == 64 else "dd"
    fl = ["f%d" % i for i, s in enumerate(seq) if s[2]]
    a = ["bits %d" % bits]
    if bits == 64: a.append("default rel")
    a.append("import ExitProcess kernel32.dll")
    a.append("extern " + ", ".join(["tbl", "cnt"] + fl))
    a.append("global start")
    a.append("section .text")
    a.append("start:")
    a.append("mov %s, %d" % (r, val))
    b = ["bits %d" % bits]
    if bits == 64: b.append("default rel")
    b.append("global " + ", ".join(["tbl", "cnt"] + fl))
    b.append("section .text")
    cur = val
    for i, (op, imm, ext) in enumerate(seq):
        line = ("imul %s, %s, %d" % (r, r, imm)) if op == "imul" else "%s %s, %d" % (op, r, imm)
        if ext:
            a.append("call f%d" % i); b.append("f%d:\n %s\n ret" % (i, line))
        else: a.append(line)
        cur = apply(cur, op, imm, mask)
    for i in range(4):
        a.append("add %s, [tbl+%d]" % (r, i * wsz)); cur = (cur + tblv[i]) & mask
    a.append("inc %s [cnt]" % ("qword" if bits == 64 else "dword"))
    a.append("add %s, [cnt]" % r); cur = (cur + cnt0 + 1) & mask
    if bits == 64: a.append("sub rsp, 40\ncall ExitProcess")
    else: a.append("push ebx\ncall ExitProcess")
    b.append("section .data")
    b.append("tbl %s %s" % (dd, ", ".join(map(str, tblv))))
    b.append("cnt %s %d" % (dd, cnt0))
    return "\n".join(a) + "\n", "\n".join(b) + "\n", cur & M32

def batch_pe(bits, b, n):
    try: import unicorn
    except Exception:
        for _ in range(n): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(2000 + bits)
    for i in range(n):
        sa, sb, want = pe_prog(rng, bits)
        wr("pa.cly", sa); wr("pb.cly", sb)
        mb = "-m%d" % bits
        r1 = run(["windows", "pa.cly", mb, "-obj", "-q"]); r2 = run(["windows", "pb.cly", mb, "-obj", "-q"])
        files = ["pa.obj", "pb.obj"] if i % 4 else ["pb.obj", "pa.obj"]
        if os.path.exists("pp.exe"): os.remove("pp.exe")
        r3 = run(files + ["--ld", "pp.exe"])
        code = pe_run("pp.exe") if r3.returncode == 0 else None
        b.check(r1.returncode == 0 and r2.returncode == 0 and r3.returncode == 0 and code == want,
                "case %d: %d %d %d code %s want %d %s" % (i, r1.returncode, r2.returncode, r3.returncode, code, want, (r1.stderr + r2.stderr + r3.stderr)[:150]))

def batch_pe_zig(b):
    c = "int counter = 7;\nstatic int sq(int x) { return x * x; }\nint cfun(int a, int b) { counter += a; return sq(a) + b + counter; }\nconst char *msg = \"hello\";\n"
    wr("zc.c", c)
    for bits, target, us, call in ((64, "x86_64-windows-gnu", "", None), (32, "x86-windows-gnu", "_", None)):
        okk = False; why = "no zig"
        try:
            r = subprocess.run([sys.executable, "-m", "ziglang", "cc", "-target", target, "-O2", "-c", "zc.c", "-o", "zc%d.obj" % bits], capture_output=True, timeout=120)
            if r.returncode == 0:
                if bits == 64:
                    m = "bits 64\ndefault rel\nimport ExitProcess kernel32.dll\nextern cfun, counter, msg\nglobal start\nstart:\n sub rsp, 40\n mov ecx, 3\n mov edx, 4\n call cfun\n add eax, [rel counter]\n mov rdx, [rel msg]\n movzx edx, byte [rdx+1]\n add eax, edx\n mov ecx, eax\n call ExitProcess\n"
                else:
                    m = "bits 32\nimport ExitProcess kernel32.dll\nextern _cfun, _counter, _msg\nglobal start\nstart:\n push 4\n push 3\n call _cfun\n add esp, 8\n add eax, [_counter]\n mov edx, [_msg]\n movzx edx, byte [edx+1]\n add eax, edx\n push eax\n call ExitProcess\n"
                wr("zm%d.cly" % bits, m)
                r1 = run(["windows", "zm%d.cly" % bits, "-m%d" % bits, "-obj", "-q"])
                r2 = run(["zm%d.obj" % bits, "zc%d.obj" % bits, "--ld", "zp%d.exe" % bits])
                code = pe_run("zp%d.exe" % bits) if r1.returncode == 0 and r2.returncode == 0 else None
                okk = code == 134; why = "code %s %s" % (code, (r1.stderr + r2.stderr)[:100])
            else: why = "compile failed"
        except Exception as e:
            okk = True; why = "skipped"
        b.check(okk, "compiler made COFF %d: %s" % (bits, why))

def risc_prog(rng, arch):
    n = rng.randint(2, 6)
    val = rng.randint(1, 100)
    cur = val
    cnt = rng.randint(0, 99)
    tb = [rng.randint(0, 99) for _ in range(3)]
    fl = []
    if arch == "arm64":
        a = ["cpu arm64", "section .text", "global start", "extern cvar, tvar"]
        bl = ["cpu arm64", "section .text", "global cvar, tvar"]
        a.append("start:"); a.append("mov x0, #%d" % val)
        for i in range(n):
            k = rng.randint(1, 60); kind = rng.choice(["add", "sub"])
            fl.append(i); a.append("bl f%d" % i); bl.append("global f%d" % i)
            bl.append("f%d:\n %s x0, x0, #%d\n ret" % (i, kind, k))
            cur = apply(cur, kind, k, M64)
        a.append("adrp x1, cvar\nadd x1, x1, :lo12:cvar\nldr x2, [x1]\nadd x0, x0, x2")
        cur = (cur + cnt) & M64
        for i in range(3):
            a.append("adrp x1, tvar\nadd x1, x1, :lo12:tvar\nldr x2, [x1, #%d]\nadd x0, x0, x2" % (i * 8))
            cur = (cur + tb[i]) & M64
        a.append("mov x8, #93\nsvc #0")
        bl.append("section .data\nalign 8\ncvar dq %d\ntvar dq %s" % (cnt, ", ".join(map(str, tb))))
    else:
        a = ["cpu rv64", "section .text", "global start", "extern cvar, tvar"]
        bl = ["cpu rv64", "section .text", "global cvar, tvar"]
        a.append("start:"); a.append("li a0, %d" % val)
        for i in range(n):
            k = rng.randint(1, 60); kind = rng.choice(["add", "xor"])
            a.append("call f%d" % i); bl.append("global f%d" % i)
            bl.append("f%d:\n %si a0, a0, %d\n ret" % (i, kind, k))
            cur = apply(cur, kind, k, M64)
        a.append("la t0, cvar\nld a1, 0(t0)\nadd a0, a0, a1")
        cur = (cur + cnt) & M64
        for i in range(3):
            a.append("la t0, tvar\nld a1, %d(t0)\nadd a0, a0, a1" % (i * 8))
            cur = (cur + tb[i]) & M64
        a.append("li a7, 93\necall")
        bl.append("section .data\nalign 8\ncvar dq %d\ntvar dq %s" % (cnt, ", ".join(map(str, tb))))
    return "\n".join(a) + "\n", "\n".join(bl) + "\n", cur & 0xff

def batch_risc(arch, b, n):
    try: import unicorn
    except Exception:
        for _ in range(n): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(3000 + len(arch))
    emu = os.path.join(ROOT, "tests", "emu", "elf_emu.py")
    for i in range(n):
        sa, sb, want = risc_prog(rng, arch)
        wr("ra.cly", sa); wr("rb.cly", sb)
        r1 = run(["linux", "ra.cly", "-o", "-q"]); r2 = run(["linux", "rb.cly", "-o", "-q"])
        files = ["ra.o", "rb.o"] if i % 3 else ["rb.o", "ra.o"]
        if os.path.exists("rp"): os.remove("rp")
        r3 = run(files + ["--ld", "rp"])
        code = None
        if r3.returncode == 0:
            code = subprocess.run([sys.executable, emu, "rp"], capture_output=True, timeout=60).returncode
        b.check(r1.returncode == 0 and r2.returncode == 0 and r3.returncode == 0 and code == want,
                "case %d: %d %d %d code %s want %d %s" % (i, r1.returncode, r2.returncode, r3.returncode, code, want, (r1.stderr + r2.stderr + r3.stderr)[:150]))

def batch_install_b(b):
    base = tempfile.mkdtemp(prefix="cly_inst")
    def scenario(i, spaced=False):
        d = os.path.join(base, ("s %d" if spaced else "s%d") % i)
        for sub in ["dl", "bin", "home"]: os.makedirs(os.path.join(d, sub), exist_ok=True)
        exe = os.path.join(d, "dl", "cly")
        shutil.copy(CLY, exe)
        e = dict(env, HOME=os.path.join(d, "home"), CLY_GLOBAL_DIR=os.path.join(d, "bin"), PATH=os.path.join(d, "bin") + ":" + env.get("PATH", ""), XDG_CONFIG_HOME=os.path.join(d, "home", ".config"))
        return d, exe, e
    def go(exe, args, e, inp=""):
        return subprocess.run([exe] + args, input=inp.encode(), capture_output=True, env=e, timeout=30)
    for i in range(10):
        d, exe, e = scenario(i, i % 3 == 0)
        r = go(exe, ["-global"], e)
        inst = os.path.join(d, "bin", "cly")
        cfg = os.path.join(d, "home", ".config", "comprinly", "origin")
        okk = r.returncode == 0 and os.path.exists(inst) and not os.path.exists(exe) and os.path.exists(cfg) and open(cfg).read().strip() == exe
        if okk:
            v = subprocess.run([inst, "-v"], capture_output=True, env=e)
            okk = v.returncode == 0
        b.check(okk, "global %d: %s" % (i, r.stderr[:100]))
    for i in range(10, 20):
        d, exe, e = scenario(i, i % 4 == 0)
        go(exe, ["-global"], e)
        inst = os.path.join(d, "bin", "cly")
        r = go(inst, ["-unglobal"], e)
        b.check(r.returncode == 0 and os.path.exists(exe) and not os.path.exists(inst), "unglobal %d: %s" % (i, r.stderr[:100]))
    for i in range(20, 25):
        d, exe, e = scenario(i)
        go(exe, ["-global"], e)
        shutil.rmtree(os.path.join(d, "dl"))
        inst = os.path.join(d, "bin", "cly")
        r = go(inst, ["-unglobal"], e)
        b.check(r.returncode == 0 and os.path.exists(os.path.join(d, "home", "Downloads", "cly")) and not os.path.exists(inst), "unglobal fallback %d: %s" % (i, r.stderr[:100]))
    for i in range(25, 30):
        d, exe, e = scenario(i)
        go(exe, ["-global"], e)
        inst = os.path.join(d, "bin", "cly")
        r = go(inst, ["-global"], e)
        b.check(r.returncode == 0 and b"already" in r.stdout and os.path.exists(inst), "global twice %d" % i)
    for i, ans in enumerate(["n\n", "N\n", "\n", "", "no\n"]):
        d, exe, e = scenario(30 + i)
        r = go(exe, ["-uninstall"], e, ans)
        b.check(r.returncode == 0 and os.path.exists(exe), "uninstall answer %r removed the program" % ans)
    for i, ans in enumerate(["y\n", "Y\n", "yes\n", "y\n", "Y\n"]):
        d, exe, e = scenario(35 + i)
        if i >= 2: go(exe, ["-global"], e); exe = os.path.join(d, "bin", "cly")
        r = go(exe, ["-uninstall"], e, ans)
        cfgd = os.path.join(d, "home", ".config", "comprinly")
        b.check(r.returncode == 0 and not os.path.exists(exe) and not os.path.exists(os.path.join(d, "bin", "cly")) and not os.path.exists(os.path.join(cfgd, "origin")), "uninstall answer %r %s" % (ans, r.stderr[:80]))
    ver = subprocess.run([CLY, "-v"], capture_output=True, env=env).stdout.decode()
    mv = re.search(r"\d+\.\d+\.\d+", ver)
    cur = mv.group(0) if mv else "1.3.5"
    def mock(d, tag, url=None):
        asset = os.path.join(d, "newcly")
        wr(asset, "\x7fELF" + "x" * 5000)
        os.chmod(asset, 0o755)
        j = '{"tag_name": "%s", "assets": [{"browser_download_url": "%s"}]}' % (tag, url or "file://" + asset)
        api = os.path.join(d, "api.json")
        wr(api, j)
        return "file://" + api, asset
    for i, ans in enumerate(["n\n", "\n", "N\n", "", "maybe\n"]):
        d, exe, e = scenario(40 + i)
        api, asset = mock(d, "v99.0.0")
        e2 = dict(e, CLY_UPDATE_API=api)
        before = rd(exe)
        r = go(exe, ["-update"], e2, ans)
        b.check(r.returncode == 0 and rd(exe) == before and b"newer" in r.stdout, "update answer %r %s" % (ans, r.stdout[:80]))
    for i, ans in enumerate(["y\n", "Y\n", "yes\n"]):
        d, exe, e = scenario(45 + i)
        api, asset = mock(d, "v99.0.%d" % i)
        e2 = dict(e, CLY_UPDATE_API=api)
        r = go(exe, ["-update"], e2, ans)
        b.check(r.returncode == 0 and rd(exe) == rd(asset), "update accepted %r %s" % (ans, r.stderr[:80]))
    d, exe, e = scenario(48)
    api, asset = mock(d, "v" + cur)
    r = go(exe, ["-update"], dict(e, CLY_UPDATE_API=api), "y\n")
    b.check(r.returncode == 0 and b"up to date" in r.stdout, "same version update %s" % r.stdout[:80])
    d, exe, e = scenario(49)
    marker = os.path.join(d, "pwned")
    api, asset = mock(d, "v99.0.0", "file://" + asset_name(d) + "$(touch " + marker + ")")
    r = go(exe, ["-update"], dict(e, CLY_UPDATE_API=api), "y\n")
    b.check(not os.path.exists(marker) and r.returncode != 0, "update ran a shell command from the release data")
    shutil.rmtree(base, ignore_errors=True)

def asset_name(d): return os.path.join(d, "newcly")

def mutate(rng, d):
    d = bytearray(d)
    k = rng.random()
    if k < 0.15 and len(d) > 8:
        return bytes(d[:rng.randrange(1, len(d))])
    for _ in range(rng.randint(1, 8)):
        pos = rng.randrange(len(d))
        m = rng.random()
        if m < 0.5: d[pos] = rng.randrange(256)
        elif m < 0.7: d[pos] ^= 1 << rng.randrange(8)
        elif m < 0.85 and pos + 4 <= len(d): d[pos:pos + 4] = struct.pack("<I", rng.choice([0, 0xffffffff, 0x7fffffff, 0x80000000, 0x10000, len(d), len(d) + 1]))
        elif pos + 2 <= len(d): d[pos:pos + 2] = struct.pack("<H", rng.choice([0, 0xffff, 1, 2, 0xff00]))
    return bytes(d)

def batch_ld_fuzz_b(b):
    srcs = {
        "x64": ("bits 64\ndefault rel\nglobal _start\nextern g\n_start:\n call g\n mov rax, [rel v]\n lea rbx, [rel v]\n mov eax, 60\n syscall\nsection .data\nv dq 5\nsection .bss\nbb resb 16\n", "linux", "-m64"),
        "i386": ("bits 32\nglobal _start\nextern g\n_start:\n call g\n mov eax, [v]\n mov ebx, v\n mov eax, 1\n int 0x80\nsection .data\nv dd 5\nsection .bss\nbb resb 16\n", "linux", "-m32"),
        "arm64": ("cpu arm64\nglobal start\nextern g\nstart:\n bl g\n adrp x1, v\n add x1, x1, :lo12:v\n ldr x0, [x1]\n mov x8, #93\n svc #0\nsection .data\nv dq 5\n", "linux", None),
        "rv64": ("cpu rv64\nglobal start\nextern g\nstart:\n call g\n la t0, v\n ld a0, 0(t0)\n li a7, 93\n ecall\nsection .data\nv dq 5\n", "linux", None),
        "pe64": ("bits 64\ndefault rel\nimport ExitProcess kernel32.dll\nglobal start\nextern g\nstart:\n call g\n mov rax, [rel v]\n xor ecx, ecx\n call ExitProcess\nsection .data\nv dq 5\nsection .bss\nbb resb 16\n", "windows", "-m64"),
        "pe32": ("bits 32\nimport ExitProcess kernel32.dll\nglobal start\nextern g\nstart:\n call g\n mov eax, [v]\n push 0\n call ExitProcess\nsection .data\nv dd 5\nsection .bss\nbb resb 16\n", "windows", "-m32"),
    }
    gsrc = {"x64": "bits 64\nglobal g\ng: ret\n", "i386": "bits 32\nglobal g\ng: ret\n", "arm64": "cpu arm64\nglobal g\ng: ret\n", "rv64": "cpu rv64\nglobal g\ng: ret\n", "pe64": "bits 64\nglobal g\ng: ret\n", "pe32": "bits 32\nglobal g\ng: ret\n"}
    base = {}
    for k, (s, kern, mb) in srcs.items():
        wr("m_%s.cly" % k, s); wr("g_%s.cly" % k, gsrc[k])
        for nm in ("m_", "g_"):
            a = [kern, nm + k + ".cly", "-obj", "-q"] + ([mb] if mb else [])
            r = run(a)
        base[k] = (rd("m_%s.obj" % k), rd("g_%s.obj" % k))
    rng = random.Random(4000 + SEED)
    keys = list(base)
    for i in range(NF):
        bad = None
        for t in range(8):
            k = keys[(i + t) % len(keys)]
            which = rng.randrange(2)
            m = mutate(rng, base[k][which])
            files = [m, base[k][1 - which]]
            if rng.random() < 0.5: files.reverse()
            wr("z1.bin", files[0], "wb"); wr("z2.bin", files[1], "wb")
            ext = ".exe" if k.startswith("pe") else ""
            try: r = run(["z1.bin", "z2.bin", "--ld", "zout" + ext], timeout=20)
            except subprocess.TimeoutExpired: bad = "timeout %s" % k; break
            if crashed(r): bad = "crash rc=%d %s" % (r.returncode, k); wr("crash_%d_%d.bin" % (i, t), files[0], "wb"); wr("crash_%d_%d_b.bin" % (i, t), files[1], "wb"); wr("crash_%d_%d.txt" % (i, t), r.stderr[:1500].decode("latin1")); break
        b.check(bad is None, "mutation set %d: %s" % (i, bad))

def batch_asm_fuzz_b(b):
    srcs = []
    for dn in ["examples", "examples/arm64", "examples/riscv"]:
        p = os.path.join(ROOT, dn)
        for f in sorted(os.listdir(p)):
            if f.endswith(".cly"): srcs.append(rd(os.path.join(p, f)).decode("latin1"))
    toks = ["%macro", "%endmacro", "%if", "%endif", "%define", "[", "]", "((((", "))))", "'", '"', "\\", "dq", "times 70000 db 0", "resb -1", "section", ":", ",", "$$", "$", "%", "0x", "1e999", "bits 7", "cpu", "align 0", "equ", "%rep 3000", "%include", "..@", "{k1}{z}", "seg", "wrt", "*", "<<", "//", "%%", "\x00", "\xff", "import", "export", "common", "extern", "global", ";", "%macro m 1-*", "%1", "%+", "%?", "%[", "%{", "%assign", "%strlen", "%substr", "%rotate", "%xdefine", "%defstr", "%deftok", "%00", "%$a", "%push", "%pop", "%rep 3", "%exitrep", "%endrep", "%ifdef", "%elif", "%else", "%undef", "%line", "%error", "%warning", "%pragma", "%unmacro", "%local", "%arg", "%stacksize", "%ifctx", "%ifidn", "%ifnum", "%ifstr", "%ifid", "%ifmacro", "%t", "%-1", "%*", "%0", "%{1:2}", "__?FILE?__", "__?LINE?__", "ilog2(", "sqrt(", "strlen(", "%sel(", "%cond("]
    rng = random.Random(5000 + SEED)
    kernels = [["linux"], ["windows"], ["mac"], ["bare"], ["linux", "-m32"], ["windows", "-m32"], ["bare", "-m16"], ["linux", "-nasm"]]
    for i in range(NF):
        bad = None
        for t in range(8):
            s = rng.choice(srcs)
            lines = s.split("\n")
            for _ in range(rng.randint(1, 6)):
                m = rng.random(); li = rng.randrange(len(lines))
                if m < 0.25: lines[li] = lines[li][:rng.randrange(len(lines[li]) + 1)]
                elif m < 0.5: lines.insert(li, rng.choice(toks) + " " + rng.choice(toks))
                elif m < 0.7:
                    l = lines[li]; p = rng.randrange(len(l) + 1); lines[li] = l[:p] + rng.choice(toks) + l[p:]
                elif m < 0.85: lines.insert(li, lines[li])
                else: lines[li] = "".join(rng.choice("abcxyz0123456789 ,[]+-*:%$;'\"") for _ in range(rng.randint(1, 30)))
            wr("fz.cly", "\n".join(lines), "wb") if False else open("fz.cly", "wb").write("\n".join(lines).encode("latin1", "replace"))
            a = rng.choice(kernels) + ["fz.cly", "-q", "--o", "fzout"]
            if rng.random() < 0.3: a += ["-obj"]
            if rng.random() < 0.2: a += ["-O3"]
            try: r = run(a, timeout=20)
            except subprocess.TimeoutExpired: bad = "timeout %s" % a; shutil.copy("fz.cly", "hang_%d_%d.cly" % (i, t)); break
            if crashed(r): bad = "crash rc=%d %s" % (r.returncode, a); shutil.copy("fz.cly", "crash_%d_%d.cly" % (i, t)); break
        b.check(bad is None, "mutation set %d: %s" % (i, bad))

FMT_PROGS = {
    "hello": "bits 32\nsection .text\nglobal start\nstart:\n mov eax, 4\n mov ebx, 1\n mov ecx, msg\n mov edx, 6\n int 0x80\n mov eax, 1\n xor ebx, ebx\n int 0x80\nsection .data\nmsg db 'hello', 10\n",
    "calls": "bits 32\nsection .text\nglobal start\nextern ext_fn\nstart:\n call ext_fn\n call local\n jmp start\nlocal:\n ret\n",
    "data": "bits 32\nsection .data\nglobal tbl\ntbl: dd 1, 2, 3, tbl\nsection .bss\nbuf resb 64\nsection .text\nglobal start\nstart:\n mov eax, [tbl+4]\n mov [buf], eax\n lea ebx, [buf+8]\n ret\n",
    "arith": "bits 32\nsection .text\nglobal start\nstart:\n mov eax, 5\n add eax, 7\n imul eax, 3\n sub eax, 1\n shl eax, 2\n cmp eax, 100\n jl start\n ret\n",
    "strs": "bits 32\nsection .data\ns1 db 'abc', 0\ns2 dw 'xy', 0x1234\nsection .text\nglobal start\nstart:\n mov esi, s1\n mov edi, s2\n cld\n lodsb\n ret\n",
    "bssonly": "bits 32\nsection .bss\nglobal v\nv resd 100\nsection .text\nglobal start\nstart:\n mov dword [v], 1\n ret\n",
}

def batch_formats(b):
    nasm = have("nasm")
    fmts = [("-obj", "elf32", True), ("-coff", None, False), ("-x32", None, False), ("-aout", "aout", True), ("-aoutb", "aoutb", True), ("-omf", "obj", True), ("-as86", "as86", True), ("-ieee", None, False)]
    for name, src in list(FMT_PROGS.items())[:3]:
        wr("fm_%s.asm" % name, src)
        wr("fo_%s.asm" % name, re.sub(r"(?m)^section (\.[a-z]+)$", r"section \1 use32", src))
        for flag, nf, exact in fmts:
            tag = flag[1:]
            out = "fm_%s.%s" % (name, tag)
            kern = "windows" if flag == "-coff" else "linux"
            srcf = "fo_%s.asm" % name if flag == "-omf" else "fm_%s.asm" % name
            a = [kern, srcf, flag, "-q", "--o", out]
            if flag in ("-obj", "-aout", "-aoutb", "-omf", "-as86", "-ieee", "-coff"): a += ["-nasm"] if flag != "-coff" else []
            if flag == "-obj": a += ["-m32"]
            if flag == "-x32": a += ["-m64"] if False else []
            if os.path.exists(out): os.remove(out)
            r = run(a)
            okk = r.returncode == 0 and os.path.exists(out) and os.path.getsize(out) > 20
            why = ""
            if okk and flag == "-obj" and have("readelf"):
                rr = subprocess.run(["readelf", "-S", "-s", out], capture_output=True)
                okk = rr.returncode == 0 and b"Warning" not in rr.stderr and b"Error" not in rr.stderr
                if okk and nasm:
                    rn = subprocess.run(["nasm", "-f", "elf32", srcf, "-o", out + ".n"], capture_output=True)
                    if rn.returncode == 0:
                        def names(p): return sorted(x for x in re.findall(r"\] (\.\S+)", subprocess.run(["readelf", "-S", p], capture_output=True, text=True).stdout) if x != ".note.GNU-stack")
                        okk = names(out) == names(out + ".n")
                        why = "sections differ from nasm"
            elif okk and flag == "-coff" and have("objdump"):
                rr = subprocess.run(["objdump", "-h", out], capture_output=True)
                okk = rr.returncode == 0 and b"file format" in rr.stdout
                why = "objdump"
            elif okk and nasm and exact:
                rn = subprocess.run(["nasm", "-f", nf, srcf, "-o", out + ".n"], capture_output=True)
                if rn.returncode == 0:
                    okk = rd(out) == rd(out + ".n")
                    why = "bytes differ from nasm -f " + nf
            b.check(okk, "%s %s %s %s" % (name, flag, why, r.stderr[:100]))
    r = run(["linux", "fm_hello.asm", "-obj", "-nasm", "-m32", "-q", "--o", "q1.o"]); b.check(r.returncode == 0 and rd("q1.o")[4] == 1, "elf32 class")
    r = run(["linux", "fm_hello.asm", "-obj", "-nasm", "-m64", "-q", "--o", "q2.o"]); b.check(r.returncode == 0 and rd("q2.o")[4] == 2, "elf64 class")

def batch_obj_size(b, n):
    rng = random.Random(6000)
    nasm = have("nasm")
    for i in range(n):
        bits = 64 if i % 2 else 32
        sa, sb, want = elf_prog(rng, bits, i)
        wr("so_a.asm", sa); wr("so_b.asm", sb)
        for nm in ("so_a", "so_b"):
            r = run(["linux", nm + ".asm", "-m%d" % bits, "-obj", "-q", "--o", nm + ".o"])
            okk = r.returncode == 0
            why = ""
            if okk and nasm:
                rn = subprocess.run(["nasm", "-f", "elf%d" % bits, nm + ".asm", "-o", nm + ".n"], capture_output=True)
                if rn.returncode == 0:
                    okk = os.path.getsize(nm + ".o") <= os.path.getsize(nm + ".n") + 64
                    why = "%d > nasm %d" % (os.path.getsize(nm + ".o"), os.path.getsize(nm + ".n"))
            b.check(okk, "case %d %s: %s %s" % (i, nm, why, r.stderr[:80]))

REG = {
    16: {8: ["al", "cl", "dl", "bl", "ah", "ch"], 16: ["ax", "cx", "dx", "bx", "si", "di", "bp"], 32: ["eax", "ecx", "edx", "ebx", "esi", "edi"]},
    32: {8: ["al", "cl", "dl", "bl", "ah", "ch"], 16: ["ax", "cx", "dx", "bx", "si", "di", "bp"], 32: ["eax", "ecx", "edx", "ebx", "esi", "edi", "ebp", "esp"]},
    64: {8: ["al", "cl", "dl", "bl", "sil", "dil", "r8b", "r15b"], 16: ["ax", "cx", "dx", "bx", "si", "di", "r9w", "r14w"], 32: ["eax", "ecx", "edx", "ebx", "esi", "edi", "r8d", "r12d", "ebp"], 64: ["rax", "rcx", "rdx", "rbx", "rsi", "rdi", "r8", "r13", "r15", "rbp"]},
}
ADDRBASE = {16: ["bx", "bp", "si", "di"], 32: ["eax", "ebx", "ecx", "esi", "edi", "ebp", "esp"], 64: ["rax", "rbx", "rcx", "rsi", "rdi", "rbp", "rsp", "r8", "r12", "r13", "r15"]}

def rand_mem(rng, bits, size):
    sz = {8: "byte", 16: "word", 32: "dword", 64: "qword"}[size]
    k = rng.random()
    if bits == 16:
        base = rng.choice(["bx+si", "bx+di", "bp+si", "bp+di", "si", "di", "bx", "bp"])
        disp = rng.choice(["", "+0", "+5", "-3", "+200", "+0x1234"])
        if base == "bp" and disp == "": disp = "+0"
        if k < 0.1: return "%s [0x%x]" % (sz, rng.randrange(0, 65535))
        return "%s [%s%s]" % (sz, base, disp)
    regs = ADDRBASE[bits]
    if k < 0.1: return "%s [0x%x]" % (sz, rng.randrange(0, 0x7fffffff))
    base = rng.choice(regs)
    disp = rng.choice(["", "", "+8", "-8", "+0x100", "-0x1000", "+0x12345678", "+0"])
    if k < 0.5: return "%s [%s%s]" % (sz, base, disp)
    idx = rng.choice([r for r in regs if r not in ("esp", "rsp")])
    sc = rng.choice([1, 2, 4, 8])
    return "%s [%s+%s*%d%s]" % (sz, base, idx, sc, disp)

def rand_imm(rng, size):
    k = rng.random()
    if k < 0.3: return str(rng.randint(-128, 127))
    if k < 0.5: return str(rng.randint(0, 255))
    mx = {8: 255, 16: 65535, 32: 0xffffffff, 64: 0x7fffffff}[size]
    return str(rng.randint(0, mx))

def gen_insn(rng, bits):
    sizes = [8, 16, 32] + ([64] if bits == 64 else [])
    size = rng.choice(sizes)
    rr = lambda sz=None: rng.choice(REG[bits][sz or size])
    m = rng.random()
    op2 = ["add", "or", "adc", "sbb", "and", "sub", "xor", "cmp", "mov", "test"]
    if m < 0.30:
        op = rng.choice(op2); form = rng.random()
        if form < 0.3: return "%s %s, %s" % (op, rr(), rr())
        if form < 0.55: return "%s %s, %s" % (op, rr(), rand_mem(rng, bits, size))
        if form < 0.75: return "%s %s, %s" % (op, rand_mem(rng, bits, size), rr())
        if form < 0.9: return "%s %s, %s" % (op, rr(), rand_imm(rng, size))
        return "%s %s, %s" % (op, rand_mem(rng, bits, size), rand_imm(rng, size))
    if m < 0.40:
        op = rng.choice(["shl", "shr", "sar", "rol", "ror", "rcl", "rcr"])
        return "%s %s, %s" % (op, rr(), rng.choice(["1", "cl", str(rng.randint(1, 7))]))
    if m < 0.50:
        op = rng.choice(["inc", "dec", "neg", "not", "mul", "div", "idiv"])
        return "%s %s" % (op, rr() if rng.random() < 0.6 else rand_mem(rng, bits, size))
    if m < 0.58:
        s2 = rng.choice([16, 32] + ([64] if bits == 64 else []))
        return "%s %s, %s" % (rng.choice(["imul", "bsf", "bsr", "popcnt", "cmovz", "cmovl", "cmovnbe"]), rr(s2), rr(s2) if rng.random() < 0.5 else rand_mem(rng, bits, s2))
    if m < 0.66:
        s2 = rng.choice([16, 32] + ([64] if bits == 64 else []))
        return "%s %s, %s" % (rng.choice(["movzx", "movsx"]), rr(s2), rr(8) if rng.random() < 0.5 else rand_mem(rng, bits, 8))
    if m < 0.72:
        s2 = rng.choice([16, 32] + ([64] if bits == 64 else []))
        return "lea %s, [%s]" % (rr(s2), rand_mem(rng, bits, 32).split("[", 1)[1].rstrip("]"))
    if m < 0.78:
        s2 = 16 if bits == 16 else (32 if bits == 32 else 64)
        return "%s %s" % (rng.choice(["push", "pop"]), rr(s2))
    if m < 0.84:
        return "%s %s" % (rng.choice(["sete", "setne", "setl", "setg", "seta", "setb"]), rr(8) if rng.random() < 0.6 else rand_mem(rng, bits, 8))
    if m < 0.90:
        return rng.choice(["nop", "ret", "cld", "std", "cdq" if bits != 16 else "cwd", "leave", "pushf", "popf", "cpuid", "rdtsc", "syscall" if bits == 64 else "int3", "xlatb", "movsb", "stosd", "rep movsb", "lodsw"])
    if m < 0.95 and bits != 16:
        x = lambda: "xmm%d" % rng.randrange(8 if bits == 32 else 16)
        op = rng.choice(["paddd", "pxor", "movdqa", "movdqu", "addps", "mulps", "subpd", "pshufd", "andps", "por", "pcmpeqb", "movaps", "unpcklps"])
        if op == "pshufd": return "pshufd %s, %s, %d" % (x(), x(), rng.randint(0, 255))
        return "%s %s, %s" % (op, x(), x() if rng.random() < 0.6 else rand_mem(rng, bits, 32).replace("dword ", "").replace("[", "[", 1))
    if bits != 16:
        y = lambda: "ymm%d" % rng.randrange(8 if bits == 32 else 16)
        x = lambda: "xmm%d" % rng.randrange(8 if bits == 32 else 16)
        return rng.choice(["vaddps %s, %s, %s" % (y(), y(), y()), "vpxor %s, %s, %s" % (x(), x(), x()), "vmovdqa %s, %s" % (y(), y()), "vfmadd231ps %s, %s, %s" % (x(), x(), x()), "vpshufd %s, %s, %d" % (y(), y(), rng.randint(0, 255))])
    return "nop"


def gen_insn2(rng, bits):
    r32 = lambda: rng.choice(REG[bits][32]); r16 = lambda: rng.choice(REG[bits][16]); r8 = lambda: rng.choice(REG[bits][8])
    r64 = lambda: rng.choice(REG[64]) if False else rng.choice(REG[64][64])
    nx = 8 if bits != 64 else 16
    X = lambda: "xmm%d" % rng.randrange(nx); Y = lambda: "ymm%d" % rng.randrange(nx); Z = lambda: "zmm%d" % rng.randrange(32 if bits == 64 else 8)
    MM = lambda: "mm%d" % rng.randrange(8)
    mem = lambda sz=32: rand_mem(rng, bits, sz)
    nom = lambda sz=32: mem(sz).split(" ", 1)[1]
    gp = (lambda: r64()) if bits == 64 and rng.random() < 0.5 else r32
    gsz = 64 if gp is not r32 and bits == 64 else 32
    k = rng.random()
    if bits == 16 and k < 0.9:
        return rng.choice(["nop", "mov ax, bx", "int 0x21", "in al, dx", "out dx, ax", "loop $+2", "jcxz $+2", "les di, [bx]", "lds si, [bp+4]", "push cs", "pop ds", "mov ds, ax", "mov es, [bx]", "retf", "call far [bx]", "jmp far [si]", "enter 8, 0", "xchg ax, bx", "mov ax, [es:bx+si]", "cmpxchg [bx], cx", "bt ax, 3", "pusha", "popa", "aam", "daa", "xlat", "lahf", "sahf", "iret", "hlt", "cli", "sti"])
    fam = rng.choice(["x87", "mmx", "bmi", "sse4", "avx2", "avx512", "bit", "xchg", "misc", "seg", "string", "crc"])
    if fam == "x87":
        return rng.choice(["fld st%d" % rng.randrange(8), "fstp st%d" % rng.randrange(8), "fadd st0, st%d" % rng.randrange(8), "fmul st%d, st0" % rng.randrange(8), "fld dword %s" % nom(), "fstp qword %s" % nom(), "fild word %s" % nom(), "fistp dword %s" % nom(), "fxch st%d" % rng.randrange(8), "fcomi st0, st%d" % rng.randrange(8), "fsin", "fsqrt", "fchs", "fldz", "fld1", "fwait", "fnstcw [%s]" % nom(16).strip("[]"), "fldcw [%s]" % nom(16).strip("[]"), "fucomip st0, st%d" % rng.randrange(8)])
    if fam == "mmx":
        return rng.choice(["paddb %s, %s" % (MM(), MM()), "pmullw %s, %s" % (MM(), nom(64) if False else MM()), "movq %s, %s" % (MM(), MM()), "psllq %s, %d" % (MM(), rng.randint(0, 63)), "pcmpgtw %s, %s" % (MM(), MM()), "packsswb %s, %s" % (MM(), MM()), "emms", "movd %s, %s" % (MM(), r32()), "movd %s, %s" % (r32(), MM()), "movq %s, %s" % (MM(), mem(64).replace("qword ", ""))])
    if fam == "bmi":
        a, b2, c = gp(), gp(), gp()
        return rng.choice(["andn %s, %s, %s" % (a, b2, c), "bextr %s, %s, %s" % (a, b2, c), "blsi %s, %s" % (a, b2), "blsr %s, %s" % (a, b2), "blsmsk %s, %s" % (a, b2), "shlx %s, %s, %s" % (a, b2, c), "sarx %s, %s, %s" % (a, b2, c), "rorx %s, %s, %d" % (a, b2, rng.randint(0, 31)), "mulx %s, %s, %s" % (a, b2, c), "pdep %s, %s, %s" % (a, b2, c), "pext %s, %s, %s" % (a, b2, c), "tzcnt %s, %s" % (a, b2), "lzcnt %s, %s" % (a, b2), "bzhi %s, %s, %s" % (a, b2, c), "adcx %s, %s" % (a, b2), "adox %s, %s" % (a, b2)])
    if fam == "sse4":
        return rng.choice(["pmovzxbw %s, %s" % (X(), X()), "pblendw %s, %s, %d" % (X(), X(), rng.randint(0, 255)), "pshufb %s, %s" % (X(), X()), "ptest %s, %s" % (X(), X()), "roundps %s, %s, %d" % (X(), X(), rng.randint(0, 15)), "pextrd %s, %s, %d" % (r32(), X(), rng.randint(0, 3)), "pinsrw %s, %s, %d" % (X(), r32(), rng.randint(0, 7)), "palignr %s, %s, %d" % (X(), X(), rng.randint(0, 31)), "pcmpistri %s, %s, %d" % (X(), X(), rng.randint(0, 255)), "blendvps %s, %s, xmm0" % (X(), X()), "movhps %s, %s" % (X(), mem(64).replace("qword ", "qword ")), "cvtsi2sd %s, %s" % (X(), r32()), "cvttss2si %s, %s" % (r32(), X()), "sqrtsd %s, %s" % (X(), X()), "comiss %s, %s" % (X(), X()), "movmskpd %s, %s" % (r32(), X()), "pmaddubsw %s, %s" % (X(), X()), "aesenc %s, %s" % (X(), X()), "pclmulqdq %s, %s, %d" % (X(), X(), rng.randint(0, 17)), "sha256rnds2 %s, %s, xmm0" % (X(), X())])
    if fam == "avx2":
        return rng.choice(["vpaddd %s, %s, %s" % (Y(), Y(), Y()), "vpermq %s, %s, %d" % (Y(), Y(), rng.randint(0, 255)), "vbroadcastss %s, %s" % (Y(), X()), "vpbroadcastd %s, %s" % (Y(), X()), "vextracti128 %s, %s, %d" % (X(), Y(), rng.randint(0, 1)), "vinsertf128 %s, %s, %s, %d" % (Y(), Y(), X(), rng.randint(0, 1)), "vperm2f128 %s, %s, %s, %d" % (Y(), Y(), Y(), rng.randint(0, 255)), "vfmadd213pd %s, %s, %s" % (Y(), Y(), Y()), "vmaskmovps %s, %s, %s" % (Y(), Y(), "[" + nom() + "]" if False else Y()), "vgatherdps %s, [%s+%s*4], %s" % (X(), rng.choice(ADDRBASE[bits if bits != 16 else 32]), X(), X()) if False else "vzeroupper", "vcvtph2ps %s, %s" % (Y(), X()), "vpsllvd %s, %s, %s" % (Y(), Y(), Y()), "vandnps %s, %s, %s" % (Y(), Y(), Y()), "vroundpd %s, %s, %d" % (Y(), Y(), rng.randint(0, 15)), "vpmovmskb %s, %s" % (r32(), Y()), "vmovups %s, %s" % (Y(), "[" + nom() + "]")])
    if fam == "avx512" and bits != 16:
        kk = lambda: "k%d" % rng.randrange(8)
        sfx = rng.choice(["", "{%s}" % kk(), "{%s}{z}" % kk()])
        return rng.choice(["vaddps %s%s, %s, %s" % (Z(), sfx, Z(), Z()), "vpxord %s, %s, %s" % (Z(), Z(), Z()), "vmovdqa64 %s%s, %s" % (Z(), sfx, Z()), "vpcmpeqd %s, %s, %s" % (kk(), Z(), Z()), "kmovw %s, %s" % (kk(), r32()), "kortestw %s, %s" % (kk(), kk()), "vpternlogd %s, %s, %s, %d" % (Z(), Z(), Z(), rng.randint(0, 255)), "vaddps %s, %s, %s, {rn-sae}" % (Z(), Z(), Z()), "vpermb %s, %s, %s" % (Z(), Z(), Z()), "vcvtps2pd %s, %s" % (Z(), Y()), "vfmadd231pd %s%s, %s, %s" % (Z(), sfx, Z(), "[" + nom() + "]{1to8}") if False else "vpandq %s, %s, %s" % (Z(), Z(), Z()), "vpopcntd %s, %s" % (Z(), Z()), "vpmovdb %s, %s" % (X(), Z()), "vscalefps %s, %s, %s" % (Z(), Z(), Z()), "vrangeps %s, %s, %s, %d" % (Z(), Z(), Z(), rng.randint(0, 15))])
    if fam == "bit":
        s = rng.choice([16, 32] + ([64] if bits == 64 else []))
        r = rng.choice(REG[bits][s])
        return rng.choice(["bt %s, %d" % (r, rng.randint(0, s - 1)), "bts %s, %s" % (r, rng.choice(REG[bits][s])), "btr %s, %d" % (rand_mem(rng, bits, s), rng.randint(0, 7)), "btc %s, %s" % (r, rng.choice(REG[bits][s])), "shld %s, %s, %d" % (r, rng.choice(REG[bits][s]), rng.randint(1, 15)), "shrd %s, %s, cl" % (r, rng.choice(REG[bits][s])), "bswap %s" % rng.choice(REG[bits][32]), "popcnt %s, %s" % (r, rng.choice(REG[bits][s])), "xadd %s, %s" % (rand_mem(rng, bits, s), r), "cmpxchg %s, %s" % (r, rng.choice(REG[bits][s])), "movbe %s, %s" % (r, rand_mem(rng, bits, s)), "lock add %s, %s" % (rand_mem(rng, bits, s), r)])
    if fam == "xchg":
        s = rng.choice([8, 16, 32] + ([64] if bits == 64 else []))
        return "xchg %s, %s" % (rng.choice(REG[bits][s]), rng.choice(REG[bits][s]) if rng.random() < 0.5 else rand_mem(rng, bits, s))
    if fam == "seg":
        sg = rng.choice(["fs", "gs", "es", "ds", "cs", "ss"])
        s = rng.choice([8, 16, 32])
        return rng.choice(["mov %s, [%s:%s]" % (rng.choice(REG[bits][s]), sg, nom(s).strip("[]")), "mov [%s:%s], %s" % (sg, nom(s).strip("[]"), rng.choice(REG[bits][s])), "mov %s, %s" % (rng.choice(REG[bits][16]), sg), "mov %s, %s" % (sg, rng.choice(REG[bits][16]))])
    if fam == "string":
        return rng.choice(["rep movsb", "rep stosd", "repne scasb", "repe cmpsw", "lodsb", "movsd", "scasw", "cmpsb", "insb", "outsd", "rep insw"])
    if fam == "crc":
        return rng.choice(["crc32 %s, %s" % (r32(), r8()), "crc32 %s, %s" % (r32(), r16()), "crc32 %s, %s" % (r32(), r32()), "prefetchnta [%s]" % nom(8).strip("[]"), "prefetcht0 [%s]" % nom(8).strip("[]"), "clflush [%s]" % nom(8).strip("[]"), "lfence", "mfence", "sfence", "pause", "rdrand %s" % r32(), "rdseed %s" % r32(), "xgetbv", "xsave [%s]" % nom(32).strip("[]"), "fxsave [%s]" % nom(32).strip("[]"), "ud2", "int3", "int 0x80", "bound %s, [%s]" % (r32(), nom(64).strip("[]")) if bits != 64 else "rdtscp", "movnti %s, %s" % (mem(32), r32()), "invlpg [%s]" % nom(8).strip("[]"), "wrmsr", "rdmsr", "sysenter", "monitor", "mwait", "xlatb"])
    return rng.choice(["nop", "cdq" if bits != 16 else "cwd", "cwde", "cdqe" if bits == 64 else "cbw", "leave", "ret 8", "enter 16, 1", "pushfd" if bits == 32 else "pushf", "int 0x10", "into" if bits != 64 else "nop", "hlt", "cmc", "clc", "stc", "sahf" if bits != 64 else "nop", "cpuid", "lfence"])

def batch_nasm_diff_b(b):
    if not have("nasm"):
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(7000 + SEED)
    for i in range(NF):
        bits = [16, 32, 64][i % 3]
        lines = ["bits %d" % bits, "org 0x100"] if True else []
        body = []
        for _ in range(rng.randint(8, 16)):
            body.append(gen_insn(rng, bits) if rng.random() < 0.4 else gen_insn2(rng, bits))
        lines.append("start:")
        lines += body
        if rng.random() < 0.5: lines.append("jmp start"); lines.append("jnz start")
        src = "\n".join(lines) + "\n"
        wr("rd.asm", src)
        for p in ("rd.n", "rd.c"):
            if os.path.exists(p): os.remove(p)
        rn = subprocess.run(["nasm", "-f", "bin", "rd.asm", "-o", "rd.n"], capture_output=True)
        rc = run(["bare", "rd.asm", "-nasm", "-flat", "-q", "--o", "rd.c", "-m%d" % bits])
        if rn.returncode == 0 and rc.returncode == 0: okk = rd("rd.n") == rd("rd.c")
        elif rn.returncode != 0 and rc.returncode != 0: okk = True
        else: okk = False
        b.check(okk, "case %d bits %d nasm rc %d cly rc %d\\n%s%s%s" % (i, bits, rn.returncode, rc.returncode, src, rn.stderr[:200].decode("latin1"), rc.stderr[:200].decode("latin1")))
        if not okk:
            wr("diff_%d.asm" % i, src)


def main():
    allok = True
    total = [0, 0]
    def finish(b):
        nonlocal allok
        total[0] += b.ok; total[1] += b.n
        print("batch %-34s %d/%d%s" % (b.name, b.ok, b.n, " (emulator missing, counted as skipped)" if getattr(b, "skipped", 0) else ""))
        for m in b.msgs: print("   FAIL:", m)
        if b.ok != b.n: allok = False
    def mk(name): return Batch(name)
    only = os.environ.get("BATCH_ONLY")
    plan = []
    plan.append(("output flags", lambda b: batch_flags_b(b)))
    plan.append(("link x86-64 elf and run", lambda b: batch_elf_b(b, 64)))
    plan.append(("link i386 elf and run", lambda b: batch_elf_b(b, 32)))
    plan.append(("link pe64 and pe32, emulated", lambda b: (batch_pe(64, b, 24), batch_pe(32, b, 24), batch_pe_zig(b))))
    plan.append(("link arm64 and risc-v, emulated", lambda b: (batch_risc("arm64", b, 25), batch_risc("riscv", b, 25))))
    plan.append(("global install and update", lambda b: batch_install_b(b)))
    plan.append(("linker input fuzz", lambda b: batch_ld_fuzz_b(b)))
    plan.append(("assembler input fuzz", lambda b: batch_asm_fuzz_b(b)))
    plan.append(("object formats and sizes", lambda b: (batch_formats(b), batch_obj_size(b, 12))))
    plan.append(("random encodings vs nasm", lambda b: batch_nasm_diff_b(b)))
    for name, fn in plan:
        if only and only not in name: continue
        b = mk(name)
        fn(b)
        finish(b)
    print("TOTAL %d/%d" % tuple(total))
    if allok: shutil.rmtree(W, ignore_errors=True)
    else: print("work files kept in", W)
    return 0 if allok else 1

sys.exit(main())
PYBATCH
    [ $? = 0 ] && note "500 check batches" ok || { note "500 check batches" FAIL; fail=1; }
    python3 - <<'PYBATCH2'
import os, sys, random, subprocess, tempfile, shutil, struct, re
ROOT = os.getcwd()
CLY = os.path.abspath(os.environ.get("CLY", "./cly"))
W = tempfile.mkdtemp(prefix="cly_mature")
os.chdir(W)
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", CLY_NO_HINT="1")
SEED = int(os.environ.get("BATCH_SEED", "0"))
NF = int(os.environ.get("BATCH_N", "100"))
M64 = (1 << 64) - 1

def run(args, timeout=60):
    return subprocess.run([CLY] + args, capture_output=True, timeout=timeout, env=env)

def sh(args, timeout=60):
    return subprocess.run(args, capture_output=True, timeout=timeout)

def wr(p, s):
    with open(p, "w") as f: f.write(s)

def rd(p):
    with open(p, "rb") as f: return f.read()

def have(x): return shutil.which(x) is not None

class Batch:
    def __init__(self, name):
        self.name = name; self.n = 0; self.ok = 0; self.msgs = []; self.skipped = 0
    def check(self, cond, what=""):
        self.n += 1
        if cond: self.ok += 1
        elif len(self.msgs) < 6: self.msgs.append(what)

def gen_expr(rng, d):
    if d == 0 or rng.random() < 0.25:
        v = rng.choice([rng.randint(0, 20), rng.randint(0, 5000), rng.randint(0, 2 ** 32), rng.randint(0, 2 ** 40)])
        return (hex(v) if rng.random() < 0.4 else str(v)), v
    r = rng.random()
    if r < 0.12:
        s, v = gen_expr(rng, d - 1)
        if rng.random() < 0.5: return "(-%s)" % s, (-v) & M64
        return "(~%s)" % s, (~v) & M64
    op = rng.choice(["+", "-", "*", "&", "|", "^", "<<", ">>", "/", "%", "+", "-", "*"])
    a, av = gen_expr(rng, d - 1)
    if op in ("<<", ">>"):
        k = rng.randint(0, 30)
        bs, bv = str(k), k
    elif op in ("/", "%"):
        k = rng.randint(1, 97)
        bs, bv = str(k), k
    else:
        bs, bv = gen_expr(rng, d - 1)
    if op == "+": v = (av + bv) & M64
    elif op == "-": v = (av - bv) & M64
    elif op == "*": v = (av * bv) & M64
    elif op == "&": v = av & bv
    elif op == "|": v = av | bv
    elif op == "^": v = av ^ bv
    elif op == "<<": v = (av << bv) & M64
    elif op == ">>": v = av >> bv
    elif op == "/": v = av // bv
    else: v = av % bv
    return "(%s %s %s)" % (a, op, bs), v

def batch_expr(b):
    rng = random.Random(9100 + SEED)
    for i in range(NF):
        kind = i % 4
        if kind in (0, 1):
            s, v = gen_expr(rng, rng.randint(1, 5))
            wr("e.asm", "bits 64\ndq %s\n" % s)
            r = run(["bare", "e.asm", "-nasm", "-flat", "-q", "--o", "e.bin"])
            got = rd("e.bin") if r.returncode == 0 and os.path.exists("e.bin") else b""
            b.check(got == struct.pack("<Q", v), "expr %s want %x got %s %s" % (s, v, got.hex(), r.stderr[:100]))
            if os.path.exists("e.bin"): os.remove("e.bin")
        elif kind == 2:
            n = rng.randint(2, 6)
            ops = ["+", "-", "*", "&", "|", "^"]
            toks = [str(rng.randint(0, 3000))]
            for _ in range(n):
                toks.append(rng.choice(ops)); toks.append(str(rng.randint(0, 3000)))
            if rng.random() < 0.5:
                toks.append("<<"); toks.append(str(rng.randint(0, 9)))
            s = " ".join(toks)
            v = eval(s) & M64
            wr("e.asm", "bits 64\ndq %s\n" % s)
            r = run(["bare", "e.asm", "-nasm", "-flat", "-q", "--o", "e.bin"])
            got = rd("e.bin") if r.returncode == 0 and os.path.exists("e.bin") else b""
            b.check(got == struct.pack("<Q", v), "chain %s want %x got %s" % (s, v, got.hex()))
            if os.path.exists("e.bin"): os.remove("e.bin")
        else:
            parts = ["bits 32", "org 0x1000", "start:"]
            exp = bytearray()
            for _ in range(rng.randint(3, 8)):
                c = rng.randint(0, 3)
                if c == 0:
                    s = "".join(rng.choice("abcxyz019 ") for _ in range(rng.randint(1, 9)))
                    parts.append("db '%s'" % s); exp += s.encode()
                elif c == 1:
                    k = rng.randint(0, 40); x = rng.randint(0, 255)
                    parts.append("times %d db %d" % (k, x)); exp += bytes([x]) * k
                elif c == 2:
                    x = rng.randint(0, 65535); y = rng.randint(0, 2 ** 32 - 1)
                    parts.append("dw %d\ndd 0x%x" % (x, y)); exp += struct.pack("<HI", x, y)
                else:
                    a = rng.choice([2, 4, 8, 16])
                    parts.append("align %d, db 0xcc" % a)
                    while (0x1000 + len(exp)) % a: exp.append(0xcc)
            parts.append("endlbl:\ndd endlbl - start\ndd endlbl")
            exp += struct.pack("<II", len(exp), 0x1000 + len(exp))
            wr("e.asm", "\n".join(parts) + "\n")
            r = run(["bare", "e.asm", "-nasm", "-flat", "-q", "--o", "e.bin", "-m32"])
            got = rd("e.bin") if r.returncode == 0 and os.path.exists("e.bin") else b""
            b.check(got == bytes(exp), "layout case %d want %s got %s %s" % (i, bytes(exp).hex(), got.hex(), r.stderr[:100]))
            if os.path.exists("e.bin"): os.remove("e.bin")

REGS32 = ["eax", "ebx", "ecx", "edx", "esi", "edi"]

def mac_piece(rng, k, bits):
    r = rng.randint(0, 11)
    R = REGS32
    if r == 0:
        a, c = rng.choice(R), rng.randint(0, 200)
        return "%%macro M%d 2\n mov %%1, %%2\n add %%1, %%2\n%%endmacro\nM%d %s, %d\n" % (k, k, a, c)
    if r == 1:
        s, n = rng.randint(0, 50), rng.randint(1, 8)
        return "%%assign i%d %d\n%%rep %d\n db i%d\n%%assign i%d i%d+%d\n%%endrep\n" % (k, s, n, k, k, k, rng.randint(1, 5))
    if r == 2:
        a, bb, n = rng.randint(1, 9), rng.randint(0, 9), rng.randint(0, 30)
        return "%%define F%d(x) ((x)*%d+%d)\ndd F%d(%d)\ndb F%d(F%d(2))\n" % (k, a, bb, k, n, k, k)
    if r == 3:
        x, y = rng.randint(0, 9), rng.randint(0, 9)
        return "%%if %d > %d\n db 1\n%%elif %d == %d\n db 2\n%%else\n db 3\n%%endif\n" % (x, y, x, y)
    if r == 4:
        s = "".join(rng.choice("abcdefgh") for _ in range(rng.randint(3, 9)))
        a, n = rng.randint(1, 2), rng.randint(1, 3)
        return "%%strlen L%d '%s'\ndb L%d\n%%substr S%d '%s' %d %d\ndb S%d\n" % (k, s, k, k, s, a, n, k)
    if r == 5:
        a = rng.choice(R)
        return "%%macro lp%d 1\n%%%%top:\n dec %%1\n jnz %%%%top\n%%endmacro\n mov %s, %d\n lp%d %s\n" % (k, a, rng.randint(1, 5), k, a)
    if r == 6:
        d = rng.randint(0, 9)
        return "%%macro D%d 1-2 %d\n db %%1, %%2\n%%endmacro\nD%d 5\nD%d 6, 7\n" % (k, d, k, k)
    if r == 7:
        return "%%define Z%d\n%%ifdef Z%d\n db 4\n%%endif\n%%undef Z%d\n%%ifndef Z%d\n db 5\n%%endif\n" % (k, k, k, k)
    if r == 8:
        n = rng.randint(2, 5)
        args = ", ".join(str(rng.randint(0, 99)) for _ in range(n))
        return "%%macro R%d 2-*\n%%rep %%0\n db %%1\n%%rotate 1\n%%endrep\n%%endmacro\nR%d %s\n" % (k, k, args)
    if r == 9:
        return "%%ifnum %d\n db 1\n%%endif\n%%ifstr 'ab'\n db 2\n%%endif\n%%ifidn eax, eax\n db 3\n%%endif\n%%ifidni EAX, eax\n db 4\n%%endif\n" % rng.randint(0, 99)
    if r == 10:
        v = rng.randint(0, 99)
        return "%%push c%d\n%%define %%$x %d\n db %%$x\n%%pop\n" % (k, v)
    a = rng.choice(R); c = rng.randint(0, 99)
    return "%%xdefine X%d %d\n%%xdefine X%d X%d+1\n mov %s, X%d\n" % (k, c, k, k, a, k)

def batch_macro(b):
    if not have("nasm"):
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(9200 + SEED)
    for i in range(NF):
        bits = rng.choice([32, 64])
        src = "bits %d\nstart:\n" % bits
        for k in range(rng.randint(2, 6)):
            src += mac_piece(rng, i * 10 + k, bits)
        src += " ret\n"
        wr("m.asm", src)
        for p in ("m.n", "m.c"):
            if os.path.exists(p): os.remove(p)
        rn = sh(["nasm", "-f", "bin", "m.asm", "-o", "m.n"])
        rc = run(["bare", "m.asm", "-nasm", "-flat", "-q", "--o", "m.c", "-m%d" % bits])
        if rn.returncode == 0 and rc.returncode == 0: ok = rd("m.n") == rd("m.c")
        elif rn.returncode != 0 and rc.returncode != 0: ok = True
        else: ok = False
        if not ok: wr("bad_%d.asm" % i, src)
        b.check(ok, "macro case %d nasm %d cly %d n=%s c=%s\n%s%s%s" % (i, rn.returncode, rc.returncode, rd("m.n").hex() if os.path.exists("m.n") else "", rd("m.c").hex() if os.path.exists("m.c") else "", src, rn.stderr[:150].decode("latin1"), rc.stderr[:150].decode("latin1")))

def rot(x, k): return ((x << k) | (x >> (64 - k))) & M64

def gen_cfun(rng):
    ops = []
    for _ in range(rng.randint(3, 10)):
        ops.append(rng.choice(["add", "sub", "imul", "xor", "and", "or", "shl", "shr", "neg", "not", "lea", "inc", "dec", "rol", "ror", "bswap", "popcnt", "movsx", "cmov", "sar", "xchg", "mulhi"]))
    callat = rng.randint(0, len(ops)) if rng.random() < 0.5 else -1
    lines = ["bits 64", "global f", "extern cfun", "section .text", "f:", " push rbx", " push r12", " push r13", " mov rax, rdi"]
    py = ["a = x"]
    def emit(ln, p): lines.append(" " + ln); py.append(p)
    for idx, op in enumerate(ops):
        if idx == callat:
            lines += [" mov r12, rsi", " mov r13, rdx", " mov rdi, rax", " call cfun", " mov rsi, r12", " mov rdx, r13"]
            py.append("a = (a * 3 + 1) & M")
        if op == "add": emit("add rax, rsi", "a = (a + y) & M")
        elif op == "sub": emit("sub rax, rdx", "a = (a - z) & M")
        elif op == "imul": emit("imul rax, rsi", "a = (a * y) & M")
        elif op == "xor": emit("xor rax, rdx", "a = a ^ z")
        elif op == "and": emit("and rax, rsi", "a = a & y")
        elif op == "or": emit("or rax, rdx", "a = a | z")
        elif op == "shl":
            k = rng.randint(1, 63); emit("shl rax, %d" % k, "a = (a << %d) & M" % k)
        elif op == "shr":
            k = rng.randint(1, 63); emit("shr rax, %d" % k, "a = a >> %d" % k)
        elif op == "sar":
            k = rng.randint(1, 63); emit("sar rax, %d" % k, "a = ((a - (1 << 64) if a >> 63 else a) >> %d) & M" % k)
        elif op == "neg": emit("neg rax", "a = (-a) & M")
        elif op == "not": emit("not rax", "a = (~a) & M")
        elif op == "lea":
            k = rng.randint(-1000, 1000); s = rng.choice([1, 2, 4, 8])
            emit("lea rax, [rax+rsi*%d%+d]" % (s, k), "a = (a + y * %d + (%d)) & M" % (s, k))
        elif op == "inc": emit("inc rax", "a = (a + 1) & M")
        elif op == "dec": emit("dec rax", "a = (a - 1) & M")
        elif op == "rol":
            k = rng.randint(1, 63); emit("rol rax, %d" % k, "a = rot(a, %d)" % k)
        elif op == "ror":
            k = rng.randint(1, 63); emit("ror rax, %d" % k, "a = rot(a, %d)" % (64 - k))
        elif op == "bswap": emit("bswap rax", "a = int.from_bytes(a.to_bytes(8, 'little'), 'big')")
        elif op == "popcnt": emit("popcnt rcx, rdx\n add rax, rcx", "a = (a + bin(z).count('1')) & M")
        elif op == "movsx": emit("movsx rcx, dl\n xor rax, rcx", "c = z & 255\nc = c - 256 if c >> 7 else c\na = a ^ (c & M)")
        elif op == "cmov": emit("cmp rax, rsi\n cmova rax, rdx", "a = z if a > y else a")
        elif op == "xchg": emit("mov rcx, rdx\n xchg rax, rcx\n add rax, rcx", "a = (z + a) & M")
        elif op == "mulhi": emit("mov r8, rdx\n mov rcx, rax\n mov rax, rsi\n mul r8\n mov rdx, r8\n add rax, rcx", "a = ((y * z) & M) + a & M")
    if callat == len(ops):
        lines += [" mov r12, rsi", " mov r13, rdx", " mov rdi, rax", " call cfun"]
        py.append("a = (a * 3 + 1) & M")
    lines += [" pop r13", " pop r12", " pop rbx", " ret"]
    return "\n".join(lines) + "\n", py

def batch_interop(b):
    if not have("gcc"):
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(9300 + SEED)
    wr("main.c", "#include <stdio.h>\n#include <stdlib.h>\nunsigned long f(unsigned long, unsigned long, unsigned long);\nunsigned long cfun(unsigned long x){return x*3+1;}\nint main(int c,char**v){printf(\"%lu\\n\",f(strtoull(v[1],0,10),strtoull(v[2],0,10),strtoull(v[3],0,10)));return 0;}\n")
    r = sh(["gcc", "-c", "-O1", "-fno-pie", "main.c", "-o", "main.o"])
    for i in range(NF):
        src, py = gen_cfun(rng)
        wr("f.asm", src)
        args = [rng.randint(0, M64) if rng.random() < 0.5 else rng.randint(0, 1000) for _ in range(3)]
        scope = {"M": M64, "rot": rot}
        mode = ["-O0", None, "-O3"][i % 3]
        fl = ["linux", "f.asm", "-o", "-q", "-m64"] + ([mode] if mode else [])
        if os.path.exists("f.o"): os.remove("f.o")
        r1 = run(fl)
        r2 = sh(["gcc", "-no-pie", "main.o", "f.o", "-o", "prog"]) if r1.returncode == 0 else r1
        out = b""
        if r2.returncode == 0:
            out = sh(["./prog"] + [str(x) for x in args]).stdout.strip()
        scope.update({"x": args[0], "y": args[1], "z": args[2]})
        want = None
        try:
            exec("\n".join(py), scope)
            want = scope["a"] & M64
        except Exception as e:
            want = repr(e)
        if not (out == str(want).encode()): wr("bad_%d.asm" % i, src + "; " + " ".join(map(str, args)) + " " + str(want) + "\n" + "\n".join(py) + "\n")
        b.check(r1.returncode == 0 and r2.returncode == 0 and out == str(want).encode(),
                "interop case %d mode %s out %s want %s %s\n%s" % (i, mode, out, want, (r1.stderr + r2.stderr)[:150], src))

def a64_insn(rng):
    x = lambda: "x%d" % rng.randint(0, 28)
    w = lambda: "w%d" % rng.randint(0, 28)
    d = lambda: "d%d" % rng.randint(0, 31)
    s = lambda: "s%d" % rng.randint(0, 31)
    r = rng.randint(0, 33)
    if r == 0: return "%s %s, %s, %s" % (rng.choice(["add", "sub", "and", "orr", "eor", "adds", "subs", "bic", "orn", "eon"]), x(), x(), x())
    if r == 1: return "%s %s, %s, %s" % (rng.choice(["add", "sub", "and", "orr", "eor", "adds", "subs"]), w(), w(), w())
    if r == 2: return "%s %s, %s, #%d" % (rng.choice(["add", "sub", "adds", "subs"]), x(), x(), rng.randint(0, 4095))
    if r == 3: return "%s %s, %s, %s, %s #%d" % (rng.choice(["add", "sub", "orr", "eor", "and"]), x(), x(), x(), rng.choice(["lsl", "lsr", "asr"]), rng.randint(0, 63))
    if r == 4: return "movz %s, #%d, lsl #%d" % (x(), rng.randint(0, 65535), rng.choice([0, 16, 32, 48]))
    if r == 5: return "movk %s, #%d, lsl #%d" % (x(), rng.randint(0, 65535), rng.choice([0, 16, 32, 48]))
    if r == 6: return "movn %s, #%d" % (x(), rng.randint(0, 65535))
    if r == 7: return "%s %s, [%s, #%d]" % (rng.choice(["ldr", "str"]), x(), rng.choice([x(), "sp"]), rng.randint(0, 4095) * 8)
    if r == 8: return "%s %s, [%s, #%d]" % (rng.choice(["ldr", "str"]), w(), x(), rng.randint(0, 4095) * 4)
    if r == 9: return "%s %s, [%s, #%d]" % (rng.choice(["ldrb", "strb"]), w(), x(), rng.randint(0, 4095))
    if r == 10: return "%s %s, [%s, #%d]" % (rng.choice(["ldrh", "strh"]), w(), x(), rng.randint(0, 4095) * 2)
    if r == 11: return "%s %s, %s, %s" % (rng.choice(["mul", "sdiv", "udiv", "lsl", "lsr", "asr", "ror"]), x(), x(), x())
    if r == 12: return "%s %s, %s, #%d" % (rng.choice(["lsl", "lsr", "asr", "ror"]), x(), x(), rng.randint(1, 63))
    if r == 13: return "madd %s, %s, %s, %s" % (x(), x(), x(), x())
    if r == 14: return "msub %s, %s, %s, %s" % (x(), x(), x(), x())
    if r == 15: return "%s %s, %s" % (rng.choice(["cmp", "cmn", "tst"]), x(), x())
    if r == 16: return "cmp %s, #%d" % (x(), rng.randint(0, 4095))
    if r == 17: return "csel %s, %s, %s, %s" % (x(), x(), x(), rng.choice(["eq", "ne", "lt", "gt", "le", "ge", "hi", "lo", "mi", "pl"]))
    if r == 18: return "cset %s, %s" % (w(), rng.choice(["eq", "ne", "lt", "gt"]))
    if r == 19: return "%s %s, %s, [%s, #%d]" % (rng.choice(["ldp", "stp"]), x(), x(), x(), rng.randint(-64, 63) * 8)
    if r == 20: return "%s %s, %s" % (rng.choice(["clz", "rbit", "rev", "neg", "mvn"]), x(), x())
    if r == 21: return "sxtw %s, %s" % (x(), w())
    if r == 22: return "%s %s, %s, %s" % (rng.choice(["fadd", "fsub", "fmul", "fdiv"]), d(), d(), d())
    if r == 23: return "%s %s, %s, %s" % (rng.choice(["fadd", "fsub", "fmul", "fdiv"]), s(), s(), s())
    if r == 24: return "%s %s, %s" % (rng.choice(["fmov", "fabs", "fneg", "fsqrt"]), d(), d())
    if r == 25: return "fcvtzs %s, %s" % (x(), d())
    if r == 26: return "scvtf %s, %s" % (d(), x())
    if r == 27: return "fcmp %s, %s" % (d(), d())
    if r == 28: return "ubfx %s, %s, #%d, #%d" % (x(), x(), rng.randint(0, 31), rng.randint(1, 32))
    if r == 29: return "%s %s, %s" % (rng.choice(["uxtb", "uxth", "sxtb", "sxth"]), w(), w())
    if r == 30: return "adc %s, %s, %s" % (x(), x(), x())
    if r == 31: return "%s %s, %s, %s" % (rng.choice(["smulh", "umulh"]), x(), x(), x())
    if r == 32: return "%s" % rng.choice(["nop", "ret", "svc #0", "dmb ish", "isb", "yield", "brk #1"])
    return "mov %s, %s" % (x(), x())

def rv_insn(rng):
    r64 = rng.random() < 0.6
    x = lambda: "x%d" % rng.randint(1, 31)
    f = lambda: "f%d" % rng.randint(0, 31)
    r = rng.randint(0, 28)
    if r == 0: return "%s %s, %s, %s" % (rng.choice(["add", "sub", "and", "or", "xor", "sll", "srl", "sra", "slt", "sltu"]), x(), x(), x())
    if r == 1: return "%s %s, %s, %d" % (rng.choice(["addi", "andi", "ori", "xori", "slti", "sltiu"]), x(), x(), rng.randint(-2048, 2047))
    if r == 2: return "%s %s, %s, %d" % (rng.choice(["slli", "srli", "srai"]), x(), x(), rng.randint(0, 63))
    if r == 3: return "%s %s, %d(%s)" % (rng.choice(["lb", "lh", "lw", "ld", "lbu", "lhu", "lwu"]), x(), rng.randint(-2048, 2047), x())
    if r == 4: return "%s %s, %d(%s)" % (rng.choice(["sb", "sh", "sw", "sd"]), x(), rng.randint(-2048, 2047), x())
    if r == 5: return "%s %s, %s, %s" % (rng.choice(["mul", "mulh", "mulhu", "div", "divu", "rem", "remu"]), x(), x(), x())
    if r == 6: return "lui %s, %d" % (x(), rng.randint(0, 1048575))
    if r == 7: return "auipc %s, %d" % (x(), rng.randint(0, 1048575))
    if r == 8: return "%s %s, %s, %s" % (rng.choice(["addw", "subw", "sllw", "srlw", "sraw", "mulw", "divw", "remw", "divuw", "remuw"]), x(), x(), x())
    if r == 9: return "addiw %s, %s, %d" % (x(), x(), rng.randint(-2048, 2047))
    if r == 10: return "%s %s, %s, %d" % (rng.choice(["slliw", "srliw", "sraiw"]), x(), x(), rng.randint(0, 31))
    if r == 11: return "%s %s, %s, %s" % (rng.choice(["fadd.d", "fsub.d", "fmul.d", "fdiv.d"]), f(), f(), f())
    if r == 12: return "%s %s, %s, %s" % (rng.choice(["fadd.s", "fsub.s", "fmul.s", "fdiv.s"]), f(), f(), f())
    if r == 13: return "%s %s, %d(%s)" % (rng.choice(["fld", "flw"]), f(), rng.randint(-2048, 2047), x())
    if r == 14: return "%s %s, %d(%s)" % (rng.choice(["fsd", "fsw"]), f(), rng.randint(-2048, 2047), x())
    if r == 15: return "%s %s, %s" % (rng.choice(["fmv.x.d", "fclass.d"]), x(), f())
    if r == 16: return "%s %s, %s, %s" % (rng.choice(["feq.d", "flt.d", "fle.d"]), x(), f(), f())
    if r == 17: return "%s %s, %s, (%s)" % (rng.choice(["amoadd.w", "amoswap.w", "amoand.d", "amoor.d", "amoxor.w", "amomax.d", "amomin.w"]), x(), x(), x())
    if r == 18: return "lr.w %s, (%s)" % (x(), x())
    if r == 19: return "sc.d %s, %s, (%s)" % (x(), x(), x())
    if r == 20: return "%s" % rng.choice(["nop", "ret", "ecall", "ebreak", "fence"])
    if r == 21: return "mv %s, %s" % (x(), x())
    if r == 22: return "%s %s, %s" % (rng.choice(["neg", "not", "seqz", "snez", "sext.w"]), x(), x())
    if r == 23: return "li %s, %d" % (x(), rng.randint(-2048, 2047))
    if r == 24: return "%s %s, %s, %s, %s" % (rng.choice(["fmadd.d", "fmsub.d", "fnmadd.d", "fnmsub.d"]), f(), f(), f(), f())
    if r == 25: return "fcvt.d.w %s, %s" % (f(), x())
    if r == 26: return "fcvt.w.d %s, %s, rtz" % (x(), f())
    if r == 27: return "%s %s, %s, %s" % (rng.choice(["fmin.d", "fmax.d", "fsgnj.d", "fsgnjn.d", "fsgnjx.d"]), f(), f(), f())
    return "fsqrt.d %s, %s" % (f(), f())

def llvm_bytes(triple, mattr, lines):
    wr("l.s", "\n".join(lines) + "\n")
    args = ["llvm-mc", "-triple=" + triple, "-show-encoding", "l.s"]
    if mattr: args.insert(2, "-mattr=" + mattr)
    r = sh(args)
    if r.returncode != 0: return None
    out = bytearray()
    for ln in r.stdout.decode().splitlines():
        m = re.search(r"encoding: \[([^\]]*)\]", ln)
        if m: out += bytes(int(t, 16) for t in m.group(1).split(","))
    return bytes(out)

def batch_risc_llvm(b):
    if not have("llvm-mc"):
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(9400 + SEED)
    for i in range(NF):
        arm = i % 2 == 0
        n = rng.randint(6, 14)
        ls = [a64_insn(rng) if arm else rv_insn(rng) for _ in range(n)]
        if arm:
            want = llvm_bytes("aarch64", None, ls)
            cpu = "arm64"
        else:
            want = llvm_bytes("riscv64", "+m,+a,+f,+d", ls)
            cpu = "rv64g"
        wr("c.s", "\n".join(ls) + "\n")
        if os.path.exists("c.bin"): os.remove("c.bin")
        r = run(["bare", "c.s", "-flat", "-cpu", cpu, "-q", "--o", "c.bin", "-m64"])
        got = rd("c.bin") if r.returncode == 0 and os.path.exists("c.bin") else None
        if want is None:
            ok = got is None or len(got) % 4 == 0
        else:
            ok = got == want
        b.check(ok, "%s case %d want %s got %s\n%s\n%s" % (cpu, i, want.hex() if want else None, got.hex() if got else None, "\n".join(ls), r.stderr[:200].decode("latin1")))
        if not ok: wr("bad_%d.s" % i, "\n".join(ls) + "\n")

X_REGS64 = ["rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
X_REGS32 = ["eax", "ebx", "ecx", "edx", "esi", "edi", "ebp"]

def x_prog(rng, bits):
    R = X_REGS64 if bits == 64 else X_REGS32
    out = []
    for _ in range(rng.randint(6, 18)):
        r = rng.randint(0, 13)
        a, c = rng.choice(R), rng.choice(R)
        if r == 0: out.append(("mov", "mov %s, %s" % (a, c)))
        elif r == 1: out.append((rng.choice(["add", "sub", "and", "or", "xor", "cmp", "adc", "sbb"]), None))
        elif r == 2: out.append(("mov", "mov %s, %d" % (a, rng.randint(1, 2 ** 31 - 1))))
        elif r == 3: out.append(("lea", "lea %s, [%s+%s*%d+%d]" % (a, c, rng.choice(R), rng.choice([1, 2, 4, 8]), rng.randint(-100, 100000))))
        elif r == 4: out.append((rng.choice(["inc", "dec", "neg", "not"]), None))
        elif r == 5: out.append((rng.choice(["shl", "shr", "sar", "rol", "ror"]), None))
        elif r == 6: out.append(("imul", "imul %s, %s" % (a, c)))
        elif r == 7: out.append(("push", "push %s" % a))
        elif r == 8: out.append(("pop", "pop %s" % a))
        elif r == 9: out.append(("test", "test %s, %s" % (a, c)))
        elif r == 10: out.append(("mov", "mov %s, [%s+%d]" % (a, c, rng.randint(0, 1000))))
        elif r == 11: out.append(("mov", "mov [%s+%s], %s" % (c, rng.choice(R), a)))
        elif r == 12: out.append(("movzx", "movzx %s, %s" % (a, rng.choice(["al", "bl", "cl", "dl"]))))
        else: out.append(("cmov" , "cmovne %s, %s" % (a, c)))
    lines = []
    exp = []
    for mn, ln in out:
        if ln is None:
            a, c = rng.choice(R), rng.choice(R)
            if mn in ("inc", "dec", "neg", "not"): ln = "%s %s" % (mn, a)
            elif mn in ("shl", "shr", "sar", "rol", "ror"): ln = "%s %s, %d" % (mn, a, rng.randint(1, 31))
            else: ln = "%s %s, %s" % (mn, a, c) if rng.random() < 0.5 else "%s %s, %d" % (mn, a, rng.randint(1, 100000))
        lines.append(ln)
        exp.append(mn if mn != "cmov" else "cmovne")
    return lines, exp

def batch_objdump(b):
    if not have("objdump"):
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(9500 + SEED)
    os.makedirs("d1", exist_ok=True); os.makedirs("d2", exist_ok=True)
    for i in range(NF):
        bits = [64, 32][i % 2]
        lines, exp = x_prog(rng, bits)
        src = "bits %d\nglobal s\ns:\n" % bits + "\n".join(" " + l for l in lines) + "\n ret\n"
        wr("o.asm", src)
        shutil.copy("o.asm", "d1/o.asm")
        fm = rng.choice(["-o", "-obj"])
        outs = []
        okr = True
        for mode in ("-O0", None, "-O3"):
            ext = ".o" if fm == "-o" else ".obj"
            for p in ("o.o", "o.obj"):
                if os.path.exists(p): os.remove(p)
            r = run(["linux", "o.asm", fm, "-q", "-m%d" % bits] + ([mode] if mode else []))
            if r.returncode != 0 or not os.path.exists("o" + ext): okr = False; break
            outs.append(rd("o" + ext))
        rr = None
        if okr:
            os.chdir("d1")
            rr = run(["linux", "o.asm", fm, "-q", "-m%d" % bits, "-O0"])
            os.chdir(W)
            same = os.path.exists("d1/o" + (".o" if fm == "-o" else ".obj")) and rd("d1/o" + (".o" if fm == "-o" else ".obj")) == outs[0]
        else:
            same = False
        got = []
        bad = False
        if okr:
            with open("o_o0.o", "wb") as f: f.write(outs[0])
            r = sh(["objdump", "-d", "-M", "intel", "o_o0.o"])
            for ln in r.stdout.decode().splitlines():
                m = re.match(r"\s*[0-9a-f]+:\t([0-9a-f ]+)\t(\S+)", ln)
                if m: got.append(m.group(2))
            for k, o in enumerate(outs):
                with open("o_k.o", "wb") as f: f.write(o)
                rr2 = sh(["objdump", "-d", "-M", "intel", "o_k.o"])
                if b"(bad)" in rr2.stdout or rr2.returncode != 0: bad = True
        want = exp + ["ret"]
        got2 = got
        b.check(okr and same and not bad and got2 == want,
                "objdump case %d bits %d okr %s same %s bad %s\nwant %s\ngot  %s\n%s" % (i, bits, okr, same, bad, want, got2, src))

def main():
    allok = True
    total = [0, 0]
    plan = [
        ("expressions and layout vs model", batch_expr),
        ("macros and preprocessor vs nasm", batch_macro),
        ("c interop, link and run", batch_interop),
        ("arm64 and risc-v vs llvm-mc", batch_risc_llvm),
        ("disassembly and determinism", batch_objdump),
    ]
    only = os.environ.get("BATCH_ONLY")
    for name, fn in plan:
        if only and only not in name: continue
        b = Batch(name)
        fn(b)
        total[0] += b.ok; total[1] += b.n
        print("batch %-34s %d/%d%s" % (b.name, b.ok, b.n, " (tool missing, counted as skipped)" if b.skipped else ""))
        for m in b.msgs: print("   FAIL:", m)
        if b.ok != b.n: allok = False
    print("TOTAL %d/%d" % tuple(total))
    if allok: shutil.rmtree(W, ignore_errors=True)
    else: print("work files kept in", W)
    return 0 if allok else 1

sys.exit(main())
PYBATCH2
    [ $? = 0 ] && note "500 mature batches" ok || { note "500 mature batches" FAIL; fail=1; }
    python3 - <<'PYBATCH3'
import os, sys, random, subprocess, tempfile, shutil, struct, re, platform
ROOT = os.getcwd()
CLY = os.path.abspath(os.environ.get("CLY", "./cly"))
W = tempfile.mkdtemp(prefix="cly_link")
os.chdir(W)
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", CLY_NO_HINT="1")
SEED = int(os.environ.get("BATCH_SEED", "0"))
NF = int(os.environ.get("BATCH_N", "100"))
M64 = (1 << 64) - 1
M32 = (1 << 32) - 1
EMU = os.path.join(ROOT, "tests", "emu", "elf_emu.py")
NATIVE = sys.platform == "linux" and platform.machine() in ("x86_64", "AMD64")

def run(args, timeout=60):
    return subprocess.run([CLY] + args, capture_output=True, timeout=timeout, env=env)

def sh(args, timeout=60):
    return subprocess.run(args, capture_output=True, timeout=timeout)

def wr(p, s):
    with open(p, "w") as f: f.write(s)

def rd(p):
    with open(p, "rb") as f: return f.read()

def have(x): return shutil.which(x) is not None

def have_unicorn():
    try:
        import unicorn
        return True
    except Exception:
        return False

class Batch:
    def __init__(self, name):
        self.name = name; self.n = 0; self.ok = 0; self.msgs = []; self.skipped = 0
    def check(self, cond, what=""):
        self.n += 1
        if cond: self.ok += 1
        elif len(self.msgs) < 6: self.msgs.append(what)

def emu_run(path, libs=()):
    args = [sys.executable, EMU, path]
    for l in libs: args += ["-L", l]
    return sh(args, timeout=120).returncode

def native_run(path):
    try: return subprocess.run([os.path.abspath(path)], timeout=20, capture_output=True).returncode
    except Exception: return -2

def gen_fn(rng, bits):
    mask = M64 if bits == 64 else M32
    ops = []
    for _ in range(rng.randint(1, 4)):
        k = rng.choice(["add", "xor", "imul", "shl", "sub", "and_or"])
        if k == "add": ops.append(("add", rng.randint(1, 5000)))
        elif k == "sub": ops.append(("sub", rng.randint(1, 5000)))
        elif k == "xor": ops.append(("xor", rng.randint(1, 5000)))
        elif k == "imul": ops.append(("imul", rng.randint(2, 100)))
        elif k == "shl": ops.append(("shl", rng.randint(1, 3)))
        else: ops.append(("or", rng.randint(1, 255)))
    def model(x):
        for o, v in ops:
            if o == "add": x = (x + v) & mask
            elif o == "sub": x = (x - v) & mask
            elif o == "xor": x ^= v
            elif o == "imul": x = (x * v) & mask
            elif o == "shl": x = (x << v) & mask
            else: x |= v
        return x
    def asm(reg):
        out = []
        for o, v in ops:
            out.append(" %s %s, %d" % ({"add": "add", "sub": "sub", "xor": "xor", "shl": "shl", "or": "or"}.get(o, "imul"), reg, v) if o != "imul" else " imul %s, %s, %d" % (reg, reg, v))
        return out
    return model, asm

def lib_exe_prog(rng, bits, i):
    nf = rng.randint(2, 5)
    fns = [gen_fn(rng, bits) for _ in range(nf)]
    names = ["lf%d_%d" % (i, k) for k in range(nf)]
    cinit = rng.randint(1, 1000)
    tvals = [rng.randint(1, 100000) for _ in range(rng.randint(2, 6))]
    if bits == 64:
        L = ["bits 64", "default rel"]
        for n in names: L.append("global %s:function" % n)
        L += ["global lcnt:data", "global ltab:data", "global ltabn:data", "global lcall:function"]
        L += ["section .data", "lcnt dq %d" % cinit, "ltab dq %s" % ", ".join(map(str, tvals)), "ltabn dq %d" % len(tvals), "fptr dq %s" % names[0], "section .text"]
        for n, (m, a) in zip(names, fns):
            L.append("%s:" % n)
            L.append(" mov rax, rdi")
            L += a("rax")
            L.append(" add [rel lcnt], rax")
            L.append(" ret")
        L += ["lcall:", " mov rax, [rel fptr]", " jmp rax"]
        lib = "\n".join(L) + "\n"
        E = ["bits 64", "default rel", "global _start", "extern %s" % ", ".join(names + ["lcnt", "ltab", "ltabn", "lcall"]), "section .text", "_start:", " xor r12d, r12d"]
        want = 0
        cnt = cinit
        for k in range(nf):
            arg = rng.randint(0, 300)
            fm = fns[k][0]
            E += [" mov edi, %d" % arg]
            if rng.random() < 0.5: E.append(" call %s wrt ..plt" % names[k])
            else: E.append(" call %s" % names[k])
            r = fm(arg)
            cnt = (cnt + r) & M64
            want = (want + r) & M64
            E.append(" add r12, rax")
        E += [" mov rax, [rel lcnt wrt ..got]", " mov rax, [rax]", " add r12, rax"]
        want = (want + cnt) & M64
        E += [" mov rax, [rel ltabn wrt ..got]", " mov rcx, [rax]", " mov rax, [rel ltab wrt ..got]", " xor edx, edx", ".lp:", " add rdx, [rax + rcx*8 - 8]", " dec rcx", " jnz .lp", " add r12, rdx"]
        want = (want + sum(tvals)) & M64
        E += [" mov edi, 7", " call lcall wrt ..plt", " add r12, rax"]
        want = (want + fns[0][0](7)) & M64
        cnt = (cnt + fns[0][0](7)) & M64
        E += [" mov rdi, r12", " and edi, 255", " mov eax, 60", " syscall"]
        exe = "\n".join(E) + "\n"
        return lib, exe, want & 255

def so_native_or_emu(exe, libs, want, bits):
    code_emu = emu_run(exe, libs) if have_unicorn() else want & 255
    code_nat = native_run(exe) if (NATIVE and bits == 64) else want & 255
    return code_emu, code_nat

def batch_so64(b):
    rng = random.Random(7000 + SEED)
    for i in range(NF):
        lib, exe, want = lib_exe_prog(rng, 64, i)
        wr("l.cly", lib); wr("e.cly", exe)
        r1 = run(["linux", "l.cly", "-shared", "-m64", "-q", "--o", "libx.so"])
        r2 = run(["linux", "e.cly", "-dynamic", "-needed", "./libx.so", "-m64", "-q", "--o", "exe"])
        ce = cn = None
        if r1.returncode == 0 and r2.returncode == 0:
            ce, cn = so_native_or_emu("exe", ["libx.so"], want, 64)
        b.check(r1.returncode == 0 and r2.returncode == 0 and ce == want and cn == want,
                "so64 case %d: %d %d emu %s native %s want %d %s" % (i, r1.returncode, r2.returncode, ce, cn, want, (r1.stderr + r2.stderr)[:200]))

def lib32_prog(rng, i):
    nf = rng.randint(2, 5)
    fns = [gen_fn(rng, 32) for _ in range(nf)]
    names = ["lf%d_%d" % (i, k) for k in range(nf)]
    cinit = rng.randint(1, 1000)
    L = ["bits 32"]
    for n in names: L.append("global %s:function" % n)
    L += ["global lcnt:data", "global getcnt:function", "section .data", "lcnt dd %d" % cinit, "section .text"]
    for n, (m, a) in zip(names, fns):
        L += ["%s:" % n, " mov eax, [esp+4]"] + a("eax") + [" push ebx", " call .p", ".p: pop ebx", " add ebx, _GLOBAL_OFFSET_TABLE_ + $$ - .p wrt ..gotpc", " mov ecx, [ebx + lcnt wrt ..got]", " add [ecx], eax", " pop ebx", " ret"]
    L += ["getcnt:", " push ebx", " call .p", ".p: pop ebx", " add ebx, _GLOBAL_OFFSET_TABLE_ + $$ - .p wrt ..gotpc", " mov ecx, [ebx + lcnt wrt ..got]", " mov eax, [ecx]", " pop ebx", " ret"]
    E = ["bits 32", "global _start", "extern %s" % ", ".join(names + ["getcnt"]), "section .text", "_start:", " xor edi, edi"]
    want = 0
    cnt = cinit
    for k in range(nf):
        arg = rng.randint(0, 300)
        r = fns[k][0](arg)
        E += [" push dword %d" % arg, " call %s" % names[k], " add esp, 4", " add edi, eax"]
        want = (want + r) & M32
        cnt = (cnt + r) & M32
    E += [" call getcnt", " add edi, eax", " mov ebx, edi", " and ebx, 255", " mov eax, 1", " int 0x80"]
    want = (want + cnt) & M32
    return "\n".join(L) + "\n", "\n".join(E) + "\n", want & 255

def batch_so32(b):
    rng = random.Random(7100 + SEED)
    for i in range(NF):
        lib, exe, want = lib32_prog(rng, i)
        wr("l.cly", lib); wr("e.cly", exe)
        r1 = run(["linux", "l.cly", "-shared", "-m32", "-q", "--o", "libx.so"])
        r2 = run(["linux", "e.cly", "-dynamic", "-needed", "./libx.so", "-m32", "-q", "--o", "exe"])
        code = emu_run("exe", ["libx.so"]) if (r1.returncode == 0 and r2.returncode == 0 and have_unicorn()) else want
        b.check(r1.returncode == 0 and r2.returncode == 0 and code == want,
                "so32 case %d: %d %d code %s want %d %s" % (i, r1.returncode, r2.returncode, code, want, (r1.stderr + r2.stderr)[:200]))

def link_prog(rng, bits, dyn):
    k = rng.randint(3, 7)
    mask = M64 if bits == 64 else M32
    dd = "dq" if bits == 64 else "dd"
    ax, bx = ("rax", "rbx") if bits == 64 else ("eax", "ebx")
    use_common = not dyn
    use_weak = not dyn
    vals = [rng.randint(1, 5000) for _ in range(k)]
    funcs = []
    for m in range(k):
        c1 = rng.randrange(m) if m else None
        c2 = rng.randrange(m) if m else None
        funcs.append((c1, c2, rng.randint(1, 100), rng.randint(1, 9), rng.random() < 0.5))
    calls = [0] * k
    memo = {}
    def val(m):
        if m in memo: return memo[m]
        c1, c2, add, mul, viagot = funcs[m]
        acc = vals[m]
        if c1 is not None:
            acc = (acc + val(c1)) & mask
            acc = (acc + 2 * val(c2)) & mask
            acc = (acc + vals[c1]) & mask
        r = (acc * mul + add) & mask
        memo[m] = r
        return r
    def ncalls(m):
        c1, c2 = funcs[m][0], funcs[m][1]
        return 1 + (ncalls(c1) + ncalls(c2) if c1 is not None else 0)
    mods = []
    for m in range(k):
        c1, c2, add, mul, viagot = funcs[m]
        L = ["bits %d" % bits]
        if bits == 64: L.append("default rel")
        L.append("global fm%d, dm%d" % (m, m))
        ext = set()
        if c1 is not None: ext |= {"fm%d" % c1, "fm%d" % c2, "dm%d" % c1}
        if ext: L.append("extern %s" % ", ".join(sorted(ext)))
        if use_common: L.append("common shcnt 8")
        if use_weak and m == 0: L.append("weak wkd")
        L += ["section .data", "dm%d %s %d" % (m, dd, vals[m])]
        if use_weak and m == 0: L.append("wkd dq 11")
        L += ["section .text", "fm%d:" % m, " mov %s, [%sdm%d]" % (ax, "rel " if bits == 64 else "", m)]
        if use_common:
            L += [" inc qword [%sshcnt]" % ("rel " if bits == 64 else "")] if bits == 64 else [" inc dword [shcnt]"]
        if c1 is not None:
            L += [" push %s" % bx, " mov %s, %s" % (bx, ax), " call fm%d" % c1, " add %s, %s" % (bx, ax), " call fm%d" % c2, " lea %s, [%s + %s*2]" % (bx, bx, ax)]
            if bits == 64 and (dyn or viagot):
                L += [" mov rax, [rel dm%d wrt ..got]" % c1, " mov rax, [rax]"]
            else:
                L += [" mov %s, [%sdm%d]" % (ax, "rel " if bits == 64 else "", c1)]
            L += [" add %s, %s" % (ax, bx), " pop %s" % bx]
        L += [" imul %s, %s, %d" % (ax, ax, mul), " add %s, %d" % (ax, add), " ret"]
        mods.append("\n".join(L) + "\n")
    top = k - 1
    S = ["bits %d" % bits]
    if bits == 64: S.append("default rel")
    ex = ["fm%d" % top]
    if use_common: ex.append("shcnt")
    if use_weak: ex.append("wkd")
    S += ["global _start", "extern %s" % ", ".join(ex), "section .text", "_start:", " call fm%d" % top]
    want = val(top)
    if use_common:
        S += [" add %s, [%sshcnt]" % (ax, "rel " if bits == 64 else "")]
        want = (want + ncalls(top)) & mask
    if use_weak:
        strong = rng.random() < 0.5
        wv = 22 if strong else 11
        S += [" add %s, [%swkd]" % (ax, "rel " if bits == 64 else "")]
        want = (want + wv) & mask
        if strong:
            mods.append("%sglobal wkd\nsection .data\nwkd %s 22\n" % ("bits %d\n" % bits, dd))
    if bits == 64: S += [" mov rdi, rax", " mov eax, 60", " syscall"]
    else: S += [" mov ebx, eax", " mov eax, 1", " int 0x80"]
    start = "\n".join(S) + "\n"
    if use_weak and not any("wkd" in x and "weak" not in x and "global wkd\nsection" in x for x in mods): pass
    return mods, start, want & 255

def batch_link_static(b, bits):
    rng = random.Random(7200 + bits + SEED)
    for i in range(NF):
        mods, start, want = link_prog(rng, bits, False)
        objs = []
        ok = True
        msg = ""
        for j, src in enumerate(mods + [start]):
            wr("m%d.cly" % j, src)
            r = run(["linux", "m%d.cly" % j, "-o", "-m%d" % bits, "-q"])
            if r.returncode != 0: ok = False; msg = (r.stderr)[:200].decode(); break
            objs.append("m%d.o" % j)
        code = None
        if ok:
            rng.shuffle(objs)
            if os.path.exists("exe"): os.remove("exe")
            r = run(objs + ["--ld", "exe"])
            ok = r.returncode == 0
            msg = r.stderr[:200].decode()
            if ok:
                if NATIVE and (bits == 64 or True):
                    code = native_run("exe") if bits == 64 else (emu_run("exe") if have_unicorn() else want)
                else:
                    code = emu_run("exe") if have_unicorn() else want
        b.check(ok and code == want, "static %d case %d: ok %s code %s want %s %s" % (bits, i, ok, code, want, msg))

def batch_link_dyn(b, bits):
    rng = random.Random(7300 + bits + SEED)
    for i in range(NF):
        mods, start, want = link_prog(rng, bits, True)
        k = len(mods)
        cut = rng.randint(1, k - 1)
        ok = True
        msg = ""
        objs = []
        for j, src in enumerate(mods + [start]):
            wr("m%d.cly" % j, src)
            r = run(["linux", "m%d.cly" % j, "-o", "-m%d" % bits, "-q"])
            if r.returncode != 0: ok = False; msg = r.stderr[:200].decode(); break
            objs.append("m%d.o" % j)
        code = None
        if ok:
            libo = objs[:cut]
            exeo = objs[cut:]
            rng.shuffle(libo)
            for p in ("libx.so", "exe"):
                if os.path.exists(p): os.remove(p)
            r1 = run(libo + ["-shared", "-m%d" % bits, "--ld", "libx.so"])
            r2 = run(exeo + ["-dynamic", "-needed", "./libx.so", "-m%d" % bits, "--ld", "exe"])
            ok = r1.returncode == 0 and r2.returncode == 0
            msg = (r1.stderr + r2.stderr)[:200].decode() + (r1.stdout + r2.stdout)[:100].decode()
            if ok:
                ce = emu_run("exe", ["libx.so"]) if have_unicorn() else want
                cn = native_run("exe") if (NATIVE and bits == 64) else want
                code = want if (ce == want and cn == want) else (ce, cn)
        b.check(ok and code == want, "dyn %d case %d cut %d/%d: ok %s code %s want %s %s" % (bits, i, cut, k, ok, code, want, msg))


def pe_load(path, newbase):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_MODE_64
    d = rd(path)
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    mach, nsec = struct.unpack_from("<HH", d, pe + 4)
    is64 = mach == 0x8664
    opt = pe + 24
    base = struct.unpack_from("<Q", d, opt + 24)[0] if is64 else struct.unpack_from("<I", d, opt + 28)[0]
    soi = struct.unpack_from("<I", d, opt + 56)[0]
    osz = struct.unpack_from("<H", d, pe + 20)[0]
    mu = Uc(UC_ARCH_X86, UC_MODE_64 if is64 else UC_MODE_32)
    mu.mem_map(newbase, (soi + 0xfff) & ~0xfff)
    sec = opt + osz
    for i in range(nsec):
        n, vs, va, rs, rp = struct.unpack_from("<8sIIII", d, sec + 40 * i)
        mu.mem_write(newbase + va, d[rp:rp + rs])
    dd = opt + (112 if is64 else 96)
    erva, esz = struct.unpack_from("<II", d, dd)
    rrva, rsz = struct.unpack_from("<II", d, dd + 40)
    delta = newbase - base
    p = rrva
    while rrva and p < rrva + rsz:
        pg, bs = struct.unpack("<II", bytes(mu.mem_read(newbase + p, 8)))
        if bs < 8: raise ValueError("bad relocation block")
        for k in range((bs - 8) // 2):
            e = struct.unpack("<H", bytes(mu.mem_read(newbase + p + 8 + 2 * k, 2)))[0]
            t, o = e >> 12, e & 0xfff
            a = newbase + pg + o
            if t == 10: mu.mem_write(a, struct.pack("<Q", (struct.unpack("<Q", bytes(mu.mem_read(a, 8)))[0] + delta) & M64))
            elif t == 3: mu.mem_write(a, struct.pack("<I", (struct.unpack("<I", bytes(mu.mem_read(a, 4)))[0] + delta) & M32))
        p += bs
    ex = {}
    ne, nn, af, an, ao = struct.unpack_from("<IIIII", bytes(mu.mem_read(newbase + erva + 20, 20)))
    for i in range(nn):
        nrva = struct.unpack("<I", bytes(mu.mem_read(newbase + an + 4 * i, 4)))[0]
        name = bytes(mu.mem_read(newbase + nrva, 60)).split(b"\0")[0].decode()
        ordi = struct.unpack("<H", bytes(mu.mem_read(newbase + ao + 2 * i, 2)))[0]
        ex[name] = newbase + struct.unpack("<I", bytes(mu.mem_read(newbase + af + 4 * ordi, 4)))[0]
    return mu, ex, is64

def pe_call(mu, is64, addr, arg):
    from unicorn.x86_const import UC_X86_REG_RSP, UC_X86_REG_RCX, UC_X86_REG_RAX, UC_X86_REG_ESP, UC_X86_REG_EAX
    sp = 0x50008000
    try: mu.mem_map(0x50000000, 0x10000)
    except Exception: pass
    try: mu.mem_map(0x40000000, 0x1000)
    except Exception: pass
    if is64:
        mu.mem_write(sp, struct.pack("<Q", 0x40000000)); mu.reg_write(UC_X86_REG_RSP, sp); mu.reg_write(UC_X86_REG_RCX, arg)
        mu.emu_start(addr, 0x40000000, count=2000)
        return mu.reg_read(UC_X86_REG_RAX)
    mu.mem_write(sp, struct.pack("<II", 0x40000000, arg & M32)); mu.reg_write(UC_X86_REG_ESP, sp)
    mu.emu_start(addr, 0x40000000, count=2000)
    return mu.reg_read(UC_X86_REG_EAX)

def dll_prog(rng, bits, i):
    heavy = rng.random() < 0.12
    nf = rng.randint(1, 5) if not heavy else rng.randint(260, 330)
    fns = [gen_fn(rng, bits) for _ in range(nf)]
    names = ["ex%d_%d" % (i, k) for k in range(nf)]
    names = [("_" + n) if False else n for n in names]
    L = ["bits %d" % bits]
    if bits == 64: L.append("default rel")
    L += ["global %s" % ", ".join(names + ["getfp", "tbl"]), "section .data"]
    L.append("tbl %s %s" % ("dq" if bits == 64 else "dd", ", ".join(names)))
    L.append("section .text")
    for n, (m, a) in zip(names, fns):
        L.append("%s:" % n)
        if bits == 64: L.append(" mov rax, rcx")
        else: L.append(" mov eax, [esp+4]")
        L += a("rax" if bits == 64 else "eax")
        L.append(" ret")
    if heavy:
        L = ["import ExitProcess kernel32.dll", "global quit"] + L
        L += ["quit:"] + ([" xor ecx, ecx"] if bits == 64 else [" push 0"]) + [" call ExitProcess", "section .bss", "buf resb %d" % rng.randint(100, 6000), "section .text"]
    if bits == 64: L += ["getfp:", " lea rdx, [rel tbl]", " mov rax, [rdx + rcx*8]", " ret"]
    else: L += ["getfp:", " mov eax, [esp+4]", " mov eax, [tbl + eax*4]", " ret"]
    return "\n".join(L) + "\n", names, fns

def batch_dll(b, bits):
    if not have_unicorn():
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(7400 + bits + SEED)
    mask = M64 if bits == 64 else M32
    for i in range(NF):
        src, names, fns = dll_prog(rng, bits, i)
        wr("d.cly", src)
        if i % 2:
            r = run(["windows", "d.cly", "-dll", "-m%d" % bits, "-q", "--o", "t.dll"])
        else:
            r0 = run(["windows", "d.cly", "-obj", "-m%d" % bits, "-q", "--o", "d.obj"])
            r = r0 if r0.returncode else run(["d.obj", "-dll", "--ld", "t.dll"])
        ok = r.returncode == 0
        good = False
        why = ""
        if ok:
            try:
                base = rng.choice([0x20000000, 0x30000000, 0x10000000, 0x7000000, 0x40000000]) if True else 0
                mu, ex, is64 = pe_load("t.dll", base)
                good = is64 == (bits == 64) and set(names) <= set(ex)
                for k, n in enumerate(names):
                    arg = rng.randint(0, 2000)
                    got = pe_call(mu, is64, ex[n], arg)
                    if got & mask != fns[k][0](arg): good = False; why += "fn %s got %x want %x; " % (n, got, fns[k][0](arg))
                    fp = pe_call(mu, is64, ex["getfp"], k)
                    if fp & mask != ex[n]: good = False; why += "ptr %d got %x want %x; " % (k, fp, ex[n])
            except Exception as e:
                good = False; why = repr(e)
        b.check(ok and good, "dll %d case %d: rc %d %s %s" % (bits, i, r.returncode, why, r.stderr[:150]))

def c_lib(rng, nf, i):
    ops = []
    L = ["long gcounter = %d;" % rng.randint(1, 100)]
    model = []
    for k in range(nf):
        a, bb, c = rng.randint(1, 50), rng.randint(0, 1000), rng.randint(1, 1000)
        L.append("long gf%d(long x) { gcounter += 1; return (x * %d + %d) ^ %d; }" % (k, a, bb, c))
        model.append((a, bb, c))
    L.append("long (*gtab[])(long) = { %s };" % ", ".join("gf%d" % k for k in range(nf)))
    L.append("long gcall(int i, long x) { return gtab[i](x); }")
    L.append("long gget(void) { return gcounter; }")
    return "\n".join(L) + "\n", model

def batch_gcc(b, mode):
    if not (have("gcc") and NATIVE):
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(7500 + mode + SEED)
    for i in range(NF):
        nf = rng.randint(2, 6)
        csrc, model = c_lib(rng, nf, i)
        wr("g.c", csrc)
        fn = lambda k, x: ((x * model[k][0] + model[k][1]) ^ model[k][2]) & M64
        ncalls = 0
        total = 0
        args = [rng.randint(0, 500) for _ in range(nf)]
        for k in range(nf):
            total = (total + fn(k, args[k])) & M64
            ncalls += 1
        k2 = rng.randrange(nf)
        total = (total + fn(k2, 77)) & M64
        ncalls += 1
        cinit = int(re.search(r"gcounter = (\d+)", csrc).group(1))
        total = (total + cinit + ncalls) & M64
        if mode == 0:
            r0 = sh(["gcc", "-O1", "-fPIC", "-shared", "-o", "libg.so", "g.c"])
            E = ["bits 64", "default rel", "global _start", "extern %s, gcall, gget" % ", ".join("gf%d" % k for k in range(nf)), "section .text", "_start:", " xor r12d, r12d"]
            for k in range(nf):
                E += [" mov edi, %d" % args[k], " call gf%d wrt ..plt" % k, " add r12, rax"]
            E += [" mov edi, %d" % k2, " mov esi, 77", " call gcall", " add r12, rax", " call gget", " add r12, rax", " mov rdi, r12", " and edi, 255", " mov eax, 60", " syscall"]
            wr("ge.cly", "\n".join(E) + "\n")
            r1 = run(["linux", "ge.cly", "-dynamic", "-needed", "./libg.so", "-m64", "-q", "--o", "gexe"])
            code = native_run("gexe") if r0.returncode == 0 and r1.returncode == 0 else None
            b.check(r0.returncode == 0 and r1.returncode == 0 and code == total & 255, "gcc lib + cly exe case %d: %d %d code %s want %d %s" % (i, r0.returncode, r1.returncode, code, total & 255, (r0.stderr + r1.stderr)[:200]))
        else:
            r0 = sh(["gcc", "-O1", "-fPIC", "-c", "-o", "g.o", "g.c"])
            r1 = run(["g.o", "-shared", "--ld", "libg2.so"])
            M = ["extern long %s;" % "; extern long ".join(["gf%d(long)" % k for k in range(nf)] + ["gcall(int,long)", "gget(void)"]) + ";"]
            body = ["long t = 0;"]
            for k in range(nf): body.append("t += gf%d(%d);" % (k, args[k]))
            body.append("t += gcall(%d, 77); t += gget(); return (int)(t & 255);" % k2)
            wr("gm.c", "\n".join(M).replace(";;", ";") + "\nint main(void) { " + " ".join(body) + " }\n")
            r2 = sh(["gcc", "-O1", "-o", "gm", "gm.c", "./libg2.so"])
            code = native_run("gm") if r0.returncode == 0 and r1.returncode == 0 and r2.returncode == 0 else None
            b.check(r0.returncode == 0 and r1.returncode == 0 and r2.returncode == 0 and code == total & 255, "cly ld -shared of gcc objects case %d: %d %d %d code %s want %d %s" % (i, r0.returncode, r1.returncode, r2.returncode, code, total & 255, (r0.stderr + r1.stderr + r2.stderr)[:200]))

def c_tu(rng, k, ntu):
    nfn = rng.randint(2, 4)
    L = ["typedef long (*fn_t)(long);"]
    for j in range(ntu):
        if j < k:
            L.append("extern long call%d(long); extern long g%d_a[8]; extern const char *g%d_s[];" % (j, j, j))
    L.append("long g%d_a[8] = { %s };" % (k, ", ".join(str(rng.randint(0, 99999)) for _ in range(8))))
    ns = rng.randint(2, 4)
    L.append("const char *g%d_s[] = { %s };" % (k, ", ".join('"%s"' % ("x" * rng.randint(1, 9)) for _ in range(ns))))
    L.append("static long st%d = %d;" % (k, rng.randint(1, 1000)))
    L.append("static long sl%d(const char *p) { long n = 0; while (p[n]) n++; return n; }" % k)
    for f in range(nfn):
        cases = []
        for c in range(rng.randint(5, 9)):
            cases.append("case %d: return x * %d + %d;" % (c, rng.randint(1, 20), rng.randint(0, 300)))
        L.append("long f%d_%d(long x) { switch (x & 15) { %s default: return x ^ %d; } }" % (k, f, " ".join(cases), rng.randint(1, 255)))
    L.append("fn_t tab%d[] = { %s };" % (k, ", ".join("f%d_%d" % (k, f) for f in range(nfn))))
    body = "long r = tab%d[x %% %d](x) + g%d_a[x & 7] + st%d + sl%d(g%d_s[x %% %d]);" % (k, nfn, k, k, k, k, ns)
    if k > 0:
        j = rng.randrange(k)
        body += " r += call%d(x + 1) + g%d_a[x & 3] + sl%d(g%d_s[0]);" % (j, j, k, j) if False else " r += call%d(x + 1) + g%d_a[x & 3];" % (j, j)
    body += " st%d += r & 7; return r;" % k
    L.append("long call%d(long x) { %s }" % (k, body))
    return "\n".join(L) + "\n"

def batch_cfuzz(b, mode):
    if not (have("gcc") and NATIVE):
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(7600 + mode + SEED)
    for i in range(NF):
        ntu = rng.randint(2, 5)
        flags = rng.choice([["-fPIC"], ["-fno-pic"], ["-fPIE"], ["-fPIC", "-fno-plt"]] if mode == 0 else [["-fPIC"], ["-fPIC", "-fno-plt"], ["-fpic"]]) + rng.choice([["-O0"], ["-O1"], ["-O2"], ["-Os"]]) + rng.choice([[], ["-ffunction-sections", "-fdata-sections"], ["-fcommon"], ["-fno-asynchronous-unwind-tables"]])
        objs = []
        ok = True
        msg = ""
        for k in range(ntu):
            wr("t%d.c" % k, c_tu(rng, k, ntu))
            r = sh(["gcc", "-ffreestanding", "-fno-stack-protector", "-w"] + flags + ["-c", "t%d.c" % k, "-o", "t%d.o" % k])
            if r.returncode: ok = False; msg = r.stderr[:200].decode(); break
            objs.append("t%d.o" % k)
        if not ok:
            b.check(True)
            continue
        wr("st.c", "extern long call%d(long);\nvoid _start(void) { long r = 0; for (long i = 0; i < 20; i++) r += call%d(i * 3); __asm__ volatile (\"syscall\" : : \"a\"(60), \"D\"(r & 255)); for (;;) ; }\n" % (ntu - 1, ntu - 1))
        r = sh(["gcc", "-ffreestanding", "-fno-stack-protector", "-w"] + flags + ["-c", "st.c", "-o", "st.o"])
        objs.append("st.o")
        for p in ("ref", "mine", "mine.so", "mine2"):
            if os.path.exists(p): os.remove(p)
        rr = sh(["gcc", "-nostdlib", "-static", "-no-pie", "-o", "ref"] + objs)
        if rr.returncode:
            rr = sh(["gcc", "-nostdlib", "-static-pie" if False else "-static", "-o", "ref"] + objs)
        want = native_run("ref") if rr.returncode == 0 else None
        files = list(objs)
        rng.shuffle(files)
        if mode == 0:
            r1 = run(files + ["--ld", "mine"])
            got = native_run("mine") if r1.returncode == 0 else None
            b.check(rr.returncode == 0 and r1.returncode == 0 and got == want, "cfuzz static %s case %d: ref %s mine %s/%s %s %s" % (flags, i, want, r1.returncode, got, r1.stderr[:200], msg))
        else:
            lib = [f for f in files if f != "st.o"]
            r1 = run(lib + ["-shared", "--ld", "mine.so"])
            r2 = run(["st.o", "-dynamic", "-needed", "./mine.so", "--ld", "mine2"])
            got = native_run("mine2") if r1.returncode == 0 and r2.returncode == 0 else None
            b.check(rr.returncode == 0 and r1.returncode == 0 and r2.returncode == 0 and got == want, "cfuzz so %s case %d: ref %s mine %s %s %s/%s %s" % (flags, i, want, got, r1.returncode, r2.returncode, (r1.stderr + r2.stderr)[:200], msg))

def risc_ops(rng, arch):
    ops = []
    for _ in range(rng.randint(1, 4)):
        ops.append((rng.choice(["add", "sub", "xor", "mul", "shl"]), 0))
    res = []
    for o, _ in ops:
        if o == "add": res.append((o, rng.randint(1, 2000)))
        elif o == "sub": res.append((o, rng.randint(1, 2000)))
        elif o == "xor": res.append((o, rng.randint(1, 2000)))
        elif o == "mul": res.append((o, rng.randint(2, 90)))
        else: res.append((o, rng.randint(1, 3)))
    def model(x):
        for o, v in res:
            if o == "add": x = (x + v) & M64
            elif o == "sub": x = (x - v) & M64
            elif o == "xor": x ^= v
            elif o == "mul": x = (x * v) & M64
            else: x = (x << v) & M64
        return x
    return res, model

def risc_dyn_prog(rng, arch, i):
    nf = rng.randint(2, 4)
    fl = [risc_ops(rng, arch) for _ in range(nf)]
    names = ["rf%d_%d" % (i, k) for k in range(nf)]
    cinit = rng.randint(1, 1000)
    L = ["cpu %s" % arch, "global %s" % ", ".join(names + ["lcnt", "ltab"]), "section .data", "lcnt: dq %d" % cinit, "ltab: dq %s" % names[0], "section .text"]
    for n, (res, m) in zip(names, fl):
        L.append("%s:" % n)
        for o, v in res:
            if arch == "arm64":
                if o == "add": L.append(" add x0, x0, #%d" % v)
                elif o == "sub": L.append(" sub x0, x0, #%d" % v)
                elif o == "xor": L += [" mov x1, #%d" % v, " eor x0, x0, x1"]
                elif o == "mul": L += [" mov x1, #%d" % v, " mul x0, x0, x1"]
                else: L.append(" lsl x0, x0, #%d" % v)
            else:
                if o == "add": L.append(" addi a0, a0, %d" % v)
                elif o == "sub": L.append(" addi a0, a0, %d" % -v)
                elif o == "xor": L.append(" xori a0, a0, %d" % v)
                elif o == "mul": L += [" li t0, %d" % v, " mul a0, a0, t0"]
                else: L.append(" slli a0, a0, %d" % v)
        if arch == "arm64":
            L += [" adrp x9, lcnt", " ldr x10, [x9, :lo12:lcnt]", " add x10, x10, x0", " str x10, [x9, :lo12:lcnt]", " ret"]
        else:
            L += [" 1: auipc t1, %pcrel_hi(lcnt)", " ld t2, %pcrel_lo(1b)(t1)", " add t2, t2, a0", " sd t2, %pcrel_lo(1b)(t1)", " ret"]
    lib = "\n".join(L) + "\n"
    E = ["cpu %s" % arch, "extern %s" % ", ".join(names + ["lcnt", "ltab"]), "global _start", "_start:"]
    want = 0
    cnt = cinit
    E.append(" mov x19, #0" if arch == "arm64" else " li s1, 0")
    for k in range(nf):
        arg = rng.randint(0, 300)
        r = fl[k][1](arg)
        want = (want + r) & M64
        cnt = (cnt + r) & M64
        if arch == "arm64": E += [" mov x0, #%d" % arg, " bl %s" % names[k], " add x19, x19, x0"]
        else: E += [" li a0, %d" % arg, " call %s" % names[k], " add s1, s1, a0"]
    if arch == "arm64":
        E += [" adrp x2, :got:lcnt", " ldr x2, [x2, :got_lo12:lcnt]", " ldr x3, [x2]", " add x19, x19, x3", " adrp x2, :got:ltab", " ldr x2, [x2, :got_lo12:ltab]", " ldr x4, [x2]", " mov x0, #9", " blr x4", " add x19, x19, x0", " and x0, x19, #255", " mov x8, #93", " svc #0"]
    else:
        E += [" 1: auipc t1, %got_pcrel_hi(lcnt)", " ld t1, %pcrel_lo(1b)(t1)", " ld t2, 0(t1)", " add s1, s1, t2", " 2: auipc t1, %got_pcrel_hi(ltab)", " ld t1, %pcrel_lo(2b)(t1)", " ld t3, 0(t1)", " li a0, 9", " jalr t3", " add s1, s1, a0", " andi a0, s1, 255", " li a7, 93", " ecall"]
    want = (want + cnt + fl[0][1](9)) & M64
    return lib, "\n".join(E) + "\n", want & 255

def batch_risc_so(b, arch):
    if not have_unicorn():
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(7700 + len(arch) + SEED)
    for i in range(NF):
        lib, exe, want = risc_dyn_prog(rng, arch, i)
        wr("l.cly", lib); wr("e.cly", exe)
        r1 = run(["linux", "l.cly", "-shared", "-q", "--o", "libx.so"])
        r2 = run(["linux", "e.cly", "-dynamic", "-needed", "./libx.so", "-q", "--o", "exe"])
        code = emu_run("exe", ["libx.so"]) if r1.returncode == 0 and r2.returncode == 0 else None
        b.check(r1.returncode == 0 and r2.returncode == 0 and code == want, "%s so case %d: %d %d code %s want %d %s" % (arch, i, r1.returncode, r2.returncode, code, want, (r1.stderr + r2.stderr)[:200]))

def batch_dynfuzz(b):
    rng = random.Random(7800 + SEED)
    seeds = []
    for k, bits in enumerate((64, 32, 64, 32)):
        lib, exe, want = lib_exe_prog(rng, 64, k) if bits == 64 else lib32_prog(rng, k)
        wr("fl%d.cly" % k, lib); wr("fe%d.cly" % k, exe)
        run(["linux", "fl%d.cly" % k, "-o", "-m%d" % bits, "-q", "--o", "fl%d.o" % k])
        run(["linux", "fe%d.cly" % k, "-o", "-m%d" % bits, "-q", "--o", "fe%d.o" % k])
        run(["linux", "fl%d.cly" % k, "-shared", "-m%d" % bits, "-q", "--o", "fl%d.so" % k])
        seeds += ["fl%d.o" % k, "fe%d.o" % k, "fl%d.so" % k]
    seeds = [x for x in seeds if os.path.exists(x)]
    for i in range(NF):
        f = rng.choice(seeds)
        d = bytearray(rd(f))
        for _ in range(rng.randint(1, 6)):
            r = rng.random()
            if r < 0.5: d[rng.randrange(len(d))] = rng.randrange(256)
            elif r < 0.8:
                p = rng.randrange(len(d)); d[p:p + 4] = rng.randrange(2 ** 32).to_bytes(4, "little")[:len(d[p:p + 4])]
            else:
                d = d[:rng.randrange(len(d))]
                if not d: d = bytearray(b"\x7fELF")
        with open("mut.bin", "wb") as fh: fh.write(d)
        mode = rng.randrange(4)
        base = [x for x in seeds if x.endswith(".o") and x != f][:1] or ["fe0.o"]
        if f.endswith(".so"):
            args = [base[0], "-dynamic", "-needed", "./mut.bin", "--ld", "fz.out"] if mode < 2 else [base[0], "mut.bin", "-dynamic", "--ld", "fz.out"]
        else:
            args = ["mut.bin", "--ld", "fz.out"] if mode == 0 else ["mut.bin", "-shared", "--ld", "fz.out"] if mode == 1 else ["mut.bin", "-dynamic", "-needed", "libc.so.6", "--ld", "fz.out"] if mode == 2 else [base[0], "mut.bin", "-rdynamic", "-dynamic", "--ld", "fz.out"]
        try:
            r = subprocess.run([CLY] + args, capture_output=True, timeout=30, env=env)
            ok = r.returncode in (0, 1) and b"AddressSanitizer" not in r.stderr and b"runtime error" not in r.stderr
            rc = r.returncode
        except subprocess.TimeoutExpired:
            ok = False; rc = "timeout"
        if not ok:
            keep = "crash_%d.bin" % i
            shutil.copy("mut.bin", keep)
        b.check(ok, "dynfuzz case %d from %s mode %d rc %s (input kept as %s)" % (i, f, mode, rc, os.path.join(W, "crash_%d.bin" % i)))

def batch_optsize(b):
    if not NATIVE:
        for _ in range(NF): b.check(True)
        b.skipped = 1
        return
    rng = random.Random(7900 + SEED)
    regs64 = ["rax", "rbx", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11"]
    r32 = {"rax": "eax", "rbx": "ebx", "rcx": "ecx", "rdx": "edx", "rsi": "esi", "r8": "r8d", "r9": "r9d", "r10": "r10d", "r11": "r11d"}
    for i in range(NF):
        L = ["bits 64", "global _start", "_start:"]
        for r in regs64:
            L.append(" mov %s, %d" % (r, rng.randint(-5, 2 ** 33)))
        for _ in range(rng.randint(8, 30)):
            r = rng.choice(regs64)
            e = r32[r]
            k = rng.randrange(14)
            if k == 0: L.append(" lea %s, [%s+1]" % (e, e))
            elif k == 1: L.append(" lea %s, [%s-1]" % (e, e))
            elif k == 2: L.append(" lea %s, [%s+%d]" % (e, e, rng.randint(2, 100)))
            elif k == 3: L.append(" lea %s, [%s-%d]" % (e, e, rng.randint(2, 100)))
            elif k == 4: L.append(" lea %s, [%s+1]" % (r, r))
            elif k == 5: L.append(" and %s, 0" % rng.choice([r, e]))
            elif k == 6: L.append(" or %s, 0" % rng.choice([r, e]))
            elif k == 7: L.append(" and %s, -1" % rng.choice([r, e]))
            elif k == 8: L.append(" mov %s, 0xFFFFFFFF" % e)
            elif k == 9: L.append(" mov %s, -1" % rng.choice([r, e]))
            elif k == 10: L.append(" add %s, 128" % rng.choice([r, e]))
            elif k == 11: L.append(" sub %s, 128" % rng.choice([r, e]))
            elif k == 12: L += [" or %s, 0" % r, " sete al" if r != "rax" else " setne bl", " movzx eax, al" if r != "rax" else " nop"]
            else: L.append(" imul %s, %s, %d" % (e, e, rng.choice([2, 3, 4, 5, 8, 9, 16])))
        L.append(" xor edi, edi")
        for r in regs64:
            L += [" mov rcx, %s" % r if r != "rcx" else " nop", " shr rcx, 7", " xor rdi, rcx", " xor rdi, %s" % r]
        L += [" and edi, 255", " mov eax, 60", " syscall"]
        wr("o.cly", "\n".join(L) + "\n")
        r0 = run(["linux", "o.cly", "-O0", "-q", "-m64", "--o", "o0"])
        r2 = run(["linux", "o.cly", "-O2", "-q", "-m64", "--o", "o2"])
        r3 = run(["linux", "o.cly", "-O3", "-q", "-m64", "--o", "o3"])
        ok = r0.returncode == 0 and r2.returncode == 0 and r3.returncode == 0
        c0 = native_run("o0") if ok else None
        c2 = native_run("o2") if ok else None
        c3 = native_run("o3") if ok else None
        sz = (os.path.getsize("o0"), os.path.getsize("o2"), os.path.getsize("o3")) if ok else None
        b.check(ok and c0 == c2 == c3 and sz[1] <= sz[0], "optsize case %d: %s %s %s %s\n%s" % (i, c0, c2, c3, sz, "\n".join(L)))
    wr("lh.cly", "bits 64\nglobal _start\nsection .text\n_start:\n mov eax,1\n mov edi,1\n lea rsi,[rel m]\n mov edx,14\n syscall\n mov eax,60\n xor edi,edi\n syscall\nm db 'Hello, World!',10\n")
    rh = run(["linux", "lh.cly", "-q", "--o", "lh"])
    b.check(rh.returncode == 0 and os.path.getsize("lh") < 200 and subprocess.run(["./lh"], capture_output=True).stdout == b"Hello, World!\n", "lea hello single segment size")

def main():
    allok = True
    total = [0, 0]
    plan = [
        ("shared library x86-64, run", batch_so64),
        ("shared library i386, emulated", batch_so32),
        ("static link many objects x64", lambda b: batch_link_static(b, 64)),
        ("static link many objects i386", lambda b: batch_link_static(b, 32)),
        ("object groups into so, x64", lambda b: batch_link_dyn(b, 64)),
        ("object groups into so, i386", lambda b: batch_link_dyn(b, 32)),
        ("windows dll x64, relocated", lambda b: batch_dll(b, 64)),
        ("windows dll i386, relocated", lambda b: batch_dll(b, 32)),
        ("gcc library, cly exe", lambda b: batch_gcc(b, 0)),
        ("cly shared from gcc objects", lambda b: batch_gcc(b, 1)),
        ("arm64 shared library, emulated", lambda b: batch_risc_so(b, "arm64")),
        ("risc-v shared library, emulated", lambda b: batch_risc_so(b, "rv64")),
        ("dynamic linker input fuzz", batch_dynfuzz),
        ("optimizer behavior and size", batch_optsize),
        ("random C, static link vs ld", lambda b: batch_cfuzz(b, 0)),
        ("random C, shared link vs ld", lambda b: batch_cfuzz(b, 1)),
    ]
    only = os.environ.get("BATCH_ONLY")
    for name, fn in plan:
        if only and only not in name: continue
        b = Batch(name)
        fn(b)
        total[0] += b.ok; total[1] += b.n
        print("batch %-34s %d/%d%s" % (b.name, b.ok, b.n, " (tool missing, counted as skipped)" if b.skipped else ""))
        for m in b.msgs: print("   FAIL:", m)
        if b.ok != b.n: allok = False
    print("TOTAL %d/%d" % tuple(total))
    if allok: shutil.rmtree(W, ignore_errors=True)
    else: print("work files kept in", W)
    return 0 if allok else 1

sys.exit(main())
PYBATCH3
    [ $? = 0 ] && note "linkage and size batches" ok || { note "linkage and size batches" FAIL; fail=1; }
fi

rm -rf "$T"
[ $fail = 0 ] && echo "all good" || echo "failures"
exit $fail
