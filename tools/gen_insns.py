import re
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "data/insns.dat"
DST = sys.argv[2] if len(sys.argv) > 2 else "src/insn_gen.c"

SFLAGS = {"SB", "SW", "SD", "SQ", "SO", "SY", "SZ", "SX"}
SKIP_FLAGS = {"LONG", "AMXTILE", "AMXINT8", "AMXBF16", "SM2", "X64"}
VSIB = {"nohi", "vsibx", "vsiby", "vsibz", "vm32x", "vm32y", "vm32z", "vm64x", "vm64y", "vm64z"}
TUPLES = {"fv", "hv", "fvm", "t1s", "t1s8", "t1s16", "t1f32", "t1f64", "t2", "t4", "t8", "hvm", "qvm", "ovm", "m128", "dup"}
MAPS = {"0f": 1, "0f38": 2, "0f3a": 3, "map5": 5, "map6": 6}
PPS = {"np": 0, "66": 1, "f3": 2, "f2": 3}
CLS = {"xmm": "x", "ymm": "y", "zmm": "z"}
KSZ = {"8": "1", "16": "2", "32": "4", "64": "8"}


def conv_op(tok):
    star = tok.endswith("*")
    if star:
        tok = tok[:-1]
    parts = tok.split("|")
    base = parts[0]
    decs = parts[1:]
    out = None
    m = re.fullmatch(r"(xmm|ymm|zmm)reg", base)
    if m:
        out = "v" + CLS[m.group(1)]
    m = re.fullmatch(r"(xmm|ymm|zmm)rm(\d+)", base)
    if m:
        out = "v" + CLS[m.group(1)] + str(int(m.group(2)) // 8)
    m = re.fullmatch(r"([xyz])mem(32|64)", base)
    if m:
        out = "vs" + m.group(1) + m.group(2)
    if base == "kreg":
        out = "vk"
    m = re.fullmatch(r"krm(8|16|32|64)", base)
    if m:
        out = "vk" + KSZ[m.group(1)]
    m = re.fullmatch(r"mem(8|16|32|64|128|256|512)?", base)
    if m:
        out = "m" + (m.group(1) or "")
    if base in ("reg8", "reg16", "reg32"):
        out = "r" + base[3:]
    if base in ("rm8", "rm16", "rm32"):
        out = base
    if base == "rm64":
        out = "m64"
    if base == "imm8":
        out = "imm8"
    if base == "imm32":
        out = "imm32"
    if base == "reg32":
        out = "r32"
    if out is None:
        return None
    for d in decs:
        if d in ("rs2", "rs4"):
            continue
        if d in ("mask", "z", "er", "sae") or re.fullmatch(r"b(16|32|64)", d):
            out += "|" + d
        else:
            return None
    if star:
        out += "*"
    return out


def parse_enc(enc):
    enc = enc.strip()
    m = re.match(r"^([a-z]*):([a-z0-9]*):?\s*(.*)$", enc)
    if not m:
        order, tup, rest = "", "", enc
    else:
        order, tup, rest = m.groups()
    toks = [t for t in rest.split() if t not in VSIB]
    if not toks:
        return None
    return order, tup, toks


def convert(mn, ops, enc, flags):
    late = "LATEVEX" in flags.split(",")
    if set(flags.split(",")) & SKIP_FLAGS:
        return None
    pe = parse_enc(enc)
    if not pe:
        return None
    order, tup, toks = pe
    if not toks[0].startswith(("vex.", "evex.", "xop.")):
        return None
    vt = toks[0].split(".")
    kind = {"evex": "E", "xop": "X"}.get(vt[0], "V")
    L, pp, mp, W = 0, 0, None, 0
    for p in vt[1:]:
        if kind != "E" and re.fullmatch(r"m\d+", p):
            mp = int(p[1:])
            continue
        if kind != "E" and re.fullmatch(r"p[0-3]", p):
            pp = int(p[1])
            continue
        if p == "lz":
            continue
        if p in ("nds", "dds", "ndd", "lig", "l0", "lz", "128"):
            L = 0 if p != "128" else 0
        elif p in ("l1", "256"):
            L = 1
        elif p == "512":
            L = 2
        elif p in PPS:
            pp = PPS[p]
        elif p in MAPS:
            mp = MAPS[p]
        elif p == "w1":
            W = 1
        elif p in ("w0", "wig"):
            W = 0
        else:
            return None
    if mp is None:
        return None
    oplist = [] if ops == "void" else ops.split(",")
    conv = []
    for o in oplist:
        c = conv_op(o)
        if c is None:
            return None
        conv.append(c)
    if order == "" and conv:
        return None
    roles = order
    if len(roles) == len(conv) + 1 and roles.endswith("i") and not re.search(r"\bi[bd]\b", enc):
        roles = roles[:-1]
    if len(roles) != len(conv):
        return None
    if any(r not in "rmvis" for r in roles):
        return None
    if tup and tup not in TUPLES:
        return None
    opc = []
    modrm = None
    post = []
    ib = None
    is4 = False
    for t in toks[1:]:
        if t in ("vsibx", "vsiby", "vsibz", "vm32x", "vm32y", "vm32z", "vm64x", "vm64y", "vm64z"):
            continue
        if re.fullmatch(r"[0-9a-fA-F]{2}", t):
            if modrm is None:
                opc.append(t.lower())
            else:
                post.append(t.lower())
        elif t == "/r":
            modrm = "/r"
        elif re.fullmatch(r"/[0-7]", t):
            modrm = t
        elif t == "/is4":
            is4 = True
        elif t in ("ib", "ib,u", "ib,s"):
            ib = "ib"
        elif t in ("id", "id,s", "id,u"):
            ib = "id"
        else:
            return None
    if not opc:
        return None
    if ("s" in roles) != is4:
        return None
    if ("i" in roles) != bool(ib):
        return None
    parts = ["xv:%s:%d:%d:%d:%d:%s" % (kind, L, pp, mp, W, tup or "x")]
    parts += opc
    if modrm:
        parts.append(modrm)
    parts += ["p" + x for x in post]
    if is4:
        parts.append("is4")
    if ib:
        parts.append(ib)
    parts.append("rl:" + (roles if roles else "-"))
    if late:
        parts.append("late")
    if set(flags.split(",")) & SFLAGS:
        parts.append("sf")
    return (kind, mn.lower(), ",".join(conv), " ".join(parts))


def main():
    seen = set()
    out = {"V": [], "E": [], "L": [], "X": []}
    skipped = 0
    for line in open(SRC):
        if line.startswith(";") or not line.strip():
            continue
        m = re.match(r"^(\S+)\s+(\S+)\s+\[([^\]]*)\]\s*(\S*)", line)
        if not m:
            continue
        mn, ops, enc, flags = m.groups()
        if "vex." not in enc and "xop." not in enc:
            continue
        r = convert(mn, ops, enc, flags)
        if r is None:
            skipped += 1
            continue
        key = r[1:]
        if key in seen:
            continue
        seen.add(key)
        late = "LATEVEX" in flags.split(",")
        out["L" if late else r[0]].append(r)
    with open(DST, "w") as f:
        f.write('#include "cly.h"\n#include "insn.h"\n\n')
        f.write("void gen_tab(void)\n{\n")
        for k, lv in (("V", 14), ("E", 15), ("L", 14), ("X", 14)):
            f.write("    tab_lv(%d);\n" % lv)
            for _, mn, ops, enc in out[k]:
                f.write('    T("%s", "%s", "%s");\n' % (mn, ops, enc))
        f.write("}\n")
    sys.stderr.write("generated %d vex, %d evex, skipped %d\n" % (len(out["V"]), len(out["E"]), skipped))


main()
