# Comprinly 1.3.4

This release is about instruction coverage and about trust. I compared Comprinly with NASM 2.16 on every instruction name NASM knows, with many operand shapes each, in 64-bit and 32-bit mode, and fixed what differed. Then I went hunting for bugs with two sets of 500 batched checks. The first set (10 batches of 50) builds, links and runs programs, compares random instructions with NASM, and feeds damaged files to the assembler and the linker. The second set (5 batches of 100) is different: it checks expressions and data layout against a model, macros and the preprocessor against NASM, assembly linked into C programs with gcc and run at three optimizer levels, ARM64 and RISC-V encodings against llvm-mc, and disassembly with objdump plus identical output on every run. All 1000 pass. 1.3.4 is much more stable and much improved over earlier versions, and it is a release you can trust.

## Wrong code that is now right

Vector instructions with `xmm16` to `xmm31` (and the `ymm` ones) used to assemble with a different register. `vaddps xmm16, xmm1, xmm2` came out as `vaddps xmm0, xmm1, xmm2`, with no warning. The short VEX form cannot name those registers, so it is skipped now and the EVEX form is used.

Vector instructions with `ymm8` to `ymm15` (or `xmm8` to `xmm15`) as the last operand, or with `r8` to `r15` in the memory operand, used the short two byte VEX prefix, which cannot name them. `vaddps ymm0, ymm6, ymm8` came out as `vaddps ymm0, ymm6, ymm0`. They now use the three byte form, the same as NASM.

In `-nasm` mode, 64-bit register names in 16-bit and 32-bit code are errors, as they are in NASM. They used to be quietly read as their 32-bit halves.

An unterminated `%[` in a macro line crashed the preprocessor. It is an error now.

## 64-bit forms that were missing

`movbe` with a 64-bit register, and `fadd st0, st0` style forms now pick the same encoding NASM does.

The BMI1, BMI2 and TBM instructions only accepted 32-bit registers. `mulx rax, rbx, rcx` and the others now work. The same goes for `kmovq` with a 64-bit register, the scalar conversions with a 64-bit integer (`vcvtsi2sd xmm0, xmm1, rax`, `vcvtsd2si rax, xmm0` and the unsigned and FP16 ones), `vmovq` with a 64-bit register, and the mask and broadcast instructions that take one.

## New instructions

AMX tiles (`tmm0` to `tmm7`, loads, stores, the dot products), the supervisor and enclave instructions `clac`, `stac`, `enclv`, `pconfig`, `xresldtrk`, `xsusldtrk`, the RAO-INT `aadd`, `aand`, `axor`, plus `hreset`, `prefetchit0` and `prefetchit1`.

A lone name that is not an instruction, such as `clac` was before, is still read as a label. Use `-nasm` to make that an error.

## What NASM has that Comprinly now has too

- `--o file` names the output, `-o` writes an object named `.o`, `-obj` writes one named `.obj`. The kernel word is optional and defaults to the machine you are on.
- `--ld` links objects into a program: ELF objects (x86-64, i386, ARM64, RISC-V) into a static ELF, COFF objects into a Windows `.exe`.
- `-global`, `-unglobal`, `-uninstall` and `-update` for installing and updating from the command line, and a one time hint about `-global` on the first run.
- Object files are written small. Unused empty sections and unneeded section symbols are left out, and string tables share their tails.
- `times` with a large count of plain data (`times 100000000 db 0`) is now fast. It took minutes before.

- `-g` debug line information in ELF files and objects (DWARF 4), checked with gdb, readelf and ld.
- More object formats: `-x32`, `-aout`, `-aoutb`, `-omf`, `-as86`, `-ieee`. a.out, OMF and as86 come out byte for byte like NASM's for the test programs.
- Warning classes, `-Z`, `-s`, `-X`, `-map`, `-@` response files, `-MF -MT -MQ -MP -MG`, and the `prefix` and `postfix` directives.
- Mask register instructions without a size letter.

The output formats are `-x32`, `-aout`, `-aoutb`, `-omf`, `-as86` and `-ieee`.
