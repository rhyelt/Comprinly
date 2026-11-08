/* Writer for Windows PE executables (.exe), 32 and 64-bit: DOS stub, headers, text, data and import sections. */
#include "cly.h"

static i64 pe_base = 0x400000;
#define PE_BASE pe_base
#define PE_FA 0x200
#define PE_SA 0x1000

static void put32(Bytes *b, u32 v) { badle(b, v, 4); }
static void put16(Bytes *b, u16 v) { badle(b, v, 2); }
static void put64(Bytes *b, u64 v) { badle(b, v, 8); }

#define fxa(x) ((x) & (((i64)1 << 62) - 1))
static i64 rup(i64 v, i64 a) { return (v + a - 1) / a * a; }

typedef struct {
    char name[8];
    u32 vsize, rva, rawsize, rawptr, chars;
} PSec;

/* Writes one PE section header. */
static void put_sec(Bytes *b, const PSec *s)
{
    badd(b, s->name, 8);
    put32(b, s->vsize);
    put32(b, s->rva);
    put32(b, s->rawsize);
    put32(b, s->rawptr);
    put32(b, 0);
    put32(b, 0);
    put16(b, 0);
    put16(b, 0);
    put32(b, s->chars);
}

static void pe_exports(Obj *o, Bytes *edata, i64 edata_va)
{
    int ne = 0;
    Sym **ex = xmalloc(sizeof(Sym *) * (size_t)(o->nsyms + 1));
    for (int i = 0; i < o->nsyms; i++) {
        Sym *q = o->syms[i];
        if (q->global && q->defined && !q->common && !q->ext && q->name[0] && q->svis == 0 && !q->isequ && q->sec >= 0) ex[ne++] = q;
    }
    for (int i = 1; i < ne; i++) { Sym *t = ex[i]; int j = i - 1; while (j >= 0 && strcmp(ex[j]->name, t->name) > 0) { ex[j + 1] = ex[j]; j--; } ex[j + 1] = t; }
    if (ne > 0) {
        const char *dn = g_outname ? g_outname : "cly.dll";
        const char *b1 = strrchr(dn, '/'), *b2 = strrchr(dn, '\\');
        if (b2 && (!b1 || b2 > b1)) b1 = b2;
        if (b1) dn = b1 + 1;
        size_t pos = 40 + (size_t)ne * 8 + (size_t)ne * 2;
        size_t dnpos = pos;
        pos += strlen(dn) + 1;
        size_t *np = xmalloc(sizeof(size_t) * (size_t)ne);
        for (int i = 0; i < ne; i++) { np[i] = pos; pos += strlen(ex[i]->name) + 1; }
        bfill(edata, 0, pos);
        bputle(edata, 12, (u64)(edata_va + (i64)dnpos), 4);
        bputle(edata, 16, 1, 4);
        bputle(edata, 20, (u64)ne, 4);
        bputle(edata, 24, (u64)ne, 4);
        bputle(edata, 28, (u64)edata_va + 40, 4);
        bputle(edata, 32, (u64)(edata_va + 40 + ne * 4), 4);
        bputle(edata, 36, (u64)(edata_va + 40 + ne * 8), 4);
        for (int i = 0; i < ne; i++) {
            bputle(edata, 40 + (size_t)i * 4, (u64)(sym_addr(o, ex[i]) - PE_BASE), 4);
            bputle(edata, 40 + (size_t)ne * 4 + (size_t)i * 4, (u64)(edata_va + (i64)np[i]), 4);
            bputle(edata, 40 + (size_t)ne * 8 + (size_t)i * 2, (u64)i, 2);
            memcpy(edata->p + np[i], ex[i]->name, strlen(ex[i]->name) + 1);
        }
        memcpy(edata->p + dnpos, dn, strlen(dn) + 1);
        free(np);
    }
    free(ex);
}

static void pe_relocs(Obj *o, Bytes *reloc)
{
    i64 *fx = g_pefix;
    int nf = g_npefix;
    for (int i = 1; i < nf; i++) { i64 t = fx[i]; int j = i - 1; while (j >= 0 && fx[j] > t) { fx[j + 1] = fx[j]; j--; } fx[j + 1] = t; }
    int i = 0;
    while (i < nf) {
        i64 page = (fxa(fx[i]) - PE_BASE) & ~(i64)0xFFF;
        size_t bstart = reloc->n;
        put32(reloc, (u32)page);
        put32(reloc, 0);
        int cnt = 0;
        while (i < nf && (((fxa(fx[i]) - PE_BASE) & ~(i64)0xFFF) == page)) {
            if (i == 0 || fx[i] != fx[i - 1]) { put16(reloc, (u16)(((o->is64 ? 10 : 3) << 12) | ((fxa(fx[i]) - PE_BASE) & 0xFFF))); cnt++; }
            i++;
        }
        if (cnt & 1) { put16(reloc, 0); cnt++; }
        bputle(reloc, bstart + 4, (u64)(8 + cnt * 2), 4);
    }
}


