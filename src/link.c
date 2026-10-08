#include "cly.h"

Layout g_lay;
static ImpDll *imps;
static int nimps;

static i64 round_up(i64 v, i64 a)
{
    if (a <= 1) return v;
    return (v + a - 1) / a * a;
}

static int sec_group(const Sec *s)
{
    if (!s->alloc) return 3;
    if (s->nobits) return 2;
    if (s->write) return 1;
    return 0;
}

i64 sec_addr(Obj *o, int idx)
{
    if (idx < 0 || idx >= o->nsecs) return 0;
    Sec *s = o->secs[idx];
    return s->has_vstart ? s->vstart : s->base;
}

static int builtin_sym(Obj *o, const char *name, i64 *out)
{
    (void)o;
    if (!strcmp(name, "__cly_image_end") || !strcmp(name, "_end") || !strcmp(name, "__bss_end")) { *out = g_lay.end; return 1; }
    if (!strcmp(name, "__image_base")) { *out = g_lay.base; return 1; }
    if (!strcmp(name, "_etext") || !strcmp(name, "__text_end")) { *out = g_lay.a_end; return 1; }
    if (!strcmp(name, "_edata") || !strcmp(name, "__data_end")) { *out = g_lay.file_end; return 1; }
    if (!strcmp(name, "__bss_start")) { *out = g_lay.file_end; return 1; }
    return 0;
}

i64 val_addr(Obj *o, const Val *v, int *ok);

i64 sym_addr(Obj *o, Sym *s)
{
    if (s->isequ) {
        int ok = 1;
        return val_addr(o, &s->eqv, &ok);
    }
    if (s->defined) {
        if (s->sec == SEC_ABS || s->sec < 0) return s->off;
        return sec_addr(o, s->sec) + s->off;
    }
    i64 b;
    if (builtin_sym(o, s->name, &b)) return b;
    return 0;
}

i64 val_addr(Obj *o, const Val *v, int *ok)
{
    i64 r = v->n;
    if (v->pos >= 0) r += sec_addr(o, v->pos);
    if (v->neg >= 0) r -= sec_addr(o, v->neg);
    if (v->ext) {
        Sym *s = v->ext;
        i64 b;
        if (s->defined || s->isequ) r += sym_addr(o, s);
        else if (builtin_sym(o, s->name, &b)) r += b;
        else if (ok) *ok = 0;
    }
    return r;
}

static void order_secs(Obj *o)
{
    free(g_lay.order);
    g_lay.order = xmalloc(sizeof(Sec *) * (size_t)(o->nsecs + 1));
    g_lay.n = 0;
    for (int g = 0; g < 3; g++) {
        if (g == 0) {
            for (int i = 0; i < o->nsecs; i++) {
                Sec *s = o->secs[i];
                if (!s->absolute && sec_group(s) == 0 && s->exec) g_lay.order[g_lay.n++] = s;
            }
            for (int i = 0; i < o->nsecs; i++) {
                Sec *s = o->secs[i];
                if (!s->absolute && sec_group(s) == 0 && !s->exec) g_lay.order[g_lay.n++] = s;
            }
        } else {
            for (int i = 0; i < o->nsecs; i++) {
                Sec *s = o->secs[i];
                if (!s->absolute && sec_group(s) == g) g_lay.order[g_lay.n++] = s;
            }
        }
    }
    for (int rep = 0; rep < g_lay.n; rep++) {
        int moved = 0;
        for (int i = 0; i < g_lay.n; i++) {
            Sec *s = g_lay.order[i];
            if (!s->follows || !*s->follows) continue;
            int t = -1;
            for (int k = 0; k < g_lay.n; k++) if (!strcmp(g_lay.order[k]->name, s->follows)) t = k;
            if (t < 0 || t == i - 1) continue;
            if (t == i) continue;
            for (int k = i; k < g_lay.n - 1; k++) g_lay.order[k] = g_lay.order[k + 1];
            g_lay.n--;
            if (t > i) t--;
            for (int k = g_lay.n; k > t + 1; k--) g_lay.order[k] = g_lay.order[k - 1];
            g_lay.order[t + 1] = s;
            g_lay.n++;
            moved = 1;
            break;
        }
        if (!moved) break;
    }
}

