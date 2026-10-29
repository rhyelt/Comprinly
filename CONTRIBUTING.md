# Contributing

Comprinly is open to collaborators. Bug reports, fixes, new instruction forms, tests and docs are all welcome.

## Build and test

    make
    sh tests/run_all.sh

The test run skips what your machine does not have. Real NASM, llvm-mc and unicorn unlock the comparison and emulator tests.

`run_all.sh` ends with 500 checks in 10 batches of 50 (the block named `500 check batches`). They build random programs and link them with `--ld`, run the result (natively for x86 ELF, in the emulator for PE, ARM64 and RISC-V), compare object formats and random instructions with NASM, test `-global` and `-update` in a throwaway folder, and feed mutated files to the assembler and the linker. A second block (`500 mature batches`) has 5 batches of 100 that test other things: expressions and layout against a model, macros against NASM, assembly linked into C with gcc, ARM64 and RISC-V encodings against llvm-mc, and objdump checks. A third block (`linkage and size batches`) has 16 batches of 100 for shared libraries and DLLs (x86-64 run natively, everything else in the emulators), links of many objects with weak and common symbols, gcc libraries used from Comprinly programs and the other way around, random C programs linked with Comprinly and with `ld` and compared, damaged inputs to the dynamic linker, and optimizer behavior and size. Each batch is skipped if its tool is missing. To go harder, set `BATCH_N` (size of the fuzz and NASM batches, and of each batch in the second block), `BATCH_SEED` (different random data), `BATCH_ONLY` (part of a batch name) and `CLY` (another build, for example one built with `-fsanitize=address,undefined`).

## Where things are

The source is in `src/`. `src/core` holds the preprocessor, expressions and the assembler core. Each CPU has its own folder, and `src/format` has the executable and object writers. See the source layout in `DOCUMENTATION.md`.

Some files are generated. Edit the template or script, not the output:

- `rt/gen_libc.py` writes `src/extras/libc_data.c` from `rt/libc.tpl` and `rt/libc_fmt.tpl`
- `tools/gen_insns.py` and `tools/gen_insns64.py` write the x86 tables
- `tools/lz.py` is the packer the generators share. The generated files hold compressed data, so rerun the generator (`make gen`, `make rt`) instead of editing them

## Ground rules

- No comments in the C source.
- Keep it small. A new feature should not slow down or bloat the rest.
- Add a test with each fix. NASM behaviour is checked in `tests/compat`, optimizer changes in `tests/run_opt.sh` and the fuzzers.
- The optimizer must never change what a program does.

## Pull requests

Keep them focused, say what changed and why, and run `sh tests/run_all.sh` first.
