#include "cly.h"
#include "insn.h"

HT g_etab;
static int cur_level;

static const struct { const char *n; int id; } specnames[] = {
    { "r8", S_R8 }, { "r16", S_R16 }, { "r32", S_R32 },
    { "rr8", S_RR8 }, { "rr16", S_RR16 }, { "rr32", S_RR32 },
    { "rm8", S_RM8 }, { "rm16", S_RM16 }, { "rm32", S_RM32 },
    { "m", S_M }, { "m8", S_M8 }, { "m16", S_M16 }, { "m32", S_M32 }, { "m64", S_M64 },
    { "m80", S_M80 }, { "m128", S_M128 }, { "m256", S_M256 }, { "mfar", S_MFAR },
    { "mo8", S_MO8 }, { "mo16", S_MO16 }, { "mo32", S_MO32 },
    { "imm8", S_IMM8 }, { "imm16", S_IMM16 }, { "imm32", S_IMM32 },
    { "sb16", S_SB16 }, { "sb32", S_SB32 }, { "one", S_ONE },
    { "al", S_AL }, { "cl", S_CL }, { "dx", S_DX }, { "ax", S_AX }, { "eax", S_EAX },
    { "es", S_ES }, { "cs", S_CS }, { "ss", S_SS }, { "ds", S_DS }, { "fs", S_FS }, { "gs", S_GS },
    { "sreg", S_SREG }, { "cr", S_CR }, { "dr", S_DR }, { "tr", S_TR },
    { "mm", S_MM }, { "mmr", S_MMR }, { "mm32", S_MM32 }, { "mm64", S_MM64 },
    { "xmm", S_XMM }, { "xmmr", S_XMMR }, { "xm16", S_XM16 }, { "xm32", S_XM32 }, { "xm64", S_XM64 }, { "xm128", S_XM128 },
    { "ymm", S_YMM }, { "ymmr", S_YMMR }, { "ym256", S_YM256 },
    { "st0", S_ST0 }, { "sti", S_STI }, { "xmm0", S_XMM0 },
    { "rel8", S_REL8 }, { "rel", S_REL }, { "far", S_FARPTR },
    { "r32m16", S_R32M16 }, { "r32m8", S_R32M8 },
    { "m512", S_M512 }, { "bnd", S_BND }, { "bndr", S_BNDR },
    { "r64", S_R64 }, { "rr64", S_RR64 }, { "rm64", S_RM64 }, { "rax", S_RAX }, { "rcx", S_RCX },
    { "imm64", S_IMM64 }, { "sd", S_SD }, { "ud", S_UD }, { "mo64", S_MO64 }, { "r32x", S_R32X }, { "rm32x", S_RM32X }, { "sb64", S_SB64 }, { "ecx", S_ECX }, { "edx", S_EDX }, { "cx", S_CX }, { "ibs", S_IBS }, { "ibu", S_IBU },
    { NULL, 0 }
};

static int vec_spec(const char *n, int len)
{
    if (len < 2 || n[0] != 'v') return -1;
    int cls = n[1] == 'x' ? 0 : n[1] == 'y' ? 1 : n[1] == 'z' ? 2 : n[1] == 'k' ? 3 : -1;
    if (cls >= 0 && len == 2) return SV(cls, 0, 0);
    if (cls >= 0 && isdigit((unsigned char)n[2])) {
        int sz = atoi(n + 2);
        int code = 0;
        for (int c = 1; c <= 7; c++) if ((1 << (c - 1)) * 1 == sz) code = c;
        if (!code) return -1;
        return SV(cls, code, 0);
    }
    if (n[1] == 's' && len >= 4) {
        int c2 = n[2] == 'x' ? 0 : n[2] == 'y' ? 1 : n[2] == 'z' ? 2 : -1;
        if (c2 < 0) return -1;
        return SV(c2, 0, 1);
    }
    return -1;
}

static int spec_id(const char *n, int len)
{
    {
        int v = vec_spec(n, len);
        if (v >= 0) return v;
    }
    for (int i = 0; specnames[i].n; i++)
        if ((int)strlen(specnames[i].n) == len && !strncmp(specnames[i].n, n, (size_t)len)) return specnames[i].id;
    fprintf(stderr, "cly: internal: bad spec %.*s\n", len, n);
    exit(3);
}

int spec_msize(int s)
{
    switch (s) {
    case S_RM8: case S_M8: case S_MO8: case S_R32M8: return 1;
    case S_RM16: case S_M16: case S_MO16: case S_XM16: case S_R32M16: return 2;
    case S_RM32: case S_M32: case S_MO32: case S_XM32: case S_MM32: case S_RM32X: return 4;
    case S_M64: case S_MM64: case S_XM64: case S_RM64: case S_MO64: return 8;
    case S_M80: return 10;
    case S_M128: case S_XM128: return 16;
    case S_M256: case S_YM256: return 32;
    default: return 0;
    }
}

void T(const char *mn, const char *ops, const char *enc)
{
    Ent *e = xcalloc(1, sizeof *e);
    e->mn = mn;
    e->level = (u8)cur_level;
    e->enc = xstrdup(enc);
    const char *p = ops;
    int n = 0;
    while (*p) {
        const char *q = p;
        while (*q && *q != ',') q++;
        const char *nm_end = p;
        while (nm_end < q && *nm_end != '|' && *nm_end != '*') nm_end++;
        e->sp[n] = (u16)spec_id(p, (int)(nm_end - p));
        const char *m = nm_end;
        while (m < q) {
            if (*m == '*') { e->dc[n] |= DC_STAR; m++; continue; }
            m++;
            const char *me = m;
            while (me < q && *me != '|' && *me != '*') me++;
            size_t ml = (size_t)(me - m);
            if (ml == 4 && !strncmp(m, "mask", 4)) e->dc[n] |= DC_MASK;
            else if (ml == 1 && *m == 'z') e->dc[n] |= DC_Z;
            else if (ml == 2 && !strncmp(m, "er", 2)) e->dc[n] |= DC_ER;
            else if (ml == 3 && !strncmp(m, "sae", 3)) e->dc[n] |= DC_SAE;
            else if (ml == 3 && m[0] == 'b') {
                int b = atoi(m + 1);
                e->dc[n] |= DC_BC;
                e->dc[n] = (u8)((e->dc[n] & 0x3f) | ((b == 16 ? 1 : b == 32 ? 2 : 3) << 6));
            }
            m = me;
        }
        n++;
        p = *q ? q + 1 : q;
    }
    e->nops = n;
    for (const char *t = enc; *t; ) {
        if (!strncmp(t, "o16", 3)) e->osz = 16;
        else if (!strncmp(t, "o32", 3)) e->osz = 32;
        else if (!strncmp(t, "only64", 6)) e->mode = 1;
        else if (!strncmp(t, "no64", 4)) e->mode = 2;
        while (*t && *t != ' ') t++;
        while (*t == ' ') t++;
    }
    for (int i = 0; i < n; i++) if (!e->msize) e->msize = (u8)spec_msize(e->sp[i]);
    for (const char *t = enc; *t; ) {
        if (!strncmp(t, "xv:", 3)) {
            char k = t[3];
            int L, pp, mp, W;
            char tu[8] = "";
            sscanf(t + 5, "%d:%d:%d:%d:%7[a-z0-9]", &L, &pp, &mp, &W, tu);
            e->vkind = k == 'E' ? VK_EVEX : (k == 'X' ? VK_XOP : VK_VEX);
            e->vL = (u8)L; e->vpp = (u8)pp; e->vmap = (u8)mp; e->vW = (u8)W;
            static const struct { const char *n; int v; } tt[] = {
                { "fv", TU_FV }, { "hv", TU_HV }, { "fvm", TU_FVM }, { "t1s", TU_T1S }, { "t1s8", TU_T1S8 }, { "t1s16", TU_T1S16 },
                { "t1f32", TU_T1F32 }, { "t1f64", TU_T1F64 }, { "t2", TU_T2 }, { "t4", TU_T4 }, { "t8", TU_T8 }, { "hvm", TU_HVM },
                { "qvm", TU_QVM }, { "ovm", TU_OVM }, { "m128", TU_M128 }, { "dup", TU_DUP }, { NULL, 0 }
            };
            for (int i = 0; tt[i].n; i++) if (!strcmp(tt[i].n, tu)) e->tuple = (u8)tt[i].v;
        } else if (!strncmp(t, "rl:", 3)) {
            size_t l = 0;
            while (t[3 + l] && t[3 + l] != ' ' && l < 7) { e->rl[l] = t[3 + l]; l++; }
        } else if (!strncmp(t, "dup", 3) && (t[3] == ' ' || !t[3])) e->dup = 1;
        else if (!strncmp(t, "late", 4) && (t[4] == ' ' || !t[4])) e->late = 1;
        else if (!strncmp(t, "sf", 2) && (t[2] == ' ' || !t[2])) e->sflag = 1;
        while (*t && *t != ' ') t++;
        while (*t == ' ') t++;
    }
    int len = 0;
    for (const char *t = enc; *t; ) {
        int tl = 0;
        while (t[tl] && t[tl] != ' ') tl++;
        if (t[0] == '=') len++;
                else if (isxdigit((unsigned char)t[0]) && isxdigit((unsigned char)t[1]) && (t[2] == ' ' || t[2] == 0 || t[2] == '+') && !(tl == 2 && !strncmp(t, "cb", 2)) && !(tl == 2 && !strncmp(t, "cd", 2))) len++;
        while (*t && *t != ' ') t++;
        while (*t == ' ') t++;
    }
    e->fixedlen = (u8)(len + 1);
    Ent *h = ht_get(&g_etab, mn);
    if (!h) ht_put(&g_etab, mn, e);
    else { while (h->next) h = h->next; h->next = e; }
}

