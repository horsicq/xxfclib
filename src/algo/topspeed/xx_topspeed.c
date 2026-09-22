/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TopSpeed member codec, ported from XArchive/Algos/xtopspeeddecoder.cpp.
 *
 * Deliberately kept from the reference, do not "fix":
 *
 *  - The 0x4600 compressed-size cap is applied ONLY when the block is really
 *    compressed (packed < plain).  A stored block's "compressed" field is a
 *    hypothetical figure the packer never used and is legitimately larger, so
 *    checking it unconditionally rejects valid members.
 *  - The checksum covers min(plain, packed) bytes, i.e. exactly what is on
 *    disk, and is a plain 16-bit truncated sum of bytes, not a CRC.
 *  - The first code of a block, and the first code after a dictionary restart,
 *    is emitted as a LITERAL - only its low eight bits reach the output, while
 *    the full 12-bit value is stored as prefix[0x100].  A 12-bit seed code is
 *    therefore not an error; its high nibble is simply dropped from the output
 *    and kept in the table.  That looks like a bug and is load-bearing.
 *  - The restart test (next == 0xfff) happens AFTER the code is read, so the
 *    code that trips the restart is not discarded: it becomes the literal seed
 *    of the fresh dictionary.
 *  - prefix[next + 1] is written BEFORE the current code is expanded, while
 *    suffix[next] is written after.  Entry `next` therefore ends up as
 *    (previous code, first byte of the current phrase), which is the ordinary
 *    LZW rule expressed with a one-step stagger.  Keep both halves together.
 *  - The per-block tables are freshly zeroed.  After a restart the walk can
 *    still meet entries left over from the previous generation (prefix[0x100]
 *    may point high), and the only thing that stops a cyclic chain is the
 *    stack-capacity test.  That capacity is 0x1001, one more than the
 *    dictionary, and it is what turns a malformed stream into a clean failure
 *    rather than a hang.
 *
 * The reference builds the plaintext in a growing QByteArray and only then
 * compares its length with the block's declared plaintext size, so a phrase
 * that overshoots always ends in failure.  This port fails on the first byte
 * that would exceed the block's declared size, which is output-equivalent: the
 * length can only grow, so once it has overshot the final equality test can
 * never succeed.  (A restart can never overshoot - it is only reached while
 * produced < plain - so that path needs no such test in either version.)
 *
 * One intentional difference: the reference refuses a member whose measured
 * size does not fit qint32.  This port caps the decoded size against the
 * caller's output_size and against scan's max_output, and has no other ceiling.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/topspeed/xx_topspeed.h"

#define TS_HEADER_SIZE 6
#define TS_MAX_UNCOMPRESSED 0x3800
#define TS_MAX_COMPRESSED 0x4600
#define TS_DICT_SIZE 0x1000
#define TS_DICT_FIRST 0x100
#define TS_DICT_LAST 0xfff
#define TS_TABLE_SIZE (TS_DICT_SIZE + 1) /* 0x1001, as in the reference */
#define TS_MAX_BLOCKS 100000

typedef struct ts_reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint8_t high;
    int pair;
} ts_reader;

typedef struct ts_dict {
    uint16_t prefix[TS_TABLE_SIZE];
    uint8_t suffix[TS_TABLE_SIZE];
    uint8_t stack[TS_TABLE_SIZE];
} ts_dict;

/* Two codes per three bytes; the leading byte holds both high nibbles.
 * -1 on exhaustion. */
static int ts_read_code(ts_reader *reader)
{
    int code;

    if (reader->pair == 0) {
        if (reader->pos >= reader->size) return -1;
        reader->high = reader->data[reader->pos];
        reader->pos++;
        reader->pair = 2;
    }
    if (reader->pos >= reader->size) return -1;
    code = (int)((((uint32_t)(reader->high & 0xf0)) << 4) |
                 (uint32_t)reader->data[reader->pos]);
    reader->pos++;
    reader->high = (uint8_t)((reader->high << 4) & 0xff);
    reader->pair--;

    return code;
}

/* Walk the block chain, validating headers and checksums.  This is the
 * reference's measure(); decode() runs it first and then decodes, so the two
 * can never disagree about the member's length or its validity. */
static bool ts_walk(const uint8_t *input, size_t input_size, size_t max_output,
                    size_t *total_out)
{
    size_t pos = 0U;
    size_t total = 0U;
    uint32_t blocks = 0U;

    if (input_size < (size_t)TS_HEADER_SIZE) return false;

    while (pos < input_size) {
        uint32_t checksum;
        uint32_t plain;
        uint32_t packed;
        uint32_t on_disk;
        uint32_t sum = 0U;
        const uint8_t *payload;
        uint32_t i;

        if ((input_size - pos) < (size_t)TS_HEADER_SIZE) return false;
        checksum = (uint32_t)input[pos] | ((uint32_t)input[pos + 1] << 8);
        plain = (uint32_t)input[pos + 2] | ((uint32_t)input[pos + 3] << 8);
        packed = (uint32_t)input[pos + 4] | ((uint32_t)input[pos + 5] << 8);

        if ((plain == 0U) || (packed == 0U)) return false;
        if (plain > (uint32_t)TS_MAX_UNCOMPRESSED) return false;
        /* the compressed cap applies ONLY to a real compressed block */
        if ((packed < plain) && (packed > (uint32_t)TS_MAX_COMPRESSED)) {
            return false;
        }

        on_disk = (packed < plain) ? packed : plain;
        if ((input_size - pos - (size_t)TS_HEADER_SIZE) < (size_t)on_disk) {
            return false;
        }

        payload = input + pos + TS_HEADER_SIZE;
        for (i = 0U; i < on_disk; ++i) sum += (uint32_t)payload[i];
        if ((sum & 0xffffU) != checksum) return false;

        /* total <= max_output is an invariant here, so the subtraction on the
         * right cannot wrap */
        if ((size_t)plain > (max_output - total)) return false;
        total += (size_t)plain;
        pos += (size_t)TS_HEADER_SIZE + (size_t)on_disk;
        blocks++;
        if (blocks > (uint32_t)TS_MAX_BLOCKS) return false;
    }

    if ((pos != input_size) || (blocks == 0U) || (total == 0U)) return false;

    *total_out = total;

    return true;
}

