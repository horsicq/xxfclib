/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * sfArk v2 compressed SoundFont.  xx_sfark_compressed_soundfont.h carries the
 * header table.  Original code written from the format's structure; checked
 * byte-for-byte against sfarkxtc (sfArkLib 3.0), which was used as a black
 * box only.
 *
 * After the header, the file name and the optional licence / notes blocks,
 * everything is one bit stream: 16-bit little-endian words read MSB first.
 * The output SoundFont is rebuilt in three phases:
 *
 *   1. bytes [0, audio start): zlib blocks, each "u32 length, stream",
 *      the length and the stream bytes themselves read 8 bits at a time
 *      from the bit stream;
 *   2. the 16-bit samples [audio start, post-audio start), in chunks of
 *      4096 samples (1024 for method 5), each Rice coded and then undone
 *      through LPC prediction (methods 6, 7), up to 20 levels of
 *      differencing and per-64-sample left shifts;
 *   3. bytes from post-audio start to the end: zlib blocks again.
 *
 * The file check (header 0x0C) starts at 0, runs Adler-32 over every
 * non-audio byte (licence, notes, then the SoundFont's own non-audio blocks)
 * and folds each audio chunk in as check = 2 * check + sum, which is
 * verified after the last block.
 *
 * The LPC predictor recomputes its coefficients from the decoded signal, in
 * single-precision floating point.  Reproducing the encoder exactly needs
 * the same arithmetic order: float x float products are exact in double,
 * sums are carried in double and rounded to float after every run of 16
 * terms (and after each term of the tail), coefficients are truncated.  The
 * decoder below keeps that order literally; do not "simplify" the loops.
 * (An x87 build without SSE2 would add excess precision and is not
 * supported for methods 6 and 7.)
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfark_compressed_soundfont/xx_sfark_compressed_soundfont.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef SFARK_COMPRESSED_SOUNDFONT
#define XX_SFARK_COMPRESSED_SOUNDFONT_FILE_TYPE XX_FILE_TYPE_SFARK_COMPRESSED_SOUNDFONT
#else
#define XX_SFARK_COMPRESSED_SOUNDFONT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SFK_HEADER_SIZE 42U
#define SFK_MAX_NAME 1024U     /* file name incl. NUL */
#define SFK_BLOCK_MAX 0x40000U /* zlib block, packed and unpacked */
#define SFK_MAX_CHUNK 4096U    /* samples per audio chunk */
#define SFK_MAX_LEVELS 20
#define SFK_MAX_ORDER 128
#define SFK_MAX_DELTA_ZEROS 64U    /* unary part of a small delta */
#define SFK_MAX_RICE_ZEROS 0xFFFFU /* unary part of a residual */
#define SFK_WINDOW 65536U
/* The structural walk that measures the stream is skipped above this. */
#define SFK_MAX_SIZE_PASS ((int64_t)256 * 1024 * 1024)
#define SFK_FALLBACK_NAME "soundfont.sf2"

/* ------------------------------------------------------------------ */
/* header                                                               */
/* ------------------------------------------------------------------ */

typedef struct sfk_header_s {
    uint32_t flags;
    uint32_t original_size;
    uint32_t compressed_size;
    uint32_t file_check;
    uint32_t method;
    uint32_t audio_start;
    uint32_t post_audio;
    char name[SFK_MAX_NAME]; /* sanitised output name */
    int64_t text_offset;     /* first licence / notes block */
    int64_t input_size;      /* bytes from base to the device end */
} sfk_header;

typedef struct sfk_text_s {
    bool present;
    int64_t offset; /* of the u32 length */
    uint32_t packed;
    uint32_t size;
} sfk_text;

typedef struct sfk_context_s {
    sfk_header header;
    sfk_text text[2]; /* [0] licence, [1] notes */
    int64_t base_address;
    int64_t stream_offset; /* first word of the bit stream */
    int64_t archive_size;  /* measured, or input_size when unknown */
    char names[3][SFK_MAX_NAME + 16];
    size_t count;
    size_t kinds[3]; /* 0 SoundFont, 1 licence, 2 notes */
} sfk_context;

static bool sfk_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* sfArk's check uses Adler-32 with a 0 seed; the zlib update itself does
 * not care what the seed is. */
static uint32_t sfk_adler(uint32_t check, const uint8_t *data, size_t size)
{
    return size ? xx_adler32_update(check, data, size) : check;
}

static bool sfk_is_device_stem(const char *name, size_t stem)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t d, i;
    for (d = 0U; d < sizeof(devices) / sizeof(devices[0]); ++d) {
        const char *w = devices[d];
        for (i = 0U; i < stem && w[i]; ++i) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (c != w[i]) break;
        }
        if (i == stem && !w[i]) return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        char a = name[0], b = name[1], c = name[2];
        if (a >= 'a') a = (char)(a - 32);
        if (b >= 'a') b = (char)(b - 32);
        if (c >= 'a') c = (char)(c - 32);
        if ((a == 'C' && b == 'O' && c == 'M') || (a == 'L' && b == 'P' && c == 'T')) return true;
    }
    return false;
}

/* The stored name is a bare Windows file name.  Keep its last path
 * component, map bytes a file system may refuse to '_', and fall back to a
 * fixed name when nothing usable is left (empty, dots only, device name). */
