import re
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "data/insns.dat"
DST = sys.argv[2] if len(sys.argv) > 2 else "src/insn_gen64.c"

LV = {"386": 3, "486": 4, "PENT": 5, "P6": 6, "KATMAI": 7, "SSE": 7, "WILLAMETTE": 8, "SSE2": 8, "PRESCOTT": 9, "SSE3": 9,
      "SSSE3": 10, "SSE41": 12, "SSE42": 12, "NEHALEM": 12, "WESTMERE": 12}
DROP = {"hle", "hlexr", "hlenl", "odf", "adf", "nof3", "norep", "mustrep", "repe", "wait", "resb", "jlen", "jmp8", "jcc8",
        "nohi", "norexb", "np", "o16p", "o32p", "norexw", "rex.l", "nof2"}


def conv_op(tok, role, ctx):
    tok = tok.strip()
    base = tok.split("|")[0]
    mods = tok.split("|")[1:]
    rr = role in ("m",)
    simple = {
        "reg_al": "al", "reg_cl": "cl", "reg_dx": "dx", "reg_ax": "ax", "reg_eax": "eax", "reg_rax": "rax",
        "reg_ecx": "ecx", "reg_rcx": "rcx", "reg_edx": "edx", "reg_cx": "cx",
        "reg_sreg": "sreg", "reg_creg": "cr", "reg_dreg": "dr", "reg_treg": "tr", "reg_fs": "fs", "reg_gs": "gs",
        "unity": "one", "xmm0": "xmm0", "fpu0": "st0", "mem512": "m512", "mem80": "m80", "mem128": "m128",
        "mem64": "m64", "mem32": "m32", "mem16": "m16", "mem8": "m8", "rm8": "rm8", "rm16": "rm16", "rm32": "rm32",
        "rm64": "rm64", "imm8": ctx["i8"], "imm16": "imm16", "imm32": "imm32", "imm64": "imm64",
        "sdword": "sd", "udword": "ud", "sbytedword": "sb64" if ctx["o64"] else "sb32", "sbytedword64": "sb64",
        "sbytedword32": "sb32", "sbyteword": "sb16", "sbyteword16": "sb16", "fpureg": "sti",
        "reg32na": "r32", "mem_offs": None, "xmmrm128": "xm128", "xmmrm64": "xm64", "xmmrm32": "xm32", "xmmrm16": "xm16",
        "mmxrm64": "mm64", "mmxrm": "mm64", "bndreg": "bnd" if not rr else "bndr",
    }
    if "to" in mods:
        return None
    if base in ("reg8", "reg16", "reg32", "reg64"):
        n = base[3:]
        return ("rr" if rr else "r") + n
    if base == "xmmreg":
        return "xmmr" if rr else "xmm"
    if base == "mmxreg":
        return "mmr" if rr else "mm"
    if base == "xmmrm":
        mn = ctx["mn"]
        if mn.endswith("ss") or mn in ("cvtss2si", "cvttss2si"):
            return "xm32"
        if mn.endswith("sd") or mn in ("cvtsd2si", "cvttsd2si"):
            return "xm64"
        return "xm128"
    if base == "mem":
        if "far" in mods:
            return "mfar"
        return "m"
    if base in ("mem16", "mem32", "mem64") and "far" in mods:
        return "mfar"
    if base == "mem" and "near" in mods:
        return "m"
    if base == "rm64" and "near" in mods:
        return "rm64"
    if base == "imm":
        if "near" in mods or "short" in mods:
            return "rel" if "short" not in mods else "rel8"
        if ctx["rel"]:
            return "rel"
        if ctx["rel8"]:
            return "rel8"
        if ctx["iq"]:
            return "imm64"
        if ctx["o64"] and ctx["ids"]:
            return "sd"
        if ctx["ib"] and not ctx["iw"] and not ctx["id"]:
            return ctx["i8"]
        if ctx["iw"] and not ctx["id"]:
            return "imm16"
        if ctx["id"]:
            return "imm32"
        return None
    if base == "imm64":
        if "near" in mods:
            return "rel"
        if ctx["id"] and not ctx["iq"]:
            return "sd"
        return "imm64"
    if base == "imm32" and "near" in mods:
        return "rel"
    if base == "mem_offs":
        return ctx["moffs"]
    return simple.get(base)


def parse_enc(enc):
    enc = enc.strip()
    m = re.match(r"^([a-z0-9+-]*):\s*(.*)$", enc)
    if not m:
        return "", enc.split()
    return m.group(1), m.group(2).split()


