# How to use Comprinly

Comprinly (`cly`) turns assembly source into a program you can run. You write NASM-style code for x86, ARM64 or RISC-V, pick the system you want to run it on, and get a finished file. No assembler or linker needed.

## 1. Get it

You need a C compiler.

    make            build ./cly
    make static     small static build
    make cly64      small static 64-bit Linux build (needs python3 and ziglang)
    make cly32      32-bit Linux build of cly itself (needs python3 and ziglang)
    make cly.exe    32-bit Windows build of cly (needs python3 and ziglang)
    make check      run the tests
    make install    copy to /usr/local/bin

Without make: `gcc -O2 -fwrapv -Isrc -o cly src/main.c src/*/*.c`

## 2. Your first program

Save this as `hello.cly`:

    msg db "Hello, World!", 10
    len equ $ - msg

    start:
        mov rax, 1
        mov rdi, 1
        mov rsi, msg
        mov rdx, len
        syscall

        mov rax, 60
        mov rdi, 0
        syscall

Build and run:

    cly linux hello.cly
    ./hello

The same file works for every system:

    cly windows hello.cly      makes hello.exe
    cly mac hello.cly          makes hello
    cly bare hello.cly         makes hello.bin, a bootable disk image

The output is written next to the source file, not in the folder you ran `cly` from and not next to the `cly` program. `cly linux src/hello.cly` makes `src/hello`.

Linux and mac both name the file `hello`, so use `-out` if you build both.

## 3. 32-bit or 64-bit

Code is 64-bit by default for `linux`, `windows` and `mac`, and 32-bit for `bare` and the raw types (`-flat`, `-com`, `-hex`, `-srec`, `-img`). Change it in one of three ways:

    cly linux hello.cly -m32       flag on the command line
    bits 32                        line in the source (first one wins)
    use64                          same thing, other spelling

The flag beats the source line. `-m16` is there for `-com` style code.

In 64-bit mode you get `rax` to `r15`, the byte registers `sil`, `dil`, `spl`, `bpl` and `r8b` to `r15b`, `xmm0` to `xmm15`, `cr8`, and RIP-relative addressing:

    lea rsi, [rel msg]
    default rel                    make [msg] mean [rel msg] from here on

A few 64-bit rules to know:

- `mov rax, 5` is shortened to `mov eax, 5`, the same as NASM. `mov rax, msg` loads the full 64-bit address.
- Plain `[msg]` is an absolute 32-bit address. That fits on Linux and Windows but not on mac, where programs load above 4 GB. Use `[rel msg]` or `default rel` there (it is on for mac by default). If an address does not fit you get a clear error.
- Use the 64-bit names for addresses. `[rdi]` is fine. `[edi]` also works and adds an address-size prefix.
- `push` and `pop` take 64-bit registers in 64-bit mode.

The same source can work in both modes if you stick to `eax`, `ebx`, `ecx`, `edx`, `esi`, `edi` for values and use `lea` or `mov reg, label` for addresses.

## 4. The command

    cly <kernel> <file> [-type] [modes] [options]

Kernels: `linux`, `windows`, `mac`, `bare`.

Types:

| Type | What you get |
|------|--------------|
| `-bin` | Native program for the kernel (default) |
| `-elf` `-exe` `-macho` | Force that format |
| `-o` | Same object file as `-obj`, but the file is always named `.o`, also on windows. This is a type flag, not a file name. Use `-out FILE` to name the output |
| `-obj` | Object file: ELF for linux, mac and bare, COFF for windows (32 or 64-bit to match the code) |
| `-coff` | COFF object file |
| `-flat` | Raw bytes, nothing added |
| `-img` | Disk image |
| `-com` | DOS .com file |
| `-hex` `-srec` | Intel HEX or Motorola S-record |

Modes switch behaviours on and can be combined in any order after the type:

    cly linux prog.asm -bin -nasm
    cly linux prog.asm -elf -nolibc -noleak -m32
    cly linux prog.asm -bin -fix