void TF(const char *mn, const char *ops, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    T(xstrdup(mn), ops, xstrdup(buf));
}

#define LV(n) (cur_level = (n))
void tab_lv(int n) { cur_level = n; }

static void alu_tab(void)
{
    static const char *alu[] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
    for (int k = 0; k < 8; k++) {
        const char *m = alu[k];
        int b = k * 8;
        TF(m, "rm8,r8", "%02x /r", b);
        TF(m, "rm16,r16", "o16 %02x /r", b + 1);
        TF(m, "rm32,r32", "o32 %02x /r", b + 1);
        TF(m, "r8,rm8", "%02x /r", b + 2);
        TF(m, "r16,rm16", "o16 %02x /r", b + 3);
        TF(m, "r32,rm32", "o32 %02x /r", b + 3);
        TF(m, "rm16,sb16", "o16 83 /%d ib", k);
        TF(m, "rm32,sb32", "o32 83 /%d ib", k);
        TF(m, "al,imm8", "%02x ib", b + 4);
        TF(m, "ax,imm16", "o16 %02x iw", b + 5);
        TF(m, "eax,imm32", "o32 %02x id", b + 5);
        TF(m, "rm8,imm8", "80 /%d ib", k);
        TF(m, "rm16,imm16", "o16 81 /%d iw", k);
        TF(m, "rm32,imm32", "o32 81 /%d id", k);
    }
}

static const char *ccn[] = { "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g" };
static const struct { const char *n; int c; } ccal[] = {
    { "o", 0 }, { "no", 1 }, { "b", 2 }, { "c", 2 }, { "nae", 2 }, { "ae", 3 }, { "nb", 3 }, { "nc", 3 },
    { "e", 4 }, { "z", 4 }, { "ne", 5 }, { "nz", 5 }, { "be", 6 }, { "na", 6 }, { "a", 7 }, { "nbe", 7 },
    { "s", 8 }, { "ns", 9 }, { "p", 10 }, { "pe", 10 }, { "np", 11 }, { "po", 11 },
    { "l", 12 }, { "nge", 12 }, { "ge", 13 }, { "nl", 13 }, { "le", 14 }, { "ng", 14 }, { "g", 15 }, { "nle", 15 },
    { NULL, 0 }
};

