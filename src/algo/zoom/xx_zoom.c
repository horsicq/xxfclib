/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Port of XArchive/Algos/xzoomdecoder.cpp (plus the Zoom parameter set of
 * xlzhufdecoder.cpp, which only Zoom uses).  See the header for the container
 * layout and for the three traps.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/zoom/xx_zoom.h"

#define ZOOM_HEADER_SIZE 0x4c
#define ZOOM_CHUNK_HEADER_SIZE 42
#define ZOOM_CYLINDER_SIZE 0x2c00
#define ZOOM_SECTOR_SIZE 0x200
#define ZOOM_SECTORS_PER_CYLINDER 22
#define ZOOM_SLOTS 5
#define ZOOM_SLOT_UNUSED 0xff
#define ZOOM_MAX_OUTPUT 0x7fffffff

/* A chunk's every intermediate length is a u16, and the stored-sector total of
 * one record is at most 5 * 22 * 512 = 56320, so 64 KiB covers both stages. */
#define ZOOM_SCRATCH 0x10000

/* --- Zoom's LZHUF parameters (XLZHUFDecoder::getZoomOptions) ------------- */
#define ZOOM_NCHAR 317                  /* 0x13D */
#define ZOOM_T (ZOOM_NCHAR * 2 - 1)     /* 633   */
#define ZOOM_R (ZOOM_T - 1)             /* 632   */
#define ZOOM_MAX_FREQ 0x8000
#define ZOOM_EOF_CODE 316               /* 0x13C */
#define ZOOM_RING_SIZE 0x1000
#define ZOOM_RING_MASK (ZOOM_RING_SIZE - 1)

/* LHA's classic "-lh1-" position tables.  Read-only, so sharing them across
 * threads is safe. */
static const uint8_t zoom_d_code[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0a,
    0x0a, 0x0a, 0x0a, 0x0a, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0c, 0x0c, 0x0c, 0x0c, 0x0d, 0x0d, 0x0d, 0x0d, 0x0e, 0x0e, 0x0e, 0x0e,
    0x0f, 0x0f, 0x0f, 0x0f, 0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
    0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13, 0x14, 0x14, 0x14, 0x14,
    0x15, 0x15, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1a, 0x1a, 0x1b, 0x1b, 0x1c, 0x1c, 0x1d, 0x1d,
    0x1e, 0x1e, 0x1f, 0x1f, 0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
    0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27, 0x28, 0x28, 0x29, 0x29,
    0x2a, 0x2a, 0x2b, 0x2b, 0x2c, 0x2c, 0x2d, 0x2d, 0x2e, 0x2e, 0x2f, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
    0x3c, 0x3d, 0x3e, 0x3f
};

static const uint8_t zoom_d_len[16] = {
    3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8
};

typedef struct zoom_huf {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t buffer;
    int32_t count;
    bool eof;
    uint16_t freq[ZOOM_T + 1];
    int32_t son[ZOOM_T];
    int32_t prnt[ZOOM_T + ZOOM_NCHAR];
} zoom_huf;

/* Everything the decoder needs, owned by the caller - no module-level state. */
typedef struct zoom_ctx {
    zoom_huf huf;
    uint8_t ring[ZOOM_RING_SIZE];
    uint8_t stage_a[ZOOM_SCRATCH];
    uint8_t stage_b[ZOOM_SCRATCH];
} zoom_ctx;

/* ----------------------------------------------------------- bit reader --- */

static void zoom_huf_fill(zoom_huf *h)
{
    uint32_t byte = 0;

    if (h->pos < h->size) {
        byte = h->data[h->pos];
        ++h->pos;
    } else {
        h->eof = true;
    }

    h->buffer = h->buffer | (byte << ((8 - h->count) & 0x1f));
    h->count += 8;
}

static int32_t zoom_huf_get_bit(zoom_huf *h)
{
    uint32_t value;

    while (h->count == 0) {
        zoom_huf_fill(h);
        if (h->eof) break;
    }

    value = h->buffer;
    h->buffer = h->buffer << 1;
    --h->count;

    return (value & 0x8000u) ? 1 : 0;
}

