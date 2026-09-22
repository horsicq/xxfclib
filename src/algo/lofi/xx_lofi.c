/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Solaris compressed lofi image decoder.  Ported from the XArchive reference
 * decoder (XArchive/Algos/xlofidecoder.cpp): the same header rules, the same
 * index rules (entry 0 is zero, every later entry ascends by at least the
 * 1 + 13 framing bytes), the same per-segment framing byte, the same demand
 * that the alone header's declared length equal the geometry's expectation,
 * and the same per-segment LZMA reset.
 *
 * Deviations, all output-equivalent:
 *   - The index is validated in place out of the input buffer instead of being
 *     copied into a QList; no value is interpreted differently.
 *   - Segments are decoded with xx_lzma_decompress_memory(uncomp_size = the
 *     segment's declared length) instead of the reference's LZMA_FINISH_ANY
 *     loop.  Both stop at exactly that length without requiring an end marker,
 *     and both treat a stream that ends early as a failure -- the reference
 *     through its "no forward progress" test, this through the range decoder
 *     running out of input.
 *   - The reference keeps one CLzmaDec allocation alive across segments with
 *     identical props; that is an allocator optimisation, and it calls
 *     LzmaDec_Init before every segment, so each segment starts from a clean
 *     state and dictionary either way.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lofi/xx_lofi.h"
#include "xxfclib/algo/lzma/xx_lzma.h"

#define LOFI_NAME_SIZE 36
#define LOFI_INDEX_OFFSET 0x30
#define LOFI_SEGMENT_PREFIX 1
#define LOFI_ALONE_HEADER 13
/* `lofiadm` caps the segment size well below this; the bound only keeps a
 * corrupt header from asking for an absurd allocation. */
#define LOFI_MAX_SEGMENT_SIZE (64u * 1024u * 1024u)
/* 8 bytes per entry, i.e. a 512 MiB index -- far past any image `lofiadm` can
 * produce, and it keeps entries * 8 in range. */
#define LOFI_MAX_INDEX_ENTRIES (64u * 1024u * 1024u)

typedef struct lofi_geometry_s {
    uint64_t segment_size;
    uint64_t last_segment_size;
    uint64_t index_entries; /* segments + 1 */
    uint64_t data_offset;   /* first byte after the index */
    uint64_t data_size;     /* index[last] */
    uint64_t image_size;
} lofi_geometry;

static uint32_t lofi_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t lofi_be64(const uint8_t *p)
{
    return ((uint64_t)lofi_be32(p) << 32) | (uint64_t)lofi_be32(p + 4);
}

static uint64_t lofi_le64(const uint8_t *p)
{
    uint64_t value = 0;
    int i;
    for (i = 7; i >= 0; --i) value = (value << 8) | (uint64_t)p[i];
    return value;
}

static uint64_t lofi_index_at(const uint8_t *input, uint64_t i)
{
    return lofi_be64(input + LOFI_INDEX_OFFSET + i * 8);
}

static bool lofi_parse_geometry(const uint8_t *input, size_t input_size,
                                lofi_geometry *geometry)
{
    uint64_t i;
    uint64_t previous;
    uint64_t segments;

    if (input_size < (size_t)(LOFI_INDEX_OFFSET + 8)) return false;

    /* The name field is the algorithm followed by zero padding.  Only "lzma"
     * is supported; see the header for why "gzip" is refused. */
    if ((input[0] != 'l') || (input[1] != 'z') || (input[2] != 'm') ||
        (input[3] != 'a')) {
        return false;
    }
    for (i = 4; i < (uint64_t)LOFI_NAME_SIZE; i++) {
        if (input[i] != 0) return false;
    }

    geometry->segment_size = (uint64_t)lofi_be32(input + 0x24);
    geometry->index_entries = (uint64_t)lofi_be32(input + 0x28);
    geometry->last_segment_size = (uint64_t)lofi_be32(input + 0x2c);

    /* Exactly the reference detector's rules: a positive segment size, more
     * than one index entry, and a final segment that is positive and no larger
     * than a full one. */
    if ((geometry->segment_size == 0) ||
        (geometry->segment_size > (uint64_t)LOFI_MAX_SEGMENT_SIZE)) {
        return false;
    }
    if ((geometry->index_entries <= 1) ||
        (geometry->index_entries > (uint64_t)LOFI_MAX_INDEX_ENTRIES)) {
        return false;
    }
    if ((geometry->last_segment_size == 0) ||
        (geometry->last_segment_size > geometry->segment_size)) {
        return false;
    }

    geometry->data_offset =
        (uint64_t)LOFI_INDEX_OFFSET + geometry->index_entries * 8;
    if (geometry->data_offset > (uint64_t)input_size) return false;

    segments = geometry->index_entries - 1;
    geometry->image_size = (segments - 1) * geometry->segment_size +
                           geometry->last_segment_size;

    previous = 0;
    for (i = 0; i < geometry->index_entries; i++) {
        uint64_t value = lofi_index_at(input, i);
        if (value > (uint64_t)INT64_MAX) return false;
        /* Entry 0 is 0 (the reference detector checks exactly this), and the
         * rest strictly ascend: every segment must carry at least its own
         * framing bytes. */
        if (i == 0) {
            if (value != 0) return false;
        } else if (value <
                   previous + (uint64_t)(LOFI_SEGMENT_PREFIX + LOFI_ALONE_HEADER)) {
            /* Written as an addition, not as (value - previous) < 14: the
             * reference does that subtraction in SIGNED qint64, where a
             * descending index goes negative and is rejected.  Unsigned, the
             * same subtraction would wrap and accept it.  `previous` is capped
             * at INT64_MAX above, so the addition cannot overflow. */
            return false;
        }
        previous = value;
    }
    geometry->data_size = previous;
    if (geometry->data_offset + geometry->data_size > (uint64_t)input_size) {
        return false;
    }
    return true;
}

bool xx_lofi_scan_memory(const uint8_t *input, size_t input_size,
                         size_t max_output, size_t *consumed, size_t *produced)
{
    lofi_geometry geometry;

    if (consumed) *consumed = 0;
    if (produced) *produced = 0;
    if (!input) return false;

    xx_rt_memset(&geometry, 0, sizeof(geometry));
    if (!lofi_parse_geometry(input, input_size, &geometry)) return false;
    if (geometry.image_size > (uint64_t)max_output) return false;
    if (geometry.image_size > (uint64_t)(size_t)-1) return false;

    if (consumed) *consumed = (size_t)(geometry.data_offset + geometry.data_size);
    if (produced) *produced = (size_t)geometry.image_size;
    return true;
}

bool xx_lofi_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size, size_t *written)
{
    lofi_geometry geometry;
    const uint8_t *base;
    uint64_t segments;
    uint64_t produced = 0;
    uint64_t i;

    if (written) *written = 0;
    if (!input || !output) return false;

    xx_rt_memset(&geometry, 0, sizeof(geometry));
    if (!lofi_parse_geometry(input, input_size, &geometry)) return false;
    if (geometry.image_size > (uint64_t)output_size) return false;

    base = input + geometry.data_offset;
    segments = geometry.index_entries - 1;

    for (i = 0; i < segments; i++) {
        uint64_t start = lofi_index_at(input, i);
        uint64_t end = lofi_index_at(input, i + 1);
        uint64_t segment_bytes = end - start;
        const uint8_t *segment;
        const uint8_t *props;
        uint64_t declared;
        uint64_t want;
        size_t segment_written = 0;

        if (segment_bytes <=
            (uint64_t)(LOFI_SEGMENT_PREFIX + LOFI_ALONE_HEADER)) {
            return false;
        }
        segment = base + start;
        /* Every segment carries this framing byte in every known image; it is
         * not part of the LZMA "alone" header that follows it. */
        if (segment[0] != 0x01) return false;

        props = segment + LOFI_SEGMENT_PREFIX;
        declared = lofi_le64(props + 5);
        want = (i + 1 == segments) ? geometry.last_segment_size
                                   : geometry.segment_size;
        if (declared != want) return false;
        if (produced + want > (uint64_t)output_size) return false;

        if (!xx_lzma_decompress_memory(
                segment + LOFI_SEGMENT_PREFIX + LOFI_ALONE_HEADER,
                (size_t)(segment_bytes - LOFI_SEGMENT_PREFIX -
                         LOFI_ALONE_HEADER),
                props, 5, (int64_t)want, output + (size_t)produced,
                (size_t)want, &segment_written)) {
            return false;
        }
        if ((uint64_t)segment_written != want) return false;
        produced += want;
    }

    if (produced != geometry.image_size) return false;

    if (written) *written = (size_t)produced;
    return true;
}
