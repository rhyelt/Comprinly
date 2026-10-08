#include "cly.h"
#include "insn.h"

typedef struct {
    Bytes b;
    Reloc *r;
    int nr, cap;
} Chunk;

enum { K_NONE, K_CODE, K_DATA, K_BSS, K_EQU };

typedef struct {
    AsmOpts *o;
    LineVec *lines;
    HT symtab;
    Sym **syms;
    int nsyms, capsyms;
    Sec **secs;
    int nsecs, capsecs;
    Sec absec;
    Sec *text, *data, *bss, *vregsec;
    Sec *cur, *last, *stmt;
    int explicit_mode;
    int has_org;
    i64 org;
    int bits, default_bits, defrel;
    int cpu;
    int pass, changed, final;
    u8 *wide;
    int nwide;
    char *lastlabel;
    char **pending;
    int npending, cappending;
    int pend_align;
    Bytes pend_fill;
    int has_pend_fill;
    int pend_smart;
    int smart_on, smart_mode, smart_thr;
    Reloc *relocs;
    int nrelocs, caprelocs;
    int cur_line;
    char *struc_name;
    Sec *struc_save_cur;
    int struc_active;
    char *istruc_name;
    char *istruc_savelbl;
    Val istruc_start;
    Sec *istruc_sec;
    int uses_syscall;
    int uses_vreg;
    i64 *psize;
    int npsize;
    Sym *entry;
    i64 totalsize;
    Chunk *lst_chunks;
    i64 *lst_off;
    int *lst_sec;
    u8 **lst_bytes;
    int *lst_len;
    int kernel;
    int type;
} AS;

static AS A;

static void chunk_reloc(Chunk *c, const Val *v, int size, int kind, i64 off)
{
    if (c->nr == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->r = xrealloc(c->r, sizeof(Reloc) * (size_t)c->cap);
    }
    Reloc *r = &c->r[c->nr++];
    memset(r, 0, sizeof *r);
    r->off = off;
    r->size = size;
    r->kind = kind;
    r->v = *v;
}

static void chunk_val(Chunk *c, const Val *v, int size)
{
    if (v->unk || val_isconst(v)) {
        i64 n = v->n;
        if (!v->unk) {
            int ok = 1;
            if (size == 1) ok = n >= -128 && n <= 255;
            else if (size == 2) ok = n >= -32768 && n <= 65535;
            else if (size == 4) ok = n >= -2147483648LL && n <= 4294967295LL;
            if (!ok) warn("%s data exceeds bounds", size == 1 ? "byte" : (size == 2 ? "word" : "dword"));
        }
        if (size <= 8) badle(&c->b, (u64)n, size);
        else {
            badle(&c->b, (u64)n, 8);
            bfill(&c->b, n < 0 ? 0xFF : 0, (size_t)(size - 8));
        }
        return;
    }
    int rs = size > 4 ? 4 : size;
    chunk_reloc(c, v, rs, RK_ABS, (i64)c->b.n);
    badle(&c->b, 0, size);
}

static void chunk_free(Chunk *c)
{
    free(c->b.p);
    free(c->r);
    memset(c, 0, sizeof *c);
}

static Sym *get_sym(const char *name)
{
    Sym *s = ht_get(&A.symtab, name);
    if (s) return s;
    s = xcalloc(1, sizeof *s);
    s->name = xstrdup(name);
    s->sec = SEC_ABS;
    s->defpass = -1;
    ht_put(&A.symtab, name, s);
    if (A.nsyms == A.capsyms) {
        A.capsyms = A.capsyms ? A.capsyms * 2 : 64;
        A.syms = xrealloc(A.syms, sizeof(Sym *) * (size_t)A.capsyms);
    }
    A.syms[A.nsyms++] = s;
    return s;
}

Sym *sym_find(const char *name) { return ht_get(&A.symtab, name); }

static char *full_name(const char *name)
{
    if (name[0] == '.' && name[1] != '.' && A.lastlabel && A.lastlabel[0]) {
        Str s = {0};
        sadd(&s, A.lastlabel);
        sadd(&s, name);
        return sget(&s);
    }
    return xstrdup(name);
}

static Sec *new_sec(const char *name)
{
    Sec *s = xcalloc(1, sizeof *s);
    s->name = xstrdup(name);
    s->idx = A.nsecs;
    s->alloc = 1;
    s->align = 4;
    if (A.nsecs == A.capsecs) {
        A.capsecs = A.capsecs ? A.capsecs * 2 : 8;
        A.secs = xrealloc(A.secs, sizeof(Sec *) * (size_t)A.capsecs);
    }
    A.secs[A.nsecs++] = s;
    return s;
}

static void sec_defaults(Sec *s)
{
    const char *n = s->name;
    if (!strncmp(n, ".text", 5) || !strcmp(n, "CODE") || !strcmp(n, "_TEXT")) { s->exec = 1; s->align = 16; }
    else if (!strncmp(n, ".bss", 4) || !strcmp(n, "BSS") || !strcmp(n, "_BSS")) { s->nobits = 1; s->write = 1; s->align = 4; }
    else if (!strncmp(n, ".data", 5) || !strcmp(n, "DATA") || !strcmp(n, "_DATA")) { s->write = 1; s->align = 4; }
    else if (!strncmp(n, ".rodata", 7)) { s->align = 4; }
    else { s->write = 1; s->align = 4; }
}

static Sec *find_sec(const char *name)
{
    for (int i = 0; i < A.nsecs; i++) if (!strcmp(A.secs[i]->name, name)) return A.secs[i];
    return NULL;
}

static int pow2_ok(i64 n) { return n > 0 && (n & (n - 1)) == 0; }

static void ensure_vregsec(void);

static int asm_sym_lookup(const char *name, Val *out)
{
    if (!strcmp(name, "__cly_vregs")) {
        ensure_vregsec();
        A.uses_vreg = 1;
        val_const(out, 0);
        out->pos = A.vregsec->idx;
        return 1;
    }
    if (!strncmp(name, "section.", 8)) {
        const char *dot = strrchr(name, '.');
        if (dot && dot > name + 8) {
            char *mid = xstrndup(name + 8, (size_t)(dot - name - 8));
            Sec *sc = NULL;
            for (int i = 0; i < A.nsecs; i++) if (!strcmp(A.secs[i]->name, mid)) sc = A.secs[i];
            free(mid);
            if (sc) {
                val_const(out, 0);
                if (!strcmp(dot, ".start") || !strcmp(dot, ".vstart")) { out->pos = sc->idx; return 1; }
                if (!strcmp(dot, ".length")) {
                    out->n = sc->idx < A.npsize ? A.psize[sc->idx] : 0;
                    return 1;
                }
            }
        }
    }
    char *fn = full_name(name);
    Sym *s = get_sym(fn);
    free(fn);
    if (!s->referenced) { s->reffile = g_pos.file; s->refline = g_pos.line; }
    s->referenced = 1;
    val_const(out, 0);
    if (s->ext || s->common) {
        out->ext = s;
        return 1;
    }
    if (!s->defined) return 0;
    if (s->isequ) { *out = s->eqv; return 1; }
    if (s->sec == SEC_ABS) { out->n = s->off; return 1; }
    out->pos = s->sec;
    out->n = s->off;
    return 1;
}

static void asm_cur_loc(Val *out)
{
    Sec *s = A.stmt ? A.stmt : A.text;
    val_const(out, 0);
    if (s->absolute) { out->n = s->size; return; }
    out->pos = s->idx;
    out->n = s->size;
}

static void asm_sec_start(Val *out)
{
    Sec *s = A.stmt ? A.stmt : A.text;
    val_const(out, 0);
    if (s->absolute) { out->n = s->start; return; }
    out->pos = s->idx;
}

static const char *skipws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static const char *scan_word(const char *p, char *buf, size_t sz)
{
    size_t n = 0;
    if (*p == '$') { if (n < sz - 1) buf[n++] = *p; p++; }
    while (ident_char((unsigned char)*p) && n < sz - 1) buf[n++] = *p++;
    buf[n] = 0;
    return p;
}


static void def_label_now(const char *fname, Sec *s)
{
    Sym *sym = get_sym(fname);
    if (sym->ext) { err("symbol `%s' redefined: was declared extern", fname); return; }
    if (sym->defpass == A.pass) { err("symbol `%s' redefined", fname); return; }
    int sec = s->absolute ? SEC_ABS : s->idx;
    i64 off = s->size;
    if (!sym->defined || sym->isequ || sym->sec != sec || sym->off != off) A.changed = 1;
    sym->defined = 1;
    sym->isequ = 0;
    sym->sec = sec;
    sym->off = off;
    sym->defpass = A.pass;
    sym->defline = A.cur_line;
}

static void flush_labels(Sec *s)
{
    for (int i = 0; i < A.npending; i++) {
        def_label_now(A.pending[i], s);
        free(A.pending[i]);
    }
    A.npending = 0;
}

static void add_pending(const char *fname)
{
    if (A.npending == A.cappending) {
        A.cappending = A.cappending ? A.cappending * 2 : 16;
        A.pending = xrealloc(A.pending, sizeof(char *) * (size_t)A.cappending);
    }
    A.pending[A.npending++] = xstrdup(fname);
}

static void sec_pad(Sec *s, i64 n, u8 fill, const Bytes *pat)
{
    if (n <= 0) return;
    if (s->absolute) { s->size += n; return; }
    if (s->nobits) { s->size += n; return; }
    if (pat && pat->n) {
        for (i64 i = 0; i < n; i++) bad1(&s->data, pat->p[i % (i64)pat->n]);
    } else bfill(&s->data, fill, (size_t)n);
    s->size += n;
}

