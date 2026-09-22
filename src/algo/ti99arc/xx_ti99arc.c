/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TI99 ARC (.ARK) LZW payload codec.  Ported from XTI99ARCDecoder /
 * XSharedLZWDecoder with the parameter block that family passes:
 *
 *     maxBits 12, CLEAR 0x100, END 0x101, MSB-FIRST, no RLE90 filter,
 *     no block padding, width-step bias 0
 *
 * Because block padding is OFF for this family the shared decoder's
 * group-of-eight padding logic can never run, so it is not reproduced here.
 * Everything else follows the reference code path for code path.
 */

#include "xxfclib/algo/ti99arc/xx_ti99arc.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define XX_TI99_MAX_BITS 12
#define XX_TI99_CAPACITY (1 << XX_TI99_MAX_BITS)
#define XX_TI99_CLEAR 0x100
#define XX_TI99_END 0x101
/* CLEAR and END both present, so the first assignable slot is 0x102. */
#define XX_TI99_FIRST_FREE 0x102
/* The phrase walk is guarded to at most one entry per dictionary slot, plus
 * the single byte the KwKwK case pushes first, plus the root byte. */
#define XX_TI99_STACK_SIZE (XX_TI99_CAPACITY + 2)

typedef struct {
    uint16_t prefix[XX_TI99_CAPACITY];
    uint8_t suffix[XX_TI99_CAPACITY];
    uint8_t stack[XX_TI99_STACK_SIZE];
} xx_ti99_tables;

typedef struct {
    const uint8_t *data;
    uint64_t bits;     /* total bits available */
    uint64_t bit_pos;  /* next bit to read */
    int width;
    int max_code;
    int free_slot;
} xx_ti99_reader;

static void xx_ti99_reader_init(xx_ti99_reader *r, const uint8_t *data, size_t size)
{
    r->data = data;
    r->bits = (uint64_t)size * 8u;
    r->bit_pos = 0;
    r->width = 9;
    r->max_code = 0x1ff;
    r->free_slot = XX_TI99_FIRST_FREE;
}

static void xx_ti99_reader_reset(xx_ti99_reader *r)
{
    r->width = 9;
    r->max_code = 0x1ff;
    r->free_slot = XX_TI99_FIRST_FREE;
}

static void xx_ti99_reader_add_entry(xx_ti99_reader *r)
{
    if (r->free_slot < XX_TI99_CAPACITY) ++r->free_slot;
}

/* Reads one code MSB-first.  A code that RUNS PAST the final byte is still
 * readable - the encoder pads the tail with zero bits - but a code that STARTS
 * past it is not.  That asymmetry is the reference's and it is what makes a
 * stream ending on a partial code a clean end rather than an error. */
static bool xx_ti99_reader_next(xx_ti99_reader *r, int *code)
{
    uint32_t value = 0;
    int i = 0;

    /* Width step: bias 0, i.e. step when the next free slot no longer fits. */
    if (r->max_code < r->free_slot) {
        ++r->width;
        r->max_code = (r->width == XX_TI99_MAX_BITS) ? XX_TI99_CAPACITY : ((1 << r->width) - 1);
    }

    if (r->bit_pos >= r->bits) return false;

    for (i = 0; i < r->width; ++i) {
        uint64_t bit = r->bit_pos + (uint64_t)i;
        uint32_t set = (bit >= r->bits) ? 0u : (uint32_t)((r->data[bit >> 3] >> (7 - (unsigned)(bit & 7))) & 1u);
        value = (value << 1) | set;
    }

    r->bit_pos += (uint64_t)r->width;
    *code = (int)value;

    return true;
}

/* The single core routine.  @p output may be NULL, in which case bytes are
 * counted and discarded - LZW needs no history window, the dictionary is
 * self-contained, so measuring costs nothing extra and can never disagree with
 * the decode.
 *
 * @p truncate_ok selects the two callers' contracts: the ARK reader expands a
 * PREFIX of the stream on purpose and a stop at the cap is its success; the
 * strict entry point treats the same stop as a failure.
 */
