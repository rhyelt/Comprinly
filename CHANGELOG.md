# Changelog

## 1.3.2

- New: `-o` writes an object file named `.o` on every kernel, Windows included. It is a type flag like `-obj`, not a file name. Use `-out FILE` to name any output. The error for a stray file name after `-o` now says so.
- Smaller Mac programs: the last segment of a 64-bit Mach-O file is no longer padded to a full page. A program with no file data went from 4096 bytes to about 1100.
- Smarter optimizer: `imul` by 1, 2, 4, 8 becomes nothing or a shift, and `imul` by 3, 5, 9 becomes one `lea`, when nothing reads the flags afterwards.
- Better errors: in `-nasm` mode a memory operand with no size next to an immediate (`mov [x], 5`) asks for `byte`, `word`, `dword` or `qword`. A misspelled name that is a defined name plus one or two extra characters (`n0` for `n`) now gets a "did you mean" hint, also for one letter names.
- Fixed: `dd sym - label` where the label is in the section being written now becomes a PC relative relocation in object files, as NASM does, instead of failing with "cannot be expressed as an ELF relocation".
- Fixed: the test scripts used `-o` as a file name, they use `-out` now.

## 1.3.1

- Changed: `syscall` is now a plain pass-through. It sends the registers to the system the program runs on and returns whatever comes back. The built-in per-system runtimes, the number translation tables and the call checks for Windows, Mac and bare are gone, so programs carry no support code. Use the call numbers of the system you build for. Linux numbers are still checked.
- Changed: the source now carries comments that explain each file and function.

- Smaller compiler: the generated instruction tables, and the built-in C library are now stored compressed and unpacked at start. `cly` went from 1.1 MB to 0.6 MB, `cly32` from 860 KB to 570 KB and `cly.exe` from 910 KB to 630 KB. The release builds also use `-Os`, section garbage collection and no unwind tables (`make static`, `make cly64`, `make cly32`, `make cly.exe`).
- Smaller programs: when the optimizer is on, `mov r, N` that repeats a value already in another register becomes a register copy, a redundant `xor r, r` or `mov r, N` is dropped, `xor r64, r64` becomes `xor r32, r32`, `add r, 128` becomes `sub r, -128` when the flags are free, and `mov r64, label` becomes the 5 byte `mov r32, label` in Linux 64-bit executables. A conditional jump to the next line is removed and a `jmp` to a `ret` becomes the `ret`.
- Smaller ELF files: with the optimizer on, x86 Linux executables leave out unused header space and default section padding. A program without data saves a whole program header.
- Faster: symbol tables grow as they fill, which took a 200,000 line source from 29 seconds to under 2. Keyword checks skip most string compares, the instruction encoder no longer compares every prefix name for every token, and the optimizer stops re-reading unchanged lines.
- Fixed: the compiler was very slow on musl builds (`cly32`) because of line searches over the whole library text. Start up on `cly32` went from 100 ms to 14 ms.
- Fixed: `-q` now hides warnings too, as the README says. It used to hide only the size line.
- Fixed: the pass lines of `-log` reported a hash instead of the real size.

## 1.3.0