/* -strip: the whole program, its data and the import tables go into one read, write and execute section, so a small program is a 512 byte header and one 512 byte block. Returns -1 when the program has bss, which needs the usual layout. */
static int pe_single(Obj *o, Bytes *out)
{
    int w = o->is64 ? 8 : 4;
    pe_base = 0x400000;
    g_compact = 1;
    link_layout(o, PE_BASE + PE_SA, 0, 0);
    g_compact = 0;
    if (g_lay.end != g_lay.file_end) return -1;
    if (!link_apply(o)) return 0;
    if (!o->entry) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    i64 entry = link_entry(o);
    Bytes img = {0};
    i64 filesz = g_lay.file_end - g_lay.base;
    link_range(o, &img, g_lay.base, g_lay.file_end);
    int nd;
    const ImpDll *imps = link_imports(&nd);
    i64 idata_off = rup(filesz, 8);
    Bytes idata = {0};
    i64 iat_rva = 0, iat_size = 0;
    if (nd > 0) {
        Sec *iat = NULL;
        for (int i = 0; i < o->nsecs; i++) if (!strcmp(o->secs[i]->name, ".iat")) iat = o->secs[i];
        iat_rva = iat->base - PE_BASE;
        iat_size = iat->size;
        i64 idata_va = PE_SA + idata_off;
        size_t dir_sz = (size_t)(nd + 1) * 20;
        i64 pos = (i64)dir_sz;
        i64 *ilt = xmalloc(sizeof(i64) * (size_t)nd);
        for (int d = 0; d < nd; d++) { ilt[d] = pos; pos += (imps[d].n + 1) * w; }
        i64 *namepos = xmalloc(sizeof(i64) * (size_t)nd);
        for (int d = 0; d < nd; d++) { namepos[d] = pos; pos += (i64)strlen(imps[d].dll) + 1; if (pos & 1) pos++; }
        i64 **hn = xmalloc(sizeof(i64 *) * (size_t)nd);
        for (int d = 0; d < nd; d++) {
            hn[d] = xmalloc(sizeof(i64) * (size_t)(imps[d].n + 1));
            for (int k = 0; k < imps[d].n; k++) { hn[d][k] = pos; pos += 2 + (i64)strlen(imps[d].names[k]) + 1; if (pos & 1) pos++; }
        }
        bfill(&idata, 0, (size_t)pos);
        bfill(&img, 0, (size_t)(idata_off - filesz));
        for (int d = 0; d < nd; d++) {
            bputle(&idata, (size_t)(d * 20), (u64)(idata_va + ilt[d]), 4);
            bputle(&idata, (size_t)(d * 20 + 12), (u64)(idata_va + namepos[d]), 4);
            bputle(&idata, (size_t)(d * 20 + 16), (u64)(iat_rva + imps[d].slot[0]), 4);
            memcpy(idata.p + namepos[d], imps[d].dll, strlen(imps[d].dll) + 1);
            for (int k = 0; k < imps[d].n; k++) {
                bputle(&idata, (size_t)(ilt[d] + k * w), (u64)(idata_va + hn[d][k]), w);
                memcpy(idata.p + hn[d][k] + 2, imps[d].names[k], strlen(imps[d].names[k]) + 1);
                i64 at = iat_rva + imps[d].slot[k] - PE_SA;
                if (at >= 0 && at + w <= (i64)img.n) bputle(&img, (size_t)at, (u64)(idata_va + hn[d][k]), w);
            }
        }
        for (int d = 0; d < nd; d++) free(hn[d]);
        free(hn);
        free(ilt);
        free(namepos);
        badd(&img, idata.p, idata.n);
    }
    i64 vs = (i64)img.n;
    i64 raw = rup(vs, PE_FA);
    bfill(&img, 0, (size_t)(raw - vs));
    i64 hdr_raw = rup(64 + 24 + (o->is64 ? 240 : 224) + 40, PE_FA);
    i64 image_size = rup(PE_SA + vs, PE_SA);
    Bytes f = {0};
    u8 mz[64] = { 'M', 'Z' };
    mz[0x3C] = 0x40;
    badd(&f, mz, 64);
    badd(&f, "PE\0\0", 4);
    put16(&f, o->is64 ? 0x8664 : 0x14C);
    put16(&f, 1);
    put32(&f, 0); put32(&f, 0); put32(&f, 0);
    put16(&f, o->is64 ? 240 : 224);
    put16(&f, o->is64 ? 0x0022 : 0x0103);
    put16(&f, o->is64 ? 0x20B : 0x10B);
    bad1(&f, 1);
    bad1(&f, 0);
    put32(&f, (u32)raw);
    put32(&f, 0);
    put32(&f, 0);
    put32(&f, (u32)(entry - PE_BASE));
    put32(&f, PE_SA);
    if (o->is64) put64(&f, PE_BASE);
    else { put32(&f, PE_SA); put32(&f, PE_BASE); }
    put32(&f, PE_SA);
    put32(&f, PE_FA);
    if (o->is64) { put16(&f, 6); put16(&f, 0); } else { put16(&f, 4); put16(&f, 0); }
    put16(&f, 0); put16(&f, 0);
    if (o->is64) { put16(&f, 6); put16(&f, 0); } else { put16(&f, 4); put16(&f, 0); }
    put32(&f, 0);
    put32(&f, (u32)image_size);
    put32(&f, (u32)hdr_raw);
    put32(&f, 0);
    put16(&f, 3);
    put16(&f, 0);
    if (o->is64) { put64(&f, 0x100000); put64(&f, 0x1000); put64(&f, 0x100000); put64(&f, 0x1000); }
    else { put32(&f, 0x100000); put32(&f, 0x1000); put32(&f, 0x100000); put32(&f, 0x1000); }
    put32(&f, 0);
    put32(&f, 16);
    for (int i = 0; i < 16; i++) {
        if (i == 1 && nd > 0) { put32(&f, (u32)(PE_SA + idata_off)); put32(&f, (u32)((nd + 1) * 20)); }
        else if (i == 12 && nd > 0) { put32(&f, (u32)iat_rva); put32(&f, (u32)iat_size); }
        else { put32(&f, 0); put32(&f, 0); }
    }
    PSec s;
    memset(&s, 0, sizeof s);
    memcpy(s.name, ".text", 5);
    s.vsize = (u32)vs; s.rva = PE_SA; s.rawsize = (u32)raw; s.rawptr = (u32)hdr_raw; s.chars = 0xE0000060;
    put_sec(&f, &s);
    while ((i64)f.n < hdr_raw) bad1(&f, 0);
    badd(&f, img.p, img.n);
    badd(out, f.p, f.n);
    return 1;
}