static bool xx_ti99_run(const uint8_t *input, size_t input_size, uint8_t *output, size_t cap, bool truncate_ok, size_t *written, bool *complete,
                        size_t *consumed)
{
    xx_ti99_tables *tables = NULL;
    xx_ti99_reader reader;
    size_t produced = 0;
    int previous = -1;
    uint8_t previous_first = 0;
    bool ended = false;
    bool ok = true;
    int i = 0;

    if (written) *written = 0;
    if (complete) *complete = false;
    if (consumed) *consumed = 0;

    if (!input || (input_size == 0)) return false;
    if (cap == 0) return false;

    tables = (xx_ti99_tables *)xx_mem_alloc(sizeof(xx_ti99_tables));
    if (!tables) return false;
    xx_rt_memset(tables->prefix, 0, sizeof(tables->prefix));
    xx_rt_memset(tables->suffix, 0, sizeof(tables->suffix));
    for (i = 0; i < 256; ++i) tables->suffix[i] = (uint8_t)i;

    xx_ti99_reader_init(&reader, input, input_size);

    while (produced < cap) {
        int code = 0;
        int current = 0;
        int guard = 0;
        size_t depth = 0;

        if (!xx_ti99_reader_next(&reader, &code)) {
            /* Code stream exhausted at a code boundary: a clean end. */
            ended = true;
            break;
        }
        if (code == XX_TI99_END) {
            ended = true;
            break;
        }

        if (code == XX_TI99_CLEAR) {
            xx_ti99_reader_reset(&reader);
            if (!xx_ti99_reader_next(&reader, &code)) {
                ended = true;
                break;
            }
            /* DELIBERATE ASYMMETRY, kept from the reference: a non-literal
             * code straight after a CLEAR ends the stream quietly, while the
             * same code at the very start of the stream is a hard failure
             * (below).  Do not "fix" one to match the other. */
            if (code > 0xff) {
                ended = true;
                break;
            }
            previous = code;
            previous_first = (uint8_t)code;
            if (output) output[produced] = (uint8_t)code;
            ++produced;
            continue;
        }

        if (previous < 0) {
            if (code > 0xff) {
                ok = false;
                break;
            }
            previous = code;
            previous_first = (uint8_t)code;
            if (output) output[produced] = (uint8_t)code;
            ++produced;
            continue;
        }

        current = code;
        if (code >= reader.free_slot) {
            /* KwKwK.  The byte appended is the PREVIOUS PHRASE'S FIRST BYTE,
             * not the low byte of the previous code.  The two agree whenever
             * the previous code was a literal, which is why the usual mistake
             * survives small members. */
            tables->stack[depth++] = previous_first;
            current = previous;
        }
        while (current > 0xff) {
            if ((current >= XX_TI99_CAPACITY) || (++guard > XX_TI99_CAPACITY) || (depth >= XX_TI99_STACK_SIZE)) {
                ok = false;
                break;
            }
            tables->stack[depth++] = tables->suffix[current];
            current = (int)tables->prefix[current];
        }
        if (!ok) break;
        if (depth >= XX_TI99_STACK_SIZE) {
            ok = false;
            break;
        }
        tables->stack[depth++] = (uint8_t)current;

        /* Emit reversed. */
        while (depth > 0) {
            if (produced >= cap) break;
            --depth;
            if (output) output[produced] = tables->stack[depth];
            ++produced;
        }
        if (depth > 0) {
            /* The buffer filled mid-phrase.  The reference lets the output run
             * past the requested size here and its caller discards the excess;
             * stopping exactly at the cap is output-equivalent for every use,
             * because the requested size always ends at or after the end of
             * the range the caller goes on to read. */
            if (!truncate_ok) ok = false;
            break;
        }

        if (reader.free_slot < XX_TI99_CAPACITY) {
            tables->prefix[reader.free_slot] = (uint16_t)previous;
            tables->suffix[reader.free_slot] = (uint8_t)current;
            xx_ti99_reader_add_entry(&reader);
        }
        previous = code;
        previous_first = (uint8_t)current;
    }

    if (ok && !ended && (produced >= cap) && !truncate_ok) {
        /* Filled the buffer exactly on a phrase boundary but the stream has
         * more to say: still an overflow for the strict contract. */
        int probe = 0;
        xx_ti99_reader look = reader;
        if (xx_ti99_reader_next(&look, &probe) && (probe != XX_TI99_END)) ok = false;
        else ended = true;
    }

    if (consumed) {
        uint64_t bytes = (reader.bit_pos + 7u) / 8u;
        if (bytes > (uint64_t)input_size) bytes = (uint64_t)input_size;
        *consumed = (size_t)bytes;
    }

    xx_mem_free(tables);

    if (!ok) return false;
    if (produced == 0) return false;

    if (written) *written = produced;
    if (complete) *complete = ended;

    return true;
}

