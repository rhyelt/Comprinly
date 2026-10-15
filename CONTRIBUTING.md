# Contributing

Comprinly is open to collaborators. Bug reports, fixes, new instruction forms, tests and docs are all welcome.

## Build and test

    make
    sh tests/run_all.sh

The test run skips what your machine does not have. Real NASM, llvm-mc and unicorn unlock the comparison and emulator tests.

## Where things are

The source is in `src/`. `src/core` holds the preprocessor, expressions and the assembler core. Each CPU has its own folder, and `src/format` has the executable and object writers. See the source layout in `DOCUMENTATION.md`.

Some files are generated. Edit the template or script, not the output:

- `rt/gen.py` writes `src/runtime/rt_data.c`
- `rt/gen_libc.py` writes `src/extras/libc_data.c` from `rt/libc.tpl` and `rt/libc_fmt.tpl`
- `tools/gen_insns.py` and `tools/gen_insns64.py` write the x86 tables

## Ground rules

- No comments in the C source.
- Keep it small. A new feature should not slow down or bloat the rest.
- Add a test with each fix. NASM behaviour is checked in `tests/compat`, optimizer changes in `tests/run_opt.sh` and the fuzzers.
- The optimizer must never change what a program does.

## Pull requests

Keep them focused, say what changed and why, and run `sh tests/run_all.sh` first.