/* One compressed block.  `limit` is the block's declared plaintext size; the
 * caller has already proved that many bytes fit in the output buffer. */
static bool ts_decode_block(const uint8_t *payload, size_t payload_size,
                            uint32_t limit, ts_dict *dict, uint8_t *output,
                            size_t *produced)
{
    ts_reader reader;
    int code;
    uint32_t emitted = 0U;

    xx_rt_memset(dict->prefix, 0, sizeof(dict->prefix));
    xx_rt_memset(dict->suffix, 0, sizeof(dict->suffix));
    xx_rt_memset(dict->stack, 0, sizeof(dict->stack));

    reader.data = payload;
    reader.size = payload_size;
    reader.pos = 0U;
    reader.high = 0U;
    reader.pair = 0;

    code = ts_read_code(&reader);
    if (code < 0) return false;

    for (;;) {
        int next = TS_DICT_FIRST;
        int previous_first;
        bool restart = false;

        /* a restart, and the very first code of the block, always emit a
         * literal and re-seed the dictionary */
        dict->prefix[TS_DICT_FIRST] = (uint16_t)code;
        output[*produced] = (uint8_t)(code & 0xff);
        (*produced)++;
        emitted++;
        previous_first = code & 0xff;

        while (emitted < limit) {
            int first = 0;
            int stack_top = 0;
            int walk;

            code = ts_read_code(&reader);
            if (code < 0) return false;

            if (next == TS_DICT_LAST) {
                restart = true;
                break;
            }
            dict->prefix[next + 1] = (uint16_t)code;

            if (code < TS_DICT_FIRST) {
                if (emitted >= limit) return false;
                output[*produced] = (uint8_t)code;
                (*produced)++;
                emitted++;
                dict->suffix[next] = (uint8_t)code;
                first = code;
            } else {
                if (code > next) return false;
                walk = code;
                if (code == next) {
                    dict->stack[stack_top] = (uint8_t)previous_first;
                    stack_top++;
                    walk = (int)dict->prefix[next];
                }
                while (walk > 0xff) {
                    if ((stack_top >= TS_TABLE_SIZE) || (walk > TS_DICT_LAST)) {
                        return false;
                    }
                    dict->stack[stack_top] = dict->suffix[walk];
                    stack_top++;
                    walk = (int)dict->prefix[walk];
                }
                first = walk;
                if (emitted >= limit) return false;
                output[*produced] = (uint8_t)walk;
                (*produced)++;
                emitted++;
                dict->suffix[next] = (uint8_t)walk;
                while (stack_top > 0) {
                    stack_top--;
                    if (emitted >= limit) return false;
                    output[*produced] = dict->stack[stack_top];
                    (*produced)++;
                    emitted++;
                }
            }
            next++;
            previous_first = first;
        }

        if (!restart) break;
    }

    return emitted == limit;
}

bool xx_topspeed_scan_memory(const uint8_t *input, size_t input_size,
                             size_t max_output, size_t *consumed,
                             size_t *produced)
{
    size_t total = 0U;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!input && (input_size != 0U)) return false;
    if (!ts_walk(input, input_size, max_output, &total)) return false;

    if (consumed) *consumed = input_size;
    if (produced) *produced = total;

    return true;
}

bool xx_topspeed_decode_memory(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               size_t *written)
{
    ts_dict *dict;
    size_t total = 0U;
    size_t pos = 0U;
    size_t produced = 0U;
    bool ok = true;

    if (written) *written = 0U;
    if (!output) return false;
    if (!input && (input_size != 0U)) return false;
    if (output_size == 0U) return false;

    if (!ts_walk(input, input_size, output_size, &total)) return false;
    if (total != output_size) return false;

    dict = (ts_dict *)xx_mem_alloc(sizeof(ts_dict));
    if (!dict) return false;

    while (pos < input_size) {
        uint32_t plain;
        uint32_t packed;
        uint32_t on_disk;
        const uint8_t *payload;

        plain = (uint32_t)input[pos + 2] | ((uint32_t)input[pos + 3] << 8);
        packed = (uint32_t)input[pos + 4] | ((uint32_t)input[pos + 5] << 8);
        on_disk = (packed < plain) ? packed : plain;
        payload = input + pos + TS_HEADER_SIZE;

        /* ts_walk has proved the totals, so this can only be an equality */
        if ((output_size - produced) < (size_t)plain) {
            ok = false;
            break;
        }

        if (packed >= plain) {
            xx_rt_memcpy(output + produced, payload, (size_t)plain);
            produced += (size_t)plain;
        } else if (!ts_decode_block(payload, (size_t)on_disk, plain, dict,
                                    output, &produced)) {
            ok = false;
            break;
        }

        pos += (size_t)TS_HEADER_SIZE + (size_t)on_disk;
    }

    xx_mem_free(dict);

    if (!ok || (produced != output_size)) return false;
    if (written) *written = produced;

    return true;
}
