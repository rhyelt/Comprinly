#include "cly.h"

typedef struct { const char *n; int cls, num, size; } RegDef;

static HT regs;
static HT regs64;
static int rmode64;
static int reg_ready;
static RegDef **defs;
static int ndefs;

static void radd2(HT *h, const char *n, int cls, int num, int size)
{
    RegDef *d = xmalloc(sizeof *d);
    d->n = xstrdup(n);
    d->cls = cls;
    d->num = num;
    d->size = size;
    ht_put(h, n, d);
}

static void radd(const char *n, int cls, int num, int size)
{
    RegDef *d = xmalloc(sizeof *d);
    d->n = xstrdup(n);
    d->cls = cls;
    d->num = num;
    d->size = size;
    defs = xrealloc(defs, sizeof(RegDef *) * (size_t)(ndefs + 1));
    defs[ndefs++] = d;
    ht_put(&regs, n, d);
}

static void reg_init(void)
{
    static const char *r8[] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
    static const char *r16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
    static const char *r32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    static const char *r64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" };
    static const char *sg[] = { "es", "cs", "ss", "ds", "fs", "gs" };
    char b[32];
    reg_ready = 1;
    for (int i = 0; i < 8; i++) {
        radd(r8[i], RC_R8, i, 1);
        radd(r16[i], RC_R16, i, 2);
        radd(r32[i], RC_R32, i, 4);
        radd(r64[i], RC_R32, i, 4);
    }
    for (int i = 0; i < 6; i++) radd(sg[i], RC_SEG, i, 2);
    for (int i = 0; i < 8; i++) {
        snprintf(b, sizeof b, "cr%d", i); radd(b, RC_CR, i, 4);
        snprintf(b, sizeof b, "dr%d", i); radd(b, RC_DR, i, 4);
        snprintf(b, sizeof b, "tr%d", i); radd(b, RC_TR, i, 4);
        snprintf(b, sizeof b, "mm%d", i); radd(b, RC_MMX, i, 8);
        snprintf(b, sizeof b, "k%d", i); radd(b, RC_K, i, 8);
        snprintf(b, sizeof b, "st%d", i); radd(b, RC_ST, i, 10);
    }
    radd("st", RC_ST, 0, 10);
    for (int i = 0; i < 32; i++) {
        snprintf(b, sizeof b, "xmm%d", i); radd(b, RC_XMM, i, 16);
        snprintf(b, sizeof b, "ymm%d", i); radd(b, RC_YMM, i, 32);
        snprintf(b, sizeof b, "zmm%d", i); radd(b, RC_ZMM, i, 64);
    }
    for (int i = 0; i < 4; i++) {
        snprintf(b, sizeof b, "bnd%d", i); radd(b, RC_BND, i, 16);
    }
    for (int i = 8; i < 16; i++) {
        snprintf(b, sizeof b, "r%d", i); radd(b, RC_VREG, i - 8, 4);
        snprintf(b, sizeof b, "r%dd", i); radd(b, RC_VREG, i - 8, 4);
        snprintf(b, sizeof b, "r%dw", i); radd(b, RC_VREG, i - 8, 2);
        snprintf(b, sizeof b, "r%db", i); radd(b, RC_VREG, i - 8, 1);
        snprintf(b, sizeof b, "r%dl", i); radd(b, RC_VREG, i - 8, 1);
    }
}

