# How to use Comprinly

Comprinly (`cly`) turns assembly source into a program you can run. You write NASM-style x86 code (32-bit or 64-bit), pick the system you want to run it on, and get a finished file. No assembler or linker needed.

## 1. Get it

You need a C compiler.

    make            build ./cly
    make static     small static build
    make cly32      32-bit Linux build of cly itself (needs python3 and ziglang)
    make cly.exe    32-bit Windows build of cly (needs python3 and ziglang)
    make check      run the tests
    make install    copy to /usr/local/bin

Without make: `gcc -O2 -fwrapv -o cly src/*.c`

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

    cly <kernel> <file> [-type] [options]

Kernels: `linux`, `windows`, `mac`, `bare`.

Types:

| Type | What you get |
|------|--------------|
| `-bin` | Native program for the kernel (default) |
| `-elf` `-exe` `-macho` | Force that format |
| `-obj` | Object file: ELF for linux, mac and bare, COFF for windows (32 or 64-bit to match the code) |
| `-coff` | COFF object file |
| `-flat` | Raw bytes, nothing added |
| `-img` | Disk image |
| `-com` | DOS .com file |
| `-hex` `-srec` | Intel HEX or Motorola S-record |

Options:

    -out FILE     name the output
    -I DIR        include folder
    -D NAME=VAL   define a macro
    -U NAME       undefine a macro
    -P FILE       include a file first
    -l [FILE]     write a listing
    -m32 -m64     force 32-bit or 64-bit code
    -O0           turn the optimizer off
    -E            only run the preprocessor
    -w            hide warnings
    -q            quiet
    -v            version

## 5. Optimizer

Comprinly makes the program smaller and a bit faster on its own. It is on by default and keeps the behavior of your program the same. Turn it off with `-O0` when you want the code exactly as you wrote it.

What it does:

- `mov eax, 0` becomes `xor eax, eax` when nothing reads the flags afterwards
- `cmp reg, 0` becomes `test reg, reg`
- `add reg, 1` and `sub reg, 1` become `inc` and `dec` when the carry flag is not needed
- removes `mov a, a`, a repeated `mov b, a` after `mov a, b`, and a load right after a store to the same place
- removes code that can never run (after `jmp` or `ret`, until the next label)
- removes a `jmp` to the next line, and turns `jcc over; jmp x; over:` into one inverted jump
- follows jumps that only lead to another jump
- turns `call f` followed by `ret` into `jmp f`
- puts tiny functions (up to 3 simple instructions) directly at the call
- drops functions that nothing refers to
- leaves out the section headers in Linux ELF files (`-O0` keeps them for debuggers)

It does not touch raw outputs (`-flat`, `-com`, `-hex`, `-srec`, `-img`), 16-bit code, files with `org`, or code that uses `$` arithmetic inside instructions, because those depend on exact layout. Object files (`-obj`, `-coff`) keep every function so other files can link to them.

It does not move values between registers or rewrite your algorithm. You chose those registers, and an assembler cannot know what else relies on them.

## 6. What it does for you

- Sections are automatic. Put `db`, `resb` and code anywhere. Data, bss and code get sorted out.
- `start:` is the entry point. You do not need `global`.
- `syscall` uses Linux x86-64 numbers and registers (`rax`, then `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`) on every system. On 64-bit Linux it is the real instruction. Everywhere else a small runtime translates the call. It is only added when you use `syscall`.
- Quotes can be curly. Commas between operands can be missing.
- Names ignore case when only one spelling exists, so `START:`, `Msg` and `MSG` all find the same thing. The entry point can be `start`, `_start` or `START`, with or without a colon.
- `%include` ignores case in file names and accepts backslashes in paths, so Windows-style names work on Linux.
- In 64-bit code, 32-bit habits keep working: `push eax`, `pop ebx`, `call eax`, `mov ebp, esp`, `sub esp, 16`, `[esp+4]`, `[ebp-8]`, `pushad`, `popad` and `pushfd` are treated as their 64-bit versions.
- A line like `len equ $ - msg` counts every byte up to `$`. If your string ends in `, 0`, that zero is counted too, and Comprinly warns about it. Use `$ - msg - 1` for the text only.
- In 32-bit code the 64-bit register names (`rax`, `rdi`) work as their 32-bit halves. `r8` to `r15` live in memory.

Common calls like read, write, open, close and exit work everywhere. Anything the target system cannot do returns -38 (ENOSYS). The runtime sources in `rt/` show exactly what is mapped.

## 7. Bare metal

    cly bare prog.cly
    qemu-system-x86_64 -drive format=raw,file=prog.bin

You get a boot sector, a switch to protected mode (or to long mode with `-m64`), and then your code. In 64-bit mode the first 1 GB is identity mapped. Output goes to the screen (VGA text), serial COM1 and port 0xE9. When you exit, QEMU, Bochs and VirtualBox power off. On real hardware the CPU halts.

There is no interrupt table. Only the basic syscalls work.

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
- In 32-bit code, xmm8 to xmm15 and other 64-bit-only registers are rejected, as in NASM's 32-bit mode.
- AMX is not supported. XOP, FMA4 and TBM work.
- No Mach-O object output.
- A handful of rarely used forms NASM accepts are rejected, mostly old x87 shorthands such as `fadd` with no operands.
- The 64-bit runtimes for Windows, macOS and bare metal were tested in emulators only. Native Linux 32-bit and 64-bit programs were run for real.
- The compiler itself runs as a 64-bit or 32-bit program, on Linux or Windows.
- The bare metal runtime is small on purpose.

## 11. Examples

The `examples/` folder has `print`, `echo`, `count` and `fact`. They build in both 32-bit and 64-bit mode. Build them all:

    for k in linux windows mac bare; do cly $k examples/print.cly; done
