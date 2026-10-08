import sys
from unicorn import *
from unicorn.x86_const import *
img=open(sys.argv[1],'rb').read()
stdin=sys.argv[2].encode() if len(sys.argv)>2 else b''
mu=Uc(UC_ARCH_X86,UC_MODE_16)
mu.mem_map(0,0x2000000)
mu.mem_write(0x7C00,img[:512])
out=bytearray()
def intr(uc,intno,ud):
    ah=(uc.reg_read(UC_X86_REG_AX)>>8)&0xff
    fl=uc.reg_read(UC_X86_REG_EFLAGS)
    if intno==0x13:
        if ah==8:
            uc.reg_write(UC_X86_REG_EFLAGS,fl|1)
        elif ah==0x42:
            si=uc.reg_read(UC_X86_REG_SI)
            dap=bytes(uc.mem_read(si,16))
            cnt=int.from_bytes(dap[2:4],'little'); off=int.from_bytes(dap[4:6],'little'); seg=int.from_bytes(dap[6:8],'little'); lba=int.from_bytes(dap[8:16],'little')
            uc.mem_write(seg*16+off,img[lba*512:(lba+cnt)*512].ljust(cnt*512,b'\0'))
            uc.reg_write(UC_X86_REG_EFLAGS,fl&~1)
        else:
            uc.reg_write(UC_X86_REG_EFLAGS,fl&~1)
    else:
        uc.reg_write(UC_X86_REG_EFLAGS,fl&~1)
mu.hook_add(UC_HOOK_INTR,intr)
sin=bytearray(stdin)
def hin(uc,port,size,ud):
    if port==0x3FD: return 0x61 if sin else 0x60
    if port==0x3F8: return sin.pop(0) if sin else 0
    if port==0x64: return 0
    return 0
def hout(uc,port,size,val,ud):
    if port==0xE9: out.append(val&0xff)
    if port in (0x604,0xB004): uc.emu_stop()
    if port==0xF4: ud.append(val)
codes=[]
mu.hook_add(UC_HOOK_INSN,hin,codes,1,0,UC_X86_INS_IN)
mu.hook_add(UC_HOOK_INSN,hout,codes,1,0,UC_X86_INS_OUT)
mu.reg_write(UC_X86_REG_DL,0x80)
mu.reg_write(UC_X86_REG_CS,0)
try:
    mu.emu_start(0x7C00,0,count=int(sys.argv[3]) if len(sys.argv)>3 else 3000000)
except UcError as e:
    print("uc error",e,hex(mu.reg_read(UC_X86_REG_EIP)))
print("E9:",bytes(out))
vga=bytes(mu.mem_read(0xB8000,160))
print("VGA:",bytes(vga[0::2]).rstrip(b' '))
print("exit codes",codes, "eip",hex(mu.reg_read(UC_X86_REG_EIP)))