static void reg64_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    static const char *r8[] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
    static const char *r64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi" };
    static const char *lo[] = { "spl", "bpl", "sil", "dil" };
    char b[16];
    for (int i = 0; i < 8; i++) radd2(&regs64, r64[i], RC_R64, i, 8);
    for (int i = 0; i < 4; i++) radd2(&regs64, lo[i], RC_R8, 20 + i, 1);
    for (int i = 8; i < 16; i++) {
        snprintf(b, sizeof b, "r%d", i); radd2(&regs64, b, RC_R64, i, 8);
        snprintf(b, sizeof b, "r%dd", i); radd2(&regs64, b, RC_R32, i, 4);
        snprintf(b, sizeof b, "r%dw", i); radd2(&regs64, b, RC_R16, i, 2);
        snprintf(b, sizeof b, "r%db", i); radd2(&regs64, b, RC_R8, i, 1);
        snprintf(b, sizeof b, "r%dl", i); radd2(&regs64, b, RC_R8, i, 1);
    }
    radd2(&regs64, "rip", RC_RIP, 0, 8);
    for (int i = 0; i < 8; i++) (void)r8[i];
    for (int i = 8; i < 16; i++) {
        snprintf(b, sizeof b, "cr%d", i); radd2(&regs64, b, RC_CR, i, 8);
        snprintf(b, sizeof b, "dr%d", i); radd2(&regs64, b, RC_DR, i, 8);
    }
    for (int i = 0; i < 8; i++) {
        snprintf(b, sizeof b, "cr%d", i); radd2(&regs64, b, RC_CR, i, 8);
        snprintf(b, sizeof b, "dr%d", i); radd2(&regs64, b, RC_DR, i, 8);
    }
}

void reg_mode(int bits)
{
    if (bits == 64) reg64_init();
    rmode64 = bits == 64;
}

int reg_slot(int rid)
{
    int n = REGNUM(rid);
    switch (REGCLS(rid)) {
    case RC_R32: return n;
    case RC_R16: return 16 + n;
    case RC_R64: return 32 + n;
    case RC_RIP: return 48;
    case RC_XMM: return 64 + n;
    case RC_YMM: return 96 + n;
    case RC_ZMM: return 128 + n;
    }
    return -1;
}

int slot_reg(int s)
{
    if (s < 16) return REGID(RC_R32, s);
    if (s < 32) return REGID(RC_R16, s - 16);
    if (s < 48) return REGID(RC_R64, s - 32);
    if (s == 48) return REGID(RC_RIP, 0);
    if (s < 96) return REGID(RC_XMM, s - 64);
    if (s < 128) return REGID(RC_YMM, s - 96);
    return REGID(RC_ZMM, s - 128);
}

int reg_lookup(const char *name, int *size)
{
    if (!reg_ready) reg_init();
    char b[16];
    size_t n = strlen(name);
    if (n >= sizeof b || n < 2) return 0;
    for (size_t i = 0; i <= n; i++) b[i] = (char)tolower((unsigned char)name[i]);
    RegDef *d = rmode64 ? ht_get(&regs64, b) : NULL;
    if (!d) d = ht_get(&regs, b);
    if (!d) return 0;
    if (size) *size = d->size;
    return REGID(d->cls, d->num);
}

const char *reg_name(int id)
{
    if (!reg_ready) reg_init();
    for (int i = 0; i < ndefs; i++)
        if (REGID(defs[i]->cls, defs[i]->num) == id && defs[i]->cls != RC_VREG) {
            if (defs[i]->n[0] == 'r' && defs[i]->cls == RC_R32) continue;
            return defs[i]->n;
        }
    return "?";
}

void reg_altreg(int on)
{
    static int done;
    if (!on || done) return;
    if (!reg_ready) reg_init();
    done = 1;
    char b[16];
    for (int i = 0; i < 8; i++) {
        snprintf(b, sizeof b, "r%d", i); radd(b, RC_R32, i, 4);
        snprintf(b, sizeof b, "r%dd", i); radd(b, RC_R32, i, 4);
        snprintf(b, sizeof b, "r%dw", i); radd(b, RC_R16, i, 2);
    }
    for (int i = 0; i < 4; i++) {
        snprintf(b, sizeof b, "r%dl", i); radd(b, RC_R8, i, 1);
        snprintf(b, sizeof b, "r%db", i); radd(b, RC_R8, i, 1);
        snprintf(b, sizeof b, "r%dh", i); radd(b, RC_R8, i + 4, 1);
    }
}
