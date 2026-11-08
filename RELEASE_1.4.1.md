# Comprinly 1.4.1

A fix release for Windows. Someone tried `cly.exe` on NASM examples and found things that did not work. They are fixed here.

## Linking to C library DLLs

    cly -nasm -fwin64 hello.asm -needed msvcrt.dll --o hello.exe

Names your code leaves undefined (`extern printf`) are imported from the DLLs you list with `-needed`. If the DLL file is next to you, Comprinly reads its export table. If it is not (like `msvcrt.dll` on a Linux machine), C style names go to a C runtime DLL and Windows API names go to the others. In 32-bit code the `_` prefix and `@4` suffix are removed.

A program with no `start` label starts at `main`, `_main` or `WinMain`, so NASM examples that use `global main` link unchanged.

## Windows API names without -needed

Common names are known already. `extern ExitProcess`, `extern GetStdHandle`, `extern WriteFile` and about 150 other kernel32 names, plus common user32, gdi32, shell32, advapi32 and C runtime names, are imported from the right DLL by themselves. Names not on the list are matched by rules (`Reg*` to advapi32, `gl*` to opengl32, `WSA*` and socket calls to ws2_32, `Nt*` to ntdll, user32 words like `Window` and `Message`, and the usual kernel32 verbs), and the `A`, `W` and `Ex` forms follow their base name:

    cly windows hello.asm --o hello.exe

`-needed` is for names that are not on the list. A name that is on the list always goes to its own DLL.

## NASM style format option

`-f win64`, `-fwin64`, `-fwin32`, `-felf64`, `-felf32` and `-fmacho64` choose the kernel and the code size.

## Fixes

- `$name` is the same as `name`, even when the name looks like a keyword or register (`$eax:`, `jmp $dword`). A label written `$next:` was kept with the `$`, so `jmp $next` said `next` was undefined.
- Thousands of imports could crash the linker or take seconds. 30,000 imports now take 0.4 s.
- About 13 percent faster on large inputs.

## Tests

`tests/run_all.sh` has 6 more batches of 100 for the Windows work.
