bits 64
default rel
import __imp_GetStdHandle kernel32.dll GetStdHandle
import __imp_WriteFile kernel32.dll WriteFile
import __imp_ReadFile kernel32.dll ReadFile
import __imp_CreateFileA kernel32.dll CreateFileA
import __imp_CloseHandle kernel32.dll CloseHandle
import __imp_SetFilePointer kernel32.dll SetFilePointer
import __imp_VirtualAlloc kernel32.dll VirtualAlloc
import __imp_VirtualFree kernel32.dll VirtualFree
import __imp_ExitProcess kernel32.dll ExitProcess
import __imp_GetCurrentProcessId kernel32.dll GetCurrentProcessId
import __imp_Sleep kernel32.dll Sleep
import __imp_GetSystemTimeAsFileTime kernel32.dll GetSystemTimeAsFileTime
import __imp_DeleteFileA kernel32.dll DeleteFileA
import __imp_CreateDirectoryA kernel32.dll CreateDirectoryA
import __imp_RemoveDirectoryA kernel32.dll RemoveDirectoryA
import __imp_GetCurrentDirectoryA kernel32.dll GetCurrentDirectoryA
import __imp_SetCurrentDirectoryA kernel32.dll SetCurrentDirectoryA
section .data
align 8
__cly_brk dq 0
__cly_brkbase dq 0
__cly_brkcom dq 0
section .text
__cly_h:
    cmp edi, 2
    ja .raw
    mov ecx, -10
    sub ecx, edi
    sub rsp, 40
    call [__imp_GetStdHandle]
    add rsp, 40
    ret
.raw:
    mov rax, rdi
    ret
__cly_ft:
    sub rsp, 56
    lea rcx, [rsp+32]
    call [__imp_GetSystemTimeAsFileTime]
    mov rax, [rsp+32]
    add rsp, 56
    mov rcx, 116444736000000000
    sub rax, rcx
    xor edx, edx
    mov rcx, 10000000
    div rcx
    ret
__cly_syscall:
    push rbx
    push rbp
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    mov rbp, rsp
    and rsp, -16
    sub rsp, 96
    cmp eax, 0
    je .read
    cmp eax, 1
    je .write
    cmp eax, 2
    je .open
    cmp eax, 3
    je .close
    cmp eax, 8
    je .lseek
    cmp eax, 9
    je .mmap
    cmp eax, 11
    je .munmap
    cmp eax, 12
    je .brk
    cmp eax, 16
    je .notty
    cmp eax, 24
    je .yield
    cmp eax, 35
    je .nanosleep
    cmp eax, 39
    je .getpid
    cmp eax, 60
    je .exit
    cmp eax, 79
    je .getcwd
    cmp eax, 80
    je .chdir
    cmp eax, 83
    je .mkdir
    cmp eax, 84
    je .rmdir
    cmp eax, 87
    je .unlink
    cmp eax, 96
    je .gtod
    cmp eax, 102
    je .zero
    cmp eax, 104
    je .zero
    cmp eax, 107
    je .zero
    cmp eax, 108
    je .zero
    cmp eax, 201
    je .time
    cmp eax, 228
    je .cgt
    cmp eax, 231
    je .exit
    mov rax, -38
    jmp .out
.zero:
    xor eax, eax
    jmp .out
.notty:
    mov rax, -25
    jmp .out
.eio:
    mov rax, -5
    jmp .out
.enoent:
    mov rax, -2
    jmp .out
.read:
    mov rbx, rdx
    call __cly_h
    mov rcx, rax
    mov rdx, rsi
    mov r8, rbx
    lea r9, [rsp+64]
    mov qword [rsp+32], 0
    call [__imp_ReadFile]
    test eax, eax
    jz .eio
    mov eax, [rsp+64]
    jmp .out
.write:
    mov rbx, rdx
    call __cly_h
    mov rcx, rax
    mov rdx, rsi
    mov r8, rbx
    lea r9, [rsp+64]
    mov qword [rsp+32], 0
    call [__imp_WriteFile]
    test eax, eax
    jz .eio
    mov eax, [rsp+64]
    jmp .out
.open:
    mov rbx, rsi
    mov edx, 0x80000000
    mov eax, ebx
    and eax, 3
    cmp eax, 1
    jne .o1
    mov edx, 0x40000000
.o1:
    cmp eax, 2
    jne .o2
    mov edx, 0xC0000000
.o2:
    mov eax, 3
    test ebx, 0x40
    jz .o4
    mov eax, 4
    test ebx, 0x80
    jz .o3
    mov eax, 1
    jmp .o5
.o3:
    test ebx, 0x200
    jz .o5
    mov eax, 2
    jmp .o5
