/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Sydex self-extracting diskette image ("WB" text stage + "SXD" image
 * stage in the DOS overlay of an MZ + NE extractor).  The field tables are
 * in xx_sfx_sydex_diskette_image.h.
 *
 * The container walk (overlay from the MZ size words, the stage-1 checks,
 * the self-checking stage-2 header, the per-track {CRC, length} blocks and
 * the CopyQM-style RLE) follows XArchive's sfx/xsydexsfxarchive.cpp (MIT,
 * same author).  The one addition is that the stage-2 header is looked for
 * first where the "WB" size words say stage 1 ends, before the reference's
 * scan.
 *
 * The LZHUF track codec is written here rather than taken from
 * src/algo/lzhuf: that module presets its ring to 0x20 and has no parameter
 * for the 0x00 preset this format needs, and src/algo/lzh's -lh1- presets
 * only part of the ring and insists on a byte-exact stream end.  The model
 * is the textbook one (Yoshizaki's adaptive Huffman tree over 314 symbols,
 * the LHA position code); the bit reader is strict, so a block can never
 * lend bits to the one behind it.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_sydex_diskette_image/xx_sfx_sydex_diskette_image.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: picks up the real file type as soon as the
 * enumerator (and its alias macro) exist in xxfc_defs.h. */
#ifdef SFX_SYDEX_DISKETTE_IMAGE
#define XX_SFX_SYDEX_DISKETTE_IMAGE_FILE_TYPE \
    XX_FILE_TYPE_SFX_SYDEX_DISKETTE_IMAGE
#else
#define XX_SFX_SYDEX_DISKETTE_IMAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SXD_MZ_MIN_IMAGE 0x40
#define SXD_WB_SIZE 10U         /* "WB", three u16, first stream word */
#define SXD_HEADER_SIZE 33U
#define SXD_BLOCK_HEADER 4U
#define SXD_MIN_TRACK 128U
#define SXD_MAX_TRACK 0x8000U
/* Bounds the header scan; the stage-1 text is under 3 KiB in every known
 * extractor, so this is far above any real offset and keeps the probe
 * bounded on anything else. */
#define SXD_MAX_SCAN 0x20000U
/* A 2.88 MB diskette is 2.9 MB; this only rules out garbage geometry. */
#define SXD_MAX_IMAGE (UINT64_C(64) * 1024U * 1024U)
#define SXD_MEMBER_NAME "image.img"

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static uint32_t sxd_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static int32_t sxd_sle16(const uint8_t *bytes) {
    uint32_t value = sxd_le16(bytes);
    return (value & 0x8000U) ? (int32_t)value - 0x10000 : (int32_t)value;
}

static bool sxd_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

uint16_t xx_sfx_sydex_diskette_image_crc16(const uint8_t *data,
                                           size_t size) {
    uint32_t crc = 0U;
    size_t index;
    unsigned bit;
    if (!data) return 0U;
    for (index = 0U; index < size; ++index) {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xA001U) : (crc >> 1U);
    }
    return (uint16_t)crc;
}

/* ---------------------------------------------------------------------- */
/* LZHUF track codec                                                       */

#define SXD_N 4096U           /* ring size, a power of two */
#define SXD_F 60              /* longest match */
#define SXD_THRESHOLD 2       /* matches are THRESHOLD+1..F bytes */
#define SXD_NCHAR (256 - SXD_THRESHOLD + SXD_F) /* 314 symbols */
#define SXD_T (SXD_NCHAR * 2 - 1)               /* 627 tree nodes */
#define SXD_R (SXD_T - 1)                       /* root */
#define SXD_MAX_FREQ 0x8000U

typedef struct sxd_lzhuf_s {
    uint32_t freq[SXD_T + 1]; /* freq[T] is the 0xFFFF sentinel */
    int32_t son[SXD_T];
    int32_t prnt[SXD_T + SXD_NCHAR];
    uint8_t d_code[256];
    uint8_t d_len[256];
    uint8_t ring[SXD_N];
    const uint8_t *input;
    size_t bit_count;         /* bits in the block */
    size_t bit_position;      /* next bit to read */
} sxd_lzhuf;

