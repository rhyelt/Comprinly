import re,os
here=os.path.dirname(os.path.abspath(__file__))
src=open(here+"/libc.tpl").read()+open(here+"/libc_fmt.tpl").read()

def expand(bits):
    w=8 if bits==64 else 4
    R={'a':'rax','b':'rbx','c':'rcx','d':'rdx','si':'rsi','di':'rdi','bp':'rbp','sp':'rsp'}
    if bits==32: R={k:'e'+v[1:] for k,v in R.items()}
    frame=(14*w+256+15)&~15
    tok={'W':str(w),'P':'qword' if bits==64 else 'dword','D':'dq' if bits==64 else 'dd',
         'H':str(w*2),'G':str(w*2),'FRAME':str(frame),'FR2':str(16*w),
         'buf':R['bp']+'-(%d*14+192)'%w,'scr':R['bp']+'-(%d*14+208)'%w,
         'FLIM':'9.2e18' if bits==64 else '2.1e9'}
    for k,v in R.items(): tok[k]=v
    for i in range(1,6): tok['T%d'%i]='[%s-(%d*14+%d)]'%(R['bp'],w,208+8*i)
    for i in range(1,15): tok['L%d'%i]='[%s-%d*%d]'%(R['bp'],w,i)
    vaoff=128 if bits==64 else 12
    for i in (1,2,3): tok['X%d'%i]='[%s+%d]'%(R['bp'],vaoff+(i-1)*w)
    pat=re.compile(r'@([A-Za-z][A-Za-z0-9]*)')
    out=[]
    saves=[]
    for line in src.split('\n'):
        m=re.match(r'^(64|32)\|(.*)$',line)
        if m:
            if int(m.group(1))!=bits: continue
            line=m.group(2)
        s=line.strip()
        if line.startswith(';;@'):
            out.append(line); out.append('section .text'); continue
        if line.startswith(';;>'):
            out.append(line); continue
        mm=re.match(r'^@func (\w+) (\d+)(.*)$',s)
        if mm:
            name,n,sv=mm.group(1),int(mm.group(2)),mm.group(3).split()
            saves=sv
            out.append('__cly_%s:'%name)
            if bits==64:
                for r in sv: out.append('    push %s'%R[r])
            else:
                saves=['b','si','di','bp']
                for r in saves: out.append('    push %s'%R[r])
                regs=['edi','esi','edx','ecx']
                for i in range(n): out.append('    mov %s, [esp+%d]'%(regs[i],20+4*i))
            continue
        if s=='@fret':
            if bits==32:
                out+=['    sub esp, 8','    movsd [esp], xmm0','    fld qword [esp]','    add esp, 8']
            for r in reversed(saves): out.append('    pop %s'%R[r])
            out.append('    ret'); continue
        if s=='@ret':
            for r in reversed(saves): out.append('    pop %s'%R[r])
            out.append('    ret'); continue
        mm=re.match(r'^@call (\w+) (\d+)$',s)
        if mm:
            name,n=mm.group(1),int(mm.group(2))
            if bits==32:
                regs=['edi','esi','edx','ecx']
                for i in reversed(range(n)): out.append('    push %s'%regs[i])
            out.append('    call __cly_%s'%name)
            if bits==32 and n: out.append('    add esp, %d'%(4*n))
            continue
        mm=re.match(r'^@mkva (\d+)$',s)
        if mm:
            nf=int(mm.group(1))
            if bits==64:
                ov=160+8+8*len(saves)
                out+=['    sub rsp, 160','    mov [rsp+24], rsi','    mov [rsp+32], rdx','    mov [rsp+40], rcx','    mov [rsp+48], r8','    mov [rsp+56], r9']
                for i in range(8): out.append('    movsd [rsp+%d], xmm%d'%(64+8*i,i))
                out+=['    mov qword [rsp], %d'%(nf-1),'    mov qword [rsp+8], 0','    lea rax, [rsp+%d]'%ov,'    mov [rsp+16], rax']
            else:
                out+=['    sub esp, 32','    mov dword [esp], 5','    mov dword [esp+4], 8','    lea eax, [esp+%d]'%(32+20+4*nf),'    mov [esp+8], eax']
            continue
        if s=='@endva':
            out.append('    lea %s, [%s+%d]'%(R['sp'],R['bp'],160 if bits==64 else 32)); continue
        if s=='@data': out.append('section .data'); continue
        if s=='@bss': out.append('section .bss'); continue
        line=re.sub(r'@RES (\d+)',lambda m:('resq ' if bits==64 else 'resd ')+m.group(1),line)
        def sub(m):
            k=m.group(1)
            if k in tok: return tok[k]
            raise SystemExit('unknown token @'+k+' in: '+line)
        out.append(pat.sub(sub,line))
    return '\n'.join(out)+'\n'

def cstr(name,text):
    r='const char %s[] =\n'%name
    for l in text.split('\n'):
        if l=='': continue
        r+='    "%s\\n"\n'%l.replace('\\','\\\\').replace('"','\\"')
    return r+';\n'
open(here+'/../src/extras/libc_data.c','w').write('#include "cly.h"\n'+cstr('libc_src64','bits 64\n'+expand(64))+cstr('libc_src32','bits 32\n'+expand(32)))
