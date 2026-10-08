#include "cly.h"
#include "insn.h"

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
    T("movdir64b", "r32,m", "66 0f 38 f8 /r");
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