static void smart_pad(Sec *s, i64 n)
{
    static const char *g[] = { "", "90", "89f6", "8d7600", "8d742600", "908d742600", "8db600000000", "8db42600000000" };
    static const char *k7[] = { "", "90", "8bc0", "8d0420", "8d442000", "8d44200090", "8d8000000000", "8d040500000000" };
    static const char *p6[] = { "", "90", "6690", "0f1f00", "0f1f4000", "0f1f440000", "660f1f440000", "0f1f8000000000", "0f1f840000000000" };
    static const char *k8[] = { "", "90", "6690", "666690", "66666690" };
    const char **tab = g;
    int maxlen = 7;
    if (A.smart_mode == 2) { tab = k7; maxlen = 7; }
    else if (A.smart_mode == 3) { tab = k8; maxlen = 4; }
    else if (A.smart_mode == 4) { tab = p6; maxlen = 8; }
    int thr = A.smart_thr;
    if (A.smart_mode == 0) { thr = 16; maxlen = 1; tab = g; }
    if (s->nobits || s->absolute) { s->size += n; return; }
    Bytes b = {0};
    if (n > thr) {
        if (n - 2 <= 127) {
            bad1(&b, 0xeb);
            bad1(&b, (u8)(n - 2));
            for (i64 i = 2; i < n; i++) bad1(&b, 0x90);
        } else {
            bad1(&b, 0xe9);
            badle(&b, (u64)(n - 5), 4);
            for (i64 i = 5; i < n; i++) bad1(&b, 0x90);
        }
    } else {
        i64 left = n;
        while (left > 0) {
            int l = left > maxlen ? maxlen : (int)left;
            const char *h = A.smart_mode == 0 ? "90" : tab[l];
            for (const char *q = h; *q; q += 2) {
                char hx[3] = { q[0], q[1], 0 };
                bad1(&b, (u8)strtol(hx, NULL, 16));
            }
            left -= l;
        }
    }
    badd(&s->data, b.p, b.n);
    s->size += n;
    free(b.p);
}

static void apply_align(Sec *s)
{
    if (A.pend_align > 1) {
        i64 n = A.pend_align;
        i64 r = s->size % n;
        if (r && A.pend_smart && A.smart_on && !A.has_pend_fill) smart_pad(s, n - r);
        else if (r) sec_pad(s, n - r, s->exec ? 0x90 : 0, A.has_pend_fill ? &A.pend_fill : NULL);
        if (!s->absolute && n > s->align) s->align = (int)n;
    }
    A.pend_align = 0;
    A.pend_smart = 0;
    A.has_pend_fill = 0;
    A.pend_fill.n = 0;
}

static Sec *pick_sec(int kind)
{
    if (A.cur) return A.cur;
    switch (kind) {
    case K_CODE: return A.text;
    case K_BSS: return A.bss;
    default: return A.data;
    }
}

static Sec *begin_emit(int kind)
{
    Sec *s = kind == K_NONE || kind == K_EQU ? (A.cur ? A.cur : (A.last ? A.last : A.text)) : pick_sec(kind);
    if (kind == K_NONE || kind == K_EQU) {
        A.stmt = s;
        return s;
    }
    apply_align(s);
    flush_labels(s);
    A.stmt = s;
    return s;
}

static void commit_chunk(Sec *s, Chunk *c, i64 size_only)
{
    i64 base = s->size;
    if (s->absolute) {
        s->size += size_only >= 0 ? size_only : (i64)c->b.n;
        return;
    }
    if (s->nobits) {
        s->size += size_only >= 0 ? size_only : (i64)c->b.n;
        return;
    }
    badd(&s->data, c->b.p, c->b.n);
    s->size += (i64)c->b.n;
    for (int i = 0; i < c->nr; i++) {
        if (A.nrelocs == A.caprelocs) {
            A.caprelocs = A.caprelocs ? A.caprelocs * 2 : 64;
            A.relocs = xrealloc(A.relocs, sizeof(Reloc) * (size_t)A.caprelocs);
        }
        Reloc r = c->r[i];
        r.sec = s->idx;
        r.off += base;
        if (r.kind == RK_REL) r.relbase += base;
        r.line = A.cur_line;
        A.relocs[A.nrelocs++] = r;
    }
    A.last = s;
}

static void note_list(Sec *s, i64 off, const u8 *p, size_t n)
{
    if (!A.o->list || !A.final) return;
    int i = A.cur_line;
    A.lst_sec[i] = s->idx;
    if (A.lst_len[i] == 0) A.lst_off[i] = off;
    int old = A.lst_len[i];
    A.lst_bytes[i] = xrealloc(A.lst_bytes[i], (size_t)old + n + 1);
    if (n && p) memcpy(A.lst_bytes[i] + old, p, n);
    else if (n) memset(A.lst_bytes[i] + old, 0, n);
    A.lst_len[i] = old + (int)n;
}

static int is_kw(const char *w, const char *k) { return !strcasecmp(w, k); }

typedef struct { const char *n; int size; } KW;
static const KW sizekw[] = {
    { "byte", 1 }, { "word", 2 }, { "dword", 4 }, { "qword", 8 }, { "tword", 10 }, { "oword", 16 }, { "yword", 32 }, { "zword", 64 }, { NULL, 0 }
};

static int is_segreg(const char *w, int *reg)
{
    int sz;
    int r = reg_lookup(w, &sz);
    if (r && REGCLS(r) == RC_SEG) { *reg = r; return 1; }
    return 0;
}

static void ensure_vregsec(void)
{
    if (!A.vregsec) {
        A.vregsec = new_sec(".cly_vreg");
        A.vregsec->nobits = 1;
        A.vregsec->write = 1;
        A.vregsec->align = 4;
        A.vregsec->size = 32;
    }
}

static int lin_to_ea(Val *v, Op *o, int *err_out)
{
    int base = -1, idx = -1, scale = 1;
    int order[VR_N];
    int no = 0;
    for (int i = 0; i < v->nrseq; i++) order[no++] = v->rseq[i];
    for (int i = 0; i < VR_N; i++) {
        if (!v->r[i]) continue;
        int f = 0;
        for (int j = 0; j < no; j++) if (order[j] == i) f = 1;
        if (!f) order[no++] = i;
    }
    int vi = -1, vsc = 1;
    for (int k = 0; k < no; k++) {
        int i = order[k];
        int c = v->r[i];
        if (!c) continue;
        if (i >= 64) {
            if (vi >= 0 || (c != 1 && c != 2 && c != 4 && c != 8)) { err("invalid effective address: bad vector index"); *err_out = 1; return 0; }
            vi = i;
            vsc = c;
            continue;
        }
        if (c < 0 || c > 9) { err("invalid effective address"); *err_out = 1; return 0; }
        if (c == 1) {
            if (base < 0) base = i;
            else if (idx < 0) { idx = i; scale = 1; }
            else { err("invalid effective address: too many registers"); *err_out = 1; return 0; }
        } else if (c == 2 || c == 4 || c == 8) {
            if (idx < 0) { idx = i; scale = c; }
            else if (base < 0 && scale == 1) { base = idx; idx = i; scale = c; }
            else { err("invalid effective address: too many index registers"); *err_out = 1; return 0; }
        } else if (c == 3 || c == 5 || c == 9) {
            if (base < 0 && idx < 0) { base = i; idx = i; scale = c - 1; }
            else { err("invalid effective address"); *err_out = 1; return 0; }
        } else { err("invalid effective address scale"); *err_out = 1; return 0; }
    }
    if (vi >= 0) {
        if (idx >= 0) { err("invalid effective address: too many index registers"); *err_out = 1; return 0; }
        o->base = base < 0 ? 0 : slot_reg(base);
        o->index = slot_reg(vi);
        o->scale = vsc;
        return 1;
    }
    if (idx >= 0 && scale == 1 && (idx & 15) == 4 && base >= 0 && (base & ~15) == (idx & ~15)) {
        int t = base; base = idx; idx = t;
    }
    if (base < 0 && idx >= 0 && scale == 2 && !(o->flags & OF_NOSPLIT)) { base = idx; scale = 1; }
    o->base = base < 0 ? 0 : slot_reg(base);
    o->index = idx < 0 ? 0 : slot_reg(idx);
    if (o->index && REGCLS(o->index) == RC_RIP) { err("rip cannot be used as an index register"); *err_out = 1; return 0; }
    o->scale = scale;
    return 1;
}

static int parse_modifiers(const char **pp, Op *o)
{
    const char *p = *pp;
    for (;;) {
        p = skipws(p);
        char w[32];
        const char *e = scan_word(p, w, sizeof w);
        if (e == p) break;
        int found = 0;
        for (int i = 0; sizekw[i].n; i++)
            if (is_kw(w, sizekw[i].n)) { o->size = sizekw[i].size; found = 1; break; }
        if (!found) {
            if (is_kw(w, "short")) { o->flags |= OF_SHORT; found = 1; }
            else if (is_kw(w, "near")) { o->flags |= OF_NEAR; found = 1; }
            else if (is_kw(w, "far")) { o->flags |= OF_FAR; found = 1; }
            else if (is_kw(w, "strict")) { o->flags |= OF_STRICT; found = 1; }
            else if (is_kw(w, "to")) { o->flags |= OF_TO; found = 1; }
            else if (is_kw(w, "nosplit")) { o->flags |= OF_NOSPLIT; found = 1; }
        }
        if (!found) break;
        const char *q = skipws(e);
        if (!*q || *q == ',' || *q == ';') {
            if (found) { *pp = e; return 0; }
        }
        p = e;
    }
    *pp = p;
    return 1;
}

static int parse_memory(const char **pp, Op *o)
{
    const char *p = *pp;
    p = skipws(p + 1);
    int dsz = 0;
    for (;;) {
        char w[32];
        const char *e = scan_word(p, w, sizeof w);
        if (e == p) break;
        int found = 0;
        if (is_kw(w, "byte")) { dsz = 1; found = 1; }
        else if (is_kw(w, "word")) { dsz = 2; found = 1; }
        else if (is_kw(w, "dword")) { dsz = 4; found = 1; }
        else if (is_kw(w, "qword") && A.bits == 64) { dsz = 8; found = 1; }
        else if (is_kw(w, "nosplit")) { o->flags |= OF_NOSPLIT; found = 1; }
        else if (is_kw(w, "rel")) { o->flags |= OF_REL; found = 1; }
        else if (is_kw(w, "abs")) { o->flags |= OF_ABS; found = 1; }
        else if (is_kw(w, "a16")) { o->asize = 16; found = 1; }
        else if (is_kw(w, "a32")) { o->asize = 32; found = 1; }
        if (!found) break;
        const char *q = skipws(e);
        if (*q == ']' || *q == ':') break;
        p = skipws(e);
    }
    o->dispsize = dsz;
    {
        char w[32];
        const char *e = scan_word(p, w, sizeof w);
        int sr;
        if (e != p && *e == ':' && is_segreg(w, &sr) && e[1] != ':') {
            o->mseg = sr;
            p = skipws(e + 1);
        }
    }
    Val v;
    if (!expr_parse(&p, &v, EF_EA)) return 0;
    p = skipws(p);
    if (*p != ']') { err("expecting `]' at end of effective address"); return 0; }
    p++;
    int eo = 0;
    if (!lin_to_ea(&v, o, &eo)) return 0;
    if (!o->base && !o->index && (dsz == 2 || dsz == 4 || dsz == 8)) {
        if (A.bits != 64 || dsz == 8) o->asize = dsz == 2 ? 16 : (dsz == 4 ? 32 : 64);
        o->dispsize = 0;
    }
    v.hasreg = 0;
    memset(v.r, 0, sizeof v.r);
    v.nrseq = 0;
    o->val = v;
    o->kind = OK_MEM;
    *pp = p;
    return 1;
}