static int32_t zoom_huf_get_byte(zoom_huf *h)
{
    uint32_t value;

    while (h->count < 8) {
        zoom_huf_fill(h);
        if (h->eof) break;
    }

    value = h->buffer;
    h->buffer = h->buffer << 8;
    h->count -= 8;

    return (int32_t)((value >> 8) & 0xffu);
}

/* ---------------------------------------------------- adaptive Huffman --- */

static void zoom_huf_start(zoom_huf *h)
{
    int32_t i;
    int32_t j;

    for (i = 0; i < ZOOM_NCHAR; ++i) {
        h->freq[i] = 1;
        h->son[i] = i + ZOOM_T;
        h->prnt[i + ZOOM_T] = i;
    }

    i = 0;
    j = ZOOM_NCHAR;

    while (j <= ZOOM_R) {
        h->freq[j] = (uint16_t)(h->freq[i] + h->freq[i + 1]);
        h->son[j] = i;
        h->prnt[i] = j;
        h->prnt[i + 1] = j;
        i += 2;
        ++j;
    }

    h->freq[ZOOM_T] = 0xffff;
    h->prnt[ZOOM_R] = 0;
}

static void zoom_huf_init(zoom_huf *h, const uint8_t *data, size_t size)
{
    xx_rt_memset(h, 0, sizeof(*h));
    h->data = data;
    h->size = size;
    zoom_huf_start(h);
}

static void zoom_huf_update(zoom_huf *h, int32_t symbol)
{
    int32_t c;

    if ((int32_t)h->freq[ZOOM_R] == ZOOM_MAX_FREQ) {
        /* DELIBERATE, do not "fix": Zoom's decoder simply stops re-weighting
         * from here.  The textbook LZHUF halves every frequency and rebuilds
         * the tree; Zoom does not, and the two are not interchangeable. */
        return;
    }

    c = h->prnt[symbol + ZOOM_T];

    do {
        uint16_t f;
        int32_t l;

        h->freq[c] = (uint16_t)(h->freq[c] + 1);
        f = h->freq[c];
        l = c + 1;

        if (h->freq[l] < f) {
            int32_t i;
            int32_t j;

            /* freq[T] is the 0xFFFF sentinel that stops this walk, so l may
             * legitimately reach T - one past the last node - before the
             * decrement pulls it back into range. */
            l = c + 2;
            while ((l <= ZOOM_T) && (h->freq[l] < f)) ++l;
            --l;
            if ((l < 0) || (l >= ZOOM_T)) return;

            h->freq[c] = h->freq[l];
            h->freq[l] = f;

            i = h->son[c];
            h->prnt[i] = l;
            if (i < ZOOM_T) h->prnt[i + 1] = l;

            j = h->son[l];
            h->son[l] = i;
            h->prnt[j] = c;
            if (j < ZOOM_T) h->prnt[j + 1] = c;

            h->son[c] = j;
            c = l;
        }

        c = h->prnt[c];
    } while (c != 0);
}

/* -1 on a short stream or a corrupt tree. */
static int32_t zoom_huf_decode_char(zoom_huf *h)
{
    int32_t code = h->son[ZOOM_R];

    while (code < ZOOM_T) {
        int32_t bit = zoom_huf_get_bit(h);
        int32_t index;
        if (h->eof) return -1;
        index = code + bit;
        if ((index < 0) || (index >= ZOOM_T)) return -1;
        code = h->son[index];
    }

    code -= ZOOM_T;
    if ((code < 0) || (code >= ZOOM_NCHAR)) return -1;
    zoom_huf_update(h, code);

    return code;
}

/* -1 on a short stream.  Distance variant 1: code << 6 plus the low six bits,
 * d_len - 2 further raw bits. */
static int32_t zoom_huf_decode_position(zoom_huf *h)
{
    int32_t byte = zoom_huf_get_byte(h);
    int32_t base;
    int32_t extra;

    if (h->eof) return -1;
    byte &= 0xff;

    base = ((int32_t)zoom_d_code[byte]) << 6;
    extra = (int32_t)zoom_d_len[byte >> 4] - 2;

    while (extra > 0) {
        --extra;
        byte = ((byte << 1) + zoom_huf_get_bit(h)) & 0xffff;
        if (h->eof) return -1;
    }

    return base | (byte & 0x3f);
}

