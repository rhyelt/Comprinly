#include "cly.h"

typedef struct { int x64, other; } SysPair;

static const SysPair map_linux[] = {
    {0,3},{1,4},{2,5},{3,6},{4,106},{5,108},{6,107},{7,168},{8,19},{9,192},{10,125},{11,91},{12,45},
    {13,174},{14,175},{16,54},{17,180},{18,181},{19,145},{20,146},{21,33},{22,42},{23,142},{24,158},
    {25,163},{26,144},{28,219},{32,41},{33,63},{34,29},{35,162},{36,105},{37,27},{38,104},{39,20},
    {40,187},{57,2},{58,190},{59,11},{60,1},{61,114},{62,37},{63,122},{72,55},{73,143},{74,118},
    {75,148},{76,92},{77,93},{78,141},{79,183},{80,12},{81,133},{82,38},{83,39},{84,40},{85,8},
    {86,9},{87,10},{88,83},{89,85},{90,15},{91,94},{92,182},{93,207},{94,198},{95,60},{96,78},
    {97,191},{98,77},{99,116},{100,43},{102,199},{104,200},{105,213},{106,214},{107,201},{108,202},
    {109,57},{110,64},{111,65},{112,66},{115,205},{116,206},{121,132},{124,147},{125,184},{126,185},
    {131,186},{132,30},{133,14},{135,136},{137,99},{138,100},{140,96},{141,97},{160,75},{161,61},
    {162,36},{165,21},{166,52},{169,88},{170,74},{186,224},{200,238},{201,13},{202,240},{204,242},
    {217,220},{218,258},{228,265},{229,266},{230,267},{231,252},{234,270},{257,295},{258,296},
    {262,300},{263,301},{273,311},{291,329},{302,340},{318,355},{-1,-1}
};

static const SysPair map_mac[] = {
    {0,3},{1,4},{3,6},{10,74},{11,73},{16,54},{19,120},{20,121},{21,33},{22,42},{23,93},{28,75},
    {32,41},{33,90},{39,20},{57,2},{59,59},{60,1},{61,7},{62,37},{72,92},{74,95},{77,201},{80,12},
    {81,13},{82,128},{83,136},{84,137},{86,9},{87,10},{88,57},{89,58},{90,15},{91,124},{92,16},
    {95,60},{97,194},{98,117},{102,24},{104,47},{107,25},{108,43},{110,39},{112,147},{160,195},
    {162,36},{231,1},{-1,-1}
};

static void add_table(Str *s, const SysPair *m, int bits)
{
    int mx = 0;
    for (int i = 0; m[i].x64 >= 0; i++) if (m[i].x64 > mx) mx = m[i].x64;
    int n = mx + 1;
    int *t = xmalloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++) t[i] = 0xFFFF;
    for (int i = 0; m[i].x64 >= 0; i++) t[m[i].x64] = m[i].other;
    sfmt(s, "bits %d\nsection .data\n", bits);
    sfmt(s, "__cly_nsys equ %d\n__cly_sysmap:\n", n);
    for (int i = 0; i < n; i += 16) {
        sadd(s, "dw ");
        for (int k = i; k < i + 16 && k < n; k++) sfmt(s, "%s%d", k > i ? "," : "", t[k]);
        sadd(s, "\n");
    }
    free(t);
}

static const SysPair map_mac64[] = {
    {0,3},{1,4},{3,6},{8,199},{10,74},{11,73},{16,54},{19,120},{20,121},{21,33},{22,42},{23,93},{28,75},
    {32,41},{33,90},{39,20},{57,2},{59,59},{60,1},{61,7},{62,37},{72,92},{74,95},{77,201},{80,12},
    {81,13},{82,128},{83,136},{84,137},{86,9},{87,10},{88,57},{89,58},{90,15},{91,124},{92,16},
    {95,60},{97,194},{98,117},{102,24},{104,47},{107,25},{108,43},{110,39},{112,147},{160,195},
    {162,36},{231,1},{-1,-1}
};

static void add_keys(Str *s, int bits)
{
    sfmt(s, "bits %d\n", bits);
    static const char nrm[] = "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shf[] = "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    const char *tabs[2] = { nrm, shf };
    const char *names[2] = { "__cly_ktab", "__cly_ktab_s" };
    sadd(s, "section .data\n");
    for (int t = 0; t < 2; t++) {
        sfmt(s, "%s:\n", names[t]);
        for (int i = 0; i < 58; i += 16) {
            sadd(s, "db ");
            for (int k = i; k < i + 16 && k < 58; k++) sfmt(s, "%s%d", k > i ? "," : "", (unsigned char)tabs[t][k]);
            sadd(s, "\n");
        }
    }
}

int rt_build(int kernel, int type, int bits, Str *src)
{
    (void)type;
    int b64 = bits == 64;
    switch (kernel) {
    case K_LINUX:
        if (b64) return 1;
        add_table(src, map_linux, 32);
        sadd(src, rt_src_linux);
        return 1;
    case K_MAC:
        add_table(src, b64 ? map_mac64 : map_mac, b64 ? 64 : 32);
        sadd(src, b64 ? rt_src_mac64 : rt_src_mac);
        return 1;
    case K_WINDOWS:
        sadd(src, b64 ? rt_src_win64 : rt_src_win);
        return 1;
    case K_BARE:
        sadd(src, b64 ? rt_src_bare64 : rt_src_bare);
        add_keys(src, bits);
        return 1;
    }
    return 0;
}