def convert(mn, ops, enc, flags):
    fs = flags.split(",")
    if "NOLONG" in fs:
        return None
    if not ("LONG" in fs or "X86_64" in fs):
        return None
    if re.search(r"vex|xop", enc):
        return None
    if set(fs) & {"AMXTILE", "AMXINT8", "AMXBF16", "SM2", "IA64", "UNDOC", "OBSOLETE", "NEVER", "FUTURE"} and "LONG" not in fs:
        return None
    order, toks = parse_enc(enc)
    if mn.upper() == "MOV" and "reg_sreg" in ops and "rm64" in ops:
        return None
    oplist = [] if ops == "void" else ops.split(",")
    ctx = {"mn": mn.lower(), "o64": "o64" in toks, "ids": "id,s" in toks, "iq": "iq" in toks, "ib": "ib" in toks or "ib,s" in toks or "ib,u" in toks,
           "iw": "iw" in toks, "id": "id" in toks or "id,s" in toks, "rel": "rel" in toks, "rel8": "rel8" in toks, "moffs": None,
           "i8": "ibs" if "ib,s" in toks else ("ibu" if "ib,u" in toks else "imm8")}
    if "iwdq" in toks:
        if any(o.endswith("reg_rax") or o == "reg_rax" for o in oplist):
            ctx["moffs"] = "mo64"
        else:
            return None
    ro = order.replace("+", "")
    roles = list(ro)
    if len(roles) != len(oplist):
        roles = roles + ["-"] * (len(oplist) - len(roles))
    conv = []
    for i, o in enumerate(oplist):
        r = roles[i] if i < len(roles) else "-"
        c = conv_op(o, r, ctx)
        if c is None:
            return None
        conv.append(c)
    out = []
    plusr = any(re.fullmatch(r"[0-9a-f]{2}\+r", t) for t in toks)
    for t in toks:
        if t in DROP:
            continue
        if t in ("o64", "o64nw", "o16", "o32", "a16", "a32", "a64", "odf", "adf"):
            out.append(t)
        elif t in ("f3i", "f2i"):
            out.append(t[:2])
        elif t in ("cb", "cd"):
            out.append("=" + t)
        elif re.fullmatch(r"[0-9a-f]{2}(\+r)?", t):
            out.append(t)
        elif re.fullmatch(r"[0-9a-f]{2}\+c", t):
            return None
        elif re.fullmatch(r"/[0-7r]", t):
            out.append(t)
        elif t in ("ib", "ib,s", "ib,u"):
            out.append("ib")
        elif t in ("id", "id,s", "id,u"):
            out.append("id")
        elif t == "iw":
            out.append("iw")
        elif t == "iq":
            out.append("iq")
        elif t == "iwdq":
            out.append("om")
        elif t == "rel8":
            out.append("cb")
        elif t == "rel":
            out.append("cz")
        elif t == "seg":
            return None
        else:
            return None
    if "o64nw" in out and "o64" in out:
        return None
    if any("far" in o for o in oplist):
        return None
    if mn.upper() in ("PEXTRW", "PINSRW") and "o64" not in out:
        out.insert(0, "o64")
    enc2 = " ".join(["only64"] + out)
    level = 0
    for f in fs:
        level = max(level, LV.get(f, 0))
    return mn.lower(), ",".join(conv), enc2, level


def main():
    seen = set()
    n = 0
    skipped = 0
    lines = []
    for line in open(SRC):
        if line.startswith(";") or not line.strip():
            continue
        m = re.match(r"^(\S+)\s+(\S+)\s+\[([^\]]*)\]\s*(\S*)", line)
        if not m:
            continue
        mn, ops, enc, flags = m.groups()
        if "Jcc" in mn or "SETcc" in mn or "CMOVcc" in mn:
            continue
        r = convert(mn, ops, enc, flags)
        if r is None:
            if ("LONG" in flags.split(",") or "X86_64" in flags.split(",")) and "NOLONG" not in flags.split(",") and not re.search(r"vex|xop", enc):
                skipped += 1
                sys.stderr.write("skip: %s %s [%s] %s\n" % (mn, ops, enc, flags))
            continue
        if r in seen:
            continue
        seen.add(r)
        lines.append(r)
        n += 1
    with open(DST, "w") as f:
        f.write('#include "cly.h"\n#include "insn.h"\n\n')
        f.write("void gen64_tab(void)\n{\n")
        cur = -1
        for mn, ops, enc, lv in lines:
            if lv != cur:
                f.write("    tab_lv(%d);\n" % lv)
                cur = lv
            f.write('    T("%s", "%s", "%s");\n' % (mn, ops, enc))
        f.write("}\n")
    sys.stderr.write("generated %d, skipped %d\n" % (n, skipped))


main()
