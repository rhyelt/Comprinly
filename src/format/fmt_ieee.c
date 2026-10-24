/* Writer for IEEE-695 ASCII object files (the format of NASM's -f ieee). */
#include <time.h>
#include "cly.h"

static Bytes g_o;
static int g_sum;

static void put(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int l = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (l < 0) return;
    if ((size_t)l >= sizeof buf) l = sizeof buf - 1;
    for (int i = 0; i < l; i++) if ((u8)buf[i] > 31) g_sum += buf[i];
    badd(&g_o, buf, (size_t)l);
}

static void put_cs(void)
{
    g_sum += 'C';
    g_sum += 'S';
    put("CS%02X.\n", g_sum & 127);
    g_sum = 0;
}

typedef struct { i64 off; int size, kind, id; i64 add; } Fix;

static int cmp_fix(const void *a, const void *b)
{
    const Fix *x = a, *y = b;
    return x->off < y->off ? -1 : x->off > y->off;
}

static void put_ld(const u8 *p, i64 start, i64 end)
{
    while (start < end) {
        i64 n = end - start;
        if (n > 32) n = 32;
        put("LD");
        for (i64 i = 0; i < n; i++) put("%02X", p[start + i]);
        put(".\n");
        start += n;
    }
}

int fmt_ieee(Obj *o, Bytes *out)
{
    int ns = o->nsecs;
    if (o->arch != ARCH_X86 || o->is64 || o->bits == 64) {
        fprintf(stderr, "cly: IEEE output is only supported for 16 and 32-bit x86\n");
        return 0;
    }
    memset(&g_o, 0, sizeof g_o);
    g_sum = 0;
    int *ord = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    int *extno = xcalloc((size_t)o->nsyms + 1, sizeof(int));
    for (int i = 0; i < o->nsyms; i++) ord[i] = i;
    for (int i = 1; i < o->nsyms; i++) {
        int k = ord[i], j = i - 1;
        while (j >= 0 && o->syms[ord[j]]->defline > o->syms[k]->defline) { ord[j + 1] = ord[j]; j--; }
        ord[j + 1] = k;
    }
    int next = 0;
    Sym *start = NULL;
    for (int oi = 0; oi < o->nsyms; oi++) {
        int i = ord[oi];
        Sym *s = o->syms[i];
        if (!s->name[0] || s->import) continue;
        if (!strcmp(s->name, "..start") && s->defined) { start = s; continue; }
        if (s->ext || s->common || (!s->defined && s->referenced)) extno[i] = ++next;
    }
    const char *src = o->src ? o->src : "cly";
    put("MBFNASM,%02X%s.\n", (unsigned)strlen(src), src);
    {
        char tr[64];
        if (g_nasm) snprintf(tr, sizeof tr, "The Netwide Assembler 2.16.01");
        else snprintf(tr, sizeof tr, "Comprinly %s", CLY_VERSION);
        put("CO0,%02X%s.\n", (unsigned)strlen(tr), tr);
    }
    put("AD8,4,L.\n");
    {
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        put("DT%04d%02d%02d%02d%02d%02d.\n", 1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    put("CO101,07ENDHEAD.\n");
    put_cs();
    i64 *secextra = xcalloc((size_t)ns + 1, sizeof(i64));
    for (int i = 0; i < o->nrelocs; i++) {
        Reloc *r = &o->relocs[i];
        Val *v = &r->v;
        if (r->kind > RK_REL || (r->size != 2 && r->size != 4) || v->neg >= 0 || (v->pos >= 0 && v->ext) || (v->pos < 0 && !v->ext)) continue;
        secextra[r->sec] += r->size;
    }
    for (int i = 0; i < ns; i++) {
        Sec *s = o->secs[i];
        const char *nm = s->name;
        while (*nm == '.') nm++;
        char attr = s->combine == 1 ? 'S' : s->combine == 7 ? 'M' : 'C';
        put("ST%X,%c,%02X%s.\n", i + 1, attr, (unsigned)strlen(nm), nm);
        put("SA%X,%X.\n", i + 1, s->ualign > 0 ? s->ualign : 1);
        put("ASS%X,%X.\n", i + 1, (unsigned)(s->size + secextra[i]));
    }
    if (start && start->sec >= 0) put("ASG,R%X,%llX,+.\n", start->sec + 1, (unsigned long long)start->off);
    put_cs();
    int pi = 1;
    for (int si = 0; si < ns; si++) {
        for (int oi = 0; oi < o->nsyms; oi++) {
            Sym *s = o->syms[ord[oi]];
            if (!s->name[0] || s->import || s->ext || s->common || !s->defined || s->isequ || s->sec != si) continue;
            if (!s->global && !s->exported) continue;
            if (!strcmp(s->name, "..start")) continue;
            const char *nm = sym_mangle(s, 1);
            put("NI%X,%02X%s.\n", pi, (unsigned)strlen(nm), nm);
            put("ASI%X,R%X,%llX,+.\n", pi, si + 1, (unsigned long long)s->off);
            pi++;
        }
    }
    for (int oi = 0; oi < o->nsyms; oi++) {
        int i = ord[oi];
        if (!extno[i]) continue;
        const char *nm = sym_mangle(o->syms[i], 1);
        put("NX%X,%02X%s.\n", extno[i], (unsigned)strlen(nm), nm);
    }
    put_cs();
    put("CO100,06ENDSYM.\n");
    for (int si = 0; si < ns; si++) {
        Sec *s = o->secs[si];
        if (!s->size) continue;
        Fix *fx = NULL;
        int nf = 0;
        Bytes data = {0};
        for (i64 k = 0; k < s->size; k++) bad1(&data, 0);
        if (s->data.n) memcpy(data.p, s->data.p, s->data.n < (size_t)s->size ? s->data.n : (size_t)s->size);
        for (int i = 0; i < o->nrelocs; i++) {
            Reloc *r = &o->relocs[i];
            Val *v = &r->v;
            if (r->sec != si) continue;
            if (r->kind > RK_REL || (r->size != 2 && r->size != 4)) {
                g_pos.file = "<link>";
                g_pos.line = r->line;
                err("this kind of reference cannot be written to an IEEE object file");
                continue;
            }
            if (v->neg >= 0 || (v->pos >= 0 && v->ext)) {
                g_pos.file = "<link>";
                g_pos.line = r->line;
                err("expression cannot be expressed as an IEEE fixup");
                continue;
            }
            Fix f;
            memset(&f, 0, sizeof f);
            f.off = r->off;
            f.size = r->size;
            f.add = v->n;
            int rel = r->kind == RK_REL;
            if (v->ext) {
                int k = -1;
                for (int j = 0; j < o->nsyms; j++) if (o->syms[j] == v->ext) k = j;
                if (k < 0 || !extno[k]) { err("symbol `%s' cannot be used in an IEEE fixup", v->ext->name); continue; }
                f.kind = rel ? 5 : 3;
                f.id = extno[k];
            } else if (v->pos >= 0) {
                f.kind = rel ? 1 : 2;
                f.id = v->pos + 1;
            } else continue;
            for (int b = 0; b < r->size; b++) data.p[r->off + b] = (u8)((u64)f.add >> (8 * b));
            fx = xrealloc(fx, sizeof(Fix) * (size_t)(nf + 1));
            fx[nf++] = f;
        }
        if (g_errors) return 0;
        if (nf > 1) qsort(fx, (size_t)nf, sizeof(Fix), cmp_fix);
        i64 extra = 0;
        for (int k = 0; k < nf; k++) extra += fx[k].size;
        i64 vsize = s->size + extra;
        Bytes vd = {0};
        i64 voff = 0, rpos = 0;
        for (int k = 0; k <= nf; k++) {
            i64 end = k < nf ? fx[k].off : s->size;
            while (rpos < end) { bad1(&vd, data.p[rpos++]); }
            if (k == nf) break;
            fx[k].off = (i64)vd.n;
            for (int b2 = 0; b2 < fx[k].size; b2++) bad1(&vd, 0);
            for (int b2 = 0; b2 < fx[k].size; b2++) bad1(&vd, data.p[rpos + b2]);
            rpos += fx[k].size;
        }
        (void)voff;
        free(data.p);
        data = vd;
        put("SB%X.\n", si + 1);
        i64 org = 0;
        for (int k = 0; k <= nf; k++) {
            i64 end = k < nf ? fx[k].off : vsize;
            while (org < end) {
                i64 e = (org / 1024 + 1) * 1024;
                if (e > end) e = end;
                put_ld(data.p, org, e);
                org = e;
            }
            if (k == nf) break;
            Fix *f = &fx[k];
            char buf[80];
            switch (f->kind) {
            case 2: snprintf(buf, sizeof buf, "R%X,%llX,+", f->id, (unsigned long long)(u32)f->add); break;
            case 1: snprintf(buf, sizeof buf, "R%X,%llX,+,P,-,%X,-", f->id, (unsigned long long)(u32)f->add, f->size); break;
            case 3: snprintf(buf, sizeof buf, "X%X", f->id); break;
            default: snprintf(buf, sizeof buf, "X%X,P,-,%X,-", f->id, f->size); break;
            }
            put("LR(%s,%X).\n", buf, f->size);
            org = f->off + f->size;
        }
        put_cs();
        free(data.p);
        free(fx);
    }
    put("ME.\n");
    badd(out, g_o.p, g_o.n);
    free(g_o.p);
    return 1;
}
