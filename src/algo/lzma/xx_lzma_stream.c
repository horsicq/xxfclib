/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Strict, bounded raw LZMA stream decoding. The codec reports the exact
 * consumed extent and accepts known-size streams without an end marker.
 * Its dictionary grows with decoded data up to the memory limit; invalid
 * distances and all-zero streams with implausible output are rejected.
 * Container headers, concatenation policy and archive metadata belong to
 * formats/lzma. This implementation follows the public-domain LZMA format
 * description (LZMA SDK lzma-specification.txt).
 */

#include "xx_lzma_stream_internal.h"
#include "platforms/xx_lzma_platform.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"

#define XX_LZMA_DICT_MIN 4096U
/* Largest dictionary window ever allocated, whatever the header claims. */
#define XX_LZMA_WINDOW_MAX ((uint32_t)XX_LZMA_MAX_DICT_SIZE)
#define XX_LZMA_WINDOW_FIRST ((uint32_t)64U * 1024U)
#define XX_LZMA_PD_STEP ((uint64_t)1U << 20)

/* Probability model layout (one uint16_t array per stream). */
#define XX_LZMA_STATES 12U
#define XX_LZMA_POS_STATES 16U
#define XX_LZMA_LEN_CODER (2U + 2U * XX_LZMA_POS_STATES * 8U + 256U)
#define XX_LZMA_P_IS_MATCH 0U
#define XX_LZMA_P_IS_REP (XX_LZMA_P_IS_MATCH + XX_LZMA_STATES * XX_LZMA_POS_STATES)
#define XX_LZMA_P_IS_REP_G0 (XX_LZMA_P_IS_REP + XX_LZMA_STATES)
#define XX_LZMA_P_IS_REP_G1 (XX_LZMA_P_IS_REP_G0 + XX_LZMA_STATES)
#define XX_LZMA_P_IS_REP_G2 (XX_LZMA_P_IS_REP_G1 + XX_LZMA_STATES)
#define XX_LZMA_P_IS_REP0_LONG (XX_LZMA_P_IS_REP_G2 + XX_LZMA_STATES)
#define XX_LZMA_P_POS_SLOT (XX_LZMA_P_IS_REP0_LONG + XX_LZMA_STATES * XX_LZMA_POS_STATES)
#define XX_LZMA_P_SPEC_POS (XX_LZMA_P_POS_SLOT + 4U * 64U)
#define XX_LZMA_P_ALIGN (XX_LZMA_P_SPEC_POS + 115U)
#define XX_LZMA_P_LEN (XX_LZMA_P_ALIGN + 16U)
#define XX_LZMA_P_REP_LEN (XX_LZMA_P_LEN + XX_LZMA_LEN_CODER)
#define XX_LZMA_P_LITERAL (XX_LZMA_P_REP_LEN + XX_LZMA_LEN_CODER)

#define XX_LZMA_MARKER_DISTANCE UINT32_C(0xFFFFFFFF)

struct xx_lzma_stream_decoder_s {
    /* Input: the device range [in_offset, in_end) not yet buffered. */
    xx_io_device *device;
    int64_t in_offset;
    int64_t in_end;
    size_t in_pos;
    size_t in_len;
    uint64_t consumed;
    uint8_t data_bits; /* OR of every data byte after the first */
    bool in_failed;
    uint32_t range;
    uint32_t code;
    /* Model. */
    uint16_t *probs;
    size_t probs_count;
    /* Dictionary window: grows up to window_limit, then wraps. */
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
    bool alloc_failed; /* out of memory: not evidence about the data */
    xx_pd_struct *pd;
    uint8_t *in_buffer;
    size_t io_capacity;
};

