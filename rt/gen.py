import os
import sys
out=["#include \"cly.h\"\n"]
for n in ["linux","win","mac","bare","win64","mac64","bare64"]:
    src=open(os.path.join(os.path.dirname(os.path.abspath(__file__)),"%s.s"%n)).read().split("\n")
    out.append("const char rt_src_%s[] =\n"%n)
    for l in src:
        if l=="" : continue
        out.append('    "%s\\n"\n'%l.replace("\\","\\\\").replace('"','\\"'))
    out.append(";\n")
open(os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","src","runtime","rt_data.c"),"w").write("".join(out))
