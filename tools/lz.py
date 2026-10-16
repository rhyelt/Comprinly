# LZ4 style block compressor used by the generators. src/core/lz.c is the matching decoder.
def compress(data):
    n = len(data)
    out = bytearray()
    head = {}
    chain = [0] * (n + 1)
    i = 0
    anchor = 0
    def put_len(v):
        while v >= 255:
            out.append(255)
            v -= 255
        out.append(v)
    def emit(lit_end, mlen, off):
        lit = lit_end - anchor
        tok = (min(lit, 15) << 4) | (min(mlen - 4, 15) if mlen else 0)
        out.append(tok)
        if lit >= 15:
            put_len(lit - 15)
        out.extend(data[anchor:lit_end])
        if mlen:
            out.append(off & 255)
            out.append(off >> 8)
            if mlen - 4 >= 15:
                put_len(mlen - 4 - 15)
    def insert(p):
        if p + 4 <= n:
            k = bytes(data[p:p + 4])
            chain[p] = head.get(k, -1)
            head[k] = p
    last = n - 5
    while i < last:
        k = bytes(data[i:i + 4])
        cand = head.get(k, -1)
        best = 0
        boff = 0
        depth = 0
        while cand >= 0 and i - cand <= 65535 and depth < 200:
            l = 4
            while i + l < n - 4 and data[cand + l] == data[i + l]:
                l += 1
            if l > best:
                best = l
                boff = i - cand
            cand = chain[cand]
            depth += 1
        if best >= 4:
            emit(i, best, boff)
            for p in range(i, i + best):
                insert(p)
            i += best
            anchor = i
        else:
            insert(i)
            i += 1
    emit(n, 0, 0)
    return bytes(out)


def decompress(buf):
    out = bytearray()
    i = 0
    n = len(buf)
    while i < n:
        t = buf[i]
        i += 1
        l = t >> 4
        if l == 15:
            while True:
                s = buf[i]
                i += 1
                l += s
                if s != 255:
                    break
        out += buf[i:i + l]
        i += l
        if i >= n:
            break
        off = buf[i] | (buf[i + 1] << 8)
        i += 2
        m = t & 15
        if m == 15:
            while True:
                s = buf[i]
                i += 1
                m += s
                if s != 255:
                    break
        m += 4
        for _ in range(m):
            out.append(out[-off])
    return bytes(out)


def c_array(name, blob, rawlen):
    rows = []
    for k in range(0, len(blob), 24):
        rows.append("    " + ",".join(str(b) for b in blob[k:k + 24]) + ",")
    return "const unsigned char %s[] = {\n%s\n};\nconst unsigned long %s_raw = %d;\n" % (name, "\n".join(rows), name, rawlen)


def pack(name, text):
    raw = text.encode()
    z = compress(raw)
    assert decompress(z) == raw
    return c_array(name, z, len(raw))