static u32 rd32le(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static int pe_read_exports(const char *path, char ***names, int *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0x100 || len > (64L << 20)) { fclose(f); return 0; }
    u8 *d = xmalloc((size_t)len);
    size_t got = fread(d, 1, (size_t)len, f);
    fclose(f);
    int ok = 0;
    if ((long)got == len && d[0] == 'M' && d[1] == 'Z') {
        u32 pe = rd32le(d + 0x3c);
        if (pe + 0x108 < (u32)len && !memcmp(d + pe, "PE\0\0", 4)) {
            u16 ns = (u16)(d[pe + 6] | d[pe + 7] << 8), osz = (u16)(d[pe + 20] | d[pe + 21] << 8);
            u32 opt = pe + 24;
            int p64 = (d[opt] | d[opt + 1] << 8) == 0x20b;
            u32 erva = rd32le(d + opt + (p64 ? 112 : 96));
            u32 sec = opt + osz;
            #define RVA2OFF(r, out) do { out = 0; for (int q = 0; q < ns; q++) { u32 b = sec + 40u * (u32)q; if (b + 40 > (u32)len) break; u32 va = rd32le(d + b + 12), vs = rd32le(d + b + 8), rs = rd32le(d + b + 16), rp = rd32le(d + b + 20); if ((r) >= va && (r) < va + (vs > rs ? vs : rs)) { out = rp + ((r) - va); break; } } } while (0)
            u32 eo = 0;
            if (erva) RVA2OFF(erva, eo);
            if (eo && eo + 40 <= (u32)len) {
                u32 nn = rd32le(d + eo + 24), np = rd32le(d + eo + 32), npo = 0;
                RVA2OFF(np, npo);
                if (npo && nn < 100000 && npo + 4ull * nn <= (u64)len) {
                    char **v = xmalloc(sizeof(char *) * (nn + 1));
                    int c = 0;
                    for (u32 i = 0; i < nn; i++) {
                        u32 sr = rd32le(d + npo + 4 * i), so = 0;
                        RVA2OFF(sr, so);
                        if (so && so < (u32)len) { size_t m = strnlen((char *)d + so, (size_t)len - so); v[c++] = xstrndup((char *)d + so, m); }
                    }
                    *names = v; *n = c; ok = 1;
                }
            }
        }
    }
    free(d);
    return ok;
}

typedef struct { const char *path, *base; char **names; int n, loaded; } NeedDll;

