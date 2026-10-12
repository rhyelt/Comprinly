# Changelog

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
