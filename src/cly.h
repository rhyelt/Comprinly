/* Shared header: types, globals and prototypes for every part of the assembler. */
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

#define CLY_VERSION "1.3.1"

/* Case-insensitive string compare that bails out on the first differing character, which is faster than strcasecmp on typical symbol names. */
static inline int cly_ieq(const char *a, const char *b)
{
    int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
    if (x != y) return x - y;
    return strcasecmp(a, b);
}

/* Same as cly_ieq but looks at no more than n characters. */
static inline int cly_nieq(const char *a, const char *b, size_t n)
{
    if (!n) return 0;
    int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
    if (x != y) return x - y;
    return strncasecmp(a, b, n);
}

/* Finds the next newline in p. Returns NULL when there is none. Used instead of strchr because strchr scans to the end of long buffers on musl. */
static inline char *cly_nl(const char *p)
{
    while (*p && *p != '\n') p++;
    return *p ? (char *)p : NULL;
}

#undef strcasecmp
#undef strncasecmp
#define strcasecmp cly_ieq
#define strncasecmp cly_nieq

/* Short names for the fixed width integer types used everywhere. */
typedef int64_t i64;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

/* Growable string: text, length, capacity. */
typedef struct { char *s; size_t n, cap; } Str;
/* Growable byte buffer used for all machine code and file output. */
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

/* String keyed hash table. Chained buckets; grows as it fills. */
typedef struct HEnt { char *key; void *val; struct HEnt *next; } HEnt;
typedef struct { HEnt **b; size_t nb, cnt; } HT;
void *ht_get(HT *h, const char *k);
char *lower_dup(const char *s);
void ht_put(HT *h, const char *k, void *v);
int ht_del(HT *h, const char *k);
extern int g_quiet;
void ht_clear(HT *h);

int is_dir(const char *path);
int edit_distance(const char *a, const char *b);
const char *insn_suggest(const char *mn);
/* Source position used in messages. g_pos is the line being processed. */
typedef struct { const char *file; int line; } Pos;
extern Pos g_pos;
extern int g_log;
int log_open(const char *path);
void log_close(void);
void dep_add(const char *path);
int dep_count(void);
const char *dep_get(int i);
extern int g_werror;
void xlog(const char *phase, const char *fmt, ...);
extern int g_hidewarn;
extern int g_strip, g_errors, g_warnings, g_nowarn, g_nasm, g_objmode;
void err(const char *fmt, ...);
void warn(const char *fmt, ...);
void fatal(const char *fmt, ...);

/* One source line with the file and line number it came from. */
typedef struct { char *text; const char *file; int line; } SLine;
/* A list of source lines. The preprocessor produces one, every later stage works on it. */
typedef struct { SLine *v; int n, cap; } LineVec;
void lv_add(LineVec *l, const char *text, const char *file, int line);

typedef struct Sym Sym;

/* Number of virtual registers tracked inside an address expression. */
#define VR_N 160
/* An expression value. n is the constant part. pos and neg are section indexes of a symbol added or subtracted (-1 for none), so label differences fold to constants. ext is an external symbol. unk means the value is not known yet (first pass). r[] counts registers used in an address, for base+index*scale. */
typedef struct {
    i64 n;
    int pos, neg;
    Sym *ext;
    int unk;
    int seg;
    int wrt;
    int wk;
    int hasreg;
    int isfloat;
    signed char r[VR_N];
    short rseq[4];
    int nrseq;
} Val;

void val_const(Val *v, i64 n);
int val_isconst(const Val *v);

/* Register classes. A register id is (class << 5) | number. */
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

/* Expression flags: EF_PP is preprocessor mode (no symbols), EF_EA is address mode (registers allowed). */
enum { EF_PP = 1, EF_EA = 2 };

/* Callbacks the assembler gives the expression parser so it can resolve symbols, $ and $$. */
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
extern int num_overflow;
int parse_number_tok(const char *s, const char **end, i64 *out, int *isfloat);
int parse_charconst(const char **pp, Str *out);
int float_encode(const char *text, int kind, u8 *out);
int float_special(const char *name, const char *text, u8 *out, int *nbytes);

typedef struct {
    LineVec lines;
    int ok;
} PPResult;

/* Options for the preprocessor: include folders, -D and -U macros, pre-included files, and facts about the target for the predefined names. */
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
    int arch;
} PPOpts;