| Mode | What it does |
|------|--------------|
| `-nasm` | Acts like NASM and nothing more. See section 16. |
| `-nolibc` | Leave out the built-in C library |
| `-noleak` | Skip the leak check |
| `-fix` | The leak-free copy becomes the main output |
| `-fixsrc` | Also write the source of the leak-free copy |
| `-O0` | Turn the optimizer off |
| `--dump-opt` | Print the source after optimizing, to stderr |

Options:

    -out FILE     name the output
    -I DIR        include folder
    -D NAME=VAL   define a macro
    -U NAME       undefine a macro
    -P FILE       include a file first
    -l [FILE]     write a listing
    -m32 -m64     force 32-bit or 64-bit code
    -cpu NAME     x86-64, arm64, risc-v, rv32, rv64, rv64gc, rv32imac ...
    -E            only run the preprocessor
    -w            hide warnings
    -q            hide everything but errors
    -v            version

## 5. Optimizer

Comprinly makes the program smaller and a bit faster on its own. It is on by default and keeps the behavior of your program the same. Turn it off with `-O0` when you want the code exactly as you wrote it.

What it does:

- `mov eax, 0` becomes `xor eax, eax` when nothing reads the flags afterwards
- `cmp reg, 0` becomes `test reg, reg`
- `add reg, 1` and `sub reg, 1` become `inc` and `dec` when the carry flag is not needed
- removes `add` or `sub` of 0 when the flags are not needed, shifts and rotates by 0, `lea r, [r]`, and `push r` right before `pop r`
- turns `mov r, -1` into the shorter `or r, -1` when the flags are not needed
- turns a `mov r, N` that repeats a value another register already holds into a register copy, and drops a `mov` or `xor` that sets a register to the value it already has
- turns `xor r64, r64` into `xor r32, r32`, and `add r, 128` into `sub r, -128` when the flags are not needed
- turns `mov r64, label` into `mov r32, label` in Linux 64-bit executables (the address always fits)
- removes a conditional jump to the next line, and turns a `jmp` to a `ret` into the `ret`
- removes `mov a, a`, a repeated `mov b, a` after `mov a, b`, and a load right after a store to the same place
- removes code that can never run (after `jmp` or `ret`, until the next label)
- removes a `jmp` to the next line, and turns `jcc over; jmp x; over:` into one inverted jump
- follows jumps that only lead to another jump
- turns `call f` followed by `ret` into `jmp f`
- puts tiny functions (up to 3 simple instructions) directly at the call
- drops functions that nothing refers to
- leaves out the section headers in Linux ELF files (`-O0` keeps them for debuggers)

### ARM64 and RISC-V

The same switch (`-O0`) controls a smaller set of cleanups on ARM64 and RISC-V:

- removes `mov x0, x0`, `mv a0, a0`, and adding or subtracting zero
- removes code that can never run, after an unconditional jump or `ret`
- removes a jump to the very next line
- sends jumps that land on another jump straight to the final target
- turns `b.eq over; b far; over:` into one inverted branch
- turns `cmp x0, 0` plus `b.eq` or `b.ne` into `cbz` or `cbnz` when the flags are not used afterwards
- drops a reload right after a store to the same stack slot, or turns it into a register move
- drops a constant load that is overwritten on the next line

Like on x86, raw outputs and files with `org` are left alone. Memory-mapped registers are never touched, only stack slots.

It does not touch raw outputs (`-flat`, `-com`, `-hex`, `-srec`, `-img`), 16-bit code, files with `org`, or code that uses `$` arithmetic inside instructions, because those depend on exact layout. Object files (`-obj`, `-coff`) keep every function so other files can link to them.

It does not move values between registers or rewrite your algorithm. You chose those registers, and an assembler cannot know what else relies on them.

## 6. What it does for you