bool xx_lzma_stream_read_exact_at(xx_io_device *device, int64_t offset, void *data, size_t size, xx_pd_struct *pd)
{
    size_t done = 0U;
    const size_t io_capacity = xx_get_file_buffer_size();
    if (!device || (!data && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        size_t request = size - done;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (request > io_capacity) request = io_capacity;
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

size_t xx_lzma_stream_model_entries(const xx_lzma_stream_props *properties)
{
    if (!properties || properties->lc > 8U || properties->lp > 4U || properties->pb > 4U) return 0U;
    return (size_t)XX_LZMA_P_LITERAL + ((size_t)0x300U << (properties->lc + properties->lp));
}

/* ---------------------------------------------------------------- input */

static bool xx_lzma_refill(xx_lzma_stream_decoder *decoder)
{
    int64_t available = decoder->in_end - decoder->in_offset;
    size_t request;
    if (available <= 0) return false;
    request = (uint64_t)available < (uint64_t)decoder->io_capacity ? (size_t)available : decoder->io_capacity;
    if (!xx_lzma_stream_read_exact_at(decoder->device, decoder->in_offset, decoder->in_buffer, request, decoder->pd)) {
        return false;
    }
    decoder->in_offset += (int64_t)request;
    decoder->in_pos = 0U;
    decoder->in_len = request;
    return true;
}

static XX_LZMA_INLINE uint8_t xx_lzma_next_byte(xx_lzma_stream_decoder *decoder)
{
    uint8_t value;
    if (decoder->in_pos == decoder->in_len && !xx_lzma_refill(decoder)) {
        decoder->in_failed = true;
        return 0U;
    }
    value = decoder->in_buffer[decoder->in_pos++];
    if (decoder->consumed++ != 0U) decoder->data_bits |= value;
    return value;
}

/* --------------------------------------------------------- range decoder */

static XX_LZMA_INLINE void xx_lzma_normalize(xx_lzma_stream_decoder *decoder)
{
    if (decoder->range < (UINT32_C(1) << 24U)) {
        decoder->range <<= 8U;
        decoder->code = (decoder->code << 8U) | xx_lzma_next_byte(decoder);
    }
}

static XX_LZMA_INLINE unsigned xx_lzma_bit(xx_lzma_stream_decoder *decoder, uint16_t *prob)
{
    const uint32_t range = decoder->range, code = decoder->code, value = *prob;
    const uint32_t bound = (range >> 11U) * value;
    const unsigned bit = code >= bound;
    const uint32_t mask = 0U - bit;
    /* Incompressible literals make this decision unpredictable. Select the
     * interval and probability arithmetically, without a per-bit branch. */
    decoder->range = (bound & ~mask) | ((range - bound) & mask);
    decoder->code = code - (bound & mask);
    *prob = (uint16_t)(value + (((2048U - value) >> 5U) & ~mask) - ((value >> 5U) & mask));
    xx_lzma_normalize(decoder);
    return bit;
}

static uint32_t xx_lzma_direct_bits(xx_lzma_stream_decoder *decoder, unsigned count)
{
    uint32_t result = 0U;
    while (count-- > 0U) {
        decoder->range >>= 1U;
        result <<= 1U;
        if (decoder->code >= decoder->range) {
            decoder->code -= decoder->range;
            result |= 1U;
        }
        xx_lzma_normalize(decoder);
    }
    return result;
}

static uint32_t xx_lzma_tree(xx_lzma_stream_decoder *decoder, uint16_t *probs, unsigned bits)
{
    uint32_t node = 1U;
    unsigned index;
    for (index = 0U; index < bits; ++index) {
        node = (node << 1U) | xx_lzma_bit(decoder, probs + node);
    }
    return node - (UINT32_C(1) << bits);
}

static uint32_t xx_lzma_tree_reverse(xx_lzma_stream_decoder *decoder, uint16_t *probs, unsigned bits)
{
    uint32_t node = 1U;
    uint32_t result = 0U;
    unsigned index;
    for (index = 0U; index < bits; ++index) {
        unsigned bit = xx_lzma_bit(decoder, probs + node);
        node = (node << 1U) | bit;
        result |= (uint32_t)bit << index;
    }
    return result;
}

static uint32_t xx_lzma_length(xx_lzma_stream_decoder *decoder, uint16_t *coder, unsigned pos_state)
{
    if (!xx_lzma_bit(decoder, coder)) {
        return xx_lzma_tree(decoder, coder + 2U + pos_state * 8U, 3U);
    }
    if (!xx_lzma_bit(decoder, coder + 1U)) {
        return 8U + xx_lzma_tree(decoder, coder + 2U + XX_LZMA_POS_STATES * 8U + pos_state * 8U, 3U);
    }
    return 16U + xx_lzma_tree(decoder, coder + 2U + 2U * XX_LZMA_POS_STATES * 8U, 8U);
}

/* length is the coded length (actual length minus two). */
static uint32_t xx_lzma_distance(xx_lzma_stream_decoder *decoder, uint32_t length)
{
    uint32_t len_state = length < 4U ? length : 3U;
    uint32_t slot = xx_lzma_tree(decoder, decoder->probs + XX_LZMA_P_POS_SLOT + len_state * 64U, 6U);
    unsigned direct;
    uint32_t distance;
    if (slot < 4U) return slot;
    direct = (unsigned)(slot >> 1U) - 1U;
    distance = (2U | (slot & 1U)) << direct;
    if (slot < 14U) {
        return distance + xx_lzma_tree_reverse(decoder, decoder->probs + XX_LZMA_P_SPEC_POS + distance - slot, direct);
    }
    distance += xx_lzma_direct_bits(decoder, direct - 4U) << 4U;
    return distance + xx_lzma_tree_reverse(decoder, decoder->probs + XX_LZMA_P_ALIGN, 4U);
}

/* ---------------------------------------------------------------- window */

static bool xx_lzma_flush(xx_lzma_stream_decoder *decoder, uint32_t end)
{
    uint32_t at = decoder->flush_pos;
    while (decoder->destination && at < end) {
        if (decoder->pd && xx_pd_is_stopped(decoder->pd)) return false;
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

/* Called when the window is full: flush it, then grow it (while it is
 * below its limit, it holds everything decoded so far, so growing keeps
 * every byte at its index) or start overwriting it from the beginning. */
static bool xx_lzma_window_advance(xx_lzma_stream_decoder *decoder)
{
    if (!xx_lzma_flush(decoder, decoder->window_size)) return false;
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

static XX_LZMA_INLINE bool xx_lzma_put(xx_lzma_stream_decoder *decoder, uint8_t value)
{
    if (decoder->window_pos == decoder->window_size && !xx_lzma_window_advance(decoder)) {
        return false;
    }
    decoder->window[decoder->window_pos++] = value;
    ++decoder->produced;
    return true;
}

/* distance is 1-based; the caller has checked it against what is held. */
static uint8_t xx_lzma_peek(const xx_lzma_stream_decoder *decoder, uint32_t distance)
{
    uint32_t index = decoder->window_pos >= distance ? decoder->window_pos - distance : decoder->window_pos + decoder->window_size - distance;
    return decoder->window[index];
}

static bool xx_lzma_distance_ok(const xx_lzma_stream_decoder *decoder, uint32_t rep0)
{
    uint32_t held = decoder->wrapped ? decoder->window_size : decoder->window_pos;
    return rep0 < held;
}

/* ---------------------------------------------------------------- stream */

static void xx_lzma_release_stream(xx_lzma_stream_decoder *decoder)
{
    if (decoder->window) xx_mem_free(decoder->window);
    if (decoder->probs) xx_mem_free(decoder->probs);
    decoder->window = NULL;
    decoder->probs = NULL;
}

static bool xx_lzma_prepare_stream(xx_lzma_stream_decoder *decoder, const xx_lzma_stream_props *header, int64_t data_offset, int64_t data_end)
{
    uint64_t limit;
    xx_lzma_release_stream(decoder);
    decoder->in_offset = data_offset;
    decoder->in_end = data_end;
    decoder->in_pos = 0U;
    decoder->in_len = 0U;
    decoder->consumed = 0U;
    decoder->data_bits = 0U;
    decoder->in_failed = false;
    decoder->output_failed = false;
    decoder->alloc_failed = false;
    decoder->produced = 0U;
    decoder->next_pd_check = XX_LZMA_PD_STEP;
    decoder->window_pos = 0U;
    decoder->flush_pos = 0U;
    decoder->wrapped = false;

    limit = header->dictionary_size < XX_LZMA_DICT_MIN ? XX_LZMA_DICT_MIN : header->dictionary_size;
    if (header->declared_size != XX_LZMA_STREAM_UNKNOWN_SIZE && header->declared_size < limit) {
        limit = header->declared_size == 0U ? 1U : header->declared_size;
    }
    if (limit > XX_LZMA_WINDOW_MAX) limit = XX_LZMA_WINDOW_MAX;
    decoder->window_limit = (uint32_t)limit;
    decoder->window_size = decoder->window_limit < XX_LZMA_WINDOW_FIRST ? decoder->window_limit : XX_LZMA_WINDOW_FIRST;
    decoder->probs_count = xx_lzma_stream_model_entries(header);
    decoder->window = (uint8_t *)xx_mem_alloc(decoder->window_size);
    decoder->probs = (uint16_t *)xx_mem_alloc(decoder->probs_count * sizeof(uint16_t));
    if (!decoder->window || !decoder->probs) {
        xx_lzma_release_stream(decoder);
        decoder->alloc_failed = true;
        return false;
    }
    xx_lzma_platform_select()->fill_probs(decoder->probs, decoder->probs_count);
    return true;
}

/*
 * Decode one stream whose data starts at data_offset, reading no further
 * than data_end.  probe_output > 0 stops early (XX_LZMA_STREAM_PARTIAL) once that
 * much output has been produced without an error.  On XX_LZMA_STREAM_FINISHED,
 * *info describes the stream.
 *
 * Data made of nothing but zero bytes keeps the range coder's code at zero,
 * so it "decodes" as a run of zero literals and ends cleanly at any declared
 * size.  No encoder writes that for more than two output bytes (from the
 * third byte on, the LZMA SDK codes a zero run as a repeat, which sets
 * bits), so a stream still all zeros after XX_LZMA_ZERO_DATA_LIMIT data
 * bytes is given up early instead of being decoded to its claimed size.
 */
#define XX_LZMA_ZERO_DATA_LIMIT 16U

static xx_lzma_stream_result xx_lzma_decode_stream_data(xx_lzma_stream_decoder *decoder, const xx_lzma_stream_props *header, int64_t data_offset, int64_t data_end,
                                                        uint64_t probe_output, xx_lzma_stream_info *info)
{
    uint32_t rep0 = 0U, rep1 = 0U, rep2 = 0U, rep3 = 0U;
    unsigned state = 0U;
    const uint32_t pb_mask = (UINT32_C(1) << header->pb) - 1U;
    const uint32_t lp_mask = (UINT32_C(1) << header->lp) - 1U;
    const bool known = header->declared_size != XX_LZMA_STREAM_UNKNOWN_SIZE;
    bool marker = false;
    uint16_t *probs;
    unsigned index;

    if (!xx_lzma_prepare_stream(decoder, header, data_offset, data_end)) {
        return XX_LZMA_STREAM_FAILED;
    }
    probs = decoder->probs;
    if (xx_lzma_next_byte(decoder) != 0U) return XX_LZMA_STREAM_FAILED;
    decoder->range = UINT32_C(0xFFFFFFFF);
    decoder->code = 0U;
    for (index = 0U; index < 4U; ++index) {
        decoder->code = (decoder->code << 8U) | xx_lzma_next_byte(decoder);
    }
    if (decoder->in_failed || decoder->code == decoder->range) {
        return XX_LZMA_STREAM_FAILED;
    }

    for (;;) {
        uint32_t pos_state;
        uint32_t length;
        if (known && decoder->produced == header->declared_size) break;
        if (probe_output != 0U && decoder->produced >= probe_output) {
            return XX_LZMA_STREAM_PARTIAL;
        }
        if (decoder->in_failed || decoder->output_failed || (decoder->data_bits == 0U && decoder->consumed > XX_LZMA_ZERO_DATA_LIMIT)) {
            return XX_LZMA_STREAM_FAILED;
        }
        if (decoder->produced >= decoder->next_pd_check) {
            decoder->next_pd_check = decoder->produced + XX_LZMA_PD_STEP;
            if (decoder->pd && xx_pd_is_stopped(decoder->pd)) {
                return XX_LZMA_STREAM_FAILED;
            }
        }
        pos_state = (uint32_t)decoder->produced & pb_mask;

        if (!xx_lzma_bit(decoder, probs + XX_LZMA_P_IS_MATCH + state * XX_LZMA_POS_STATES + pos_state)) {
            uint32_t previous = decoder->produced != 0U ? xx_lzma_peek(decoder, 1U) : 0U;
            uint32_t context = (((uint32_t)decoder->produced & lp_mask) << header->lc) + (previous >> (8U - header->lc));
            uint16_t *literal = probs + XX_LZMA_P_LITERAL + (size_t)0x300U * context;
            uint32_t symbol = 1U;
            if (state >= 7U) {
                uint32_t match_byte;
                if (!xx_lzma_distance_ok(decoder, rep0)) {
                    return XX_LZMA_STREAM_FAILED;
                }
                match_byte = xx_lzma_peek(decoder, rep0 + 1U);
                do {
                    uint32_t match_bit = (match_byte >> 7U) & 1U;
                    unsigned bit;
                    match_byte <<= 1U;
                    bit = xx_lzma_bit(decoder, literal + ((1U + match_bit) << 8U) + symbol);
                    symbol = (symbol << 1U) | bit;
                    if (match_bit != bit) break;
                } while (symbol < 0x100U);
            }
            while (symbol < 0x100U) {
                symbol = (symbol << 1U) | xx_lzma_bit(decoder, literal + symbol);
            }
            if (!xx_lzma_put(decoder, (uint8_t)(symbol & 0xFFU))) {
                return XX_LZMA_STREAM_FAILED;
            }
            state = state < 4U ? 0U : (state < 10U ? state - 3U : state - 6U);
            continue;
        }

        if (!xx_lzma_bit(decoder, probs + XX_LZMA_P_IS_REP + state)) {
            uint32_t distance;
            length = xx_lzma_length(decoder, probs + XX_LZMA_P_LEN, pos_state);
            state = state < 7U ? 7U : 10U;
            distance = xx_lzma_distance(decoder, length);
            if (distance == XX_LZMA_MARKER_DISTANCE) {
                marker = true;
                break;
            }
            rep3 = rep2;
            rep2 = rep1;
            rep1 = rep0;
            rep0 = distance;
        } else {
            if (!xx_lzma_bit(decoder, probs + XX_LZMA_P_IS_REP_G0 + state)) {
                if (!xx_lzma_bit(decoder, probs + XX_LZMA_P_IS_REP0_LONG + state * XX_LZMA_POS_STATES + pos_state)) {
                    if (!xx_lzma_distance_ok(decoder, rep0) || !xx_lzma_put(decoder, xx_lzma_peek(decoder, rep0 + 1U))) {
                        return XX_LZMA_STREAM_FAILED;
                    }
                    state = state < 7U ? 9U : 11U;
                    continue;
                }
            } else {
                uint32_t distance;
                if (!xx_lzma_bit(decoder, probs + XX_LZMA_P_IS_REP_G1 + state)) {
                    distance = rep1;
                } else {
                    if (!xx_lzma_bit(decoder, probs + XX_LZMA_P_IS_REP_G2 + state)) {
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
            length = xx_lzma_length(decoder, probs + XX_LZMA_P_REP_LEN, pos_state);
            state = state < 7U ? 8U : 11U;
        }

        length += 2U;
        if (decoder->in_failed || !xx_lzma_distance_ok(decoder, rep0) || (known && (uint64_t)length > header->declared_size - decoder->produced)) {
            return XX_LZMA_STREAM_FAILED;
        }
        while (length-- > 0U) {
            if (!xx_lzma_put(decoder, xx_lzma_peek(decoder, rep0 + 1U))) {
                return XX_LZMA_STREAM_FAILED;
            }
        }
    }

    if (decoder->in_failed || decoder->output_failed) return XX_LZMA_STREAM_FAILED;
    if (!marker && decoder->code != 0U) {
        /* Known size reached with the coder not at rest: only an end
         * marker may follow. */
        uint32_t pos_state = (uint32_t)decoder->produced & pb_mask;
        uint32_t length;
        if (!xx_lzma_bit(decoder, probs + XX_LZMA_P_IS_MATCH + state * XX_LZMA_POS_STATES + pos_state) || xx_lzma_bit(decoder, probs + XX_LZMA_P_IS_REP + state)) {
            return XX_LZMA_STREAM_FAILED;
        }
        length = xx_lzma_length(decoder, probs + XX_LZMA_P_LEN, pos_state);
        if (xx_lzma_distance(decoder, length) != XX_LZMA_MARKER_DISTANCE) {
            return XX_LZMA_STREAM_FAILED;
        }
        marker = true;
    }
    if (decoder->in_failed || decoder->code != 0U || (known && decoder->produced != header->declared_size) || !xx_lzma_flush(decoder, decoder->window_pos)) {
        return XX_LZMA_STREAM_FAILED;
    }
    info->produced = decoder->produced;
    info->consumed = decoder->consumed;
    info->end_marker = marker;
    info->zero_data = decoder->data_bits == 0U;
    return XX_LZMA_STREAM_FINISHED;
}

xx_lzma_stream_decoder *xx_lzma_stream_decoder_create(xx_io_device *device, xx_io_device *destination, xx_pd_struct *pd)
{
    const size_t io_capacity = xx_get_file_buffer_size();
    xx_lzma_stream_decoder *decoder;
    if (io_capacity > (size_t)-1 - sizeof(*decoder)) return NULL;
    decoder = (xx_lzma_stream_decoder *)xx_mem_calloc(1U, sizeof(*decoder) + io_capacity);
    if (!decoder) return NULL;
    decoder->in_buffer = (uint8_t *)(decoder + 1);
    decoder->io_capacity = io_capacity;
    decoder->device = device;
    decoder->destination = destination;
    decoder->pd = pd;
    return decoder;
}

void xx_lzma_stream_decoder_free(xx_lzma_stream_decoder *decoder)
{
    if (!decoder) return;
    xx_lzma_release_stream(decoder);
    xx_mem_free(decoder);
}

xx_lzma_stream_result xx_lzma_stream_decode(xx_lzma_stream_decoder *decoder, const xx_lzma_stream_props *properties, int64_t data_offset, int64_t data_end,
                                            uint64_t probe_output, xx_lzma_stream_info *info)
{
    xx_lzma_stream_result result;
    if (!info) return XX_LZMA_STREAM_FAILED;
    xx_mem_zero(info, sizeof(*info));
    if (!decoder || data_offset < 0 || data_end < data_offset || data_end - data_offset < 5 || xx_lzma_stream_model_entries(properties) == 0U ||
        (decoder->pd && xx_pd_is_stopped(decoder->pd))) {
        return XX_LZMA_STREAM_FAILED;
    }
    result = xx_lzma_decode_stream_data(decoder, properties, data_offset, data_end, probe_output, info);
    info->produced = decoder->produced;
    info->consumed = decoder->consumed;
    info->allocation_failed = decoder->alloc_failed;
    return result;
}
