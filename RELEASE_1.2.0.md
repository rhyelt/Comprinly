# Comprinly 1.2.0

Comprinly now speaks three CPUs with the same easy syntax: x86-64 as before, plus ARM64 and RISC-V.

REMINDER: Comprinly is fully vibe-coded and for fun. Expect some bugs, and please report them.

## Pick a CPU

Put it on the first line:

    cpu arm64
    cpu x86-64
    cpu risc-v
    cpu rv32imac

Or use `-cpu arm64` on the command line. With neither, Comprinly looks at your registers and instructions and guesses, and falls back to x86-64.

## ARM64

The base instruction set, floating point, the full NEON set, crypto, atomics, pointer authentication, memory tagging and more. The `#` on numbers is optional, commas can be missing, and big constants, `ldr x0, =value`, symbol loads and numeric labels are handled for you. Branches that are too far get fixed automatically.

## RISC-V

32 and 64-bit with I, M, A, F, D, C, the bit manipulation sets, Zicond, half-precision float, scalar crypto and the cache and hypervisor extras. Pseudo instructions like `li`, `la`, `call` and `tail` pick the shortest code. Compressed instructions are used automatically.

## Output

Linux executables and objects for both. Objects link with LLVM and GNU objects. Bare gives flat, Intel HEX and S-record files.

## How it was tested

- Tens of thousands of instruction forms compared byte for byte with LLVM, for ARM64 and four RISC-V configurations
- Real programs run in an emulator
- Far branches, constant loading and linking tests
- The whole x86 suite again, still clean

## Not in this release

- SVE, SME, RISC-V vector, Zcb
- PE and Mach-O output for ARM64, and Windows or macOS for RISC-V
- 32-bit ARM

## Smarter guessing

If you do not say which CPU you mean, Comprinly follows the first instruction or register that only one CPU has. After that, anything in the file that clearly belongs to a different CPU is an error that points back at the line it based its choice on.

## Syscall checks

Comprinly watches the number you load before `syscall`, `svc` and `ecall`. It warns about numbers that are not Linux calls, calls the windows, mac and bare runtimes cannot handle, `svc` with a number, and `int 0x80` in 64-bit code.

## ARM64 and RISC-V optimizer

Dead code, jumps to the next line, jump chains, inverted branches, `cbz` and `cbnz`, and stack reloads are cleaned up. It is on by default and `-O0` turns it off. It was checked by running random programs both ways in an emulator.

## Better errors

- Numbers too big for 64 bits are an error, not a silent wrap
- Redefined symbols show where they were first defined
- Undefined symbols and mistyped instructions suggest the closest name, on every CPU
- Writing `jmp` in an ARM64 file (or the other way around) tells you which CPU it belongs to
- Wrong macro parameter counts say how many the macro takes
- An unclosed `%if` points at the line that opened it
- Out of range short jumps say how far away the target is

## Also new

- Warning when a lone word looks like a mistyped instruction
- Many more x86 CPU name aliases
- Rewritten README, HOWTOUSE and DOCUMENTATION
