/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MXS archives, the packed data files shipped with several DOS era games and
 * driver sets (*.PAC, *.LAB collections, "resource.urk", "SCEN*.SSM", ...).
 * The container has NO MAGIC: it is a bare chain of members running from the
 * first byte of the file to the last, and that exact fit is most of what makes
 * a file identifiable as one.
 *
 *   member header, 17 bytes:
 *     0x00   4  u32 LE   packed length: the bytes that follow this header and
 *                        belong to this member, the plaintext length word
 *                        below included
 *     0x04  13  char     name field, NUL terminated; the bytes behind the NUL
 *                        are UNINITIALISED WRITER MEMORY, not padding, and
 *                        differ between otherwise identical copies of the same
 *                        archive, so nothing in them may be validated
 *   then the member's data:
 *     0x11   4  u32 LE   plaintext length
 *     0x15   n  payload  LZHUF stream
 *
 * The next member begins immediately behind, with no alignment and no
 * separator, and the last one ends exactly at end-of-file. There is no
 * directory, no member count, no timestamp and no checksum anywhere.
 *
 * The payload is LZHUF - Haruyasu Yoshizaki's adaptive-Huffman-over-LZSS, the
 * LZHUF.C that circulated from 1988 on: a 4096 byte ring buffer pre-filled
 * with spaces, match lengths of 3..62, positions whose upper six bits come
 * through the fixed table below and whose lower six bits are read verbatim,
 * and an adaptive Huffman tree over 314 symbols that is rebuilt whenever the
 * root frequency reaches 0x8000. The u32 plaintext length in front of the
 * stream is LZHUF.C's own "textsize" word, written by its Encode().
 *
 * WHAT IS DOCUMENTED and what is not: the LZHUF codec is documented - the
 * decoder below follows the published LZHUF.C, tables included. The container
 * around it is NOT documented anywhere this port could find; the layout above
 * was recovered from the corpus, where all 224 archives chain to an exact
 * end-of-file fit and the recovered payloads are the expected resource files.
 * The junk behind the name was identified the way dtpacked's was: two copies
 * of the same archive differ in exactly those bytes and nowhere else.
 *
 * Having no magic, this reader leans on structure instead: every member must
 * fit, the chain must land exactly on end-of-file, names must be plausible,
 * and the first member's stream must decode to exactly the length it declares.
 * The trial decode costs a 4 KiB ring buffer and no output allocation.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mxs/xx_mxs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The alias macro is defined next to the enumerator in xxfc_defs.h, so testing
 * for it picks up the real file type as soon as MXS is registered there.
 * Until then the reader identifies itself as unknown rather than borrowing
 * another format's id. See the port report for the registration this needs. */
#ifdef MXS
#define XX_MXS_FILE_TYPE XX_FILE_TYPE_MXS
#else
#define XX_MXS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_MXS_HEADER_SIZE 17
#define XX_MXS_NAME_FIELD 13
#define XX_MXS_SIZE_PREFIX 4
/* A member always carries at least its plaintext length word. */
#define XX_MXS_MIN_PACKED 4
#define XX_MXS_MAX_MEMBERS 65536
#define XX_MXS_METHOD_LZHUF 1U
#define XX_MXS_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* LZHUF tops out around ninety-six plaintext bytes per packed byte; the
 * reference corpus peaks at thirty-four. The ceiling below leaves headroom for
 * both and still refuses a header claiming a gigabyte behind a few bytes. */
#define XX_MXS_MAX_RATIO 256
#define XX_MXS_RATIO_SLACK 4096
/* The bit reader keeps a sixteen bit look-ahead, so a stream that is read to
 * its end reports a consumed length up to two bytes past it. Anything further
 * past the end means the decoder was still asking for input that is not there,
 * and anything well short of the end means it stopped early: both say the
 * stream is not what the header describes. */
#define XX_MXS_CONSUMED_SLACK 3

/* ---------------------------------------------------------- LZHUF codec -- */

#define XX_MXS_LZ_N 4096                /* ring buffer size */
#define XX_MXS_LZ_F 60                  /* look-ahead buffer size */
#define XX_MXS_LZ_THRESHOLD 2
#define XX_MXS_LZ_N_CHAR (256 - XX_MXS_LZ_THRESHOLD + XX_MXS_LZ_F)
#define XX_MXS_LZ_T (XX_MXS_LZ_N_CHAR * 2 - 1)
#define XX_MXS_LZ_ROOT (XX_MXS_LZ_T - 1)
#define XX_MXS_LZ_MAX_FREQ 0x8000U
/* The bit reader zero-fills past the end of the input; a handful of those is
 * normal at the very end of a stream, many of them mean the decode has gone
 * off the rails and is being fed nothing. */
#define XX_MXS_LZ_MAX_OVERRUN 8

/* Upper six bits of a match position: the fixed table from LZHUF.C. */
static const uint8_t xx_mxs_pos_code[256] = {
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
    0x3c, 0x3d, 0x3e, 0x3f,
};

static const uint8_t xx_mxs_pos_len[256] = {
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08,
};

typedef struct xx_mxs_lzhuf_s {
    const uint8_t *input;
    size_t input_size;
    size_t position;   /* bytes taken from input, past-the-end ones included */
    unsigned overrun;  /* zero bytes invented behind the end of the input */
    uint16_t buffer;   /* the reference's 16-bit getbuf, MSB first */
    unsigned bits;
    uint16_t freq[XX_MXS_LZ_T + 1];
    uint16_t son[XX_MXS_LZ_T];
    uint16_t parent[XX_MXS_LZ_T + XX_MXS_LZ_N_CHAR];
    uint8_t window[XX_MXS_LZ_N + XX_MXS_LZ_F - 1];
} xx_mxs_lzhuf;

static void xx_mxs_lzhuf_start(xx_mxs_lzhuf *state, const uint8_t *input,
                               size_t input_size) {
    unsigned i;
    unsigned j;

    state->input = input;
    state->input_size = input_size;
    state->position = 0U;
    state->overrun = 0U;
    state->buffer = 0U;
    state->bits = 0U;
    for (i = 0U; i < (unsigned)XX_MXS_LZ_N_CHAR; ++i) {
        state->freq[i] = 1U;
        state->son[i] = (uint16_t)(i + XX_MXS_LZ_T);
        state->parent[i + XX_MXS_LZ_T] = (uint16_t)i;
    }
    i = 0U;
    j = (unsigned)XX_MXS_LZ_N_CHAR;
    while (j <= (unsigned)XX_MXS_LZ_ROOT) {
        state->freq[j] = (uint16_t)(state->freq[i] + state->freq[i + 1U]);
        state->son[j] = (uint16_t)i;
        state->parent[i] = (uint16_t)j;
        state->parent[i + 1U] = (uint16_t)j;
        i += 2U;
        ++j;
    }
    state->freq[XX_MXS_LZ_T] = 0xffffU;
    state->parent[XX_MXS_LZ_ROOT] = 0U;
    /* LZHUF pre-fills the ring with spaces; a match may legitimately reach
     * into the part of it that nothing has written yet. */
    xx_rt_memset(state->window, ' ', sizeof(state->window));
}

static void xx_mxs_lzhuf_fill(xx_mxs_lzhuf *state) {
    while (state->bits <= 8U) {
        unsigned byte = 0U;
        if (state->position < state->input_size) {
            byte = state->input[state->position];
        } else {
            ++state->overrun;
        }
        ++state->position;
        state->buffer =
            (uint16_t)(state->buffer | (uint16_t)(byte << (8U - state->bits)));
        state->bits += 8U;
    }
}

static unsigned xx_mxs_lzhuf_bit(xx_mxs_lzhuf *state) {
    unsigned value;

    xx_mxs_lzhuf_fill(state);
    value = (state->buffer & 0x8000U) ? 1U : 0U;
    state->buffer = (uint16_t)(state->buffer << 1);
    --state->bits;
    return value;
}

static unsigned xx_mxs_lzhuf_byte(xx_mxs_lzhuf *state) {
    unsigned value;

    xx_mxs_lzhuf_fill(state);
    value = (unsigned)(state->buffer >> 8) & 0xffU;
    state->buffer = (uint16_t)(state->buffer << 8);
    state->bits -= 8U;
    return value;
}

/* Halve every frequency and rebuild the tree, exactly as LZHUF.C's reconst().
 * The reference moves (j - k) table entries with memmove; this moves the same
 * entries one at a time, from the back, which is what that overlapping move
 * amounts to. */
static void xx_mxs_lzhuf_rebuild(xx_mxs_lzhuf *state) {
    unsigned i;
    unsigned j;
    unsigned k;

    j = 0U;
    for (i = 0U; i < (unsigned)XX_MXS_LZ_T; ++i) {
        if (state->son[i] >= (uint16_t)XX_MXS_LZ_T) {
            state->freq[j] = (uint16_t)((state->freq[i] + 1U) / 2U);
            state->son[j] = state->son[i];
            ++j;
        }
    }
    i = 0U;
    j = (unsigned)XX_MXS_LZ_N_CHAR;
    while (j < (unsigned)XX_MXS_LZ_T) {
        unsigned value =
            (unsigned)state->freq[i] + (unsigned)state->freq[i + 1U];
        unsigned move;

        state->freq[j] = (uint16_t)value;
        k = j - 1U;
        while (value < state->freq[k]) --k;
        ++k;
        move = j - k;
        while (move-- != 0U) {
            state->freq[k + move + 1U] = state->freq[k + move];
            state->son[k + move + 1U] = state->son[k + move];
        }
        state->freq[k] = (uint16_t)value;
        state->son[k] = (uint16_t)i;
        i += 2U;
        ++j;
    }
    for (i = 0U; i < (unsigned)XX_MXS_LZ_T; ++i) {
        k = state->son[i];
        if (k >= (unsigned)XX_MXS_LZ_T) {
            state->parent[k] = (uint16_t)i;
        } else {
            state->parent[k] = (uint16_t)i;
            state->parent[k + 1U] = (uint16_t)i;
        }
    }
}

static void xx_mxs_lzhuf_update(xx_mxs_lzhuf *state, unsigned code) {
    unsigned node;

    if (state->freq[XX_MXS_LZ_ROOT] == (uint16_t)XX_MXS_LZ_MAX_FREQ) {
        xx_mxs_lzhuf_rebuild(state);
    }
    node = state->parent[code + XX_MXS_LZ_T];
    do {
        unsigned count;
        unsigned swap;

        ++state->freq[node];
        count = state->freq[node];
        swap = node + 1U;
        /* freq[T] is the 0xffff sentinel, so the scan below always stops. */
        if (count > state->freq[swap]) {
            unsigned left;
            unsigned right;

            while (count > state->freq[swap]) ++swap;
            --swap;
            state->freq[node] = state->freq[swap];
            state->freq[swap] = (uint16_t)count;
            left = state->son[node];
            state->parent[left] = (uint16_t)swap;
            if (left < (unsigned)XX_MXS_LZ_T) {
                state->parent[left + 1U] = (uint16_t)swap;
            }
            right = state->son[swap];
            state->son[swap] = (uint16_t)left;
            state->parent[right] = (uint16_t)node;
            if (right < (unsigned)XX_MXS_LZ_T) {
                state->parent[right + 1U] = (uint16_t)node;
            }
            state->son[node] = (uint16_t)right;
            node = swap;
        }
        node = state->parent[node];
    } while (node != 0U);
}

static unsigned xx_mxs_lzhuf_symbol(xx_mxs_lzhuf *state) {
    unsigned code = state->son[XX_MXS_LZ_ROOT];

    while (code < (unsigned)XX_MXS_LZ_T) {
        code += xx_mxs_lzhuf_bit(state);
        code = state->son[code];
    }
    code -= (unsigned)XX_MXS_LZ_T;
    xx_mxs_lzhuf_update(state, code);
    return code;
}

static unsigned xx_mxs_lzhuf_match_position(xx_mxs_lzhuf *state) {
    unsigned index = xx_mxs_lzhuf_byte(state);
    unsigned value = (unsigned)xx_mxs_pos_code[index] << 6;
    unsigned remaining = (unsigned)xx_mxs_pos_len[index] - 2U;

    while (remaining-- != 0U) {
        index = ((index << 1) + xx_mxs_lzhuf_bit(state)) & 0xffffU;
    }
    return value | (index & 0x3fU);
}

/* One pass over an LZHUF stream.
 *
 * @p output is optional: with NULL the walk only measures, which is what parse
 * wants - the ring buffer still has to be maintained, but no plaintext buffer
 * is allocated, so validating a member that claims megabytes costs 4 KiB.
 * When it is given it must have room for @p limit bytes, and @p limit is
 * always the declared plaintext length, so the decoder cannot be talked into
 * writing past the buffer the caller sized from that field.
 *
 * Returns false only when the stream stops feeding the decoder real input long
 * before @p limit bytes exist. LZHUF has no other structural fault to find:
 * every symbol and every position it can decode is a legal one, which is why
 * parse pairs this with a consumed-length check. */
static bool xx_mxs_lzhuf_run(xx_mxs_lzhuf *state, const uint8_t *input,
                             size_t input_size, uint8_t *output, size_t limit,
                             size_t *consumed, size_t *produced) {
    size_t written = 0U;
    unsigned cursor = (unsigned)(XX_MXS_LZ_N - XX_MXS_LZ_F);

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!state || (!input && input_size != 0U)) return false;
    xx_mxs_lzhuf_start(state, input, input_size);

    while (written < limit) {
        unsigned code;

        if (state->overrun > (unsigned)XX_MXS_LZ_MAX_OVERRUN) return false;
        code = xx_mxs_lzhuf_symbol(state);
        if (code < 256U) {
            if (output) output[written] = (uint8_t)code;
            ++written;
            state->window[cursor] = (uint8_t)code;
            cursor = (cursor + 1U) & (unsigned)(XX_MXS_LZ_N - 1);
            continue;
        }
        {
            unsigned source =
                (cursor - xx_mxs_lzhuf_match_position(state) - 1U) &
                (unsigned)(XX_MXS_LZ_N - 1);
            unsigned length = code - 255U + (unsigned)XX_MXS_LZ_THRESHOLD;
            unsigned index;

            for (index = 0U; index < length; ++index) {
                uint8_t byte = state->window[(source + index) &
                                             (unsigned)(XX_MXS_LZ_N - 1)];
                /* The last match of a stream routinely overshoots the declared
                 * length; the surplus is dropped rather than treated as a
                 * fault. */
                if (written >= limit) break;
                if (output) output[written] = byte;
                ++written;
                state->window[cursor] = byte;
                cursor = (cursor + 1U) & (unsigned)(XX_MXS_LZ_N - 1);
            }
        }
    }
    /* How far the reader actually reached, its look-ahead included, so the
     * caller can tell "ended on the last byte" from "stopped early". */
    if (consumed) {
        *consumed = state->position <= state->input_size
                        ? state->position
                        : state->input_size + (size_t)state->overrun;
    }
    if (produced) *produced = written;
    return true;
}

/* ------------------------------------------------------------- members -- */

typedef struct xx_mxs_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    bool is_folder;
} xx_mxs_member;

typedef struct xx_mxs_stream_s {
    xx_mxs_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_mxs_stream;

static void xx_mxs_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_mxs_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_mxs_read_at(Abstractformat *self, int64_t offset,
                           uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_mxs_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. MXS names come
 * from DOS and may carry either separator, so both are treated as one. */
static bool xx_mxs_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    if (name[0] != '\0' && name[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;

        while (*end && *end != '/' && *end != '\\') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* The name occupies a fixed thirteen byte field and ends at the first NUL; the
 * bytes behind it are the writer's leftover memory and are dropped unread. A
 * field with no NUL, an empty name, or any byte outside printable ASCII is not
 * a member header - which, in a format with no magic, is a large part of what
 * keeps this reader off other people's files. */
static char *xx_mxs_name_from_field(const uint8_t *field) {
    char *name;
    size_t length = 0U;

    while (length < (size_t)XX_MXS_NAME_FIELD && field[length] != 0U) {
        if (field[length] < 0x20U || field[length] > 0x7eU) return NULL;
        ++length;
    }
    if (length == 0U || length >= (size_t)XX_MXS_NAME_FIELD) return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    xx_rt_memcpy(name, field, length);
    name[length] = '\0';
    return name;
}

static void xx_mxs_stream_free(void *pointer) {
    xx_mxs_stream *stream = (xx_mxs_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of member->name. */
static bool xx_mxs_add(xx_mxs_stream *stream, const xx_mxs_member *member) {
    xx_mxs_member *grown = (xx_mxs_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Decode the first member's stream without keeping its plaintext. With no
 * magic to lean on, this is what separates an MXS archive from any other file
 * whose first seventeen bytes happen to look like a member header. */
static bool xx_mxs_verify_first(Abstractformat *self,
                                const xx_mxs_member *member) {
    xx_mxs_lzhuf *state;
    uint8_t *packed;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool measured;

    if (member->compressed_size < 0 ||
        member->compressed_size > XX_MXS_MAX_DECODED ||
        member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_MXS_MAX_DECODED) {
        return false;
    }
    /* An empty member is its four byte length word and nothing else; there is
     * no stream to trial-decode. */
    if (member->uncompressed_size == 0) return member->compressed_size == 0;
    if (member->compressed_size == 0) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_mxs_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    state = (xx_mxs_lzhuf *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(packed);
        return false;
    }
    measured = xx_mxs_lzhuf_run(state, packed, (size_t)member->compressed_size,
                                NULL, (size_t)member->uncompressed_size,
                                &consumed, &produced);
    xx_mem_free(state);
    xx_mem_free(packed);
    if (!measured) return false;
    if (produced != (size_t)member->uncompressed_size) return false;
    /* The stream must end where the member ends: a real LZHUF stream runs out
     * of bits within the reader's look-ahead of its last byte. */
    if (consumed + (size_t)XX_MXS_CONSUMED_SLACK <
            (size_t)member->compressed_size ||
        consumed > (size_t)member->compressed_size +
                       (size_t)XX_MXS_CONSUMED_SLACK) {
        return false;
    }
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_mxs_stream *xx_mxs_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_mxs_stream *stream;
    uint8_t header[XX_MXS_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)XX_MXS_HEADER_SIZE + XX_MXS_MIN_PACKED) return NULL;

    stream = (xx_mxs_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (offset < span) {
        xx_mxs_member member;
        char *name;
        int64_t packed;
        int64_t uncompressed;
        int64_t data_offset;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_MXS_MAX_MEMBERS) goto fail;
        if (!xx_mxs_range_within(span, offset, (int64_t)XX_MXS_HEADER_SIZE)) {
            goto fail;
        }
        if (!xx_mxs_read_at(self, self->base_address + offset, header,
                            sizeof(header))) {
            goto fail;
        }
        /* Bounded before it is used for anything: the packed length has to fit
         * in what is left of the file, or this is not a member header. */
        {
            uint32_t declared = xx_mxs_le32(header);
            if (declared > (uint32_t)XX_MXS_MAX_DECODED) goto fail;
            packed = (int64_t)declared;
        }
        if (packed < XX_MXS_MIN_PACKED) goto fail;
        if (!xx_mxs_range_within(span, offset + XX_MXS_HEADER_SIZE, packed)) {
            goto fail;
        }
        name = xx_mxs_name_from_field(header + 4);
        if (!name) goto fail;

        /* The member's own data opens with the plaintext length that LZHUF.C
         * writes in front of its stream; the stream is the rest. */
        {
            uint8_t prefix[XX_MXS_SIZE_PREFIX];
            if (!xx_mxs_read_at(self,
                                self->base_address + offset +
                                    XX_MXS_HEADER_SIZE,
                                prefix, sizeof(prefix))) {
                xx_str_free(name);
                goto fail;
            }
            uncompressed = (int64_t)xx_mxs_le32(prefix);
        }
        if (uncompressed > XX_MXS_MAX_DECODED) {
            xx_str_free(name);
            goto fail;
        }
        data_offset = offset + XX_MXS_HEADER_SIZE + XX_MXS_SIZE_PREFIX;
        if (uncompressed >
            ((packed - XX_MXS_SIZE_PREFIX) * XX_MXS_MAX_RATIO) +
                XX_MXS_RATIO_SLACK) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = (int64_t)XX_MXS_HEADER_SIZE + XX_MXS_SIZE_PREFIX;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = packed - XX_MXS_SIZE_PREFIX;
        member.uncompressed_size = uncompressed;
        member.method = XX_MXS_METHOD_LZHUF;
        /* The chain has no directory entries and never will: every member is
         * a file. */
        member.is_folder = false;

        if (!xx_mxs_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        offset += (int64_t)XX_MXS_HEADER_SIZE + packed;
    }

    /* The chain IS the format: members run back to back with no alignment and
     * no separator, so anything but an exact landing on end-of-file means this
     * was never an MXS archive. */
    if (offset != span) goto fail;
    if (stream->count == 0U) goto fail;
    if (!xx_mxs_verify_first(self, &stream->items[0])) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_mxs_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_mxs_decode(Abstractformat *self, const xx_mxs_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    xx_mxs_lzhuf *state;
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_MXS_METHOD_LZHUF) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_MXS_MAX_DECODED ||
        member->uncompressed_size > XX_MXS_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size == 0) {
        /* An empty member: the length word says zero and there is no stream. */
        return member->compressed_size == 0;
    }
    if (member->compressed_size == 0) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_mxs_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    state = (xx_mxs_lzhuf *)xx_mem_alloc(sizeof(*state));
    if (!output || !state) {
        xx_mem_free(output);
        xx_mem_free(state);
        xx_mem_free(input);
        return false;
    }
    /* Exactly the declared plaintext length, or nothing. The format has no
     * checksum, so this equality is the whole of extraction's correctness
     * check, and a short decode reported as success is the one failure the
     * caller cannot detect. */
    if (!xx_mxs_lzhuf_run(state, input, (size_t)member->compressed_size, output,
                          (size_t)member->uncompressed_size, NULL, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(state);
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(state);
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_mxs_init(xx_mxs *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MXS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mxs");
    xx_format_set_extension(&archive->format, "mxs");
    archive->format.check_is_valid = xx_mxs_check_is_valid;
    archive->format.handle_base_info = xx_mxs_handle_base_info;
    archive->format.get_format_size = xx_mxs_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mxs_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mxs_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mxs_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mxs_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mxs_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mxs_free_archive_records_reading;
    archive->format.destroy = xx_mxs_vtable_destroy;
}

xx_mxs *xx_mxs_create(xx_io_device *device, int64_t base_address) {
    xx_mxs *archive = (xx_mxs *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_mxs_init(archive, device, base_address);
    return archive;
}

void xx_mxs_destroy(xx_mxs *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_mxs_free(xx_mxs *archive) {
    if (!archive) return;
    xx_mxs_destroy(archive);
    xx_mem_free(archive);
}

static void xx_mxs_vtable_destroy(Abstractformat *self) {
    xx_mxs_destroy((xx_mxs *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_mxs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_mxs_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_mxs_parse(self, pd);
    if (!stream) return false;
    xx_mxs_stream_free(stream);
    return true;
}

bool xx_mxs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_mxs *archive = (xx_mxs *)self;
    xx_mxs_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_mxs_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_mxs_stream_free(stream);
    return true;
}

int64_t xx_mxs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_mxs_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_mxs *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_mxs_set_record(xx_archive_record *record,
                              const xx_mxs_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_mxs_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_mxs_get_option(const xx_list_s *options,
                                       uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_mxs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_mxs_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_mxs_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mxs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_mxs_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_mxs_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_mxs_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_mxs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_mxs_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_mxs_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_mxs_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_mxs_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_mxs_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_mxs_stream *stream;
    const xx_mxs_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_mxs_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_mxs_path_safe(member->name)) return false;

    path_option = xx_mxs_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_mxs_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_mxs_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_mxs_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