static void sfk_make_name(const uint8_t *raw, size_t length, char *out)
{
    size_t start = 0U, i, n = 0U, stem;
    bool meaningful = false;
    for (i = 0U; i < length; ++i)
        if (raw[i] == '/' || raw[i] == '\\' || raw[i] == ':') start = i + 1U;
    for (i = start; i < length && n + 1U < SFK_MAX_NAME; ++i) {
        uint8_t c = raw[i];
        if (c < 0x20U || c > 0x7EU || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') c = '_';
        if (c != '.' && c != ' ') meaningful = true;
        out[n++] = (char)c;
    }
    out[n] = 0;
    /* Windows drops trailing dots and spaces. */
    while (n > 0U && (out[n - 1U] == '.' || out[n - 1U] == ' ')) out[--n] = 0;
    stem = 0U;
    while (stem < n && out[stem] != '.') ++stem;
    while (stem > 0U && out[stem - 1U] == ' ') --stem;
    if (!meaningful || n == 0U || sfk_is_device_stem(out, stem)) xx_rt_memcpy(out, SFK_FALLBACK_NAME, sizeof(SFK_FALLBACK_NAME));
}

/* The header, the file name and the header check.  Cheap: at most
 * SFK_HEADER_SIZE + SFK_MAX_NAME bytes are read. */
static bool sfk_parse_header(Abstractformat *format, sfk_header *h)
{
    uint8_t buf[SFK_HEADER_SIZE + SFK_MAX_NAME];
    int64_t total, size;
    size_t avail, name_len = 0U;
    uint32_t stored, check;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)SFK_HEADER_SIZE + 2) return false;
    avail = size < (int64_t)sizeof(buf) ? (size_t)size : sizeof(buf);
    if (!sfk_read_at(format->device, format->base_address, buf, SFK_HEADER_SIZE)) return false;
    if (xx_rt_memcmp(buf + 0x1AU, "sfArk", 5U) != 0) return false;
    xx_mem_zero(h, sizeof(*h));
    h->flags = xx_data_get_u32(buf, 4, 0, false);
    h->original_size = xx_data_get_u32(buf + 4U, 4, 0, false);
    h->compressed_size = xx_data_get_u32(buf + 8U, 4, 0, false);
    h->file_check = xx_data_get_u32(buf + 12U, 4, 0, false);
    stored = xx_data_get_u32(buf + 16U, 4, 0, false);
    h->method = buf[0x1FU];
    h->audio_start = xx_data_get_u32(buf + 0x22U, 4, 0, false);
    h->post_audio = xx_data_get_u32(buf + 0x26U, 4, 0, false);
    if (h->method < 4U || h->method > 7U) return false;
    if (h->original_size == 0U || h->original_size > (uint32_t)INT32_MAX || h->audio_start == 0U || h->audio_start >= h->post_audio || h->post_audio > h->original_size)
        return false;
    /* The name: read what is there (up to the cap) and find its NUL. */
    if (avail > SFK_HEADER_SIZE && !sfk_read_at(format->device, format->base_address + SFK_HEADER_SIZE, buf + SFK_HEADER_SIZE, avail - SFK_HEADER_SIZE)) return false;
    while (SFK_HEADER_SIZE + name_len < avail && buf[SFK_HEADER_SIZE + name_len] != 0U) ++name_len;
    if (SFK_HEADER_SIZE + name_len >= avail) return false;
    buf[16] = buf[17] = buf[18] = buf[19] = 0U;
    check = sfk_adler(0U, buf, SFK_HEADER_SIZE + name_len + 1U);
    if (check != stored) return false;
    sfk_make_name(buf + SFK_HEADER_SIZE, name_len, h->name);
    h->text_offset = format->base_address + (int64_t)SFK_HEADER_SIZE + (int64_t)name_len + 1;
    h->input_size = size;
    return true;
}

/* Inflate one zlib block of a known packed length into `out`
 * (SFK_BLOCK_MAX bytes).  Returns the unpacked length, 0 on failure. */
static uint32_t sfk_inflate(const uint8_t *in, uint32_t packed, uint8_t *out)
{
    size_t written = 0U;
    if (!xx_zlib_stream_decode_memory(in, packed, out, SFK_BLOCK_MAX, &written) || written == 0U || written > SFK_BLOCK_MAX) return 0U;
    return (uint32_t)written;
}

/* Licence then notes, as raw "u32 length + zlib" blocks.  `scratch_in` and
 * `scratch_out` are SFK_BLOCK_MAX bytes each.  With `check` the unpacked
 * text is folded into the running file check. */
static bool sfk_parse_texts(Abstractformat *format, sfk_context *ctx, uint8_t *scratch_in, uint8_t *scratch_out, uint32_t *check)
{
    int64_t offset = ctx->header.text_offset;
    int64_t end = format->base_address + ctx->header.input_size;
    int which;
    for (which = 0; which < 2; ++which) {
        uint32_t bit = which == 0 ? 2U : 1U;
        uint8_t word[4];
        uint32_t packed, size;
        sfk_text *t = &ctx->text[which];
        xx_mem_zero(t, sizeof(*t));
        if (!(ctx->header.flags & bit)) continue;
        if (end - offset < 4 || !sfk_read_at(format->device, offset, word, 4U)) return false;
        packed = xx_data_get_u32(word, 4, 0, false);
        if (packed == 0U || packed > SFK_BLOCK_MAX || (int64_t)packed > end - offset - 4) return false;
        if (!sfk_read_at(format->device, offset + 4, scratch_in, packed)) return false;
        size = sfk_inflate(scratch_in, packed, scratch_out);
        if (size == 0U) return false;
        if (check) *check = sfk_adler(*check, scratch_out, size);
        t->present = true;
        t->offset = offset;
        t->packed = packed;
        t->size = size;
        offset += 4 + (int64_t)packed;
    }
    ctx->stream_offset = offset;
    return offset < end;
}