static void vreg_to_mem(Op *o, int regid, int size)
{
    ensure_vregsec();
    A.uses_vreg = 1;
    Val v;
    val_const(&v, 4 * REGNUM(regid));
    v.pos = A.vregsec->idx;
    memset(o, 0, sizeof *o);
    o->kind = OK_MEM;
    o->val = v;
    o->scale = 1;
    o->size = size;
    o->vreg = REGNUM(regid) + 1;
    o->vsize = size;
}

static int operand_start(const char *p)
{
    p = skipws(p);
    if (!*p || *p == ',' || *p == ';') return 0;
    return ident_start((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '{' || *p == '[' || *p == '$' || *p == '"' || *p == '\'' || *p == '`' || *p == '(' || *p == '-' || *p == '+' || *p == '~' || (unsigned char)*p == 0xE2;
}

static int parse_operand0(const char **pp, Op *o)
{
    memset(o, 0, sizeof *o);
    o->scale = 1;
    val_const(&o->val, 0);
    val_const(&o->seg, 0);
    const char *p = skipws(*pp);
    int onlykw = 0;
    (void)onlykw;
    if (!parse_modifiers(&p, o)) {
        *pp = p;
        o->kind = OK_IMM;
        return 1;
    }
    p = skipws(p);
    char w[256];
    const char *e = scan_word(p, w, sizeof w);
    int sr;
    if (e != p && *e == ':' && e[1] != ':' && is_segreg(w, &sr)) {
        const char *q = skipws(e + 1);
        if (*q == '[') {
            if (!parse_memory(&q, o)) return 0;
            o->mseg = sr;
            if (!o->size) {}
            *pp = q;
            return 1;
        }
    }
    if (*p == '[') {
        int sz = o->size, fl = o->flags;
        if (!parse_memory(&p, o)) return 0;
        o->size = sz;
        o->flags = fl | (o->flags & (OF_NOSPLIT | OF_REL | OF_ABS));
        *pp = p;
        return 1;
    }
    if (e != p && w[0] != '$') {
        int rsz;
        int rid = reg_lookup(w, &rsz);
        if (rid) {
            const char *q = skipws(e);
            if (*q != ':' || q[1] == ':') {
                if (REGCLS(rid) == RC_VREG) {
                    int fl = o->flags;
                    vreg_to_mem(o, rid, rsz);
                    o->flags = fl;
                } else {
                    o->kind = OK_REG;
                    o->reg = rid;
                }
                *pp = e;
                return 1;
            }
        }
    }
    Val v;
    if (!expr_parse(&p, &v, 0)) return 0;
    p = skipws(p);
    if (*p == ':' && p[1] != ':') {
        Val off;
        p++;
        {
            Op m;
            memset(&m, 0, sizeof m);
            (void)m;
        }
        if (!parse_modifiers(&p, o)) { *pp = p; return 0; }
        if (!expr_parse(&p, &off, 0)) return 0;
        o->kind = OK_FAR;
        o->seg = v;
        o->val = off;
        *pp = p;
        return 1;
    }
    o->kind = OK_IMM;
    o->val = v;
    *pp = p;
    return 1;
}

static int parse_deco(const char **pp, Op *o)
{
    const char *p = skipws(*pp);
    while (*p == '{') {
        const char *q = p + 1;
        char w[32];
        size_t n = 0;
        while (*q && *q != '}' && n < sizeof w - 1) w[n++] = *q++;
        w[n] = 0;
        if (*q != '}') { err("unterminated `{' decorator"); return 0; }
        q++;
        char *t = w;
        while (*t == ' ') t++;
        for (size_t k = strlen(t); k && t[k - 1] == ' '; k--) t[k - 1] = 0;
        if ((t[0] == 'k' || t[0] == 'K') && t[1] >= '0' && t[1] <= '7' && !t[2] && t[1] != '0') {
            if (o->kreg) { err("opmask k%d is already set", o->kreg); return 0; }
            o->kreg = t[1] - '0';
        } else if (!strcasecmp(t, "z")) o->zero = 1;
        else if (!strncmp(t, "1to", 3) && isdigit((unsigned char)t[3])) {
            int n = atoi(t + 3);
            if (n != 2 && n != 4 && n != 8 && n != 16 && n != 32) { err("`%s' is not a valid decorator with braces", t); return 0; }
            o->bcst = n;
        }
        else if (!strcasecmp(t, "rn-sae")) o->rc = 1;
        else if (!strcasecmp(t, "rd-sae")) o->rc = 2;
        else if (!strcasecmp(t, "ru-sae")) o->rc = 3;
        else if (!strcasecmp(t, "rz-sae")) o->rc = 4;
        else if (!strcasecmp(t, "sae")) o->rc = 5;
        else { err("`%s' is not a valid decorator with braces", t); return 0; }
        p = skipws(q);
    }
    *pp = p;
    return 1;
}

static int parse_operand(const char **pp, Op *o)
{
    if (!parse_operand0(pp, o)) return 0;
    return parse_deco(pp, o);
}

static int parse_operands(const char **pp, Op *ops, int *n)
{
    const char *p = skipws(*pp);
    *n = 0;
    while (*p && *p != ';') {
        if (*n >= 5) { err("too many operands"); return 0; }
        if (*p == '{') {
            if (!*n) { err("decorator without an operand"); return 0; }
            Op d;
            memset(&d, 0, sizeof d);
            if (!parse_deco(&p, &d)) return 0;
            if (d.rc) ops[*n - 1].rc = d.rc;
            if (d.kreg) ops[*n - 1].kreg = d.kreg;
            if (d.zero) ops[*n - 1].zero = 1;
            if (d.bcst) ops[*n - 1].bcst = d.bcst;
            p = skipws(p);
            if (*p == ',') { p = skipws(p + 1); continue; }
            break;
        }
        if (!parse_operand(&p, &ops[*n])) return 0;
        (*n)++;
        p = skipws(p);
        if (*p == ',') { p = skipws(p + 1); if (!*p || *p == ';') { err("expected operand after `,'"); return 0; } continue; }
        if (*p && *p != ';' && operand_start(p)) continue;
        break;
    }
    *pp = p;
    return 1;
}

static int is_float_start(const char *p)
{
    const char *q = p;
    if (*q == '-' || *q == '+') q++;
    q = skipws(q);
    if (!strncmp(q, "__", 2) || !strncmp(q, "__?", 3)) {
        char w[64];
        scan_word(q, w, sizeof w);
        const char *n = w + 2;
        if (!strncasecmp(n, "infinity", 8) || !strncasecmp(n, "qnan", 4) || !strncasecmp(n, "snan", 4) || !strncasecmp(n, "nan", 3)) return 1;
        if (!strncasecmp(n, "?infinity", 9) || !strncasecmp(n, "?qnan", 5) || !strncasecmp(n, "?snan", 5) || !strncasecmp(n, "?nan", 4)) return 1;
        return 0;
    }
    if (!isdigit((unsigned char)*q)) return 0;
    if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) {
        const char *t = q + 2;
        while (isxdigit((unsigned char)*t)) t++;
        return *t == '.' || *t == 'p' || *t == 'P';
    }
    const char *end;
    i64 n;
    int isf = 0;
    if (parse_number_tok(q, &end, &n, &isf)) return isf;
    return 0;
}

static int float_text(const char **pp, char *buf, size_t sz)
{
    const char *p = skipws(*pp);
    size_t n = 0;
    if (*p == '-' || *p == '+') buf[n++] = *p++;
    p = skipws(p);
    if (!strncmp(p, "__", 2)) {
        while (ident_char((unsigned char)*p) && n < sz - 1) buf[n++] = *p++;
        buf[n] = 0;
        *pp = p;
        return 1;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        buf[n++] = *p++;
        buf[n++] = *p++;
        while ((isxdigit((unsigned char)*p) || *p == '.' || *p == '_') && n < sz - 2) { if (*p != '_') buf[n++] = *p; p++; }
        if (*p == 'p' || *p == 'P') {
            buf[n++] = *p++;
            if (*p == '+' || *p == '-') buf[n++] = *p++;
            while (isdigit((unsigned char)*p) && n < sz - 2) buf[n++] = *p++;
        }
    } else {
        while ((isdigit((unsigned char)*p) || *p == '.' || *p == '_') && n < sz - 2) { if (*p != '_') buf[n++] = *p; p++; }
        if (*p == 'e' || *p == 'E') {
            buf[n++] = *p++;
            if (*p == '+' || *p == '-') buf[n++] = *p++;
            while (isdigit((unsigned char)*p) && n < sz - 2) buf[n++] = *p++;
        }
    }
    buf[n] = 0;
    *pp = p;
    return 1;
}

static int utf_encode(const char *name, const char *s, size_t len, Chunk *c)
{
    const char *n = name;
    if (!strncmp(n, "__?", 3)) n += 3; else n += 2;
    int bits = !strncasecmp(n, "utf16", 5) ? 16 : 32;
    int be = strstr(n, "be") != NULL;
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    while (i < len) {
        u32 cp;
        if (u[i] < 0x80) { cp = u[i]; i++; }
        else if ((u[i] & 0xE0) == 0xC0 && i + 1 < len) { cp = (u32)((u[i] & 0x1F) << 6) | (u[i + 1] & 0x3F); i += 2; }
        else if ((u[i] & 0xF0) == 0xE0 && i + 2 < len) { cp = (u32)((u[i] & 0x0F) << 12) | (u32)((u[i + 1] & 0x3F) << 6) | (u[i + 2] & 0x3F); i += 3; }
        else if ((u[i] & 0xF8) == 0xF0 && i + 3 < len) { cp = (u32)((u[i] & 7) << 18) | (u32)((u[i + 1] & 0x3F) << 12) | (u32)((u[i + 2] & 0x3F) << 6) | (u[i + 3] & 0x3F); i += 4; }
        else { cp = u[i]; i++; }
        u32 units[2];
        int nu = 1;
        if (bits == 16) {
            if (cp >= 0x10000) {
                cp -= 0x10000;
                units[0] = 0xD800 + (cp >> 10);
                units[1] = 0xDC00 + (cp & 0x3FF);
                nu = 2;
            } else units[0] = cp;
        } else units[0] = cp;
        for (int k = 0; k < nu; k++) {
            int nb = bits / 8;
            for (int b = 0; b < nb; b++) {
                int sh = be ? 8 * (nb - 1 - b) : 8 * b;
                bad1(&c->b, (u8)(units[k] >> sh));
            }
        }
    }
    return 1;
}

static int data_items(const char **ppos, int unit, Chunk *c, int depth)
{
    const char *p = skipws(*ppos);
    int first = 1;
    while (*p && *p != ';') {
        if (!first) {
            if (*p == ',') p = skipws(p + 1);
            else if (!operand_start(p) && *p != '?') break;
        }
        first = 0;
        if (!*p || *p == ';') { if (c->b.n == 0 && depth == 0) {} break; }
        if (*p == ')' && depth > 0) break;
        if (*p == '?') {
            p++;
            bfill(&c->b, 0, (size_t)unit);
            p = skipws(p);
            continue;
        }
        if (!strncasecmp(p, "__utf", 5) || !strncasecmp(p, "__?utf", 6)) {
            char w[40];
            const char *e = scan_word(p, w, sizeof w);
            e = skipws(e);
            if (*e == '(') {
                e = skipws(e + 1);
                Str s = {0};
                if (!parse_charconst(&e, &s)) { err("string expected in `%s'", w); return 0; }
                e = skipws(e);
                if (*e != ')') { err("expecting `)'"); return 0; }
                e++;
                utf_encode(w, s.s ? s.s : "", s.n, c);
                free(s.s);
                p = skipws(e);
                continue;
            }
        }
        unsigned char c0 = (unsigned char)*p;
        if (c0 == '"' || c0 == '\'' || c0 == '`' || c0 == 0xE2) {
            const char *q = p;
            Str s = {0};
            parse_charconst(&q, &s);
            const char *r = skipws(q);
            if (!*r || *r == ',' || *r == ';' || (*r == ')' && depth > 0) || !strncasecmp(r, "dup", 3)) {
                if (!strncasecmp(r, "dup", 3) && !ident_char((unsigned char)r[3])) goto expr_path;
                badd(&c->b, s.s, s.n);
                size_t rem = s.n % (size_t)unit;
                if (rem && unit > 1) bfill(&c->b, 0, (size_t)unit - rem);
                free(s.s);
                p = r;
                continue;
            }
            free(s.s);
        }
        if (unit >= 2 && unit != 1 && is_float_start(p)) {
            char buf[256];
            const char *q = p;
            float_text(&q, buf, sizeof buf);
            u8 out[16];
            int kind = unit == 2 ? 2 : unit;
            if (!float_encode(buf, kind, out)) { err("bad floating point constant `%s'", buf); return 0; }
            badd(&c->b, out, (size_t)(unit > 16 ? 16 : unit));
            if (unit > 16) bfill(&c->b, 0, (size_t)(unit - 16));
            p = skipws(q);
            continue;
        }
    expr_path:;
        {
            const char *q = p;
            Val v;
            if (!expr_parse(&q, &v, 0)) return 0;
            q = skipws(q);
            if (!strncasecmp(q, "dup", 3) && !ident_char((unsigned char)q[3])) {
                q = skipws(q + 3);
                if (!val_isconst(&v) && !v.unk) { err("dup count must be constant"); return 0; }
                i64 cnt = v.unk ? 0 : v.n;
                if (*q != '(') { err("expecting `(' after dup"); return 0; }
                q++;
                Chunk tmp;
                memset(&tmp, 0, sizeof tmp);
                if (!data_items(&q, unit, &tmp, depth + 1)) { chunk_free(&tmp); return 0; }
                q = skipws(q);
                if (*q != ')') { err("expecting `)' after dup list"); chunk_free(&tmp); return 0; }
                q++;
                if (cnt < 0) { err("dup count is negative"); chunk_free(&tmp); return 0; }
                for (i64 k = 0; k < cnt; k++) {
                    i64 base = (i64)c->b.n;
                    badd(&c->b, tmp.b.p, tmp.b.n);
                    for (int r = 0; r < tmp.nr; r++) chunk_reloc(c, &tmp.r[r].v, tmp.r[r].size, tmp.r[r].kind, tmp.r[r].off + base);
                }
                chunk_free(&tmp);
                p = skipws(q);
                continue;
            }
            chunk_val(c, &v, unit);
            p = q;
        }
    }
    *ppos = p;
    return 1;
}

static int const_expr(const char **pp, i64 *out, const char *what)
{
    Val v;
    if (!expr_parse(pp, &v, 0)) return 0;
    if (v.unk) { *out = 0; return 1; }
    if (!val_isconst(&v)) { err("%s must be a constant expression", what); return 0; }
    *out = v.n;
    return 1;
}

static void do_stmt(const char *p, int kind_hint);

static int word_kind(const char *w, int *unit)
{
    static const struct { const char *n; int kind; int unit; } t[] = {
        { "db", K_DATA, 1 }, { "dw", K_DATA, 2 }, { "dd", K_DATA, 4 }, { "dq", K_DATA, 8 }, { "dt", K_DATA, 10 },
        { "do", K_DATA, 16 }, { "dy", K_DATA, 32 }, { "dz", K_DATA, 64 },
        { "resb", K_BSS, 1 }, { "resw", K_BSS, 2 }, { "resd", K_BSS, 4 }, { "resq", K_BSS, 8 }, { "rest", K_BSS, 10 },
        { "reso", K_BSS, 16 }, { "resy", K_BSS, 32 }, { "resz", K_BSS, 64 },
        { "incbin", K_DATA, 0 }, { "istruc", K_DATA, 0 }, { "at", K_DATA, 0 }, { "iend", K_DATA, 0 },
        { NULL, 0, 0 }
    };
    for (int i = 0; t[i].n; i++)
        if (!strcasecmp(t[i].n, w)) { if (unit) *unit = t[i].unit; return t[i].kind; }
    return -1;
}

static int is_directive_word(const char *w)
{
    static const char *d[] = {
        "bits", "use16", "use32", "use64", "section", "segment", "absolute", "global", "extern", "common", "static", "org", "align", "alignb",
        "default", "cpu", "alignmode", "__cly_use", "import", "export", "group", "uppercase", "struc", "endstruc", "times", "equ", "float", "warning", "list",
        "extrn", "public", "incbin", "istruc", "iend", "at", "map", "debug", "sectalign", "prefix", "postfix", "gprefix", "gpostfix", "lprefix", "lpostfix", "required", "global_", NULL
    };
    for (int i = 0; d[i]; i++) if (!strcasecmp(d[i], w)) return 1;
    return 0;
}

static int is_prefix_word(const char *w)
{
    static const char *d[] = { "lock", "rep", "repe", "repz", "repne", "repnz", "a16", "a32", "o16", "o32", "bnd", "xacquire", "xrelease", "nobnd", NULL };
    for (int i = 0; d[i]; i++) if (!strcasecmp(d[i], w)) return 1;
    return 0;
}

static void set_bits(int b)
{
    if (b == 16 || b == 32 || b == 64) { A.bits = b; reg_mode(b); }
    else err("`bits' must be 16, 32 or 64");
}

static void define_equ(const char *fname, const Val *v)
{
    Sym *s = get_sym(fname);
    if (s->ext) { err("symbol `%s' redefined: was declared extern", fname); return; }
    if (s->defpass == A.pass) { err("symbol `%s' redefined", fname); return; }
    if (!s->defined || !s->isequ || s->eqv.n != v->n || s->eqv.pos != v->pos || s->eqv.neg != v->neg || s->eqv.ext != v->ext || s->eqv.unk != v->unk) A.changed = 1;
    s->defined = 1;
    s->isequ = 1;
    s->eqv = *v;
    s->defpass = A.pass;
    s->defline = A.cur_line;
}

static void handle_data_stmt(const char *rest, int unit)
{
    Sec *s = begin_emit(K_DATA);
    Chunk c;
    memset(&c, 0, sizeof c);
    const char *p = rest;
    i64 off = s->size;
    if (data_items(&p, unit, &c, 0)) {
        p = skipws(p);
        if (*p && *p != ';') err("junk at end of line: `%.20s'", p);
        note_list(s, off, c.b.p, c.b.n);
        commit_chunk(s, &c, -1);
    }
    chunk_free(&c);
}

static void handle_res_stmt(const char *rest, int unit)
{
    Sec *s = begin_emit(K_BSS);
    const char *p = rest;
    i64 n = 0;
    if (!const_expr(&p, &n, "reserve count")) return;
    p = skipws(p);
    if (*p && *p != ';') err("junk at end of line: `%.20s'", p);
    if (n < 0) { err("negative reserve count"); return; }
    i64 total = n * unit;
    i64 off = s->size;
    if (s->absolute || s->nobits) s->size += total;
    else {
        Chunk c;
        memset(&c, 0, sizeof c);
        bfill(&c.b, 0, (size_t)total);
        commit_chunk(s, &c, -1);
        chunk_free(&c);
    }
    note_list(s, off, NULL, 0);
    A.last = s;
}

static char *find_file(const char *name)
{
    if (file_exists(name)) return xstrdup(name);
    const char *src = A.o->srcpath ? A.o->srcpath : "";
    char *dir = path_dir(src);
    char *c = path_join(dir, name);
    free(dir);
    if (file_exists(c)) return c;
    free(c);
    return NULL;
}

static void handle_incbin(const char *rest)
{
    Sec *s = begin_emit(K_DATA);
    const char *p = skipws(rest);
    Str name = {0};
    if (!parse_charconst(&p, &name)) { err("`incbin' expects a file name"); return; }
    i64 skip = 0, len = -1;
    p = skipws(p);
    if (*p == ',') {
        p = skipws(p + 1);
        if (!const_expr(&p, &skip, "incbin skip")) return;
        p = skipws(p);
        if (*p == ',') { p = skipws(p + 1); if (!const_expr(&p, &len, "incbin length")) return; }
    }
    char *path = find_file(sget(&name));
    if (!path) { err("unable to open incbin file `%s'", sget(&name)); free(name.s); return; }
    size_t flen;
    char *buf = read_file(path, &flen);
    free(path);
    free(name.s);
    if (!buf) { err("unable to read incbin file"); return; }
    if (skip > (i64)flen) skip = (i64)flen;
    i64 avail = (i64)flen - skip;
    if (len < 0 || len > avail) len = avail;
    Chunk c;
    memset(&c, 0, sizeof c);
    badd(&c.b, buf + skip, (size_t)len);
    free(buf);
    note_list(s, s->size, c.b.p, c.b.n);
    commit_chunk(s, &c, -1);
    chunk_free(&c);
}

static void handle_times(const char *rest);

static int parse_name_list(const char *p, void (*fn)(const char *name, const char *attr, void *ud), void *ud)
{
    p = skipws(p);
    while (*p && *p != ';') {
        char w[256];
        const char *e = scan_word(p, w, sizeof w);
        if (e == p) { err("symbol name expected"); return 0; }
        const char *a = e;
        char attr[256];
        attr[0] = 0;
        e = skipws(e);
        if (*e == ':') {
            const char *s = e + 1;
            size_t n = 0;
            while (*s && *s != ',' && *s != ';' && n < sizeof attr - 1) attr[n++] = *s++;
            attr[n] = 0;
            e = s;
        }
        (void)a;
        fn(w, attr, ud);
        e = skipws(e);
        if (*e == ',') { p = skipws(e + 1); continue; }
        p = e;
        break;
    }
    return 1;
}

static void cb_global(const char *name, const char *attr, void *ud)
{
    (void)attr;
    (void)ud;
    char *fn = full_name(name);
    Sym *s = get_sym(fn);
    s->global = 1;
    free(fn);
}

static void cb_extern(const char *name, const char *attr, void *ud)
{
    (void)attr;
    (void)ud;
    char *fn = full_name(name);
    Sym *s = get_sym(fn);
    if (s->defined && s->defpass >= 0) { free(fn); return; }
    s->ext = 1;
    free(fn);
}

static void cb_static(const char *name, const char *attr, void *ud)
{
    (void)attr;
    (void)ud;
    char *fn = full_name(name);
    get_sym(fn)->isstatic = 1;
    free(fn);
}


static void handle_section(const char *rest)
{
    const char *p = skipws(rest);
    char name[256];
    size_t n = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != ';' && n < sizeof name - 1) name[n++] = *p++;
    name[n] = 0;
    if (!n) { err("`section' expects a section name"); return; }
    if (A.npending) flush_labels(A.cur ? A.cur : (A.last ? A.last : A.text));
    Sec *s = find_sec(name);
    int created = 0;
    if (!s) { s = new_sec(name); sec_defaults(s); created = 1; }
    if (s == A.text || s == A.data || s == A.bss) created = created;
    for (;;) {
        p = skipws(p);
        if (!*p || *p == ';') break;
        char w[64];
        const char *e = p;
        size_t k = 0;
        while (*e && *e != ' ' && *e != '\t' && *e != ';' && k < sizeof w - 1) w[k++] = *e++;
        w[k] = 0;
        p = e;
        char *eq = strchr(w, '=');
        i64 val = 0;
        if (eq) {
            *eq = 0;
            Val v;
            const char *vp = eq + 1;
            if (strcasecmp(w, "follows") && strcasecmp(w, "vfollows") && expr_eval_str(vp, &v, 0) && val_isconst(&v)) val = v.n;
        }
        if (!strcasecmp(w, "progbits")) s->nobits = 0;
        else if (!strcasecmp(w, "nobits")) s->nobits = 1;
        else if (!strcasecmp(w, "exec")) s->exec = 1;
        else if (!strcasecmp(w, "noexec")) s->exec = 0;
        else if (!strcasecmp(w, "write")) s->write = 1;
        else if (!strcasecmp(w, "nowrite")) s->write = 0;
        else if (!strcasecmp(w, "alloc")) s->alloc = 1;
        else if (!strcasecmp(w, "noalloc")) s->alloc = 0;
        else if (!strcasecmp(w, "align")) { if (pow2_ok(val)) s->align = (int)val; else err("section alignment must be a power of two"); }
        else if (!strcasecmp(w, "start")) { s->has_start = 1; s->start = val; }
        else if (!strcasecmp(w, "vstart")) { s->has_vstart = 1; s->vstart = val; }
        else if (!strcasecmp(w, "follows")) { free(s->follows); s->follows = xstrdup(eq ? eq + 1 : ""); }
        else if (!strcasecmp(w, "vfollows")) { free(s->vfollows); s->vfollows = xstrdup(eq ? eq + 1 : ""); }
        else if (!strcasecmp(w, "code") || !strcasecmp(w, "text")) s->exec = 1;
        else if (!strcasecmp(w, "data") || !strcasecmp(w, "bss") || !strcasecmp(w, "rdata") || !strcasecmp(w, "private") || !strcasecmp(w, "public") || !strcasecmp(w, "stack") || !strcasecmp(w, "common") || !strcasecmp(w, "use16") || !strcasecmp(w, "use32") || !strcasecmp(w, "flat") || !strcasecmp(w, "class") || !strcasecmp(w, "info") || !strcasecmp(w, "tls") || !strcasecmp(w, "lib") || !strcasecmp(w, "comdat") || !strcasecmp(w, "discard") || !strcasecmp(w, "read") || !strcasecmp(w, "execute") || !strcasecmp(w, "remove") || !strcasecmp(w, "shared") || !strcasecmp(w, "nodiscard")) {
            if (!strcasecmp(w, "bss")) { s->nobits = 1; s->write = 1; }
            if (!strcasecmp(w, "execute")) s->exec = 1;
            if (!strcasecmp(w, "read")) {}
        } else warn("unknown section attribute `%s'", w);
    }
    A.cur = s;
    A.last = s;
    {
        const char *rf = A.lines->v[A.cur_line].file;
        if (!(rf && !strcmp(rf, "<runtime>"))) A.explicit_mode = 1;
    }
}

