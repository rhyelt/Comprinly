#ifndef CLY_H
#define CLY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#define CLY_VERSION "1.0.0"

typedef int64_t i64;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef struct { char *s; size_t n, cap; } Str;
typedef struct { u8 *p; size_t n, cap; } Bytes;

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

void sadd(Str *s, const char *t);
void saddn(Str *s, const char *t, size_t n);
void sadc(Str *s, int c);
void sfmt(Str *s, const char *fmt, ...);
char *sget(Str *s);
void sclear(Str *s);

void badd(Bytes *b, const void *d, size_t n);
void bad1(Bytes *b, u8 c);
void badle(Bytes *b, u64 v, int n);
void bputle(Bytes *b, size_t off, u64 v, int n);
void bfill(Bytes *b, u8 c, size_t n);
void bpad(Bytes *b, size_t align, u8 fill);

char *read_file(const char *path, size_t *len);
int write_file(const char *path, const void *data, size_t len, int exec);
char *path_dir(const char *path);
char *path_join(const char *dir, const char *name);
int file_exists(const char *path);

typedef struct HEnt { char *key; void *val; struct HEnt *next; } HEnt;
typedef struct { HEnt **b; size_t nb, cnt; } HT;
void *ht_get(HT *h, const char *k);
void ht_put(HT *h, const char *k, void *v);
int ht_del(HT *h, const char *k);
extern int g_quiet;
void ht_clear(HT *h);

int is_dir(const char *path);
int edit_distance(const char *a, const char *b);
const char *insn_suggest(const char *mn);
typedef struct { const char *file; int line; } Pos;
extern Pos g_pos;
extern int g_errors, g_warnings, g_nowarn;
void err(const char *fmt, ...);
void warn(const char *fmt, ...);
void fatal(const char *fmt, ...);

typedef struct { char *text; const char *file; int line; } SLine;
typedef struct { SLine *v; int n, cap; } LineVec;
void lv_add(LineVec *l, const char *text, const char *file, int line);

typedef struct Sym Sym;

#define VR_N 160
typedef struct {
    i64 n;
    int pos, neg;
    Sym *ext;
    int unk;
    int seg;
    int wrt;
    int hasreg;
    int isfloat;
    signed char r[VR_N];
    short rseq[4];
    int nrseq;
} Val;

void val_const(Val *v, i64 n);
int val_isconst(const Val *v);

enum {
    RC_NONE, RC_R8, RC_R16, RC_R32, RC_R64, RC_RIP, RC_SEG, RC_CR, RC_DR, RC_TR,
    RC_MMX, RC_XMM, RC_YMM, RC_ST, RC_VREG, RC_ZMM, RC_K, RC_BND
};
#define REGID(c, n) (((c) << 5) | (n))
#define REGCLS(r) ((r) >> 5)
#define REGNUM(r) ((r) & 31)

int reg_lookup(const char *name, int *size);
const char *reg_name(int id);
void reg_altreg(int on);
void reg_mode(int bits);
int reg_slot(int rid);
int slot_reg(int slot);

enum { EF_PP = 1, EF_EA = 2 };

typedef struct {
    int (*sym_lookup)(const char *name, Val *out);
    void (*cur_loc)(Val *out);
    void (*sec_start)(Val *out);
} ExprHooks;
extern ExprHooks g_hooks;

int expr_parse(const char **pp, Val *out, int flags);
int expr_eval_str(const char *s, Val *out, int flags);
int ident_start(int c);
int ident_char(int c);
int parse_number_tok(const char *s, const char **end, i64 *out, int *isfloat);
int parse_charconst(const char **pp, Str *out);
int float_encode(const char *text, int kind, u8 *out);
int float_special(const char *name, const char *text, u8 *out, int *nbytes);

typedef struct {
    LineVec lines;
    int ok;
} PPResult;

typedef struct {
    char **incdirs;
    int nincdirs;
    char **defs;
    int ndefs;
    char **undefs;
    int nundefs;
    char **preincs;
    int npreincs;
    const char *outfmt;
    int bits;
    int pass;
} PPOpts;

int pp_run(const char *path, PPOpts *o, LineVec *out);

enum { SEC_ABS = -1 };

typedef struct Sec {
    char *name;
    Bytes data;
    i64 size;
    int nobits, exec, write, alloc;
    int align;
    int has_start, has_vstart;
    i64 start, vstart;
    char *follows, *vfollows;
    int idx;
    int absolute;
    i64 base;
    i64 vbase;
    i64 fileoff;
} Sec;

enum { RK_ABS, RK_REL };