int rt_boot(Str *s, u32 entry, int nsect, u32 bss, u32 bsslen, int bits)
{
    sadd(s,
        "org 0x7C00\n"
        "bits 16\n"
        "cli\n"
        "xor ax, ax\n"
        "mov ds, ax\n"
        "mov es, ax\n"
        "mov ss, ax\n"
        "mov sp, 0x7C00\n"
        "sti\n"
        "mov [drive], dl\n"
        "mov ah, 8\n"
        "int 0x13\n"
        "jc geo_bad\n"
        "and cl, 0x3F\n"
        "mov [spt], cl\n"
        "inc dh\n"
        "mov [heads], dh\n"
        "jmp geo_ok\n"
        "geo_bad:\n"
        "mov byte [spt], 18\n"
        "mov byte [heads], 2\n"
        "geo_ok:\n"
        "xor ax, ax\n"
        "mov es, ax\n"
        "rd_next:\n"
        "mov dl, [drive]\n"
        "mov si, dap\n"
        "mov ah, 0x42\n"
        "int 0x13\n"
        "jnc rd_ok\n"
        "mov ax, [dap_lba]\n"
        "xor dx, dx\n"
        "movzx cx, byte [spt]\n"
        "div cx\n"
        "inc dx\n"
        "mov cl, dl\n"
        "xor dx, dx\n"
        "movzx bx, byte [heads]\n"
        "div bx\n"
        "mov ch, al\n"
        "shl ah, 6\n"
        "or cl, ah\n"
        "mov dh, dl\n"
        "mov dl, [drive]\n"
        "mov ax, [dap_seg]\n"
        "mov es, ax\n"
        "xor bx, bx\n"
        "mov ax, 0x0201\n"
        "int 0x13\n"
        "jc rd_fail\n"
        "rd_ok:\n"
        "add word [dap_seg], 0x20\n"
        "inc word [dap_lba]\n"
        "dec word [count]\n"
        "jnz rd_next\n"
        "mov ax, 0x2401\n"
        "int 0x15\n"
        "in al, 0x92\n"
        "or al, 2\n"
        "and al, 0xFE\n"
        "out 0x92, al\n"
        "mov ax, 3\n"
        "int 0x10\n"
        "cli\n"
        "lgdt [gdtr]\n"
        "mov eax, cr0\n"
        "or al, 1\n"
        "mov cr0, eax\n"
        "jmp 0x08:pm\n"
        "rd_fail:\n"
        "mov si, errmsg\n"
        "er_l:\n"
        "lodsb\n"
        "test al, al\n"
        "jz er_h\n"
        "mov ah, 0x0E\n"
        "int 0x10\n"
        "jmp er_l\n"
        "er_h:\n"
        "hlt\n"
        "jmp er_h\n"
        "bits 32\n"
        "pm:\n"
        "mov ax, 0x10\n"
        "mov ds, ax\n"
        "mov es, ax\n"
        "mov fs, ax\n"
        "mov gs, ax\n"
        "mov ss, ax\n"
        "mov esp, 0x7C00\n");
    if (bits == 64) {
        sadd(s,
            "mov edi, 0x1000\n"
            "xor eax, eax\n"
            "mov ecx, 0xC00\n"
            "cld\n"
            "rep stosd\n"
            "mov dword [0x1000], 0x2003\n"
            "mov dword [0x2000], 0x3003\n"
            "mov edi, 0x3000\n"
            "mov eax, 0x83\n"
            "mov ecx, 512\n"
            "pt_l:\n"
            "mov [edi], eax\n"
            "add eax, 0x200000\n"
            "add edi, 8\n"
            "loop pt_l\n"
            "mov eax, 0x1000\n"
            "mov cr3, eax\n"
            "mov eax, cr4\n"
            "or al, 0x20\n"
            "mov cr4, eax\n"
            "mov ecx, 0xC0000080\n"
            "rdmsr\n"
            "or ah, 1\n"
            "wrmsr\n"
            "mov eax, cr0\n"
            "or eax, 0x80000000\n"
            "mov cr0, eax\n"
            "jmp 0x18:lm\n"
            "bits 64\n"
            "lm:\n");
        sfmt(s, "mov edi, %u\nmov ecx, %u\nxor eax, eax\nrep stosb\nmov eax, %u\njmp rax\n", bss, bsslen, entry);
    } else {
        sfmt(s, "mov edi, %u\nmov ecx, %u\nxor eax, eax\ncld\nrep stosb\nmov eax, %u\njmp eax\n", bss, bsslen, entry);
    }
    sadd(s,
        "gdt:\n"
        "dq 0\n"
        "dq 0x00CF9A000000FFFF\n"
        "dq 0x00CF92000000FFFF\n"
        "dq 0x00AF9A000000FFFF\n"
        "gdtr:\n"
        "dw 31\n"
        "dd gdt\n"
        "drive: db 0\n"
        "spt: db 0\n"
        "heads: db 0\n"
        "errmsg: db 'Disk read error', 0\n");
    sfmt(s, "count: dw %d\n", nsect);
    sadd(s,
        "dap: db 16, 0\n"
        "dw 1\n"
        "dw 0\n"
        "dap_seg: dw 0x0800\n"
        "dap_lba: dd 1, 0\n"
        "times 510-($-$$) db 0\n"
        "dw 0xAA55\n");
    return 1;
}
