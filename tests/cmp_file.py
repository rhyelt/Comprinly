import subprocess, re, sys, os
CLY = os.environ.get('CLY', '/home/claude/componly/cly')
f = sys.argv[1]
prune = '--noprune' not in sys.argv
src = open(f).read().split('\n')
W = '/tmp/cmpfile'
os.makedirs(W, exist_ok=True)
keep = list(range(len(src)))
dropped = []
while True:
    open(W + '/n.asm', 'w').write('\n'.join(src[i] for i in keep) + '\n')
    r = subprocess.run(['nasm', '-f', 'bin', '-l', W + '/n.lst', W + '/n.asm', '-o', W + '/n.bin'], capture_output=True, text=True)
    errs = set(int(m.group(1)) - 1 for m in re.finditer(r'n\.asm:(\d+): error', r.stderr))
    if not errs and r.returncode == 0: break
    if not errs or not prune:
        print('nasm failed:', r.stderr[:400]); sys.exit(2)
    for k in errs: dropped.append(src[keep[k]])
    keep = [keep[k] for k in range(len(keep)) if k not in errs]
if dropped: print('dropped (nasm rejects):', len(dropped))
r = subprocess.run([CLY, 'linux', W + '/n.asm', '-flat', '-q', '-out', W + '/c.bin', '-l', W + '/c.lst'], capture_output=True, text=True)
if r.returncode != 0:
    print('CLY FAILED:', r.stderr[:1500]); sys.exit(1)
nb = open(W + '/n.bin', 'rb').read()
cb = open(W + '/c.bin', 'rb').read()
if nb == cb:
    print('ok', len(nb), 'bytes'); sys.exit(0)
print('DIFF sizes', len(nb), len(cb))
lines = open(W + '/n.asm').read().split('\n')
shown = 0
for l in open(W + '/n.lst').read().splitlines():
    m = re.match(r'\s*(\d+)\s+([0-9A-F]{8})\s+([0-9A-F\[\]]+)(-?)\s+(.*)$', l)
    if not m: continue
    off = int(m.group(2), 16)
    h = m.group(3).replace('[', '').replace(']', '')
    n = len(h) // 2
    a = nb[off:off + n]
    b = cb[off:off + n]
    if a != b:
        print('line', m.group(1), repr(lines[int(m.group(1)) - 1]), 'nasm', a.hex(), 'cly', b.hex())
        shown += 1
        if shown >= 8: break
sys.exit(1)
