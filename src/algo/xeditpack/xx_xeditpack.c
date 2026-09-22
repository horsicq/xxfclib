/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * XEDIT PACK / CMS COPYFILE PACK byte codec.  Ported from the reference
 * decoder XArchive/Algos/xxeditpackdecoder.cpp, opcode for opcode; see the
 * header for the opcode table and the three load-bearing details.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/xeditpack/xx_xeditpack.h"

#define XEP_BLANK 0x40U /* the EBCDIC space */

/* One walker drives both the measure and the decode, exactly as the reference
 * does: if the two ever disagreed about how a damaged stream ends, the reader
 * would publish a length the extractor cannot reproduce. */
typedef struct xep_walker {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t *out;      /* NULL while measuring */
    size_t produced;
    size_t ceiling;    /* the format ceiling, as in the reference */
    size_t capacity;   /* the caller's buffer; SIZE_MAX while measuring */
    bool overflow;
    bool truncated;
} xep_walker;

static void xep_init(xep_walker *w, const uint8_t *data, size_t size,
                     uint8_t *out, size_t ceiling, size_t capacity) {
    w->data = data;
    w->size = size;
    w->position = 0U;
    w->out = out;
    w->produced = 0U;
    w->ceiling = ceiling;
    w->capacity = capacity;
    w->overflow = false;
    w->truncated = false;
}

static bool xep_read_byte(xep_walker *w, uint8_t *value) {
    if (w->position >= w->size) return false;
    *value = w->data[w->position];
    ++w->position;
    return true;
}

/* Big endian, high byte first -- this is a mainframe format. */
static bool xep_read_word(xep_walker *w, uint32_t *value) {
    uint8_t high = 0U;
    uint8_t low = 0U;
    if (!xep_read_byte(w, &high)) return false;
    if (!xep_read_byte(w, &low)) return false;
    *value = ((uint32_t)high << 8U) | (uint32_t)low;
    return true;
}

static bool xep_reserve(xep_walker *w, size_t count) {
    if (count > (w->ceiling - w->produced)) {
        w->overflow = true;
        return false;
    }
    return true;
}

/* The caller's buffer is a limit of its own, checked against the bytes that
 * are actually emitted.  It is deliberately NOT the walk's ceiling: the
 * reference runs measure() and decode() with the same fixed ceiling and lets
 * the container compare lengths afterwards, and a truncated literal copy
 * reserves its FULL count against that ceiling (see xep_copy).  Folding the
 * two limits into one would make a truncated member fail to decode at the very
 * length measure() just reported for it. */
static bool xep_fits(xep_walker *w, size_t count) {
    return count <= (w->capacity - w->produced);
}

static bool xep_fill(xep_walker *w, uint8_t byte, size_t count) {
    size_t i;
    if (!xep_reserve(w, count)) return false;
    if (!xep_fits(w, count)) return false;
    if (w->out) {
        for (i = 0U; i < count; ++i) w->out[w->produced + i] = byte;
    }
    w->produced += count;
    return true;
}

/* A literal copy that runs past the end still appends the bytes that ARE
 * present and only then ends the walk.  That is what the reference extractor
 * does, and a truncated member decodes differently otherwise.  Note also that
 * the FULL count is reserved against the ceiling before the clamp, and that
 * the cursor advances by the full count as well -- both deliberate, both
 * copied from the reference. */
static bool xep_copy(xep_walker *w, size_t count) {
    size_t available;
    size_t taken;
    if (!xep_reserve(w, count)) return false;
    available = (w->position < w->size) ? (w->size - w->position) : 0U;
    taken = (count <= available) ? count : available;
    if (!xep_fits(w, taken)) return false;
    if (w->out && (taken > 0U)) {
        xx_rt_memcpy(w->out + w->produced, w->data + w->position, taken);
    }
    w->produced += taken;
    w->position += count;
    w->truncated = (taken != count);
    return true;
}

static bool xep_run(xep_walker *w) {
    for (;;) {
        uint8_t opcode = 0U;
        if (!xep_read_byte(w, &opcode)) break;

        if (opcode <= 0x77U) {
            if (!xep_fill(w, (uint8_t)XEP_BLANK, (size_t)opcode + 1U))
                return false;
        } else if ((opcode == 0x78U) || (opcode == 0x7cU)) {
            uint8_t count = 0U;
            if (!xep_read_byte(w, &count)) break;
            if (!xep_fill(w, (uint8_t)XEP_BLANK, (size_t)count + 1U))
                return false;
        } else if ((opcode == 0x79U) || (opcode == 0x7dU)) {
            uint32_t count = 0U;
            if (!xep_read_word(w, &count)) break;
            if (!xep_fill(w, (uint8_t)XEP_BLANK, (size_t)count + 1U))
                return false;
        } else if ((opcode == 0x7aU) || (opcode == 0x7eU)) {
            uint8_t count = 0U;
            uint8_t byte = 0U;
            /* The count is read first and the run byte second; when the run
             * byte is missing NOTHING is emitted for this opcode. */
            if (!xep_read_byte(w, &count)) break;
            if (!xep_read_byte(w, &byte)) break;
            if (!xep_fill(w, byte, (size_t)count + 1U)) return false;
        } else if ((opcode == 0x7bU) || (opcode == 0x7fU)) {
            uint32_t count = 0U;
            uint8_t byte = 0U;
            if (!xep_read_word(w, &count)) break;
            if (!xep_read_byte(w, &byte)) break;
            if (!xep_fill(w, byte, (size_t)count + 1U)) return false;
        } else if (opcode <= 0xf7U) { /* 0x80..0xF7 */
            if (!xep_copy(w, (size_t)(opcode - 0x80U) + 1U)) return false;
            if (w->truncated) break;
        } else if ((opcode == 0xf8U) || (opcode == 0xfcU)) {
            uint8_t count = 0U;
            if (!xep_read_byte(w, &count)) break;
            if (!xep_copy(w, (size_t)count + 1U)) return false;
            if (w->truncated) break;
        } else if ((opcode == 0xf9U) || (opcode == 0xfdU)) {
            uint32_t count = 0U;
            if (!xep_read_word(w, &count)) break;
            if (!xep_copy(w, (size_t)count + 1U)) return false;
            if (w->truncated) break;
        } else {
            /* 0xFF is the clean end; 0xFA / 0xFB / 0xFE are malformed and stop
             * the walk the same way, keeping the output so far. */
            break;
        }
    }

    return !w->overflow;
}

bool xx_xeditpack_decode_memory(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size,
                                size_t *written) {
    xep_walker walker;
    if (written) *written = 0U;
    if (!written) return false;
    if (!input && (input_size != 0U)) return false;
    if (!output && (output_size != 0U)) return false;

    xep_init(&walker, input, input_size, output,
             (output_size > XX_XEDITPACK_MAX_OUTPUT) ? output_size
                                                     : XX_XEDITPACK_MAX_OUTPUT,
             output_size);
    if (!xep_run(&walker)) return false;

    *written = walker.produced;

    return true;
}

bool xx_xeditpack_scan_memory(const uint8_t *input, size_t input_size,
                              size_t max_output, size_t *consumed,
                              size_t *produced) {
    xep_walker walker;
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!input || (input_size == 0U)) return false;

    xep_init(&walker, input, input_size, NULL, max_output, (size_t)-1);
    if (!xep_run(&walker)) return false;
    /* measure() in the reference rejects an empty result. */
    if (walker.produced == 0U) return false;

    if (consumed) {
        *consumed = (walker.position < input_size) ? walker.position
                                                   : input_size;
    }
    if (produced) *produced = walker.produced;

    return true;
}