static void int_tab(void)
{
    LV(0);
    alu_tab();
    TF("mov", "al,mo8", "a0 om");
    TF("mov", "ax,mo16", "o16 a1 om");
    TF("mov", "eax,mo32", "o32 a1 om");
    TF("mov", "mo8,al", "a2 om");
    TF("mov", "mo16,ax", "o16 a3 om");
    TF("mov", "mo32,eax", "o32 a3 om");
    T("mov", "rm8,r8", "88 /r");
    T("mov", "rm16,r16", "o16 89 /r");
    T("mov", "rm32,r32", "o32 89 /r");
    T("mov", "r8,rm8", "8a /r");
    T("mov", "r16,rm16", "o16 8b /r");
    T("mov", "r32,rm32", "o32 8b /r");
    T("mov", "m16,sreg", "8c /r");
    T("mov", "rr16,sreg", "o16 8c /r");
    T("mov", "rr32,sreg", "o32 8c /r");
    T("mov", "sreg,rm16", "8e /r");
    T("mov", "sreg,rr32", "8e /r");
    T("mov", "r8,imm8", "b0+r ib");
    T("mov", "r16,imm16", "o16 b8+r iw");
    T("mov", "r32,imm32", "o32 b8+r id");
    T("mov", "rm8,imm8", "c6 /0 ib");
    T("mov", "rm16,imm16", "o16 c7 /0 iw");
    T("mov", "rm32,imm32", "o32 c7 /0 id");
    LV(3);
    T("mov", "rr32,cr", "0f 20 /r");
    T("mov", "cr,rr32", "0f 22 /r");
    T("mov", "rr32,dr", "0f 21 /r");
    T("mov", "dr,rr32", "0f 23 /r");
    T("mov", "rr32,tr", "0f 24 /r");
    T("mov", "tr,rr32", "0f 26 /r");
    LV(0);
    T("push", "r16", "o16 50+r");
    T("push", "r32", "o32 50+r");
    T("push", "rm16", "o16 ff /6");
    T("push", "rm32", "o32 ff /6");
    T("push", "cs", "0e");
    T("push", "ss", "16");
    T("push", "ds", "1e");
    T("push", "es", "06");
    LV(1);
    T("push", "sb16", "o16 6a ib");
    T("push", "sb32", "o32 6a ib");
    T("push", "imm16", "o16 68 iw");
    T("push", "imm32", "o32 68 id");
    LV(3);
    T("push", "fs", "0f a0");
    T("push", "gs", "0f a8");
    LV(0);
    T("pop", "r16", "o16 58+r");
    T("pop", "r32", "o32 58+r");
    T("pop", "rm16", "o16 8f /0");
    T("pop", "rm32", "o32 8f /0");
    T("pop", "ds", "1f");
    T("pop", "es", "07");
    T("pop", "ss", "17");
    LV(3);
    T("pop", "fs", "0f a1");
    T("pop", "gs", "0f a9");
    LV(0);
    T("xchg", "ax,r16", "o16 90+r");
    T("xchg", "r16,ax", "o16 90+r");
    T("xchg", "eax,r32", "o32 90+r");
    T("xchg", "r32,eax", "o32 90+r");
    T("xchg", "r8,rr8", "86 /r");
    T("xchg", "r16,rr16", "o16 87 /r");
    T("xchg", "r32,rr32", "o32 87 /r");
    T("xchg", "rm8,r8", "86 /r");
    T("xchg", "r8,rm8", "86 /r");
    T("xchg", "rm16,r16", "o16 87 /r");
    T("xchg", "r16,rm16", "o16 87 /r");
    T("xchg", "rm32,r32", "o32 87 /r");
    T("xchg", "r32,rm32", "o32 87 /r");
    T("lea", "r16,m", "o16 8d /r");
    T("lea", "r32,m", "o32 8d /r");
    T("les", "r16,m", "o16 c4 /r");
    T("les", "r32,m", "o32 c4 /r");
    T("lds", "r16,m", "o16 c5 /r");
    T("lds", "r32,m", "o32 c5 /r");
    LV(3);
    T("lss", "r16,m", "o16 0f b2 /r");
    T("lss", "r32,m", "o32 0f b2 /r");
    T("lfs", "r16,m", "o16 0f b4 /r");
    T("lfs", "r32,m", "o32 0f b4 /r");
    T("lgs", "r16,m", "o16 0f b5 /r");
    T("lgs", "r32,m", "o32 0f b5 /r");
    T("movzx", "r16,rm8", "o16 0f b6 /r");
    T("movzx", "r32,rm8", "o32 0f b6 /r");
    T("movzx", "r32,rm16", "o32 0f b7 /r");
    T("movzx", "r16,rm16", "o16 0f b7 /r");
    T("movsx", "r16,rm8", "o16 0f be /r");
    T("movsx", "r32,rm8", "o32 0f be /r");
    T("movsx", "r32,rm16", "o32 0f bf /r");
    T("movsx", "r16,rm16", "o16 0f bf /r");
    LV(0);
    T("inc", "r16", "o16 40+r");
    T("inc", "r32", "o32 40+r");
    T("inc", "rm8", "fe /0");
    T("inc", "rm16", "o16 ff /0");
    T("inc", "rm32", "o32 ff /0");
    T("dec", "r16", "o16 48+r");
    T("dec", "r32", "o32 48+r");
    T("dec", "rm8", "fe /1");
    T("dec", "rm16", "o16 ff /1");
    T("dec", "rm32", "o32 ff /1");
    static const char *un[] = { "not", "neg", "mul", "imul", "div", "idiv" };
    static const int und[] = { 2, 3, 4, 5, 6, 7 };
    for (int i = 0; i < 6; i++) {
        TF(un[i], "rm8", "f6 /%d", und[i]);
        TF(un[i], "rm16", "o16 f7 /%d", und[i]);
        TF(un[i], "rm32", "o32 f7 /%d", und[i]);
    }
    LV(3);
    T("imul", "r16,rm16", "o16 0f af /r");
    T("imul", "r32,rm32", "o32 0f af /r");
    LV(1);
    T("imul", "r16,rm16,sb16", "o16 6b /r ib");
    T("imul", "r32,rm32,sb32", "o32 6b /r ib");
    T("imul", "r16,rm16,imm16", "o16 69 /r iw");
    T("imul", "r32,rm32,imm32", "o32 69 /r id");
    T("imul", "r16,sb16", "o16 6b /r ib");
    T("imul", "r32,sb32", "o32 6b /r ib");
    T("imul", "r16,imm16", "o16 69 /r iw");
    T("imul", "r32,imm32", "o32 69 /r id");
    LV(0);
    T("test", "rm8,r8", "84 /r");
    T("test", "rm16,r16", "o16 85 /r");
    T("test", "rm32,r32", "o32 85 /r");
    T("test", "al,imm8", "a8 ib");
    T("test", "ax,imm16", "o16 a9 iw");
    T("test", "eax,imm32", "o32 a9 id");
    T("test", "rm8,imm8", "f6 /0 ib");
    T("test", "rm16,imm16", "o16 f7 /0 iw");
    T("test", "rm32,imm32", "o32 f7 /0 id");
    T("test", "r8,rm8", "84 /r");
    T("test", "r16,rm16", "o16 85 /r");
    T("test", "r32,rm32", "o32 85 /r");
    static const char *sh[] = { "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar" };
    static const int shd[] = { 0, 1, 2, 3, 4, 5, 4, 7 };
    for (int i = 0; i < 8; i++) {
        LV(0);
        TF(sh[i], "rm8,one", "d0 /%d", shd[i]);
        TF(sh[i], "rm8,cl", "d2 /%d", shd[i]);
        TF(sh[i], "rm16,one", "o16 d1 /%d", shd[i]);
        TF(sh[i], "rm16,cl", "o16 d3 /%d", shd[i]);
        TF(sh[i], "rm32,one", "o32 d1 /%d", shd[i]);
        TF(sh[i], "rm32,cl", "o32 d3 /%d", shd[i]);
        LV(1);
        TF(sh[i], "rm8,imm8", "c0 /%d ib", shd[i]);
        TF(sh[i], "rm16,imm8", "o16 c1 /%d ib", shd[i]);
        TF(sh[i], "rm32,imm8", "o32 c1 /%d ib", shd[i]);
    }
    LV(3);
    T("shld", "rm16,r16,imm8", "o16 0f a4 /r ib");
    T("shld", "rm16,r16,cl", "o16 0f a5 /r");
    T("shld", "rm32,r32,imm8", "o32 0f a4 /r ib");
    T("shld", "rm32,r32,cl", "o32 0f a5 /r");
    T("shrd", "rm16,r16,imm8", "o16 0f ac /r ib");
    T("shrd", "rm16,r16,cl", "o16 0f ad /r");
    T("shrd", "rm32,r32,imm8", "o32 0f ac /r ib");
    T("shrd", "rm32,r32,cl", "o32 0f ad /r");
    static const char *bt[] = { "bt", "bts", "btr", "btc" };
    static const int btop[] = { 0xa3, 0xab, 0xb3, 0xbb };
    for (int i = 0; i < 4; i++) {
        TF(bt[i], "rm16,r16", "o16 0f %02x /r", btop[i]);
        TF(bt[i], "rm32,r32", "o32 0f %02x /r", btop[i]);
        TF(bt[i], "rm16,imm8", "o16 0f ba /%d ib", 4 + i);
        TF(bt[i], "rm32,imm8", "o32 0f ba /%d ib", 4 + i);
    }
    T("bsf", "r16,rm16", "o16 0f bc /r");
    T("bsf", "r32,rm32", "o32 0f bc /r");
    T("bsr", "r16,rm16", "o16 0f bd /r");
    T("bsr", "r32,rm32", "o32 0f bd /r");
    LV(4);
    T("cmpxchg", "rm8,r8", "0f b0 /r");
    T("cmpxchg", "rm16,r16", "o16 0f b1 /r");
    T("cmpxchg", "rm32,r32", "o32 0f b1 /r");
    T("xadd", "rm8,r8", "0f c0 /r");
    T("xadd", "rm16,r16", "o16 0f c1 /r");
    T("xadd", "rm32,r32", "o32 0f c1 /r");
    T("bswap", "r32", "o32 0f c8+r");
    LV(5);
    T("cmpxchg8b", "m", "0f c7 /1");
    LV(0);
    for (int i = 0; ccal[i].n; i++) {
        int c = ccal[i].c;
        char mn[16];
        snprintf(mn, sizeof mn, "j%s", ccal[i].n);
        LV(0);
        TF(mn, "rel8", "%02x cb", 0x70 + c);
        LV(3);
        TF(mn, "rel", "0f %02x cz", 0x80 + c);
        snprintf(mn, sizeof mn, "set%s", ccal[i].n);
        TF(mn, "rm8", "0f %02x /0", 0x90 + c);
        LV(6);
        snprintf(mn, sizeof mn, "cmov%s", ccal[i].n);
        TF(mn, "r16,rm16", "o16 0f %02x /r", 0x40 + c);
        TF(mn, "r32,rm32", "o32 0f %02x /r", 0x40 + c);
    }
    (void)ccn;
    LV(0);
    T("jmp", "rel8", "eb cb");
    T("jmp", "rel", "e9 cz");
    T("jmp", "far", "ea fp");
    T("jmp", "mfar", "ff /5");
    T("jmp", "rm16", "o16 ff /4");
    T("jmp", "rm32", "o32 ff /4");
    T("call", "rel", "e8 cz");
    T("call", "far", "9a fp");
    T("call", "mfar", "ff /3");
    T("call", "rm16", "o16 ff /2");
    T("call", "rm32", "o32 ff /2");
    T("ret", "", "c3");
    T("retn", "", "c3");
    T("ret", "imm16", "c2 iw");
    T("retn", "imm16", "c2 iw");
    T("retf", "", "=cb");
    T("retf", "imm16", "ca iw");
    T("retw", "", "o16 c3");
    T("retd", "", "o32 c3");
    T("loop", "rel8", "e2 cb");
    T("loopw", "rel8", "a16 e2 cb");
    T("loopd", "rel8", "a32 e2 cb");
    T("loope", "rel8", "e1 cb");
    T("loopew", "rel8", "a16 e1 cb");
    T("looped", "rel8", "a32 e1 cb");
    T("loopz", "rel8", "e1 cb");
    T("loopzw", "rel8", "a16 e1 cb");
    T("loopzd", "rel8", "a32 e1 cb");
    T("loopne", "rel8", "e0 cb");
    T("loopnew", "rel8", "a16 e0 cb");
    T("loopned", "rel8", "a32 e0 cb");
    T("loopnz", "rel8", "e0 cb");
    T("loopnzw", "rel8", "a16 e0 cb");
    T("loopnzd", "rel8", "a32 e0 cb");
    T("jcxz", "rel8", "a16 e3 cb");
    T("jecxz", "rel8", "a32 e3 cb");
    LV(1);
    T("enter", "imm16,imm8", "c8 iw ib");
    T("leave", "", "c9");
    LV(0);
    T("int", "imm8", "=cd ib");
    T("int3", "", "cc");
    T("int1", "", "f1");
    T("icebp", "", "f1");
    T("into", "", "ce");
    T("iret", "", "cf");
    T("iretw", "", "o16 cf");
    T("iretd", "", "o32 cf");
    T("in", "al,imm8", "e4 ib");
    T("in", "ax,imm8", "o16 e5 ib");
    T("in", "eax,imm8", "o32 e5 ib");
    T("in", "al,dx", "ec");
    T("in", "ax,dx", "o16 ed");
    T("in", "eax,dx", "o32 ed");
    T("out", "imm8,al", "e6 ib");
    T("out", "imm8,ax", "o16 e7 ib");
    T("out", "imm8,eax", "o32 e7 ib");
    T("out", "dx,al", "ee");
    T("out", "dx,ax", "o16 ef");
    T("out", "dx,eax", "o32 ef");
    T("nop", "", "90");
    LV(6);
    T("nop", "rm16", "o16 0f 1f /0");
    T("nop", "rm32", "o32 0f 1f /0");
    LV(0);
    static const struct { const char *n, *e; int lv; } sing[] = {
        { "pushf", "9c", 0 }, { "pushfw", "o16 9c", 0 }, { "pushfd", "o32 9c", 3 },
        { "popf", "9d", 0 }, { "popfw", "o16 9d", 0 }, { "popfd", "o32 9d", 3 },
        { "pusha", "60", 1 }, { "pushaw", "o16 60", 1 }, { "pushad", "o32 60", 3 },
        { "popa", "61", 1 }, { "popaw", "o16 61", 1 }, { "popad", "o32 61", 3 },
        { "cbw", "o16 98", 0 }, { "cwde", "o32 98", 3 }, { "cwd", "o16 99", 0 }, { "cdq", "o32 99", 3 },
        { "sahf", "9e", 0 }, { "lahf", "9f", 0 }, { "xlat", "d7", 0 }, { "xlatb", "d7", 0 },
        { "daa", "27", 0 }, { "das", "2f", 0 }, { "aaa", "37", 0 }, { "aas", "3f", 0 },
        { "aam", "d4 0a", 0 }, { "aad", "d5 0a", 0 }, { "salc", "d6", 0 },
        { "hlt", "f4", 0 }, { "cmc", "f5", 0 }, { "clc", "f8", 0 }, { "stc", "f9", 0 },
        { "cli", "fa", 0 }, { "sti", "fb", 0 }, { "cld", "fc", 0 }, { "std", "fd", 0 },
        { "wait", "9b", 0 }, { "fwait", "9b", 0 },
        { "movsb", "a4", 0 }, { "movsw", "o16 a5", 0 }, { "movsd", "o32 a5", 3 },
        { "cmpsb", "a6", 0 }, { "cmpsw", "o16 a7", 0 }, { "cmpsd", "o32 a7", 3 },
        { "scasb", "ae", 0 }, { "scasw", "o16 af", 0 }, { "scasd", "o32 af", 3 },
        { "lodsb", "ac", 0 }, { "lodsw", "o16 ad", 0 }, { "lodsd", "o32 ad", 3 },
        { "stosb", "aa", 0 }, { "stosw", "o16 ab", 0 }, { "stosd", "o32 ab", 3 },
        { "insb", "6c", 1 }, { "insw", "o16 6d", 1 }, { "insd", "o32 6d", 3 },
        { "outsb", "6e", 1 }, { "outsw", "o16 6f", 1 }, { "outsd", "o32 6f", 3 },
        { "clts", "0f 06", 2 }, { "invd", "0f 08", 4 }, { "wbinvd", "0f 09", 4 },
        { "ud2", "0f 0b", 6 }, { "ud2a", "0f 0b", 6 }, { "ud2b", "0f b9", 6 },
        { "cpuid", "0f a2", 5 }, { "rdtsc", "0f 31", 5 }, { "rdmsr", "0f 32", 5 }, { "wrmsr", "0f 30", 5 },
        { "rdpmc", "0f 33", 5 }, { "rsm", "0f aa", 5 }, { "sysenter", "0f 34", 6 }, { "sysexit", "0f 35", 6 },
        { "loadall", "0f 07", 3 }, { "loadall286", "0f 05", 2 }, { "emms", "0f 77", 5 }, { "femms", "0f 0e", 5 },
        { "pause", "f3 90", 7 }, { "lfence", "0f ae e8", 8 }, { "mfence", "0f ae f0", 8 }, { "sfence", "0f ae f8", 7 },
        { "rdtscp", "0f 01 f9", 9 }, { "xgetbv", "0f 01 d0", 9 }, { "xsetbv", "0f 01 d1", 9 },
        { "monitor", "0f 01 c8", 9 }, { "mwait", "0f 01 c9", 9 }, { "swapgs", "0f 01 f8", 99 },
        { "vmcall", "0f 01 c1", 12 }, { "vmlaunch", "0f 01 c2", 12 }, { "vmresume", "0f 01 c3", 12 }, { "vmxoff", "0f 01 c4", 12 },
        { "sysret", "0f 07", 99 }, { "getsec", "0f 37", 12 }, { "rdpid", "", 99 },
        { "fnop", "d9 d0", 0 }, { "fchs", "d9 e0", 0 }, { "fabs", "d9 e1", 0 }, { "ftst", "d9 e4", 0 }, { "fxam", "d9 e5", 0 },
        { "fld1", "d9 e8", 0 }, { "fldl2t", "d9 e9", 0 }, { "fldl2e", "d9 ea", 0 }, { "fldpi", "d9 eb", 0 },
        { "fldlg2", "d9 ec", 0 }, { "fldln2", "d9 ed", 0 }, { "fldz", "d9 ee", 0 },
        { "f2xm1", "d9 f0", 0 }, { "fyl2x", "d9 f1", 0 }, { "fptan", "d9 f2", 0 }, { "fpatan", "d9 f3", 0 },
        { "fxtract", "d9 f4", 0 }, { "fprem1", "d9 f5", 3 }, { "fdecstp", "d9 f6", 0 }, { "fincstp", "d9 f7", 0 },
        { "fprem", "d9 f8", 0 }, { "fyl2xp1", "d9 f9", 0 }, { "fsqrt", "d9 fa", 0 }, { "fsincos", "d9 fb", 3 },
        { "frndint", "d9 fc", 0 }, { "fscale", "d9 fd", 0 }, { "fsin", "d9 fe", 3 }, { "fcos", "d9 ff", 3 },
        { "fcompp", "de d9", 0 }, { "fucompp", "da e9", 3 },
        { "fnclex", "db e2", 0 }, { "fclex", "9b db e2", 0 }, { "fninit", "db e3", 0 }, { "finit", "9b db e3", 0 },
        { "fnsetpm", "db e4", 2 }, { "fsetpm", "db e4", 2 }, { "feni", "9b db e0", 0 }, { "fdisi", "9b db e1", 0 }, { "fneni", "db e0", 0 }, { "fndisi", "db e1", 0 },
        { "frstpm", "db e5", 2 },
        { NULL, NULL, 0 }
    };
    for (int i = 0; sing[i].n; i++) {
        if (!sing[i].e[0]) continue;
        LV(sing[i].lv);
        T(sing[i].n, "", sing[i].e);
    }
    LV(0);
    T("aam", "imm8", "d4 ib");
    T("aad", "imm8", "d5 ib");
    T("bound", "r16,m", "o16 62 /r");
    T("bound", "r32,m", "o32 62 /r");
    LV(2);
    T("arpl", "rm16,r16", "63 /r");
    T("lar", "r16,rm16", "o16 0f 02 /r");
    T("lar", "r32,rm16", "o32 0f 02 /r");
    T("lsl", "r16,rm16", "o16 0f 03 /r");
    T("lsl", "r32,rm16", "o32 0f 03 /r");
    T("verr", "rm16", "0f 00 /4");
    T("verw", "rm16", "0f 00 /5");
    T("sldt", "m16", "0f 00 /0");
    T("sldt", "rr16", "o16 0f 00 /0");
    T("sldt", "rr32", "o32 0f 00 /0");
    T("str", "m16", "0f 00 /1");
    T("str", "rr16", "o16 0f 00 /1");
    T("str", "rr32", "o32 0f 00 /1");
    T("lldt", "rm16", "0f 00 /2");
    T("ltr", "rm16", "0f 00 /3");
    T("sgdt", "m", "0f 01 /0");
    T("sidt", "m", "0f 01 /1");
    T("lgdt", "m", "0f 01 /2");
    T("lidt", "m", "0f 01 /3");
    T("smsw", "m16", "0f 01 /4");
    T("smsw", "rr16", "o16 0f 01 /4");
    T("smsw", "rr32", "o32 0f 01 /4");
    T("lmsw", "rm16", "0f 01 /6");
    LV(4);
    T("invlpg", "m", "0f 01 /7");
    LV(5);
    T("rdrand", "rr16", "o16 0f c7 /6");
    T("rdrand", "rr32", "o32 0f c7 /6");
    T("rdseed", "rr16", "o16 0f c7 /7");
    T("rdseed", "rr32", "o32 0f c7 /7");
    LV(7);
    T("ldmxcsr", "m32", "0f ae /2");
    T("stmxcsr", "m32", "0f ae /3");
    T("fxsave", "m", "0f ae /0");
    T("fxrstor", "m", "0f ae /1");
    T("xsave", "m", "0f ae /4");
    T("xrstor", "m", "0f ae /5");
    T("xsaveopt", "m", "0f ae /6");
    T("clflush", "m8", "0f ae /7");
    T("prefetchnta", "m8", "0f 18 /0");
    T("prefetcht0", "m8", "0f 18 /1");
    T("prefetcht1", "m8", "0f 18 /2");
    T("prefetcht2", "m8", "0f 18 /3");
    T("prefetch", "m8", "0f 0d /0");
    T("prefetchw", "m8", "0f 0d /1");
    T("movnti", "m32,r32", "0f c3 /r");
    LV(5);
    T("movbe", "r16,m16", "o16 0f 38 f0 /r");
    T("movbe", "r32,m32", "o32 0f 38 f0 /r");
    T("movbe", "m16,r16", "o16 0f 38 f1 /r");
    T("movbe", "m32,r32", "o32 0f 38 f1 /r");
    LV(12);
    T("popcnt", "r16,rm16", "o16 f3 0f b8 /r");
    T("popcnt", "r32,rm32", "o32 f3 0f b8 /r");
    T("lzcnt", "r16,rm16", "o16 f3 0f bd /r");
    T("lzcnt", "r32,rm32", "o32 f3 0f bd /r");
    T("tzcnt", "r16,rm16", "o16 f3 0f bc /r");
    T("tzcnt", "r32,rm32", "o32 f3 0f bc /r");
    T("crc32", "r32,rm8", "f2 0f 38 f0 /r");
    T("crc32", "r32,rm16", "o16 f2 0f 38 f1 /r");
    T("crc32", "r32,rm32", "o32 f2 0f 38 f1 /r");
}

