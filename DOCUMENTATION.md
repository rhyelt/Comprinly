# Comprinly preprocessor and language reference

This file covers the language: CPU selection, the directives, the preprocessor, and the ARM64 and RISC-V details. For installing and running Comprinly, see HOWTOUSE.md. The syntax follows NASM, so NASM examples carry over.

Everything listed here has been run and checked. Things Comprinly does not do are at the end.

## Macros

Multi-line macros take a name and a parameter count:

    %macro load 2
        mov %1, %2
    %endmacro

    load eax, 5

- `%1`, `%2`, ... are the parameters. `%0` is how many were passed.
- A range such as `%macro pad 1-3` or `%macro all 1-*` allows a variable number of parameters.
- `%%name` makes a label that is unique to each use of the macro.
- Macros can call other macros, and a macro can define another macro. The inner one exists after the outer one has run.
- `%rep N` ... `%endrep` repeats a block.

### %rotate

`%rotate n` shifts the parameters left by `n`, or right if `n` is negative. It is the way to walk through a long parameter list:

    %macro emit 1-*
    %rep %0
        db %1
    %rotate 1
    %endrep
    %endmacro

    emit 1, 2, 3

## Defining names

| Directive | What it does |
|-----------|--------------|
| `%define A 1` | Case-sensitive. The body is expanded when `A` is used. |
| `%idefine A 1` | Same, but `A` and `a` match the same name. |
| `%xdefine B A+1` | The body is expanded right now, when you define it. |
| `%assign n n+1` | A number. The expression is calculated once and can be changed later. |
| `%defstr S abc` | Makes the string `"abc"`. |
| `%deftok T 5` | Turns a string into tokens. |
| `%strcat C "a", "b"` | Joins strings. |
| `%undef A` | Removes a name. |
| `%defalias Z 9` | Makes a name that points at another. |

The difference between `%define` and `%xdefine`:

    %define A 1
    %xdefine B A+1
    %define A 9
    mov eax, B        ; becomes mov eax, 1+1

## Strings

    %strlen n "hello"        ; n is 5
    %substr s "hello" 2, 3   ; s is "ell"

`%substr` counts from 1. The first number is the start and the second is the length.

## Conditionals

`%if`, `%elif`, `%else` and `%endif` work on a number expression. These checks are available as well, and each one has a negated form with `n` after `if` (for example `%ifnnum`):

| Directive | True when |
|-----------|-----------|
| `%ifdef X` | `X` is defined |
| `%ifmacro m` | a macro named `m` exists |
| `%ifid x` | `x` is an identifier |
| `%ifnum x` | `x` is a number |
| `%ifstr x` | `x` is a string |
| `%iftoken x` | `x` is a single token |
| `%ifempty` | nothing follows |
| `%ifenv NAME` | the environment variable `NAME` exists |
| `%ifctx name` | the top context is `name` |
| `%ifidn a, b` | `a` and `b` are the same text |
| `%ifidni a, b` | same, ignoring case |

    %ifidn %1, eax
        xor %1, %1
    %endif

`%ifnidn` and `%ifnidni` are the negated comparisons.

## Context stack

A context is a named scope for macros. Names that start with `%$` belong to the top context and disappear when it is popped.

    %push loopctx
    %define %$count 5
        mov ecx, %$count
    %pop

- `%push name` opens a context.
- `%pop` closes it.
- `%repl name` renames the top context.
- `%ifctx name` and `%ifnctx name` test it.

### %arg, %local and %stacksize

These name stack variables inside a context. `%stacksize` picks the frame style:

| Mode | Base register | Slot size |
|------|---------------|-----------|
| `flat` | `ebp` | 4 |
| `flat64` | `rbp` | 8 |
| `small`, `large` | `bp` | 2 |

If you do not set one, Comprinly uses `flat` in 32-bit code and `flat64` in 64-bit code.

    bits 64
    f:
    %push c
    %stacksize flat64
    %assign %$localsize 0
    %arg a:qword, b:dword
    %local t:qword
        push rbp
        mov rbp, rsp
        sub rsp, %$localsize
        mov rax, [a]        ; [rbp+16]
        mov [t], rax        ; [rbp-8]
        leave
        ret
    %pop