static void handle_absolute(const char *rest)
{
    const char *p = skipws(rest);
    i64 v = 0;
    if (A.npending) flush_labels(A.cur ? A.cur : (A.last ? A.last : A.text));
    if (!const_expr(&p, &v, "absolute address")) return;
    memset(&A.absec, 0, sizeof A.absec);
    A.absec.name = "__absolute";
    A.absec.absolute = 1;
    A.absec.size = v;
    A.absec.start = v;
    A.absec.idx = -1;
    A.cur = &A.absec;
    A.last = &A.absec;
}

static void handle_align(const char *rest, int isalignb)
{
    const char *p = skipws(rest);
    i64 n = 0;
    if (!const_expr(&p, &n, "alignment")) return;
    if (!pow2_ok(n)) { err("alignment must be a power of two"); return; }
    p = skipws(p);
    Chunk fill;
    memset(&fill, 0, sizeof fill);
    int hasfill = 0;
    if (*p == ',') {
        p = skipws(p + 1);
        char w[64];
        const char *e = scan_word(p, w, sizeof w);
        int unit;
        if (e != p && word_kind(w, &unit) == K_DATA && unit > 0) {
            const char *q = skipws(e);
            if (data_items(&q, unit, &fill, 0)) hasfill = fill.b.n > 0;
        } else if (*p) {
            Op ops[5];
            int no = 0;
            const char *q = skipws(e);
            if (parse_operands(&q, ops, &no)) {
                IC ic;
                memset(&ic, 0, sizeof ic);
                ic.bits = A.bits;
                ic.cpu = A.cpu;
                ic.cur_sec = 0;
                ic.quiet = 0;
                InsnOut out;
                memset(&out, 0, sizeof out);
                if (insn_encode(w, ops, no, &ic, &out)) { badd(&fill.b, out.code.p, out.code.n); hasfill = 1; }
                free(out.code.p);
                free(out.relocs);
            }
        }
    }
    if (isalignb && !hasfill) { bad1(&fill.b, 0); hasfill = 1; }
    A.pend_align = (int)n;
    A.pend_smart = !isalignb && !hasfill;
    free(A.pend_fill.p);
    memset(&A.pend_fill, 0, sizeof A.pend_fill);
    if (hasfill) { badd(&A.pend_fill, fill.b.p, fill.b.n); A.has_pend_fill = 1; }
    else A.has_pend_fill = 0;
    chunk_free(&fill);
    if (A.explicit_mode || A.cur) {
        Sec *s = A.cur ? A.cur : (A.last ? A.last : A.text);
        flush_labels(s);
        apply_align(s);
    }
}