static void fpu_tab(void)
{
    LV(0);
    T("fld", "m32", "d9 /0");
    T("fld", "m64", "dd /0");
    T("fld", "m80", "db /5");
    T("fld", "sti", "d9 c0+i");
    T("fst", "m32", "d9 /2");
    T("fst", "m64", "dd /2");
    T("fst", "sti", "dd d0+i");
    T("fstp", "m32", "d9 /3");
    T("fstp", "m64", "dd /3");
    T("fstp", "m80", "db /7");
    T("fstp", "sti", "dd d8+i");
    T("fild", "m16", "df /0");
    T("fild", "m32", "db /0");
    T("fild", "m64", "df /5");
    T("fist", "m16", "df /2");
    T("fist", "m32", "db /2");
    T("fistp", "m16", "df /3");
    T("fistp", "m32", "db /3");
    T("fistp", "m64", "df /7");
    LV(9);
    T("fisttp", "m16", "df /1");
    T("fisttp", "m32", "db /1");
    T("fisttp", "m64", "dd /1");
    LV(0);
    T("fbld", "m80", "df /4");
    T("fbstp", "m80", "df /6");
    T("fldcw", "m16", "d9 /5");
    T("fnstcw", "m16", "d9 /7");
    T("fstcw", "m16", "9b d9 /7");
    T("fldenv", "m", "d9 /4");
    T("fnstenv", "m", "d9 /6");
    T("fstenv", "m", "9b d9 /6");
    T("frstor", "m", "dd /4");
    T("fnsave", "m", "dd /6");
    T("fsave", "m", "9b dd /6");
    T("fnstsw", "m16", "dd /7");
    T("fstsw", "m16", "9b dd /7");
    T("fnstsw", "ax", "df e0");
    T("fstsw", "ax", "9b df e0");
    T("fxch", "", "d9 c9");
    T("fxch", "sti", "d9 c8+i");
    T("fxch", "st0,sti", "d9 c8+i");
    T("fxch", "sti,st0", "d9 c8+i");
    T("ffree", "sti", "dd c0+i");
    T("ffreep", "sti", "df c0+i");
    T("fcom", "", "d8 d1");
    T("fcom", "m32", "d8 /2");
    T("fcom", "m64", "dc /2");
    T("fcom", "sti", "d8 d0+i");
    T("fcom", "st0,sti", "d8 d0+i");
    T("fcomp", "", "d8 d9");
    T("fcomp", "m32", "d8 /3");
    T("fcomp", "m64", "dc /3");
    T("fcomp", "sti", "d8 d8+i");
    T("fcomp", "st0,sti", "d8 d8+i");
    LV(3);
    T("fucom", "", "dd e1");
    T("fucom", "sti", "dd e0+i");
    T("fucom", "st0,sti", "dd e0+i");
    T("fucomp", "", "dd e9");
    T("fucomp", "sti", "dd e8+i");
    T("fucomp", "st0,sti", "dd e8+i");
    LV(6);
    T("fcomi", "sti", "db f0+i");
    T("fcomi", "st0,sti", "db f0+i");
    T("fcomip", "sti", "df f0+i");
    T("fcomip", "st0,sti", "df f0+i");
    T("fucomi", "sti", "db e8+i");
    T("fucomi", "st0,sti", "db e8+i");
    T("fucomip", "sti", "df e8+i");
    T("fucomip", "st0,sti", "df e8+i");
    static const char *cm[] = { "fcmovb", "fcmove", "fcmovbe", "fcmovu", "fcmovnb", "fcmovne", "fcmovnbe", "fcmovnu" };
    for (int i = 0; i < 8; i++) {
        int op = i < 4 ? 0xda : 0xdb;
        int lo = 0xc0 + (i & 3) * 8;
        TF(cm[i], "sti", "%02x %02x+i", op, lo);
        TF(cm[i], "st0,sti", "%02x %02x+i", op, lo);
    }
    LV(0);
    static const char *ar[] = { "fadd", "fmul", NULL, NULL, "fsub", "fsubr", "fdiv", "fdivr" };
    static const char *arp[] = { "faddp", "fmulp", NULL, NULL, "fsubp", "fsubrp", "fdivp", "fdivrp" };
    static const char *iar[] = { "fiadd", "fimul", "ficom", "ficomp", "fisub", "fisubr", "fidiv", "fidivr" };
    static const int dc_lo[] = { 0xc0, 0xc8, 0, 0, 0xe8, 0xe0, 0xf8, 0xf0 };
    static const int d8_lo[] = { 0xc0, 0xc8, 0, 0, 0xe0, 0xe8, 0xf0, 0xf8 };
    for (int i = 0; i < 8; i++) {
        if (ar[i]) {
            TF(ar[i], "m32", "d8 /%d", i);
            TF(ar[i], "m64", "dc /%d", i);
            TF(ar[i], "sti", "d8 %02x+i", d8_lo[i]);
            TF(ar[i], "st0,sti", "d8 %02x+i", d8_lo[i]);
            TF(ar[i], "sti,st0", "dc %02x+i", dc_lo[i]);
            TF(arp[i], "sti", "de %02x+i", dc_lo[i]);
            TF(arp[i], "sti,st0", "de %02x+i", dc_lo[i]);
            TF(arp[i], "", "de %02x", dc_lo[i] + 1);
        }
        TF(iar[i], "m16", "de /%d", i);
        TF(iar[i], "m32", "da /%d", i);
    }
}

