# Benchmarks

Results are listed by version, starting at 1.3.2. Each version adds its own section.

## 1.3.5

Same commands as 1.3.3. The programs are hand written in assembly for NASM and Comprinly (the same source for both) and plain C for gcc, so the machine code differs from 1.3.3's programs and the sizes are not comparable with that section. Hello World writes the string with a `write` call and exits. The prime counter prints 25997 in all three.

### Hello World

| | Compile | Run | Size |
|---|---|---|---|
| C (gcc 13.3) | 39.1 ms | 2.8 ms | 15,960 bytes |
| NASM 2.16 + ld | 6.6 ms | **1.3 ms** | 4,696 bytes |
| Comprinly | **5.6 ms** | 1.4 ms | **164 bytes** |

### Count primes below 300,000

| | Compile | Run | Size |
|---|---|---|---|
| C (gcc 13.3) | 42.2 ms | 35.6 ms | 15,960 bytes |
| NASM 2.16 + ld | **6.5 ms** | **30.1 ms** | 4,920 bytes |
| Comprinly | 7.0 ms | 30.3 ms | **299 bytes** |

### Large inputs

A shared library with 12,000 functions (`-shared`) builds in 2.2 s and the result is 529,440 bytes. The same input took about 5 s before the quadratic symbol work was removed in this version.

### What it shows

- Size: 53 to 97 times smaller than gcc, and 16 to 29 times smaller than NASM plus `ld`.
- Run speed: the same as NASM within noise, since the machine code is the same.
- Compile speed: about 7 times faster than gcc, level with NASM plus `ld` while also linking and shrinking in one step.

## 1.3.3

Comprinly, C and NASM on the same two programs, Linux x86-64, using each tool's plain command with no extra flags:

| Tool | Command |
|------|---------|
| C | `gcc file.c -o file` |
| NASM | `nasm -f elf64 file.asm -o file.o && ld file.o -o file` |
| Comprinly | `./cly linux file.cly` |

The programs are Hello World, and a loop that counts the primes below 300,000 with trial division and prints the count (25997). All three versions print the same output. Times are the median of repeated runs on a 2 core machine, and the run time includes starting the program from a shell. Lower is better everywhere.

### Hello World

| | Compile | Run | Size |
|---|---|---|---|
| C (gcc 13.3) | 39.5 ms | 2.6 ms | 15,960 bytes |
| NASM 2.16 + ld | 8.3 ms | **1.6 ms** | 8,864 bytes |
| Comprinly | **5.6 ms** | 1.8 ms | **162 bytes** |

### Count primes below 300,000

| | Compile | Run | Size |
|---|---|---|---|
| C (gcc 13.3) | 43.0 ms | 51.3 ms | 15,960 bytes |
| NASM 2.16 + ld | 7.2 ms | **48.5 ms** | 5,032 bytes |
| Comprinly | **6.3 ms** | 49.3 ms | **361 bytes** |

### What it shows

- Size: Comprinly writes the executable itself, so there is no linker padding. Its programs are 44 to 98 times smaller than the C ones and 14 to 54 times smaller than NASM plus `ld`. A program that only moves values between registers and makes write and exit calls has no writable memory to protect, so its data shares the code segment and the file needs just one program header.
- Run speed: assembly written by hand runs the same in NASM and Comprinly (the small differences are noise from starting the program), since the machine code is the same. Plain `gcc` without `-O` is a little slower on the first program and about the same on the second.
- Compile speed: Comprinly is 7 times faster than gcc and a little faster than NASM plus `ld`, even though it also links, checks and shrinks the program in the same step.
- Behavior: the output is identical in all three.

## 1.3.2

Same commands and the same Hello World as in 1.3.3, with Comprinly 1.3.2.

### Hello World

| | Compile | Run | Size |
|---|---|---|---|
| C (gcc 13.3) | 35.5 ms | 2.3 ms | 15,960 bytes |
| NASM 2.16 + ld | 6.4 ms | 1.5 ms | 8,864 bytes |
| Comprinly 1.3.2 | 9.6 ms | 1.5 ms | **274 bytes** |

### Count primes below 300,000

| | Compile | Run | Size |
|---|---|---|---|
| C (gcc 13.3) | 38.6 ms | 50.2 ms | 15,960 bytes |
| NASM 2.16 + ld | **6.6 ms** | 48.4 ms | 5,032 bytes |
| Comprinly 1.3.2 | 10.7 ms | 48.4 ms | **361 bytes** |

Comprinly 1.3.2 was the smallest by size but slower to compile than NASM. In 1.3.3 the program shrank to 162 bytes and the compile time dropped to under NASM's.