Use the names inside square brackets. `%assign %$localsize 0` has to come before `%local` so Comprinly can add up the space.

## Messages

    %warning this is not finished
    %error  this is wrong
    %fatal  cannot go on

- `%warning` prints a warning and keeps going.
- `%error` prints an error and the build fails at the end.
- `%fatal` stops right away.

## Other directives

- `%line 100+1 other.asm` makes later messages report that file and line number. Useful for generated code.
- `%pragma` is accepted and ignored, so NASM files that contain it still build.
- `%include "file"` pastes a file in. See HOWTOUSE.md for how the names are found.

## Environment variables

`%!NAME` becomes the text of the environment variable, as a string:

    db %!HOME

## Predefined macros

| Name | Value |
|------|-------|
| `__FILE__` | current file name, as a string |
| `__LINE__` | current line number |
| `__BITS__` | 16, 32 or 64 (follows `-m32`, `-m64` and the default for your target) |
| `__SECT__` | the current section directive |
| `__OUTPUT_FORMAT__` | `elf64`, `elf32`, `win64`, `win32`, `macho64`, `macho32` or `bin` |
| `__NASM_MAJOR__`, `__NASM_MINOR__`, `__NASM_SUBMINOR__`, `__NASM_PATCHLEVEL__` | the NASM version Comprinly matches (2.16.01) |
| `__NASM_VERSION_ID__`, `__NASM_VER__` | the same version as a number and as a string |
| `__CLY__`, `__CLY_MAJOR__`, `__CLY_MINOR__`, `__CLY_VERSION__` | the Comprinly version |
| `__DATE__`, `__TIME__`, `__DATE_NUM__`, `__TIME_NUM__` | build date and time (UTC) |
| `__UTC_DATE__`, `__UTC_TIME__`, `__UTC_DATE_TIME__`, `__POSIX_TIME__` | the same in other forms |
| `__PASS__` | always 3 |
| `__CLY_ARCH__` | `"x86-64"`, `"arm64"`, `"riscv32"` or `"riscv64"` |
| `__ARCH_X86__`, `__ARCH_ARM64__`, `__ARCH_RISCV__`, `__ARCH_RISCV32__`, `__ARCH_RISCV64__` | defined as 1 for the chosen CPU only |

Every one of these also works with question marks around the name, for example `__?FILE?__` and `__?NASM_MAJOR?__`.

You can add your own on the command line with `-D NAME=value` and remove one with `-U NAME`. `-P file` pastes a file before everything else. `-E` prints the result of the preprocessor and stops.

## The cpu keyword

    cpu arm64
    cpu x86-64
    cpu risc-v
    cpu rv32imac

`cpu` selects the instruction set. It has to be the first real line of the file. Blank lines and comments above it are fine. Anywhere else it is an error saying it must be the first line, because the whole file is read for one CPU.

Order of choice: `-cpu` on the command line, the `cpu` line, a guess from the code (the first instruction or register that belongs to one CPU only), then x86-64. After that, anything that clearly belongs to another CPU is an error that names the line the choice came from. A name Comprinly does not know gets a warning and Comprinly falls back to guessing. Old 32-bit ARM names (`arm`, `armv7`, `thumb`) are recognised and rejected with a clear message.

RISC-V names carry the extensions: `rv64gc`, `rv32imac`, `rv64imafdc_zba_zbb`. Leaving out `c` turns off compressed code, leaving out `m`, `a`, `f` or `d` makes those instructions an error. `risc-v` and `riscv` alone mean the default feature set, 64-bit, and follow `-m32`.

## Directives shared by every CPU

These behave the same on x86, ARM64 and RISC-V:

| Directive | Notes |
|-----------|-------|
| `section .text`, `.data`, `.bss`, `.rodata` | sections are also made automatically |
| `global`, `extern` | symbols for object files and linking |
| `db`, `dw`, `dd`, `dq` | data. Strings, floats and expressions work. `dt`, `do`, `dy` and `dz` on x86 only |
| `resb`, `resw`, `resd`, `resq` | bss space |
| `equ`, `times`, `align`, `alignb` | as in NASM |
| `incbin` | include raw bytes |
| `struc`, `endstruc`, `istruc`, `at`, `iend` | structures |
| `$`, `$$`, `%` operators, `seg`, `wrt` | expression terms where the target has them |
| `org` | start address for raw output |
| `default rel`, `bits`, `use16`, `use32`, `use64` | x86 only |

The full macro language (`%macro`, `%if`, `%rep`, `%include`, contexts, `%assign` and the rest below) is shared by all CPUs. Your macros can hide the difference between CPUs:

    %macro exit 1
    %ifdef __ARCH_ARM64__
        mov x0, %1
        mov x8, 93
        svc 0
    %elifdef __ARCH_RISCV__
        li a0, %1
        li a7, 93
        ecall
    %else
        mov rdi, %1
        mov rax, 60
        syscall
    %endif
    %endmacro

## Forgiving syntax

Comprinly accepts what people tend to type:

- Optional `#` on ARM64 immediates, optional commas, curly quotes, any case.
- Missing `global` and a missing section line.
- `start`, `_start` and `START` as entry points.
- Case-insensitive symbol fallback when there is only one match.
- Typo hints on unknown instructions.
- A lone word on a line is a label, as in NASM. When it is one edit away from a real instruction, a warning points that out.
- Numeric local labels on ARM64 and RISC-V.
- Automatic repair of branches that are out of range.
- Automatic constant building (`mov`, `li`, `ldr =`).

It stays strict about the things that change meaning: operand sizes, ranges, and register classes. Out of range immediates are errors with the allowed range in the message.

## ARM64 reference

Registers: `x0` to `x30`, `w0` to `w30`, `sp`, `wsp`, `xzr`, `wzr`, `lr`, `fp`, vectors `v0` to `v31` (with `.8b .16b .4h .8h .2s .4s .1d .2d` and element indexes like `v1.s[2]`), scalar `b h s d q` registers.

Instruction groups, all checked against LLVM byte for byte:

- Data processing: add and subtract (with carry, shifted and extended registers), logical, shifts, rotates, bitfield moves and inserts, `extr`, move wide, multiply-add, long multiplies, divides, conditional select, conditional compare, `clz`, `cls`, `rbit`, `rev` forms, `crc32`.
- Branches: `b`, `bl`, `br`, `blr`, `ret`, `b.cond`, `cbz`, `cbnz`, `tbz`, `tbnz`, and the pointer authentication forms (`braa`, `retaa`, `blraa`, ...).
- Memory: `ldr`, `str`, `ldp`, `stp`, unscaled forms, literal loads, signed and sized loads, exclusives and acquire-release, prefetch, `ldnp`, `stnp`, atomics (LSE) and `ldapr` forms.
- Floating point: arithmetic, compare, conditional, convert, round, fused multiply-add, `fmov`, half precision, bfloat conversions.
- NEON: integer, floating point, pairwise, across-lane, permute, extract, table lookup, shifts, long, wide and narrow forms, by-element forms, structure loads and stores, `movi`, `mvni`, `orr`, `bic` immediates, dot product, matrix multiply, complex numbers.
- Crypto: AES, SHA1, SHA2, SHA3, SHA512, SM3, SM4.
- System: `svc`, `hvc`, `smc`, `brk`, `hlt`, `mrs`, `msr`, `sys`, `dc`, `ic`, `tlbi`, `at`, barriers, hints, BTI, memory tagging, flag manipulation, CSSC, LS64, TME, `wfet`, `wfit`, `udf`.

Pseudo instructions: `mov` with any 64-bit constant, `ldr Rt, =expr`, `adr` and `adrp` with symbols, `cmp`, `cmn`, `tst`, `neg`, `mvn`, `cset`, `csetm`, `cinc`, `cinv`, `cneg`, `ngc`, `lsl`/`lsr`/`asr`/`ror` with immediates, `sxtw`, `uxtb` and friends, and `nop`.

## RISC-V reference

