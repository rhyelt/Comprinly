#include "cly.h"

#define PE_BASE 0x400000
#define PE_FA 0x200
#define PE_SA 0x1000

static void put32(Bytes *b, u32 v) { badle(b, v, 4); }
static void put16(Bytes *b, u16 v) { badle(b, v, 2); }
static void put64(Bytes *b, u64 v) { badle(b, v, 8); }

static i64 rup(i64 v, i64 a) { return (v + a - 1) / a * a; }

typedef struct {
    char name[8];
    u32 vsize, rva, rawsize, rawptr, chars;
} PSec;

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

int fmt_pe(Obj *o, Bytes *out)
{
    int w = o->is64 ? 8 : 4;
    link_prepare_imports(o);
    link_layout(o, PE_BASE + PE_SA, PE_SA, 1);
    if (!link_apply(o)) return 0;
    if (!o->entry) {
        fprintf(stderr, "cly: error: `start' is not defined (add a `start:' label as the entry point)\n");
        return 0;
    }
    i64 entry = link_entry(o);
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
    i64 idata_va = rup(g_lay.end - PE_BASE, PE_SA);
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

    int nsec = 1 + (has_data ? 1 : 0) + (nd > 0 ? 1 : 0);
    i64 hdr_raw = PE_FA;
    i64 text_ptr = hdr_raw;
    i64 data_ptr = text_ptr + text_raw;
    i64 idata_ptr = data_ptr + data_raw;
    i64 image_size = rup((nd > 0 ? idata_va + idata_vs : g_lay.end - PE_BASE), PE_SA);

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
    put16(&f, o->is64 ? 0x0022 : 0x0103);
    put16(&f, o->is64 ? 0x20B : 0x10B);
    bad1(&f, 1);
    bad1(&f, 0);
    put32(&f, (u32)text_raw);
    put32(&f, (u32)(data_raw + idata_raw));
    put32(&f, (u32)(data_vs > data_fsz ? data_vs - data_fsz : 0));
    put32(&f, (u32)(entry - PE_BASE));
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
    put16(&f, 3);
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
        if (i == 1 && nd > 0) { put32(&f, (u32)idata_va); put32(&f, (u32)((nd + 1) * 20)); }
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
    if (nd > 0) {
        memset(&s, 0, sizeof s);
        memcpy(s.name, ".idata", 6);
        s.vsize = (u32)idata_vs; s.rva = (u32)idata_va; s.rawsize = (u32)idata_raw; s.rawptr = (u32)idata_ptr; s.chars = 0xC0000040;
        put_sec(&f, &s);
    }
    while ((i64)f.n < hdr_raw) bad1(&f, 0);
    badd(&f, text.p, text.n);
    badd(&f, data.p, data.n);
    badd(&f, idata.p, idata.n);
    badd(out, f.p, f.n);
    return 1;
}
