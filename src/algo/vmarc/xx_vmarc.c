/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * VM/CMS VMARC member codecs: a port of XVMARCDecoder, keeping its exact leaf
 * recycling and lazy character fill.  See xx_vmarc.h for the format.
 */
#include "xxfclib/algo/vmarc/xx_vmarc.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define VMARC_ENTRIES 4096      /* dictionary entries                       */
#define VMARC_TOP 0x101         /* first recyclable entry                   */
#define VMARC_LIMIT 4095        /* last recyclable entry                    */
#define VMARC_SCRATCH 4096      /* used when nothing can be recycled        */
#define VMARC_SLOTS 4097        /* entries plus the scratch slot            */
#define VMARC_PARENT_ROOT (-1)  /* the 257 seeded entries                   */
#define VMARC_PARENT_UNSET (-2) /* never assigned                           */

/* The LZW tables.  Heap-owned per call, never a module-level array: this
 * decoder is called from several threads. */
typedef struct vmarc_tables {
    int parent[VMARC_SLOTS];
    int character[VMARC_SLOTS];
    int refs[VMARC_SLOTS];
    int chain[VMARC_SLOTS];
} vmarc_tables;

/* The CMS record sink (translate flag OFF). */
typedef struct vmarc_sink {
    uint8_t *output; /* NULL when only measuring                            */
    int lrecl;
    bool fixed;
    int new_lines;
    int column;
    size_t count;
    size_t limit;
    bool overflow;
} vmarc_sink;

/* Returns 1 to continue, 0 when the member is finished, -1 on overflow. */
static int vmarc_sink_put(vmarc_sink *sink, int symbol)
{
    int value = symbol - 1;

    if (value < 0) {
        sink->new_lines++;
        /* 'F' members end on the first end-of-record, 'V' members need two in
         * a row - any data byte resets the counter below. */
        if ((sink->new_lines < 2) && (!sink->fixed)) return 1;
        return 0;
    }
    sink->new_lines = 0;
    if (sink->fixed) {
        /* Column tracking exists in the reference sink but produces no output
         * of its own; it is kept so the shape of the port matches. */
        if (sink->column == sink->lrecl) sink->column = 0;
        sink->column++;
    }
    if (sink->count >= sink->limit) {
        sink->overflow = true;
        return -1;
    }
    if (sink->output) sink->output[sink->count] = (uint8_t)(value & 0xff);
    sink->count++;
    return 1;
}

/* 12-bit codes, MSB first. */
typedef struct vmarc_bits {
    const uint8_t *input;
    size_t position;
    size_t end;
    uint32_t bits;
    int bit_count;
} vmarc_bits;

static bool vmarc_bits_get(vmarc_bits *reader, int *code)
{
    while (reader->bit_count <= 11) {
        uint32_t byte;
        if (reader->position >= reader->end) return false;
        byte = (uint32_t)reader->input[reader->position];
        reader->position++;
        reader->bits = (uint32_t)(reader->bits +
                                  (byte << (24 - reader->bit_count)));
        reader->bit_count += 8;
    }
    *code = (int)(reader->bits >> 20);
    reader->bits = (uint32_t)(reader->bits << 12);
    reader->bit_count -= 12;
    return true;
}

static bool vmarc_lzw(vmarc_bits *reader, vmarc_sink *sink,
                      vmarc_tables *tables)
{
    int rover;
    int pending;
    int i;

    for (i = 0; i < VMARC_SLOTS; ++i) {
        tables->parent[i] = VMARC_PARENT_UNSET;
        tables->character[i] = 0;
        tables->refs[i] = 0;
    }
    for (i = 0; i < VMARC_TOP; ++i) {
        tables->parent[i] = VMARC_PARENT_ROOT;
        tables->character[i] = i;
        tables->refs[i] = 1;
    }
    rover = VMARC_TOP - 1;
    pending = VMARC_TOP;
    tables->parent[pending] = VMARC_PARENT_ROOT;

    for (;;) {
        int code = 0;
        int length = 0;
        int node;
        int slot;
        bool wrapped = false;

        if (!vmarc_bits_get(reader, &code)) return false;
        if (code < 0 || code >= VMARC_ENTRIES) return false;
        if (tables->parent[code] == VMARC_PARENT_UNSET) return false;

        node = code;
        while (node != VMARC_PARENT_ROOT) {
            if (node < 0 || node >= VMARC_SLOTS || length >= VMARC_SLOTS) {
                return false;
            }
            tables->chain[length] = node;
            length++;
            node = tables->parent[node];
            if (node == VMARC_PARENT_UNSET) return false;
        }
        if (length == 0) return false;

        /* Lazy fill: the pending entry finally learns its character, which is
         * the first character of the string that has just been decoded.  This
         * is what resolves the KwKwK case; do not hoist it or special-case it.
         */
        tables->character[pending] =
            tables->character[tables->chain[length - 1]];

        for (i = length - 1; i >= 0; --i) {
            int result = vmarc_sink_put(sink,
                                        tables->character[tables->chain[i]]);
            if (result < 0) return false;
            if (result == 0) return true;
        }

        tables->refs[code]++;

        /* Round-robin search for a childless entry to recycle. */
        slot = rover;
        for (;;) {
            slot++;
            if (slot > VMARC_LIMIT) {
                if (wrapped) break;
                wrapped = true;
                slot = VMARC_TOP;
            }
            if (tables->refs[slot] == 0) break;
        }

        if (slot > VMARC_LIMIT) {
            /* Nothing recyclable: the reference takes the reference count back
             * off and parks the pending entry in the scratch slot, so the lazy
             * fill above still has somewhere to land. */
            tables->refs[code]--;
            pending = VMARC_SCRATCH;
        } else {
            int old_parent = tables->parent[slot];
            if (old_parent != VMARC_PARENT_UNSET &&
                old_parent != VMARC_PARENT_ROOT) {
                tables->refs[old_parent]--;
            }
            tables->parent[slot] = code;
            rover = slot;
            pending = slot;
        }
    }
}