.o4:
    test ebx, 0x200
    jz .o5
    mov eax, 5
.o5:
    mov rcx, rdi
    mov r8d, 3
    xor r9d, r9d
    mov [rsp+32], rax
    mov qword [rsp+40], 0x80
    mov qword [rsp+48], 0
    call [__imp_CreateFileA]
    cmp rax, -1
    je .enoent
    test ebx, 0x400
    jz .out
    mov rbx, rax
    mov rcx, rbx
    xor edx, edx
    xor r8d, r8d
    mov r9d, 2
    call [__imp_SetFilePointer]
    mov rax, rbx
    jmp .out
.close:
    mov rcx, rdi
    call [__imp_CloseHandle]
    xor eax, eax
    jmp .out
.lseek:
    mov r9d, edx
    mov rcx, rdi
    mov rdx, rsi
    xor r8d, r8d
    call [__imp_SetFilePointer]
    mov eax, eax
    jmp .out
.mmap:
    xor ecx, ecx
    mov rdx, rsi
    mov r8d, 0x3000
    mov r9d, 4
    call [__imp_VirtualAlloc]
    test rax, rax
    jnz .out
    mov rax, -12
    jmp .out
.munmap:
    mov rcx, rdi
    xor edx, edx
    mov r8d, 0x8000
    call [__imp_VirtualFree]
    xor eax, eax
    jmp .out
.brk:
    mov rbx, [__cly_brk]
    test rbx, rbx
    jnz .b1
    xor ecx, ecx
    mov edx, 0x10000000
    mov r8d, 0x2000
    mov r9d, 4
    call [__imp_VirtualAlloc]
    test rax, rax
    jz .zero
    mov [__cly_brkbase], rax
    mov [__cly_brk], rax
    mov [__cly_brkcom], rax
.b1:
    test rdi, rdi
    jz .b2
    mov rax, [__cly_brkbase]
    cmp rdi, rax
    jb .b2
    add rax, 0x10000000
    cmp rdi, rax
    ja .b2
    mov rax, [__cly_brkcom]
    cmp rdi, rax
    jbe .b3
    mov rdx, rdi
    add rdx, 4095
    and rdx, -4096
    mov rbx, rdx
    sub rdx, rax
    mov rcx, rax
    mov r8d, 0x1000
    mov r9d, 4
    call [__imp_VirtualAlloc]
    test rax, rax
    jz .b2
    mov [__cly_brkcom], rbx
.b3:
    mov [__cly_brk], rdi
.b2:
    mov rax, [__cly_brk]
    jmp .out
.yield:
    xor ecx, ecx
    call [__imp_Sleep]
    xor eax, eax
    jmp .out
.nanosleep:
    mov rax, [rdi]
    imul rax, rax, 1000
    mov rbx, rax
    mov rax, [rdi+8]
    xor edx, edx
    mov ecx, 1000000
    div rcx
    add rax, rbx
    mov ecx, eax
    call [__imp_Sleep]
    xor eax, eax
    jmp .out
.getpid:
    call [__imp_GetCurrentProcessId]
    jmp .out
.exit:
    mov ecx, edi
    call [__imp_ExitProcess]
    jmp .out
.getcwd:
    mov rcx, rsi
    mov rdx, rdi
    call [__imp_GetCurrentDirectoryA]
    test eax, eax
    jz .enoent
    inc eax
    jmp .out
.chdir:
    mov rcx, rdi
    call [__imp_SetCurrentDirectoryA]
    test eax, eax
    jz .enoent
    xor eax, eax
    jmp .out
.mkdir:
    mov rcx, rdi
    xor edx, edx
    call [__imp_CreateDirectoryA]
    test eax, eax
    jz .enoent
    xor eax, eax
    jmp .out
.rmdir:
    mov rcx, rdi
    call [__imp_RemoveDirectoryA]
    test eax, eax
    jz .enoent
    xor eax, eax
    jmp .out
.unlink:
    mov rcx, rdi
    call [__imp_DeleteFileA]
    test eax, eax
    jz .enoent
    xor eax, eax
    jmp .out
.gtod:
    call __cly_ft
    mov [rdi], rax
    mov rax, rdx
    xor edx, edx
    mov ecx, 10
    div rcx
    mov [rdi+8], rax
    xor eax, eax
    jmp .out
.time:
    call __cly_ft
    test rdi, rdi
    jz .out
    mov [rdi], rax
    jmp .out
.cgt:
    call __cly_ft
    mov [rsi], rax
    imul rdx, rdx, 100
    mov [rsi+8], rdx
    xor eax, eax
.out:
    mov rsp, rbp
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rbp
    pop rbx
    ret
