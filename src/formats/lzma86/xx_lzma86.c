/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LZMA86 streams (.lzma86): the LZMA SDK's Lzma86 container, an LZMA-alone
 * stream with one filter byte in front (0 = none, 1 = x86 BCJ).  7-Zip
 * opens it as the "lzma86" type.
 *
 *   +0   filter byte      0 or 1
 *   +1   properties byte  (pb * 5 + lp) * 9 + lc, below 225
 *   +2   u32 LE           dictionary size
 *   +6   u64 LE           uncompressed size; all ones = unknown
 *   +14  range-coded LZMA data
 *
 * The LZMA SDK encoder (Lzma86_Encode) writes a known size and no end
 * marker; an end marker may still follow the last byte, and an unknown
 * size needs one.  Either way a properly flushed range coder ends with its
 * code register at zero, which is what makes the end of a stream, and so
 * its exact compressed length, checkable.  With filter 1 the decoded bytes
 * go through the x86 BCJ decoder, start address 0, state reset per stream.
 *
 * The file has no signature, so the header has to pass the test 7-Zip
 * applies before it takes a file as lzma86 (filter byte 0/1, properties
 * below 225, a dictionary size of 2^n or 3 * 2^n -- plus the whole-MiB
 * sizes the SDK encoder writes for 2 MiB and more -- a size below 2^56 or
 * unknown, a zero first range-coder byte and, for a known non-zero size, a
 * clear top bit in the second), and detection then trial-decodes the first
 * XX_LZMA86_PROBE_OUTPUT bytes.  Data made only of zero bytes, which
 * "decodes" cleanly to any size, is only taken for the tiny whole files
 * encoders really write that way.
 *
 * Consecutive streams decode as one payload, the way 7-Zip does: after the
 * first one, another is followed while its header passes the same test and
 * it decodes to a clean finish.  Anything else after the last stream is
 * reported as overlay.
 *
 * The LZMA stream decoder follows the one in src/formats/lzma/xx_lzma.c
 * (this library, MIT), which was written from the LZMA format description
 * (LZMA SDK lzma-specification.txt, public domain); the x86 BCJ decoder is
 * the one in src/formats/7zip/xx_7zip_branch.c (this library, MIT), made
 * resumable so that it can filter the output as it streams.
 *
 * Hostile input: the dictionary window is allocated as output arrives and
 * never above XX_LZMA86_WINDOW_MAX; every match distance is checked against
 * the bytes actually held; a known size bounds its stream's output and a
 * header claiming more than XX_LZMA86_MAX_RATIO output bytes per input byte
 * is refused before any decoding; the input is bounded by the device; the
 * number of streams and the total model setup are capped.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lzma86/xx_lzma86.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested. */
#ifdef LZMA86
#define XX_LZMA86_FILE_TYPE XX_FILE_TYPE_LZMA86
#else
#define XX_LZMA86_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_LZMA86_PAYLOAD_NAME "payload"

#define XX_LZMA86_HEADER XX_LZMA86_HEADER_SIZE
#define XX_LZMA86_RC_INIT_SIZE 5U
#define XX_LZMA86_MIN_STREAM (XX_LZMA86_HEADER + XX_LZMA86_RC_INIT_SIZE)
#define XX_LZMA86_UNKNOWN_SIZE UINT64_MAX
/* 7-Zip refuses a declared size of 2^56 or more; so does this reader. */
#define XX_LZMA86_SIZE_LIMIT ((uint64_t)1 << 56)
#define XX_LZMA86_DICT_MIN 4096U
#define XX_LZMA86_WINDOW_MAX ((uint32_t)512U * 1024U * 1024U)
#define XX_LZMA86_WINDOW_FIRST ((uint32_t)64U * 1024U)
#define XX_LZMA86_MAX_STREAMS 65536U
#define XX_LZMA86_MODEL_BUDGET ((uint64_t)64U * 1024U * 1024U)
#define XX_LZMA86_PROBE_OUTPUT ((uint64_t)16U * 1024U)
#define XX_LZMA86_PD_STEP ((uint64_t)1U << 20)
/* 7-Zip's open test: more than this many data bytes behind a header that
 * declares an empty payload with properties byte 0 is not a stream. */
#define XX_LZMA86_EMPTY_PROPS0_SLACK 10

/* One data byte can never yield more than about 7630 output bytes (see
 * src/formats/lzma/xx_lzma.c); a header claiming more is refused. */
#define XX_LZMA86_MAX_RATIO 8192U

/* Probability model layout (one uint16_t array per stream). */
#define XX_LZMA86_STATES 12U
#define XX_LZMA86_POS_STATES 16U
#define XX_LZMA86_LEN_CODER (2U + 2U * XX_LZMA86_POS_STATES * 8U + 256U)
#define XX_LZMA86_P_IS_MATCH 0U
#define XX_LZMA86_P_IS_REP (XX_LZMA86_P_IS_MATCH + XX_LZMA86_STATES * XX_LZMA86_POS_STATES)
#define XX_LZMA86_P_IS_REP_G0 (XX_LZMA86_P_IS_REP + XX_LZMA86_STATES)
#define XX_LZMA86_P_IS_REP_G1 (XX_LZMA86_P_IS_REP_G0 + XX_LZMA86_STATES)
#define XX_LZMA86_P_IS_REP_G2 (XX_LZMA86_P_IS_REP_G1 + XX_LZMA86_STATES)
#define XX_LZMA86_P_IS_REP0_LONG (XX_LZMA86_P_IS_REP_G2 + XX_LZMA86_STATES)
#define XX_LZMA86_P_POS_SLOT (XX_LZMA86_P_IS_REP0_LONG + XX_LZMA86_STATES * XX_LZMA86_POS_STATES)
#define XX_LZMA86_P_SPEC_POS (XX_LZMA86_P_POS_SLOT + 4U * 64U)
#define XX_LZMA86_P_ALIGN (XX_LZMA86_P_SPEC_POS + 115U)
#define XX_LZMA86_P_LEN (XX_LZMA86_P_ALIGN + 16U)
#define XX_LZMA86_P_REP_LEN (XX_LZMA86_P_LEN + XX_LZMA86_LEN_CODER)
#define XX_LZMA86_P_LITERAL (XX_LZMA86_P_REP_LEN + XX_LZMA86_LEN_CODER)

