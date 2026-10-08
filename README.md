# Componly

Componly (`cly`) is a small low-level language for x86 and x86-64. It is a simplified and forgiving version of the assembler NASM. It compiles to Linux, Windows, macOS and bare metal, but only Linux is really tested and macOS is experimental.

REMINDER: This project is fully vibe-coded, so you may find some bugs. It is purely for recreational purposes.

See HOWTOUSE.md for a full guide and DOCUMENTATION.md for the preprocessor reference.

## Download

Prebuilt binaries are on the [Releases page](https://github.com/rhyelt/Componly/releases). Grab the one for your system, or build it yourself below.

## Use

    cly <linux|windows|mac|bare> <file> <-type>

    cly linux print.cly -bin
    cly windows print.cly -bin
    cly bare print.cly -bin
    cly linux print.cly -m32

The result is written next to the source file. Code is 64-bit by default, except for `bare` and raw types, which default to 32-bit. A `bits 32` or `bits 64` line in the source also works.

Types: `-bin` (native format for the kernel), `-elf`, `-exe`, `-macho`, `-obj`, `-coff`, `-flat`, `-img`, `-com`, `-hex`, `-srec`.
Other options: `-m32`, `-m64`, `-O0` (optimizer off), `-out file`, `-I dir`, `-D name=val`, `-U name`, `-l [file]` (listing), `-E` (preprocess only), `-w`, `-q`, `-v`, `-h`.

## What it does for you

- Data, bss and code go into sections automatically.
- `start:` is always the entry point.
- `syscall` uses Linux x86-64 numbers and registers (rax, rdi, rsi, rdx, r10, r8, r9) on every kernel. A tiny runtime is added only when you use it.
- In 32-bit code the 64-bit names work as 32-bit registers. r8 to r15 live in memory there.
- In 64-bit code you get the full register set, REX, RIP-relative addressing (`[rel x]`, `default rel`) and SSE through AVX-512.
- Curly quotes and missing commas are accepted.
- Symbol names, `start` and `%include` file names ignore case when there is only one match.
- 32-bit habits like `push eax`, `mov ebp, esp` and `[esp+4]` work in 64-bit code.
- The optimizer shrinks and speeds up your code by default and keeps behavior the same. `-O0` turns it off.
- Bare metal builds a bootable image: boot sector, protected or long mode switch, then your code. Output goes to VGA text, serial COM1 and port 0xE9. Exit powers off in QEMU, Bochs and VirtualBox, or halts.

## Build

You need a C compiler and make.

    make

or `gcc -O2 -fwrapv -o cly src/*.c`.

Extra targets:

- `make static` small static build
- `make cly32` 32-bit Linux build of cly itself (needs python3 and ziglang)
- `make cly.exe` 32-bit Windows build of cly itself (needs python3 and ziglang). It still makes 64-bit programs.
- `make gen` rebuild the instruction tables (needs python3)
- `make check` run the tests (the NASM comparisons need nasm, the emulator tests need python3 and unicorn)

## Instruction data

`data/insns.dat` is the NASM instruction table (BSD licence, notice kept in the file). `tools/gen_insns.py` turns its AVX and AVX-512 lines into `src/insn_gen.c`, and `tools/gen_insns64.py` does the same for the 64-bit-only lines (`src/insn_gen64.c`).

## Layout

- `src/` compiler (preprocessor, expressions, assembler, linker, format writers)
- `rt/` runtime sources per kernel, embedded by `rt/gen.py`
- `tests/` comparison scripts against NASM
- `examples/` sample programs
- `dist/` sample outputs

## Limits

- Linux programs were run for real, in 32-bit and 64-bit. Windows, macOS and bare metal output, including the 64-bit runtimes, was only run in an emulator.
- AVX, AVX2, FMA, BMI, AVX-512 (with masks, broadcast, rounding and FP16) and MPX work. XOP, FMA4 and TBM work too. AMX does not.
- A few rare old x87 shorthands that NASM accepts are rejected.
- Bare metal has no interrupt table, and `syscall` only offers the basic Linux calls.
- No Mach-O object output.

## Licence

AGPL-3.0. The NASM instruction table keeps its own BSD notice.