int pp_run(const char *path, PPOpts *o, LineVec *out);
void opt_run(LineVec *lv, int bits, int kernel, int type);
void opt_run_nx(LineVec *lv, int arch, int kernel, int type);

/* Section index used for absolute (not section relative) values. */
enum { SEC_ABS = -1 };

/* An output section. data holds the bytes (empty for bss). size is the length. nobits marks bss. exec, write and alloc are the flags. align is the section alignment and ualign the one the user asked for. start and vstart are fixed addresses if given. base is the address chosen by the linker. */
typedef struct Sec {
    char *name;
    Bytes data;
    i64 size;
    int nobits, exec, write, alloc;
    int align;
    int ualign;
    int has_start, has_vstart;
    i64 start, vstart;
    char *follows, *vfollows;
    int idx;
    int declared, order;
    int absolute;
    i64 base;
    i64 vbase;
    i64 fileoff;
} Sec;

/* Relocation kinds: plain absolute, PC relative, then the ARM64 and RISC-V instruction field kinds. */
enum {
    RK_ABS, RK_REL,
    RK_A64_B26, RK_A64_B19, RK_A64_B14, RK_A64_ADR, RK_A64_ADRP, RK_A64_LO12, RK_A64_MOVW,
    RK_RV_JAL, RK_RV_BRANCH, RK_RV_HI20, RK_RV_LO12I, RK_RV_LO12S, RK_RV_PCHI20, RK_RV_PCLO12I, RK_RV_PCLO12S, RK_RV_CALL, RK_RV_CJ, RK_RV_CB
};

/* Supported CPUs. */
enum { ARCH_X86, ARCH_A64, ARCH_RV64, ARCH_RV32 };
/* RISC-V extension bits. RVX_DEFAULT is what plain risc-v and rv64gc style names get. */
enum { RVX_I = 1, RVX_M = 2, RVX_A = 4, RVX_F = 8, RVX_D = 16, RVX_C = 32, RVX_ZICSR = 64, RVX_ZIFENCEI = 128, RVX_ZBA = 256, RVX_ZBB = 512, RVX_ZBS = 1024, RVX_ZBC = 2048, RVX_ZICOND = 4096, RVX_ZFH = 1 << 13, RVX_ZFHMIN = 1 << 14, RVX_ZBKB = 1 << 15, RVX_ZBKX = 1 << 16, RVX_ZKND = 1 << 17, RVX_ZKNE = 1 << 18, RVX_ZKNH = 1 << 19, RVX_ZKSED = 1 << 20, RVX_ZKSH = 1 << 21, RVX_ZICBOM = 1 << 22, RVX_ZICBOZ = 1 << 23, RVX_ZICBOP = 1 << 24, RVX_ZAWRS = 1 << 25, RVX_SVINVAL = 1 << 26, RVX_H = 1 << 27 };
#define RVX_DEFAULT (RVX_I | RVX_M | RVX_A | RVX_F | RVX_D | RVX_C | RVX_ZICSR | RVX_ZIFENCEI | RVX_ZBA | RVX_ZBB | RVX_ZBS | RVX_ZBC | RVX_ZICOND | RVX_ZFH | RVX_ZFHMIN | RVX_ZBKB | RVX_ZBKX | RVX_ZKND | RVX_ZKNE | RVX_ZKNH | RVX_ZKSED | RVX_ZKSH | RVX_ZICBOM | RVX_ZICBOZ | RVX_ZICBOP | RVX_ZAWRS | RVX_SVINVAL | RVX_H)

/* A relocation: patch size bytes at offset off of section sec with the value of v. relbase is the point a PC relative value is measured from. */
typedef struct {
    int sec;
    i64 off;
    int size;
    int kind;
    i64 relbase;
    Val v;
    int line;
    int sext;
} Reloc;

/* A symbol. Covers labels, equ constants, externs, globals, commons, imports and exports, with where it was defined for error messages. cinext chains symbols that differ only by case. */
struct Sym {
    char *name;
    Sym *cinext;
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
    const char *deffile;
    int defln;
    int isabs;
    int stype, svis, commonalign;
    i64 ssize;
    char *sexpr;
};