static void handle_times(const char *rest)
{
    const char *p = skipws(rest);
    Val v;
    if (!expr_parse(&p, &v, 0)) return;
    i64 cnt = 0;
    if (v.unk) cnt = 0;
    else if (!val_isconst(&v)) { err("`times' count must be a constant expression"); return; }
    else cnt = v.n;
    if (v.isfloat) { err("`times' count must be an integer"); return; }
    if (cnt < 0) { err("`times' value is negative"); return; }
    if (cnt > ((i64)1 << 28)) { err("`times' count is too large"); return; }
    p = skipws(p);
    if (!*p || *p == ';') { err("`times' needs an instruction or data"); return; }
    for (i64 i = 0; i < cnt; i++) {
        int before = g_errors;
        do_stmt(p, 0);
        if (g_errors != before) break;
    }
}

static void do_org(const char *rest)
{
    const char *p = skipws(rest);
    i64 v;
    if (!const_expr(&p, &v, "`org' address")) return;
    A.has_org = 1;
    A.org = v;
}

static void struc_begin(const char *rest)
{
    char w[256];
    const char *p = skipws(rest);
    scan_word(p, w, sizeof w);
    if (!w[0]) { err("`struc' expects a name"); return; }
    if (A.struc_active) { err("nested `struc' is not supported"); return; }
    A.struc_active = 1;
    A.struc_save_cur = A.cur;
    free(A.struc_name);
    A.struc_name = xstrdup(w);
    if (A.npending) flush_labels(A.cur ? A.cur : (A.last ? A.last : A.text));
    memset(&A.absec, 0, sizeof A.absec);
    A.absec.name = "__absolute";
    A.absec.absolute = 1;
    A.absec.idx = -1;
    A.cur = &A.absec;
    Val z;
    val_const(&z, 0);
    char *fn = full_name(w);
    Sym *s = get_sym(fn);
    if (!s->defined || s->sec != SEC_ABS || s->off != 0 || s->isequ) A.changed = 1;
    if (s->defpass == A.pass) err("symbol `%s' redefined", fn);
    s->defined = 1;
    s->isequ = 0;
    s->sec = SEC_ABS;
    s->off = 0;
    s->defpass = A.pass;
    free(fn);
    free(A.lastlabel);
    A.lastlabel = xstrdup(w);
}