/* ------------------------------------------------------------------ */
/* bit stream                                                           */
/* ------------------------------------------------------------------ */

typedef struct sfk_bits_s {
    xx_io_device *device;
    int64_t next; /* device offset of the next byte to buffer */
    int64_t end;  /* device offset of the end of input */
    size_t length, index;
    uint32_t word;
    int left; /* bits still unread in `word` */
    uint8_t buffer[SFK_WINDOW];
} sfk_bits;

static void sfk_bits_init(sfk_bits *b, xx_io_device *device, int64_t start, int64_t end)
{
    b->device = device;
    b->next = start;
    b->end = end;
    b->length = b->index = 0U;
    b->word = 0U;
    b->left = 0;
}

/* Device offset just past the last word consumed. */
static int64_t sfk_bits_position(const sfk_bits *b)
{
    return b->next - (int64_t)(b->length - b->index);
}

static bool sfk_bits_fetch(sfk_bits *b)
{
    if (b->length - b->index < 2U) {
        size_t keep = b->length - b->index, want;
        int64_t remain = b->end - b->next;
        if (keep) b->buffer[0] = b->buffer[b->index];
        b->length = keep;
        b->index = 0U;
        want = SFK_WINDOW - keep;
        if (remain < (int64_t)want) want = remain > 0 ? (size_t)remain : 0U;
        if (want) {
            if (!sfk_read_at(b->device, b->next, b->buffer + keep, want)) return false;
            b->next += (int64_t)want;
            b->length += want;
        }
        if (b->length < 2U) return false;
    }
    b->word = (uint32_t)b->buffer[b->index] | ((uint32_t)b->buffer[b->index + 1U] << 8U);
    b->index += 2U;
    b->left = 16;
    return true;
}

/* One bit, or -1 at the end of input. */
static int sfk_bit(sfk_bits *b)
{
    if (b->left == 0 && !sfk_bits_fetch(b)) return -1;
    --b->left;
    return (int)((b->word >> (unsigned)b->left) & 1U);
}

static bool sfk_get(sfk_bits *b, unsigned count, uint32_t *out)
{
    uint32_t v = 0U;
    while (count) {
        unsigned take;
        if (b->left == 0 && !sfk_bits_fetch(b)) return false;
        take = count < (unsigned)b->left ? count : (unsigned)b->left;
        b->left -= (int)take;
        v = (take >= 32U ? 0U : v << take) | ((b->word >> (unsigned)b->left) & ((1U << take) - 1U));
        count -= take;
    }
    *out = v;
    return true;
}

/* A small signed change: n zero bits, a one, and when n > 0 a sign bit
 * (1 = negative).  Returns base + change. */
static bool sfk_delta(sfk_bits *b, int32_t base, int32_t *out)
{
    uint32_t zeros = 0U;
    int bit;
    while ((bit = sfk_bit(b)) == 0)
        if (++zeros > SFK_MAX_DELTA_ZEROS) return false;
    if (bit < 0) return false;
    if (zeros) {
        bit = sfk_bit(b);
        if (bit < 0) return false;
        *out = bit ? base - (int32_t)zeros : base + (int32_t)zeros;
    } else {
        *out = base;
    }
    return true;
}