- New: plain NASM programs build and run. `global`, `extern`, `main`, `_start`, `wrt ..plt`, `wrt ..got`, `@PLT`, `section`, `istruc` and friends are accepted.
- New: a small built-in C library for Linux x86 and x86-64 (`printf`, `scanf`, `malloc`, string and memory functions, `puts`, `exit` and more). Only what the program uses is added, so a `printf "Hello"` program stays tiny.
- New: calls to `printf`, `puts`, `putchar` and `exit` with constant text turn into a direct write or exit. A NASM hello world comes out at about the same size as the Comprinly one.
- New: leak warnings. When a block from `malloc`, `calloc`, `strdup`, `realloc`, `mmap` or a function that returns one may never be freed, the warning names the lines. The program still builds.
- New: a leak-free copy is built next to the output as `<name>_fixed`. `-fix` makes the fixed copy the main output, `-fixsrc` also writes its source.
- New: `sscanf`, `strtod`, `atof`, `qsort`, `%e` and `%g` in `printf`, and math functions (`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `log10`, `log2`, `pow`, `fmod`, `hypot`, `round`, `trunc`, `fmin`, `fmax`).
- New: `-nasm` mode. Strict NASM behaviour: x86 only, case-sensitive symbols, declared externs, no missing commas, no guessing, no hints, no library, no leak check, no optimizer.
- New: modes can be given in any order after the output type.
- New: `-nolibc` and `-noleak`.
- New: `-log` and `-log=FILE`. Timestamped log of the compile steps: preprocessing, includes, macros, library, optimizer, assembler passes, link and write.
- New: `-Werror` fails the build when any warning was shown. `-M` prints a make rule with the include and incbin files, `-MD` writes it to `<output>.d`.
- Improved: the optimizer also drops `add/sub r, 0`, shifts and rotates by 0, `lea r, [r]` and `push r` followed by `pop r`, and turns `mov r, -1` into the shorter `or r, -1`. Each one only fires when it cannot change a register or a flag the program uses. `--dump-opt` prints the optimized source.
- Fixed: a warning that came up twice for one line is now shown once.
- Improved: `-nasm` follows NASM much more closely. Object files match NASM's sections, symbols, relocation types, `common`, `global` attributes, FILE symbol and `align` fill. `mov r64, symbol`, `dq symbol`, `%[...]` joins, `%isdef` style functions, `0x_ff` numbers and several error cases now behave the same. A compare suite in `tests/compat` checks this against a real NASM.
- Fixed: large sources were slow because every undefined name was matched case-insensitively against all symbols. A 200,000 line file went from 29 seconds to 4.
- Changed: the source is sorted into `src/core`, `src/x86`, `src/arm64`, `src/riscv`, `src/opt`, `src/format`, `src/runtime` and `src/extras`.
- Project: Comprinly began as a vibe-coded proof of concept and is now a serious project open to collaborators. From 1.3.0 it is partly vibe-coded and partly hand-written. See `CONTRIBUTING.md`.

## 1.2.1

- Fix: `cmp ebp,-1`, `xor ebp,ebp`, `test ebp,ebp` and `lea ebp,[rax+1]` were shortened wrongly and changed meaning. Fixed.
- New: 64-bit `add`, `cmp` and similar accept a symbol as the immediate.
- New: `-obj` treats undefined symbols as external. ELF objects carry a `.note.GNU-stack` section so `ld` stays quiet.

## 1.2.0

- New: ARM64 support. Base ISA, floating point, NEON, crypto, LSE atomics, pointer authentication, memory tagging and the usual extensions.
- New: RISC-V support, 32 and 64-bit. I, M, A, F, D, C, Zicsr, Zifencei, Zba, Zbb, Zbs, Zbc, Zicond, Zfh, scalar crypto, cache and hypervisor instructions.
- New: `cpu` keyword on the first line, `-cpu` option, and automatic guessing from the code. The default stays x86-64.
- New: predefined macros `__CLY_ARCH__`, `__ARCH_X86__`, `__ARCH_ARM64__`, `__ARCH_RISCV__`, `__ARCH_RISCV32__`, `__ARCH_RISCV64__`.
- New: ELF executables and relocatable objects for ARM64 and RISC-V, linkable with LLVM and GNU objects.
- New: raw, Intel HEX and S-record output for ARM64 and RISC-V.
- New: numeric local labels (`1:`, `1b`, `1f`), automatic repair of far branches and automatic constant building on the new CPUs.
- New: RISC-V compression happens by itself, with `-O0` to turn it off.
- New: a warning when a lone word on a line is one edit from a real instruction.
- New: x86 CPU name aliases (`amd64`, `i386`, `x64` and more).
- Docs: README, HOWTOUSE and DOCUMENTATION rewritten. New banner and logo in `images/`.
- Tests: tens of thousands of ARM64 and RISC-V instruction forms compared with LLVM, emulator runs, long-branch tests, `li` checks, and cross-linking tests.
- New: better errors. Numbers above 64 bits are rejected instead of wrapping. A redefined symbol shows where it was first defined. Undefined symbols and unknown instructions suggest a close match on every CPU. Using an instruction from another CPU says so and how to switch. Wrong macro parameter counts are named. An unclosed `%if` points at the line that opened it. Short jumps that are out of range say by how much.
- New: the CPU guess now follows the first thing that only one CPU has, and foreign registers or instructions later in the file are errors.
- New: syscall checks. Unknown Linux call numbers, `svc` with a number, `int 0x80` in 64-bit code, and calls the windows, mac, bare and 32-bit Linux runtimes cannot do are warned about.
- New: ARM64 and RISC-V optimizer with dead code removal, jump cleanup, branch inversion, `cbz` and `cbnz`, and stack reload cleanup. `-O0` turns it off.
- Tests: `tests/opt_nx_fuzz.py` runs random ARM64 and RISC-V programs with and without the optimizer in an emulator.
- Tests: `tests/errors.sh` checks the messages, and a mutation fuzzer under address and undefined-behavior checking found no crashes or hangs.
- Fixed: the `li` test generated values over 64 bits that used to wrap silently.
- Not included: SVE, SME, RISC-V V, Zcb, PE/Mach-O for ARM64.

## 1.1.0

- New: optimizer, on by default, `-O0` turns it off. See HOWTOUSE.md section 4.
- New: smaller Linux ELF files (no section headers unless `-O0`).
- New: `START`, `_start` and `start` (with or without a colon) all work as the entry point.
- New: symbol names fall back to a case-insensitive match when there is exactly one.
- New: `%include` ignores case in file names and accepts backslashes.
- New: 64-bit code accepts `push eax`, `pop ebx`, `call eax`, `mov ebp, esp`, `sub esp, 16`, `[esp+4]`, `[ebp-8]`, `pushad`, `popad`, `pushfd` and `popfd`.
- New: warning when `$ - label` counts a trailing 0 byte.
- Fixed: `cmovcc` with 64-bit registers was rejected in 64-bit mode.
- Fixed: `__BITS__` and `__OUTPUT_FORMAT__` now match the real target (64-bit by default), the `__?NAME?__` forms work for every predefined macro, and `%arg`/`%local` use `rbp` in 64-bit code.
- New: DOCUMENTATION.md, a preprocessor and directive reference.
- Tests: optimizer fuzzers for 32-bit and 64-bit code that compare results in an emulator.

## 1.0.0

First release.