int link_layout(Obj *o, i64 base, i64 pagesz, int mode)
{
    order_secs(o);
    memset(&g_lay.base, 0, sizeof(i64) * 6 + sizeof(int) * 2);
    g_lay.base = base;
    i64 cur = base;
    int seen_rw = 0;
    g_lay.a_end = base;
    g_lay.file_end = base;
    for (int i = 0; i < g_lay.n; i++) {
        Sec *s = g_lay.order[i];
        int g = sec_group(s);
        if (s->size == 0 && !s->data.n) {
            s->base = cur;
            continue;
        }
        if ((mode == 1 || mode == 3) && pagesz && g >= 1 && !seen_rw) {
            g_lay.rw_lma = cur;
            i64 low = cur % pagesz;
            cur = round_up(cur, pagesz);
            if (mode == 3) cur += low;
            seen_rw = 1;
            g_lay.rw_start = cur;
            g_lay.has_rw = 1;
        }
        if (s->has_start) cur = s->start;
        int a = s->align > 0 ? s->align : 1;
        cur = round_up(cur, a);
        s->base = cur;
        cur += s->size;
        if (g == 0) g_lay.a_end = cur;
        if (g <= 1) g_lay.file_end = cur;
        if (g == 2) g_lay.has_bss = 1;
    }
    if (g_lay.a_end > g_lay.file_end) g_lay.file_end = g_lay.a_end;
    g_lay.end = cur;
    if (!g_lay.has_rw) { g_lay.rw_start = g_lay.file_end; g_lay.rw_lma = g_lay.file_end; }
    if (mode == 1) g_lay.rw_lma = g_lay.rw_start;
    return 1;
}

static void put_reloc(Obj *o, Reloc *r, i64 val)
{
    Sec *s = o->secs[r->sec];
    if (s->nobits) return;
    if (r->size < 1 || r->size > 8) return;
    if ((size_t)(r->off + r->size) > s->data.n) return;
    if (r->size < 8) {
        i64 lo = -((i64)1 << (r->size * 8 - 1));
        i64 hi = ((i64)1 << (r->size * 8)) - 1;
        if (val < lo || val > hi) {
            g_pos.file = "<link>";
            g_pos.line = r->line;
            err("relocation out of range (%lld does not fit in %d byte%s)%s", (long long)val, r->size, r->size > 1 ? "s" : "", r->size == 4 && o->is64 ? "; use rip-relative addressing (default rel, [rel x]) or lea reg, [rel x]" : "");
            return;
        }
    }
    bputle(&s->data, (size_t)r->off, (u64)val, r->size);
}

int link_apply(Obj *o)
{
    int bad = 0;
    for (int i = 0; i < o->nrelocs; i++) {
        Reloc *r = &o->relocs[i];
        int ok = 1;
        i64 v = val_addr(o, &r->v, &ok);
        if (!ok) {
            const char *nm = r->v.ext ? r->v.ext->name : "?";
            fprintf(stderr, "cly: undefined symbol `%s' cannot be resolved for this output type\n", nm);
            bad++;
            continue;
        }
        if (r->kind == RK_REL) v -= sec_addr(o, r->sec) + r->relbase;
        put_reloc(o, r, v);
    }
    return bad ? 0 : 1;
}

void link_range(Obj *o, Bytes *dst, i64 lo, i64 hi)
{
    (void)o;
    if (hi < lo) hi = lo;
    size_t start = dst->n;
    bfill(dst, 0, (size_t)(hi - lo));
    for (int i = 0; i < g_lay.n; i++) {
        Sec *s = g_lay.order[i];
        if (s->nobits || !s->alloc || !s->data.n) continue;
        i64 a = s->base, b = s->base + (i64)s->data.n;
        if (b <= lo || a >= hi) continue;
        i64 from = a < lo ? lo : a;
        i64 to = b > hi ? hi : b;
        memcpy(dst->p + start + (from - lo), s->data.p + (from - a), (size_t)(to - from));
    }
}

