/* Writer for Intel OMF (.obj) object files for 16 and 32-bit x86, laid out like NASM's -f obj output. */
#include "cly.h"

typedef struct { Bytes b; int type; } Rec;

static void rec_begin(Rec *r, int type) { memset(r, 0, sizeof *r); r->type = type; }
static void r8(Rec *r, int v) { bad1(&r->b, (u8)v); }
static void r16(Rec *r, u32 v) { badle(&r->b, v, 2); }
static void r32(Rec *r, u32 v) { badle(&r->b, v, 4); }

static void rname(Rec *r, const char *s)
{
    size_t n = strlen(s);
    if (n > 255) n = 255;
    r8(r, (int)n);
    badd(&r->b, s, n);
}

static void ridx(Rec *r, int v)
{
    if (v > 127) r8(r, 0x80 | (v >> 8));
    r8(r, v & 255);
}

static void rec_end(Rec *r, Bytes *out)
{
    u32 sum = (u32)r->type + (u32)((r->b.n + 1) & 255) + (u32)(((r->b.n + 1) >> 8) & 255);
    for (size_t i = 0; i < r->b.n; i++) sum += r->b.p[i];
    bad1(out, (u8)r->type);
    badle(out, r->b.n + 1, 2);
    badd(out, r->b.p, r->b.n);
    bad1(out, (u8)(0x100 - (sum & 255)));
    free(r->b.p);
}

typedef struct { int idx; } NameIdx;

static int lname_add(char ***tab, int *n, const char *s)
{
    for (int i = 0; i < *n; i++) if (!strcmp((*tab)[i], s)) return i + 1;
    *tab = xrealloc(*tab, sizeof(char *) * (size_t)(*n + 1));
    (*tab)[*n] = xstrdup(s);
    return ++*n;
}

static int seg_bits(Obj *o, Sec *s) { return s->sbits ? s->sbits : (o->defbits ? o->defbits : 32); }

typedef struct { Sym *s; int kind; } Ext;

