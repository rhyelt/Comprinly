#include "cly.h"

#define BL 1200

typedef struct { u32 d[BL]; int n; } Big;

static void bnorm(Big *a) { while (a->n > 0 && a->d[a->n - 1] == 0) a->n--; }

static void bset(Big *a, u64 v)
{
    a->n = 0;
    while (v) { a->d[a->n++] = (u32)v; v >>= 32; }
}

static int big_iszero(const Big *a) { return a->n == 0; }

static void bmuladd(Big *a, u32 m, u32 add)
{
    u64 c = add;
    for (int i = 0; i < a->n; i++) {
        u64 t = (u64)a->d[i] * m + c;
        a->d[i] = (u32)t;
        c = t >> 32;
    }
    if (c && a->n < BL) a->d[a->n++] = (u32)c;
}

static int bbitlen(const Big *a)
{
    if (!a->n) return 0;
    u32 t = a->d[a->n - 1];
    int b = 0;
    while (t) { b++; t >>= 1; }
    return (a->n - 1) * 32 + b;
}

static int bbit(const Big *a, int i)
{
    int w = i / 32;
    if (w >= a->n) return 0;
    return (a->d[w] >> (i % 32)) & 1;
}

static void bshl2(Big *a, int s)
{
    if (!a->n || s <= 0) return;
    Big r;
    memset(r.d, 0, sizeof(u32) * (size_t)(a->n + s / 32 + 2));
    r.n = a->n + s / 32 + 1;
    int w = s / 32, b = s % 32;
    for (int i = 0; i < a->n; i++) {
        u64 v = (u64)a->d[i] << b;
        r.d[i + w] |= (u32)v;
        r.d[i + w + 1] |= (u32)(v >> 32);
    }
    bnorm(&r);
    *a = r;
}