typedef struct {
    int sec;
    i64 off;
    int size;
    int kind;
    i64 relbase;
    Val v;
    int line;
} Reloc;

struct Sym {
    char *name;
    int defined;
    int isequ;
    int sec;
    i64 off;
    Val eqv;
    int global, ext, common, isstatic;
    i64 commonsize;
    int referenced;
    const char *reffile;
    int refline;
    int import;
    char *impdll, *impname;
    int exported;
    char *expname;
    int defline;
    int defpass;
    int isabs;
};

typedef struct {
    Sec **secs;
    int nsecs;
    Reloc *relocs;
    int nrelocs, caprelocs;
    Sym **syms;
    int nsyms;
    int bits;
    int is64;
    int org, has_org;
    Sym *entry;
    int uses_syscall;
    char **listing;
    int nlist;
} Obj;

typedef struct {
    const char *kernel;
    const char *type;
    int bits;
    int raw;
    int flat;
} Target;

enum { K_BARE, K_WINDOWS, K_LINUX, K_MAC };
enum { T_BIN, T_ELF, T_EXE, T_MACHO, T_FLAT, T_IMG, T_COM, T_HEX, T_SREC, T_OBJ, T_COFF };

typedef struct {
    int kernel;
    int type;
    const char *srcpath;
    LineVec lines;
    int flat_mode;
    int list;
    char *listpath;
    int cpu;
    int optimize;
    int bits;
} AsmOpts;

int assemble(AsmOpts *o, Obj *out);
void obj_free(Obj *o);

Sym *sym_find(const char *name);

void insn_init(void);
void insn_dump(FILE *f);

typedef struct {
    Bytes code;
    Reloc *relocs;
    int nrel;
    int caprel;
    int widen;
    int cpuwarn;
} InsnOut;

enum { OK_NONE, OK_REG, OK_MEM, OK_IMM, OK_FAR };
enum { OF_SHORT = 1, OF_NEAR = 2, OF_FAR = 4, OF_STRICT = 8, OF_TO = 16, OF_NOSPLIT = 32, OF_REL = 64, OF_ABS = 128 };

typedef struct Op {
    int kind, reg, size, flags;
    int kreg, zero, bcst, rc;
    Val val, seg;
    int base, index, scale, mseg, asize, dispsize;
    int vreg, vsize;
} Op;

typedef struct {
    int bits;
    int lock, rep, repne, xacq, xrel, bnd, segpre, o16, o32, a16, a32;
    int cur_sec;
    i64 start_off;
    int wide;
    int need_wide;
    int cpu;
    int quiet;
    int line;
    int nomatch;
    const char *why;
    int vexpfx;
} IC;

int insn_known(const char *mn);
int insn_encode(const char *mn, Op *ops, int nops, IC *ic, InsnOut *out);
void insn_cpu_set(const char *name, int *lvl);


typedef struct {
    i64 base, a_end, rw_start, file_end, end, rw_lma;
    int has_rw, has_bss;
    Sec **order;
    int n;
} Layout;
extern Layout g_lay;

typedef struct {
    char *dll;
    char **names;
    i64 *slot;
    int n, cap;
} ImpDll;

extern const char rt_src_linux[], rt_src_win[], rt_src_mac[], rt_src_bare[], rt_src_win64[], rt_src_mac64[], rt_src_bare64[];

int rt_build(int kernel, int type, int bits, Str *src);
int rt_boot(Str *src, u32 entry, int nsect, u32 bss, u32 bsslen, int bits);

i64 sym_addr(Obj *o, Sym *s);
i64 val_addr(Obj *o, const Val *v, int *ok);
i64 sec_addr(Obj *o, int idx);
int link_layout(Obj *o, i64 base, i64 pagesz, int mode);
int link_apply(Obj *o);
int link_image(Obj *o, Bytes *img);
void link_range(Obj *o, Bytes *dst, i64 lo, i64 hi);
void link_prepare_imports(Obj *o);
const ImpDll *link_imports(int *n);
int link_obj(Obj *o, int kernel, int type, Bytes *out);
i64 link_entry(Obj *o);

int fmt_flat(Obj *o, Bytes *out);
int fmt_elf(Obj *o, Bytes *out);
int fmt_pe(Obj *o, Bytes *out);
int fmt_macho(Obj *o, Bytes *out);
int fmt_bare(Obj *o, Bytes *out);
int fmt_elfobj(Obj *o, Bytes *out);
int fmt_coff(Obj *o, Bytes *out);
void fmt_ihex(const Bytes *in, i64 base, i64 entry, Str *out);
void fmt_srec(const Bytes *in, i64 base, i64 entry, Str *out);

#endif
