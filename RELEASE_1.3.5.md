# Comprinly 1.3.5

This release is about linking. Comprinly can now write shared libraries and Windows DLLs, link against them, and link against libraries built by other tools. I also went after bugs in the linker with a lot of test programs, and made some outputs smaller.

## Shared libraries and dynamic programs

`-shared` writes a Linux `.so`. `-dynamic` and `-needed` write a program that loads one. It works for x86-64, i386, ARM64 and RISC-V:

    cly linux lib.cly -shared --o libx.so
    cly linux main.cly -dynamic -needed ./libx.so --o main

You can also link objects: `cly a.o b.o -shared --ld libx.so` and `cly main.o -dynamic -needed libc.so.6 --ld main`. Libraries that gcc built work as inputs, and objects that gcc built with `-fPIC` link into a `.so` that gcc programs can use. Both directions are tested by running the programs.

The linker writes the dynamic section, symbol and string tables, hash table, relocations, PLT and GOT stubs, copy relocations for data, `NEEDED`, `SONAME`, `RUNPATH` and the init and fini arrays. It also writes the section headers that GNU ld wants when it links against your library. There is no lazy binding and no symbol versioning.

Sizes stay small. A library with a few functions is about 1.6 KB. A program that calls `puts` from libc is 888 bytes.

For position independent code, the `wrt ..got`, `..gotoff`, `..gotpc` and `..plt` forms match NASM's relocations. ARM64 got `:got:` and `:got_lo12:`, RISC-V got `%got_pcrel_hi`.

## Windows DLLs

`-dll` writes a DLL from source, and `--ld -dll` links COFF objects into one, for 32 and 64-bit x86. The export table is sorted for the loader, absolute addresses get base relocations, and the DLL can load anywhere. The tests load DLLs at different addresses in an emulator, call the exports and check that pointer tables were moved.

## Weak symbols and more linker features

The `weak` directive (and `global x:weak`, `extern x:weak`) is new. The linker resolves weak and common symbols across many objects, sorts `.init_array.NNNNN` by priority, and provides `__start_NAME`, `__stop_NAME` and the `__init_array_start` family. COMDAT groups from C++ work.

## Bugs fixed

- The i386 `R_386_GOTPC` addend was wrong, so 32-bit position independent code found the wrong GOT.
- `--ld` crashed on an object with no global symbols, and read past a very short file.
- `.init_array` priorities were ignored, so constructors ran in the wrong order.
- `..got` references to a global named the section in the object file instead of the symbol.
- On RISC-V, the low part of a `%got_pcrel_hi` pair now points at the GOT slot.
- A DLL with many exports, imports and a bss section had an oversized PE header that made the loader hang.
- The `weak` directive was not accepted by the assembler.
- Linking inputs with thousands of symbols was quadratic. A 12000 function library now builds in about 2 seconds instead of 5.

## Smaller output

A Linux program that loads its data with `lea reg, [rel label]` can use the single segment layout too (a Hello World is 164 bytes). A Windows program keeps its import data in the data section when it can, so a hello world went from 2048 to 1536 bytes. A DLL keeps its exports and relocations in the free space of the code page, which gives a 1536 byte DLL for a small source. New optimizer rules turn `lea r, [r+1]` into `inc r`, `and r, 0` into `xor`, `or r, 0` and `and r, -1` on 64-bit registers into `test`, and `mov r32, 0xFFFFFFFF` into `or r32, -1`.

## Tests

`tests/run_all.sh` has a third block of 16 batches of 100 on top of the two 500 sets: shared libraries (x86-64 run natively, i386, ARM64 and RISC-V in the emulator), DLLs, links of many objects, gcc in both directions, 200 random C programs linked statically and as libraries and compared with GNU ld, fuzzing of the dynamic linker, and optimizer behavior and size. Everything passes. I also ran the linker under AddressSanitizer with mutated inputs, which found the two crashes above.
