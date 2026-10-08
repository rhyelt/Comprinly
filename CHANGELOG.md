# Changelog

## 1.1.0

- New: optimizer, on by default, `-O0` turns it off. See HOWTOUSE.md section 4.
- New: smaller Linux ELF files (no section headers unless `-O0`).
- New: `START`, `_start` and `start` (with or without a colon) all work as the entry point.
- New: symbol names fall back to a case-insensitive match when there is exactly one.
- New: `%include` ignores case in file names and accepts backslashes.
- New: 64-bit code accepts `push eax`, `pop ebx`, `call eax`, `mov ebp, esp`, `sub esp, 16`, `[esp+4]`, `[ebp-8]`, `pushad`, `popad`, `pushfd` and `popfd`.
- New: warning when `$ - label` counts a trailing 0 byte.
- Fixed: `cmovcc` with 64-bit registers was rejected in 64-bit mode.
- Fixed: `__BITS__` and `__OUTPUT_FORMAT__` now match the real target (64-bit by default), the `__?NAME?__` forms work for every predefined macro, and `%arg`/`%local` use `rbp` in 64-bit code.
- New: DOCUMENTATION.md, a preprocessor and directive reference.
- Tests: optimizer fuzzers for 32-bit and 64-bit code that compare results in an emulator.

## 1.0.0

First release.