Registers: `x0` to `x31` with ABI names (`zero ra sp gp tp t0-t6 s0-s11 fp a0-a7`), floating point `f0` to `f31` with `ft0-ft11 fs0-fs11 fa0-fa7`.

Instruction groups, all checked against LLVM byte for byte on both RV32 and RV64:

- I: everything including `fence`, `fence.tso`, `ecall`, `ebreak`, `wfi`, `mret`, `sret`.
- M, A (all `.aq .rl .aqrl` forms), F, D, Zfh and Zfhmin.
- C: automatic compression, or explicit `c.` instructions. Zcb forms are not included.
- Zicsr, Zifencei, Zicond.
- Bit manipulation: Zba, Zbb, Zbc, Zbs.
- Scalar crypto: Zbkb, Zbkx, Zknd, Zkne, Zknh, Zksed, Zksh.
- Cache and misc: Zicbom, Zicboz, Zicbop, Zawrs, Svinval, hypervisor (H), `pause`, `ntl.*`.
- CSR names for machine, supervisor, user, hypervisor and debug registers.

Pseudo instructions: `li`, `la`, `lla`, `lga`, `mv`, `not`, `neg`, `negw`, `sext.w`, `sext.b`, `sext.h`, `zext.b`, `zext.h`, `zext.w`, `seqz`, `snez`, `sltz`, `sgtz`, `beqz`, `bnez`, `blez`, `bgez`, `bltz`, `bgtz`, `bgt`, `ble`, `bgtu`, `bleu`, `j`, `jr`, `ret`, `call`, `tail`, `nop`, `fmv.s`, `fmv.d`, `fneg.*`, `fabs.*`, `fgt.*`, `fge.*`, `csrr`, `csrw`, `csrs`, `csrc`, `csrwi`, `csrsi`, `csrci`, `rdcycle`, `rdtime`, `rdinstret`, `frcsr`, `fscsr`, `frrm`, `fsrm`, `frflags`, `fsflags`, `unimp`.

## Output support

| Target | x86 | ARM64 | RISC-V |
|--------|-----|-------|--------|
| linux executable | yes | yes | yes |
| linux object (`-obj`) | yes | yes | yes |
| windows | yes | no | no |
| mac | yes | no | no |
| bare (boot image) | yes | raw only | raw only |
| `-flat`, `-hex`, `-srec` | yes | yes | yes |

"Raw only" means flat bytes, Intel HEX or S-records with no boot code added.

## Coverage compared with NASM

Things NASM does that Comprinly does too: the whole preprocessor, macros with ranges and defaults, contexts, `%rep`, conditionals including `%ifidn`, `%ifnum`, `%ifstr`, `%ifmacro`, `%ifctx` and `%ifenv`, string functions, `%include` and `%pathsearch`, `incbin`, `times`, `struc`, local labels, `$` and `$$`, floating point data, `equ`, `seg`, `wrt`, `{vex}` and `{evex}` prefixes, `align` and `alignmode`, `-D`, `-U`, `-P`, `-I`, `-E` and listings. Instruction coverage for x86 goes up to AVX-512 and FP16 and was compared with NASM.

Things Comprinly adds on top: ARM64 and RISC-V from the same syntax, one-step linking into executables, bare metal images, an optimizer, instruction typo hints, numeric labels and automatic branch repair.

Things NASM and other assemblers have that Comprinly does not are listed next.

## Not supported

These are not in Comprinly, so they are not documented above:

- APX (the extended general registers `r16` to `r31` and the new instruction forms)
- OMF and `.obj` output for 16-bit tools (`-coff` and `-obj` make COFF and ELF)
- dependency files (`-M` style output)
- debug information of any kind: DWARF, STABS, and the `-g` option
- debugger integration
- a disassembler like NDISASM
- `%note` (NASM does not have it either)
- SVE and SME on ARM64, 32-bit ARM
- the RISC-V vector extension (V) and the Zcb `c.` forms
- PE and Mach-O output for ARM64, and any Windows or macOS output for RISC-V
- GNU assembler syntax (`.globl`, `.word` and so on). Use the NASM-style names
