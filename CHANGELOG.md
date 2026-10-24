# Changelog

## 1.3.4

- New: `--o file` output flag. `-o` alone writes `name.o`, `-obj` writes `name.obj`. The kernel flag is optional (host default).
- New: `--ld` lightweight linker. ELF objects (x86-64, i386, ARM64, RISC-V) make a static ELF, COFF objects (32 and 64-bit) make a Windows exe.
- New: `-global`, `-unglobal`, `-uninstall`, `-update`, and a one-time `-global` hint on first run. `CLY_GLOBAL_DIR` picks another folder.
- New: smaller object files (empty sections, unneeded section symbols and repeated string tails are not written; `-nasm` keeps NASM's layout).
- New: two sets of 500 batched checks at the end of `tests/run_all.sh`. The first set is 10 batches of 50 (link and run, NASM comparison, install and update, format checks, fuzzing). The second set is 5 batches of 100 and tests different things: expressions and data layout against a model, macros and the preprocessor against NASM, assembly linked into C programs with gcc and run (also at `-O0` and `-O3`), ARM64 and RISC-V encodings against llvm-mc, and disassembly with objdump plus identical output on every run.
- Fix: `%substr` with a space between the start and the length (`%substr s 'abc' 2 1`) always returned the first character. It now reads the start like NASM does.
- Fix: 2 byte VEX prefix used with `ymm8` to `ymm15` or `r8` to `r15` in the last operand, which wrote the wrong register.
- Fix: `movbe` with 64-bit registers was rejected.
- Fix: `fadd`, `fmul`, `fsub`, `fsubr`, `fdiv`, `fdivr` with `st0, st0` picked a different (equal) encoding than NASM.
- Fix: `-nasm` accepted 64-bit register names in 16-bit and 32-bit code and read them as 32-bit. It is an error now.
- Fix: `-nasm` with `-omf`, `-as86`, `-ieee`, `-aout` and `-aoutb` used 64-bit defaults. They use NASM's defaults (16-bit, 32-bit for a.out).
- Fix: an unterminated `%[` crashed the preprocessor.
- Fix: `times` with huge counts of data took minutes.
- Fix: `--ld` crashed on damaged or truncated ELF input, and `-update` ran release data through a shell.

- Fix: vector instructions (VEX) with `xmm16` to `xmm31`, `ymm16` to `ymm31` as an operand were written with the wrong register (for example `vaddps xmm16, xmm1, xmm2` came out as `vaddps xmm0, xmm1, xmm2`). They now use the EVEX form, or are an error when the instruction has none.
- Fix: the 64-bit forms of the BMI and TBM instructions were missing: `andn`, `bextr`, `bzhi`, `blsi`, `blsmsk`, `blsr`, `mulx`, `pdep`, `pext`, `rorx`, `sarx`, `shlx`, `shrx`, `blcfill`, `blci`, `blcic`, `blcmsk`, `blcs`, `blsfill`, `blsic`, `t1mskc`, `tzmsk`, and `llwpcb`, `slwpcb`, `lwpins`, `lwpval` with 64-bit registers.
- Fix: 64-bit registers were rejected, or the wrong form was picked, in `kmovq`, `vmovq`, `vmovmskps`, `vmovmskpd`, `vpmovmskb`, `vpbroadcastb`, `vpbroadcastw`, `vpbroadcastq`, `vcvtsd2si`, `vcvtss2si`, `vcvttsd2si`, `vcvttss2si`, `vcvtsd2usi`, `vcvtss2usi`, `vcvttsd2usi`, `vcvttss2usi`, `vcvtsh2si`, `vcvtsh2usi`, `vcvttsh2si`, `vcvttsh2usi`, `vcvtsi2sd`, `vcvtsi2ss`, `vcvtusi2sd`, `vcvtusi2ss`, `vcvtsi2sh` and `vcvtusi2sh`. A memory operand with `qword` now picks the shorter VEX form where one exists.
- Fix: `vpinsrq` and `vpextrq` with a 64-bit register, and `vpextrb`, `vpextrw`, `vpextrd`, `vextractps` with a 64-bit destination register, were rejected. `vpinsrq` and `vpextrq` with memory now use the shorter VEX form.
- New: Intel AMX. The `tmm0` to `tmm7` registers and `ldtilecfg`, `sttilecfg`, `tilezero`, `tilerelease`, `tileloadd`, `tileloaddt1`, `tilestored`, `tdpbssd`, `tdpbsud`, `tdpbusd`, `tdpbuud`, `tdpbf16ps`. The tile loads and stores always get the SIB byte they need.
- New: `clac`, `stac`, `enclv`, `pconfig`, `xresldtrk`, `xsusldtrk`, `aadd`, `aand`, `axor`, `hreset`, `prefetchit0`, `prefetchit1`, and the immediate forms of `retnw`, `retfw`, `retfd`, `movntss`, `movntsd`, and `prefetch` and `prefetchw` with a sized memory operand. Before, a lone instruction name like `clac` was silently taken as a label.
- New: `-g` writes a DWARF 4 line table (`.debug_line`, `.debug_info`, `.debug_abbrev`, `.debug_str`) into ELF programs and ELF objects, for x86, ARM64 and RISC-V. gdb and `readelf --debug-dump=decodedline` read it. The optimizer keeps the original file and line of every instruction. With `-g` the section headers are kept in the output.
- New: output formats `-x32` (ELF x32), `-aout`, `-aoutb` and `-omf` (Intel OMF with `use16`/`use32`, `class=`, `group`, `..start`, `import`, `export`, `common`). a.out and OMF output matches `nasm -f aout`, `aoutb` and `obj` byte for byte on the test programs.
- New: `-map [FILE]` and the `map` directive write a section and symbol map.
- New: warning classes `-w+NAME` and `-w-NAME` (and `-w+error`, `-w+error=NAME`, `-w-all`), the `warning` directive, `-Z FILE` and `-s` for the error stream, `-X gnu|vc` for the message style.
- New: dependency options `-MF`, `-MT`, `-MQ`, `-MP`, `-MG` next to `-M` and `-MD`.
- New: `-@ FILE` response files.
- New: the `prefix`, `postfix`, `gprefix` and `gpostfix` directives add text to every global symbol name in object files.
- New: mask register shorthand. `kmov`, `kand`, `kor` and the other mask instructions without a size letter pick the size from the operands (`kmovw k1, ax`, `kmov k1, eax` is `kmovd`). NASM reads the last one as `kmovw`, this is a deliberate difference.
- Fix: `-nasm` mode no longer turns `[esp+4]` and `[ebp-8]` in 64-bit code into `[rsp+4]` and `[rbp-8]`. It now writes the 67 address size prefix like NASM. The default mode keeps the friendly conversion.
- Fix: `movdir64b`, `enqcmd` and `enqcmds` with a 32-bit register now get the address size prefix NASM writes (in 64-bit code and, for `enqcmd`, in 32-bit code).
- New: `-as86` (byte for byte like NASM) and `-ieee` (IEEE-695, same records as NASM), and `-g` for x32 objects.

## 1.3.3

- New: i386 and IA-32 CPUs. `-cpu i386`, `i486`, `i586`, `i686` and `ia32` (also `ia-32`, `x86-32`; a `cpu` line in the source does the same) mean 32-bit Intel code. Code is 32-bit by default, `-m64` is an error, and the name sets how far the instruction set goes (`i386` is the 80386 only, `ia32` is everything that exists in 32-bit mode). The 64-bit registers (`rax`, `r8d`, `sil`) and `syscall` are errors with a message that names the 32-bit way. The built-in C library needs SSE2, so it is only offered with `ia32`.
- Fix: `rdpid` did not assemble in 32-bit mode.
- Fix: `vreducesh` had its operands in the wrong order, and `vcvtneps2bf16` took the wrong operand sizes.
- Fix: the 8-bit disp8 shrink for FP16 scalar instructions (`vmovsh`, `vmovw`, `vcvtsh2si`, `vucomish` and friends) used the wrong scale, so a memory operand such as `[rax+0x40]` got a different displacement than other assemblers. Checked against LLVM.
- Docs: the benchmarks moved from the README to `BENCHMARKS.md`, organized by version starting at 1.3.2.
- Fix: the static Linux build could crash on ARM64 and RISC-V sources whose text ended exactly at a memory page boundary (a libc string function read past the end). The optimizer now copies that text itself.
- New: `vpdpbssd`, `vpdpbssds`, `vpdpbsud`, `vpdpbsuds`, `vpdpbuud`, `vpdpbuuds` (AVX-VNNI-INT8).
- New: `-O3` and `-Ofast`. They add a division shortcut: `xor edx, edx` + `div r64` and `cqo` + `idiv r64` take the faster 32-bit instruction when both numbers fit, and fall back to the 64-bit one otherwise. Results are identical. A primes loop dominated by `div` ran about 35 percent faster on an Intel Xeon. Costs about 20 bytes per division. The default optimizer is unchanged.
- New: `linux32` kernel. `cly linux32 file.cly` builds a 32-bit Linux program, the same as `cly linux file.cly -m32`. `linux64` is accepted too. Mixing `linux32` with `-m64` is an error.
- Faster start: the instruction tables are no longer fully built at start. The generated rows are filed by name and each instruction's rows are built the first time it is used. The table lookups, the 64-bit mode marks and `--dump-insns` give exactly the same result as before. A Hello World compiles in about half the time (the time before the first pass went from about 7 ms to about 1.5 ms).
- Smaller programs: when the optimizer can prove a Linux program never writes to its own memory, the data goes into the same read and execute segment as the code, and the stack header is left out if the program has no stack use. Hello World went from 274 to 162 bytes (64-bit) and is 129 bytes as a 32-bit program.
- The proof is strict. The program may only use register and jump instructions with no memory operands and no stack, and every `syscall` (or `int 0x80` in 32-bit code) must follow a constant load of `write`, `exit`, `exit_group` or `getpid`. Anything else, such as a store, a `read`, a `push` or a `call`, keeps the normal layout with a writable data segment.

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
