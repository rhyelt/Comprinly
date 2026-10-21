# Benchmarks

Benchmarks of versions.

# 1.3.2

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