static bool sfk_get_bytes(sfk_bits *b, uint8_t *out, uint32_t count)
{
    uint32_t i, v;
    for (i = 0U; i < count; ++i) {
        if (!sfk_get(b, 8U, &v)) return false;
        out[i] = (uint8_t)v;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* LPC                                                                  */
/* ------------------------------------------------------------------ */

typedef struct sfk_lpc_s {
    int32_t hist[SFK_MAX_ORDER];
    float acc[4][SFK_MAX_ORDER + 1];
    int32_t state[SFK_MAX_ORDER + 1];
    int ring;
} sfk_lpc;

static int32_t sfk_mul(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

static int32_t sfk_neg(int32_t a)
{
    return (int32_t)(0U - (uint32_t)a);
}

static int32_t sfk_add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}

static int32_t sfk_sub(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a - (uint32_t)b);
}

static int32_t sfk_sar(int32_t v, unsigned n)
{
    return v < 0 ? (int32_t)~(~(uint32_t)v >> n) : (int32_t)((uint32_t)v >> n);
}

/* double -> int as a 64-bit truncating convert, low 32 bits kept; NaN and
 * out-of-range values give the "integer indefinite" value, whose low half
 * is 0. */
static int32_t sfk_trunc(double x)
{
    int64_t v;
    if (!(x > -9223372036854775808.0 && x < 9223372036854775808.0)) return 0;
    v = (int64_t)x;
    return (int32_t)(uint32_t)(uint64_t)v;
}

static void sfk_lpc_reset(sfk_lpc *l)
{
    xx_mem_zero(l, sizeof(*l));
}

/* Reflection coefficients (Q14) from the autocorrelation r[0..p]. */
static void sfk_schur(const float *r, int p, int32_t *k)
{
    float a[SFK_MAX_ORDER], b[SFK_MAX_ORDER];
    double e;
    int i, j;
    if (r[0] == 0.0f) {
        for (i = 0; i < p; ++i) k[i] = 0;
        return;
    }
    e = (double)r[0];
    for (i = 0; i < p; ++i) a[i] = b[i] = r[i + 1];
    for (i = 0;;) {
        double a0 = (double)a[0];
        float q = (float)(a0 / e);
        double kd = 0.0 - (double)q;
        float en = (float)(a0 * kd + e);
        k[i] = sfk_trunc((double)(float)(kd * 16384.0));
        if (++i >= p) break;
        for (j = 0; j < p - i; ++j) {
            double a1 = (double)a[j + 1];
            a[j] = (float)(kd * (double)b[j] + a1);
            b[j] = (float)(kd * a1 + (double)b[j]);
        }
        e = (double)en;
    }
}

/* sum_{t<count} x[t] * y[t], carried in double, rounded to float after
 * each full run of 16 terms (while the run start is below `group_end`) and
 * after each remaining term. */
static float sfk_dot(const float *x, const float *y, int start, int group_end, int end)
{
    float s = 0.0f;
    int j = start;
    while (j < group_end) {
        double d = (double)s;
        int u;
        for (u = 0; u < 16; ++u) d += (double)x[j + u] * (double)y[j + u];
        s = (float)d;
        j += 16;
    }
    while (j < end) {
        s = (float)((double)s + (double)x[j] * (double)y[j]);
        ++j;
    }
    return s;
}

static void sfk_lpc_block(sfk_lpc *l, const int32_t *in, int32_t *out, int p, bool bypass)
{
    float r[SFK_MAX_ORDER + 1];
    int32_t k[SFK_MAX_ORDER];
    float tmp[2 * SFK_MAX_ORDER];
    float tb[128];
    int i, j, lag;
    for (i = 0; i <= p; ++i) r[i] = (float)((((double)l->acc[1][i] + (double)l->acc[0][i]) + (double)l->acc[2][i]) + (double)l->acc[3][i]);
    if (bypass) {
        sfk_lpc_reset(l);
        for (i = 0; i < 128; ++i) out[i] = in[i];
    } else {
        int32_t *st = l->state;
        sfk_schur(r, p, k);
        for (i = 0; i < 128; ++i) {
            int32_t v = in[i];
            for (j = p - 1; j >= 0; --j) {
                int32_t prod = sfk_mul(st[j], k[j]);
                v = prod < 0 ? sfk_add(v, sfk_sar(sfk_neg(prod), 14U)) : sfk_sub(v, sfk_sar(prod, 14U));
                prod = sfk_mul(v, k[j]);
                st[j + 1] = prod < 0 ? sfk_sub(st[j], sfk_sar(sfk_neg(prod), 14U)) : sfk_add(st[j], sfk_sar(prod, 14U));
            }
            st[0] = v;
            out[i] = v;
        }
    }
    /* Cross terms between the previous history and this block. */
    for (i = 0; i < p; ++i) {
        tmp[i] = (float)l->hist[i];
        tmp[p + i] = (float)out[i];
    }
    for (lag = p; lag >= 1; --lag) {
        float s = sfk_dot(tmp + lag, tmp, p - lag, p - 15, p);
        l->acc[l->ring][lag] = (float)((double)l->acc[l->ring][lag] + (double)s);
    }
    l->ring = (l->ring + 1) & 3;
    for (i = 0; i < 128; ++i) tb[i] = (float)out[i];
    for (lag = p; lag >= 0; --lag) l->acc[l->ring][lag] = sfk_dot(tb + lag, tb, 0, 128 - lag - 15, 128 - lag);
    for (i = 0; i < p; ++i) l->hist[i] = out[i];
}

/* ------------------------------------------------------------------ */
/* decoder                                                              */
/* ------------------------------------------------------------------ */

typedef struct sfk_decoder_s {
    sfk_bits bits;
    uint32_t method;
    int bufsize, maxdiff, maxdiff2, order;
    int32_t nbits;
    int32_t prev_ndiff, prev_ndiff2;
    int16_t prev_shift, prev_used_shift;
    int16_t prev[SFK_MAX_LEVELS];
    uint32_t check;
    int16_t buf_a[SFK_MAX_CHUNK + 64], buf_b[SFK_MAX_CHUNK + 64];
    int16_t shifts[SFK_MAX_CHUNK / 64 + 1];
    int32_t lin[SFK_MAX_CHUNK + 128], lout[SFK_MAX_CHUNK + 128];
    uint8_t zin[SFK_BLOCK_MAX], zout[SFK_BLOCK_MAX];
    uint8_t out[2 * SFK_MAX_CHUNK];
    sfk_lpc lpc;
} sfk_decoder;

static uint32_t sfk_bufsum(const int16_t *v, int n)
{
    uint32_t s = 0U;
    int i;
    for (i = 0; i < n; ++i) s += v[i] < 0 ? (uint32_t)(uint16_t)~(uint16_t)v[i] : (uint32_t)v[i];
    return s;
}

static bool sfk_read_group(sfk_decoder *d, int16_t *out, int count)
{
    int i;
    if (!sfk_delta(&d->bits, d->nbits, &d->nbits)) return false;
    if (d->nbits >= 0 && d->nbits < 14) {
        unsigned nb = (unsigned)d->nbits;
        for (i = 0; i < count; ++i) {
            uint32_t v, q = 0U, mag;
            int bit;
            if (!sfk_get(&d->bits, nb + 1U, &v)) return false;
            while ((bit = sfk_bit(&d->bits)) == 0)
                if (++q > SFK_MAX_RICE_ZEROS) return false;
            if (bit < 0) return false;
            mag = ((v >> 1U) | (q << nb)) & 0xFFFFU;
            out[i] = (int16_t)(uint16_t)((v & 1U) ? ~mag : mag);
        }
    } else if (d->nbits == 14) {
        for (i = 0; i < count; ++i) {
            uint32_t v;
            if (!sfk_get(&d->bits, 16U, &v)) return false;
            out[i] = (int16_t)(uint16_t)v;
        }
    } else if (d->nbits == -1) {
        for (i = 0; i < count; ++i) {
            int bit = sfk_bit(&d->bits);
            if (bit < 0) return false;
            out[i] = (int16_t)-bit;
        }
    } else if (d->nbits == -2) {
        for (i = 0; i < count; ++i) out[i] = 0;
    } else {
        return false;
    }
    return true;
}

static bool sfk_read_residual(sfk_decoder *d, int16_t *out, int n, int group)
{
    int g;
    for (g = 0; g < n; g += group)
        if (!sfk_read_group(d, out + g, n - g < group ? n - g : group)) return false;
    return true;
}

static unsigned sfk_bitlen(uint32_t v)
{
    unsigned n = 0U;
    while (v) {
        ++n;
        v >>= 1U;
    }
    return n;
}

/* Per-64-sample shift map.  Returns 1 with the map filled, 0 when the
 * chunk carries none, -1 on a malformed map. */
static int sfk_read_shifts(sfk_decoder *d, int n)
{
    int32_t count = (n + 63) >> 6, pos = 0, filled = 0;
    int bit = sfk_bit(&d->bits);
    unsigned changes = 0U;
    if (bit <= 0) return bit;
    while ((bit = sfk_bit(&d->bits)) == 1) {
        uint32_t step;
        int32_t value;
        if (++changes > 1024U) return -1;
        if (!sfk_get(&d->bits, sfk_bitlen((uint32_t)(count - pos - 1)), &step)) return -1;
        pos = (int32_t)((uint32_t)pos + step);
        if (d->prev_shift == 0) {
            if (!sfk_delta(&d->bits, d->prev_used_shift, &value)) return -1;
            d->prev_used_shift = (int16_t)value;
        } else if (!sfk_delta(&d->bits, 0, &value)) {
            return -1;
        }
        if (pos > count) return -1;
        for (; filled < pos; ++filled) d->shifts[filled] = d->prev_shift;
        d->prev_shift = (int16_t)value;
    }
    if (bit < 0) return -1;
    for (; filled < count; ++filled) d->shifts[filled] = d->prev_shift;
    return 1;
}

/* Level undo: running sum carried across chunks. */
static void sfk_undo_sum(int16_t *dst, const int16_t *src, int n, int16_t *prev)
{
    int i;
    int16_t acc = *prev;
    if (n <= 0) return;
    for (i = 0; i < n; ++i) {
        acc = (int16_t)(uint16_t)((uint16_t)acc + (uint16_t)src[i]);
        dst[i] = acc;
    }
    *prev = dst[n - 1];
}

/* Level undo: centred average, run backwards over the chunk. */
static void sfk_undo_avg(int16_t *dst, const int16_t *src, int n, int16_t *prev)
{
    int i;
    if (n <= 0) return;
    dst[n - 1] = src[n - 1];
    if (n >= 2) {
        for (i = n - 2; i >= 1; --i) dst[i] = (int16_t)(uint16_t)((uint16_t)src[i] + (uint16_t)(int16_t)(((int32_t)dst[i + 1] + (int32_t)src[i - 1]) >> 1));
        dst[0] = (int16_t)(uint16_t)((uint16_t)src[0] + (uint16_t)(int16_t)(dst[1] >> 1));
    }
    *prev = dst[n - 1];
}

/* Level undo: half-step accumulator. */
static void sfk_undo_half(int16_t *dst, const int16_t *src, int n, int16_t *prev)
{
    int i;
    int16_t s = *prev;
    for (i = 0; i < n; ++i) {
        int16_t e = src[i], h;
        dst[i] = (int16_t)(uint16_t)((uint16_t)s + (uint16_t)e);
        h = e < 0 ? (int16_t)-(int16_t)((-(int32_t)e) >> 1) : (int16_t)(e >> 1);
        s = (int16_t)(uint16_t)((uint16_t)s + (uint16_t)h);
    }
    *prev = s;
}

static void sfk_swap(int16_t **a, int16_t **b)
{
    int16_t *t = *a;
    *a = *b;
    *b = t;
}

/* One audio chunk of n (0..4096) samples into d->out.  `compute` false
 * walks the bit stream only (no prediction, no output). */
static bool sfk_audio_chunk(sfk_decoder *d, int n, bool compute)
{
    int16_t *cur = d->buf_a, *spare = d->buf_b;
    int i;
    if (d->method == 4U) {
        int32_t nd;
        if (!sfk_delta(&d->bits, d->prev_ndiff, &nd) || nd < 0 || nd > d->maxdiff) return false;
        d->prev_ndiff = nd;
        if (!sfk_read_residual(d, cur, n, 256)) return false;
        if (compute) {
            for (i = nd - 1; i >= 0; --i) {
                if (i == 0) d->check = 2U * d->check + sfk_bufsum(cur, n);
                sfk_undo_sum(spare, cur, n, &d->prev[i]);
                sfk_swap(&cur, &spare);
            }
        }
    } else {
        int shifted = sfk_read_shifts(d, n);
        int path, method_bits[SFK_MAX_LEVELS];
        int32_t nd;
        uint32_t flags = 0U;
        if (shifted < 0) return false;
        path = sfk_bit(&d->bits);
        if (path < 0) return false;
        if (path == 0) {
            if (!sfk_delta(&d->bits, d->prev_ndiff, &nd) || nd < 0 || nd > d->maxdiff) return false;
            d->prev_ndiff = nd;
            for (i = 0; i < nd; ++i)
                if ((method_bits[i] = sfk_bit(&d->bits)) < 0) return false;
        } else {
            if (!sfk_delta(&d->bits, d->prev_ndiff2, &nd) || nd < 0 || nd > d->maxdiff2) return false;
            d->prev_ndiff2 = nd;
        }
        if (d->method != 5U) {
            int bit = sfk_bit(&d->bits);
            if (bit < 0) return false;
            if (bit) {
                uint32_t lo, hi;
                if (!sfk_get(&d->bits, 16U, &lo) || !sfk_get(&d->bits, 16U, &hi)) return false;
                flags = lo | (hi << 16U);
            }
        }
        if (!sfk_read_residual(d, cur, n, 32)) return false;
        if (!compute) return true;
        if (d->method != 5U) {
            if (n < 128) {
                /* Too short to predict: taken as is. */
            } else {
                uint32_t bit = 1U;
                int b0;
                for (i = 0; i < n; ++i) d->lin[i] = cur[i];
                for (; i < n + 128; ++i) d->lin[i] = 0;
                for (b0 = 0; b0 < n; b0 += 128) {
                    sfk_lpc_block(&d->lpc, d->lin + b0, d->lout + b0, d->order, (flags & bit) != 0U);
                    bit <<= 1U;
                }
                for (i = 0; i < n; ++i) spare[i] = (int16_t)(uint16_t)(uint32_t)d->lout[i];
                sfk_swap(&cur, &spare);
            }
        }
        for (i = nd - 1; i >= 0; --i) {
            if (path == 1) sfk_undo_half(spare, cur, n, &d->prev[i]);
            else if (method_bits[i] == 0) sfk_undo_sum(spare, cur, n, &d->prev[i]);
            else sfk_undo_avg(spare, cur, n, &d->prev[i]);
            sfk_swap(&cur, &spare);
        }
        if (shifted) {
            int count = (n + 63) >> 6, b;
            for (b = 0; b < count; ++b) {
                unsigned s = (unsigned)d->shifts[b] & 31U;
                int end = b * 64 + 64 < n ? b * 64 + 64 : n;
                if (!d->shifts[b]) continue;
                for (i = b * 64; i < end; ++i) cur[i] = (int16_t)(uint16_t)(((uint32_t)(uint16_t)cur[i] << s) & 0xFFFFU);
            }
        }
        d->check = 2U * d->check + sfk_bufsum(cur, n);
    }
    if (compute)
        for (i = 0; i < n; ++i) {
            d->out[2 * i] = (uint8_t)((uint16_t)cur[i] & 0xFFU);
            d->out[2 * i + 1] = (uint8_t)((uint16_t)cur[i] >> 8U);
        }
    return true;
}

static bool sfk_write_all(xx_io_device *dst, const uint8_t *data, size_t size)
{
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(dst, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Run the whole stream.  With `compute` false it only measures: zlib blocks
 * are still inflated (their sizes drive the phases) but the audio math, the
 * check and all output are skipped.  `end` receives the device offset just
 * past the last word read. */
static bool sfk_run(Abstractformat *format, const sfk_context *ctx, xx_io_device *dst, bool compute, int64_t *end, xx_pd_struct *pd)
{
    static const int params[4][4] = {{3, 0, 4096, 0}, {20, 20, 1024, 0}, {3, 3, 4096, 8}, {3, 5, 4096, 128}};
    const sfk_header *h = &ctx->header;
    sfk_decoder *d;
    uint32_t pos = 0U;
    int phase = 0; /* 0 pre-audio, 1 audio, 2 post-audio */
    bool result = false;
    uint64_t steps = 0U;
    d = (sfk_decoder *)xx_mem_calloc(1U, sizeof(*d));
    if (!d) return false;
    d->method = h->method;
    d->maxdiff = params[h->method - 4U][0];
    d->maxdiff2 = params[h->method - 4U][1];
    d->bufsize = params[h->method - 4U][2];
    d->order = params[h->method - 4U][3];
    d->nbits = 8;
    sfk_lpc_reset(&d->lpc);
    if (compute) {
        /* The check starts over the licence and notes texts. */
        sfk_context scratch = *ctx;
        if (!sfk_parse_texts(format, &scratch, d->zin, d->zout, &d->check)) goto done;
    }
    sfk_bits_init(&d->bits, format->device, ctx->stream_offset, format->base_address + h->input_size);
    while (pos < h->original_size) {
        if (pd && (++steps & 63U) == 0U && xx_pd_is_stopped(pd)) goto done;
        if (phase == 1) {
            uint32_t remain = h->post_audio - pos;
            int n = d->bufsize;
            bool last = false;
            if (remain <= 2U * (uint32_t)d->bufsize) {
                n = (int)(remain / 2U);
                last = true;
            }
            if (!sfk_audio_chunk(d, n, compute)) goto done;
            if (compute && dst && !sfk_write_all(dst, d->out, (size_t)n * 2U)) goto done;
            pos += (uint32_t)n * 2U;
            if (last) phase = 2;
        } else {
            uint8_t word[4];
            uint32_t packed, size;
            if (!sfk_get_bytes(&d->bits, word, 4U)) goto done;
            packed = xx_data_get_u32(word, 4, 0, false);
            if (packed == 0U || packed > SFK_BLOCK_MAX || !sfk_get_bytes(&d->bits, d->zin, packed)) goto done;
            size = sfk_inflate(d->zin, packed, d->zout);
            if (size == 0U) goto done;
            if (size > h->original_size - pos) goto done;
            if (compute) {
                d->check = sfk_adler(d->check, d->zout, size);
                if (dst && !sfk_write_all(dst, d->zout, size)) goto done;
            }
            pos += size;
            /* A block may run past the declared audio start (sfArkLib
             * accepts that); the samples then start where it ended. */
            if (phase == 0 && pos >= h->audio_start) {
                if (pos > h->post_audio || ((h->post_audio - pos) & 1U)) goto done;
                phase = 1;
            }
        }
    }
    if (compute && d->check != h->file_check) goto done;
    if (end) *end = sfk_bits_position(&d->bits);
    result = true;
done:
    xx_mem_free(d);
    return result;
}

/* ------------------------------------------------------------------ */
/* container                                                            */
/* ------------------------------------------------------------------ */

static void sfk_change_ext(const char *name, const char *ext, char *out, size_t cap)
{
    size_t len = xx_str_len(name), dot = len, i, e = xx_str_len(ext);
    for (i = len; i > 0U; --i)
        if (name[i - 1U] == '.') {
            dot = i - 1U;
            break;
        }
    if (dot == 0U) dot = len; /* ".sf2" alone: keep it, append */
    if (dot + e + 1U > cap) dot = cap - e - 1U;
    xx_rt_memcpy(out, name, dot);
    xx_rt_memcpy(out + dot, ext, e + 1U);
}

static bool sfk_same_name(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
        if (!x) return true;
    }
}

static bool sfk_parse(Abstractformat *format, sfk_context *ctx, bool measure, xx_pd_struct *pd)
{
    uint8_t *zin, *zout;
    bool ok;
    size_t i, j;
    xx_mem_zero(ctx, sizeof(*ctx));
    if (!sfk_parse_header(format, &ctx->header)) return false;
    ctx->base_address = format->base_address;
    zin = (uint8_t *)xx_mem_alloc(SFK_BLOCK_MAX);
    zout = (uint8_t *)xx_mem_alloc(SFK_BLOCK_MAX);
    ok = zin && zout && sfk_parse_texts(format, ctx, zin, zout, NULL);
    if (zin) xx_mem_free(zin);
    if (zout) xx_mem_free(zout);
    if (!ok) return false;
    ctx->archive_size = ctx->header.input_size;
    if (measure && ctx->header.input_size <= SFK_MAX_SIZE_PASS) {
        int64_t end = 0;
        if (sfk_run(format, ctx, NULL, false, &end, pd) && end > format->base_address) ctx->archive_size = end - format->base_address;
    }
    /* Records: the SoundFont, then licence and notes. */
    ctx->count = 0U;
    xx_rt_memcpy(ctx->names[0], ctx->header.name, xx_str_len(ctx->header.name) + 1U);
    ctx->kinds[ctx->count++] = 0U;
    if (ctx->text[0].present) {
        sfk_change_ext(ctx->header.name, ".license.txt", ctx->names[ctx->count], sizeof(ctx->names[0]));
        ctx->kinds[ctx->count++] = 1U;
    }
    if (ctx->text[1].present) {
        sfk_change_ext(ctx->header.name, ".txt", ctx->names[ctx->count], sizeof(ctx->names[0]));
        ctx->kinds[ctx->count++] = 2U;
    }
    /* Keep the three names distinct (a SoundFont stored as "x.txt"). */
    for (i = 1U; i < ctx->count; ++i)
        for (j = 0U; j < i; ++j)
            if (sfk_same_name(ctx->names[i], ctx->names[j])) {
                size_t len = xx_str_len(ctx->names[i]);
                xx_rt_memcpy(ctx->names[i] + len, i == 1U ? "_1" : "_2", 3U);
            }
    return true;
}

typedef struct sfk_stream_s {
    sfk_context context;
    size_t index;
} sfk_stream;

static void sfk_stream_free(void *opaque)
{
    if (opaque) xx_mem_free(opaque);
}

static bool sfk_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *sfk_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sfk_set_record(xx_archive_record *record, const sfk_context *ctx, size_t index)
{
    size_t kind = ctx->kinds[index];
    int64_t offset, packed;
    uint64_t size;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (kind == 0U) {
        offset = ctx->stream_offset;
        packed = ctx->base_address + ctx->archive_size - ctx->stream_offset;
        size = ctx->header.original_size;
        record->header_offset = ctx->base_address;
        record->header_size = ctx->stream_offset - ctx->base_address;
    } else {
        const sfk_text *t = &ctx->text[kind - 1U];
        offset = t->offset + 4;
        packed = t->packed;
        size = t->size;
        record->header_offset = t->offset;
        record->header_size = 4;
    }
    if (packed < 0) packed = 0;
    record->data_offset = offset;
    record->compressed_size = packed;
    return xx_archive_record_set_original_name(record, ctx->names[index]) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, size) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_sfark_compressed_soundfont_init(xx_sfark_compressed_soundfont *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFARK_COMPRESSED_SOUNDFONT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sfark");
    xx_format_set_extension(&archive->format, "sfArk");
    archive->format.check_is_valid = xx_sfark_compressed_soundfont_check_is_valid;
    archive->format.handle_base_info = xx_sfark_compressed_soundfont_handle_base_info;
    archive->format.get_format_size = xx_sfark_compressed_soundfont_get_format_size;
    archive->format.get_number_of_archive_records = xx_sfark_compressed_soundfont_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_sfark_compressed_soundfont_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_sfark_compressed_soundfont_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_sfark_compressed_soundfont_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_sfark_compressed_soundfont_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_sfark_compressed_soundfont_free_archive_records_reading;
}

xx_sfark_compressed_soundfont *xx_sfark_compressed_soundfont_create(xx_io_device *device, int64_t base_address)
{
    xx_sfark_compressed_soundfont *archive = (xx_sfark_compressed_soundfont *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfark_compressed_soundfont_init(archive, device, base_address);
    return archive;
}

void xx_sfark_compressed_soundfont_destroy(xx_sfark_compressed_soundfont *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfark_compressed_soundfont_free(xx_sfark_compressed_soundfont *archive)
{
    if (!archive) return;
    xx_sfark_compressed_soundfont_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfark_compressed_soundfont_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    sfk_header header;
    (void)pd;
    return sfk_parse_header(format, &header);
}

bool xx_sfark_compressed_soundfont_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    sfk_context *ctx;
    xx_sfark_compressed_soundfont *archive;
    if (!format) return false;
    ctx = (sfk_context *)xx_mem_alloc(sizeof(*ctx));
    if (!ctx) return false;
    if (!sfk_parse(format, ctx, true, pd)) {
        xx_mem_free(ctx);
        return false;
    }
    archive = (xx_sfark_compressed_soundfont *)format;
    archive->number_of_records = ctx->count;
    archive->unpacked_size = ctx->header.original_size;
    archive->method = ctx->header.method;
    format->number_of_archive_records = ctx->count;
    format->format_size = ctx->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    xx_mem_free(ctx);
    return true;
}

int64_t xx_sfark_compressed_soundfont_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_sfark_compressed_soundfont_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_sfark_compressed_soundfont_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_sfark_compressed_soundfont_handle_base_info(format, pd))
               ? ((xx_sfark_compressed_soundfont *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_sfark_compressed_soundfont_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    sfk_stream *stream;
    xx_archive_record_state *state;
    stream = (sfk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!sfk_parse(format, &stream->context, false, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    /* The measured extent, when base info already walked the stream. */
    if (format->base_info_handled && format->format_size > 0 && format->format_size <= stream->context.header.input_size)
        stream->context.archive_size = format->format_size;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sfk_stream_free;
    state->total_records = stream->context.count;
    if (!sfk_copy_options(&state->options, options) || !sfk_set_record(&state->current_record, &stream->context, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfark_compressed_soundfont_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_sfark_compressed_soundfont_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sfk_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (sfk_stream *)state->internal_state) || ++stream->index >= stream->context.count) {
        if (state) state->has_record = false;
        return false;
    }
    if (!sfk_set_record(&state->current_record, &stream->context, stream->index)) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

/* A licence or notes text: one zlib block. */
static bool sfk_unpack_text(Abstractformat *format, const sfk_text *t, xx_io_device *dst)
{
    uint8_t *zin = (uint8_t *)xx_mem_alloc(SFK_BLOCK_MAX);
    uint8_t *zout = (uint8_t *)xx_mem_alloc(SFK_BLOCK_MAX);
    bool ok = false;
    if (zin && zout && sfk_read_at(format->device, t->offset + 4, zin, t->packed)) {
        uint32_t size = sfk_inflate(zin, t->packed, zout);
        ok = size == t->size && (!dst || sfk_write_all(dst, zout, size));
    }
    if (zin) xx_mem_free(zin);
    if (zout) xx_mem_free(zout);
    return ok;
}

static bool sfk_unpack_index(Abstractformat *format, const sfk_context *ctx, size_t index, xx_io_device *dst, xx_pd_struct *pd)
{
    size_t kind = ctx->kinds[index];
    if (kind == 0U) return sfk_run(format, ctx, dst, true, NULL, pd);
    return sfk_unpack_text(format, &ctx->text[kind - 1U], dst);
}

bool xx_sfark_compressed_soundfont_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sfk_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    const char *name;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (sfk_stream *)state->internal_state) || stream->index >= stream->context.count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = sfk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return sfk_unpack_index(format, &stream->context, stream->index, NULL, pd);
    name = stream->context.names[stream->index];
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = sfk_unpack_index(format, &stream->context, stream->index, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfark_compressed_soundfont_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
