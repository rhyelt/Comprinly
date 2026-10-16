/* Tiny LZ4-style block decoder. The instruction tables, runtimes and C library are stored compressed and unpacked here at start. */
#include "cly.h"

/* Decodes one block. Each token holds a literal length and a match length; matches copy bytes from earlier output (possibly overlapping). */
void lz_unpack(const unsigned char *in, size_t n, unsigned char *out)
{
    const unsigned char *e = in + n;
    while (in < e) {
        unsigned t = *in++;
        size_t l = t >> 4, m = t & 15;
        unsigned s;
        if (l == 15) do { s = *in++; l += s; } while (s == 255);
        memcpy(out, in, l);
        in += l;
        out += l;
        if (in >= e) break;
        size_t off = (size_t)in[0] | ((size_t)in[1] << 8);
        in += 2;
        if (m == 15) do { s = *in++; m += s; } while (s == 255);
        m += 4;
        const unsigned char *p = out - off;
        while (m--) *out++ = *p++;
    }
}

/* Unpacks a blob into a new NUL-terminated string of rawn bytes. The caller keeps the buffer. */
char *lz_text(const unsigned char *in, size_t n, size_t rawn)
{
    char *o = xmalloc(rawn + 1);
    lz_unpack(in, n, (unsigned char *)o);
    o[rawn] = 0;
    return o;
}
