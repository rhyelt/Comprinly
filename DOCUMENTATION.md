# Componly preprocessor and language reference

This file covers the preprocessor and the directives around it. For installing and running Componly, see HOWTOUSE.md. The syntax follows NASM, so NASM examples carry over.

Everything listed here has been run and checked. Things Componly does not do are at the end.

## Macros

Multi-line macros take a name and a parameter count:

    %macro load 2
        mov %1, %2
    %endmacro

    load eax, 5

- `%1`, `%2`, ... are the parameters. `%0` is how many were passed.
- A range such as `%macro pad 1-3` or `%macro all 1-*` allows a variable number of parameters.
- `%%name` makes a label that is unique to each use of the macro.
- Macros can call other macros, and a macro can define another macro. The inner one exists after the outer one has run.
- `%rep N` ... `%endrep` repeats a block.

### %rotate

`%rotate n` shifts the parameters left by `n`, or right if `n` is negative. It is the way to walk through a long parameter list:

    %macro emit 1-*
    %rep %0
        db %1
    %rotate 1
    %endrep
    %endmacro

    emit 1, 2, 3

## Defining names

| Directive | What it does |
|-----------|--------------|
| `%define A 1` | Case-sensitive. The body is expanded when `A` is used. |
| `%idefine A 1` | Same, but `A` and `a` match the same name. |
| `%xdefine B A+1` | The body is expanded right now, when you define it. |
| `%assign n n+1` | A number. The expression is calculated once and can be changed later. |
| `%defstr S abc` | Makes the string `"abc"`. |
| `%deftok T 5` | Turns a string into tokens. |
| `%strcat C "a", "b"` | Joins strings. |
| `%undef A` | Removes a name. |
| `%defalias Z 9` | Makes a name that points at another. |

The difference between `%define` and `%xdefine`:

    %define A 1
    %xdefine B A+1
    %define A 9
    mov eax, B        ; becomes mov eax, 1+1

## Strings

    %strlen n "hello"        ; n is 5
    %substr s "hello" 2, 3   ; s is "ell"

`%substr` counts from 1. The first number is the start and the second is the length.

## Conditionals

`%if`, `%elif`, `%else` and `%endif` work on a number expression. These checks are available as well, and each one has a negated form with `n` after `if` (for example `%ifnnum`):

| Directive | True when |
|-----------|-----------|
| `%ifdef X` | `X` is defined |
| `%ifmacro m` | a macro named `m` exists |
| `%ifid x` | `x` is an identifier |
| `%ifnum x` | `x` is a number |
| `%ifstr x` | `x` is a string |
| `%iftoken x` | `x` is a single token |
| `%ifempty` | nothing follows |
| `%ifenv NAME` | the environment variable `NAME` exists |
| `%ifctx name` | the top context is `name` |
| `%ifidn a, b` | `a` and `b` are the same text |
| `%ifidni a, b` | same, ignoring case |

    %ifidn %1, eax
        xor %1, %1
    %endif

`%ifnidn` and `%ifnidni` are the negated comparisons.

## Context stack

A context is a named scope for macros. Names that start with `%$` belong to the top context and disappear when it is popped.

    %push loopctx
    %define %$count 5
        mov ecx, %$count
    %pop

- `%push name` opens a context.
- `%pop` closes it.
- `%repl name` renames the top context.
- `%ifctx name` and `%ifnctx name` test it.

### %arg, %local and %stacksize

These name stack variables inside a context. `%stacksize` picks the frame style:

| Mode | Base register | Slot size |
|------|---------------|-----------|
| `flat` | `ebp` | 4 |
| `flat64` | `rbp` | 8 |
| `small`, `large` | `bp` | 2 |

If you do not set one, Componly uses `flat` in 32-bit code and `flat64` in 64-bit code.

    bits 64
    f:
    %push c
    %stacksize flat64
    %assign %$localsize 0
    %arg a:qword, b:dword
    %local t:qword
        push rbp
        mov rbp, rsp
        sub rsp, %$localsize
        mov rax, [a]        ; [rbp+16]
        mov [t], rax        ; [rbp-8]
        leave
        ret
    %pop

Use the names inside square brackets. `%assign %$localsize 0` has to come before `%local` so Componly can add up the space.

## Messages

    %warning this is not finished
    %error  this is wrong
    %fatal  cannot go on

- `%warning` prints a warning and keeps going.
- `%error` prints an error and the build fails at the end.
- `%fatal` stops right away.

## Other directives

- `%line 100+1 other.asm` makes later messages report that file and line number. Useful for generated code.
- `%pragma` is accepted and ignored, so NASM files that contain it still build.
- `%include "file"` pastes a file in. See HOWTOUSE.md for how the names are found.

## Environment variables

`%!NAME` becomes the text of the environment variable, as a string:

    db %!HOME

## Predefined macros

| Name | Value |
|------|-------|
| `__FILE__` | current file name, as a string |
| `__LINE__` | current line number |
| `__BITS__` | 16, 32 or 64 (follows `-m32`, `-m64` and the default for your target) |
| `__SECT__` | the current section directive |
| `__OUTPUT_FORMAT__` | `elf64`, `elf32`, `win64`, `win32`, `macho64`, `macho32` or `bin` |
| `__NASM_MAJOR__`, `__NASM_MINOR__`, `__NASM_SUBMINOR__`, `__NASM_PATCHLEVEL__` | the NASM version Componly matches (2.16.01) |
| `__NASM_VERSION_ID__`, `__NASM_VER__` | the same version as a number and as a string |
| `__CLY__`, `__CLY_MAJOR__`, `__CLY_MINOR__`, `__CLY_VERSION__` | the Componly version |
| `__DATE__`, `__TIME__`, `__DATE_NUM__`, `__TIME_NUM__` | build date and time (UTC) |
| `__UTC_DATE__`, `__UTC_TIME__`, `__UTC_DATE_TIME__`, `__POSIX_TIME__` | the same in other forms |
| `__PASS__` | always 3 |

Every one of these also works with question marks around the name, for example `__?FILE?__` and `__?NASM_MAJOR?__`.

You can add your own on the command line with `-D NAME=value` and remove one with `-U NAME`. `-P file` pastes a file before everything else. `-E` prints the result of the preprocessor and stops.

## Not supported

These are not in Componly, so they are not documented above:

- APX (the extended general registers `r16` to `r31` and the new instruction forms)
- OMF and `.obj` output for 16-bit tools (`-coff` and `-obj` make COFF and ELF)
- dependency files (`-M` style output)
- debug information of any kind: DWARF, STABS, and the `-g` option
- debugger integration
- a disassembler like NDISASM
- `%note` (NASM does not have it either)
