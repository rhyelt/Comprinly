# Comprinly 1.3.2

## -o makes a .o file

`cly linux prog.cly -o` writes `prog.o`, on every kernel. It is the same object file as `-obj`, but the name always ends in `.o`, even on Windows where `-obj` gives `.obj`. `-o` is a type flag and does not take a file name. To pick the name, use `-out`:

    cly linux prog.cly -o -out build/prog.o

## Smaller Mac programs

The last segment of a 64-bit Mach-O file is written at its real size instead of a full page. Programs without file data shrink from 4096 bytes to about 1100. This could only be checked with the 64-bit Mach-O emulator in the tests, not on a real Mac.

## Optimizer

`imul r, 1` disappears, `imul r, 2/4/8` becomes a shift, and `imul r, 3/5/9` becomes one `lea`. These only apply when no later instruction reads the flags.

## Better errors

- `-nasm`: `mov [x], 5` now says the operation size is missing and lists the size words.
- A name that is a defined name plus one or two stray characters gets a "did you mean" hint.

## Fixes

- Object files: `dd label_in_other_section - label_here` is written as a PC relative relocation, matching NASM byte for byte.
- Test scripts no longer use `-o` as a file name.