#define XX_LZMA86_MARKER_DISTANCE UINT32_C(0xFFFFFFFF)

/* Data consisting only of zero bytes keeps the code register at zero and
 * so ends cleanly at any declared size; no encoder writes that for more
 * than two output bytes. */
#define XX_LZMA86_ZERO_DATA_LIMIT 16U
#define XX_LZMA86_ZERO_DATA_OUTPUT 2U

/* BCJ staging buffer: output is filtered in chunks of this size. */
#define XX_LZMA86_BCJ_CHUNK ((size_t)64U * 1024U)

typedef struct lzma86_header_s {
    unsigned filter;
    unsigned lc;
    unsigned lp;
    unsigned pb;
    uint32_t dictionary_size;
    uint64_t declared_size;
} lzma86_header;

typedef enum lzma86_result_e {
    LZMA86_FAILED = 0,
    LZMA86_FINISHED = 1,
    LZMA86_PARTIAL = 2
} lzma86_result;

typedef struct lzma86_decoder_s {
    xx_io_device *device;
    int64_t in_offset;
    int64_t in_end;
    size_t in_pos;
    size_t in_len;
    uint64_t consumed;
    uint8_t data_bits;
    bool in_failed;
    uint32_t range;
    uint32_t code;
    uint16_t *probs;
    size_t probs_count;
    uint8_t *window;
    uint32_t window_size;
    uint32_t window_limit;
    uint32_t window_pos;
    uint32_t flush_pos;
    bool wrapped;
    uint64_t produced;
    uint64_t next_pd_check;
    xx_io_device *destination;
    bool output_failed;
    bool alloc_failed;
    xx_pd_struct *pd;
    uint8_t *in_buffer;
    size_t io_capacity;
} lzma86_decoder;

/* ------------------------------------------------------------- x86 BCJ */

typedef struct lzma86_bcj_s {
    xx_io_device *output;
    uint8_t *buffer;
    size_t length;
    uint32_t ip;   /* stream position of buffer[0] */
    uint32_t mask; /* recent E8/E9 bytes, relative to buffer[0] */
    bool failed;
} lzma86_bcj;

static bool lzma86_x86_ms_byte(uint8_t value)
{
    return ((uint8_t)(value + 1U) & 0xFEU) == 0U;
}

/* One pass of the x86 BCJ decoder over data[0, size).  Returns how many
 * leading bytes are final; the rest (at most four) must be presented again,
 * followed by the next bytes, with ip advanced by the returned count.
 * *state carries the E8/E9 history across calls, relative to the returned
 * position. */
static size_t lzma86_bcj_step(uint8_t *data, size_t size, uint32_t ip, uint32_t *state)
{
    size_t pos = 0U;
    size_t limit;
    uint32_t mask = *state;
    if (size < 5U) return 0U;
    limit = size - 4U;
    ip += 5U;
    while (pos < limit) {
        size_t candidate = pos;
        size_t distance;
        uint32_t value;
        uint32_t current;
        while (candidate < limit && (data[candidate] & 0xFEU) != 0xE8U) {
            ++candidate;
        }
        distance = candidate - pos;
        if (candidate >= limit) {
            mask = distance > 2U ? 0U : (mask >> (unsigned)distance);
            pos = candidate;
            break;
        }
        pos = candidate;
        if (distance > 2U) {
            mask = 0U;
        } else {
            mask >>= (unsigned)distance;
            if (mask != 0U && (mask > 4U || mask == 3U || lzma86_x86_ms_byte(data[candidate + (mask >> 1U) + 1U]))) {
                mask = (mask >> 1U) | 4U;
                ++pos;
                continue;
            }
        }
        if (!lzma86_x86_ms_byte(data[candidate + 4U])) {
            mask = (mask >> 1U) | 4U;
            ++pos;
            continue;
        }
        value =
            (uint32_t)data[candidate + 1U] | ((uint32_t)data[candidate + 2U] << 8U) | ((uint32_t)data[candidate + 3U] << 16U) | ((uint32_t)data[candidate + 4U] << 24U);
        current = ip + (uint32_t)candidate;
        value -= current;
        if (mask != 0U) {
            unsigned shift = (mask & 6U) << 2U;
            if (lzma86_x86_ms_byte((uint8_t)(value >> shift))) {
                value ^= ((UINT32_C(0x100) << shift) - 1U);
                value -= current;
            }
            mask = 0U;
        }
        data[candidate + 1U] = (uint8_t)value;
        data[candidate + 2U] = (uint8_t)(value >> 8U);
        data[candidate + 3U] = (uint8_t)(value >> 16U);
        data[candidate + 4U] = (uint8_t)(0U - ((value >> 24U) & 1U));
        pos = candidate + 5U;
    }
    *state = mask;
    return pos;
}

