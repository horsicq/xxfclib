/* Impressions .RES per-member LZ codec. Reverse-engineered and verified on all
 * 476 compressed members of c1086.gob (191 PCX/PCC pass a full structural PCX
 * oracle; 0 failures). See F:\ARC\re\gob_res\ALGORITHM.md.
 *
 * Two stream forms share the directory (whose flag field is always 0), so the
 * form is recognised from the data:
 *   A) chunked byte-oriented LZSS + RLE ("u16 len, 0x40/0x80 type" chunks)
 *   B) one headerless 9..14-bit MSB-first LZW stream (clear 0x100, end 0x101)
 * A blob is form A iff its chunk chain walks exactly to its end with every
 * chunk type byte 0x40 or 0x80; otherwise it is LZW.
 */

/* ---- form A: chunked LZSS + RLE --------------------------------------- */
static bool ir_is_chunked(const uint8_t *src, size_t srclen)
{
    size_t q = 0;
    if (srclen < 3) return false;
    while (q < srclen) {
        size_t n;
        if (srclen - q < 3) return false;
        n = (size_t)src[q] | ((size_t)src[q + 1] << 8);
        if (n == 0 || n > srclen - q - 2) return false;
        if (src[q + 2] != 0x40 && src[q + 2] != 0x80) return false;
        if (src[q + 2] == 0x40 && n < 2) return false;
        q += 2 + n;
    }
    return true;
}

static size_t ir_chunked(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap)
{
    size_t q = 0, o = 0;
    while (q < srclen) {
        size_t n = (size_t)src[q] | ((size_t)src[q + 1] << 8);
        const uint8_t *c = src + q + 2;
        size_t p;
        q += 2 + n;
        if (c[0] == 0x80) { /* stored chunk: n-1 raw bytes */
            size_t k, m = n - 1;
            if (m > dstcap - o) return (size_t)-1;
            for (k = 0; k < m; ++k) dst[o + k] = c[1 + k];
            o += m;
            continue;
        }
        /* 0x40: compressed chunk; c[1] is an unused pad byte */
        p = 2;
        while (p < n) {
            unsigned flags;
            int i;
            if (n - p < 2) return (size_t)-1;
            flags = ((unsigned)c[p] << 8) | c[p + 1];
            p += 2;
            for (i = 0; i < 16 && p < n; ++i, flags <<= 1) {
                if (flags & 0x8000u) {
                    unsigned w, dist, nib;
                    if (n - p < 2) return (size_t)-1;
                    w = ((unsigned)c[p] << 8) | c[p + 1];
                    p += 2;
                    dist = w >> 4;
                    nib = w & 15u;
                    if (dist == 0) { /* run: 00 0N CC VV -> ((nib<<8)|CC)+16 copies of VV */
                        size_t cnt, k;
                        uint8_t v;
                        if (n - p < 2) return (size_t)-1;
                        cnt = (((size_t)nib << 8) | c[p]) + 16;
                        v = c[p + 1];
                        p += 2;
                        if (cnt > dstcap - o) return (size_t)-1;
                        for (k = 0; k < cnt; ++k) dst[o + k] = v;
                        o += cnt;
                    } else { /* match: distance 1..4095, length 3..18 */
                        size_t len = (size_t)nib + 3, k;
                        if (dist > o || len > dstcap - o) return (size_t)-1;
                        for (k = 0; k < len; ++k) dst[o + k] = dst[o + k - dist];
                        o += len;
                    }
                } else {
                    if (o >= dstcap || p >= n) return (size_t)-1;
                    dst[o++] = c[p++];
                }
            }
        }
    }
    return o;
}

/* ---- form B: headerless LZW ------------------------------------------- */
#define IR_LZW_MAXBITS 14
#define IR_LZW_SIZE (1u << IR_LZW_MAXBITS)
#define IR_LZW_CLEAR 0x100u
#define IR_LZW_END 0x101u
#define IR_LZW_FIRST 0x102u

static size_t ir_lzw(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap)
{
    /* Each table entry is a (position, length) slice of the already-emitted
     * output: the previous code's string plus the first byte of the current
     * one, which are contiguous in dst. Heap tables keep this reentrant. */
    size_t *pos = (size_t *)xx_mem_alloc(IR_LZW_SIZE * sizeof(*pos));
    size_t *len = (size_t *)xx_mem_alloc(IR_LZW_SIZE * sizeof(*len));
    size_t o = 0, ip = 0, result = (size_t)-1;
    unsigned long long acc = 0;
    int nacc = 0;
    unsigned width = 9, next = IR_LZW_FIRST;
    int have_prev = 0;
    size_t prev_pos = 0, prev_len = 0;

    if (!pos || !len) { xx_mem_free(pos); xx_mem_free(len); return (size_t)-1; }
    for (;;) {
        unsigned code;
        size_t slen, k, start, spos;
        while (nacc < (int)width) {
            if (ip >= srclen) goto done; /* no end code */
            acc = (acc << 8) | src[ip++];
            nacc += 8;
        }
        code = (unsigned)(acc >> (nacc - (int)width)) & ((1u << width) - 1u);
        nacc -= (int)width;

        if (code == IR_LZW_CLEAR) { width = 9; next = IR_LZW_FIRST; have_prev = 0; continue; }
        if (code == IR_LZW_END) { result = o; goto done; }

        start = o;
        if (code < 256) {
            if (o >= dstcap) goto done;
            dst[o++] = (uint8_t)code;
            slen = 1;
        } else if (code < next) {
            spos = pos[code]; slen = len[code];
            if (slen > dstcap - o) goto done;
            for (k = 0; k < slen; ++k) dst[o + k] = dst[spos + k];
            o += slen;
        } else if (code == next && have_prev && next < IR_LZW_SIZE) {
            slen = prev_len + 1; /* KwKwK: previous string + its own first byte */
            if (slen > dstcap - o) goto done;
            for (k = 0; k < slen; ++k) dst[o + k] = dst[prev_pos + k];
            o += slen;
        } else {
            goto done;
        }
        if (have_prev && next < IR_LZW_SIZE) {
            pos[next] = prev_pos;
            len[next] = prev_len + 1;
            ++next;
        }
        have_prev = 1; prev_pos = start; prev_len = slen;
        if (next + 1 >= (1u << width) && width < IR_LZW_MAXBITS) ++width; /* early change */
    }
done:
    xx_mem_free(pos);
    xx_mem_free(len);
    return result;
}

/* Produce exactly dstcap bytes, or fail. */
static bool ir_decompress(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap)
{
    size_t got;
    if (!src || !dst) return false;
    got = ir_is_chunked(src, srclen) ? ir_chunked(src, srclen, dst, dstcap)
                                     : ir_lzw(src, srclen, dst, dstcap);
    return got == dstcap;
}
