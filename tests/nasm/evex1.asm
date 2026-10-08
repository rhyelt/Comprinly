bits 32
start:
vaddps zmm1, zmm2, zmm3
vaddps zmm1{k1}{z}, zmm2, [eax+64]
vaddps zmm1, zmm2, [eax+4]{1to16}
vaddps zmm1, zmm2, zmm3, {rz-sae}
vaddpd ymm17, ymm2, [esp+0x80]
vaddss xmm1{k2}, xmm2, [ebx+ecx*4+4]
vcmpps k1{k2}, zmm2, zmm3, 5
vcmpps k3, zmm2, zmm3, {sae}, 5
vcvtsi2ss xmm1, xmm2, {rn-sae}, eax
vmovaps zmm1{k1}{z}, [label]
vmovaps [eax+0x40]{k1}, zmm1
vmovdqu8 zmm4{k5}, [eax-0x40]
vmovdqa32 zmm1, zmm2
vpaddd zmm1, zmm2, [eax+0x40]
vpbroadcastd zmm1, eax
vpbroadcastb ymm20, ebx
vgatherdps zmm1{k1}, [eax+zmm2*4]
vpscatterdd [eax+zmm2*4+0x10]{k1}, zmm3
vpgatherqd ymm1{k2}, [ebx+zmm5*8]
vpternlogd zmm1, zmm2, zmm3, 0x55
vpermt2d zmm1{k1}{z}, zmm2, [eax]{1to16}
vcvtps2ph [eax+0x20], zmm1, 5
vcompresspd [eax+8]{k1}, zmm1
vexpandps zmm1{k1}{z}, [eax+4]
vrndscaleps zmm1, zmm2, 4
vscalefss xmm1, xmm2, xmm3, {rn-sae}
vfmadd132ps zmm1, zmm2, zmm3
vpdpbusd zmm1, zmm2, [eax]
vpmovzxbd zmm1, [eax+0x10]
vbroadcastf32x4 zmm1, [eax+16]
vbroadcastf64x2 zmm17, [eax+32]
vpcmpeqb k1{k2}, zmm2, [eax+0x40]
vptestmd k1, zmm2, zmm3
vpmovm2b zmm1, k2
vpmovb2m k3, zmm4
kmovw k1, eax
kmovw k1, k2
kmovq k1, [eax]
kmovd eax, k3
kaddw k1, k2, k3
kandq k1, k2, k3
kshiftlw k1, k2, 3
kortestd k1, k2
kunpckbw k1, k2, k3
vucomiss xmm1, xmm2, {sae}
vcvtsd2si eax, xmm1, {rn-sae}
vmovq xmm17, [eax]
vpinsrb xmm17, xmm2, [eax+1], 3
vpextrw [eax+2], xmm17, 3
vaddph zmm1, zmm2, zmm3
vfmaddcph zmm1, zmm2, zmm3
{evex} vaddps xmm1, xmm2, xmm3
{vex3} vaddps xmm1, xmm2, xmm3
vpmadd52luq zmm1, zmm2, zmm3
vaesenc zmm1, zmm2, zmm3
vpclmulqdq zmm1, zmm2, zmm3, 0x11
vgf2p8affineqb zmm1, zmm2, zmm3, 1
bndmk bnd0, [eax]
bndcl bnd1, ecx
bndmov bnd0, bnd1
bndldx bnd0, [eax+ecx]
bndstx [eax], ecx, bnd0
label: dd 0
bnd jz short label
bnd call label
bnd ret