static void struc_end(void)
{
    if (!A.struc_active) { err("`endstruc' without `struc'"); return; }
    Val v;
    val_const(&v, A.absec.size);
    Str n = {0};
    sfmt(&n, "%s_size", A.struc_name);
    define_equ(sget(&n), &v);
    free(n.s);
    A.cur = A.struc_save_cur;
    A.struc_active = 0;
}

static void istruc_begin(const char *rest)
{
    char w[256];
    scan_word(skipws(rest), w, sizeof w);
    if (!w[0]) { err("`istruc' expects a structure name"); return; }
    Sec *s = begin_emit(K_DATA);
    free(A.istruc_name);
    A.istruc_name = xstrdup(w);
    asm_cur_loc(&A.istruc_start);
    A.istruc_sec = s;
    free(A.istruc_savelbl);
    A.istruc_savelbl = A.lastlabel ? xstrdup(A.lastlabel) : NULL;
    free(A.lastlabel);
    A.lastlabel = xstrdup(w);
}

static void pad_to(Sec *s, i64 target)
{
    i64 n = target - s->size;
    if (n < 0) { err("`at' offset is before the current position"); return; }
    if (n) {
        Chunk c;
        memset(&c, 0, sizeof c);
        bfill(&c.b, 0, (size_t)n);
        note_list(s, s->size, c.b.p, c.b.n);
        commit_chunk(s, &c, -1);
        chunk_free(&c);
    }
}

static void istruc_at(const char *rest)
{
    if (!A.istruc_name) { err("`at' without `istruc'"); return; }
    const char *p = skipws(rest);
    Val off;
    if (!expr_parse(&p, &off, 0)) return;
    p = skipws(p);
    if (*p == ',') p = skipws(p + 1);
    Sec *s = begin_emit(K_DATA);
    if (!off.unk) {
        if (!val_isconst(&off)) { err("`at' offset must be constant"); return; }
        pad_to(s, A.istruc_start.n + off.n);
    }
    if (*p && *p != ';') do_stmt(p, 0);
}

static void istruc_end(void)
{
    if (!A.istruc_name) { err("`iend' without `istruc'"); return; }
    Str n = {0};
    sfmt(&n, "%s_size", A.istruc_name);
    Val v;
    Sec *s = begin_emit(K_DATA);
    if (asm_sym_lookup(sget(&n), &v) && val_isconst(&v)) pad_to(s, A.istruc_start.n + v.n);
    free(n.s);
    free(A.istruc_name);
    A.istruc_name = NULL;
    free(A.lastlabel);
    A.lastlabel = A.istruc_savelbl;
    A.istruc_savelbl = NULL;
}

static void do_directive(const char *w, const char *rest)
{
    if (is_kw(w, "bits")) {
        const char *p = skipws(rest);
        i64 v;
        if (const_expr(&p, &v, "`bits' value")) set_bits((int)v);
    } else if (is_kw(w, "use16")) set_bits(16);
    else if (is_kw(w, "use32")) set_bits(32);
    else if (is_kw(w, "use64")) set_bits(64);
    else if (is_kw(w, "section") || is_kw(w, "segment")) handle_section(rest);
    else if (is_kw(w, "absolute")) handle_absolute(rest);
    else if (is_kw(w, "global") || is_kw(w, "public")) parse_name_list(rest, cb_global, NULL);
    else if (is_kw(w, "extern") || is_kw(w, "extrn")) parse_name_list(rest, cb_extern, NULL);
    else if (is_kw(w, "static")) parse_name_list(rest, cb_static, NULL);
    else if (is_kw(w, "common")) {
        char name[256];
        const char *p = scan_word(skipws(rest), name, sizeof name);
        p = skipws(p);
        i64 size = 0, al = 4;
        if (!const_expr(&p, &size, "common size")) return;
        p = skipws(p);
        if (*p == ':') { p = skipws(p + 1); const_expr(&p, &al, "common alignment"); }
        Sec *s = A.bss;
        i64 r = al > 1 ? s->size % al : 0;
        if (r) s->size += al - r;
        if (al > s->align) s->align = (int)al;
        char *fn = full_name(name);
        def_label_now(fn, s);
        get_sym(fn)->global = 1;
        free(fn);
        s->size += size;
    } else if (is_kw(w, "alignmode")) {
        char m[32];
        const char *p = scan_word(skipws(rest), m, sizeof m);
        static const char *nm[] = { "nop", "generic", "k7", "k8", "p6", NULL };
        int mode = -1;
        for (int i = 0; nm[i]; i++) if (!strcasecmp(m, nm[i])) mode = i;
        if (mode < 0) { err("unknown alignmode `%s' (use nop, generic, k7, k8 or p6)", m); return; }
        A.smart_mode = mode;
        A.smart_thr = mode == 0 ? 16 : (mode == 1 ? 8 : 16);
        p = skipws(p);
        if (*p == ',') {
            p = skipws(p + 1);
            i64 t;
            if (const_expr(&p, &t, "alignmode threshold")) A.smart_thr = (int)t;
        }
    } else if (is_kw(w, "__cly_use")) {
        char m[32];
        scan_word(skipws(rest), m, sizeof m);
        if (!strcasecmp(m, "smartalign")) { A.smart_on = 1; A.smart_mode = 1; A.smart_thr = 8; }
        else if (!strcasecmp(m, "altreg")) reg_altreg(1);
    } else if (is_kw(w, "org")) do_org(rest);
    else if (is_kw(w, "align") || is_kw(w, "alignb")) handle_align(rest, is_kw(w, "alignb"));
    else if (is_kw(w, "cpu")) {
        char n[64];
        scan_word(skipws(rest), n, sizeof n);
        insn_cpu_set(n, &A.cpu);
    } else if (is_kw(w, "import")) {
        char n[256], lib[256], ename[256];
        const char *p = scan_word(skipws(rest), n, sizeof n);
        p = scan_word(skipws(p), lib, sizeof lib);
        p = scan_word(skipws(p), ename, sizeof ename);
        char *fn = full_name(n);
        Sym *s = get_sym(fn);
        s->ext = 1;
        s->import = 1;
        free(s->impdll);
        free(s->impname);
        s->impdll = xstrdup(lib);
        s->impname = xstrdup(ename[0] ? ename : n);
        free(fn);
    } else if (is_kw(w, "export")) {
        char n[256], en[256];
        const char *p = scan_word(skipws(rest), n, sizeof n);
        scan_word(skipws(p), en, sizeof en);
        char *fn = full_name(n);
        Sym *s = get_sym(fn);
        s->global = 1;
        s->exported = 1;
        free(s->expname);
        s->expname = xstrdup(en[0] ? en : n);
        free(fn);
    } else if (is_kw(w, "struc")) struc_begin(rest);
    else if (is_kw(w, "endstruc")) struc_end();
    else if (is_kw(w, "istruc")) istruc_begin(rest);
    else if (is_kw(w, "at")) istruc_at(rest);
    else if (is_kw(w, "iend")) istruc_end();
    else if (is_kw(w, "incbin")) handle_incbin(rest);
    else if (is_kw(w, "times")) handle_times(rest);
    else if (is_kw(w, "default")) {
        char dw[32];
        const char *q = skipws(rest);
        while (*q) {
            const char *e2 = scan_word(q, dw, sizeof dw);
            if (e2 == q) break;
            if (is_kw(dw, "rel")) A.defrel = 1;
            else if (is_kw(dw, "abs")) A.defrel = 0;
            q = skipws(e2);
            if (*q == ',') q = skipws(q + 1);
        }
    } else if (is_kw(w, "group") || is_kw(w, "uppercase") || is_kw(w, "float") || is_kw(w, "warning") || is_kw(w, "list") || is_kw(w, "map") || is_kw(w, "debug") || is_kw(w, "sectalign") || is_kw(w, "prefix") || is_kw(w, "postfix") || is_kw(w, "gprefix") || is_kw(w, "gpostfix") || is_kw(w, "lprefix") || is_kw(w, "lpostfix") || is_kw(w, "required")) {
    } else err("unknown directive `%s'", w);
}

static int has_vreg_op(const Op *ops, int n)
{
    for (int i = 0; i < n; i++) if (ops[i].kind == OK_MEM && ops[i].vreg) return 1;
    return 0;
}

static int op_uses_reg(const Op *o, int reg)
{
    if (o->kind == OK_REG && REGNUM(o->reg) == REGNUM(reg) && REGCLS(o->reg) >= RC_R8 && REGCLS(o->reg) <= RC_R32) return 1;
    if (o->kind == OK_MEM) {
        if (o->base && REGNUM(o->base) == REGNUM(reg)) return 1;
        if (o->index && REGNUM(o->index) == REGNUM(reg)) return 1;
    }
    return 0;
}