- Sections are automatic. Put `db`, `resb` and code anywhere. Data, bss and code get sorted out.
- `start:` is the entry point. You do not need `global`.
- `syscall` sends the registers to the system you are running on exactly as they are: the number in `rax`, then `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`. Nothing is translated and no support code is added. You use the numbers of the target system, and you read whatever comes back in `rax`. On 64-bit Linux the numbers are the Linux ones, on macOS they are the BSD ones (for example 0x2000004 for write), and on bare metal there is no system call interface at all, so write your own.
- Quotes can be curly. Commas between operands can be missing.
- Names ignore case when only one spelling exists, so `START:`, `Msg` and `MSG` all find the same thing. The entry point can be `start`, `_start` or `START`, with or without a colon.
- `%include` ignores case in file names and accepts backslashes in paths, so Windows-style names work on Linux.
- In 64-bit code, 32-bit habits keep working: `push eax`, `pop ebx`, `call eax`, `mov ebp, esp`, `sub esp, 16`, `[esp+4]`, `[ebp-8]`, `pushad`, `popad` and `pushfd` are treated as their 64-bit versions.
- A line like `len equ $ - msg` counts every byte up to `$`. If your string ends in `, 0`, that zero is counted too, and Comprinly warns about it. Use `$ - msg - 1` for the text only.
- In 32-bit code the 64-bit register names (`rax`, `rdi`) work as their 32-bit halves. `r8` to `r15` live in memory.

## 7. Bare metal

    cly bare prog.cly
    qemu-system-x86_64 -drive format=raw,file=prog.bin

You get a boot sector, a switch to protected mode (or to long mode with `-m64`), and then your code. In 64-bit mode the first 1 GB is identity mapped. Output goes to the screen (VGA text), serial COM1 and port 0xE9. When you exit, QEMU, Bochs and VirtualBox power off. On real hardware the CPU halts.

There is no interrupt table and no system calls. Write to the screen (0xB8000), the serial port or port 0xE9 yourself.

## 8. Language notes

It follows NASM syntax: macros, `%define`, `%macro`, `%if`, `%rep`, `%include`, `times`, `struc`, `align`, local labels with a dot, `equ`, `incbin`, floating point data, and the instruction sets up to AVX-512 (including opmask registers, masking, broadcast, rounding, FP16), BMI, FMA and MPX. Forms that only exist in 64-bit mode need 64-bit code.

The `{vex}`, `{vex2}`, `{vex3}` and `{evex}` prefixes pick the encoding, the same as in NASM. A few newer VEX-only instructions need one of them.

Handy extras:

    alignmode generic        pick the padding style for align
    __cly_use                pull in a runtime piece by hand

## 9. Errors

Errors show the file, the line, the source text, and a caret. Typos in instructions get a "did you mean" hint. It stops after 25 errors. Warnings do not stop the build.

Common ones:

- `undefined symbol`: a label is used but never defined. The message points at where you used it.
- `unknown instruction`: usually a typo. It suggests the closest name.
- `invalid combination of opcode and operands`: that form does not exist. Check operand sizes, for example `mov [x], 5` needs `byte`, `word` or `dword` in front of the bracket.
- `output would overwrite the input file`: pick another name with `-out`.

## 10. Limits

- macOS output has only been tested in an emulator.
- ARM64 and RISC-V build for Linux and bare metal only. See sections 13 and 14 for what is missing.
- In 32-bit code, xmm8 to xmm15 and other 64-bit-only registers are rejected, as in NASM's 32-bit mode.
- AMX is not supported. XOP, FMA4 and TBM work.
- No Mach-O object output.
- A handful of rarely used forms NASM accepts are rejected, mostly old x87 shorthands such as `fadd` with no operands.
- The compiler itself runs as a 64-bit or 32-bit program, on Linux or Windows.

## 11. Examples

The `examples/` folder has `print`, `echo`, `count` and `fact`. They build in both 32-bit and 64-bit mode. Build them all:

    for k in linux windows mac bare; do cly $k examples/print.cly; done

## 12. Picking a CPU

Comprinly picks the CPU in this order:

1. `-cpu NAME` on the command line
2. a `cpu` line at the very top of the source file
3. a guess from your code: the first instruction or register that belongs to only one CPU decides
4. x86-64

The `cpu` line has to be the first line of the file. Comments and blank lines above it are fine.

    cpu arm64
    cpu x86-64
    cpu risc-v
    cpu rv32imac

Names that work:

| Family | Names |
|--------|-------|
| x86 | `x86-64`, `x86_64`, `amd64`, `x64`, `x86`, `i386`, `i686`, `ia32` |
| ARM64 | `arm64`, `aarch64`, `armv8`, `armv8-a`, `armv9-a`, `a64` and the numbered `armv8.x-a` names |
| RISC-V | `risc-v`, `riscv`, `rv32`, `rv64`, or a full name such as `rv64gc`, `rv32imac`, `rv64imafd_zba_zbb` |

Plain `risc-v` is 64-bit unless you pass `-m32`. An explicit `rv32` or `rv64` always wins over `-m32` and `-m64`. 32-bit ARM is not supported and says so.

With no `cpu` line and no flag, Comprinly reads down the file until it finds something that only one CPU has. A `w3` register, `ldp` or `b.eq` means ARM64. `a0`, `li`, `ecall` or `beqz` means RISC-V. `eax`, `rsp`, `jmp` and friends mean x86. Instructions that exist on several CPUs, like `mov` and `add`, are skipped until a register settles it. A bare `x0` with nothing else still leans ARM64. If there is no clue at all, you get x86-64.

Once the CPU is chosen, anything in the file that clearly belongs to a different CPU is an error:

    start:
        mov w0, 1
        mov eax, 2

    error: `eax' is an x86 register but the cpu is arm64 (chosen from `w0' on line 2; put a `cpu' line first to choose by hand)

The same goes for instructions, like `jmp` in an ARM64 file. If you really are mixing styles, say what you want with a `cpu` line.

Your code can check the choice with predefined macros: `__CLY_ARCH__`, `__ARCH_X86__`, `__ARCH_ARM64__`, `__ARCH_RISCV__`, `__ARCH_RISCV32__` and `__ARCH_RISCV64__`.

    %ifdef __ARCH_ARM64__
        mov x0, 0
    %endif

## 13. ARM64

    cpu arm64
    cly linux prog.cly

Output is 64-bit little endian. You can build `linux` programs and objects, and `bare` raw, hex and S-record files. PE and Mach-O output for ARM64 is not there yet.

### Syntax

Standard ARM64 assembly, with the forgiving bits turned on:

- `#` on immediates is optional. `mov x0, 5` and `mov x0, #5` match.
- Commas can be missing, case does not matter.
- `x0` to `x30`, `w0` to `w30`, `sp`, `wsp`, `xzr`, `wzr`, `lr` and `fp` all work. `v0` to `v31` with arrangements like `v0.16b`, `v1.4s`, and `q`, `d`, `s`, `h`, `b` scalar names also work.
- Condition codes work on branches (`b.eq`, `b.hi`), on `csel`, `cset` and friends, and `ccmp`.
- Shifts and extends are written the usual way: `add x0, x1, x2, lsl 3`, `add x0, x1, w2, uxtw 2`, `ldr x0, [x1, x2, lsl 3]`.
- Addressing: `[x0]`, `[x0, 8]`, `[x0, 8]!`, `[x0], 8`, `[x0, x1]`, and `[x0, w1, sxtw 2]`.
- `ldr x0, =value` and `ldr x0, =label` build the value or address for you.
- `mov x0, 0x123456789abcdef0` picks the shortest `movz`, `movk`, `movn` or `orr` sequence.
- `adr`, `adrp` and symbol loads such as `ldr x0, [label]` expand to the right pair.
- Numeric labels `1:` with `1b` and `1f` work.
- Branches that are too far are repaired. A short conditional branch becomes an inverted skip over a long `b`. `tbz` and `cbz` get the same treatment.
- `start:` is the entry point.

### What is covered

- Integer: arithmetic, logic, shifts, bitfields, move, multiply and divide, conditional select and compare, bit tricks (`clz`, `rbit`, `rev`), CRC32 and checksums.
- Loads and stores: all addressing modes, pairs, byte, half and word forms, signed loads, exclusives, acquire and release, prefetch, and non-temporal pairs.
- Atomics: LSE (`ldadd`, `swp`, `cas`, `casp` and friends, with `a`, `l`, `al` suffixes).
- Floating point: scalar single, double and half, conversions, fused multiply-add, rounding, `fmov` immediates.
- NEON: full integer and floating point vector set, permutes, table lookups, across-lane ops, element forms, shifts, widening and narrowing, structure loads and stores, dot product, bfloat, int8 matrix multiply, complex number ops, and the AES, SHA and SM3/SM4 crypto instructions.
- Extras: pointer authentication, memory tagging, BTI, barriers, flag manipulation, CSSC, RCpc, LS64, TME, `wfet` and `wfit`, `mrs` and `msr` with system register names.

