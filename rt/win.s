bits 32
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
__cly_brk dd 0
__cly_brkbase dd 0
__cly_brkcom dd 0
section .text
__cly_h:
    cmp edi, 2
    ja .raw
    mov eax, -10
    sub eax, edi
    push eax
    call [__imp_GetStdHandle]
    ret
.raw:
    mov eax, edi
    ret
__cly_ft:
    sub esp, 8
    push esp
    call [__imp_GetSystemTimeAsFileTime]
    pop eax
    pop edx
    sub eax, 0xD53E8000
    sbb edx, 0x019DB1DE
    mov ecx, 10000000
    div ecx
    ret
__cly_syscall:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
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
    mov eax, -38
    jmp .out
.zero:
    xor eax, eax
    jmp .out
.notty:
    mov eax, -25
    jmp .out
.eio:
    mov eax, -5
    jmp .out
.enoent:
    mov eax, -2
    jmp .out
.read:
    mov ebx, edx
    call __cly_h
    sub esp, 4
    push 0
    lea ecx, [esp+4]
    push ecx
    push ebx
    push esi
    push eax
    call [__imp_ReadFile]
    pop ecx
    test eax, eax
    jz .eio
    mov eax, ecx
    jmp .out
.write:
    mov ebx, edx
    call __cly_h
    sub esp, 4
    push 0
    lea ecx, [esp+4]
    push ecx
    push ebx
    push esi
    push eax
    call [__imp_WriteFile]
    pop ecx
    test eax, eax
    jz .eio
    mov eax, ecx
    jmp .out
.open:
    mov ebx, esi
    mov ecx, 0x80000000
    mov eax, ebx
    and eax, 3
    cmp eax, 1
    jne .o1
    mov ecx, 0x40000000
.o1:
    cmp eax, 2
    jne .o2
    mov ecx, 0xC0000000
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
    push 0
    push 0x80
    push eax
    push 0
    push 3
    push ecx
    push edi
    call [__imp_CreateFileA]
    cmp eax, -1
    je .enoent
    test ebx, 0x400
    jz .out
    mov ebx, eax
    push 2
    push 0
    push 0
    push ebx
    call [__imp_SetFilePointer]
    mov eax, ebx
    jmp .out
.close:
    push edi
    call [__imp_CloseHandle]
    xor eax, eax
    jmp .out
.lseek:
    push edx
    push 0
    push esi
    push edi
    call [__imp_SetFilePointer]
    jmp .out
.mmap:
    push 4
    push 0x3000
    push esi
    push 0
    call [__imp_VirtualAlloc]
    test eax, eax
    jnz .out
    mov eax, -12
    jmp .out
.munmap:
    push 0x8000
    push 0
    push edi
    call [__imp_VirtualFree]
    xor eax, eax
    jmp .out
.brk:
    mov ebx, [__cly_brk]
    test ebx, ebx
    jnz .b1
    push 4
    push 0x2000
    push 0x10000000
    push 0
    call [__imp_VirtualAlloc]
    test eax, eax
    jz .zero
    mov [__cly_brkbase], eax
    mov [__cly_brk], eax
    mov [__cly_brkcom], eax
.b1:
    test edi, edi
    jz .b2
    mov eax, [__cly_brkbase]
    cmp edi, eax
    jb .b2
    add eax, 0x10000000
    cmp edi, eax
    ja .b2
    mov eax, [__cly_brkcom]
    cmp edi, eax
    jbe .b3
    mov ecx, edi
    add ecx, 4095
    and ecx, -4096
    mov ebx, ecx
    sub ecx, eax
    push 4
    push 0x1000
    push ecx
    push eax
    call [__imp_VirtualAlloc]
    test eax, eax
    jz .b2
    mov [__cly_brkcom], ebx
.b3:
    mov [__cly_brk], edi
.b2:
    mov eax, [__cly_brk]
    jmp .out
.yield:
    push 0
    call [__imp_Sleep]
    xor eax, eax
    jmp .out
.nanosleep:
    mov eax, [edi]
    imul eax, eax, 1000
    mov ebx, eax
    mov eax, [edi+8]
    xor edx, edx
    mov ecx, 1000000
    div ecx
    add eax, ebx
    push eax
    call [__imp_Sleep]
    xor eax, eax
    jmp .out
.getpid:
    call [__imp_GetCurrentProcessId]
    jmp .out
.exit:
    push edi
    call [__imp_ExitProcess]
    jmp .out
.getcwd:
    push edi
    push esi
    call [__imp_GetCurrentDirectoryA]
    test eax, eax
    jz .enoent
    inc eax
    jmp .out
.chdir:
    push edi
    call [__imp_SetCurrentDirectoryA]
    test eax, eax
    jz .enoent
    xor eax, eax
    jmp .out
.mkdir:
    push 0
    push edi
    call [__imp_CreateDirectoryA]
    test eax, eax
    jz .enoent
    xor eax, eax
    jmp .out
.rmdir:
    push edi
    call [__imp_RemoveDirectoryA]
    test eax, eax
    jz .enoent
    xor eax, eax
    jmp .out
.unlink:
    push edi
    call [__imp_DeleteFileA]
    test eax, eax
    jz .enoent
    xor eax, eax
    jmp .out
.gtod:
    call __cly_ft
    mov [edi], eax
    mov dword [edi+4], 0
    mov eax, edx
    xor edx, edx
    mov ecx, 10
    div ecx
    mov [edi+8], eax
    mov dword [edi+12], 0
    xor eax, eax
    jmp .out
.time:
    call __cly_ft
    test edi, edi
    jz .out
    mov [edi], eax
    mov dword [edi+4], 0
    jmp .out
.cgt:
    call __cly_ft
    mov [esi], eax
    mov dword [esi+4], 0
    imul edx, edx, 100
    mov [esi+8], edx
    mov dword [esi+12], 0
    xor eax, eax
.out:
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret
