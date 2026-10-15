# Comprinly 1.3.0

Comprinly began as an experiment and a proof of concept, and it was vibe-coded all the way. It has become a serious project, and it now accepts collaborators. From 1.3.0 it is partly vibe-coded and partly written by hand by me and, hopefully, by others. Expect some bugs, and please report them.

## NASM files just work

Drop a normal NASM file for Linux x86 or x86-64 into Comprinly and it builds and runs. `global`, `extern`, `main` with a C style return, `_start`, `wrt ..plt`, `wrt ..got` and `@PLT` are all fine. If a program calls `printf`, `scanf`, `malloc`, `strlen` and friends, Comprinly supplies them. Nothing is linked from outside.

Only the functions a program really uses are added. Calls to `printf`, `puts`, `putchar` and `exit` with constant text become a plain write or exit. A hello world in NASM comes out close to the size of the same program written for Comprinly.

Functions available: printf family (with `%e` and `%g`), scanf and sscanf, strtod and atof, qsort, math (sin, cos, tan, asin, acos, atan, atan2, exp, log, log10, log2, pow, fmod, hypot, round, trunc, fmin, fmax, sqrt, floor, ceil), puts, putchar, fputs, fputc, fwrite, fflush, getchar, fgets, malloc, calloc, realloc, free, string and memory functions, atoi, strtol, ctype helpers, rand, time, sleep, sqrt, floor, ceil, exit and abort.

## Leak warnings

Comprinly now follows allocations through your code. If a block can be lost, you get a warning with the line numbers, for example:

    warning: potential memory leak: the block from `malloc' is lost when its pointer is overwritten (line 26)

The build still finishes. Next to it you get `<name>_fixed`, a copy with the missing `free` calls added in the right places. Use `-fix` to make that copy the main output, `-fixsrc` to get its source, or `-noleak` to turn the check off.

## Strict mode

`-nasm` makes Comprinly behave like NASM: x86 only, case-sensitive symbols, undefined symbols are errors, no missing commas, no typo hints, no library, no leak check and no optimizer. Modes combine in any order: `cly linux prog.asm -bin -nasm`.

## Compile log

`-log` prints a timestamped trace of the build: preprocessing, includes, macros, library, optimizer, each assembler pass, link and write. `-log=FILE` sends it to a file. It only covers compiling, never the running.

## Closer to NASM

`-nasm` now matches NASM's object files (sections, symbols, relocation types, `common`, `global` attributes) and rejects more of what NASM rejects. A compare suite in `tests/compat` checks it against a real NASM.

## Faster

Big sources assemble much faster. A 200,000 line file dropped from 29 seconds to 4.

## Make friendly

`-Werror` turns warnings into errors. `-M` and `-MD` give a make rule with every included file.

## Smarter optimizer

A few more safe size savings: no-op `add`, `sub`, shifts, `lea` and `push`/`pop` pairs are dropped, and `mov r, -1` becomes `or r, -1` when the flags are not needed. `--dump-opt` shows what the optimizer produced.

## Options

- `-nolibc` leaves the built-in library out.
- `-noleak` skips the leak check.
- `-fix` and `-fixsrc` as above.
- `-log` and `-log=FILE` for the compile log.

## Limits

The library and the leak check are for Linux executables on x86 and x86-64. Pointers stored in memory or passed to unknown functions are treated as handed off, so no warning is given for them. `strtod` reads decimal numbers only (no hex floats, `inf` or `nan`), and `%a` is not supported.
