<p align="center">
  <img src="images/banner.png" alt="Comprinly: simplicity, productivity, portability and agility" width="640">
</p>

# Comprinly

Comprinly (`cly`) is a small assembler written in plain C. You write assembly in a relaxed NASM style, tell it which system you are building for, and it hands you a finished program. There is no separate assembler or linker to set up.

It reads x86, ARM64 and RISC-V from the same syntax. The commands you type do not change when the CPU does.

REMINDER: This project is fully vibe-coded, so you may find some bugs. It is purely for recreational purposes.

## Get it

Prebuilt binaries are on the [Releases page](https://github.com/rhyelt/Componly/releases). Or build it yourself. You only need a C compiler and make:

    make

That gives you a `cly` file in the same folder. `make install` copies it to `/usr/local/bin`.

## Your first program

Save this as `hello.cly`:

    msg db "Hello, World!", 10

    start:
        mov rax, 1
        mov rdi, 1
        mov rsi, msg
        mov rdx, $ - msg
        syscall

        mov rax, 60
        mov rdi, 0
        syscall

Build and run it:

    cly linux hello.cly
    ./hello

The finished file is written next to the source file.

## The command

    cly <system> <file> [options]

The system is one of `linux`, `windows`, `mac` or `bare`. A few examples:

    cly linux hello.cly
    cly windows hello.cly
    cly bare hello.cly -hex
    cly linux hello.cly -O0
    cly linux hello.cly -obj

Run `cly -h` to see every option.

## Choosing a CPU

Comprinly decides the CPU in this order:

1. The `-cpu` option, for example `cly linux hello.cly -cpu arm64`
2. A `cpu` line at the very top of the file
3. The first instruction or register in your code that belongs to only one CPU
4. x86-64, if nothing gives it away

The `cpu` line looks like this:

    cpu arm64
    cpu x86-64
    cpu risc-v

If your code mixes CPUs, say `mov eax, 1` in a file that started out as ARM64, Comprinly stops and tells you which line it based its choice on.

## When something goes wrong

Errors name the file, the line, the code and what is wrong, and often suggest a fix. Warnings do not stop the build. `-w` hides them and `-q` hides everything but errors.

## Where to go next

- `HOWTOUSE.md` walks through the whole compiler, one topic at a time.
- `DOCUMENTATION.md` is the reference for directives, the preprocessor and each CPU.
- `examples/` has small programs to try.
- `CHANGELOG.md` lists what changed in each version.

## Tests

    make check

runs everything it has tools for and skips the rest.

## Licence

AGPL-3.0. The NASM instruction table keeps its own BSD notice.
