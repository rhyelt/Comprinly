#ifndef A64_SIMD_H
#define A64_SIMD_H

#include "a64.h"

#define PASS (-1)

typedef struct { VReg v[4]; int n; int lane; int hasl; } VList;
int fp_imm8(const char *s, int *out);
int sv_parse_list(Ctx *c, int idx, void *L, int allow_elem);

int sv_vreg(Ctx *c, int i, VReg *v);
int sv_gpr(Ctx *c, int i, GReg *g);
int sv_esz(int es);
const char *sv_arrname(int es, int lanes);
int sv_arr(Ctx *c, const VReg *v, int i, int *q);
int sv_same(Ctx *c, const VReg *a, const VReg *b, int i, int j);
int sv_count(Ctx *c, int lo, int hi);
int sv_has_vec(Ctx *c);
int sv_all_scalar(Ctx *c);
int sv_imm(Ctx *c, int i, i64 *v);

int shift_known(const char *mn);
int byelem_known(const char *mn);
int elem_operand(Ctx *c, int i);
int h_three(Ctx *c, const MnEnt *e);
int h_logic3(Ctx *c, const MnEnt *e);
int h_two(Ctx *c, const MnEnt *e);
int h_across(Ctx *c, const MnEnt *e);
int h_permute(Ctx *c, const MnEnt *e);
int h_ext(Ctx *c, const MnEnt *e);
int h_tbl(Ctx *c, const MnEnt *e);
int h_diff(Ctx *c, const MnEnt *e);
int h_dup(Ctx *c, const MnEnt *e);
int h_ins(Ctx *c, const MnEnt *e);
int h_umov(Ctx *c, const MnEnt *e);
int h_vmov(Ctx *c, const MnEnt *e);
int h_movi(Ctx *c, const MnEnt *e);
int h_orrbic(Ctx *c, const MnEnt *e);
int h_vfmov(Ctx *c, const MnEnt *e);
int h_shift(Ctx *c, const MnEnt *e);
int h_xtl(Ctx *c, const MnEnt *e);
int h_byelem(Ctx *c, const MnEnt *e);
int h_ldst_s(Ctx *c, const MnEnt *e);
int h_crypto(Ctx *c, const MnEnt *e);
int h_pairwise(Ctx *c, const MnEnt *e);
int h_dot(Ctx *c, const MnEnt *e);
int h_misc(Ctx *c, const MnEnt *e);

#endif
