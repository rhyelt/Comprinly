# Comprinly 1.3.1

A size and speed release. Nothing a program does has changed.

## Smaller compiler

The instruction tables, and the built-in C library are packed with a small LZ scheme and unpacked at start. The binaries shrank a lot:

- `cly` (Linux x86-64, static): 1.1 MB to 0.6 MB
- `cly32` (Linux x86): 860 KB to 570 KB
- `cly.exe` (Windows): 910 KB to 630 KB

Release builds use `-Os`, section garbage collection and no unwind tables. `make static` builds with your compiler, and `make cly64`, `make cly32` and `make cly.exe` use zig.

## Smaller programs

The optimizer found a few more safe savings:

- a `mov` of a value another register already holds becomes a register copy
- repeated zeroing and repeated `mov r, N` are dropped
- `mov r64, label` is a 5 byte move in Linux 64-bit executables
- a jump to the next line is gone and a jump to a `ret` is the `ret`
- Linux x86 executables skip unused ELF header space and default padding

## Faster

Big sources assemble much faster. A 200,000 line file went from 29 seconds to under 2. The `cly32` build starts about seven times faster than before.

## Fixes

- the compile log now shows real section sizes
- `-q` hides warnings as documented
- slow line searches on musl builds
- see `CHANGELOG.md` for the rest