static bool lzma86_write_all(xx_io_device *output, const uint8_t *data, size_t size)
{
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(output, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool lzma86_bcj_drain(lzma86_bcj *bcj)
{
    size_t ready = lzma86_bcj_step(bcj->buffer, bcj->length, bcj->ip, &bcj->mask);
    if (ready == 0U) return true;
    if (!lzma86_write_all(bcj->output, bcj->buffer, ready)) return false;
    xx_rt_memmove(bcj->buffer, bcj->buffer + ready, bcj->length - ready);
    bcj->length -= ready;
    bcj->ip += (uint32_t)ready;
    return true;
}

static ssize_t lzma86_bcj_write(xx_io_device *device, const void *data, size_t size)
{
    lzma86_bcj *bcj = device ? (lzma86_bcj *)device->priv : NULL;
    const uint8_t *bytes = (const uint8_t *)data;
    size_t done = 0U;
    if (!bcj || bcj->failed || (!data && size != 0U) || size > ((size_t)-1 >> 1U)) {
        if (bcj) bcj->failed = true;
        return -1;
    }
    while (done < size) {
        size_t room = XX_LZMA86_BCJ_CHUNK - bcj->length;
        size_t take = size - done < room ? size - done : room;
        xx_rt_memcpy(bcj->buffer + bcj->length, bytes + done, take);
        bcj->length += take;
        done += take;
        if (bcj->length == XX_LZMA86_BCJ_CHUNK && !lzma86_bcj_drain(bcj)) {
            bcj->failed = true;
            return -1;
        }
    }
    return (ssize_t)size;
}

/* End of a stream: filter what is left; its last (up to four) bytes are
 * never an instruction and go out unchanged. */
static bool lzma86_bcj_finish(lzma86_bcj *bcj)
{
    if (bcj->failed || !lzma86_bcj_drain(bcj) || !lzma86_write_all(bcj->output, bcj->buffer, bcj->length)) {
        bcj->failed = true;
        return false;
    }
    bcj->length = 0U;
    return true;
}

static void lzma86_bcj_reset(lzma86_bcj *bcj)
{
    bcj->length = 0U;
    bcj->ip = 0U;
    bcj->mask = 0U;
    bcj->failed = false;
}

/* -------------------------------------------------------------- header */

static bool lzma86_read_exact_at(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    size_t done = 0U;
    const size_t io_capacity = xx_get_file_buffer_size();
    if (!device || (!data && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (request > io_capacity) request = io_capacity;
        amount = xx_io_read(device, (uint8_t *)data + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

/* 7-Zip's dictionary test (1, 2^n, 3 * 2^n, all ones) plus the whole-MiB
 * sizes of 2 MiB and more that the LZMA SDK encoder writes. */
static bool lzma86_dictionary_ok(uint32_t size)
{
    unsigned shift;
    if (size == 1U || size == UINT32_C(0xFFFFFFFF)) return true;
    for (shift = 0U; shift <= 30U; ++shift) {
        if (size == (UINT32_C(2) << shift) || size == (UINT32_C(3) << shift)) {
            return true;
        }
    }
    return size >= (UINT32_C(1) << 21U) && (size & UINT32_C(0xFFFFF)) == 0U;
}

static bool lzma86_parse_header(const uint8_t *data, lzma86_header *header)
{
    unsigned value;
    if (!data || !header || data[0] > 1U || data[1] >= 9U * 5U * 5U) {
        return false;
    }
    header->filter = data[0];
    value = data[1];
    header->lc = value % 9U;
    value /= 9U;
    header->lp = value % 5U;
    header->pb = value / 5U;
    header->dictionary_size = xx_data_get_u32(data + 2U, 4, 0, false);
    header->declared_size = xx_data_get_u64(data + 6U, 8, 0, false);
    return lzma86_dictionary_ok(header->dictionary_size) && (header->declared_size == XX_LZMA86_UNKNOWN_SIZE || header->declared_size < XX_LZMA86_SIZE_LIMIT);
}

bool xx_lzma86_has_header(const uint8_t *data, size_t size)
{
    lzma86_header header;
    if (!data || size < (size_t)XX_LZMA86_HEADER + 2U || !lzma86_parse_header(data, &header) || data[XX_LZMA86_HEADER] != 0U) {
        return false;
    }
    return header.declared_size == XX_LZMA86_UNKNOWN_SIZE || header.declared_size == 0U || (data[XX_LZMA86_HEADER + 1U] & 0x80U) == 0U;
}

/* Header grammar plus the declared size held against the data available. */
static bool lzma86_stream_start_ok(const uint8_t *data, lzma86_header *header, int64_t available)
{
    uint64_t data_size;
    if (available < (int64_t)XX_LZMA86_MIN_STREAM || !xx_lzma86_has_header(data, XX_LZMA86_MIN_STREAM) || !lzma86_parse_header(data, header)) {
        return false;
    }
    if (header->declared_size == XX_LZMA86_UNKNOWN_SIZE) return true;
    data_size = (uint64_t)available - (uint64_t)XX_LZMA86_HEADER;
    return header->declared_size / XX_LZMA86_MAX_RATIO <= data_size;
}

/* ---------------------------------------------------------------- input */

static bool lzma86_refill(lzma86_decoder *decoder)
{
    int64_t available = decoder->in_end - decoder->in_offset;
    size_t request;
    if (available <= 0) return false;
    request = available < (int64_t)decoder->io_capacity ? (size_t)available : decoder->io_capacity;
    if (!lzma86_read_exact_at(decoder->device, decoder->in_offset, decoder->in_buffer, request)) {
        return false;
    }
    decoder->in_offset += (int64_t)request;
    decoder->in_pos = 0U;
    decoder->in_len = request;
    return true;
}

static uint8_t lzma86_next_byte(lzma86_decoder *decoder)
{
    uint8_t value;
    if (decoder->in_pos == decoder->in_len && !lzma86_refill(decoder)) {
        decoder->in_failed = true;
        return 0U;
    }
    value = decoder->in_buffer[decoder->in_pos++];
    if (decoder->consumed++ != 0U) decoder->data_bits |= value;
    return value;
}

/* --------------------------------------------------------- range decoder */

static void lzma86_normalize(lzma86_decoder *decoder)
{
    if (decoder->range < (UINT32_C(1) << 24U)) {
        decoder->range <<= 8U;
        decoder->code = (decoder->code << 8U) | lzma86_next_byte(decoder);
    }
}

static unsigned lzma86_bit(lzma86_decoder *decoder, uint16_t *prob)
{
    uint32_t bound = (decoder->range >> 11U) * (uint32_t)*prob;
    unsigned bit;
    if (decoder->code < bound) {
        decoder->range = bound;
        *prob = (uint16_t)(*prob + ((2048U - *prob) >> 5U));
        bit = 0U;
    } else {
        decoder->range -= bound;
        decoder->code -= bound;
        *prob = (uint16_t)(*prob - (*prob >> 5U));
        bit = 1U;
    }
    lzma86_normalize(decoder);
    return bit;
}

static uint32_t lzma86_direct_bits(lzma86_decoder *decoder, unsigned count)
{
    uint32_t result = 0U;
    while (count-- > 0U) {
        decoder->range >>= 1U;
        result <<= 1U;
        if (decoder->code >= decoder->range) {
            decoder->code -= decoder->range;
            result |= 1U;
        }
        lzma86_normalize(decoder);
    }
    return result;
}

static uint32_t lzma86_tree(lzma86_decoder *decoder, uint16_t *probs, unsigned bits)
{
    uint32_t node = 1U;
    unsigned index;
    for (index = 0U; index < bits; ++index) {
        node = (node << 1U) | lzma86_bit(decoder, probs + node);
    }
    return node - (UINT32_C(1) << bits);
}

static uint32_t lzma86_tree_reverse(lzma86_decoder *decoder, uint16_t *probs, unsigned bits)
{
    uint32_t node = 1U;
    uint32_t result = 0U;
    unsigned index;
    for (index = 0U; index < bits; ++index) {
        unsigned bit = lzma86_bit(decoder, probs + node);
        node = (node << 1U) | bit;
        result |= (uint32_t)bit << index;
    }
    return result;
}

static uint32_t lzma86_length(lzma86_decoder *decoder, uint16_t *coder, unsigned pos_state)
{
    if (!lzma86_bit(decoder, coder)) {
        return lzma86_tree(decoder, coder + 2U + pos_state * 8U, 3U);
    }
    if (!lzma86_bit(decoder, coder + 1U)) {
        return 8U + lzma86_tree(decoder, coder + 2U + XX_LZMA86_POS_STATES * 8U + pos_state * 8U, 3U);
    }
    return 16U + lzma86_tree(decoder, coder + 2U + 2U * XX_LZMA86_POS_STATES * 8U, 8U);
}

/* length is the coded length (actual length minus two). */
static uint32_t lzma86_distance(lzma86_decoder *decoder, uint32_t length)
{
    uint32_t len_state = length < 4U ? length : 3U;
    uint32_t slot = lzma86_tree(decoder, decoder->probs + XX_LZMA86_P_POS_SLOT + len_state * 64U, 6U);
    unsigned direct;
    uint32_t distance;
    if (slot < 4U) return slot;
    direct = (unsigned)(slot >> 1U) - 1U;
    distance = (2U | (slot & 1U)) << direct;
    if (slot < 14U) {
        return distance + lzma86_tree_reverse(decoder, decoder->probs + XX_LZMA86_P_SPEC_POS + distance - slot, direct);
    }
    distance += lzma86_direct_bits(decoder, direct - 4U) << 4U;
    return distance + lzma86_tree_reverse(decoder, decoder->probs + XX_LZMA86_P_ALIGN, 4U);
}

/* ---------------------------------------------------------------- window */

static bool lzma86_flush(lzma86_decoder *decoder, uint32_t end)
{
    uint32_t at = decoder->flush_pos;
    while (decoder->destination && at < end) {
        ssize_t amount = xx_io_write(decoder->destination, decoder->window + at, end - at);
        if (amount <= 0 || (size_t)amount > (size_t)(end - at)) {
            decoder->output_failed = true;
            return false;
        }
        at += (uint32_t)amount;
    }
    decoder->flush_pos = end;
    return true;
}

static bool lzma86_window_advance(lzma86_decoder *decoder)
{
    if (!lzma86_flush(decoder, decoder->window_size)) return false;
    if (decoder->window_size < decoder->window_limit) {
        uint32_t size = decoder->window_limit - decoder->window_size > decoder->window_size ? decoder->window_size * 2U : decoder->window_limit;
        uint8_t *window = (uint8_t *)xx_mem_alloc(size);
        if (!window) {
            decoder->alloc_failed = true;
            return false;
        }
        xx_rt_memcpy(window, decoder->window, decoder->window_size);
        xx_mem_free(decoder->window);
        decoder->window = window;
        decoder->window_size = size;
        return true;
    }
    decoder->window_pos = 0U;
    decoder->flush_pos = 0U;
    decoder->wrapped = true;
    return true;
}

static bool lzma86_put(lzma86_decoder *decoder, uint8_t value)
{
    if (decoder->window_pos == decoder->window_size && !lzma86_window_advance(decoder)) {
        return false;
    }
    decoder->window[decoder->window_pos++] = value;
    ++decoder->produced;
    return true;
}

static uint8_t lzma86_peek(const lzma86_decoder *decoder, uint32_t distance)
{
    uint32_t index = decoder->window_pos >= distance ? decoder->window_pos - distance : decoder->window_pos + decoder->window_size - distance;
    return decoder->window[index];
}

static bool lzma86_distance_ok(const lzma86_decoder *decoder, uint32_t rep0)
{
    uint32_t held = decoder->wrapped ? decoder->window_size : decoder->window_pos;
    return rep0 < held;
}

/* ---------------------------------------------------------------- stream */

static void lzma86_release_stream(lzma86_decoder *decoder)
{
    if (decoder->window) xx_mem_free(decoder->window);
    if (decoder->probs) xx_mem_free(decoder->probs);
    decoder->window = NULL;
    decoder->probs = NULL;
}

static bool lzma86_prepare_stream(lzma86_decoder *decoder, const lzma86_header *header, int64_t data_offset, int64_t data_end)
{
    uint64_t limit;
    size_t index;
    lzma86_release_stream(decoder);
    decoder->in_offset = data_offset;
    decoder->in_end = data_end;
    decoder->in_pos = 0U;
    decoder->in_len = 0U;
    decoder->consumed = 0U;
    decoder->data_bits = 0U;
    decoder->in_failed = false;
    decoder->produced = 0U;
    decoder->next_pd_check = XX_LZMA86_PD_STEP;
    decoder->window_pos = 0U;
    decoder->flush_pos = 0U;
    decoder->wrapped = false;

    limit = header->dictionary_size < XX_LZMA86_DICT_MIN ? XX_LZMA86_DICT_MIN : header->dictionary_size;
    if (header->declared_size != XX_LZMA86_UNKNOWN_SIZE && header->declared_size < limit) {
        limit = header->declared_size == 0U ? 1U : header->declared_size;
    }
    if (limit > XX_LZMA86_WINDOW_MAX) limit = XX_LZMA86_WINDOW_MAX;
    decoder->window_limit = (uint32_t)limit;
    decoder->window_size = decoder->window_limit < XX_LZMA86_WINDOW_FIRST ? decoder->window_limit : XX_LZMA86_WINDOW_FIRST;
    decoder->probs_count = (size_t)XX_LZMA86_P_LITERAL + ((size_t)0x300U << (header->lc + header->lp));
    decoder->window = (uint8_t *)xx_mem_alloc(decoder->window_size);
    decoder->probs = (uint16_t *)xx_mem_alloc(decoder->probs_count * sizeof(uint16_t));
    if (!decoder->window || !decoder->probs) {
        lzma86_release_stream(decoder);
        decoder->alloc_failed = true;
        return false;
    }
    for (index = 0U; index < decoder->probs_count; ++index) {
        decoder->probs[index] = 1024U;
    }
    return true;
}

typedef struct lzma86_stream_info_s {
    uint64_t produced;
    uint64_t consumed;
    bool end_marker;
    bool zero_data;
} lzma86_stream_info;

/* Decode one stream whose data starts at data_offset, reading no further
 * than data_end.  probe_output > 0 stops early (LZMA86_PARTIAL) once that
 * much output has been produced without an error. */
static lzma86_result lzma86_decode_stream_data(lzma86_decoder *decoder, const lzma86_header *header, int64_t data_offset, int64_t data_end, uint64_t probe_output,
                                               lzma86_stream_info *info)
{
    uint32_t rep0 = 0U, rep1 = 0U, rep2 = 0U, rep3 = 0U;
    unsigned state = 0U;
    const uint32_t pb_mask = (UINT32_C(1) << header->pb) - 1U;
    const uint32_t lp_mask = (UINT32_C(1) << header->lp) - 1U;
    const bool known = header->declared_size != XX_LZMA86_UNKNOWN_SIZE;
    bool marker = false;
    uint16_t *probs;
    unsigned index;

    if (!lzma86_prepare_stream(decoder, header, data_offset, data_end)) {
        return LZMA86_FAILED;
    }
    probs = decoder->probs;
    if (lzma86_next_byte(decoder) != 0U) return LZMA86_FAILED;
    decoder->range = UINT32_C(0xFFFFFFFF);
    decoder->code = 0U;
    for (index = 0U; index < 4U; ++index) {
        decoder->code = (decoder->code << 8U) | lzma86_next_byte(decoder);
    }
    if (decoder->in_failed || decoder->code == decoder->range) {
        return LZMA86_FAILED;
    }

    for (;;) {
        uint32_t pos_state;
        uint32_t length;
        if (known && decoder->produced == header->declared_size) break;
        if (probe_output != 0U && decoder->produced >= probe_output) {
            return LZMA86_PARTIAL;
        }
        if (decoder->in_failed || decoder->output_failed || (decoder->data_bits == 0U && decoder->consumed > XX_LZMA86_ZERO_DATA_LIMIT)) {
            return LZMA86_FAILED;
        }
        if (decoder->produced >= decoder->next_pd_check) {
            decoder->next_pd_check = decoder->produced + XX_LZMA86_PD_STEP;
            if (decoder->pd && xx_pd_is_stopped(decoder->pd)) {
                return LZMA86_FAILED;
            }
        }
        pos_state = (uint32_t)decoder->produced & pb_mask;

        if (!lzma86_bit(decoder, probs + XX_LZMA86_P_IS_MATCH + state * XX_LZMA86_POS_STATES + pos_state)) {
            uint32_t previous = decoder->produced != 0U ? lzma86_peek(decoder, 1U) : 0U;
            uint32_t context = (((uint32_t)decoder->produced & lp_mask) << header->lc) + (previous >> (8U - header->lc));
            uint16_t *literal = probs + XX_LZMA86_P_LITERAL + (size_t)0x300U * context;
            uint32_t symbol = 1U;
            if (state >= 7U) {
                uint32_t match_byte;
                if (!lzma86_distance_ok(decoder, rep0)) return LZMA86_FAILED;
                match_byte = lzma86_peek(decoder, rep0 + 1U);
                do {
                    uint32_t match_bit = (match_byte >> 7U) & 1U;
                    unsigned bit;
                    match_byte <<= 1U;
                    bit = lzma86_bit(decoder, literal + ((1U + match_bit) << 8U) + symbol);
                    symbol = (symbol << 1U) | bit;
                    if (match_bit != bit) break;
                } while (symbol < 0x100U);
            }
            while (symbol < 0x100U) {
                symbol = (symbol << 1U) | lzma86_bit(decoder, literal + symbol);
            }
            if (!lzma86_put(decoder, (uint8_t)(symbol & 0xFFU))) {
                return LZMA86_FAILED;
            }
            state = state < 4U ? 0U : (state < 10U ? state - 3U : state - 6U);
            continue;
        }

        if (!lzma86_bit(decoder, probs + XX_LZMA86_P_IS_REP + state)) {
            uint32_t distance;
            length = lzma86_length(decoder, probs + XX_LZMA86_P_LEN, pos_state);
            state = state < 7U ? 7U : 10U;
            distance = lzma86_distance(decoder, length);
            if (distance == XX_LZMA86_MARKER_DISTANCE) {
                marker = true;
                break;
            }
            rep3 = rep2;
            rep2 = rep1;
            rep1 = rep0;
            rep0 = distance;
        } else {
            if (!lzma86_bit(decoder, probs + XX_LZMA86_P_IS_REP_G0 + state)) {
                if (!lzma86_bit(decoder, probs + XX_LZMA86_P_IS_REP0_LONG + state * XX_LZMA86_POS_STATES + pos_state)) {
                    if (!lzma86_distance_ok(decoder, rep0) || !lzma86_put(decoder, lzma86_peek(decoder, rep0 + 1U))) {
                        return LZMA86_FAILED;
                    }
                    state = state < 7U ? 9U : 11U;
                    continue;
                }
            } else {
                uint32_t distance;
                if (!lzma86_bit(decoder, probs + XX_LZMA86_P_IS_REP_G1 + state)) {
                    distance = rep1;
                } else {
                    if (!lzma86_bit(decoder, probs + XX_LZMA86_P_IS_REP_G2 + state)) {
                        distance = rep2;
                    } else {
                        distance = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = distance;
            }
            length = lzma86_length(decoder, probs + XX_LZMA86_P_REP_LEN, pos_state);
            state = state < 7U ? 8U : 11U;
        }

        length += 2U;
        if (decoder->in_failed || !lzma86_distance_ok(decoder, rep0) || (known && (uint64_t)length > header->declared_size - decoder->produced)) {
            return LZMA86_FAILED;
        }
        while (length-- > 0U) {
            if (!lzma86_put(decoder, lzma86_peek(decoder, rep0 + 1U))) {
                return LZMA86_FAILED;
            }
        }
    }

    if (decoder->in_failed || decoder->output_failed) return LZMA86_FAILED;
    if (!marker && decoder->code != 0U) {
        /* Known size reached with the coder not at rest: only an end
         * marker may follow. */
        uint32_t pos_state = (uint32_t)decoder->produced & pb_mask;
        uint32_t length;
        if (!lzma86_bit(decoder, probs + XX_LZMA86_P_IS_MATCH + state * XX_LZMA86_POS_STATES + pos_state) || lzma86_bit(decoder, probs + XX_LZMA86_P_IS_REP + state)) {
            return LZMA86_FAILED;
        }
        length = lzma86_length(decoder, probs + XX_LZMA86_P_LEN, pos_state);
        if (lzma86_distance(decoder, length) != XX_LZMA86_MARKER_DISTANCE) {
            return LZMA86_FAILED;
        }
        marker = true;
    }
    if (decoder->in_failed || decoder->code != 0U || (known && decoder->produced != header->declared_size) || !lzma86_flush(decoder, decoder->window_pos)) {
        return LZMA86_FAILED;
    }
    info->produced = decoder->produced;
    info->consumed = decoder->consumed;
    info->end_marker = marker;
    info->zero_data = decoder->data_bits == 0U;
    return LZMA86_FINISHED;
}

static lzma86_decoder *lzma86_decoder_create(xx_io_device *device, xx_pd_struct *pd)
{
    size_t capacity = xx_get_file_buffer_size();
    lzma86_decoder *decoder;
    if (capacity == 0U || capacity > (size_t)-1 - sizeof(*decoder)) {
        return NULL;
    }
    decoder = (lzma86_decoder *)xx_mem_calloc(1U, sizeof(*decoder) + capacity);
    if (!decoder) return NULL;
    decoder->io_capacity = capacity;
    decoder->in_buffer = (uint8_t *)(decoder + 1);
    decoder->device = device;
    decoder->pd = pd;
    return decoder;
}

static void lzma86_decoder_free(lzma86_decoder *decoder)
{
    if (!decoder) return;
    lzma86_release_stream(decoder);
    xx_mem_free(decoder);
}

typedef struct lzma86_walk_info_s {
    uint64_t output_size;
    int64_t end;
    uint64_t streams;
    bool first_filtered;
} lzma86_walk_info;

/*
 * Walk the chain of streams starting at `start`.
 *   stop_at < 0:  measuring.  The first stream must decode to a clean
 *                 finish; further streams are followed while they pass the
 *                 header test and decode cleanly; the walk ends before the
 *                 first thing that does not.
 *   stop_at >= 0: replaying a measured chain up to exactly stop_at, writing
 *                 the (filtered) output to `destination` when it is given.
 * probe_output > 0 (measuring only) accepts a first stream that is still
 * decoding cleanly after that much output, without finishing it.
 */
static bool lzma86_walk(xx_io_device *device, int64_t start, int64_t stop_at, xx_io_device *destination, uint64_t probe_output, xx_pd_struct *pd, lzma86_walk_info *info)
{
    lzma86_decoder *decoder;
    lzma86_bcj bcj;
    xx_io_device bcj_device;
    int64_t total_size;
    int64_t position = start;
    int64_t limit;
    uint64_t output_size = 0U;
    uint64_t streams = 0U;
    uint64_t model_work = 0U;
    bool first_filtered = false;
    bool ok = false;

    if (!device || !info || start < 0) return false;
    total_size = xx_io_total_size(device);
    limit = stop_at >= 0 ? stop_at : total_size;
    if (total_size < 0 || limit > total_size || limit < start || limit - start < (int64_t)XX_LZMA86_MIN_STREAM) {
        return false;
    }
    decoder = lzma86_decoder_create(device, pd);
    if (!decoder) return false;
    xx_rt_memset(&bcj, 0, sizeof(bcj));
    xx_rt_memset(&bcj_device, 0, sizeof(bcj_device));
    bcj.output = destination;
    bcj_device.write = lzma86_bcj_write;
    bcj_device.priv = &bcj;

    while (streams < XX_LZMA86_MAX_STREAMS) {
        uint8_t head[XX_LZMA86_MIN_STREAM];
        lzma86_header header;
        lzma86_result result;
        lzma86_stream_info stream;
        int64_t next;
        bool first = streams == 0U;
        bool filtered;

        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (stop_at >= 0 && position == stop_at) break;
        if (limit - position < (int64_t)XX_LZMA86_MIN_STREAM || !lzma86_read_exact_at(device, position, head, sizeof(head)) ||
            !lzma86_stream_start_ok(head, &header, limit - position) || (!first && stop_at < 0 && header.declared_size == 0U) ||
            (first && stop_at < 0 && header.declared_size == 0U && head[1] == 0U && limit - position - (int64_t)XX_LZMA86_HEADER > XX_LZMA86_EMPTY_PROPS0_SLACK)) {
            if (first || stop_at >= 0) goto done;
            break;
        }
        {
            uint64_t model = (uint64_t)XX_LZMA86_P_LITERAL + ((uint64_t)0x300U << (header.lc + header.lp));
            if (!first && stop_at < 0 && model > XX_LZMA86_MODEL_BUDGET - model_work) {
                break;
            }
            model_work += model;
        }
        filtered = header.filter == 1U;
        if (first) first_filtered = filtered;
        if (destination && filtered) {
            if (!bcj.buffer) {
                bcj.buffer = (uint8_t *)xx_mem_alloc(XX_LZMA86_BCJ_CHUNK);
                if (!bcj.buffer) goto done;
            }
            lzma86_bcj_reset(&bcj);
            decoder->destination = &bcj_device;
        } else {
            decoder->destination = destination;
        }
        result = lzma86_decode_stream_data(decoder, &header, position + (int64_t)XX_LZMA86_HEADER, limit, first && stop_at < 0 ? probe_output : 0U, &stream);
        if (result == LZMA86_PARTIAL) {
            info->output_size = decoder->produced;
            info->end = -1;
            info->streams = 1U;
            info->first_filtered = first_filtered;
            ok = true;
            goto done;
        }
        if (result != LZMA86_FINISHED || stream.consumed > (uint64_t)(limit - position) - (uint64_t)XX_LZMA86_HEADER || stream.produced > UINT64_MAX - output_size) {
            if (first || stop_at >= 0 || decoder->alloc_failed || (pd && xx_pd_is_stopped(pd))) {
                goto done;
            }
            break;
        }
        if (destination && filtered && !lzma86_bcj_finish(&bcj)) goto done;
        next = position + (int64_t)XX_LZMA86_HEADER + (int64_t)stream.consumed;
        /* All-zero data ends cleanly at any declared size: believe it only
         * for a whole file of at most two output bytes. */
        if (stop_at < 0 && !stream.end_marker && stream.zero_data && (!first || stream.produced > XX_LZMA86_ZERO_DATA_OUTPUT || next != total_size)) {
            if (first) goto done;
            break;
        }
        position = next;
        output_size += stream.produced;
        ++streams;
        if (probe_output != 0U) break;
    }
    if (streams == 0U || (stop_at >= 0 && position != stop_at) || (pd && xx_pd_is_stopped(pd))) {
        goto done;
    }
    info->output_size = output_size;
    info->end = position;
    info->streams = streams;
    info->first_filtered = first_filtered;
    ok = true;
done:
    if (bcj.buffer) xx_mem_free(bcj.buffer);
    lzma86_decoder_free(decoder);
    return ok;
}

/* ------------------------------------------------------------ the reader */

static bool lzma86_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *lzma86_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool lzma86_populate_record(Abstractformat *self, xx_archive_record *record)
{
    const xx_lzma86 *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid || self->format_size < (int64_t)XX_LZMA86_MIN_STREAM) {
        return false;
    }
    archive = (const xx_lzma86 *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)XX_LZMA86_HEADER;
    record->data_offset = self->base_address + (int64_t)XX_LZMA86_HEADER;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME, XX_LZMA86_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static void xx_lzma86_vtable_destroy(Abstractformat *self);

void xx_lzma86_init(xx_lzma86 *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LZMA86_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzma");
    xx_format_set_extension(&archive->format, "lzma86");
    archive->format.check_is_valid = xx_lzma86_check_is_valid;
    archive->format.handle_base_info = xx_lzma86_handle_base_info;
    archive->format.get_format_size = xx_lzma86_get_format_size;
    archive->format.get_number_of_archive_records = xx_lzma86_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_lzma86_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_lzma86_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_lzma86_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_lzma86_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_lzma86_free_archive_records_reading;
    archive->format.destroy = xx_lzma86_vtable_destroy;
    archive->stream_end = -1;
}

xx_lzma86 *xx_lzma86_create(xx_io_device *device, int64_t base_address)
{
    xx_lzma86 *archive = (xx_lzma86 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lzma86_init(archive, device, base_address);
    return archive;
}

void xx_lzma86_destroy(xx_lzma86 *archive)
{
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
    archive->stream_count = 0U;
}

static void xx_lzma86_vtable_destroy(Abstractformat *self)
{
    xx_lzma86_destroy((xx_lzma86 *)self);
}

void xx_lzma86_free(xx_lzma86 *archive)
{
    if (!archive) return;
    xx_lzma86_destroy(archive);
    xx_mem_free(archive);
}

/* Header tests plus a trial decode of the first XX_LZMA86_PROBE_OUTPUT
 * bytes (or the whole first stream, if it is shorter).  The header is
 * checked from one 19-byte read before anything is allocated, so a file
 * that is not lzma86 costs one small read. */
bool xx_lzma86_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t head[XX_LZMA86_MIN_STREAM];
    lzma86_header header;
    lzma86_walk_info info;
    int64_t total_size;
    if (!self || !self->device || self->base_address < 0) return false;
    if (self->base_info_handled) return self->is_valid;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < (int64_t)XX_LZMA86_MIN_STREAM ||
        !lzma86_read_exact_at(self->device, self->base_address, head, sizeof(head)) || !lzma86_stream_start_ok(head, &header, total_size - self->base_address)) {
        return false;
    }
    return lzma86_walk(self->device, self->base_address, -1, NULL, XX_LZMA86_PROBE_OUTPUT, pd, &info);
}

bool xx_lzma86_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    lzma86_walk_info info;
    int64_t total_size;
    xx_lzma86 *archive = (xx_lzma86 *)self;
    if (!self) return false;
    if (!self->device || self->base_address < 0 || !lzma86_walk(self->device, self->base_address, -1, NULL, 0U, pd, &info) || info.end <= self->base_address) {
        archive->uncompressed_size = 0U;
        archive->stream_end = -1;
        archive->stream_count = 0U;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    total_size = xx_io_total_size(self->device);
    archive->uncompressed_size = info.output_size;
    archive->stream_end = info.end;
    archive->stream_count = info.streams;
    archive->filtered = info.first_filtered;
    self->format_size = info.end - self->base_address;
    self->number_of_archive_records = 1U;
    self->overlay_offset = info.end < total_size ? info.end : -1;
    self->overlay_size = info.end < total_size ? total_size - info.end : 0;
    self->file_type = XX_LZMA86_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_lzma86_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_lzma86_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

bool xx_lzma86_unpack_to_device(xx_lzma86 *archive, xx_io_device *destination, xx_pd_struct *pd)
{
    lzma86_walk_info info;
    if (!archive || !destination || (!archive->format.base_info_handled && !xx_format_handle_base_info(&archive->format, pd)) || !archive->format.is_valid ||
        archive->stream_end < 0) {
        return false;
    }
    return lzma86_walk(archive->format.device, archive->format.base_address, archive->stream_end, destination, 0U, pd, &info) && info.end == archive->stream_end &&
           info.output_size == archive->uncompressed_size;
}

xx_archive_record_state *xx_lzma86_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) || !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!lzma86_copy_options(&state->options, options) || !lzma86_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_lzma86_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_lzma86_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_lzma86_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_lzma86 *archive = (xx_lzma86 *)self;
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_value = lzma86_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        lzma86_walk_info info;
        return archive->stream_end >= 0 && lzma86_walk(self->device, self->base_address, archive->stream_end, NULL, 0U, pd, &info) &&
               info.output_size == archive->uncompressed_size;
    }
    if (path_value->type == XX_VAR_TYPE_STRING || path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING || path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) {
        if (owned_path) xx_str_free(owned_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' && base_path[xx_str_len(base_path) - 1U] != '\\') {
        destination_path = xx_str_concat3(base_path, "/", XX_LZMA86_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_LZMA86_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_lzma86_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_lzma86_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