/* ---------------------------------------------------------- LZHUF core --- */

static bool zoom_lzhuf_decode(zoom_ctx *ctx, const uint8_t *input,
                              size_t input_size, uint8_t *output,
                              size_t output_size)
{
    size_t produced = 0;
    int32_t ring = 0;
    bool finished = false;

    xx_rt_memset(ctx->ring, 0, ZOOM_RING_SIZE);
    zoom_huf_init(&ctx->huf, input, input_size);

    /* Zoom's alphabet has an end symbol, so the loop is driven by it and NOT
     * by the expected length; an overshoot is a corrupt stream, never a
     * truncation. */
    for (;;) {
        int32_t code = zoom_huf_decode_char(&ctx->huf);
        int32_t distance;
        int32_t source;
        int32_t length;

        if (code < 0) break;

        if (code < 0x100) {
            if (produced >= output_size) return false;
            output[produced++] = (uint8_t)code;
            ctx->ring[ring] = (uint8_t)code;
            ring = (ring + 1) & ZOOM_RING_MASK;
            continue;
        }

        if (code == ZOOM_EOF_CODE) {
            finished = true;
            break;
        }

        distance = zoom_huf_decode_position(&ctx->huf);
        if (distance < 0) break;

        /* DELIBERATE: match bias 0 (source is cursor - distance, with NO -1)
         * and length bias 0 (length is symbol - 255, with NO THRESHOLD).  Both
         * differ from textbook LZHUF and both are load-bearing. */
        source = (ring - distance) & ZOOM_RING_MASK;
        length = code - 0xff;
        if (length <= 0) return false;

        while (length > 0) {
            uint8_t byte;
            if (produced >= output_size) return false;
            byte = ctx->ring[source];
            output[produced++] = byte;
            ctx->ring[ring] = byte;
            ring = (ring + 1) & ZOOM_RING_MASK;
            source = (source + 1) & ZOOM_RING_MASK;
            --length;
        }
    }

    if (!finished) return false;

    return produced == output_size;
}

/* ------------------------------------------------------------ RLE core --- */

static bool zoom_rle_core(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *produced_out)
{
    size_t declared;
    size_t produced = 0;
    size_t pos = 4;
    uint8_t escape;

    *produced_out = 0;
    if (input_size < 4) return false;

    /* The packer's own BE24 length.  It has to agree with the record's final
     * length exactly - a mismatch means the LZHUF stage handed over the wrong
     * bytes, not that this chunk is merely unusual. */
    declared = ((size_t)input[0] << 16) | ((size_t)input[1] << 8) |
               (size_t)input[2];
    if (declared != output_size) return false;

    escape = input[3];

    while (pos < input_size) {
        uint32_t count;

        if (input[pos] != escape) {
            if (produced >= output_size) return false;
            output[produced++] = input[pos];
            ++pos;
            continue;
        }

        if ((pos + 1) >= input_size) return false;

        count = input[pos + 1];

        if (count == 0) {
            /* An escaped escape: one literal copy of the escape byte. */
            if (produced >= output_size) return false;
            output[produced++] = escape;
            pos += 2;
        } else {
            uint8_t value;
            uint32_t i;
            if ((pos + 2) >= input_size) return false;
            value = input[pos + 2];
            /* DELIBERATE off-by-one: a run byte of n emits n + 1 bytes. */
            if (((size_t)count + 1) > (output_size - produced)) return false;
            for (i = 0; i <= count; ++i) output[produced++] = value;
            pos += 3;
        }
    }

    *produced_out = produced;

    return produced == output_size;
}

bool xx_zoom_rle_decode_memory(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               size_t *written)
{
    size_t produced = 0;
    bool ok;

    if (written) *written = 0;
    if (!input || (!output && (output_size != 0))) return false;
    if (output_size > ZOOM_MAX_OUTPUT) return false;

    ok = zoom_rle_core(input, input_size, output, output_size, &produced);
    if (!ok) return false;
    if (written) *written = produced;

    return true;
}

/* --------------------------------------------------------- the container -- */

