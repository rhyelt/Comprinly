#ifndef A64_H
#define A64_H

#include "cly.h"

typedef struct { int n, w64, sp, zr; } GReg;
typedef struct { int n; int has_arr; int lanes; int es; int lane; int scalar; int sk; } VReg;

typedef struct Ctx {
    char mn[40];
    char *tok[12];
    int n;
    IC *ic;
    InsnOut *out;
    int floor;
    int optimize;
} Ctx;

typedef struct MnEnt {
    const char *name;
    int (*fn)(Ctx *c, const struct MnEnt *e);
    int a, b, c;
} MnEnt;

typedef struct {
    int base_sp;
    int base;
    int mode;
    int kind;
    Val imm;
    int hasmod;
    int mod;
    int idx, idxw64, idxzr;
    int ext;
    int shamt;
    int hassh;
    int unscaled_hint;
    char post[64];
    int has_post;
    int symbolic;
} AMem;

char *a64_split_shift(char *buf);
int a64_p_gpr(const char *s, GReg *r);
int a64_p_vec(const char *s, VReg *v);
int a64_p_cond(const char *s, int *cc);
int a64_eval(Ctx *c, const char *s, Val *v);
int a64_const(Ctx *c, const char *s, i64 *out, int *unk);
const char *a64_strip_hash(const char *s);
int a64_parse_mem(Ctx *c, const char *s, AMem *m);
void a64_W(Ctx *c, u32 w);
i64 a64_cur_off(Ctx *c);
void a64_reloc(Ctx *c, int kind, int size, const Val *v, i64 off);
int a64_same_sec(Ctx *c, const Val *v);
int a64_need_n(Ctx *c, int count);
void a64_pad(Ctx *c);
int a64_logical_imm(u64 imm, int is64, u32 *out);
int a64_enc_branch(Ctx *c, u32 base, const Val *v, int kind, int bits, int sub, int shift_field);
int a64_mov_imm(Ctx *c, GReg rd, i64 val, int unk);
int a64_load_addr(Ctx *c, int rd, const Val *v);
void a64_trim(char *s);
const MnEnt *a64_lookup(const char *mn);

extern const MnEnt a64_tab_int[];
extern const MnEnt a64_tab_mem[];
extern const MnEnt a64_tab_fp[];
extern const MnEnt a64_tab_simd[];
extern const MnEnt a64_tab_ext[];
const MnEnt *a64_vlookup(const char *mn);
int a64_simd_dispatch(Ctx *c);
int a64_simd_mov(Ctx *c);

#endif