/* The assembled program handed to the linker: sections, relocations and symbols plus facts about the target. */
typedef struct {
    Sec **secs;
    int nsecs;
    Reloc *relocs;
    int nrelocs, caprelocs;
    Sym **syms;
    int nsyms;
    int bits;
    int is64;
    int arch, rvext;
    int org, has_org;
    Sym *entry;
    char **listing;
    int nlist;
    const char *src;
} Obj;

typedef struct {
    const char *kernel;
    const char *type;
    int bits;
    int raw;
    int flat;
} Target;

/* Kernels (targets) and output types. */
enum { K_BARE, K_WINDOWS, K_LINUX, K_MAC };
enum { T_BIN, T_ELF, T_EXE, T_MACHO, T_FLAT, T_IMG, T_COM, T_HEX, T_SREC, T_OBJ, T_COFF };

/* Everything the assembler needs for one build: target, source lines, listing and CPU choices. */
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
    int arch, rvext;
    int arch_given;
} AsmOpts;
int detect_bits(AsmOpts *o);
void libc_run(LineVec *lv, int kernel, int type, int defbits);
int leak_check(LineVec *lv, int bits, LineVec *fixed, int *nfixed);

int assemble(AsmOpts *o, Obj *out);
void obj_free(Obj *o);

Sym *sym_find(const char *name);

void insn_init(void);
void insn_dump(FILE *f);

/* Result of encoding one instruction: bytes and the relocations in them. */
typedef struct {
    Bytes code;
    Reloc *relocs;
    int nrel;
    int caprel;
    int widen;
    int cpuwarn;
} InsnOut;

/* Operand kinds and operand flags (short, near, far, strict, to, rel, abs). */
enum { OK_NONE, OK_REG, OK_MEM, OK_IMM, OK_FAR };
enum { OF_SHORT = 1, OF_NEAR = 2, OF_FAR = 4, OF_STRICT = 8, OF_TO = 16, OF_NOSPLIT = 32, OF_REL = 64, OF_ABS = 128 };

/* A parsed instruction operand: a register, a memory reference (base, index, scale, displacement, segment) or an immediate, with size and AVX-512 decorators. */
typedef struct Op {
    int kind, reg, size, flags;
    int kreg, zero, bcst, rc;
    Val val, seg;
    int base, index, scale, mseg, asize, dispsize;
    int vreg, vsize;
} Op;

/* Instruction context: current bit size, prefixes seen, section, position, CPU level and the reason the last match failed. */
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
    int arch, rvext, optimize, kernel;
} IC;

int insn_known(const char *mn);
const char *arch_name(int arch);
int arch_parse(const char *s, int *rvext);
int arch_rv_generic(const char *name);
int arch_cpu_line(const char *text, char *arg, size_t sz);
int arch_first_cpu(const LineVec *lv, char *arg, size_t sz, int *lineno);
int arch_guess(const LineVec *lv, int *rvext);
extern int g_no_abs32;
void sysval_run(const LineVec *lv, int arch, int kernel, int bits);
int arch_check_foreign(const LineVec *lv, int arch);
extern int g_guess_line;
extern char g_guess_tok[64];
int arch_known(int arch, const char *mn);
int pp_macro_arity(const char *name, int *minp, int *maxp);
int arch_bits(int arch);
int arch_insn(int arch, const char *mn, const char *rest, IC *ic, InsnOut *out);
int a64_known(const char *mn);
int a64_insn(const char *mn, const char *rest, IC *ic, InsnOut *out);
int rv_known(const char *mn, int rvext);
int rv_insn(const char *mn, const char *rest, IC *ic, InsnOut *out);
int link_arch_reloc(Obj *o, Reloc *r, i64 S, i64 P, int *handled);
int insn_encode(const char *mn, Op *ops, int nops, IC *ic, InsnOut *out);
void insn_cpu_set(const char *name, int *lvl);


/* Result of laying out sections: the load address, end of code, start of writable data, end of file data and end of memory. */
typedef struct {
    i64 base, a_end, rw_start, file_end, end, rw_lma;
    int has_rw, has_bss;
    Sec **order;
    int n;
} Layout;
extern Layout g_lay;
extern int g_compact;

/* The functions imported from one DLL, with their slots in the import address table. */
typedef struct {
    char *dll;
    char **names;
    i64 *slot;
    int n, cap;
} ImpDll;

void lz_unpack(const unsigned char *in, size_t n, unsigned char *out);
char *lz_text(const unsigned char *in, size_t n, size_t rawn);
const char *libc_text(int bits);

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
