/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzx/xx_lzx.h"
#include "xxfclib/memory/xx_memory.h"
#include <string.h>

#define LZX_FRAME_SIZE 32768U
#define LZX_PRETREE_SYMBOLS 20U
#define LZX_LENGTH_SYMBOLS 249U
#define LZX_ALIGNED_SYMBOLS 8U
#define LZX_MAX_MAIN_SYMBOLS 656U
#define LZX_MAX_CODE_LENGTH 16U
#define LZX_BLOCK_VERBATIM 1U
#define LZX_BLOCK_ALIGNED 2U
#define LZX_BLOCK_UNCOMPRESSED 3U

typedef struct lzx_huff_s {
    uint16_t count[LZX_MAX_CODE_LENGTH + 1U];
    uint16_t symbols[LZX_MAX_MAIN_SYMBOLS];
    bool empty;
} lzx_huff;

typedef struct lzx_state_s {
    const uint8_t *input;
    size_t input_size, input_pos;
    uint64_t bits;
    unsigned bit_count;
    bool error;
    uint8_t *window;
    uint32_t window_size, window_pos;
    uint32_t r0, r1, r2;
    unsigned main_symbols, position_slots;
    uint8_t main_lengths[LZX_MAX_MAIN_SYMBOLS];
    uint8_t length_lengths[LZX_LENGTH_SYMBOLS];
    lzx_huff main_tree, length_tree, aligned_tree, pre_tree;
    uint32_t position_base[51];
    uint8_t position_extra[51];
} lzx_state;

static void lzx_bits_init(lzx_state *s, const uint8_t *input, size_t input_size) {
    s->input = input; s->input_size = input_size; s->input_pos = 0;
    s->bits = 0; s->bit_count = 0; s->error = false;
}

static void lzx_ensure(lzx_state *s, unsigned count) {
    while (s->bit_count < count) {
        uint32_t word;
        if (s->input_pos + 1U >= s->input_size) { s->error = true; return; }
        word = (uint32_t)s->input[s->input_pos] | ((uint32_t)s->input[s->input_pos + 1U] << 8U);
        s->input_pos += 2U;
        s->bits |= (uint64_t)word << (48U - s->bit_count);
        s->bit_count += 16U;
    }
}

static uint32_t lzx_read(lzx_state *s, unsigned count) {
    uint32_t result;
    if (!count) return 0;
    lzx_ensure(s, count);
    if (s->error || s->bit_count < count) return 0;
    result = (uint32_t)(s->bits >> (64U - count));
    s->bits <<= count; s->bit_count -= count;
    return result;
}

static bool lzx_build_tree(lzx_huff *tree, const uint8_t *lengths,
                           unsigned symbol_count, bool permit_empty) {
    int left = 1;
    unsigned offsets[LZX_MAX_CODE_LENGTH + 1U], i, len;
    xx_rt_memset(tree->count, 0, sizeof(tree->count));
    for (i = 0; i < symbol_count; ++i) {
        if (lengths[i] > LZX_MAX_CODE_LENGTH) return false;
        ++tree->count[lengths[i]];
    }
    tree->empty = tree->count[0] == symbol_count;
    if (tree->empty) return permit_empty;
    for (len = 1; len <= LZX_MAX_CODE_LENGTH; ++len) {
        left = (left << 1) - tree->count[len];
        if (left < 0) return false;
    }
    if (left) return false;
    offsets[1] = 0;
    for (len = 1; len < LZX_MAX_CODE_LENGTH; ++len)
        offsets[len + 1U] = offsets[len] + tree->count[len];
    for (i = 0; i < symbol_count; ++i)
        if (lengths[i]) tree->symbols[offsets[lengths[i]]++] = (uint16_t)i;
    return true;
}

static int lzx_decode_symbol(lzx_state *s, const lzx_huff *tree) {
    int code = 0, first = 0;
    unsigned index = 0, len;
    if (tree->empty) { s->error = true; return -1; }
    for (len = 1; len <= LZX_MAX_CODE_LENGTH; ++len) {
        int delta;
        code |= (int)lzx_read(s, 1);
        if (s->error) return -1;
        delta = code - first;
        if (delta >= 0 && delta < tree->count[len]) return tree->symbols[index + (unsigned)delta];
        index += tree->count[len];
        first = (first + tree->count[len]) << 1;
        code <<= 1;
    }
    s->error = true;
    return -1;
}