int link_image(Obj *o, Bytes *img)
{
    link_range(o, img, g_lay.base, g_lay.file_end);
    return 1;
}

i64 link_entry(Obj *o)
{
    if (!o->entry) return -1;
    return sym_addr(o, o->entry);
}

static Sec *add_sec(Obj *o, const char *name, int exec, int write)
{
    Sec *s = xcalloc(1, sizeof *s);
    s->name = xstrdup(name);
    s->idx = o->nsecs;
    s->alloc = 1;
    s->exec = exec;
    s->write = write;
    s->align = exec ? 4 : 4;
    o->secs = xrealloc(o->secs, sizeof(Sec *) * (size_t)(o->nsecs + 1));
    o->secs[o->nsecs++] = s;
    return s;
}

static void add_reloc(Obj *o, Reloc *r)
{
    if (o->nrelocs == o->caprelocs) {
        o->caprelocs = o->caprelocs ? o->caprelocs * 2 : 64;
        o->relocs = xrealloc(o->relocs, sizeof(Reloc) * (size_t)o->caprelocs);
    }
    o->relocs[o->nrelocs++] = *r;
}

void link_prepare_imports(Obj *o)
{
    nimps = 0;
    imps = NULL;
    int any = 0;
    for (int i = 0; i < o->nsyms; i++) if (o->syms[i]->import && o->syms[i]->referenced) any = 1;
    if (!any) return;
    int slots = 0;
    for (int i = 0; i < o->nsyms; i++) {
        Sym *s = o->syms[i];
        if (!s->import || !s->referenced) continue;
        const char *dll = s->impdll ? s->impdll : "kernel32.dll";
        int d = -1;
        for (int k = 0; k < nimps; k++) if (!strcasecmp(imps[k].dll, dll)) d = k;
        if (d < 0) {
            imps = xrealloc(imps, sizeof(ImpDll) * (size_t)(nimps + 1));
            memset(&imps[nimps], 0, sizeof(ImpDll));
            imps[nimps].dll = xstrdup(dll);
            d = nimps++;
        }
        const char *nm = s->impname ? s->impname : s->name;
        int found = 0;
        for (int k = 0; k < imps[d].n; k++) if (!strcmp(imps[d].names[k], nm)) found = 1;
        if (!found) {
            ImpDll *D = &imps[d];
            if (D->n == D->cap) {
                D->cap = D->cap ? D->cap * 2 : 8;
                D->names = xrealloc(D->names, sizeof(char *) * (size_t)D->cap);
                D->slot = xrealloc(D->slot, sizeof(i64) * (size_t)D->cap);
            }
            D->names[D->n] = xstrdup(nm);
            D->slot[D->n] = 0;
            D->n++;
            slots++;
        }
    }
    int sw = o->is64 ? 8 : 4;
    Sec *iat = add_sec(o, ".iat", 0, 1);
    iat->align = sw;
    int slot = 0;
    for (int d = 0; d < nimps; d++) {
        for (int k = 0; k < imps[d].n; k++) imps[d].slot[k] = (i64)slot++ * sw;
        slot++;
    }
    iat->size = (i64)slot * sw;
    bfill(&iat->data, 0, (size_t)iat->size);
    Sec *th = add_sec(o, ".thunk", 1, 0);
    th->align = 2;
    for (int i = 0; i < o->nsyms; i++) {
        Sym *s = o->syms[i];
        if (!s->import || !s->referenced) continue;
        const char *dll = s->impdll ? s->impdll : "kernel32.dll";
        const char *nm = s->impname ? s->impname : s->name;
        i64 so = 0;
        for (int d = 0; d < nimps; d++) {
            if (strcasecmp(imps[d].dll, dll)) continue;
            for (int k = 0; k < imps[d].n; k++) if (!strcmp(imps[d].names[k], nm)) so = imps[d].slot[k];
        }
        s->ext = 0;
        s->defined = 1;
        if (!strncmp(s->name, "__imp_", 6)) {
            s->sec = iat->idx;
            s->off = so;
        } else {
            s->sec = th->idx;
            s->off = th->size;
            u8 code[6] = { 0xFF, 0x25, 0, 0, 0, 0 };
            badd(&th->data, code, 6);
            Reloc r;
            memset(&r, 0, sizeof r);
            r.sec = th->idx;
            r.off = th->size + 2;
            r.size = 4;
            r.kind = o->is64 ? RK_REL : RK_ABS;
            r.relbase = th->size + 6;
            val_const(&r.v, so);
            r.v.pos = iat->idx;
            add_reloc(o, &r);
            th->size += 6;
        }
    }
}