Not there: SVE, SME, `ldclrp` style LSE128, and `rcwcas`.

### System calls

`svc 0` is the real Linux call. Put the number in `x8` and arguments in `x0` onward.

### Example

    cpu arm64
    msg db "Hello", 10
    start:
        mov x0, 1
        adr x1, msg
        mov x2, 6
        mov x8, 64
        svc 0
        mov x0, 0
        mov x8, 93
        svc 0

More in `examples/arm64/`.

## 14. RISC-V

    cpu risc-v
    cly linux prog.cly

Both 32-bit and 64-bit. The default feature set is I, M, A, F, D, C, Zicsr and Zifencei, plus Zba, Zbb, Zbs, Zbc, Zicond, Zfh, Zfhmin, the scalar crypto groups (Zbkb, Zbkx, Zknd, Zkne, Zknh, Zksed, Zksh), Zicbom, Zicboz, Zicbop, Zawrs, Svinval and H. Name a CPU like `rv64imafd` to narrow it. Leaving out `c` turns compression off, and leaving out an extension makes its instructions an error.

### Syntax

- ABI names (`a0`, `sp`, `ra`, `s0`, `fa0`) and raw names (`x10`, `f10`) both work.
- Pseudo instructions: `li`, `la`, `lla`, `lga`, `mv`, `not`, `neg`, `negw`, `sext.w`, `seqz`, `snez`, `sltz`, `sgtz`, `j`, `jr`, `ret`, `call`, `tail`, `nop`, all the `beqz` and `bgt` style branches, `fmv.s`, `fneg.d`, `fabs.s`, and the CSR helpers (`csrr`, `csrw`, `csrs`, `csrc`, `rdcycle`, `rdtime`, `frcsr`, ...).
- `li` picks the shortest sequence, using `bseti`, `add.uw` and shifts when the CPU has them.
- `la`, `call` and `tail` expand into the right instruction pair. Loads and stores straight from a symbol (`lw a0, counter`) work.
- `%pcrel_hi`, `%pcrel_lo`, `%hi` and `%lo` are accepted.
- CSR names work anywhere a CSR number does.
- Atomics take `.aq`, `.rl` and `.aqrl`. Floating point takes rounding modes (`rne`, `rtz`, `rdn`, `rup`, `rmm`, `dyn`).
- Numeric labels, far branch repair and `start:` work the same as on ARM64.

### Compression

With the C extension on and the optimizer on, Comprinly uses 16-bit encodings wherever it can. If you write `c.addi` or another `c.` mnemonic yourself, you get exactly that form. `-O0` stops automatic compression. Explicit `c.` instructions are still honoured.

### Linux and bare

Linux programs use `ecall` with the number in `a7`. `bare` gives raw, hex and S-record output. There is no Windows or macOS target for RISC-V.

Not there: the vector (V) extension and the Zcb `c.` forms.

More in `examples/riscv/`.

## 15. Objects and linking

`-obj` writes a relocatable ELF object. For ARM64 and RISC-V it is a standard RELA object, so you can link Comprinly output with LLVM or GNU objects using `ld.lld` or `ld`. Declare shared names with `global` and `extern`. The tests link Comprinly objects with LLVM-built ones both ways.

## 16. Strict NASM mode

`-nasm` turns every Comprinly extra off so the source is judged the way NASM would judge it:

- x86 only. `-cpu arm64`, `cpu riscv` and similar are errors, and the CPU is never guessed.
- No built-in library, no leak check, no optimizer and no syscall checks. `-fix` and `-fixsrc` are refused.
- Symbols are case-sensitive and undefined symbols are errors, also with `-obj`. Declare them with `extern`.
- Missing commas between operands are errors.
- No typo hints. A lone word on a line gives NASM's own orphan label warning.
- The entry point is `..start` or `_start`, nothing else.
- Raw outputs (`-flat`, `-hex`, `-srec`, `-img`) start in 16-bit mode like `nasm -f bin`, unless you say `bits` or `-m32`/`-m64`.

More of NASM's own behaviour is followed in this mode:

- `-obj` writes the same sections, symbols and relocations NASM does. Empty sections you never used are left out, `global name:function size`, `common` and the visibility words work, and `wrt ..plt`, `..got`, `..gotoff`, `..gotpc` and `..gottpoff` pick the matching relocation types.
- `mov r64, symbol` uses the 64-bit immediate form, and `dq symbol` gets a 64-bit relocation.
- Two labels on one line, a data list with a missing comma and `%comment` are errors.
- `align` pads code with `nop` bytes and numbers like `0x_ff` are read the way NASM reads them.

The tests compare accept and reject decisions and the generated code with a real NASM.

## 16b. Compile log

`-log` prints what the compiler does while it builds, with the time since the start:

    [ 0.000055] preprocess reading `lt.asm' (x86-64, 64-bit)
    [ 0.000222] preprocess include `x.inc' from lt.asm:1
    [ 0.005599] leakcheck  scanning 32-bit code
    [ 0.005855] libc       library code added, 20 lines now
    [ 0.005942] optimize   20 lines in, 20 lines out
    [ 0.006038] assemble   pass 1: 38440 bytes, layout changed
    [ 0.006854] write      lt, 200 bytes

It covers reading the file, includes and macros, the leak check, the library, the optimizer, every assembler pass, linking and writing. It never logs the running of the program. `-log=FILE` writes the log to a file instead of the screen. It works with every other mode.

## 16c. Warnings as errors and dependency rules

`-Werror` makes any warning fail the build, and the output file is removed. `-M` prints a make rule such as `prog: prog.asm defs.inc data.bin` to the screen once the build works. `-MD` writes the same rule to `<output>.d`. Both list every `%include` and `incbin` file.

## 17. NASM files and the built-in library

On Linux with x86 or x86-64 you can feed Comprinly a normal NASM file. `global main`, `extern printf`, `_start`, `wrt ..plt` and `wrt ..got` all work, and `main` can simply `ret` with its exit code. Functions like `printf`, `scanf`, `puts`, `malloc`, `free`, `strlen`, `memcpy`, `atoi`, `strtod`, `sscanf`, `qsort`, `rand`, `sin`, `pow`, `sqrt` and `exit` are supplied by Comprinly, and only the ones you use end up in the program. A `printf` or `puts` with plain text becomes a direct write, so small programs stay small. If you define a function with the same name yourself, yours wins. `-nolibc` turns all of this off.

## 18. Leak warnings

Comprinly watches `malloc`, `calloc`, `strdup`, `realloc`, anonymous `mmap` and your own functions that return such blocks. If a block can reach the end of the program or lose its last pointer without a `free`, you get a warning with the line numbers and the program still builds. A second file, `<name>_fixed`, is built with the missing `free` calls added. `-fix` makes it the main output, `-fixsrc` writes its source, `-noleak` skips the check. Pointers you store in memory or pass to functions Comprinly does not know are treated as handed off, so they are not reported.

## 19. Warnings you may see

- A word alone on a line is a label, like in NASM. If that word is one letter away from a real instruction (`retaa` typed as `retta`), you get a warning, because it was probably a typo.
- Dropped trailing zero in `$ - label`, see section 6.
- A number that does not fit in 64 bits is an error.
- Syscall checks: Comprinly follows the number you load into `rax`, `x8` or `a7` right before `syscall`, `svc` or `ecall` and warns if it is not a real Linux call number (only Linux numbers are checked, other systems get what you send). It also warns about `svc` with a non-zero number and `int 0x80` in 64-bit code. Names from `equ` work. It only looks inside one run of straight-line code, so jumps and labels reset what it knows.
- Branches that were repaired are silent. Use `-l` to see the final code.