#define SSE4(mn, op) \
    TF(mn "ps", "xmm,xm128", "0f %02x /r", op); \
    TF(mn "pd", "xmm,xm128", "66 0f %02x /r", op); \
    TF(mn "ss", "xmm,xm32", "f3 0f %02x /r", op); \
    TF(mn "sd", "xmm,xm64", "f2 0f %02x /r", op)

static void sse_tab(void)
{
    LV(7);
    SSE4("add", 0x58);
    SSE4("mul", 0x59);
    SSE4("sub", 0x5c);
    SSE4("min", 0x5d);
    SSE4("div", 0x5e);
    SSE4("max", 0x5f);
    SSE4("sqrt", 0x51);
    TF("rsqrtps", "xmm,xm128", "0f 52 /r");
    TF("rsqrtss", "xmm,xm32", "f3 0f 52 /r");
    TF("rcpps", "xmm,xm128", "0f 53 /r");
    TF("rcpss", "xmm,xm32", "f3 0f 53 /r");
    static const char *lg[] = { "and", "andn", "or", "xor" };
    for (int i = 0; i < 4; i++) {
        char m[16];
        snprintf(m, sizeof m, "%sps", lg[i]);
        TF(m, "xmm,xm128", "0f %02x /r", 0x54 + i);
        snprintf(m, sizeof m, "%spd", lg[i]);
        LV(8);
        TF(m, "xmm,xm128", "66 0f %02x /r", 0x54 + i);
        LV(7);
    }
    T("unpcklps", "xmm,xm128", "0f 14 /r");
    T("unpckhps", "xmm,xm128", "0f 15 /r");
    T("movups", "xmm,xm128", "0f 10 /r");
    T("movups", "xm128,xmm", "0f 11 /r");
    T("movaps", "xmm,xm128", "0f 28 /r");
    T("movaps", "xm128,xmm", "0f 29 /r");
    T("movss", "xmm,xm32", "f3 0f 10 /r");
    T("movss", "xm32,xmm", "f3 0f 11 /r");
    T("movlps", "xmm,m64", "0f 12 /r");
    T("movlps", "m64,xmm", "0f 13 /r");
    T("movhps", "xmm,m64", "0f 16 /r");
    T("movhps", "m64,xmm", "0f 17 /r");
    T("movlhps", "xmm,xmmr", "0f 16 /r");
    T("movhlps", "xmm,xmmr", "0f 12 /r");
    T("movntps", "m128,xmm", "0f 2b /r");
    T("movmskps", "r32,xmmr", "0f 50 /r");
    T("ucomiss", "xmm,xm32", "0f 2e /r");
    T("comiss", "xmm,xm32", "0f 2f /r");
    T("shufps", "xmm,xm128,imm8", "0f c6 /r ib");
    T("cvtpi2ps", "xmm,mm64", "0f 2a /r");
    T("cvtsi2ss", "xmm,rm32", "f3 0f 2a /r");
    T("cvtps2pi", "mm,xm64", "0f 2d /r");
    T("cvtss2si", "r32,xm32", "f3 0f 2d /r");
    T("cvttps2pi", "mm,xm64", "0f 2c /r");
    T("cvttss2si", "r32,xm32", "f3 0f 2c /r");
    T("cmpps", "xmm,xm128,imm8", "0f c2 /r ib");
    T("cmpss", "xmm,xm32,imm8", "f3 0f c2 /r ib");
    LV(8);
    T("cmppd", "xmm,xm128,imm8", "66 0f c2 /r ib");
    T("cmpsd", "xmm,xm64,imm8", "f2 0f c2 /r ib");
    static const char *pr[] = { "eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord" };
    for (int i = 0; i < 8; i++) {
        char m[24];
        snprintf(m, sizeof m, "cmp%sps", pr[i]); LV(7); TF(m, "xmm,xm128", "0f c2 /r =%02x", i);
        snprintf(m, sizeof m, "cmp%sss", pr[i]); TF(m, "xmm,xm32", "f3 0f c2 /r =%02x", i);
        snprintf(m, sizeof m, "cmp%spd", pr[i]); LV(8); TF(m, "xmm,xm128", "66 0f c2 /r =%02x", i);
        snprintf(m, sizeof m, "cmp%ssd", pr[i]); TF(m, "xmm,xm64", "f2 0f c2 /r =%02x", i);
    }
    T("unpcklpd", "xmm,xm128", "66 0f 14 /r");
    T("unpckhpd", "xmm,xm128", "66 0f 15 /r");
    T("movupd", "xmm,xm128", "66 0f 10 /r");
    T("movupd", "xm128,xmm", "66 0f 11 /r");
    T("movapd", "xmm,xm128", "66 0f 28 /r");
    T("movapd", "xm128,xmm", "66 0f 29 /r");
    T("movsd", "xmm,xm64", "f2 0f 10 /r");
    T("movsd", "xm64,xmm", "f2 0f 11 /r");
    T("movlpd", "xmm,m64", "66 0f 12 /r");
    T("movlpd", "m64,xmm", "66 0f 13 /r");
    T("movhpd", "xmm,m64", "66 0f 16 /r");
    T("movhpd", "m64,xmm", "66 0f 17 /r");
    T("movntpd", "m128,xmm", "66 0f 2b /r");
    T("movntdq", "m128,xmm", "66 0f e7 /r");
    T("movmskpd", "r32,xmmr", "66 0f 50 /r");
    T("ucomisd", "xmm,xm64", "66 0f 2e /r");
    T("comisd", "xmm,xm64", "66 0f 2f /r");
    T("shufpd", "xmm,xm128,imm8", "66 0f c6 /r ib");
    T("cvtpi2pd", "xmm,mm64", "66 0f 2a /r");
    T("cvtsi2sd", "xmm,rm32", "f2 0f 2a /r");
    T("cvtpd2pi", "mm,xm128", "66 0f 2d /r");
    T("cvtsd2si", "r32,xm64", "f2 0f 2d /r");
    T("cvttpd2pi", "mm,xm128", "66 0f 2c /r");
    T("cvttsd2si", "r32,xm64", "f2 0f 2c /r");
    T("cvtps2pd", "xmm,xm64", "0f 5a /r");
    T("cvtpd2ps", "xmm,xm128", "66 0f 5a /r");
    T("cvtss2sd", "xmm,xm32", "f3 0f 5a /r");
    T("cvtsd2ss", "xmm,xm64", "f2 0f 5a /r");
    T("cvtdq2ps", "xmm,xm128", "0f 5b /r");
    T("cvtps2dq", "xmm,xm128", "66 0f 5b /r");
    T("cvttps2dq", "xmm,xm128", "f3 0f 5b /r");
    T("cvtdq2pd", "xmm,xm64", "f3 0f e6 /r");
    T("cvtpd2dq", "xmm,xm128", "f2 0f e6 /r");
    T("cvttpd2dq", "xmm,xm128", "66 0f e6 /r");
    T("movd", "mm,rm32", "0f 6e /r");
    T("movd", "rm32,mm", "0f 7e /r");
    T("movd", "xmm,rm32", "66 0f 6e /r");
    T("movd", "rm32,xmm", "66 0f 7e /r");
    T("movq", "mm,mm64", "0f 6f /r");
    T("movq", "mm64,mm", "0f 7f /r");
    T("movq", "xmm,xm64", "f3 0f 7e /r");
    T("movq", "xm64,xmm", "66 0f d6 /r");
    T("movdqa", "xmm,xm128", "66 0f 6f /r");
    T("movdqa", "xm128,xmm", "66 0f 7f /r");
    T("movdqu", "xmm,xm128", "f3 0f 6f /r");
    T("movdqu", "xm128,xmm", "f3 0f 7f /r");
    T("movq2dq", "xmm,mmr", "f3 0f d6 /r");
    T("movdq2q", "mm,xmmr", "f2 0f d6 /r");
    T("maskmovdqu", "xmm,xmmr", "66 0f f7 /r");
    T("pshufd", "xmm,xm128,imm8", "66 0f 70 /r ib");
    T("pshufhw", "xmm,xm128,imm8", "f3 0f 70 /r ib");
    T("pshuflw", "xmm,xm128,imm8", "f2 0f 70 /r ib");
    T("pslldq", "xmmr,imm8", "66 0f 73 /7 ib");
    T("psrldq", "xmmr,imm8", "66 0f 73 /3 ib");
    T("punpckhqdq", "xmm,xm128", "66 0f 6d /r");
    T("punpcklqdq", "xmm,xm128", "66 0f 6c /r");
    LV(5);
    T("maskmovq", "mm,mmr", "0f f7 /r");
    T("movntq", "m64,mm", "0f e7 /r");
    T("pshufw", "mm,mm64,imm8", "0f 70 /r ib");
    T("pextrw", "r32,mmr,imm8", "0f c5 /r ib");
    T("pinsrw", "mm,r32m16,imm8", "0f c4 /r ib");
    T("pmovmskb", "r32,mmr", "0f d7 /r");
    LV(8);
    T("pextrw", "r32,xmmr,imm8", "66 0f c5 /r ib");
    T("pinsrw", "xmm,r32m16,imm8", "66 0f c4 /r ib");
    T("pmovmskb", "r32,xmmr", "66 0f d7 /r");
    static const struct { const char *n; int op; int mmx; } pi[] = {
        { "punpcklbw", 0x60, 1 }, { "punpcklwd", 0x61, 1 }, { "punpckldq", 0x62, 1 }, { "packsswb", 0x63, 1 },
        { "pcmpgtb", 0x64, 1 }, { "pcmpgtw", 0x65, 1 }, { "pcmpgtd", 0x66, 1 }, { "packuswb", 0x67, 1 },
        { "punpckhbw", 0x68, 1 }, { "punpckhwd", 0x69, 1 }, { "punpckhdq", 0x6a, 1 }, { "packssdw", 0x6b, 1 },
        { "pcmpeqb", 0x74, 1 }, { "pcmpeqw", 0x75, 1 }, { "pcmpeqd", 0x76, 1 },
        { "psrlw", 0xd1, 1 }, { "psrld", 0xd2, 1 }, { "psrlq", 0xd3, 1 }, { "paddq", 0xd4, 1 }, { "pmullw", 0xd5, 1 },
        { "psubusb", 0xd8, 1 }, { "psubusw", 0xd9, 1 }, { "pminub", 0xda, 1 }, { "pand", 0xdb, 1 },
        { "paddusb", 0xdc, 1 }, { "paddusw", 0xdd, 1 }, { "pmaxub", 0xde, 1 }, { "pandn", 0xdf, 1 },
        { "pavgb", 0xe0, 1 }, { "psraw", 0xe1, 1 }, { "psrad", 0xe2, 1 }, { "pavgw", 0xe3, 1 },
        { "pmulhuw", 0xe4, 1 }, { "pmulhw", 0xe5, 1 }, { "psubsb", 0xe8, 1 }, { "psubsw", 0xe9, 1 },
        { "pminsw", 0xea, 1 }, { "por", 0xeb, 1 }, { "paddsb", 0xec, 1 }, { "paddsw", 0xed, 1 },
        { "pmaxsw", 0xee, 1 }, { "pxor", 0xef, 1 }, { "psllw", 0xf1, 1 }, { "pslld", 0xf2, 1 },
        { "psllq", 0xf3, 1 }, { "pmuludq", 0xf4, 1 }, { "pmaddwd", 0xf5, 1 }, { "psadbw", 0xf6, 1 },
        { "psubb", 0xf8, 1 }, { "psubw", 0xf9, 1 }, { "psubd", 0xfa, 1 }, { "psubq", 0xfb, 1 },
        { "paddb", 0xfc, 1 }, { "paddw", 0xfd, 1 }, { "paddd", 0xfe, 1 },
        { NULL, 0, 0 }
    };
    for (int i = 0; pi[i].n; i++) {
        LV(5);
        TF(pi[i].n, "mm,mm64", "0f %02x /r", pi[i].op);
        LV(8);
        TF(pi[i].n, "xmm,xm128", "66 0f %02x /r", pi[i].op);
    }
    static const struct { const char *n; int op, dig; } psh[] = {
        { "psllw", 0x71, 6 }, { "pslld", 0x72, 6 }, { "psllq", 0x73, 6 },
        { "psrlw", 0x71, 2 }, { "psrld", 0x72, 2 }, { "psrlq", 0x73, 2 },
        { "psraw", 0x71, 4 }, { "psrad", 0x72, 4 }, { NULL, 0, 0 }
    };
    for (int i = 0; psh[i].n; i++) {
        LV(5);
        TF(psh[i].n, "mmr,imm8", "0f %02x /%d ib", psh[i].op, psh[i].dig);
        LV(8);
        TF(psh[i].n, "xmmr,imm8", "66 0f %02x /%d ib", psh[i].op, psh[i].dig);
    }
    LV(9);
    T("addsubpd", "xmm,xm128", "66 0f d0 /r");
    T("addsubps", "xmm,xm128", "f2 0f d0 /r");
    T("haddpd", "xmm,xm128", "66 0f 7c /r");
    T("haddps", "xmm,xm128", "f2 0f 7c /r");
    T("hsubpd", "xmm,xm128", "66 0f 7d /r");
    T("hsubps", "xmm,xm128", "f2 0f 7d /r");
    T("lddqu", "xmm,m128", "f2 0f f0 /r");
    T("movddup", "xmm,xm64", "f2 0f 12 /r");
    T("movshdup", "xmm,xm128", "f3 0f 16 /r");
    T("movsldup", "xmm,xm128", "f3 0f 12 /r");
    static const struct { const char *n; int op; } ss3[] = {
        { "pshufb", 0x00 }, { "phaddw", 0x01 }, { "phaddd", 0x02 }, { "phaddsw", 0x03 }, { "pmaddubsw", 0x04 },
        { "phsubw", 0x05 }, { "phsubd", 0x06 }, { "phsubsw", 0x07 }, { "psignb", 0x08 }, { "psignw", 0x09 },
        { "psignd", 0x0a }, { "pmulhrsw", 0x0b }, { "pabsb", 0x1c }, { "pabsw", 0x1d }, { "pabsd", 0x1e },
        { NULL, 0 }
    };
    for (int i = 0; ss3[i].n; i++) {
        LV(10);
        TF(ss3[i].n, "mm,mm64", "0f 38 %02x /r", ss3[i].op);
        TF(ss3[i].n, "xmm,xm128", "66 0f 38 %02x /r", ss3[i].op);
    }
    T("palignr", "mm,mm64,imm8", "0f 3a 0f /r ib");
    T("palignr", "xmm,xm128,imm8", "66 0f 3a 0f /r ib");
    LV(11);
    static const struct { const char *n; int op; const char *sp; } s41[] = {
        { "ptest", 0x17, "xmm,xm128" }, { "pmovsxbw", 0x20, "xmm,xm64" }, { "pmovsxbd", 0x21, "xmm,xm32" },
        { "pmovsxbq", 0x22, "xmm,xm16" }, { "pmovsxwd", 0x23, "xmm,xm64" }, { "pmovsxwq", 0x24, "xmm,xm32" },
        { "pmovsxdq", 0x25, "xmm,xm64" }, { "pmuldq", 0x28, "xmm,xm128" }, { "pcmpeqq", 0x29, "xmm,xm128" },
        { "packusdw", 0x2b, "xmm,xm128" }, { "pmovzxbw", 0x30, "xmm,xm64" }, { "pmovzxbd", 0x31, "xmm,xm32" },
        { "pmovzxbq", 0x32, "xmm,xm16" }, { "pmovzxwd", 0x33, "xmm,xm64" }, { "pmovzxwq", 0x34, "xmm,xm32" },
        { "pmovzxdq", 0x35, "xmm,xm64" }, { "pcmpgtq", 0x37, "xmm,xm128" }, { "pminsb", 0x38, "xmm,xm128" },
        { "pminsd", 0x39, "xmm,xm128" }, { "pminuw", 0x3a, "xmm,xm128" }, { "pminud", 0x3b, "xmm,xm128" },
        { "pmaxsb", 0x3c, "xmm,xm128" }, { "pmaxsd", 0x3d, "xmm,xm128" }, { "pmaxuw", 0x3e, "xmm,xm128" },
        { "pmaxud", 0x3f, "xmm,xm128" }, { "pmulld", 0x40, "xmm,xm128" }, { "phminposuw", 0x41, "xmm,xm128" },
        { "movntdqa", 0x2a, "xmm,m128" }, { NULL, 0, NULL }
    };
    for (int i = 0; s41[i].n; i++) TF(s41[i].n, s41[i].sp, "66 0f 38 %02x /r", s41[i].op);
    T("pblendvb", "xmm,xm128,xmm0", "66 0f 38 10 /r");
    T("pblendvb", "xmm,xm128", "66 0f 38 10 /r");
    T("blendvps", "xmm,xm128,xmm0", "66 0f 38 14 /r");
    T("blendvps", "xmm,xm128", "66 0f 38 14 /r");
    T("blendvpd", "xmm,xm128,xmm0", "66 0f 38 15 /r");
    T("blendvpd", "xmm,xm128", "66 0f 38 15 /r");
    static const struct { const char *n; int op; const char *sp; } s41i[] = {
        { "roundps", 0x08, "xmm,xm128,imm8" }, { "roundpd", 0x09, "xmm,xm128,imm8" },
        { "roundss", 0x0a, "xmm,xm32,imm8" }, { "roundsd", 0x0b, "xmm,xm64,imm8" },
        { "blendps", 0x0c, "xmm,xm128,imm8" }, { "blendpd", 0x0d, "xmm,xm128,imm8" },
        { "pblendw", 0x0e, "xmm,xm128,imm8" }, { "insertps", 0x21, "xmm,xm32,imm8" },
        { "dpps", 0x40, "xmm,xm128,imm8" }, { "dppd", 0x41, "xmm,xm128,imm8" },
        { "mpsadbw", 0x42, "xmm,xm128,imm8" }, { "pclmulqdq", 0x44, "xmm,xm128,imm8" },
        { "pinsrb", 0x20, "xmm,r32m8,imm8" }, { "pinsrd", 0x22, "xmm,rm32,imm8" },
        { "pextrb", 0x14, "r32m8,xmm,imm8" }, { "pextrw", 0x15, "r32m16,xmm,imm8" },
        { "pextrd", 0x16, "rm32,xmm,imm8" }, { "extractps", 0x17, "rm32,xmm,imm8" },
        { NULL, 0, NULL }
    };
    for (int i = 0; s41i[i].n; i++) TF(s41i[i].n, s41i[i].sp, "66 0f 3a %02x /r ib", s41i[i].op);
    LV(12);
    T("pcmpestrm", "xmm,xm128,imm8", "66 0f 3a 60 /r ib");
    T("pcmpestri", "xmm,xm128,imm8", "66 0f 3a 61 /r ib");
    T("pcmpistrm", "xmm,xm128,imm8", "66 0f 3a 62 /r ib");
    T("pcmpistri", "xmm,xm128,imm8", "66 0f 3a 63 /r ib");
    T("aesimc", "xmm,xm128", "66 0f 38 db /r");
    T("aesenc", "xmm,xm128", "66 0f 38 dc /r");
    T("aesenclast", "xmm,xm128", "66 0f 38 dd /r");
    T("aesdec", "xmm,xm128", "66 0f 38 de /r");
    T("aesdeclast", "xmm,xm128", "66 0f 38 df /r");
    T("aeskeygenassist", "xmm,xm128,imm8", "66 0f 3a df /r ib");
    static const struct { const char *n; int hi; } pcl[] = {
        { "pclmullqlqdq", 0x00 }, { "pclmulhqlqdq", 0x01 }, { "pclmullqhqdq", 0x10 }, { "pclmulhqhqdq", 0x11 }, { NULL, 0 }
    };
    for (int i = 0; pcl[i].n; i++) TF(pcl[i].n, "xmm,xm128", "66 0f 3a 44 /r =%02x", pcl[i].hi);
    T("sha1rnds4", "xmm,xm128,imm8", "0f 3a cc /r ib");
    T("sha1nexte", "xmm,xm128", "0f 38 c8 /r");
    T("sha1msg1", "xmm,xm128", "0f 38 c9 /r");
    T("sha1msg2", "xmm,xm128", "0f 38 ca /r");
    T("sha256rnds2", "xmm,xm128,xmm0", "0f 38 =cb /r");
    T("sha256rnds2", "xmm,xm128", "0f 38 =cb /r");
    T("sha256msg1", "xmm,xm128", "0f 38 cc /r");
    T("sha256msg2", "xmm,xm128", "0f 38 =cd /r");
    T("adcx", "r32,rm32", "66 0f 38 f6 /r");
    T("adox", "r32,rm32", "f3 0f 38 f6 /r");
    LV(5);
    static const struct { const char *n; int sfx; } d3[] = {
        { "pavgusb", 0xbf }, { "pf2id", 0x1d }, { "pf2iw", 0x1c }, { "pfacc", 0xae }, { "pfadd", 0x9e },
        { "pfcmpeq", 0xb0 }, { "pfcmpge", 0x90 }, { "pfcmpgt", 0xa0 }, { "pfmax", 0xa4 }, { "pfmin", 0x94 },
        { "pfmul", 0xb4 }, { "pfnacc", 0x8a }, { "pfpnacc", 0x8e }, { "pfrcp", 0x96 }, { "pfrcpit1", 0xa6 },
        { "pfrcpit2", 0xb6 }, { "pfrsqit1", 0xa7 }, { "pfrsqrt", 0x97 }, { "pfsub", 0x9a }, { "pfsubr", 0xaa },
        { "pi2fd", 0x0d }, { "pi2fw", 0x0c }, { "pmulhrw", 0xb7 }, { "pswapd", 0xbb }, { NULL, 0 }
    };
    for (int i = 0; d3[i].n; i++) TF(d3[i].n, "mm,mm64", "0f 0f /r =%02x", d3[i].sfx);
}