static bool lzx_read_lengths(lzx_state *s, uint8_t *lengths, unsigned first, unsigned last) {
    uint8_t pre_lengths[LZX_PRETREE_SYMBOLS];
    unsigned i;
    for (i = 0; i < LZX_PRETREE_SYMBOLS; ++i) pre_lengths[i] = (uint8_t)lzx_read(s, 4);
    if (s->error || !lzx_build_tree(&s->pre_tree, pre_lengths, LZX_PRETREE_SYMBOLS, false)) return false;
    i = first;
    while (i < last) {
        int z = lzx_decode_symbol(s, &s->pre_tree);
        unsigned n;
        if (z < 0 || s->error) return false;
        if (z == 17) {
            n = lzx_read(s, 4) + 4U;
            if (n > last - i) return false;
            while (n--) lengths[i++] = 0;
        } else if (z == 18) {
            n = lzx_read(s, 5) + 20U;
            if (n > last - i) return false;
            while (n--) lengths[i++] = 0;
        } else if (z == 19) {
            int z2;
            uint8_t value;
            n = lzx_read(s, 1) + 4U;
            z2 = lzx_decode_symbol(s, &s->pre_tree);
            if (s->error || z2 < 0 || z2 > 16 || n > last - i) return false;
            value = (uint8_t)((lengths[i] + 17 - z2) % 17);
            while (n--) lengths[i++] = value;
        } else if (z <= 16) {
            lengths[i] = (uint8_t)((lengths[i] + 17 - z) % 17); ++i;
        } else return false;
    }
    return !s->error;
}

static void lzx_init_positions(lzx_state *s, unsigned window_bits) {
    static const uint8_t slots[7] = {30,32,34,36,38,42,50};
    uint32_t base = 0;
    unsigned i;
    s->position_slots = slots[window_bits - 15U];
    for (i = 0; i < s->position_slots; ++i) {
        unsigned extra = i < 4U ? 0U : (i / 2U) - 1U;
        if (extra > 17U) extra = 17U;
        s->position_extra[i] = (uint8_t)extra;
        s->position_base[i] = base;
        base += (uint32_t)1U << extra;
    }
}

static bool lzx_align_word(lzx_state *s, bool always) {
    unsigned discard = s->bit_count & 15U;
    if (discard) {
        if ((s->bits >> (64U - discard)) != 0) return false;
        s->bits <<= discard; s->bit_count -= discard;
    } else if (always && lzx_read(s, 16) != 0) return false;
    return !s->error;
}

static size_t lzx_raw_position(const lzx_state *s) {
    size_t buffered = s->bit_count / 8U;
    return buffered <= s->input_pos ? s->input_pos - buffered : SIZE_MAX;
}

static void lzx_put(lzx_state *s, uint8_t *output, size_t *out_pos, uint8_t value) {
    s->window[s->window_pos] = value;
    s->window_pos = (s->window_pos + 1U) & (s->window_size - 1U);
    output[(*out_pos)++] = value;
}

static void lzx_undo_e8(uint8_t *data, size_t frame_offset, size_t frame_size, int32_t file_size) {
    size_t i = 0;
    if (!file_size || frame_size <= 10U || frame_offset >= 0x40000000U) return;
    while (i < frame_size - 10U) {
        int64_t current;
        int32_t absolute;
        uint32_t relative;
        uint8_t *p = data + frame_offset;
        if (p[i] != 0xe8U) { ++i; continue; }
        current = (int64_t)frame_offset + (int64_t)i;
        absolute = (int32_t)((uint32_t)p[i+1] | ((uint32_t)p[i+2] << 8U) |
                             ((uint32_t)p[i+3] << 16U) | ((uint32_t)p[i+4] << 24U));
        if ((int64_t)absolute >= -current && absolute < file_size) {
            relative = absolute >= 0 ? (uint32_t)((int64_t)absolute - current) : (uint32_t)(absolute + file_size);
            p[i+1]=(uint8_t)relative; p[i+2]=(uint8_t)(relative>>8U);
            p[i+3]=(uint8_t)(relative>>16U); p[i+4]=(uint8_t)(relative>>24U);
        }
        i += 5U;
    }
}