static int commit_insn(Sec *s, InsnOut *out)
{
    Chunk c;
    memset(&c, 0, sizeof c);
    c.b = out->code;
    c.r = out->relocs;
    c.nr = out->nrel;
    c.cap = out->caprel;
    note_list(s, s->size, c.b.p, c.b.n);
    commit_chunk(s, &c, -1);
    chunk_free(&c);
    memset(out, 0, sizeof *out);
    return 1;
}

static int encode_one(Sec *s, const char *mn, Op *ops, int nops, IC *ic0, int quiet)
{
    IC ic = *ic0;
    ic.cur_sec = s->absolute ? -2 : s->idx;
    ic.start_off = s->size;
    ic.quiet = quiet;
    ic.need_wide = 0;
    InsnOut out;
    memset(&out, 0, sizeof out);
    int e0 = g_errors;
    int q0 = g_quiet;
    if (quiet) g_quiet = 1;
    int ok = insn_encode(mn, ops, nops, &ic, &out);
    g_quiet = q0;
    if (quiet) g_errors = e0;
    ic0->nomatch = ic.nomatch;
    if (ic.need_wide && A.cur_line < A.nwide && !A.wide[A.cur_line]) {
        A.wide[A.cur_line] = 1;
        A.changed = 1;
    }
    if (!ok) {
        free(out.code.p);
        free(out.relocs);
        return 0;
    }
    commit_insn(s, &out);
    return 1;
}

static void handle_syscall(Sec *s, IC *ic)
{
    A.uses_syscall = 1;
    Op o;
    memset(&o, 0, sizeof o);
    o.scale = 1;
    o.kind = OK_IMM;
    Sym *rt = get_sym("__cly_syscall");
    rt->referenced = 1;
    val_const(&o.val, 0);
    if (rt->defined) {
        o.val.pos = rt->sec;
        o.val.n = rt->off;
    } else o.val.unk = 1;
    IC c2 = *ic;
    c2.bits = A.bits;
    if (A.bits == 16) {
        err("`syscall' is only available in 32-bit and 64-bit code");
        return;
    }
    encode_one(s, "call", &o, 1, &c2, 0);
}

static void handle_insn(const char *mn, const char *rest, IC *ic)
{
    Sec *s = begin_emit(K_CODE);
    Op ops[5];
    int nops = 0;
    const char *p = rest;
    if (!parse_operands(&p, ops, &nops)) return;
    p = skipws(p);
    if (*p && *p != ';') { err("junk at end of line: `%.20s'", p); return; }
    ic->bits = A.bits;
    ic->cpu = A.cpu;
    if (A.bits == 64 && A.defrel)
        for (int i = 0; i < nops; i++)
            if (ops[i].kind == OK_MEM && !ops[i].base && !ops[i].index && !(ops[i].flags & OF_ABS) && ops[i].mseg != REGID(RC_SEG, 4) && ops[i].mseg != REGID(RC_SEG, 5) && !ic->segpre) ops[i].flags |= OF_REL;
    ic->line = A.cur_line;
    ic->wide = A.cur_line < A.nwide ? A.wide[A.cur_line] : 0;
    if (!strcasecmp(mn, "syscall") && nops == 0 && !(A.bits == 64 && A.kernel == K_LINUX)) { handle_syscall(s, ic); return; }
    if (!has_vreg_op(ops, nops)) {
        encode_one(s, mn, ops, nops, ic, 0);
        return;
    }
    if (encode_one(s, mn, ops, nops, ic, 1)) return;
    int simple = !strcasecmp(mn, "mov") || !strcasecmp(mn, "add") || !strcasecmp(mn, "adc") || !strcasecmp(mn, "sub") || !strcasecmp(mn, "sbb") || !strcasecmp(mn, "and") || !strcasecmp(mn, "or") || !strcasecmp(mn, "xor") || !strcasecmp(mn, "cmp") || !strcasecmp(mn, "test");
    if (nops == 2 && simple && ops[0].kind == OK_MEM && ops[1].kind == OK_MEM) {
        int sz = ops[0].vreg ? ops[0].vsize : (ops[1].vreg ? ops[1].vsize : (ops[0].size ? ops[0].size : (ops[1].size ? ops[1].size : 4)));
        int rn[4] = { 0, 1, 2, 3 };
        int scr = 0;
        for (int i = 0; i < 4; i++) {
            if (!op_uses_reg(&ops[0], REGID(RC_R32, rn[i])) && !op_uses_reg(&ops[1], REGID(RC_R32, rn[i]))) { scr = rn[i]; break; }
        }
        Op push, mv, pop, dst;
        memset(&push, 0, sizeof push);
        push.kind = OK_REG;
        push.reg = REGID(RC_R32, scr);
        push.scale = 1;
        val_const(&push.val, 0);
        Op src = ops[1];
        dst = ops[0];
        if (src.base == REGID(RC_R32, 4) || src.index == REGID(RC_R32, 4)) src.val.n += 4;
        if (dst.base == REGID(RC_R32, 4) || dst.index == REGID(RC_R32, 4)) dst.val.n += 4;
        memset(&mv, 0, sizeof mv);
        Op sreg;
        memset(&sreg, 0, sizeof sreg);
        sreg.kind = OK_REG;
        sreg.scale = 1;
        sreg.reg = sz == 1 ? REGID(RC_R8, scr) : (sz == 2 ? REGID(RC_R16, scr) : REGID(RC_R32, scr));
        val_const(&sreg.val, 0);
        Op two[2] = { sreg, src };
        pop = push;
        Op a1[1] = { push };
        if (!encode_one(s, "push", a1, 1, ic, 0)) return;
        if (!encode_one(s, "mov", two, 2, ic, 0)) return;
        Op fin[2] = { dst, sreg };
        if (!encode_one(s, mn, fin, 2, ic, 0)) return;
        Op a3[1] = { pop };
        encode_one(s, "pop", a3, 1, ic, 0);
        return;
    }
    encode_one(s, mn, ops, nops, ic, 0);
}

static int lookup_kw_equ(const char *p)
{
    char w[32];
    const char *e = scan_word(skipws(p), w, sizeof w);
    (void)e;
    return !strcasecmp(w, "equ");
}

static void do_stmt(const char *p0, int kind_hint)
{
    (void)kind_hint;
    const char *p = skipws(p0);
    if (!*p || *p == ';') return;
    IC ic;
    memset(&ic, 0, sizeof ic);
    for (;;) {
        static const struct { const char *n; int v; } vp[] = { { "{vex}", 1 }, { "{vex2}", 2 }, { "{vex3}", 3 }, { "{evex}", 4 }, { NULL, 0 } };
        int hit = 0;
        for (int i = 0; vp[i].n; i++) {
            size_t l = strlen(vp[i].n);
            if (!strncasecmp(p, vp[i].n, l)) { ic.vexpfx = vp[i].v; p = skipws(p + l); hit = 1; break; }
        }
        if (!hit) break;
    }
    char w[256];
    const char *e = scan_word(p, w, sizeof w);
    if (e == p) {
        if (*p == '[') {
            const char *q = skipws(p + 1);
            char dn[64];
            const char *dr = scan_word(q, dn, sizeof dn);
            const char *cl = strrchr(dr, ']');
            if (!cl) { err("unterminated `[' directive"); return; }
            char *rest = xstrndup(dr, (size_t)(cl - dr));
            do_directive(dn, rest);
            free(rest);
            return;
        }
        err("parser: instruction expected");
        return;
    }
    const char *after = skipws(e);
    int unit = 0;
    int kind;
    if (*after == ':' && after[1] != ':') {
        char *fn = full_name(w);
        if (w[0] != '.' || w[1] == '.') { free(A.lastlabel); A.lastlabel = xstrdup(w); }
        free(fn);
        fn = full_name(w);
        const char *rest = skipws(after + 1);
        if (lookup_kw_equ(rest)) {
            const char *q = rest;
            char ew[16];
            q = scan_word(skipws(q), ew, sizeof ew);
            q = skipws(q);
            Val v;
            begin_emit(K_EQU);
            if (expr_parse(&q, &v, 0)) define_equ(fn, &v);
            free(fn);
            return;
        }
        add_pending(fn);
        free(fn);
        if (!*rest || *rest == ';') return;
        do_stmt(rest, 0);
        return;
    }
    if (is_prefix_word(w)) {
        for (;;) {
            if (is_kw(w, "lock")) ic.lock = 1;
            else if (is_kw(w, "rep") || is_kw(w, "repe") || is_kw(w, "repz")) ic.rep = 1;
            else if (is_kw(w, "repne") || is_kw(w, "repnz")) ic.repne = 1;
            else if (is_kw(w, "a16")) ic.a16 = 1;
            else if (is_kw(w, "a32")) ic.a32 = 1;
            else if (is_kw(w, "o16")) ic.o16 = 1;
            else if (is_kw(w, "o32")) ic.o32 = 1;
            else if (is_kw(w, "bnd")) ic.bnd = 1;
            else if (is_kw(w, "xacquire")) ic.xacq = 1;
            else if (is_kw(w, "xrelease")) ic.xrel = 1;
            p = skipws(e);
            e = scan_word(p, w, sizeof w);
            if (e == p) { err("prefix without an instruction"); return; }
            if (!is_prefix_word(w)) break;
        }
        after = skipws(e);
    }
    {
        int sr;
        if (is_segreg(w, &sr) && !insn_known(w)) {
            const char *nx = skipws(e);
            if (*nx == ':') nx = skipws(nx + 1);
            char w2[64];
            const char *e2 = scan_word(nx, w2, sizeof w2);
            if (e2 != nx && (insn_known(w2) || is_prefix_word(w2))) {
                ic.segpre = sr;
                p = nx;
                e = e2;
                memcpy(w, w2, sizeof w2);
                after = skipws(e);
            }
        }
    }
    kind = word_kind(w, &unit);
    if (kind < 0 && is_kw(w, "times")) { handle_times(e); return; }
    int dirw = is_directive_word(w) && !(insn_known(w) && !is_kw(w, "at"));
    if (kind < 0 && !dirw && !insn_known(w) && !is_prefix_word(w)) {
        char nw[64];
        const char *ne = scan_word(after, nw, sizeof nw);
        int ok2 = 0;
        if (ne != after) {
            if (word_kind(nw, NULL) >= 0 || is_kw(nw, "equ") || is_kw(nw, "times") || insn_known(nw) || is_prefix_word(nw) || is_directive_word(nw)) ok2 = 1;
        }
        if (ok2 || !*after || *after == ';') {
            char *fn = full_name(w);
            if (w[0] != '.' || w[1] == '.') { free(A.lastlabel); A.lastlabel = xstrdup(w); }
            free(fn);
            fn = full_name(w);
            if (is_kw(nw, "equ") && ne != after) {
                const char *q = skipws(ne);
                Val v;
                begin_emit(K_EQU);
                if (expr_parse(&q, &v, 0)) define_equ(fn, &v);
                free(fn);
                return;
            }
            add_pending(fn);
            free(fn);
            if (!*after || *after == ';') return;
            do_stmt(after, 0);
            return;
        }
        {
            const char *sg = insn_suggest(w);
            if (sg) err("unknown instruction or directive `%s' (did you mean `%s'?)", w, sg);
            else err("unknown instruction or directive `%s'", w);
        }
        return;
    }
    if (kind == K_DATA && unit > 0) { handle_data_stmt(e, unit); return; }
    if (kind == K_BSS) { handle_res_stmt(e, unit); return; }
    if (dirw) {
        if (is_kw(w, "equ")) { err("`equ' needs a label"); return; }
        do_directive(w, e);
        return;
    }
    if (kind == K_DATA) { do_directive(w, e); return; }
    handle_insn(w, e, &ic);
}

