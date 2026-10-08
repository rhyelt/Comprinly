# Comprinly 1.1.0

This update makes your programs smaller, makes 64-bit code friendlier to old 32-bit habits, and fixes a handful of annoying edge cases. It also adds a proper preprocessor reference.

REMINDER: Comprinly is fully vibe-coded and for fun. Expect some bugs, and please report them.

## The optimizer

Comprinly now tidies your code on its own. It is on by default and keeps what your program does exactly the same. If you want your code untouched, build with `-O0`.

What it does:

- Turns `mov eax, 0` into `xor eax, eax`, but only when nothing reads the flags afterwards
- Turns `cmp reg, 0` into `test reg, reg`
- Turns `add reg, 1` and `sub reg, 1` into `inc` and `dec` when the carry flag is not needed
- Removes pointless moves, like `mov rax, rax`, or a load right after a store to the same place
- Removes code that can never run, such as lines right after a `jmp` or `ret`
- Removes a `jmp` to the very next line, and merges `jcc over / jmp x / over:` into one jump
- Follows jumps that only lead to another jump
- Turns `call f` followed by `ret` into `jmp f`
- Puts tiny functions (up to 3 simple instructions) straight into the place that calls them
- Drops functions that nothing uses
- Leaves the section headers out of Linux ELF files, so a 64-bit hello world went from 872 bytes to 290

It stays away from raw outputs (`-flat`, `-com`, `-hex`, `-srec`, `-img`), 16-bit code, files with `org`, and code that does `$` math inside instructions, since those depend on exact layout. It does not rearrange registers or rewrite your algorithm.

I tested it by building thousands of random 32-bit and 64-bit programs, running each with and without the optimizer in an emulator, and comparing every register and memory byte. No differences. The same programs came out about 8 to 10 percent smaller.

## Friendlier 64-bit code

Code is 64-bit by default now, and old 32-bit habits keep working:

- `push eax`, `pop ebx`, `call eax` and `jmp eax` are treated as their 64-bit versions
- `mov ebp, esp`, `sub esp, 16`, `[esp+4]` and `[ebp-8]` use the full stack registers, so they no longer crash
- `pushad`, `popad`, `pushfd` and `popfd` work
- A 32-bit style calculator with `int 0x80` builds and runs correctly in both modes

## Entry point and names

- `start`, `_start` and `START` all work as the entry point, with or without a colon
- Symbol names fall back to a case-insensitive match when there is exactly one, so `Msg` finds `MSG`
- `%include` ignores case in file names and understands backslashes, so Windows-style paths work on Linux

## Warnings and fixes

- New warning when `len equ $ - msg` also counts a trailing `, 0` in your string
- Fixed: `cmov` with 64-bit registers (`cmovl rax, rbx` and friends) was being rejected in 64-bit mode
- Fixed: `__BITS__` and `__OUTPUT_FORMAT__` reported 32-bit for default 64-bit builds
- Fixed: the `__?NAME?__` spellings now work for every predefined macro
- Fixed: `%arg` and `%local` use `rbp` in 64-bit code

## Docs and tests

- New `DOCUMENTATION.md` covering macros, `%assign`, `%xdefine`, `%rotate`, conditionals, the context stack, `%arg` and `%local`, messages, `%line`, environment variables and predefined macros. It also lists what is not supported
- Updated `README.md` and `HOWTOUSE.md`, plus a `CHANGELOG.md`
- New tests for the optimizer, including two fuzzers that compare optimized and unoptimized programs in an emulator

## New options

- `-O0` turns the optimizer off
- `-m32` and `-m64` force the code size (these came in with 1.0.0 but are worth a mention)

## Still not here

- APX, OMF output, dependency files, debug info (DWARF, STABS) and a disassembler
- Windows, macOS and bare metal output has only been run in an emulator
- Old decimal helpers like `aam` and `daa` do not exist in 64-bit mode. Use `-m32` or a `div` loop

Thanks to everyone who tried it and sent feedback.