bool xx_ti99arc_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written)
{
    bool complete = false;
    size_t produced = 0;

    if (written) *written = 0;
    if (!output) return false;

    if (!xx_ti99_run(input, input_size, output, output_size, false, &produced, &complete, NULL)) return false;
    if (!complete) return false;

    if (written) *written = produced;

    return true;
}

bool xx_ti99arc_expand_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written, bool *complete)
{
    if (written) *written = 0;
    if (complete) *complete = false;
    if (!output) return false;

    return xx_ti99_run(input, input_size, output, output_size, true, written, complete, NULL);
}

bool xx_ti99arc_scan_memory(const uint8_t *input, size_t input_size, size_t max_output, size_t *consumed, size_t *produced)
{
    bool complete = false;
    size_t local_produced = 0;
    size_t local_consumed = 0;

    if (consumed) *consumed = 0;
    if (produced) *produced = 0;

    if (!xx_ti99_run(input, input_size, NULL, max_output, false, &local_produced, &complete, &local_consumed)) return false;
    if (!complete) return false;

    if (consumed) *consumed = local_consumed;
    if (produced) *produced = local_produced;

    return true;
}

static uint32_t xx_ti99_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool xx_ti99arc_decode_member(const uint8_t *input, size_t input_size, const uint8_t *props, size_t props_size, uint8_t *output, size_t output_size,
                              size_t *written)
{
    const size_t header = 0x10;
    uint32_t plain_size = 0;
    uint32_t member_offset = 0;
    uint32_t member_size = 0;
    uint32_t prefix_size = 0;
    uint8_t *plain = NULL;
    size_t expanded = 0;
    size_t total = 0;
    bool complete = false;

    if (written) *written = 0;
    if (!input || !props || !output) return false;
    if (props_size < header) return false;

    plain_size = xx_ti99_read_u32(props + 0);
    member_offset = xx_ti99_read_u32(props + 4);
    member_size = xx_ti99_read_u32(props + 8);
    prefix_size = xx_ti99_read_u32(props + 12);

    if ((size_t)prefix_size > (props_size - header)) return false;
    if ((plain_size == 0) || (plain_size > XX_TI99ARC_MAX_PLAIN_SIZE)) return false;
    if (member_size > plain_size) return false;
    if (member_offset > (plain_size - member_size)) return false;

    total = (size_t)prefix_size + (size_t)member_size;
    if (total > output_size) return false;

    plain = (uint8_t *)xx_mem_alloc((size_t)plain_size);
    if (!plain) return false;

    /* Deliberately a PREFIX expansion: the reference's expand() ignores the
     * "did it produce exactly this many bytes" result and only requires that
     * the member's own range be present. */
    if (!xx_ti99_run(input, input_size, plain, (size_t)plain_size, true, &expanded, &complete, NULL)) {
        xx_mem_free(plain);
        return false;
    }
    if (((size_t)member_offset > expanded) || ((size_t)member_size > (expanded - (size_t)member_offset))) {
        xx_mem_free(plain);
        return false;
    }

    if (prefix_size > 0) xx_rt_memcpy(output, props + header, (size_t)prefix_size);
    if (member_size > 0) xx_rt_memcpy(output + prefix_size, plain + member_offset, (size_t)member_size);

    xx_mem_free(plain);

    if (written) *written = total;

    return true;
}