static uint32_t zoom_be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

static uint32_t zoom_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int32_t zoom_popcount(uint32_t mask)
{
    int32_t result = 0;
    int32_t i;

    for (i = 0; i < ZOOM_SECTORS_PER_CYLINDER; ++i) {
        if (mask & (1u << i)) ++result;
    }

    return result;
}

static bool zoom_parse_header(const uint8_t *file, size_t size,
                              uint32_t *first, uint32_t *last,
                              bool *protected_out, size_t *chunks_offset)
{
    size_t note;
    size_t offset;
    uint32_t nfirst;
    uint32_t nlast;

    if (size < ZOOM_HEADER_SIZE) return false;
    if ((file[0] != 'Z') || (file[1] != 'O') || (file[2] != 'M') ||
        (file[3] != '5')) {
        return false;
    }
    if (file[6] != 5) return false;

    nfirst = file[4];
    nlast = file[5];
    if (nlast < nfirst) return false;

    note = (size_t)zoom_be32(file + 0x1c);

    offset = ZOOM_HEADER_SIZE;
    if (note != 0) {
        /* The note block carries its own four-byte checksum after the text. */
        if (note > (size - ZOOM_HEADER_SIZE - 4)) return false;
        offset += note + 4;
    }

    *first = nfirst;
    *last = nlast;
    *protected_out = (file[0x24] != 0);
    *chunks_offset = offset;

    return true;
}

/* One routine for both entry points: output == NULL discards, so the measure
 * and the decode can never disagree. */
static bool zoom_emit(uint8_t *output, size_t limit, size_t *pos,
                      const uint8_t *source, size_t count)
{
    if (count > (limit - *pos)) return false;
    if (output) {
        if (source) {
            xx_rt_memcpy(output + *pos, source, count);
        } else {
            xx_rt_memset(output + *pos, 0, count);
        }
    }
    *pos += count;

    return true;
}

