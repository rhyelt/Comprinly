#ifndef CLY_INSN_H
#define CLY_INSN_H

#include "cly.h"

enum {
    S_NONE, S_R8, S_R16, S_R32, S_RR8, S_RR16, S_RR32,
    S_RM8, S_RM16, S_RM32,
    S_M, S_M8, S_M16, S_M32, S_M64, S_M80, S_M128, S_M256, S_MFAR,
    S_MO8, S_MO16, S_MO32,
    S_IMM8, S_IMM16, S_IMM32, S_SB16, S_SB32, S_ONE,
    S_AL, S_CL, S_DX, S_AX, S_EAX,
    S_ES, S_CS, S_SS, S_DS, S_FS, S_GS,
    S_SREG, S_CR, S_DR, S_TR,
    S_MM, S_MMR, S_MM32, S_MM64,
    S_XMM, S_XMMR, S_XM16, S_XM32, S_XM64, S_XM128,
    S_YMM, S_YMMR, S_YM256,
    S_ST0, S_STI, S_XMM0,
    S_REL8, S_REL, S_FARPTR,
    S_R32M16, S_R32M8,
    S_M512, S_BND, S_BNDR,
    S_R64, S_RR64, S_RM64, S_RAX, S_RCX, S_IMM64, S_SD, S_UD, S_MO64, S_R32X, S_RM32X, S_SB64, S_ECX, S_EDX, S_CX, S_IBS, S_IBU
};

#define SV_BASE 0x100
#define SV(cls, mem, vsib) (SV_BASE | (cls) | ((vsib) << 2) | ((mem) << 3))
#define IS_SV(s) ((s) >= SV_BASE)
#define SV_CLS(s) ((s) & 3)
#define SV_VSIB(s) (((s) >> 2) & 1)
#define SV_MEM(s) (((s) >> 3) & 15)

enum { DC_MASK = 1, DC_Z = 2, DC_BC = 4, DC_ER = 8, DC_SAE = 16, DC_STAR = 32 };
enum { VK_NONE, VK_VEX, VK_EVEX, VK_XOP };
enum { TU_NONE, TU_FV, TU_HV, TU_FVM, TU_T1S, TU_T1S8, TU_T1S16, TU_T1F32, TU_T1F64, TU_T2, TU_T4, TU_T8, TU_HVM, TU_QVM, TU_OVM, TU_M128, TU_DUP };

typedef struct Ent {
    const char *mn;
    int nops;
    u16 sp[6];
    u8 dc[6];
    char rl[8];
    u8 vkind, vL, vpp, vmap, vW, tuple, dup, late, sflag;
    char *enc;
    u8 osz;
    u8 level;
    u8 fixedlen;
    u8 msize;
    u8 mode;
    struct Ent *next;
} Ent;

extern HT g_etab;
void insn_tab_init(void);
int spec_msize(int s);
void T(const char *mn, const char *ops, const char *enc);
void TF(const char *mn, const char *ops, const char *fmt, ...);
void tab_lv(int n);

#endif