void gen_tab(void);
void gen64_tab(void);
static void mark_nolong(void);
void ext_tab(void);

void insn_tab_init(void)
{
    int_tab();
    fpu_tab();
    sse_tab();
    ext_tab();
    gen_tab();
    gen64_tab();
    mark_nolong();
}

void insn_dump(FILE *f)
{
    for (size_t i = 0; i < g_etab.nb; i++)
        for (HEnt *h = g_etab.b[i]; h; h = h->next)
            for (Ent *e = h->val; e; e = e->next) {
                fprintf(f, "%s", e->mn);
                for (int k = 0; k < e->nops; k++) {
                    fprintf(f, "%s", k ? "," : " ");
                    if (IS_SV(e->sp[k])) {
                        int c = SV_CLS(e->sp[k]);
                        if (SV_VSIB(e->sp[k])) fprintf(f, "vs%c", "xyz?"[c]);
                        else {
                            fprintf(f, "v%c", "xyzk"[c]);
                            if (SV_MEM(e->sp[k])) fprintf(f, "%d", 1 << (SV_MEM(e->sp[k]) - 1));
                        }
                    } else {
                        for (int j = 0; specnames[j].n; j++)
                            if (specnames[j].id == e->sp[k]) { fprintf(f, "%s", specnames[j].n); break; }
                    }
                    u8 d = e->dc[k];
                    if (d & DC_MASK) fprintf(f, "|mask");
                    if (d & DC_Z) fprintf(f, "|z");
                    if (d & DC_BC) fprintf(f, "|b%d", ((d >> 6) & 3) == 1 ? 16 : ((d >> 6) & 3) == 2 ? 32 : 64);
                    if (d & DC_ER) fprintf(f, "|er");
                    if (d & DC_SAE) fprintf(f, "|sae");
                    if (d & DC_STAR) fprintf(f, "*");
                }
                fprintf(f, " | %s | %d\n", e->enc, e->level);
            }
}