static bool zoom_image_core(zoom_ctx *ctx, const uint8_t *input,
                            size_t input_size, uint8_t *output,
                            size_t output_capacity, size_t max_output,
                            size_t *consumed_out, size_t *produced_out)
{
    uint32_t first = 0;
    uint32_t last = 0;
    bool is_protected = false;
    size_t offset = 0;
    size_t cylinders;
    size_t image_size;
    size_t produced = 0;
    size_t current = 0;

    *consumed_out = 0;
    *produced_out = 0;

    if (!zoom_parse_header(input, input_size, &first, &last, &is_protected,
                           &offset)) {
        return false;
    }
    if (is_protected) return false;

    cylinders = (size_t)(last - first) + 1;
    image_size = cylinders * ZOOM_CYLINDER_SIZE;
    if (image_size > (size_t)ZOOM_MAX_OUTPUT) return false;
    if (image_size > max_output) return false;
    if (output && (image_size > output_capacity)) return false;

    /* The reference walks cylinders from zero regardless of firstCylinder. */
    while ((input_size - offset) >= ZOOM_CHUNK_HEADER_SIZE) {
        const uint8_t *record = input + offset;
        uint8_t cyl[ZOOM_SLOTS];
        uint32_t mask[ZOOM_SLOTS];
        size_t packed_size;
        size_t middle_size;
        size_t final_size;
        uint32_t flag;
        const uint8_t *blob;
        const uint8_t *buffer;
        size_t buffer_size;
        size_t total = 0;
        size_t available;
        size_t source_pos = 0;
        int32_t i;

        for (i = 0; i < ZOOM_SLOTS; ++i) {
            cyl[i] = record[i];
            mask[i] = zoom_be32(record + 6 + (i * 4));
        }
        packed_size = (size_t)zoom_be16(record + 0x1a);
        middle_size = (size_t)zoom_be16(record + 0x1c);
        final_size = (size_t)zoom_be16(record + 0x1e);
        flag = zoom_be16(record + 0x20);
        offset += ZOOM_CHUNK_HEADER_SIZE;

        if (packed_size > (input_size - offset)) return false;
        blob = input + offset;
        offset += packed_size;

        for (i = 0; i < ZOOM_SLOTS; ++i) {
            if (cyl[i] == ZOOM_SLOT_UNUSED) continue;
            total += (size_t)ZOOM_SECTOR_SIZE * (size_t)zoom_popcount(mask[i]);
        }
        /* A record with no RLE stage does not state its final length; the
         * stored sectors are the length. */
        if (middle_size == 0) final_size = total;

        buffer = blob;
        buffer_size = packed_size;

        if (flag != 0) {
            size_t want = (middle_size != 0) ? middle_size : final_size;
            if (want > ZOOM_SCRATCH) return false;
            if (!zoom_lzhuf_decode(ctx, buffer, buffer_size, ctx->stage_a,
                                   want)) {
                return false;
            }
            buffer = ctx->stage_a;
            buffer_size = want;
        }
        if (middle_size != 0) {
            size_t got = 0;
            if (final_size > ZOOM_SCRATCH) return false;
            if (!zoom_rle_core(buffer, buffer_size, ctx->stage_b, final_size,
                               &got)) {
                return false;
            }
            buffer = ctx->stage_b;
            buffer_size = final_size;
        }

        if (total < final_size) final_size = total;
        if (buffer_size < final_size) return false;

        available = final_size;

        for (i = 0; i < ZOOM_SLOTS; ++i) {
            uint32_t bits;
            int32_t k;

            if (cyl[i] == ZOOM_SLOT_UNUSED) continue;

            while (current < (size_t)cyl[i]) {
                if (!zoom_emit(output, image_size, &produced, NULL,
                               ZOOM_CYLINDER_SIZE)) {
                    return false;
                }
                ++current;
            }

            bits = mask[i];
            for (k = 0; k < ZOOM_SECTORS_PER_CYLINDER; ++k) {
                if (bits & 1u) {
                    if (available < ZOOM_SECTOR_SIZE) return false;
                    if (!zoom_emit(output, image_size, &produced,
                                   buffer + source_pos, ZOOM_SECTOR_SIZE)) {
                        return false;
                    }
                    source_pos += ZOOM_SECTOR_SIZE;
                    available -= ZOOM_SECTOR_SIZE;
                } else {
                    if (!zoom_emit(output, image_size, &produced, NULL,
                                   ZOOM_SECTOR_SIZE)) {
                        return false;
                    }
                }
                bits >>= 1;
            }
            ++current;
        }

        /* Every stored sector of the record has to be consumed exactly. */
        if (available != 0) return false;
    }

    *consumed_out = offset;

    while (current < cylinders) {
        if (!zoom_emit(output, image_size, &produced, NULL,
                       ZOOM_CYLINDER_SIZE)) {
            return false;
        }
        ++current;
    }

    if (produced != image_size) return false;
    *produced_out = produced;

    return true;
}

static bool zoom_run(const uint8_t *input, size_t input_size, uint8_t *output,
                     size_t output_capacity, size_t max_output,
                     size_t *consumed, size_t *produced)
{
    zoom_ctx *ctx;
    bool ok;

    ctx = (zoom_ctx *)xx_mem_alloc(sizeof(zoom_ctx));
    if (!ctx) return false;

    ok = zoom_image_core(ctx, input, input_size, output, output_capacity,
                         max_output, consumed, produced);

    xx_mem_free(ctx);

    return ok;
}

bool xx_zoom_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           size_t *written)
{
    size_t consumed = 0;
    size_t produced = 0;

    if (written) *written = 0;
    if (!input || !output || (output_size == 0)) return false;

    if (!zoom_run(input, input_size, output, output_size, output_size,
                  &consumed, &produced)) {
        return false;
    }

    if (written) *written = produced;

    return true;
}

bool xx_zoom_scan_memory(const uint8_t *input, size_t input_size,
                         size_t max_output, size_t *consumed, size_t *produced)
{
    size_t local_consumed = 0;
    size_t local_produced = 0;

    if (consumed) *consumed = 0;
    if (produced) *produced = 0;
    if (!input) return false;

    if (!zoom_run(input, input_size, NULL, 0, max_output, &local_consumed,
                  &local_produced)) {
        return false;
    }

    if (consumed) *consumed = local_consumed;
    if (produced) *produced = local_produced;

    return true;
}
