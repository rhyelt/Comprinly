# Componly

Componly (`cly`) is a small low-level language for x86-64. It is a simplified and forgiving version of the assembler NASM. It compiles or assembles to Linux, Windows, and MacOS but only Linux is tested and MacOS is experimental.

REMINDER: This project is fully vibe-coded in which you may find some bugs. This project is purely for recreational purposes. 

See HOWTOUSE.md for a full guide.

## Use

    cly <linux|windows|mac|bare> <file> <-type>

    cly linux print.cly -bin
    cly windows print.cly -bin
    cly bare print.cly -bin
    cly linux print.cly -m32

The result is written next to the source file. Code is 64-bit by default, except for `bare` and raw types, which default to 32-bit. A `bits 32` or `bits 64` line in the source also works.

Types: `-bin` (native format for the kernel), `-elf`, `-exe`, `-macho`, `-obj`, `-coff`, `-flat`, `-img`, `-com`, `-hex`, `-srec`.
Other options: `-m32`, `-m64`, `-out file`, `-I dir`, `-D name=val`, `-U name`, `-l [file]` (listing), `-E` (preprocess only), `-w`, `-q`, `-v`, `-h`.

## What it does for you

- Data, bss and code go into sections automatically.
- `start:` is always the entry point.
- `syscall` uses Linux x86-64 numbers and registers (rax, rdi, rsi, rdx, r10, r8, r9) on every kernel. A tiny runtime is added only when you use it.
- In 32-bit code the 64-bit names work as 32-bit registers. r8 to r15 live in memory there.
- In 64-bit code you get the full register set, REX, RIP-relative addressing (`[rel x]`, `default rel`) and SSE through AVX-512.
- Curly quotes and missing commas are accepted.
- Bare metal builds a bootable image: boot sector, protected or long mode switch, then your code. Output goes to VGA text, serial COM1 and port 0xE9. Exit powers off in QEMU, Bochs and VirtualBox, or halts.

## Build

    make

or `gcc -O2 -fwrapv -o cly src/*.c`. 32-bit builds: `make cly32` (Linux) and `make cly.exe` (Windows).

## Instruction data

`data/insns.dat` is the NASM instruction table (BSD licence, notice kept in the file). `tools/gen_insns.py` turns its AVX and AVX-512 lines into `src/insn_gen.c`, and `tools/gen_insns64.py` does the same for the 64-bit-only lines (`src/insn_gen64.c`). Run `make gen` to rebuild it.

## Layout

- `src/` compiler (preprocessor, expressions, assembler, linker, format writers)
- `rt/` runtime sources per kernel, embedded by `rt/gen.py`
- `tests/` comparison scripts against NASM
- `examples/` sample program
- `dist/` sample outputs

## Limits

- Mac output, and the 64-bit Windows, mac and bare runtimes, were tested only in an emulator.
- AVX, AVX2, FMA, BMI, AVX-512 (with masks, broadcast, rounding and FP16) and MPX work. XOP, FMA4 and TBM work too. AMX does not.
- Bare metal has no interrupt table, and `syscall` only offers the basic Linux calls.