int insn_known(const char *mn)
{
    char b[40];
    size_t n = strlen(mn);
    if (n >= sizeof b) return 0;
    for (size_t i = 0; i <= n; i++) b[i] = (char)tolower((unsigned char)mn[i]);
    if (!strcmp(b, "syscall")) return 1;
    return ht_get(&g_etab, b) != NULL;
}

const char *insn_suggest(const char *mn)
{
    static const char *dirs[] = { "section", "segment", "global", "extern", "bits", "org", "align", "times", "equ", "resb", "resw", "resd", "db", "dw", "dd", "dq", "incbin", "default", "absolute", "common", "struc", "endstruc", "istruc", "iend", "cpu", NULL };
    size_t n = strlen(mn);
    if (n < 3) return NULL;
    const char *best = NULL;
    int bd = n <= 4 ? 1 : 2;
    for (size_t i = 0; i < g_etab.nb; i++)
        for (HEnt *h = g_etab.b[i]; h; h = h->next) {
            int d = edit_distance(mn, h->key);
            if (d <= bd && strcasecmp(mn, h->key) && (d < bd || !best || strlen(h->key) < strlen(best) || (strlen(h->key) == strlen(best) && strcmp(h->key, best) < 0))) { bd = d; best = h->key; }
        }
    for (int i = 0; dirs[i]; i++) {
        int d = edit_distance(mn, dirs[i]);
        if (d <= bd && strcasecmp(mn, dirs[i])) { bd = d; best = dirs[i]; }
    }
    return best;
}