const ImpDll *link_imports(int *n)
{
    *n = nimps;
    return imps;
}

static int bare_image(Obj *o, Bytes *out)
{
    Bytes img = {0};
    Bytes boot = {0};
    i64 base = 0x8000;
    link_layout(o, base, 0, 0);
    if (!link_apply(o)) return 0;
    if (!o->entry) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    link_image(o, &img);
    i64 entry = link_entry(o);
    bpad(&img, 512, 0);
    int nsect = (int)(img.n / 512);
    if (nsect < 1) { nsect = 1; bfill(&img, 0, 512); }
    if (nsect > 1000) {
        fprintf(stderr, "cly: image is too large for the boot loader (%d sectors)\n", nsect);
        return 0;
    }
    Str src = {0};
    rt_boot(&src, (u32)entry, nsect, (u32)g_lay.file_end, (u32)(g_lay.end - g_lay.file_end), o->is64 ? 64 : 32);
    AsmOpts ao;
    memset(&ao, 0, sizeof ao);
    ao.kernel = K_BARE;
    ao.type = T_FLAT;
    ao.flat_mode = 1;
    ao.cpu = 99;
    int ln = 1;
    const char *p = src.s;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char *t = xstrndup(p, len);
        lv_add(&ao.lines, t, "<boot>", ln++);
        free(t);
        if (!nl) break;
        p = nl + 1;
    }
    free(src.s);
    Obj bo;
    memset(&bo, 0, sizeof bo);
    int e0 = g_errors;
    if (!assemble(&ao, &bo) || g_errors != e0) {
        fprintf(stderr, "cly: internal error: boot sector failed to assemble\n");
        return 0;
    }
    Layout save = g_lay;
    memset(&g_lay, 0, sizeof g_lay);
    link_layout(&bo, 0x7C00, 0, 0);
    link_apply(&bo);
    link_image(&bo, &boot);
    g_lay = save;
    if (boot.n != 512) {
        fprintf(stderr, "cly: internal error: boot sector is %zu bytes\n", boot.n);
        return 0;
    }
    badd(out, boot.p, boot.n);
    badd(out, img.p, img.n);
    return 1;
}

int fmt_bare(Obj *o, Bytes *out)
{
    if (o->has_org) return fmt_flat(o, out);
    return bare_image(o, out);
}

int fmt_flat(Obj *o, Bytes *out)
{
    i64 base = o->has_org ? o->org : 0;
    link_layout(o, base, 0, 0);
    if (!link_apply(o)) return 0;
    return link_image(o, out);
}