static const char *path_base(const char *p)
{
    const char *b = p;
    for (; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

static int is_crt_dll(const char *b)
{
    return !strncasecmp(b, "msvcr", 5) || !strncasecmp(b, "ucrt", 4) || !strncasecmp(b, "api-ms-win-crt", 14) || !strncasecmp(b, "vcruntime", 9) || !strncasecmp(b, "crtdll", 6);
}

static const char *const tk32[] = {"AllocConsole", "CloseHandle", "CopyFileA", "CreateDirectoryA", "CreateEventA", "CreateFileA", "CreateFileW", "CreateMutexA", "CreateProcessA", "CreateProcessW", "CreateThread", "DeleteCriticalSection", "DeleteFileA", "DeleteFileW", "EnterCriticalSection", "ExitProcess", "ExitThread", "FileTimeToSystemTime", "FindClose", "FindFirstFileA", "FindNextFileA", "FlushFileBuffers", "FormatMessageA", "FreeConsole", "FreeEnvironmentStringsW", "FreeLibrary", "GetACP", "GetCPInfo", "GetCommandLineA", "GetCommandLineW", "GetComputerNameA", "GetConsoleCP", "GetConsoleMode", "GetConsoleOutputCP", "GetConsoleScreenBufferInfo", "GetCurrentDirectoryA", "GetCurrentProcess", "GetCurrentProcessId", "GetCurrentThread", "GetCurrentThreadId", "GetEnvironmentStringsW", "GetEnvironmentVariableA", "GetFileAttributesA", "GetFileSize", "GetFileSizeEx", "GetLastError", "GetLocalTime", "GetModuleFileNameA", "GetModuleFileNameW", "GetModuleHandleA", "GetModuleHandleW", "GetProcAddress", "GetProcessHeap", "GetStartupInfoA", "GetStartupInfoW", "GetStdHandle", "GetSystemInfo", "GetSystemTime", "GetSystemTimeAsFileTime", "GetTickCount", "GetTickCount64", "GetUserDefaultLCID", "GetVersion", "GetVersionExA", "GlobalAlloc", "GlobalFree", "GlobalLock", "GlobalUnlock", "HeapAlloc", "HeapCreate", "HeapDestroy", "HeapFree", "HeapReAlloc", "InitializeCriticalSection", "InterlockedDecrement", "InterlockedExchange", "InterlockedIncrement", "IsDebuggerPresent", "LeaveCriticalSection", "LoadLibraryA", "LoadLibraryW", "LocalAlloc", "LocalFree", "MoveFileA", "MultiByteToWideChar", "OutputDebugStringA", "QueryPerformanceCounter", "QueryPerformanceFrequency", "RaiseException", "ReadConsoleA", "ReadConsoleW", "ReadFile", "ReleaseMutex", "RemoveDirectoryA", "ResetEvent", "SetConsoleCP", "SetConsoleCtrlHandler", "SetConsoleMode", "SetConsoleOutputCP", "SetConsoleTextAttribute", "SetConsoleTitleA", "SetConsoleTitleW", "SetCurrentDirectoryA", "SetEnvironmentVariableA", "SetEvent", "SetFilePointer", "SetFilePointerEx", "SetLastError", "SetStdHandle", "SetUnhandledExceptionFilter", "Sleep", "SleepEx", "TerminateProcess", "TlsAlloc", "TlsFree", "TlsGetValue", "TlsSetValue", "VirtualAlloc", "VirtualFree", "VirtualProtect", "WaitForMultipleObjects", "WaitForSingleObject", "WideCharToMultiByte", "WriteConsoleA", "WriteConsoleW", "WriteFile", "lstrcatA", "lstrcmpA", "lstrcpyA", "lstrlenA", "lstrlenW", 0};
static const char *const tu32[] = {"AdjustWindowRect", "BeginPaint", "ClientToScreen", "CreateWindowExA", "CreateWindowExW", "DefWindowProcA", "DefWindowProcW", "DestroyWindow", "DialogBoxParamA", "DispatchMessageA", "DispatchMessageW", "DrawTextA", "EndDialog", "EndPaint", "FillRect", "FindWindowA", "GetAsyncKeyState", "GetClientRect", "GetCursorPos", "GetDC", "GetDlgItem", "GetFocus", "GetKeyState", "GetMessageA", "GetMessageW", "GetSystemMetrics", "GetWindowRect", "GetWindowTextA", "InvalidateRect", "KillTimer", "LoadCursorA", "LoadCursorW", "LoadIconA", "LoadImageA", "MapVirtualKeyA", "MessageBoxA", "MessageBoxExA", "MessageBoxW", "MoveWindow", "PeekMessageA", "PeekMessageW", "PostMessageA", "PostQuitMessage", "RegisterClassA", "RegisterClassExA", "RegisterClassExW", "RegisterClassW", "ReleaseCapture", "ReleaseDC", "ScreenToClient", "SendMessageA", "SendMessageW", "SetCapture", "SetCursorPos", "SetDlgItemTextA", "SetFocus", "SetForegroundWindow", "SetTimer", "SetWindowPos", "SetWindowTextA", "ShowCursor", "ShowWindow", "TranslateMessage", "UpdateWindow", "keybd_event", "mouse_event", "wsprintfA", "wsprintfW", 0};
static const char *const tgdi[] = {"BitBlt", "ChoosePixelFormat", "CreateCompatibleBitmap", "CreateCompatibleDC", "CreateFontA", "CreatePen", "CreateSolidBrush", "DeleteDC", "DeleteObject", "Ellipse", "GetPixel", "GetStockObject", "LineTo", "MoveToEx", "Rectangle", "SelectObject", "SetBkColor", "SetBkMode", "SetPixel", "SetPixelFormat", "SetTextColor", "StretchBlt", "SwapBuffers", "TextOutA", 0};
static const char *const tcrt[] = {"_XcptFilter", "__getmainargs", "__p__commode", "__p__environ", "__p__fmode", "__set_app_type", "_access", "_amsg_exit", "_atoi64", "_beginthreadex", "_cexit", "_chdir", "_close", "_controlfp", "_endthreadex", "_exit", "_fileno", "_fstat", "_getch", "_getcwd", "_initterm", "_isatty", "_itoa", "_kbhit", "_ltoa", "_mkdir", "_onexit", "_open", "_read", "_rmdir", "_setmode", "_sleep", "_snprintf", "_stat", "_strdup", "_stricmp", "_strlwr", "_strnicmp", "_strupr", "_ultoa", "_unlink", "_vsnprintf", "_wcsicmp", "_wfopen", "_write", "abort", "abs", "atan", "atan2", "atexit", "atof", "atoi", "atol", "bsearch", "calloc", "ceil", "clock", "cos", "exit", "exp", "fabs", "fclose", "feof", "ferror", "fflush", "fgetc", "fgets", "floor", "fmod", "fopen", "fprintf", "fputc", "fputs", "fread", "free", "fscanf", "fseek", "ftell", "fwrite", "getc", "getchar", "getenv", "gets", "isalnum", "isalpha", "isdigit", "islower", "isspace", "isupper", "labs", "log", "log10", "longjmp", "malloc", "memchr", "memcmp", "memcpy", "memmove", "memset", "perror", "pow", "printf", "putc", "putchar", "puts", "qsort", "raise", "rand", "realloc", "remove", "rename", "rewind", "scanf", "setjmp", "signal", "sin", "snprintf", "sprintf", "sqrt", "srand", "sscanf", "strcat", "strchr", "strcmp", "strcpy", "strdup", "strlen", "strncat", "strncmp", "strncpy", "strrchr", "strstr", "strtod", "strtok", "strtol", "strtoul", "swprintf", "system", "tan", "time", "tolower", "toupper", "vprintf", "vsprintf", "wcscmp", "wcscpy", "wcslen", "wprintf", 0};
static const char *const tsh[] = {"CommandLineToArgvW", "SHGetFolderPathA", "ShellExecuteA", "ShellExecuteW", 0};
static const char *const tadv[] = {"AdjustTokenPrivileges", "CryptAcquireContextA", "CryptGenRandom", "CryptReleaseContext", "GetUserNameA", "LookupPrivilegeValueA", "OpenProcessToken", "RegCloseKey", "RegCreateKeyExA", "RegDeleteKeyA", "RegOpenKeyExA", "RegQueryValueExA", "RegSetValueExA", 0};

typedef struct { const char *dll; const char *const *names; } WinTab;

static const WinTab k_wintabs[] = {
    {"kernel32.dll", tk32}, {"user32.dll", tu32}, {"gdi32.dll", tgdi}, {"msvcrt.dll", tcrt}, {"shell32.dll", tsh}, {"advapi32.dll", tadv}, {0, 0}
};

static const char *const tws[] = {"WSACleanup", "WSAGetLastError", "WSASocketA", "WSAStartup", "accept", "bind", "closesocket", "connect", "freeaddrinfo", "getaddrinfo", "gethostbyname", "getpeername", "getsockname", "getsockopt", "htonl", "htons", "inet_addr", "inet_ntoa", "ioctlsocket", "listen", "ntohl", "ntohs", "recv", "recvfrom", "select", "send", "sendto", "setsockopt", "shutdown", "socket", 0};
static const char *const tole[] = {"CoCreateInstance", "CoInitialize", "CoInitializeEx", "CoTaskMemAlloc", "CoTaskMemFree", "CoUninitialize", "CLSIDFromString", "StringFromGUID2", 0};
static const char *const tmm[] = {"PlaySoundA", "PlaySoundW", "timeBeginPeriod", "timeEndPeriod", "timeGetTime", "waveOutClose", "waveOutOpen", "waveOutPrepareHeader", "waveOutUnprepareHeader", "waveOutWrite", 0};

static const WinTab k_wintabs2[] = {{"ws2_32.dll", tws}, {"ole32.dll", tole}, {"winmm.dll", tmm}, {0, 0}};

static int has_pfx(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

static int has_any(const char *s, const char *const *w)
{
    for (; *w; w++) if (strstr(s, *w)) return 1;
    return 0;
}

static const char *win_exact(const char *nm)
{
    for (int t = 0; k_wintabs[t].dll; t++)
        for (int i = 0; k_wintabs[t].names[i]; i++)
            if (!strcmp(k_wintabs[t].names[i], nm)) return k_wintabs[t].dll;
    for (int t = 0; k_wintabs2[t].dll; t++)
        for (int i = 0; k_wintabs2[t].names[i]; i++)
            if (!strcmp(k_wintabs2[t].names[i], nm)) return k_wintabs2[t].dll;
    return NULL;
}

static const char *win_known(const char *nm)
{
    const char *d = win_exact(nm);
    if (d) return d;
    size_t n = strlen(nm);
    if (n > 3 && n < 120) {
        char base[128];
        memcpy(base, nm, n + 1);
        if (base[n - 1] == 'A' || base[n - 1] == 'W') {
            base[n - 1] = 0;
            d = win_exact(base);
            if (d) return d;
            size_t m = n - 1;
            if (m > 2 && base[m - 2] == 'E' && base[m - 1] == 'x') { base[m - 2] = 0; d = win_exact(base); if (d) return d; }
            memcpy(base, nm, n + 1);
        }
        if (n > 2 && !strcmp(nm + n - 2, "Ex")) { base[n - 2] = 0; d = win_exact(base); if (d) return d; }
    }
    static const char *const kpfx[] = {"Nt", "Rtl", "Zw", 0};
    if (has_pfx(nm, "Reg") && isupper((unsigned char)nm[3])) return "advapi32.dll";
    if (has_pfx(nm, "Crypt") || (has_pfx(nm, "Open") && strstr(nm, "Token")) || strstr(nm, "Service")) return "advapi32.dll";
    if (has_pfx(nm, "WSA")) return "ws2_32.dll";
    if (has_pfx(nm, "wgl") || (has_pfx(nm, "gl") && isupper((unsigned char)nm[2]))) return "opengl32.dll";
    if (has_pfx(nm, "glu") && isupper((unsigned char)nm[3])) return "glu32.dll";
    if (has_pfx(nm, "Dwm")) return "dwmapi.dll";
    if (has_pfx(nm, "Co") && isupper((unsigned char)nm[2]) && strstr(nm, "Initialize")) return "ole32.dll";
    if ((has_pfx(nm, "SH") && isupper((unsigned char)nm[2])) || has_pfx(nm, "Shell")) return "shell32.dll";
    if (has_pfx(nm, "Path") && isupper((unsigned char)nm[4])) return "shlwapi.dll";
    if (has_pfx(nm, "Str") && isupper((unsigned char)nm[3]) && strlen(nm) > 5) return "shlwapi.dll";
    for (int i = 0; kpfx[i]; i++) if (has_pfx(nm, kpfx[i]) && isupper((unsigned char)nm[strlen(kpfx[i])])) return "ntdll.dll";
    if (isupper((unsigned char)nm[0]) && has_any(nm, (const char *const[]){"Window", "Message", "Dialog", "Menu", "Cursor", "Icon", "Clipboard", "Caption", "Keyboard", "MapVirtualKey", "Mouse", "Monitor", "Desktop", "Hook", "Accel", "ScrollBar", "Rect", "DC", 0}) && !has_pfx(nm, "Create") && !strstr(nm, "Console") && !strstr(nm, "Process") && !strstr(nm, "Thread")) return "user32.dll";
    if (has_pfx(nm, "Create") && has_any(nm, (const char *const[]){"Window", "Dialog", "Menu", "Cursor", "Icon", "Accelerator", 0})) return "user32.dll";
    if (isupper((unsigned char)nm[0]) && has_any(nm, (const char *const[]){"Brush", "Pen", "Bitmap", "Font", "Palette", "Region", "Blt", "TextOut", "Pixel", "StockObject", 0})) return "gdi32.dll";
    static const char *const kverb[] = {"Get", "Set", "Create", "Open", "Close", "Read", "Write", "Map", "Unmap", "Virtual", "Heap", "Local", "Global", "Find", "Wait", "Release", "Reset", "Resume", "Suspend", "Terminate", "Load", "Free", "Lock", "Unlock", "Duplicate", "Flush", "Query", "Is", "Enter", "Leave", "Initialize", "Delete", "Copy", "Move", "Peek", "Signal", "Interlocked", "Tls", "Fls", "Sleep", "Exit", "Allocate", "Console", "Debug", "Output", "Format", "Lstr", "Mul", "Add", "Remove", "Attach", "Cancel", "Connect", "Disconnect", "Transact", "Device", "Process", "Thread", "File", "Last", "Start", "Stop", "Switch", "Try", "Convert", "Compare", "Check", "Expand", "Fill", "Zero", "Rtl", 0};
    if (isupper((unsigned char)nm[0]) && strlen(nm) > 4) {
        for (int i = 0; kverb[i]; i++) if (has_pfx(nm, kverb[i]) && (isupper((unsigned char)nm[strlen(kverb[i])]) || !nm[strlen(kverb[i])])) return "kernel32.dll";
    }
    return NULL;
}

void pe_auto_imports(Obj *o)
{
    int nd = g_ldo.nneeded;
    NeedDll *D = xmalloc(sizeof(NeedDll) * (size_t)(nd + 1));
    for (int i = 0; i < nd; i++) {
        D[i].path = g_ldo.needed[i];
        D[i].base = path_base(g_ldo.needed[i]);
        D[i].names = NULL; D[i].n = 0;
        D[i].loaded = pe_read_exports(g_ldo.needed[i], &D[i].names, &D[i].n);
    }
    for (int i = 0; i < o->nsyms; i++) {
        Sym *s = o->syms[i];
        if (s->defined || s->import || !s->referenced || !s->ext) continue;
        const char *nm = s->name;
        int imp = !strncmp(nm, "__imp_", 6);
        if (imp) nm += 6;
        char buf[256];
        snprintf(buf, sizeof buf, "%s", nm);
        if (!o->is64 && buf[0] == '_') memmove(buf, buf + 1, strlen(buf));
        char *at = strrchr(buf, '@');
        if (at && at[1] && strspn(at + 1, "0123456789") == strlen(at + 1)) *at = 0;
        int pick = -1;
        for (int k = 0; k < nd && pick < 0; k++) {
            if (!D[k].loaded) continue;
            for (int j = 0; j < D[k].n; j++) if (!strcmp(D[k].names[j], buf)) { pick = k; break; }
        }
        if (pick < 0) {
            const char *kd = win_known(buf);
            if (kd) {
                for (int k = 0; k < nd && pick < 0; k++) if (!strcasecmp(D[k].base, kd)) pick = k;
                if (pick < 0) {
                    s->import = 1;
                    s->impdll = xstrdup(kd);
                    s->impname = xstrdup(buf);
                    continue;
                }
            }
        }
        if (pick < 0) {
            int cname = islower((unsigned char)buf[0]) || buf[0] == '_';
            for (int k = 0; k < nd && pick < 0; k++) if (!D[k].loaded && (is_crt_dll(D[k].base) == cname)) pick = k;
            for (int k = 0; k < nd && pick < 0; k++) if (!D[k].loaded) pick = k;
        }
        if (pick < 0) continue;
        s->import = 1;
        s->impdll = xstrdup(D[pick].base);
        s->impname = xstrdup(buf);
    }
}

static int fmt_pe_norm(Obj *o, Bytes *out);

int fmt_pe(Obj *o, Bytes *out)
{
    pe_auto_imports(o);
    if (!o->entry && !g_pe_dll) for (int i = 0; i < o->nsyms; i++) if (o->syms[i]->defined && (!strcmp(o->syms[i]->name, "main") || !strcmp(o->syms[i]->name, "_main") || !strcmp(o->syms[i]->name, "WinMain") || !strcmp(o->syms[i]->name, "start") || !strcmp(o->syms[i]->name, "_start"))) { o->entry = o->syms[i]; break; }
    link_prepare_imports(o);
    if (g_xstrip && !g_pe_dll && o->arch == ARCH_X86) {
        Bytes tb = {0};
        int tr = pe_single(o, &tb);
        if (tr == 0) return 0;
        if (tr == 1) {
            Bytes nb = {0};
            int r = fmt_pe_norm(o, &nb);
            if (r && nb.n < tb.n) badd(out, nb.p, nb.n);
            else badd(out, tb.p, tb.n);
            return 1;
        }
    }
    return fmt_pe_norm(o, out);
}

/* Lays out the image, builds the import table from the imports the code uses (kernel32 and others) and writes the file. */
static int fmt_pe_norm(Obj *o, Bytes *out)
{
    int w = o->is64 ? 8 : 4;
    int dll = g_pe_dll;
    pe_base = dll ? 0x10000000 : 0x400000;
    link_layout(o, PE_BASE + PE_SA, PE_SA, 1);
    if (!link_apply(o)) return 0;
    if (!o->entry && !dll) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    i64 entry = o->entry ? link_entry(o) : PE_BASE;
    Bytes img = {0};
    link_image(o, &img);
    i64 text_va = PE_SA;
    i64 text_vs = g_lay.a_end - (PE_BASE + PE_SA);
    if (text_vs < 0) text_vs = 0;
    i64 text_raw = rup(text_vs, PE_FA);
    Bytes text = {0};
    badd(&text, img.p, (size_t)text_vs);
    bfill(&text, 0, (size_t)(text_raw - text_vs));

    int has_data = g_lay.has_rw;
    i64 data_va = g_lay.rw_start - PE_BASE;
    i64 data_fsz = g_lay.file_end - g_lay.rw_start;
    if (data_fsz < 0) data_fsz = 0;
    i64 data_vs = g_lay.end - g_lay.rw_start;
    i64 data_raw = rup(data_fsz, PE_FA);
    Bytes data = {0};
    if (has_data && data_fsz > 0) {
        i64 doff = g_lay.rw_start - g_lay.base;
        badd(&data, img.p + doff, (size_t)data_fsz);
        bfill(&data, 0, (size_t)(data_raw - data_fsz));
    }

    int nd;
    const ImpDll *imps = link_imports(&nd);
    Bytes idata = {0};
    int merge = nd > 0 && has_data && data_fsz > 0 && data_vs == data_fsz;
    i64 idata_va = merge ? data_va + rup(data_fsz, 16) : rup(g_lay.end - PE_BASE, PE_SA);
    i64 iat_rva = 0, iat_size = 0;
    if (nd > 0) {
        Sec *iat = NULL;
        for (int i = 0; i < o->nsecs; i++) if (!strcmp(o->secs[i]->name, ".iat")) iat = o->secs[i];
        iat_rva = iat->base - PE_BASE;
        iat_size = iat->size;
        size_t dir_sz = (size_t)(nd + 1) * 20;
        i64 pos = (i64)dir_sz;
        i64 *ilt = xmalloc(sizeof(i64) * (size_t)nd);
        for (int d = 0; d < nd; d++) {
            ilt[d] = pos;
            pos += (imps[d].n + 1) * w;
        }
        i64 *namepos = xmalloc(sizeof(i64) * (size_t)nd);
        for (int d = 0; d < nd; d++) {
            namepos[d] = pos;
            pos += (i64)strlen(imps[d].dll) + 1;
            if (pos & 1) pos++;
        }
        i64 **hn = xmalloc(sizeof(i64 *) * (size_t)nd);
        for (int d = 0; d < nd; d++) {
            hn[d] = xmalloc(sizeof(i64) * (size_t)(imps[d].n + 1));
            for (int k = 0; k < imps[d].n; k++) {
                hn[d][k] = pos;
                pos += 2 + (i64)strlen(imps[d].names[k]) + 1;
                if (pos & 1) pos++;
            }
        }
        bfill(&idata, 0, (size_t)pos);
        i64 first_slot = 0;
        for (int d = 0; d < nd; d++) {
            bputle(&idata, (size_t)(d * 20), (u64)(idata_va + ilt[d]), 4);
            bputle(&idata, (size_t)(d * 20 + 12), (u64)(idata_va + namepos[d]), 4);
            bputle(&idata, (size_t)(d * 20 + 16), (u64)(iat_rva + imps[d].slot[0]), 4);
            memcpy(idata.p + namepos[d], imps[d].dll, strlen(imps[d].dll) + 1);
            for (int k = 0; k < imps[d].n; k++) {
                bputle(&idata, (size_t)(ilt[d] + k * w), (u64)(idata_va + hn[d][k]), w);
                memcpy(idata.p + hn[d][k] + 2, imps[d].names[k], strlen(imps[d].names[k]) + 1);
                if (has_data) {
                    i64 at = iat_rva + imps[d].slot[k] - data_va;
                    if (at >= 0 && at + w <= (i64)data.n) bputle(&data, (size_t)at, (u64)(idata_va + hn[d][k]), w);
                }
            }
            first_slot += imps[d].n + 1;
        }
        (void)first_slot;
        for (int d = 0; d < nd; d++) free(hn[d]);
        free(hn);
        free(ilt);
        free(namepos);
    }
    i64 idata_vs = (i64)idata.n;
    i64 idata_raw = rup(idata_vs, PE_FA);
    bfill(&idata, 0, (size_t)(idata_raw - idata_vs));
    int has_id = nd > 0 && !merge;
    i64 img_end = nd > 0 ? idata_va + idata_vs : g_lay.end - PE_BASE;
    if (merge) {
        i64 off = idata_va - data_va;
        data.n = (size_t)data_fsz;
        bfill(&data, 0, (size_t)(off - data_fsz));
        badd(&data, idata.p, (size_t)idata_vs);
        data_vs = off + idata_vs;
        data_raw = rup(data_vs, PE_FA);
        bfill(&data, 0, (size_t)(data_raw - data_vs));
        data_fsz = data_vs;
        idata.n = 0;
        idata_raw = 0;
    }

    Bytes edata = {0}, reloc = {0};
    i64 edata_va = rup(img_end, PE_SA), edata_vs = 0, edata_raw = 0;
    i64 reloc_va = 0, reloc_vs = 0, reloc_raw = 0;
    int ed_dir = 0, rl_dir = 0, tail_in_text = 0;
    i64 ed_sz = 0, rl_sz = 0;
    if (dll) {
        pe_relocs(o, &reloc);
        reloc_vs = (i64)reloc.n;
        i64 ed_off = rup(text_vs, 4);
        pe_exports(o, &edata, text_va + ed_off);
        edata_vs = (i64)edata.n;
        i64 rl_off = rup(ed_off + edata_vs, 4);
        if (rl_off + reloc_vs <= rup(text_vs, PE_SA) - 0 && (edata_vs || reloc_vs) && (!has_data || text_va + rl_off + reloc_vs <= data_va)) {
            edata_va = text_va + ed_off;
            reloc_va = text_va + rl_off;
            bfill(&text, 0, 0);
            text.n = (size_t)text_vs;
            bfill(&text, 0, (size_t)(ed_off - text_vs));
            badd(&text, edata.p, edata.n);
            bfill(&text, 0, (size_t)(rl_off - ed_off - edata_vs));
            badd(&text, reloc.p, reloc.n);
            text_vs = rl_off + reloc_vs;
            text_raw = rup(text_vs, PE_FA);
            bfill(&text, 0, (size_t)(text_raw - text_vs));
            ed_dir = edata_vs > 0;
            rl_dir = reloc_vs > 0;
            ed_sz = edata_vs;
            rl_sz = reloc_vs;
            tail_in_text = 1;
            edata.n = reloc.n = 0;
            edata_vs = reloc_vs = 0;
        } else {
            edata.n = 0;
            pe_exports(o, &edata, edata_va);
            edata_vs = (i64)edata.n;
            edata_raw = rup(edata_vs, PE_FA);
            bfill(&edata, 0, (size_t)(edata_raw - edata_vs));
            reloc_va = edata_vs ? rup(edata_va + edata_vs, PE_SA) : edata_va;
            reloc_raw = rup(reloc_vs, PE_FA);
            bfill(&reloc, 0, (size_t)(reloc_raw - reloc_vs));
            ed_dir = edata_vs > 0;
            rl_dir = reloc_vs > 0;
            ed_sz = edata_vs;
            rl_sz = reloc_vs;
        }
    }
    (void)tail_in_text;
    int has_ed = edata_vs > 0, has_rl = reloc_vs > 0;
    if (!has_ed && !tail_in_text) reloc_va = edata_va;
    int nsec = 1 + (has_data ? 1 : 0) + (has_id ? 1 : 0) + has_ed + has_rl;
    i64 hdr_raw = rup(64 + 24 + (o->is64 ? 240 : 224) + 40 * nsec, PE_FA);
    i64 text_ptr = hdr_raw;
    i64 data_ptr = text_ptr + text_raw;
    i64 idata_ptr = data_ptr + data_raw;
    i64 edata_ptr = idata_ptr + idata_raw;
    i64 reloc_ptr = edata_ptr + edata_raw;
    i64 image_size = rup(img_end, PE_SA);
    if (has_ed) image_size = rup(edata_va + edata_vs, PE_SA);
    if (has_rl) image_size = rup(reloc_va + reloc_vs, PE_SA);
    if (rup(text_va + text_vs, PE_SA) > image_size) image_size = rup(text_va + text_vs, PE_SA);

    Bytes f = {0};
    u8 mz[64] = { 'M', 'Z' };
    mz[0x3C] = 0x40;
    badd(&f, mz, 64);
    badd(&f, "PE\0\0", 4);
    put16(&f, o->is64 ? 0x8664 : 0x14C);
    put16(&f, (u16)nsec);
    put32(&f, 0);
    put32(&f, 0);
    put32(&f, 0);
    put16(&f, o->is64 ? 240 : 224);
    put16(&f, dll ? (o->is64 ? 0x2022 : 0x2102) : (o->is64 ? 0x0022 : 0x0103));
    put16(&f, o->is64 ? 0x20B : 0x10B);
    bad1(&f, 1);
    bad1(&f, 0);
    put32(&f, (u32)text_raw);
    put32(&f, (u32)(data_raw + idata_raw + edata_raw + reloc_raw));
    put32(&f, (u32)(data_vs > data_fsz ? data_vs - data_fsz : 0));
    put32(&f, o->entry ? (u32)(entry - PE_BASE) : 0);
    put32(&f, PE_SA);
    if (o->is64) put64(&f, PE_BASE);
    else {
        put32(&f, (u32)(has_data ? data_va : PE_SA));
        put32(&f, PE_BASE);
    }
    put32(&f, PE_SA);
    put32(&f, PE_FA);
    if (o->is64) { put16(&f, 6); put16(&f, 0); } else { put16(&f, 4); put16(&f, 0); }
    put16(&f, 0); put16(&f, 0);
    if (o->is64) { put16(&f, 6); put16(&f, 0); } else { put16(&f, 4); put16(&f, 0); }
    put32(&f, 0);
    put32(&f, (u32)image_size);
    put32(&f, (u32)hdr_raw);
    put32(&f, 0);
    put16(&f, dll ? 2 : 3);
    put16(&f, 0);
    if (o->is64) {
        put64(&f, 0x100000); put64(&f, 0x1000);
        put64(&f, 0x100000); put64(&f, 0x1000);
    } else {
        put32(&f, 0x100000); put32(&f, 0x1000);
        put32(&f, 0x100000); put32(&f, 0x1000);
    }
    put32(&f, 0);
    put32(&f, 16);
    for (int i = 0; i < 16; i++) {
        if (i == 0 && ed_dir) { put32(&f, (u32)edata_va); put32(&f, (u32)ed_sz); }
        else if (i == 5 && rl_dir) { put32(&f, (u32)reloc_va); put32(&f, (u32)rl_sz); }
        else if (i == 1 && nd > 0) { put32(&f, (u32)idata_va); put32(&f, (u32)((nd + 1) * 20)); }
        else if (i == 12 && nd > 0) { put32(&f, (u32)iat_rva); put32(&f, (u32)iat_size); }
        else { put32(&f, 0); put32(&f, 0); }
    }
    PSec s;
    memset(&s, 0, sizeof s);
    memcpy(s.name, ".text", 5);
    s.vsize = (u32)text_vs; s.rva = (u32)text_va; s.rawsize = (u32)text_raw; s.rawptr = (u32)text_ptr; s.chars = 0x60000020;
    put_sec(&f, &s);
    if (has_data) {
        memset(&s, 0, sizeof s);
        memcpy(s.name, ".data", 5);
        s.vsize = (u32)data_vs; s.rva = (u32)data_va; s.rawsize = (u32)data_raw; s.rawptr = (u32)(data_raw ? data_ptr : 0); s.chars = 0xC0000040;
        put_sec(&f, &s);
    }
    if (has_id) {
        memset(&s, 0, sizeof s);
        memcpy(s.name, ".idata", 6);
        s.vsize = (u32)idata_vs; s.rva = (u32)idata_va; s.rawsize = (u32)idata_raw; s.rawptr = (u32)idata_ptr; s.chars = 0xC0000040;
        put_sec(&f, &s);
    }
    if (has_ed) {
        memset(&s, 0, sizeof s);
        memcpy(s.name, ".edata", 6);
        s.vsize = (u32)edata_vs; s.rva = (u32)edata_va; s.rawsize = (u32)edata_raw; s.rawptr = (u32)edata_ptr; s.chars = 0x40000040;
        put_sec(&f, &s);
    }
    if (has_rl) {
        memset(&s, 0, sizeof s);
        memcpy(s.name, ".reloc", 6);
        s.vsize = (u32)reloc_vs; s.rva = (u32)reloc_va; s.rawsize = (u32)reloc_raw; s.rawptr = (u32)reloc_ptr; s.chars = 0x42000040;
        put_sec(&f, &s);
    }
    while ((i64)f.n < hdr_raw) bad1(&f, 0);
    badd(&f, text.p, text.n);
    badd(&f, data.p, data.n);
    badd(&f, idata.p, idata.n);
    badd(&f, edata.p, edata.n);
    badd(&f, reloc.p, reloc.n);
    badd(out, f.p, f.n);
    return 1;
}