static bool vmarc_stored(const uint8_t *input, size_t input_size,
                         vmarc_sink *sink, size_t *end_offset)
{
    size_t position = 0U;

    while ((position + 2U) <= input_size) {
        size_t length = ((size_t)input[position] << 8) |
                        (size_t)input[position + 1U];
        position += 2U;
        if (length == 0U) {
            *end_offset = position;
            return true;
        }
        if ((position + length) > input_size) {
            *end_offset = position;
            return false;
        }
        if ((sink->count + length) > sink->limit) {
            *end_offset = position;
            sink->overflow = true;
            return false;
        }
        if (sink->output) {
            xx_rt_memcpy(sink->output + sink->count, input + position, length);
        }
        sink->count += length;
        position += length;
    }
    *end_offset = position;
    return false;
}

/*
 * One core routine for both entry points: with @p output NULL nothing is kept
 * and only the length accumulates.
 *
 * DEVIATION FROM THE REFERENCE, deliberate: XVMARCDecoder::run() returns false
 * for a member that never terminates yet its callers keep everything produced
 * up to the cut.  This library's contract forbids reporting a partial decode as
 * success, so that case fails here.  The end offset is still reported, because
 * the container walk needs it to find the next 80-byte-aligned header; a member
 * that terminates cleanly produces byte-for-byte the same output either way.
 */
static bool vmarc_core(const uint8_t *input, size_t input_size,
                       const xx_vmarc_params *params, uint8_t *output,
                       size_t limit, size_t *produced, size_t *consumed)
{
    static const xx_vmarc_params defaults = {0U, false, XX_VMARC_MODE_LZW};
    vmarc_sink sink;
    const xx_vmarc_params *use = params ? params : &defaults;
    bool ok;

    if (produced) *produced = 0U;
    if (consumed) *consumed = 0U;
    /* limit == 0 is legal: an empty member is normal in VMARC, and any byte
     * produced past the limit still trips the overflow check below. */
    if (!input) return false;

    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.output = output;
    sink.lrecl = (int)use->lrecl;
    sink.fixed = use->fixed;
    sink.limit = limit;

    if (use->mode == XX_VMARC_MODE_STORED) {
        size_t end_offset = 0U;
        ok = vmarc_stored(input, input_size, &sink, &end_offset);
        if (consumed) *consumed = end_offset;
        if (ok && produced) *produced = sink.count;
        return ok;
    }
    if (use->mode != XX_VMARC_MODE_LZW) return false;

    {
        vmarc_bits reader;
        vmarc_tables *tables =
            (vmarc_tables *)xx_mem_alloc(sizeof(*tables));
        if (!tables) return false;

        xx_rt_memset(&reader, 0, sizeof(reader));
        reader.input = input;
        reader.end = input_size;

        ok = vmarc_lzw(&reader, &sink, tables);
        xx_mem_free(tables);

        /* The RAW reader position, with no adjustment for bits still sitting
         * in the cache.  The reference reports exactly this and the container
         * rounds it up to the next 80-byte boundary to find the next member
         * header; subtracting buffered bytes here, as a byte-aligned codec
         * would, could land the walk on the wrong 80-byte slot. */
        if (consumed) *consumed = reader.position;
        if (ok && produced) *produced = sink.count;
        return ok;
    }
}

bool xx_vmarc_decode_memory_ex(const uint8_t *input, size_t input_size,
                               const xx_vmarc_params *params, uint8_t *output,
                               size_t output_size, size_t *written,
                               size_t *consumed)
{
    size_t produced = 0U;

    if (written) *written = 0U;
    if (consumed) *consumed = 0U;
    if (!output) return false;
    if (!vmarc_core(input, input_size, params, output, output_size, &produced,
                    consumed)) {
        return false;
    }
    if (produced != output_size) return false;
    if (written) *written = produced;
    return true;
}

bool xx_vmarc_scan_memory_ex(const uint8_t *input, size_t input_size,
                             const xx_vmarc_params *params, size_t max_output,
                             size_t *consumed, size_t *produced)
{
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (max_output == 0U) return false;
    if (max_output > XX_VMARC_MAX_UNCOMPRESSED_SIZE) {
        max_output = XX_VMARC_MAX_UNCOMPRESSED_SIZE;
    }
    return vmarc_core(input, input_size, params, NULL, max_output, produced,
                      consumed);
}

bool xx_vmarc_decode_memory(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size,
                            size_t *written)
{
    return xx_vmarc_decode_memory_ex(input, input_size, NULL, output,
                                     output_size, written, NULL);
}

bool xx_vmarc_scan_memory(const uint8_t *input, size_t input_size,
                          size_t max_output, size_t *consumed,
                          size_t *produced)
{
    bool ok = xx_vmarc_scan_memory_ex(input, input_size, NULL, max_output,
                                      consumed, produced);
    if (!ok) {
        if (consumed) *consumed = 0U;
        if (produced) *produced = 0U;
    }
    return ok;
}