static int has_tok(const Ent *e, const char *tok)
{
    size_t l = strlen(tok);
    for (const char *t = e->enc; *t; ) {
        if (!strncmp(t, tok, l) && (t[l] == ' ' || !t[l])) return 1;
        while (*t && *t != ' ') t++;
        while (*t == ' ') t++;
    }
    return 0;
}

static int has_spec(const Ent *e, int a)
{
    for (int i = 0; i < e->nops; i++) if (e->sp[i] == a) return 1;
    return 0;
}

static void mark_nolong(void)
{
    static const char *nl[] = {
        "aaa", "aad", "aam", "aas", "daa", "das", "bound", "into", "pusha", "popa", "pushad", "popad", "pushaw", "popaw",
        "arpl", "les", "lds", "salc", "pushfd", "popfd", "retd", "retnd", NULL
    };
    for (size_t i = 0; i < g_etab.nb; i++)
        for (HEnt *h = g_etab.b[i]; h; h = h->next)
            for (Ent *e = h->val; e; e = e->next) {
                if (e->mode || e->vkind) continue;
                int bad = 0;
                for (int k = 0; nl[k]; k++) if (!strcmp(e->mn, nl[k])) bad = 1;
                if (has_tok(e, "40+r") || has_tok(e, "48+r") || has_tok(e, "a16")) bad = 1;
                if (!strcmp(e->mn, "push") || !strcmp(e->mn, "pop")) {
                    if (has_spec(e, S_R32) || has_spec(e, S_RM32) || has_spec(e, S_SB32) || has_spec(e, S_IMM32) || has_spec(e, S_ES) || has_spec(e, S_CS) || has_spec(e, S_SS) || has_spec(e, S_DS)) bad = 1;
                }
                if (!strcmp(e->mn, "call") || !strcmp(e->mn, "jmp")) {
                    if (has_spec(e, S_RM16) || has_spec(e, S_RM32) || has_spec(e, S_FARPTR)) bad = 1;
                }
                if (!strcmp(e->mn, "mov") && (has_spec(e, S_CR) || has_spec(e, S_DR) || has_spec(e, S_TR))) bad = 1;
                if (bad) e->mode = 2;
            }
}