/* The position code: the high six bits of a 12-bit distance are coded in
 * 3..8 bits (value 0 in 3, 1..3 in 4, 4..11 in 5, 12..23 in 6, 24..47 in 7,
 * 48..63 in 8); indexed by the first eight bits read, these tables give the
 * high part and the code length.  Built here instead of spelt out. */
static void sxd_lzhuf_tables(sxd_lzhuf *z) {
    static const uint8_t first_of_length[7] = {0, 1, 4, 12, 24, 48, 64};
    uint32_t value, index = 0U;
    for (value = 0U; value < 64U; ++value) {
        uint32_t length = 3U, count;
        while (length < 8U && value >= first_of_length[length - 2U])
            ++length;
        count = 1U << (8U - length);
        while (count-- != 0U && index < 256U) {
            z->d_code[index] = (uint8_t)value;
            z->d_len[index] = (uint8_t)length;
            ++index;
        }
    }
}

static void sxd_lzhuf_start(sxd_lzhuf *z, const uint8_t *input,
                            size_t input_size) {
    int32_t i, j;
    for (i = 0; i < SXD_NCHAR; ++i) {
        z->freq[i] = 1U;
        z->son[i] = i + SXD_T;
        z->prnt[i + SXD_T] = i;
    }
    for (i = 0, j = SXD_NCHAR; j <= SXD_R; i += 2, ++j) {
        z->freq[j] = z->freq[i] + z->freq[i + 1];
        z->son[j] = i;
        z->prnt[i] = j;
        z->prnt[i + 1] = j;
    }
    z->freq[SXD_T] = 0xFFFFU;
    z->prnt[SXD_R] = 0;
    xx_rt_memset(z->ring, 0, sizeof(z->ring));
    z->input = input;
    z->bit_count = input_size * 8U;
    z->bit_position = 0U;
}

/* -1 once the block has no bits left: a block never borrows from the next
 * one, which is exactly what the extractor's framing implies. */
static int sxd_lzhuf_bit(sxd_lzhuf *z) {
    size_t position = z->bit_position;
    if (position >= z->bit_count) return -1;
    z->bit_position = position + 1U;
    return (z->input[position >> 3U] >> (7U - (position & 7U))) & 1;
}

/* Halve every leaf and rebuild the internal nodes in frequency order. */
static void sxd_lzhuf_rebuild(sxd_lzhuf *z) {
    int32_t i, j, k, n;
    for (i = 0, j = 0; i < SXD_T; ++i) {
        if (z->son[i] >= SXD_T) {
            z->freq[j] = (z->freq[i] + 1U) >> 1U;
            z->son[j] = z->son[i];
            ++j;
        }
    }
    for (i = 0, j = SXD_NCHAR; j < SXD_T; i += 2, ++j) {
        uint32_t f = z->freq[i] + z->freq[i + 1];
        for (k = j - 1; k >= 0 && f < z->freq[k]; --k) {
        }
        ++k;
        for (n = j; n > k; --n) {
            z->freq[n] = z->freq[n - 1];
            z->son[n] = z->son[n - 1];
        }
        z->freq[k] = f;
        z->son[k] = i;
    }
    for (i = 0; i < SXD_T; ++i) {
        k = z->son[i];
        if (k >= SXD_T) {
            z->prnt[k] = i;
        } else {
            z->prnt[k] = i;
            z->prnt[k + 1] = i;
        }
    }
}

/* Count one more occurrence of @p symbol and restore the sibling order.
 * The tree only ever reflects which symbols were decoded, so it cannot be
 * corrupted by a hostile stream; the index checks are belt and braces. */