bool xx_lzx_cab_decode(const uint8_t *const *blocks,
                       const size_t *block_sizes,
                       const size_t *plain_sizes,
                       size_t block_count,
                       unsigned window_bits,
                       uint8_t *output,
                       size_t output_size,
                       size_t *written) {
    lzx_state s;
    size_t entry, out_pos = 0, next_frame = LZX_FRAME_SIZE, block_remaining = 0;
    unsigned block_type = 0;
    int32_t intel_size = 0;
    bool raw_odd = false, raw_pad_pending = false, ok = false;
    if (written) *written = 0;
    if (!blocks || !block_sizes || !plain_sizes || !block_count || !output ||
        window_bits < 15U || window_bits > 21U) return false;
    xx_rt_memset(&s, 0, sizeof(s));
    for (entry = 0; entry < block_count; ++entry) {
        if (!blocks[entry] || !block_sizes[entry] || !plain_sizes[entry] ||
            plain_sizes[entry] > LZX_FRAME_SIZE ||
            (entry + 1U < block_count && plain_sizes[entry] != LZX_FRAME_SIZE) ||
            plain_sizes[entry] > output_size - out_pos) return false;
    }
    s.window_size = (uint32_t)1U << window_bits;
    s.window = (uint8_t *)xx_mem_calloc(s.window_size, 1U);
    if (!s.window) return false;
    s.r0 = s.r1 = s.r2 = 1;
    lzx_init_positions(&s, window_bits);
    s.main_symbols = 256U + s.position_slots * 8U;

    for (entry = 0; entry < block_count; ++entry) {
        size_t entry_start = out_pos, entry_target = plain_sizes[entry];
        lzx_bits_init(&s, blocks[entry], block_sizes[entry]);
        if (!entry) {
            if (lzx_read(&s, 1)) {
                uint32_t high = lzx_read(&s, 16), low = lzx_read(&s, 16);
                intel_size = (int32_t)((high << 16U) | low);
            }
            if (s.error) goto done;
        }
        if (raw_pad_pending) {
            if (!s.input_size || s.input[0] != 0) goto done;
            s.input_pos = 1; raw_pad_pending = false;
        }
        while (out_pos - entry_start < entry_target && !s.error) {
            if (!block_remaining) {
                size_t new_size;
                unsigned i;
                block_type = lzx_read(&s, 3);
                new_size = lzx_read(&s, 24);
                if (s.error || !new_size || new_size > output_size - out_pos) goto done;
                block_remaining = new_size; raw_odd = false;
                if (block_type == LZX_BLOCK_ALIGNED) {
                    uint8_t aligned_lengths[LZX_ALIGNED_SYMBOLS];
                    for (i = 0; i < LZX_ALIGNED_SYMBOLS; ++i) aligned_lengths[i] = (uint8_t)lzx_read(&s, 3);
                    if (s.error || !lzx_build_tree(&s.aligned_tree, aligned_lengths, LZX_ALIGNED_SYMBOLS, false)) goto done;
                }
                if (block_type == LZX_BLOCK_VERBATIM || block_type == LZX_BLOCK_ALIGNED) {
                    if (!lzx_read_lengths(&s,s.main_lengths,0,256) ||
                        !lzx_read_lengths(&s,s.main_lengths,256,s.main_symbols) ||
                        !lzx_build_tree(&s.main_tree,s.main_lengths,s.main_symbols,false) ||
                        !lzx_read_lengths(&s,s.length_lengths,0,LZX_LENGTH_SYMBOLS) ||
                        !lzx_build_tree(&s.length_tree,s.length_lengths,LZX_LENGTH_SYMBOLS,true)) goto done;
                } else if (block_type == LZX_BLOCK_UNCOMPRESSED) {
                    size_t raw;
                    const uint8_t *p;
                    uint32_t max_repeat = s.window_size - 3U;
                    if (!lzx_align_word(&s, true)) goto done;
                    raw = lzx_raw_position(&s); s.bits = 0; s.bit_count = 0;
                    if (raw == SIZE_MAX || raw > s.input_size || s.input_size - raw < 12U) goto done;
                    p = s.input + raw;
                    s.r0=(uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);
                    s.r1=(uint32_t)p[4]|((uint32_t)p[5]<<8U)|((uint32_t)p[6]<<16U)|((uint32_t)p[7]<<24U);
                    s.r2=(uint32_t)p[8]|((uint32_t)p[9]<<8U)|((uint32_t)p[10]<<16U)|((uint32_t)p[11]<<24U);
                    if (!s.r0||s.r0>max_repeat||!s.r1||s.r1>max_repeat||!s.r2||s.r2>max_repeat) goto done;
                    s.input_pos = raw + 12U; raw_odd = (new_size & 1U) != 0;
                } else goto done;
            }

            if (block_type == LZX_BLOCK_UNCOMPRESSED) {
                size_t raw = lzx_raw_position(&s), remaining_entry = entry_target - (out_pos - entry_start);
                size_t copy = block_remaining < remaining_entry ? block_remaining : remaining_entry, i;
                if (!copy || raw == SIZE_MAX || raw > s.input_size || copy > s.input_size - raw) goto done;
                for (i = 0; i < copy; ++i) lzx_put(&s, output, &out_pos, s.input[raw + i]);
                s.input_pos = raw + copy; block_remaining -= copy;
                if (!block_remaining) {
                    if (raw_odd) {
                        if (s.input_pos < s.input_size) {
                            if (s.input[s.input_pos++] != 0) goto done;
                        } else raw_pad_pending = true;
                    }
                    raw_odd = false;
                }
                while (out_pos >= next_frame) next_frame += LZX_FRAME_SIZE;
                continue;
            }

            {
                int main_symbol = lzx_decode_symbol(&s, &s.main_tree);
                if (main_symbol < 0 || s.error) goto done;
                if (main_symbol < 256) {
                    if (!block_remaining || out_pos >= output_size || out_pos-entry_start >= entry_target) goto done;
                    lzx_put(&s, output, &out_pos, (uint8_t)main_symbol); --block_remaining;
                } else {
                    unsigned length_header, position_slot, match_length;
                    uint32_t offset;
                    main_symbol -= 256; length_header = (unsigned)main_symbol & 7U;
                    position_slot = (unsigned)main_symbol >> 3U;
                    match_length = length_header + 2U;
                    if (length_header == 7U) {
                        int length_symbol = lzx_decode_symbol(&s, &s.length_tree);
                        if (length_symbol < 0 || s.error) goto done;
                        match_length = (unsigned)length_symbol + 9U;
                    }
                    if (position_slot == 0) offset = s.r0;
                    else if (position_slot == 1) { offset=s.r1; s.r1=s.r0; s.r0=offset; }
                    else if (position_slot == 2) { offset=s.r2; s.r2=s.r0; s.r0=offset; }
                    else {
                        unsigned extra, verbatim = 0;
                        if (position_slot >= s.position_slots) goto done;
                        extra = s.position_extra[position_slot];
                        if (block_type == LZX_BLOCK_ALIGNED && extra >= 3U) {
                            int aligned;
                            verbatim = lzx_read(&s, extra - 3U);
                            aligned = lzx_decode_symbol(&s, &s.aligned_tree);
                            if (aligned < 0 || s.error) goto done;
                            offset = s.position_base[position_slot] - 2U + (verbatim << 3U) + (unsigned)aligned;
                        } else {
                            if (extra) verbatim = lzx_read(&s, extra);
                            offset = s.position_base[position_slot] - 2U + verbatim;
                        }
                        s.r2=s.r1; s.r1=s.r0; s.r0=offset;
                    }
                    if (!offset || offset > s.window_size || offset > (out_pos < s.window_size ? out_pos : s.window_size) ||
                        match_length > block_remaining || match_length > output_size-out_pos ||
                        match_length > entry_target-(out_pos-entry_start) || match_length > next_frame-out_pos) goto done;
                    {
                        unsigned match_count = match_length;
                        while (match_length--) {
                        uint32_t src=(s.window_pos+s.window_size-offset)&(s.window_size-1U);
                        lzx_put(&s,output,&out_pos,s.window[src]);
                        }
                        block_remaining -= match_count;
                    }
                }
            }
            while (out_pos >= next_frame) {
                if (!lzx_align_word(&s, false)) goto done;
                next_frame += LZX_FRAME_SIZE;
            }
        }
        if (s.error || out_pos-entry_start != entry_target) goto done;
        if (block_type != LZX_BLOCK_UNCOMPRESSED && !lzx_align_word(&s, false)) goto done;
        if (s.error || s.bits || s.bit_count || lzx_raw_position(&s) != s.input_size) goto done;
    }
    /* A final odd-sized raw block may end exactly at the CFDATA boundary;
       its otherwise-required zero pad has no following block to protect. */
    if (out_pos != output_size || block_remaining) goto done;
    if (intel_size) {
        size_t frame;
        for (frame=0;frame<output_size;frame+=LZX_FRAME_SIZE) {
            size_t n=output_size-frame; if(n>LZX_FRAME_SIZE)n=LZX_FRAME_SIZE;
            lzx_undo_e8(output,frame,n,intel_size);
        }
    }
    ok = true;
done:
    xx_mem_free(s.window);
    if (ok && written) *written = out_pos;
    return ok;
}
