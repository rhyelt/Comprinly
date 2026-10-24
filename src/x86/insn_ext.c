/* Small extra instruction groups (TSX, SVM, CET, user interrupts and other rare or newer ones) added after the main tables. */
#include "cly.h"
#include "insn.h"

/* Adds the extra rows. */
void ext_tab(void)
{
    static const struct { const char *n, *e; int lv; } z[] = {
        { "xtest", "0f 01 d6", 14 }, { "xend", "0f 01 d5", 14 }, { "serialize", "0f 01 e8", 14 },
        { "rdpkru", "0f 01 ee", 14 }, { "wrpkru", "0f 01 ef", 14 }, { "enclu", "0f 01 d7", 14 }, { "encls", "0f 01 cf", 14 },
        { "clgi", "0f 01 dd", 99 }, { "stgi", "0f 01 dc", 99 }, { "vmrun", "0f 01 d8", 99 }, { "vmmcall", "0f 01 d9", 99 },
        { "vmload", "0f 01 da", 99 }, { "vmsave", "0f 01 db", 99 }, { "skinit", "0f 01 de", 99 },
        { "clzero", "0f 01 fc", 99 }, { "monitorx", "0f 01 fa", 99 }, { "mwaitx", "0f 01 fb", 99 },
        { "wbnoinvd", "f3 0f 09", 99 }, { "endbr32", "f3 0f 1e fb", 99 }, { "endbr64", "f3 0f 1e fa", 99 },
        { "pcommit", "66 0f ae f8", 99 }, { "setssbsy", "f3 0f 01 e8", 99 }, { "saveprevssp", "f3 0f 01 ea", 99 },
        { "clui", "f3 0f 01 ee", 99 }, { "stui", "f3 0f 01 ef", 99 }, { "testui", "f3 0f 01 ed", 99 }, { "uiret", "f3 0f 01 ec", 99 },
        { "smint", "0f 38", 3 }, { "rdshr", "0f 36", 3 }, { "wrshr", "0f 37", 3 },
        { "setalc", "d6", 0 },
        { "vmfunc", "0f 01 d4", 99 }, { "pvalidate", "f2 0f 01 ff", 99 }, { "rmpadjust", "f3 0f 01 fe", 99 }, { "vmgexit", "f2 0f 01 c1", 99 },
        { "retnw", "o16 c3", 0 }, { "retfw", "o16 =cb", 0 }, { "retfd", "o32 =cb", 0 },
        { "int01", "f1", 3 }, { "int03", "cc", 0 }, { "smi", "f1", 3 },
        { "cpu_read", "0f 3d", 5 }, { "cpu_write", "0f 3c", 5 }, { "dmint", "0f 39", 6 }, { "rdm", "0f 3a", 6 },
        { "xstore", "0f a7 c0", 5 }, { "xcryptecb", "f3 0f a7 c8", 5 }, { "xcryptcbc", "f3 0f a7 d0", 5 }, { "xcryptctr", "f3 0f a7 d8", 5 },
        { "xcryptcfb", "f3 0f a7 e0", 5 }, { "xcryptofb", "f3 0f a7 e8", 5 }, { "montmul", "f3 0f a6 c0", 5 },
        { "clac", "0f 01 ca", 99 }, { "stac", "0f 01 =cb", 99 }, { "enclv", "0f 01 c0", 99 }, { "pconfig", "0f 01 c5", 99 },
        { "xresldtrk", "f2 0f 01 e9", 99 }, { "xsusldtrk", "f2 0f 01 e8", 99 },
        { "xsha1", "f3 0f a6 c8", 5 }, { "xsha256", "f3 0f a6 d0", 5 },
        { NULL, NULL, 0 }
    };
    for (int i = 0; z[i].n; i++) {
        tab_lv(z[i].lv);
        T(z[i].n, "", z[i].e);
    }
    tab_lv(0);
    T("syscall", "", "only64 0f 05");
    T("sysretq", "", "only64 o64 0f 07");
    T("sysretl", "", "only64 0f 07");
    tab_lv(14);
    tab_lv(15);
    T("bndmk", "bnd,m", "f3 0f 1b /r");
    T("bndcl", "bnd,m", "f3 0f 1a /r");
    T("bndcl", "bnd,rr32", "f3 0f 1a /r");
    T("bndcu", "bnd,m", "f2 0f 1a /r");
    T("bndcu", "bnd,rr32", "f2 0f 1a /r");
    T("bndcn", "bnd,m", "f2 0f 1b /r");
    T("bndcn", "bnd,rr32", "f2 0f 1b /r");
    T("bndmov", "bnd,bndr", "66 0f 1a /r");
    T("bndmov", "bnd,m", "66 0f 1a /r");
    T("bndmov", "bndr,bnd", "66 0f 1b /r");
    T("bndmov", "m,bnd", "66 0f 1b /r");
    T("bndldx", "bnd,m", "0f 1a /r");
    T("bndstx", "m,bnd", "0f 1b /r");
    tab_lv(14);
    T("xbegin", "rel", "c7 f8 cz");
    T("xabort", "imm8", "c6 f8 ib");
    T("invpcid", "r32,m128", "66 0f 38 82 /r");
    T("clflushopt", "m8", "66 0f ae /7");
    T("clwb", "m8", "66 0f ae /6");
    T("xsavec", "m", "0f c7 /4");
    T("xsaves", "m", "0f c7 /5");
    T("xrstors", "m", "0f c7 /3");
    T("prefetchwt1", "m8", "0f 0d /2");
    T("cldemote", "m8", "0f 1c /0");
    T("movdiri", "m32,r32", "0f 38 f9 /r");
    T("movdir64b", "r32,m", "no64 66 0f 38 f8 /r");
    T("movdir64b", "r32,m", "only64 a32 66 0f 38 f8 /r");
    T("enqcmd", "r32,m512", "no64 a16 f2 0f 38 f8 /r");
    T("enqcmds", "r32,m512", "no64 a16 f3 0f 38 f8 /r");
    T("enqcmd", "r32,m512", "only64 a32 f2 0f 38 f8 /r");
    T("enqcmds", "r32,m512", "only64 a32 f3 0f 38 f8 /r");
    T("ptwrite", "rm32", "o32 f3 0f ae /4");
    T("incsspd", "rr32", "o32 f3 0f ae /5");
    T("rdsspd", "rr32", "o32 f3 0f 1e /1");
    T("wrssd", "m32,r32", "o32 0f 38 f6 /r");
    T("wrussd", "m32,r32", "o32 66 0f 38 f5 /r");
    T("clrssbsy", "m64", "f3 0f ae /6");
    T("rstorssp", "m64", "f3 0f 01 /5");
    T("tpause", "rr32", "66 0f ae /6");
    T("umwait", "rr32", "f2 0f ae /6");
    T("umonitor", "rr32", "a32 f3 0f ae /6");
    T("senduipi", "rr32", "f3 0f c7 /6");
    tab_lv(12);
    T("vmptrld", "m64", "0f c7 /6");
    T("vmptrst", "m64", "0f c7 /7");
    T("rdpid", "rr32", "no64 f3 0f c7 /7");
    T("andn", "r64,r64,rm64", "only64 xv:V:0:0:2:1:x f2 /r rl:rvm");
    T("bzhi", "r64,rm64,r64", "only64 xv:V:0:0:2:1:x f5 /r rl:rmv");
    T("mulx", "r64,r64,rm64", "only64 xv:V:0:3:2:1:x f6 /r rl:rvm");
    T("pdep", "r64,r64,rm64", "only64 xv:V:0:3:2:1:x f5 /r rl:rvm");
    T("pext", "r64,r64,rm64", "only64 xv:V:0:2:2:1:x f5 /r rl:rvm");
    T("sarx", "r64,rm64,r64", "only64 xv:V:0:2:2:1:x f7 /r rl:rmv");
    T("shlx", "r64,rm64,r64", "only64 xv:V:0:1:2:1:x f7 /r rl:rmv");
    T("shrx", "r64,rm64,r64", "only64 xv:V:0:3:2:1:x f7 /r rl:rmv");
    T("bextr", "r64,rm64,r64", "only64 xv:V:0:0:2:1:x f7 /r rl:rmv");
    T("bextr", "r64,rm64,imm32", "only64 xv:X:0:0:10:1:x 10 /r id rl:rmi");
    T("rorx", "r64,rm64,imm8", "only64 xv:V:0:3:3:1:x f0 /r ib rl:rmi");
    T("blsr", "r64,rm64", "only64 xv:V:0:0:2:1:x f3 /1 rl:vm");
    T("blsmsk", "r64,rm64", "only64 xv:V:0:0:2:1:x f3 /2 rl:vm");
    T("blsi", "r64,rm64", "only64 xv:V:0:0:2:1:x f3 /3 rl:vm");
    T("blcfill", "r64,rm64", "only64 xv:X:0:0:9:1:x 01 /1 rl:vm");
    T("blci", "r64,rm64", "only64 xv:X:0:0:9:1:x 02 /6 rl:vm");
    T("blcic", "r64,rm64", "only64 xv:X:0:0:9:1:x 01 /5 rl:vm");
    T("blcmsk", "r64,rm64", "only64 xv:X:0:0:9:1:x 02 /1 rl:vm");
    T("blcs", "r64,rm64", "only64 xv:X:0:0:9:1:x 01 /3 rl:vm");
    T("blsfill", "r64,rm64", "only64 xv:X:0:0:9:1:x 01 /2 rl:vm");
    T("blsic", "r64,rm64", "only64 xv:X:0:0:9:1:x 01 /6 rl:vm");
    T("t1mskc", "r64,rm64", "only64 xv:X:0:0:9:1:x 01 /7 rl:vm");
    T("tzmsk", "r64,rm64", "only64 xv:X:0:0:9:1:x 01 /4 rl:vm");
    T("llwpcb", "r64", "only64 xv:X:0:0:9:1:x 12 /0 rl:m");
    T("slwpcb", "r64", "only64 xv:X:0:0:9:1:x 12 /1 rl:m");
    T("lwpins", "r64,rm32,imm32", "only64 xv:X:0:0:10:1:x 12 /0 id rl:vmi");
    T("lwpval", "r64,rm32,imm32", "only64 xv:X:0:0:10:1:x 12 /1 id rl:vmi");
    T("kmovq", "vk,r64", "only64 xv:V:0:3:1:1:x 92 /r rl:rm");
    T("kmovq", "r64,vk", "only64 xv:V:0:3:1:1:x 93 /r rl:rm");
    T("vcvtsd2si", "r64,vx8", "only64 xv:V:0:3:1:1:x 2d /r rl:rm");
    T("vcvttsd2si", "r64,vx8", "only64 xv:V:0:3:1:1:x 2c /r rl:rm");
    T("vcvtss2si", "r64,vx4", "only64 xv:V:0:2:1:1:x 2d /r rl:rm");
    T("vcvttss2si", "r64,vx4", "only64 xv:V:0:2:1:1:x 2c /r rl:rm");
    T("vcvtsd2si", "r64,vx8|er", "only64 xv:E:0:3:1:1:t1f64 2d /r rl:rm");
    T("vcvtss2si", "r64,vx4|er", "only64 xv:E:0:2:1:1:t1f32 2d /r rl:rm");
    T("vcvttsd2si", "r64,vx8|sae", "only64 xv:E:0:3:1:1:t1f64 2c /r rl:rm");
    T("vcvttss2si", "r64,vx4|sae", "only64 xv:E:0:2:1:1:t1f32 2c /r rl:rm");
    T("vcvtsd2usi", "r64,vx8|er", "only64 xv:E:0:3:1:1:t1f64 79 /r rl:rm");
    T("vcvtss2usi", "r64,vx4|er", "only64 xv:E:0:2:1:1:t1f32 79 /r rl:rm");
    T("vcvttsd2usi", "r64,vx8|sae", "only64 xv:E:0:3:1:1:t1f64 78 /r rl:rm");
    T("vcvttss2usi", "r64,vx4|sae", "only64 xv:E:0:2:1:1:t1f32 78 /r rl:rm");
    T("vcvtsh2si", "r64,vx2|er", "only64 xv:E:0:2:5:1:t1s 2d /r rl:rm");
    T("vcvtsh2usi", "r64,vx2|er", "only64 xv:E:0:2:5:1:t1s 79 /r rl:rm");
    T("vcvttsh2si", "r64,vx2|sae", "only64 xv:E:0:2:5:1:t1s 2c /r rl:rm");
    T("vcvttsh2usi", "r64,vx2|sae", "only64 xv:E:0:2:5:1:t1s 78 /r rl:rm");
    T("vcvtsi2sd", "vx,vx*,rm64", "only64 xv:V:0:3:1:1:x 2a /r rl:rvm");
    T("vcvtsi2sd", "vx,vx|er,r64", "only64 xv:E:0:3:1:1:t1s 2a /r rl:rvm");
    T("vcvtsi2ss", "vx,vx*,rm64", "only64 xv:V:0:2:1:1:x 2a /r rl:rvm");
    T("vcvtsi2ss", "vx,vx|er,r64", "only64 xv:E:0:2:1:1:t1s 2a /r rl:rvm");
    T("vcvtusi2sd", "vx,vx|er,r64", "only64 xv:E:0:3:1:1:t1s 7b /r rl:rvm");
    T("vcvtusi2ss", "vx,vx|er,r64", "only64 xv:E:0:2:1:1:t1s 7b /r rl:rvm");
    T("vcvtsi2sh", "vx,vx|er,r64", "only64 xv:E:0:2:5:1:t1s 2a /r rl:rvm");
    T("vcvtusi2sh", "vx,vx|er,r64", "only64 xv:E:0:2:5:1:t1s 7b /r rl:rvm");
    T("vmovq", "vx,r64", "only64 xv:V:0:1:1:1:x 6e /r rl:rm");
    T("vmovq", "r64,vx", "only64 xv:V:0:1:1:1:x 7e /r rl:mr");
    T("vmovq", "vx,r64", "only64 xv:E:0:1:1:1:x 6e /r rl:rm");
    T("vmovq", "r64,vx", "only64 xv:E:0:1:1:1:x 7e /r rl:mr");
    T("vmovmskps", "r64,vx", "only64 xv:V:0:0:1:0:x 50 /r rl:rm");
    T("vmovmskps", "r64,vy", "only64 xv:V:1:0:1:0:x 50 /r rl:rm");
    T("vmovmskpd", "r64,vx", "only64 xv:V:0:1:1:0:x 50 /r rl:rm");
    T("vmovmskpd", "r64,vy", "only64 xv:V:1:1:1:0:x 50 /r rl:rm");
    T("vpmovmskb", "r64,vx", "only64 xv:V:0:1:1:0:x d7 /r rl:rm");
    T("vpmovmskb", "r64,vy", "only64 xv:V:1:1:1:0:x d7 /r rl:rm");
    T("vpbroadcastb", "vx|mask|z,r64", "only64 xv:E:0:1:2:0:x 7a /r rl:rm");
    T("vpbroadcastb", "vy|mask|z,r64", "only64 xv:E:1:1:2:0:x 7a /r rl:rm");
    T("vpbroadcastb", "vz|mask|z,r64", "only64 xv:E:2:1:2:0:x 7a /r rl:rm");
    T("vpbroadcastw", "vx|mask|z,r64", "only64 xv:E:0:1:2:0:x 7b /r rl:rm");
    T("vpbroadcastw", "vy|mask|z,r64", "only64 xv:E:1:1:2:0:x 7b /r rl:rm");
    T("vpbroadcastw", "vz|mask|z,r64", "only64 xv:E:2:1:2:0:x 7b /r rl:rm");
    T("vpbroadcastq", "vx|mask|z,r64", "only64 xv:E:0:1:2:1:x 7c /r rl:rm");
    T("vpbroadcastq", "vy|mask|z,r64", "only64 xv:E:1:1:2:1:x 7c /r rl:rm");
    T("vpbroadcastq", "vz|mask|z,r64", "only64 xv:E:2:1:2:1:x 7c /r rl:rm");
    T("aadd", "m32,r32", "0f 38 fc /r");
    T("aadd", "m64,r64", "only64 o64 0f 38 fc /r");
    T("aand", "m32,r32", "66 0f 38 fc /r");
    T("aand", "m64,r64", "only64 o64 66 0f 38 fc /r");
    T("axor", "m32,r32", "f3 0f 38 fc /r");
    T("axor", "m64,r64", "only64 o64 f3 0f 38 fc /r");
    T("hreset", "imm8", "f3 0f 3a f0 c0 ib");
    T("prefetchit0", "m8", "0f 18 /7");
    T("prefetchit1", "m8", "0f 18 /6");
    T("retnw", "imm16", "o16 c2 iw");
    T("retfw", "imm16", "o16 ca iw");
    T("retfd", "imm16", "o32 ca iw");
    T("ldtilecfg", "m512", "only64 xv:V:0:0:2:0:x 49 /0 rl:m");
    T("sttilecfg", "m512", "only64 xv:V:0:1:2:0:x 49 /0 rl:m");
    T("tilezero", "tmm", "only64 regonly xv:V:0:3:2:0:x 49 /r rl:r");
    T("tilerelease", "", "only64 xv:V:0:0:2:0:x 49 c0");
    T("tileloadd", "tmm,m", "only64 sib xv:V:0:3:2:0:x 4b /r rl:rm");
    T("tileloaddt1", "tmm,m", "only64 sib xv:V:0:1:2:0:x 4b /r rl:rm");
    T("tilestored", "m,tmm", "only64 sib xv:V:0:2:2:0:x 4b /r rl:mr");
    T("tdpbssd", "tmm,tmm,tmm", "only64 xv:V:0:3:2:0:x 5e /r rl:rmv");
    T("tdpbsud", "tmm,tmm,tmm", "only64 xv:V:0:2:2:0:x 5e /r rl:rmv");
    T("tdpbusd", "tmm,tmm,tmm", "only64 xv:V:0:1:2:0:x 5e /r rl:rmv");
    T("tdpbuud", "tmm,tmm,tmm", "only64 xv:V:0:0:2:0:x 5e /r rl:rmv");
    T("tdpbf16ps", "tmm,tmm,tmm", "only64 xv:V:0:2:2:0:x 5c /r rl:rmv");
    T("vpextrw", "r64,vx,imm8", "only64 xv:V:0:1:1:0:x c5 /r ib rl:rmi");
    T("vpextrw", "r64,vx,imm8", "only64 xv:E:0:1:1:0:x c5 /r ib rl:rmi");
    T("vpextrb", "r64,vx,imm8", "only64 xv:V:0:1:3:0:x 14 /r ib rl:mri");
    T("vpextrb", "r64,vx,imm8", "only64 xv:E:0:1:3:0:t1s8 14 /r ib rl:mri");
    T("vpextrd", "r64,vx,imm8", "only64 xv:V:0:1:3:0:x 16 /r ib rl:mri");
    T("vpextrd", "r64,vx,imm8", "only64 xv:E:0:1:3:0:t1s 16 /r ib rl:mri");
    T("vextractps", "r64,vx,imm8", "only64 xv:V:0:1:3:0:x 17 /r ib rl:mri");
    T("vextractps", "r64,vx,imm8", "only64 xv:E:0:1:3:0:t1s 17 /r ib rl:mri");
    T("vpextrq", "rm64,vx,imm8", "only64 xv:V:0:1:3:1:x 16 /r ib rl:mri");
    T("vpextrq", "rm64,vx,imm8", "only64 xv:E:0:1:3:1:t1s 16 /r ib rl:mri");
    T("vpinsrq", "vx,vx*,rm64,imm8", "only64 xv:V:0:1:3:1:x 22 /r ib rl:rvmi");
    T("vpinsrq", "vx,vx*,rm64,imm8", "only64 xv:E:0:1:3:1:t1s 22 /r ib rl:rvmi");
    T("movntss", "m32,xmm", "f3 0f 2b /r");
    T("movntsd", "m64,xmm", "f2 0f 2b /r");
    T("prefetch", "m", "0f 0d /0");
    T("prefetchw", "m", "0f 0d /1");
    T("vpdpbssd", "vx,vx*,vx16", "xv:V:0:3:2:0:x 50 /r rl:rvm");
    T("vpdpbssd", "vy,vy*,vy32", "xv:V:1:3:2:0:x 50 /r rl:rvm");
    T("vpdpbssds", "vx,vx*,vx16", "xv:V:0:3:2:0:x 51 /r rl:rvm");
    T("vpdpbssds", "vy,vy*,vy32", "xv:V:1:3:2:0:x 51 /r rl:rvm");
    T("vpdpbsud", "vx,vx*,vx16", "xv:V:0:2:2:0:x 50 /r rl:rvm");
    T("vpdpbsud", "vy,vy*,vy32", "xv:V:1:2:2:0:x 50 /r rl:rvm");
    T("vpdpbsuds", "vx,vx*,vx16", "xv:V:0:2:2:0:x 51 /r rl:rvm");
    T("vpdpbsuds", "vy,vy*,vy32", "xv:V:1:2:2:0:x 51 /r rl:rvm");
    T("vpdpbuud", "vx,vx*,vx16", "xv:V:0:0:2:0:x 50 /r rl:rvm");
    T("vpdpbuud", "vy,vy*,vy32", "xv:V:1:0:2:0:x 50 /r rl:rvm");
    T("vpdpbuuds", "vx,vx*,vx16", "xv:V:0:0:2:0:x 51 /r rl:rvm");
    T("vpdpbuuds", "vy,vy*,vy32", "xv:V:1:0:2:0:x 51 /r rl:rvm");
    T("vmclear", "m64", "66 0f c7 /6");
    T("vmxon", "m64", "f3 0f c7 /6");
    T("vmread", "rm32,r32", "0f 78 /r");
    T("vmwrite", "r32,rm32", "0f 79 /r");
    T("invept", "r32,m128", "66 0f 38 80 /r");
    T("invvpid", "r32,m128", "66 0f 38 81 /r");
    tab_lv(99);
    T("invlpga", "", "0f 01 df");
    tab_lv(6);
    T("ud1", "r32,rm32", "o32 0f b9 /r");
    T("ud1", "r16,rm16", "o16 0f b9 /r");
    T("ud0", "r32,rm32", "o32 0f ff /r");
    T("ud0", "r16,rm16", "o16 0f ff /r");
    tab_lv(4);
    T("cmpxchg486", "rm8,r8", "0f a6 /r");
    T("umov", "rm8,r8", "0f 10 /r");
    T("umov", "rm16,r16", "o16 0f 11 /r");
    T("umov", "rm32,r32", "o32 0f 11 /r");
    T("umov", "r8,rm8", "0f 12 /r");
    T("umov", "r16,rm16", "o16 0f 13 /r");
    T("umov", "r32,rm32", "o32 0f 13 /r");
    tab_lv(0);
}
