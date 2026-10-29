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

/* Lays out the image, builds the import table from the imports the code uses (kernel32 and others) and writes the file. */
int fmt_pe(Obj *o, Bytes *out)
{
    int w = o->is64 ? 8 : 4;
    int dll = g_pe_dll;
    pe_base = dll ? 0x10000000 : 0x400000;
    link_prepare_imports(o);
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