static bool sxd_lzhuf_update(sxd_lzhuf *z, int32_t symbol) {
    int32_t c, steps = 0;
    if (z->freq[SXD_R] == SXD_MAX_FREQ) sxd_lzhuf_rebuild(z);
    c = z->prnt[symbol + SXD_T];
    do {
        uint32_t k;
        int32_t l;
        if (c < 0 || c >= SXD_T || ++steps > SXD_T) return false;
        k = ++z->freq[c];
        l = c + 1;
        if (k > z->freq[l]) {
            int32_t i, j;
            while (l < SXD_T && k > z->freq[l + 1]) ++l;
            if (l >= SXD_T) return false;
            z->freq[c] = z->freq[l];
            z->freq[l] = k;
            i = z->son[c];
            z->prnt[i] = l;
            if (i < SXD_T) z->prnt[i + 1] = l;
            j = z->son[l];
            z->son[l] = i;
            z->prnt[j] = c;
            if (j < SXD_T) z->prnt[j + 1] = c;
            z->son[c] = j;
            c = l;
        }
        c = z->prnt[c];
    } while (c != 0);
    return true;
}

static int32_t sxd_lzhuf_symbol(sxd_lzhuf *z) {
    int32_t c = z->son[SXD_R];
    int32_t depth = 0;
    while (c < SXD_T) {
        int bit = sxd_lzhuf_bit(z);
        if (bit < 0 || c < 0 || c + bit >= SXD_T || ++depth > SXD_T)
            return -1;
        c = z->son[c + bit];
    }
    c -= SXD_T;
    if (c < 0 || c >= SXD_NCHAR || !sxd_lzhuf_update(z, c)) return -1;
    return c;
}

static int32_t sxd_lzhuf_position(sxd_lzhuf *z) {
    uint32_t byte = 0U, extra;
    int i;
    for (i = 0; i < 8; ++i) {
        int bit = sxd_lzhuf_bit(z);
        if (bit < 0) return -1;
        byte = (byte << 1U) | (uint32_t)bit;
    }
    extra = (uint32_t)z->d_len[byte] - 2U;
    {
        uint32_t high = (uint32_t)z->d_code[byte] << 6U;
        while (extra-- != 0U) {
            int bit = sxd_lzhuf_bit(z);
            if (bit < 0) return -1;
            byte = (byte << 1U) | (uint32_t)bit;
        }
        return (int32_t)(high | (byte & 0x3FU));
    }
}

static bool sxd_lzhuf_run(sxd_lzhuf *z, const uint8_t *input,
                          size_t input_size, uint8_t *output,
                          size_t output_size) {
    size_t produced = 0U;
    uint32_t cursor = 0U;
    if (!z || (!input && input_size != 0U) || (!output && output_size != 0U) ||
        input_size > SIZE_MAX / 8U)
        return false;
    sxd_lzhuf_start(z, input, input_size);
    while (produced < output_size) {
        int32_t symbol = sxd_lzhuf_symbol(z);
        if (symbol < 0) return false;
        if (symbol < 256) {
            output[produced++] = (uint8_t)symbol;
            z->ring[cursor] = (uint8_t)symbol;
            cursor = (cursor + 1U) & (SXD_N - 1U);
        } else {
            int32_t distance = sxd_lzhuf_position(z);
            uint32_t source, length;
            if (distance < 0) return false;
            /* A slot the stream has not written yet holds the 0x00 preset,
             * which the encoder counted on; it is not an overrun. */
            source = (cursor - (uint32_t)distance - 1U) & (SXD_N - 1U);
            length = (uint32_t)(symbol - 255 + SXD_THRESHOLD);
            /* No end symbol: the track size is the only stop, so a last
             * match that overshoots it is cut, as the extractor does. */
            while (length-- != 0U && produced < output_size) {
                uint8_t value = z->ring[source];
                output[produced++] = value;
                z->ring[cursor] = value;
                cursor = (cursor + 1U) & (SXD_N - 1U);
                source = (source + 1U) & (SXD_N - 1U);
            }
        }
    }
    return true;
}

bool xx_sfx_sydex_diskette_image_lzhuf_decode(const uint8_t *input,
                                              size_t input_size,
                                              uint8_t *output,
                                              size_t output_size) {
    sxd_lzhuf *z;
    bool result;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    z = (sxd_lzhuf *)xx_mem_alloc(sizeof(*z));
    if (!z) return false;
    sxd_lzhuf_tables(z);
    result = sxd_lzhuf_run(z, input, input_size, output, output_size);
    xx_mem_free(z);
    return result;
}

/* CopyQM-style RLE framed by the block length rather than an end token. */
static bool sxd_rle_decode(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size) {
    size_t position = 0U, produced = 0U;
    while (position < input_size) {
        int32_t token;
        if (input_size - position < 2U) return false;
        token = sxd_sle16(input + position);
        position += 2U;
        if (token >= 0) {
            size_t count = (size_t)token;
            if (count > input_size - position ||
                count > output_size - produced)
                return false;
            if (count != 0U)
                xx_rt_memcpy(output + produced, input + position, count);
            position += count;
            produced += count;
        } else {
            size_t count = (size_t)(-token);
            if (position >= input_size || count > output_size - produced)
                return false;
            xx_rt_memset(output + produced, input[position], count);
            position += 1U;
            produced += count;
        }
    }
    return produced == output_size;
}

/* ---------------------------------------------------------------------- */
/* Container                                                               */

typedef struct sxd_context_s {
    int64_t size;            /* device bytes from base_address on */
    int64_t overlay;         /* relative to base_address */
    int64_t header;          /* relative */
    int64_t data;            /* relative */
    int64_t end;             /* relative end of the last block, or size */
    uint64_t image_size;
    uint32_t track_size;
    uint32_t track_count;
    uint32_t comment_size;
    uint8_t sectors, heads, cylinders, stored;
    bool scanned;
    bool truncated;
} sxd_context;

typedef struct sxd_stream_s {
    sxd_context context;
    size_t index;
    size_t count;
} sxd_stream;

/* The stage-2 checks of the reference: signature, header CRC, not
 * encrypted, a plausible track size and geometry, a non-negative text. */
static bool sxd_header_ok(const uint8_t *h, sxd_context *out) {
    uint32_t track_size;
    int32_t comment;
    if (h[0] != 'S' || h[1] != 'X' || h[2] != 'D') return false;
    if (xx_sfx_sydex_diskette_image_crc16(h, 0x1FU) != sxd_le16(h + 0x1FU))
        return false;
    if (sxd_sle16(h + 0x0FU) != 0) return false;
    track_size = sxd_le16(h + 3U);
    if (track_size < SXD_MIN_TRACK || track_size > SXD_MAX_TRACK) return false;
    if (h[5] == 0U || h[6] == 0U || h[7] == 0U || h[8] == 0U || h[8] > h[7])
        return false;
    comment = sxd_sle16(h + 0x13U);
    if (comment < 0) return false;
    if (out) {
        out->track_size = track_size;
        out->sectors = h[5];
        out->heads = h[6];
        out->cylinders = h[7];
        out->stored = h[8];
        out->track_count = (uint32_t)h[6] * (uint32_t)h[8];
        out->comment_size = (uint32_t)comment;
        out->image_size = (uint64_t)out->track_count * track_size;
    }
    return true;
}

/* The reference's scan for the header behind the stage-1 text.  Bounded by
 * SXD_MAX_SCAN; reads the window once. */
static bool sxd_scan_header(xx_io_device *device, int64_t overlay_abs,
                            int64_t overlay_size, sxd_context *context) {
    int64_t last = overlay_size - (int64_t)SXD_HEADER_SIZE;
    size_t window, index;
    uint8_t *buffer;
    bool found = false;
    if (last < (int64_t)SXD_WB_SIZE) return false;
    if (last > (int64_t)SXD_MAX_SCAN) last = (int64_t)SXD_MAX_SCAN;
    window = (size_t)last + SXD_HEADER_SIZE;
    buffer = (uint8_t *)xx_mem_alloc(window);
    if (!buffer) return false;
    if (sxd_read_at(device, overlay_abs, buffer, window)) {
        for (index = SXD_WB_SIZE; index <= (size_t)last; ++index) {
            if (buffer[index] != 'S' || buffer[index + 1U] != 'X' ||
                buffer[index + 2U] != 'D')
                continue;
            if (sxd_header_ok(buffer + index, context)) {
                context->header = context->overlay + (int64_t)index;
                found = true;
                break;
            }
        }
    }
    xx_mem_free(buffer);
    return found;
}

/* Walk the {CRC, length} block headers without decoding anything, to find
 * where the payload ends.  Running off the file marks it truncated. */
static bool sxd_walk(Abstractformat *format, sxd_context *context,
                     xx_pd_struct *pd) {
    int64_t position = context->data;
    uint32_t track;
    for (track = 0U; track < context->track_count; ++track) {
        uint8_t block[SXD_BLOCK_HEADER];
        int32_t length;
        if ((track & 0xFFU) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        if (context->size - position < (int64_t)SXD_BLOCK_HEADER ||
            !sxd_read_at(format->device, format->base_address + position,
                         block, sizeof(block))) {
            context->truncated = true;
            break;
        }
        length = sxd_sle16(block + 2U);
        if (length == 0) return false;
        position += (int64_t)SXD_BLOCK_HEADER;
        if (length < 0) length = -length;
        if (context->size - position < (int64_t)length) {
            context->truncated = true;
            break;
        }
        position += (int64_t)length;
    }
    context->end = context->truncated ? context->size : position;
    return true;
}

static bool sxd_parse(Abstractformat *format, sxd_context *out, bool walk,
                      xx_pd_struct *pd) {
    uint8_t mz[6];
    uint8_t wb[SXD_WB_SIZE];
    uint8_t header[SXD_HEADER_SIZE];
    sxd_context context;
    int64_t total, image_end, overlay_size, stage1;
    uint32_t last_page, pages, first_word;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&context, sizeof(context));
    context.size = total - format->base_address;
    if (context.size < SXD_MZ_MIN_IMAGE ||
        !sxd_read_at(format->device, format->base_address, mz, sizeof(mz)))
        return false;
    if (mz[0] != 'M' || mz[1] != 'Z') return false;
    last_page = sxd_le16(mz + 2U);
    pages = sxd_le16(mz + 4U);
    if (pages == 0U || last_page >= 0x200U) return false;
    image_end = (int64_t)(pages - 1U) * 512 +
                (int64_t)(last_page ? last_page : 512U);
    if (image_end < SXD_MZ_MIN_IMAGE || image_end > context.size) return false;
    overlay_size = context.size - image_end;
    if (overlay_size < (int64_t)(SXD_WB_SIZE + SXD_HEADER_SIZE)) return false;
    context.overlay = image_end;

    /* Stage 1: every field the reference detector tests. */
    if (!sxd_read_at(format->device, format->base_address + image_end, wb,
                     sizeof(wb)))
        return false;
    if (wb[0] != 'W' || wb[1] != 'B') return false;
    last_page = sxd_le16(wb + 2U);
    pages = sxd_le16(wb + 4U);
    first_word = sxd_le16(wb + 8U);
    if (last_page >= 0x200U || pages == 0U || sxd_le16(wb + 6U) == 0U)
        return false;
    if (first_word != 0xE4D5U && first_word != 0xE5CCU) return false;

    /* Stage 2: where the WB size words put it, else by the scan. */
    stage1 = (int64_t)(pages - 1U) * 512 +
             (int64_t)(last_page ? last_page : 512U);
    if (stage1 >= (int64_t)SXD_WB_SIZE &&
        stage1 <= overlay_size - (int64_t)SXD_HEADER_SIZE &&
        sxd_read_at(format->device,
                    format->base_address + image_end + stage1, header,
                    sizeof(header)) &&
        sxd_header_ok(header, &context)) {
        context.header = image_end + stage1;
    } else {
        if (!sxd_scan_header(format->device, format->base_address + image_end,
                             overlay_size, &context))
            return false;
        context.scanned = true;
    }
    if (context.image_size == 0U || context.image_size > SXD_MAX_IMAGE)
        return false;
    context.data = context.header + (int64_t)SXD_HEADER_SIZE +
                   (int64_t)context.comment_size;
    /* The first block header has to be there and has to be non-empty. */
    if (context.size - context.data < (int64_t)SXD_BLOCK_HEADER) return false;
    {
        uint8_t block[SXD_BLOCK_HEADER];
        if (!sxd_read_at(format->device, format->base_address + context.data,
                         block, sizeof(block)) ||
            sxd_sle16(block + 2U) == 0)
            return false;
    }
    context.end = context.size;
    if (walk && !sxd_walk(format, &context, pd)) return false;
    *out = context;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Decoding                                                                */

/* Decoded tracks are staged and written in large pieces: one write per
 * track is measurably slow on file systems with on-access scanning. */
#define SXD_STAGE 0x40000U

typedef struct sxd_work_s {
    sxd_lzhuf lzhuf;
    uint8_t packed[SXD_MAX_TRACK];
    uint8_t stage[SXD_STAGE];
} sxd_work;

static bool sxd_write_all(xx_io_device *destination, const uint8_t *data,
                          size_t size) {
    size_t written = 0U;
    if (!destination) return true;
    while (written < size) {
        ssize_t amount =
            xx_io_write(destination, data + written, size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

static bool sxd_decode(Abstractformat *format, const sxd_context *context,
                       xx_io_device *destination, xx_pd_struct *pd) {
    sxd_context check;
    sxd_work *work;
    int64_t position;
    uint32_t track;
    size_t staged = 0U;
    bool result = false;
    if (!format || !context || context->truncated ||
        !sxd_parse(format, &check, true, pd) || check.truncated ||
        check.header != context->header || check.data != context->data ||
        check.end != context->end ||
        check.track_count != context->track_count ||
        check.track_size != context->track_size)
        return false;
    work = (sxd_work *)xx_mem_alloc(sizeof(*work));
    if (!work) return false;
    sxd_lzhuf_tables(&work->lzhuf);
    position = check.data;
    for (track = 0U; track < check.track_count; ++track) {
        uint8_t block[SXD_BLOCK_HEADER];
        uint8_t *output;
        int32_t length;
        size_t packed;
        bool decoded;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        /* track_size <= SXD_MAX_TRACK < SXD_STAGE, so a flushed stage
         * always has room for the next track. */
        if (SXD_STAGE - staged < check.track_size) {
            if (!sxd_write_all(destination, work->stage, staged)) goto done;
            staged = 0U;
        }
        output = work->stage + staged;
        if (check.end - position < (int64_t)SXD_BLOCK_HEADER ||
            !sxd_read_at(format->device, format->base_address + position,
                         block, sizeof(block)))
            goto done;
        position += (int64_t)SXD_BLOCK_HEADER;
        length = sxd_sle16(block + 2U);
        if (length == 0) goto done;
        packed = (size_t)(length < 0 ? -length : length);
        if (packed > sizeof(work->packed) ||
            check.end - position < (int64_t)packed ||
            !sxd_read_at(format->device, format->base_address + position,
                         work->packed, packed))
            goto done;
        position += (int64_t)packed;
        if (length > 0)
            decoded = sxd_lzhuf_run(&work->lzhuf, work->packed, packed,
                                    output, check.track_size);
        else
            decoded = sxd_rle_decode(work->packed, packed, output,
                                     check.track_size);
        if (!decoded ||
            xx_sfx_sydex_diskette_image_crc16(output, check.track_size) !=
                sxd_le16(block))
            goto done;
        staged += check.track_size;
    }
    result = (position == check.end) &&
             sxd_write_all(destination, work->stage, staged);
done:
    xx_mem_free(work);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static void sxd_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool sxd_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *sxd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sxd_set_record(Abstractformat *format, xx_archive_record *record,
                           const sxd_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + context->header;
    record->header_size =
        (int64_t)SXD_HEADER_SIZE + (int64_t)context->comment_size;
    record->data_offset = format->base_address + context->data;
    record->compressed_size = context->end - context->data;
    return xx_archive_record_set_original_name(record, SXD_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)(context->end -
                                                     context->data)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          context->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_sfx_sydex_diskette_image_init(xx_sfx_sydex_diskette_image *archive,
                                      xx_io_device *device,
                                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_SYDEX_DISKETTE_IMAGE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdos-program");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_sfx_sydex_diskette_image_check_is_valid;
    archive->format.handle_base_info =
        xx_sfx_sydex_diskette_image_handle_base_info;
    archive->format.get_format_size =
        xx_sfx_sydex_diskette_image_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_sydex_diskette_image_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_sydex_diskette_image_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_sydex_diskette_image_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_sydex_diskette_image_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_sydex_diskette_image_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_sydex_diskette_image_free_archive_records_reading;
    archive->overlay_offset = -1;
    archive->header_offset = -1;
    archive->data_offset = -1;
    archive->payload_end = -1;
}

xx_sfx_sydex_diskette_image *xx_sfx_sydex_diskette_image_create(
    xx_io_device *device, int64_t base_address) {
    xx_sfx_sydex_diskette_image *archive =
        (xx_sfx_sydex_diskette_image *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_sydex_diskette_image_init(archive, device, base_address);
    return archive;
}

void xx_sfx_sydex_diskette_image_destroy(
    xx_sfx_sydex_diskette_image *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_sydex_diskette_image_free(xx_sfx_sydex_diskette_image *archive) {
    if (!archive) return;
    xx_sfx_sydex_diskette_image_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_sydex_diskette_image_check_is_valid(Abstractformat *format,
                                                xx_pd_struct *pd) {
    sxd_context context;
    return sxd_parse(format, &context, false, pd);
}

bool xx_sfx_sydex_diskette_image_handle_base_info(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    sxd_context context;
    xx_sfx_sydex_diskette_image *archive;
    if (!format || !sxd_parse(format, &context, true, pd)) return false;
    archive = (xx_sfx_sydex_diskette_image *)format;
    archive->overlay_offset = format->base_address + context.overlay;
    archive->header_offset = format->base_address + context.header;
    archive->data_offset = format->base_address + context.data;
    archive->payload_end = format->base_address + context.end;
    archive->image_size = context.image_size;
    archive->number_of_records = 1U;
    archive->track_size = context.track_size;
    archive->track_count = context.track_count;
    archive->comment_size = context.comment_size;
    archive->sectors = context.sectors;
    archive->heads = context.heads;
    archive->cylinders = context.cylinders;
    archive->stored_cylinders = context.stored;
    archive->header_scanned = context.scanned;
    archive->truncated = context.truncated;
    format->number_of_archive_records = 1U;
    format->format_size = context.end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_sydex_diskette_image_get_format_size(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_sydex_diskette_image_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_sfx_sydex_diskette_image_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_sydex_diskette_image_handle_base_info(format, pd))
               ? ((xx_sfx_sydex_diskette_image *)format)->number_of_records
               : 0U;
}

bool xx_sfx_sydex_diskette_image_unpack_to_device(
    xx_sfx_sydex_diskette_image *archive, xx_io_device *destination,
    xx_pd_struct *pd) {
    sxd_context context;
    if (!archive || !sxd_parse(&archive->format, &context, true, pd))
        return false;
    return sxd_decode(&archive->format, &context, destination, pd);
}

xx_archive_record_state *
xx_sfx_sydex_diskette_image_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    sxd_stream *stream;
    xx_archive_record_state *state;
    sxd_context context;
    if (!sxd_parse(format, &context, true, pd)) return NULL;
    stream = (sxd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sxd_stream_free;
    state->total_records = 1;
    if (!sxd_copy_options(&state->options, options) ||
        !sxd_set_record(format, &state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfx_sydex_diskette_image_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sfx_sydex_diskette_image_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    sxd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (sxd_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_sfx_sydex_diskette_image_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    sxd_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (sxd_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = sxd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return sxd_decode(format, &stream->context, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* The member name is the reader's own constant, never taken from the
     * file, so it needs no sanitising. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", SXD_MEMBER_NAME)
               : xx_str_concat(base, SXD_MEMBER_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = sxd_decode(format, &stream->context, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_sydex_diskette_image_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
