# Comprinly 1.4.0

This release is about size. There is a new `-strip` flag for the smallest output possible, and the other formats got smaller too.

## -strip

    cly linux hello.cly -strip --o hello

`-strip` prints nothing, writes no symbols, and picks the smallest layout it can. It also lets the optimizer rely on what Linux promises when a program starts: every register is zero except the stack pointer. So setup code like `mov eax, 0` or `mov edi, 1` gets shorter. The program still does exactly the same thing. Every test builds a program both ways, runs both and compares the output.

| Target | Normal | `-strip` |
|---|---|---|
| Linux x86-64 | 164 | 116 |
| Linux i386 | 129 | 78 |
| Linux ARM64 | 286 | 166 |
| Linux RISC-V | 286 | 166 |
| Windows x86-64 | 1,536 | 1,024 |

The x86-64 hello does not reach 80 bytes. The 14 byte message and the code do not fit in the places inside the headers that can hold them. The i386 one does.

## Tiny headers

x86-64 now puts the program header inside the ELF header and starts running inside it. i386 uses the 46 byte overlapped header. ARM64 and RISC-V use one segment with a short header. The compiler builds the tiny and the normal layout and keeps the smaller file. Windows programs use a single section.

## Faster

- `strlen`, `memcmp` and `memmove` in the built-in libc work a word at a time.
- ARM64 uses `adr` instead of two instructions when a label is near.
- Writing object files with many symbols is no longer slow.

## Tests

`tests/run_all.sh` has 14 more batches of 100. They cover `-strip` on every target, linking stripped objects with ld, the libc routines and damaged input.