static int big_cmp(const Big *a, const Big *b)
{
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

static void bsub(Big *a, const Big *b)
{
    i64 br = 0;
    for (int i = 0; i < a->n; i++) {
        i64 t = (i64)a->d[i] - (i < b->n ? (i64)b->d[i] : 0) - br;
        if (t < 0) { t += ((i64)1 << 32); br = 1; } else br = 0;
        a->d[i] = (u32)t;
    }
    bnorm(a);
}

static void bmulpow10(Big *a, int e)
{
    while (e >= 9) { bmuladd(a, 1000000000u, 0); e -= 9; }
    u32 m = 1;
    while (e-- > 0) m *= 10;
    if (m > 1) bmuladd(a, m, 0);
}

static void bshr_to_u64(const Big *a, int shift, u64 *out)
{
    u64 r = 0;
    for (int i = 0; i < 64; i++)
        if (bbit(a, shift + i)) r |= (u64)1 << i;
    *out = r;
}

typedef struct { int ebits, mbits, explicit_int, bytes; } FFmt;

static FFmt ffmt_for(int kind)
{
    switch (kind) {
    case 1: return (FFmt){ 4, 3, 0, 1 };
    case 2: return (FFmt){ 5, 10, 0, 2 };
    case 3: return (FFmt){ 8, 7, 0, 2 };
    case 4: return (FFmt){ 8, 23, 0, 4 };
    case 8: return (FFmt){ 11, 52, 0, 8 };
    case 10: return (FFmt){ 15, 63, 1, 10 };
    default: return (FFmt){ 15, 112, 0, 16 };
    }
}

static void put_bits(u8 *out, int nbytes, int sign, int expf, const Big *mant, int mbits_total, int ebits)
{
    memset(out, 0, 16);
    for (int i = 0; i < mbits_total; i++)
        if (bbit(mant, i)) out[i / 8] |= (u8)(1 << (i % 8));
    for (int i = 0; i < ebits; i++)
        if ((expf >> i) & 1) { int b = mbits_total + i; out[b / 8] |= (u8)(1 << (b % 8)); }
    if (sign) { int b = mbits_total + ebits; out[b / 8] |= (u8)(1 << (b % 8)); }
    (void)nbytes;
}

static int build_float(int sign, Big *Nn, Big *Dd, int k2, int kind, u8 *out)
{
    FFmt f = ffmt_for(kind);
    int p = f.explicit_int ? 64 : f.mbits + 1;
    int bias = (1 << (f.ebits - 1)) - 1;
    int emax = bias;
    int emin = 1 - bias;
    int mfield = f.explicit_int ? 64 : f.mbits;
    Big mant;
    mant.n = 0;
    if (big_iszero(Nn)) {
        put_bits(out, f.bytes, sign, 0, &mant, mfield, f.ebits);
        return 1;
    }
    int d = bbitlen(Nn) - bbitlen(Dd);
    Big t1 = *Nn, t2 = *Dd;
    if (d >= 0) bshl2(&t2, d); else bshl2(&t1, -d);
    int E = (big_cmp(&t1, &t2) < 0) ? d - 1 : d;
    E += k2;
    if (E > emax + 2000 || E < emin - 2000 - p) {
        if (E > 0) {
            Big z; z.n = 0;
            put_bits(out, f.bytes, sign, (1 << f.ebits) - 1, &z, mfield, f.ebits);
            if (f.explicit_int) { out[7] |= 0x80; }
            return 1;
        }
        put_bits(out, f.bytes, sign, 0, &mant, mfield, f.ebits);
        return 1;
    }
    int top = E > emin ? E : emin;
    int lsb = top - (p - 1);
    int sh = k2 - lsb;
    Big nn = *Nn, dd = *Dd;
    if (sh > 0) bshl2(&nn, sh); else if (sh < 0) bshl2(&dd, -sh);
    Big Q;
    memset(Q.d, 0, sizeof Q.d);
    Q.n = 0;
    Big R = nn;
    int qbits = bbitlen(&nn) - bbitlen(&dd) + 1;
    if (qbits < 1) qbits = 1;
    Q.n = qbits / 32 + 1;
    for (int i = qbits - 1; i >= 0; i--) {
        Big sd = dd;
        bshl2(&sd, i);
        if (big_cmp(&R, &sd) >= 0) {
            bsub(&R, &sd);
            Q.d[i / 32] |= (u32)1 << (i % 32);
        }
    }
    bnorm(&Q);
    {
        Big r2 = R;
        bshl2(&r2, 1);
        int c = big_cmp(&r2, &dd);
        int odd = bbit(&Q, 0);
        if (c > 0 || (c == 0 && odd)) bmuladd(&Q, 1, 1);
    }
    if (bbitlen(&Q) > p) {
        u64 lo;
        Big h = Q;
        bshr_to_u64(&h, 1, &lo);
        bset(&Q, lo);
        lsb++;
    }
    int biased;
    if (bbitlen(&Q) < p) {
        biased = 0;
    } else {
        int Er = lsb + p - 1;
        if (Er > emax) {
            Big z; z.n = 0;
            put_bits(out, f.bytes, sign, (1 << f.ebits) - 1, &z, mfield, f.ebits);
            if (f.explicit_int) { out[7] |= 0x80; }
            return 1;
        }
        biased = Er + bias;
        if (!f.explicit_int) {
            int hb = p - 1;
            Q.d[hb / 32] &= ~((u32)1 << (hb % 32));
            bnorm(&Q);
        }
    }
    put_bits(out, f.bytes, sign, biased, &Q, mfield, f.ebits);
    return 1;
}

int float_special(const char *name, const char *text, u8 *out, int *nbytes)
{
    (void)text;
    const char *n = name;
    if (!strncmp(n, "__?", 3)) n += 3; else if (!strncmp(n, "__", 2)) n += 2;
    memset(out, 0, 16);
    u32 v = 0;
    if (!strncasecmp(n, "infinity", 8)) v = 0x7F800000u;
    else if (!strncasecmp(n, "qnan", 4) || !strncasecmp(n, "nan", 3)) v = 0x7FC00000u;
    else if (!strncasecmp(n, "snan", 4)) v = 0x7FA00000u;
    else return 0;
    for (int i = 0; i < 4; i++) out[i] = (u8)(v >> (8 * i));
    *nbytes = 4;
    return 1;
}

static int special_bits(const char *t, int sign, int kind, u8 *out)
{
    FFmt f = ffmt_for(kind);
    int mfield = f.explicit_int ? 64 : f.mbits;
    int infexp = (1 << f.ebits) - 1;
    Big m;
    m.n = 0;
    const char *n = t;
    if (!strncmp(n, "__?", 3)) n += 3; else if (!strncmp(n, "__", 2)) n += 2; else return 0;
    if (!strncasecmp(n, "infinity", 8)) {
        if (f.explicit_int) { m.d[0] = 0; m.d[1] = 0x80000000u; m.n = 2; }
        put_bits(out, f.bytes, sign, infexp, &m, mfield, f.ebits);
        return 1;
    }
    int quiet = !strncasecmp(n, "qnan", 4) || !strncasecmp(n, "nan", 3);
    int snan = !strncasecmp(n, "snan", 4);
    if (quiet || snan) {
        memset(m.d, 0, 16 * sizeof(u32));
        int top = mfield - 1;
        if (f.explicit_int) {
            m.d[1] = 0x80000000u;
            m.d[1] |= quiet ? 0x40000000u : 0;
            if (snan) m.d[0] = 1;
            m.n = 2;
            bnorm(&m);
        } else {
            if (quiet) m.d[top / 32] |= (u32)1 << (top % 32);
            else m.d[0] |= 1;
            m.n = top / 32 + 1;
            bnorm(&m);
        }
        put_bits(out, f.bytes, sign, infexp, &m, mfield, f.ebits);
        return 1;
    }
    return 0;
}

int float_encode(const char *text, int kind, u8 *out)
{
    const char *p = text;
    while (*p == ' ') p++;
    int sign = 0;
    if (*p == '-') { sign = 1; p++; } else if (*p == '+') p++;
    memset(out, 0, 16);
    if (p[0] == '_' && p[1] == '_') return special_bits(p, sign, kind, out);
    Big N;
    N.n = 0;
    Big D;
    bset(&D, 1);
    int k2 = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        int frac = 0, seen = 0;
        while (isxdigit((unsigned char)*p) || *p == '.') {
            if (*p == '.') { if (seen) return 0; seen = 1; p++; continue; }
            int d = isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10);
            bmuladd(&N, 16, (u32)d);
            if (seen) frac++;
            p++;
        }
        int ex = 0;
        if (*p == 'p' || *p == 'P') {
            p++;
            int sg = 1;
            if (*p == '+') p++; else if (*p == '-') { sg = -1; p++; }
            while (isdigit((unsigned char)*p)) { ex = ex * 10 + (*p - '0'); if (ex > 100000) ex = 100000; p++; }
            ex *= sg;
        }
        k2 = ex - 4 * frac;
        if (*p) return 0;
        return build_float(sign, &N, &D, k2, kind, out);
    }
    int frac = 0, seen = 0, any = 0;
    while (isdigit((unsigned char)*p) || *p == '.') {
        if (*p == '.') { if (seen) return 0; seen = 1; p++; continue; }
        bmuladd(&N, 10, (u32)(*p - '0'));
        any = 1;
        if (seen) frac++;
        p++;
    }
    if (!any) return 0;
    int ex = 0;
    if (*p == 'e' || *p == 'E') {
        p++;
        int sg = 1;
        if (*p == '+') p++; else if (*p == '-') { sg = -1; p++; }
        while (isdigit((unsigned char)*p)) { ex = ex * 10 + (*p - '0'); if (ex > 100000) ex = 100000; p++; }
        ex *= sg;
    }
    if (*p) return 0;
    int e10 = ex - frac;
    if (big_iszero(&N)) return build_float(sign, &N, &D, 0, kind, out);
    if (e10 > 6000) {
        Big z; z.n = 0;
        FFmt f = ffmt_for(kind);
        put_bits(out, f.bytes, sign, (1 << f.ebits) - 1, &z, f.explicit_int ? 64 : f.mbits, f.ebits);
        if (f.explicit_int) out[7] |= 0x80;
        return 1;
    }
    if (e10 < -6000) {
        Big z; z.n = 0;
        FFmt f = ffmt_for(kind);
        put_bits(out, f.bytes, sign, 0, &z, f.explicit_int ? 64 : f.mbits, f.ebits);
        return 1;
    }
    if (e10 >= 0) bmulpow10(&N, e10);
    else bmulpow10(&D, -e10);
    return build_float(sign, &N, &D, 0, kind, out);
}
