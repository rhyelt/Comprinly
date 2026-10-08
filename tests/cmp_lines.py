import subprocess, sys, os
CLY = os.environ.get('CLY', '/home/claude/componly/cly')
f = sys.argv[1]
W = '/tmp/cmplines'
os.makedirs(W, exist_ok=True)
lines = open(f).read().split('\n')
hdr = []
body = []
for l in lines:
    if l.strip().lower().startswith('bits ') and not body: hdr.append(l)
    elif l.strip(): body.append(l)
ok = bad = skip = 0
for l in body:
    src = '\n'.join(hdr + [l]) + '\n'
    open(W + '/a.asm', 'w').write(src)
    r = subprocess.run(['nasm', '-f', 'bin', W + '/a.asm', '-o', W + '/n.bin'], capture_output=True, text=True)
    if r.returncode != 0:
        skip += 1
        continue
    r2 = subprocess.run([CLY, 'linux', W + '/a.asm', '-flat', '-q', '-out', W + '/c.bin'], capture_output=True, text=True)
    if r2.returncode != 0:
        bad += 1
        print('CLY REJECTS:', l, '|', r2.stderr.strip().split('\n')[0])
        continue
    a = open(W + '/n.bin', 'rb').read()
    b = open(W + '/c.bin', 'rb').read()
    if a == b: ok += 1
    else:
        bad += 1
        print('DIFF:', l, '| nasm', a.hex(), '| cly', b.hex())
print('ok', ok, 'bad', bad, 'nasm-rejected', skip)