int fmt_omf(Obj *o, Bytes *out)
{
    if (o->arch != ARCH_X86 || o->is64 || o->bits == 64) {
        fprintf(stderr, "cly: OMF output is only supported for 16 and 32-bit x86\n");
        return 0;
    }
    int ns = o->nsecs;
    char **ln = NULL;
    int nln = 0;
    lname_add(&ln, &nln, "");
    int *snm = xcalloc((size_t)ns + 1, sizeof(int)), *scl = xcalloc((size_t)ns + 1, sizeof(int));
    for (int i = 0; i < ns; i++) {
        snm[i] = lname_add(&ln, &nln, o->secs[i]->name + (o->secs[i]->name[0] == '.' ? 1 : 0));
        scl[i] = lname_add(&ln, &nln, o->secs[i]->cls ? o->secs[i]->cls : "");
    }
    int *gnm = xcalloc((size_t)o->ngroups + 1, sizeof(int));
    for (int i = 0; i < o->ngroups; i++) gnm[i] = lname_add(&ln, &nln, o->groups[i].name);
    int *secgrp = xcalloc((size_t)ns + 1, sizeof(int));
    for (int g = 0; g < o->ngroups; g++)
        for (int k = 0; k < o->groups[g].nsegs; k++)
            for (int i = 0; i < ns; i++) if (!strcmp(o->secs[i]->name, o->groups[g].segs[k]) && !secgrp[i]) secgrp[i] = g + 1;
    int any32 = 0;
    for (int i = 0; i < ns; i++) if (seg_bits(o, o->secs[i]) == 32) any32 = 1;

    int *ord = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    for (int i = 0; i < o->nsyms; i++) ord[i] = i;
    for (int i = 1; i < o->nsyms; i++) {
        int k = ord[i], j = i - 1;
        while (j >= 0 && o->syms[ord[j]]->defline > o->syms[k]->defline) { ord[j + 1] = ord[j]; j--; }
        ord[j + 1] = k;
    }
    int *extidx = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    Ext *ex = NULL;
    int nex = 0;
    Sym *start = NULL;
    for (int oi = 0; oi < o->nsyms; oi++) {
        int i = ord[oi];
        Sym *s = o->syms[i];
        if (!s->name[0]) continue;
        if (!strcmp(s->name, "..start") && s->defined) { start = s; continue; }
        int undef = s->ext || s->common || (!s->defined && s->referenced) || s->import;
        if (!undef) continue;
        ex = xrealloc(ex, sizeof(Ext) * (size_t)(nex + 1));
        ex[nex].s = s;
        ex[nex].kind = s->common ? 2 : s->import ? 3 : 1;
        extidx[i] = ++nex;
        if (s->import) { nex--; extidx[i] = 0; }
    }
    for (int i = 0, k = 0; i < o->nsyms; i++) extidx[i] = 0, k++;
    {
        int n = 0;
        for (int oi = 0; oi < o->nsyms; oi++) {
            int i = ord[oi];
            Sym *s = o->syms[i];
            for (int k = 0; k < nex; k++) if (ex[k].s == s && ex[k].kind != 3) { extidx[i] = ++n; break; }
        }
    }

    Bytes body = {0};
    Rec r;
    rec_begin(&r, 0x80);
    rname(&r, o->src ? o->src : "cly");
    rec_end(&r, &body);
    rec_begin(&r, 0x88);
    r8(&r, 0);
    r8(&r, 0);
    {
        char tr[64];
        if (g_nasm) snprintf(tr, sizeof tr, "The Netwide Assembler 2.16.01");
        else snprintf(tr, sizeof tr, "Comprinly %s", CLY_VERSION);
        rname(&r, tr);
    }
    rec_end(&r, &body);
    for (int i = 0; i < o->nsyms; i++) {
        Sym *s = o->syms[i];
        if (!s->import || !s->impdll) continue;
        rec_begin(&r, 0x88);
        r8(&r, 0xC0);
        r8(&r, 0xA0);
        r8(&r, 1);
        r8(&r, 0);
        rname(&r, sym_mangle(s, 1));
        rname(&r, s->impdll);
        rname(&r, s->impname && strcmp(s->impname, s->name) ? s->impname : "");
        rec_end(&r, &body);
    }
    for (int i = 0; i < o->nsyms; i++) {
        Sym *s = o->syms[i];
        if (!s->exported || !s->defined) continue;
        rec_begin(&r, 0x88);
        r8(&r, 0xC0);
        r8(&r, 0xA0);
        r8(&r, 2);
        r8(&r, 0);
        rname(&r, s->expname ? s->expname : s->name);
        rname(&r, s->expname && strcmp(s->expname, s->name) ? sym_mangle(s, 1) : "");
        rec_end(&r, &body);
    }
    {
        size_t at = 0;
        while (at < (size_t)nln) {
            rec_begin(&r, 0x96);
            size_t i = at;
            while (i < (size_t)nln && r.b.n + strlen(ln[i]) + 1 < 1000) { rname(&r, ln[i]); i++; }
            if (i == at) { rname(&r, ln[i]); i++; }
            at = i;
            rec_end(&r, &body);
        }
        r.b.p = NULL;
    }
    nln = nln;
    /* The first LNAMES entry must be the empty name; the loop above wrote it first. */
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        int a = 1;
        int al = s->ualign;
        if (al == 2) a = 2;
        else if (al == 4) a = 5;
        else if (al >= 256) a = 4;
        else if (al > 4) a = 3;
        int c = s->combine == 1 ? 0 : s->combine == 6 ? 5 : s->combine == 7 ? 6 : 2;
        int big = s->size > 0xFFFF;
        rec_begin(&r, big ? 0x99 : 0x98);
        r8(&r, (a << 5) | (c << 2) | (seg_bits(o, s) == 32 ? 1 : 0));
        if (big) r32(&r, (u32)s->size); else r16(&r, (u32)s->size);
        ridx(&r, snm[i]);
        ridx(&r, scl[i]);
        ridx(&r, 1);
        rec_end(&r, &body);
    }
    for (int g = 0; g < o->ngroups; g++) {
        rec_begin(&r, 0x9A);
        ridx(&r, gnm[g]);
        for (int i = 0; i < ns; i++) if (secgrp[i] == g + 1) { r8(&r, 0xFF); ridx(&r, i + 1); }
        rec_end(&r, &body);
    }
    for (int i = 0; i < ns; i++) {
        Rec cur;
        int open = 0;
        for (int oi = 0; oi < o->nsyms; oi++) {
            Sym *s = o->syms[ord[oi]];
            if (!s->name[0] || s->import || s->ext || s->common || !s->defined || !strcmp(s->name, "..start")) continue;
            if (!(s->global || s->exported)) continue;
            if (s->isequ || s->sec < 0) continue;
            if (s->sec != i) continue;
            if (!open) { rec_begin(&cur, 0x90); ridx(&cur, secgrp[i]); ridx(&cur, i + 1); open = 1; }
            if (s->off > 0xFFFF) {
                if (cur.type == 0x90 && cur.b.n > 2) {}
                cur.type = 0x91;
            }
            if (cur.b.n + strlen(sym_mangle(s, 1)) + 8 > 1000) {
                rec_end(&cur, &body);
                int t = cur.type;
                rec_begin(&cur, t);
                ridx(&cur, secgrp[i]);
                ridx(&cur, i + 1);
            }
            rname(&cur, sym_mangle(s, 1));
            if (cur.type == 0x91) r32(&cur, (u32)s->off); else r16(&cur, (u32)s->off);
            ridx(&cur, 0);
        }
        if (open) rec_end(&cur, &body);
    }
    {
        Rec cur;
        int open = 0;
        for (int oi = 0; oi < o->nsyms; oi++) {
            Sym *s = o->syms[ord[oi]];
            if (!s->name[0] || s->import || !s->defined || !(s->global || s->exported) || !(s->isequ || s->sec < 0)) continue;
            if (!open) { rec_begin(&cur, 0x90); ridx(&cur, 0); ridx(&cur, 0); r16(&cur, 0); open = 1; }
            rname(&cur, sym_mangle(s, 1));
            r16(&cur, (u32)(s->isequ ? s->eqv.n : s->off));
            ridx(&cur, 0);
        }
        if (open) rec_end(&cur, &body);
    }
    for (int k = 0; k < nex; ) {
        int kind = ex[k].kind;
        if (kind == 3) { k++; continue; }
        Rec cur;
        rec_begin(&cur, kind == 1 ? 0x8C : 0xB0);
        while (k < nex && ex[k].kind == kind && cur.b.n < 900) {
            rname(&cur, sym_mangle(ex[k].s, 1));
            if (kind == 1) ridx(&cur, 0);
            else {
                ridx(&cur, 0);
                i64 sz = ex[k].s->commonsize;
                r8(&cur, 0x61);
                if (sz < 128) { r8(&cur, (int)sz); r8(&cur, 1); }
                else { r8(&cur, 0x81); r16(&cur, (u32)sz); r8(&cur, 1); }
            }
            k++;
        }
        rec_end(&cur, &body);
    }
    if (any32) {
        rec_begin(&r, 0x88);
        r8(&r, 0x40);
        r8(&r, 0xA2);
        r8(&r, 1);
        rec_end(&r, &body);
    }
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        if (s->nobits || s->size == 0) continue;
        int b32 = seg_bits(o, s) == 32;
        size_t pos = 0;
        const char *scn = s->cls ? s->cls : "";
        int skipzero = !strcasecmp(scn, "BSS") || !strcasecmp(scn, "STACK") || s->combine == 6 || !strcasecmp(s->name, "bss") || !strcasecmp(s->name, ".bss");
        size_t total = (size_t)s->size;
        if (s->data.n < total) total = s->data.n;
        while (pos < total) {
            size_t end = pos + 1024;
            if (end > total) end = total;
            for (int k = 0; k < o->nrelocs; k++) {
                Reloc *rl = &o->relocs[k];
                if (rl->sec != i) continue;
                if ((size_t)rl->off > pos && (size_t)rl->off < end && (size_t)(rl->off + rl->size) > end) end = (size_t)rl->off;
            }
            if (skipzero) {
                int allz = 1, anyfx = 0;
                for (size_t q = pos; q < end; q++) if (s->data.p[q]) allz = 0;
                for (int k = 0; k < o->nrelocs; k++) if (o->relocs[k].sec == i && (size_t)o->relocs[k].off >= pos && (size_t)o->relocs[k].off < end) anyfx = 1;
                if (allz && !anyfx) { pos = end; continue; }
            }
            Rec le;
            rec_begin(&le, pos > 0xFFFF ? 0xA1 : 0xA0);
            ridx(&le, i + 1);
            if (le.type == 0xA1) r32(&le, (u32)pos); else r16(&le, (u32)pos);
            Bytes chunk = {0};
            badd(&chunk, s->data.p + pos, end - pos);
            Rec fx;
            rec_begin(&fx, b32 ? 0x9D : 0x9C);
            for (int k = 0; k < o->nrelocs; k++) {
                Reloc *rl = &o->relocs[k];
                Val *v = &rl->v;
                if (rl->sec != i || (size_t)rl->off < pos || (size_t)rl->off >= end) continue;
                if (rl->kind > RK_REL || (rl->size != 1 && rl->size != 2 && rl->size != 4)) {
                    g_pos.file = "<link>";
                    g_pos.line = rl->line;
                    err("this kind of reference cannot be written to an OMF object file");
                    continue;
                }
                if (v->neg >= 0 || (v->pos >= 0 && v->ext)) {
                    g_pos.file = "<link>";
                    g_pos.line = rl->line;
                    err("expression cannot be expressed as an OMF fixup");
                    continue;
                }
                int tgt_ext = 0, tgt_idx = 0, tgt_grp = 0;
                i64 add = v->n;
                if (v->ext) {
                    int kk = -1;
                    for (int j = 0; j < o->nsyms; j++) if (o->syms[j] == v->ext) kk = j;
                    if (kk < 0 || !extidx[kk]) { err("symbol `%s' cannot be used in an OMF fixup", v->ext->name); continue; }
                    tgt_ext = 1;
                    tgt_idx = extidx[kk];
                } else if (v->pos >= 0) {
                    tgt_idx = v->pos + 1;
                    tgt_grp = secgrp[v->pos];
                } else continue;
                int pcrel = rl->kind == RK_REL;
                if (pcrel) add += rl->off + rl->size - (rl->relbase ? rl->relbase : rl->off + rl->size);
                int loc = rl->size == 1 ? 0 : rl->size == 2 ? 1 : 9;
                u32 off = (u32)(rl->off - (i64)pos);
                r8(&fx, 0x80 | (pcrel ? 0 : 0x40) | (loc << 2) | (int)((off >> 8) & 3));
                r8(&fx, (int)(off & 255));
                if (pcrel && !b32) {
                    r8(&fx, (5 << 4) | (1 << 2) | (tgt_ext ? 2 : 0));
                    ridx(&fx, tgt_idx);
                } else if (pcrel) {
                    r8(&fx, (0 << 4) | (0 << 3) | (1 << 2) | (tgt_ext ? 2 : 0));
                    ridx(&fx, tgt_ext ? 1 : i + 1);
                    ridx(&fx, tgt_idx);
                } else if (tgt_ext && b32) {
                    r8(&fx, (0 << 4) | (1 << 2) | 2);
                    ridx(&fx, secgrp[i] ? secgrp[i] : 1);
                    ridx(&fx, tgt_idx);
                } else if (tgt_ext) {
                    r8(&fx, (5 << 4) | (1 << 2) | 2);
                    ridx(&fx, tgt_idx);
                } else if (tgt_grp) {
                    r8(&fx, (1 << 4) | (1 << 2) | 0);
                    ridx(&fx, tgt_grp);
                    ridx(&fx, tgt_idx);
                } else {
                    r8(&fx, (5 << 4) | (1 << 2) | 0);
                    ridx(&fx, tgt_idx);
                }
                if (pcrel && tgt_ext) {}
                {
                    u8 *dst = chunk.p + (rl->off - (i64)pos);
                    for (int bi = 0; bi < rl->size; bi++) dst[bi] = (u8)((u64)add >> (8 * bi));
                }
            }
            badd(&le.b, chunk.p, chunk.n);
            free(chunk.p);
            rec_end(&le, &body);
            if (fx.b.n) rec_end(&fx, &body); else free(fx.b.p);
            pos = end;
        }
    }
    if (g_errors) return 0;
    if (start) {
        rec_begin(&r, any32 ? 0x8B : 0x8A);
        r8(&r, 0xC1);
        r8(&r, 0x00);
        ridx(&r, start->sec + 1);
        ridx(&r, start->sec + 1);
        if (any32) r32(&r, (u32)start->off); else r16(&r, (u32)start->off);
    } else {
        rec_begin(&r, any32 ? 0x8B : 0x8A);
        r8(&r, 0);
    }
    rec_end(&r, &body);
    badd(out, body.p, body.n);
    free(body.p);
    return 1;
}