void fmt_ihex(const Bytes *in, i64 base, i64 entry, Str *out)
{
    u32 upper = 0xFFFFFFFFu;
    for (size_t i = 0; i < in->n; i += 16) {
        u32 addr = (u32)(base + (i64)i);
        if ((addr >> 16) != upper || i == 0) {
            upper = addr >> 16;
            unsigned sum = 2 + 4 + ((upper >> 8) & 0xFF) + (upper & 0xFF);
            sfmt(out, ":02000004%04X%02X\n", upper, (-(int)sum) & 0xFF);
        }
        size_t n = in->n - i < 16 ? in->n - i : 16;
        if (((addr & 0xFFFF) + n) > 0x10000) n = 0x10000 - (addr & 0xFFFF);
        unsigned sum = (unsigned)n + ((addr >> 8) & 0xFF) + (addr & 0xFF);
        sfmt(out, ":%02X%04X00", (unsigned)n, addr & 0xFFFF);
        for (size_t k = 0; k < n; k++) {
            sfmt(out, "%02X", in->p[i + k]);
            sum += in->p[i + k];
        }
        sfmt(out, "%02X\n", (-(int)sum) & 0xFF);
        if (n < 16 && i + n < in->n) i = i + n - 16;
    }
    if (entry >= 0) {
        u32 e = (u32)entry;
        unsigned sum = 4 + 5 + ((e >> 24) & 0xFF) + ((e >> 16) & 0xFF) + ((e >> 8) & 0xFF) + (e & 0xFF);
        sfmt(out, ":04000005%08X%02X\n", e, (-(int)sum) & 0xFF);
    }
    sadd(out, ":00000001FF\n");
}

void fmt_srec(const Bytes *in, i64 base, i64 entry, Str *out)
{
    sadd(out, "S00600004844521B\n");
    for (size_t i = 0; i < in->n; i += 32) {
        size_t n = in->n - i < 32 ? in->n - i : 32;
        u32 addr = (u32)(base + (i64)i);
        unsigned sum = (unsigned)(n + 5) + ((addr >> 24) & 0xFF) + ((addr >> 16) & 0xFF) + ((addr >> 8) & 0xFF) + (addr & 0xFF);
        sfmt(out, "S3%02X%08X", (unsigned)(n + 5), addr);
        for (size_t k = 0; k < n; k++) {
            sfmt(out, "%02X", in->p[i + k]);
            sum += in->p[i + k];
        }
        sfmt(out, "%02X\n", (~sum) & 0xFF);
    }
    u32 e = entry >= 0 ? (u32)entry : (u32)base;
    unsigned sum = 5 + ((e >> 24) & 0xFF) + ((e >> 16) & 0xFF) + ((e >> 8) & 0xFF) + (e & 0xFF);
    sfmt(out, "S7%02X%08X%02X\n", 5, e, (~sum) & 0xFF);
}

int link_obj(Obj *o, int kernel, int type, Bytes *out)
{
    int plainbin = type == T_BIN;
    if (type == T_BIN) {
        type = kernel == K_WINDOWS ? T_EXE : kernel == K_MAC ? T_MACHO : kernel == K_LINUX ? T_ELF : T_IMG;
        if (kernel == K_BARE) type = T_IMG;
    }
    switch (type) {
    case T_ELF: return fmt_elf(o, out);
    case T_EXE: return fmt_pe(o, out);
    case T_MACHO: return fmt_macho(o, out);
    case T_OBJ: return kernel == K_WINDOWS ? fmt_coff(o, out) : fmt_elfobj(o, out);
    case T_COFF: return fmt_coff(o, out);
    case T_FLAT:
    case T_COM:
    case T_HEX:
    case T_SREC: {
        Bytes img = {0};
        if (!fmt_flat(o, &img)) return 0;
        if (type == T_HEX || type == T_SREC) {
            Str s = {0};
            i64 base = o->has_org ? o->org : 0;
            i64 ent = o->entry ? sym_addr(o, o->entry) : -1;
            if (type == T_HEX) fmt_ihex(&img, base, ent, &s);
            else fmt_srec(&img, base, ent, &s);
            badd(out, s.s ? s.s : "", s.n);
            return 1;
        }
        badd(out, img.p, img.n);
        return 1;
    }
    case T_IMG:
    case T_BIN: {
        Bytes img = {0};
        if (!fmt_bare(o, &img)) return 0;
        if (type == T_IMG && !plainbin && img.n < 1474560) bfill(&img, 0, 1474560 - img.n);
        badd(out, img.p, img.n);
        return 1;
    }
    }
    return 0;
}
