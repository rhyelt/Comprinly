# Comprinly 1.3.3

## linux32

`cly linux32 file.cly` builds a 32-bit Linux program. It is the same as `cly linux file.cly -m32`. `linux64` works too and means `-m64`.

## Faster compiles

Comprinly used to build its whole instruction table (about 5,600 forms) before it looked at your file. Now only the hand written forms are built at start. The big generated part is filed by instruction name and an instruction's forms are built the first time your program uses it. A Hello World compile is about twice as fast, and every result is identical, which the tests and a full table dump confirm.

## Smaller programs

A Linux Hello World is now 162 bytes (it was 274). The 32-bit version is 129 bytes.

Normally Comprinly writes code and data in two segments, so the data can be written to, plus a stack header. For a tiny program that is 112 bytes of headers it does not need. The optimizer now checks whether the program can ever write to memory. If it can not, the data sits next to the code and both headers are dropped.

The check only accepts simple programs:

- register and jump instructions, with no memory operands and no stack use
- every `syscall` (`int 0x80` in 32-bit code) right after a constant load of `write`, `exit`, `exit_group` or `getpid`

A store to the data, a `read` call, a `push`, a `call`, a `ret`, a `resb` or an `extern` all keep the old layout, so those programs behave exactly as before. Programs that use the standard library or any memory operand are not affected.

This only applies when the optimizer is on (the default) and to Linux ELF output. `-O0` always writes the full layout.

## i386 and IA-32

    cly linux hello.cly -cpu i386
    cly linux32 hello.cly -cpu ia32

`i386`, `i486`, `i586`, `i686` and `ia32` (`ia-32` and `x86-32` too) select 32-bit Intel code. Programs are 32-bit by default and `-m64` is an error. The name decides which instructions are allowed:

- `i386`: the 80386 set. `bswap`, `cmpxchg`, `cpuid`, `cmov`, SSE and the rest are errors.
- `i486`, `i586`, `i686`: each adds what that chip introduced.
- `ia32`: everything that exists in 32-bit mode.

The registers are the 32-bit ones. `rax`, `r8d`, `sil` and friends are errors that say so, and `syscall` is an error that points to `int 0x80` and `sysenter`. Without these names, 32-bit code stays as forgiving as before.

The built-in C library uses SSE2, so it only works with `ia32`. With the older names, Comprinly says so instead of putting instructions in your file that the chip does not have.

## -O3 and -Ofast

The default optimizer still puts size first. `-O3` adds a division shortcut for 64-bit code. A 64-bit `div` is slow on many Intel CPUs, and most divisions are of small numbers. So `xor edx, edx` + `div reg` (and `cqo` + `idiv reg`) check whether both numbers fit in 32 bits and use the 32-bit instruction if they do. The results are the same, and faults such as division by zero still happen as before.

On a loop that counts primes with `div`, the program went from about 45 ms to about 29 ms on an Intel Xeon. It costs about 20 bytes per division. On newer CPUs with a fast 64-bit divider the gain is small, so measure.

## Bug fixes

- `rdpid` now assembles in 32-bit mode.
- `vreducesh` operand order and `vcvtneps2bf16` operand sizes are fixed.
- FP16 scalar instructions with a memory operand (`vmovsh`, `vmovw`, `vcvtsh2si`, `vucomish` and others) now shrink the displacement with the right scale. NASM 2.16 gets this wrong, so these are checked against LLVM instead.
- Added the AVX-VNNI-INT8 instructions `vpdpbssd`, `vpdpbssds`, `vpdpbsud`, `vpdpbsuds`, `vpdpbuud` and `vpdpbuuds`.
- Fixed a crash in the static Linux build when an ARM64 or RISC-V source ended right at a memory page boundary.