static void reset_pass(void)
{
    for (int i = 0; i < A.nsecs; i++) {
        Sec *s = A.secs[i];
        s->data.n = 0;
        s->size = s == A.vregsec ? 32 : 0;
    }
    A.nrelocs = 0;
    A.cur = NULL;
    A.last = A.text;
    A.stmt = A.text;
    A.npending = 0;
    free(A.lastlabel);
    A.lastlabel = xstrdup("");
    A.bits = A.default_bits;
    reg_mode(A.bits);
    A.defrel = A.o->kernel == K_MAC && A.default_bits == 64;
    A.cpu = 99;
    A.pend_align = 0;
    A.has_pend_fill = 0;
    A.struc_active = 0;
    free(A.istruc_name);
    A.istruc_name = NULL;
    A.has_org = 0;
    A.org = 0;
    if (A.explicit_mode) A.cur = A.text;
}

static int first_word_is(const char *text, const char **words)
{
    const char *p = skipws(text);
    if (*p == '[') p = skipws(p + 1);
    char w[64];
    const char *e = scan_word(p, w, sizeof w);
    if (e == p) return 0;
    const char *a = skipws(e);
    if (*a == ':') { p = skipws(a + 1); e = scan_word(p, w, sizeof w); if (e == p) return 0; }
    for (int i = 0; words[i]; i++) if (!strcasecmp(w, words[i])) return 1;
    return 0;
}

static void run_pass(void)
{
    reset_pass();
    for (int i = 0; i < A.lines->n; i++) {
        SLine *l = &A.lines->v[i];
        g_pos.file = l->file;
        g_pos.line = l->line;
        A.cur_line = i;
        do_stmt(l->text, 0);
        if (A.o->list && A.final && 0) {}
    }
    Sec *end = A.cur ? A.cur : (A.last ? A.last : A.text);
    if (A.npending) flush_labels(end);
    g_pos.file = NULL;
    if (A.npsize < A.nsecs) {
        A.psize = xrealloc(A.psize, sizeof(i64) * (size_t)A.nsecs);
        A.npsize = A.nsecs;
    }
    for (int i = 0; i < A.nsecs; i++) A.psize[i] = A.secs[i]->size;
}

static i64 size_sum(void)
{
    i64 t = 0;
    for (int i = 0; i < A.nsecs; i++) t = t * 31 + A.secs[i]->size;
    return t;
}

void obj_free(Obj *o) { (void)o; }

static int detect_bits(AsmOpts *o)
{
    if (o->bits) return o->bits;
    for (int i = 0; i < o->lines.n; i++) {
        const char *t = skipws(o->lines.v[i].text);
        if (*t == '[') t = skipws(t + 1);
        if (!strncasecmp(t, "bits", 4) && !ident_char((unsigned char)t[4])) {
            int b = atoi(skipws(t + 4));
            if (b == 16 || b == 32 || b == 64) return b;
        }
        if (!strncasecmp(t, "use64", 5) && !ident_char((unsigned char)t[5])) return 64;
        if (!strncasecmp(t, "use32", 5) && !ident_char((unsigned char)t[5])) return 32;
    }
    int flat = o->type == T_FLAT || o->type == T_COM || o->type == T_HEX || o->type == T_SREC || o->type == T_IMG;
    return (o->kernel == K_BARE || flat) ? 32 : 64;
}

int assemble(AsmOpts *o, Obj *out)
{
    memset(&A, 0, sizeof A);
    A.o = o;
    A.lines = &o->lines;
    A.kernel = o->kernel;
    A.type = o->type;
    A.default_bits = detect_bits(o);
    reg_mode(A.default_bits);
    insn_init();
    g_hooks.sym_lookup = asm_sym_lookup;
    g_hooks.cur_loc = asm_cur_loc;
    g_hooks.sec_start = asm_sec_start;
    A.text = new_sec(".text");
    A.data = new_sec(".data");
    A.bss = new_sec(".bss");
    sec_defaults(A.text);
    sec_defaults(A.data);
    sec_defaults(A.bss);
    static const char *expl[] = { "org", "section", "segment", "absolute", NULL };
    for (int i = 0; i < A.lines->n; i++)
        if (first_word_is(A.lines->v[i].text, expl)) { A.explicit_mode = 1; break; }
    if (o->flat_mode) A.explicit_mode = 1;
    static const char *sysw[] = { "syscall", NULL };
    int need_rt = 0;
    for (int i = 0; i < A.lines->n && !need_rt; i++) {
        const char *t = A.lines->v[i].text;
        for (const char *q = t; *q; q++) {
            if ((q == t || !ident_char((unsigned char)q[-1])) && !strncasecmp(q, "syscall", 7) && !ident_char((unsigned char)q[7])) { need_rt = 1; break; }
        }
    }
    (void)sysw;
    if (need_rt && A.default_bits == 64 && o->kernel == K_LINUX) need_rt = 0;
    if (need_rt) {
        Str rt = {0};
        if (rt_build(o->kernel, o->type, A.default_bits, &rt)) {
            const char *rf = "<runtime>";
            const char *p = rt.s;
            int ln = 1;
            while (p && *p) {
                const char *nl = strchr(p, '\n');
                size_t len = nl ? (size_t)(nl - p) : strlen(p);
                char *t = xstrndup(p, len);
                lv_add(A.lines, t, rf, ln++);
                free(t);
                if (!nl) break;
                p = nl + 1;
            }
        }
        free(rt.s);
    }
    A.nwide = A.lines->n;
    A.wide = xcalloc((size_t)A.nwide + 1, 1);
    if (o->list) {
        A.lst_off = xcalloc((size_t)A.nwide + 1, sizeof(i64));
        A.lst_sec = xcalloc((size_t)A.nwide + 1, sizeof(int));
        A.lst_bytes = xcalloc((size_t)A.nwide + 1, sizeof(u8 *));
        A.lst_len = xcalloc((size_t)A.nwide + 1, sizeof(int));
    }
    int e0 = g_errors;
    int pass;
    int converged = 0;
    i64 prev_sum = -1;
    for (pass = 1; pass <= 200; pass++) {
        A.pass = pass;
        A.changed = 0;
        A.final = 0;
        int q0 = g_quiet;
        g_quiet = 1;
        int ge = g_errors, gw = g_warnings;
        run_pass();
        g_quiet = q0;
        g_errors = ge;
        g_warnings = gw;
        i64 sum = size_sum();
        if (!A.changed && sum == prev_sum) { converged = 1; break; }
        prev_sum = sum;
    }
    if (!converged) warn("assembly did not converge after %d passes", pass - 1);
    A.pass++;
    A.final = 1;
    A.changed = 0;
    g_errors = e0;
    run_pass();
    if (g_errors != e0) return 0;
    for (int i = 0; i < A.nsyms; i++) {
        Sym *s = A.syms[i];
        if (s->referenced && !s->defined && !s->ext && !s->common && strcmp(s->name, "__cly_syscall")) {
            if (!(s->name[0] == '.' && s->name[1] != '.' && 0)) {
                g_pos.file = s->reffile;
                g_pos.line = s->refline;
                err("symbol `%s' is not defined", s->name);
                g_pos.file = NULL;
            }
        }
    }
    if (g_errors != e0) return 0;
    Sym *st = sym_find("start");
    if (!st || !st->defined) st = sym_find("..start");
    A.entry = (st && st->defined) ? st : NULL;
    if (st) st->global = 1;
    out->secs = A.secs;
    out->nsecs = A.nsecs;
    out->relocs = A.relocs;
    out->nrelocs = A.nrelocs;
    out->syms = A.syms;
    out->nsyms = A.nsyms;
    out->bits = A.bits;
    out->is64 = A.default_bits == 64;
    out->has_org = A.has_org || o->flat_mode;
    out->org = (int)A.org;
    out->entry = A.entry;
    out->uses_syscall = A.uses_syscall;
    if (o->list && o->listpath) {
        FILE *f = fopen(o->listpath, "w");
        if (f) {
            for (int i = 0; i < A.lines->n; i++) {
                SLine *l = &A.lines->v[i];
                if (l->file && !strcmp(l->file, "<runtime>")) continue;
                char hex[80];
                hex[0] = 0;
                int n = A.lst_len[i];
                if (n > 16) n = 16;
                for (int k = 0; k < n; k++) sprintf(hex + k * 2, "%02X", A.lst_bytes[i][k]);
                if (A.lst_len[i] > 16) strcat(hex, "-");
                if (A.lst_len[i]) fprintf(f, "%6d %08llX %-18s %s\n", i + 1, (unsigned long long)A.lst_off[i], hex, l->text);
                else fprintf(f, "%6d %8s %-18s %s\n", i + 1, "", "", l->text);
            }
            fclose(f);
        }
    }
    return 1;
}
