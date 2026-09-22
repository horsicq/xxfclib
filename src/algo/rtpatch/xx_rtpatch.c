/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Pocket Soft RTPatch adaptive Huffman/LZSS decoder and banner-text expander.
 *
 * Codec design and reference implementation:
 * Copyright (c) 2026 Sandy Carter, https://github.com/bwrsandman/rtptool
 * (MIT License).  This is a direct port of the bounded C++ adaptation in
 * XArchive/Algos/xrtpatchdecoder.cpp, which in turn follows rtptool's
 * src/codec.rs at commit 258d1750340917bcf1361a177b90abd16de40453.
 *
 * The adaptive model is kept as the reference keeps it: one flat byte buffer
 * addressed by the original's structure offsets (0x00 rescale counter, 0x04
 * level count, 0x06 escape width, 0x08 slot count, 0x0a group count, and the
 * four table pointers at 0x0c/0x10/0x18/0x1c/0x20).  That layout is not
 * cosmetic -- the tree-rebalancing code below moves *pointers* between those
 * tables, so a "cleaner" array-of-structs model would not reproduce it.  All
 * accessors are bounds-checked and set tree->valid on failure rather than
 * aborting, exactly as the reference does, because several loops rely on a
 * failed read yielding 0 and falling out of their condition.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/rtpatch/xx_rtpatch.h"

#define XX_RTPATCH_DIFF_MAGIC 0xb59cU
#define XX_RTPATCH_WINDOW_FLAG_8K 8U

/* Worst case allowed by rtp_tree_init: escape width 8 (alphabet 256) and 24
 * levels.  limit = levels*6 + 0x40 + alphabet*4 + 4 + alphabet*2, plus the
 * 0x1c0-byte limit table. */
#define XX_RTPATCH_MODEL_MAX 2196U

#define XX_RTPATCH_TEXT_MAX_LINES 4096U

typedef struct xx_rtpatch_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    int32_t bits_left;
} xx_rtpatch_bits;

typedef struct xx_rtpatch_tree_s {
    uint8_t data[XX_RTPATCH_MODEL_MAX];
    uint32_t data_size;
    uint32_t alphabet;
    const uint8_t *packed;
    size_t packed_size;
    bool valid;
} xx_rtpatch_tree;

typedef struct xx_rtpatch_state_s {
    xx_rtpatch_tree literal;
    xx_rtpatch_tree length;
    xx_rtpatch_tree distance;
} xx_rtpatch_state;

/* ------------------------------------------------------------- bits --- */

static void rtp_bits_init(xx_rtpatch_bits *reader, const uint8_t *data,
                          size_t size) {
    reader->data = data;
    reader->size = size;
    reader->position = 0;
    reader->bits_left = 8;
}

static bool rtp_read_bit(xx_rtpatch_bits *reader, uint32_t *value) {
    uint8_t byte;
    if (!value || (reader->position >= reader->size) ||
        (reader->bits_left < 1) || (reader->bits_left > 8)) {
        return false;
    }
    byte = reader->data[reader->position];
    *value = ((uint32_t)byte >> (unsigned)(reader->bits_left - 1)) & 1U;
    --reader->bits_left;
    if (reader->bits_left == 0) {
        ++reader->position;
        reader->bits_left = 8;
    }
    return true;
}

static bool rtp_read_bits(xx_rtpatch_bits *reader, int32_t count,
                          uint32_t *value) {
    uint32_t result = 0;
    int32_t i;
    if (!value || (count < 0) || (count > 24)) return false;
    for (i = 0; i < count; ++i) {
        uint32_t bit = 0;
        if (!rtp_read_bit(reader, &bit)) return false;
        result = (result << 1) | bit;
    }
    *value = result;
    return true;
}

static bool rtp_set_cursor(xx_rtpatch_bits *reader, size_t position,
                           int32_t bits_left) {
    if ((position > reader->size) || (bits_left < 1) || (bits_left > 8)) {
        return false;
    }
    reader->position = position;
    reader->bits_left = bits_left;
    return true;
}

/* --------------------------------------------------- model accessors --- */

static bool rtp_contains(const xx_rtpatch_tree *tree, int64_t offset,
                         int64_t size) {
    return (offset >= 0) && (size >= 0) &&
           (offset <= (int64_t)tree->data_size) &&
           (size <= (int64_t)tree->data_size - offset);
}

static uint16_t rtp_read16u(xx_rtpatch_tree *tree, int64_t offset) {
    if (!rtp_contains(tree, offset, 2)) {
        tree->valid = false;
        return 0;
    }
    return (uint16_t)((uint16_t)tree->data[(size_t)offset] |
                      (uint16_t)((uint16_t)tree->data[(size_t)offset + 1] << 8));
}

static int16_t rtp_read16s(xx_rtpatch_tree *tree, int64_t offset) {
    return (int16_t)rtp_read16u(tree, offset);
}

static uint32_t rtp_read32(xx_rtpatch_tree *tree, int64_t offset) {
    if (!rtp_contains(tree, offset, 4)) {
        tree->valid = false;
        return 0;
    }
    return (uint32_t)tree->data[(size_t)offset] |
           ((uint32_t)tree->data[(size_t)offset + 1] << 8) |
           ((uint32_t)tree->data[(size_t)offset + 2] << 16) |
           ((uint32_t)tree->data[(size_t)offset + 3] << 24);
}

/* The reference passes qint64 values here and truncates to 16 bits; keep the
 * truncation, several callers depend on the wrap. */
static void rtp_write16(xx_rtpatch_tree *tree, int64_t offset,
                        uint32_t value) {
    if (!rtp_contains(tree, offset, 2)) {
        tree->valid = false;
        return;
    }
    tree->data[(size_t)offset] = (uint8_t)(value & 0xffU);
    tree->data[(size_t)offset + 1] = (uint8_t)((value >> 8) & 0xffU);
}

static void rtp_write32(xx_rtpatch_tree *tree, int64_t offset,
                        uint32_t value) {
    if (!rtp_contains(tree, offset, 4)) {
        tree->valid = false;
        return;
    }
    tree->data[(size_t)offset] = (uint8_t)(value & 0xffU);
    tree->data[(size_t)offset + 1] = (uint8_t)((value >> 8) & 0xffU);
    tree->data[(size_t)offset + 2] = (uint8_t)((value >> 16) & 0xffU);
    tree->data[(size_t)offset + 3] = (uint8_t)((value >> 24) & 0xffU);
}

/* The bit reader owns the compressed buffer; the tree needs raw bytes from it
 * for its own byte-at-a-time code walk, so it holds a borrowed pointer. */
static uint8_t rtp_input_byte(xx_rtpatch_tree *tree, size_t offset) {
    if (!tree->packed || (offset >= tree->packed_size)) {
        tree->valid = false;
        return 0;
    }
    return tree->packed[offset];
}

/* --------------------------------------------------------- model ops --- */

static void rtp_build_limits(xx_rtpatch_tree *tree, int32_t start) {
    uint32_t group_count_offset = rtp_read32(tree, 0x0c);
    uint32_t limit_offset = rtp_read32(tree, 0x10);
    int32_t levels = (int32_t)rtp_read16u(tree, 0x04);
    uint32_t accumulator;
    int32_t level;

    if (!tree->valid || (start < 0) || (start > levels)) {
        tree->valid = false;
        return;
    }
    /* Signed 16-bit doubling that is allowed to wrap: done in uint32_t so the
     * bit pattern matches the reference's qint32 arithmetic without signed
     * overflow. */
    accumulator = (start == 0)
        ? 2U
        : (uint32_t)((int32_t)rtp_read16s(tree,
                                          (int64_t)limit_offset +
                                              (int64_t)(start - 1) * 8)) * 2U;
    for (level = start; level < levels; ++level) {
        uint32_t value = accumulator -
            (uint32_t)((int32_t)rtp_read16s(
                tree, (int64_t)group_count_offset + (int64_t)level * 2));
        rtp_write16(tree, (int64_t)limit_offset + (int64_t)level * 8, value);
        accumulator = value * 2U;
        rtp_write16(tree, (int64_t)limit_offset + (int64_t)level * 8 + 2,
                    accumulator);
        rtp_write16(tree, (int64_t)limit_offset + (int64_t)level * 8 + 4,
                    value * 4U);
        rtp_write16(tree, (int64_t)limit_offset + (int64_t)level * 8 + 6,
                    value * 16U);
    }
}

static bool rtp_init(xx_rtpatch_tree *tree, int32_t escape_bits,
                     int32_t levels, uint32_t initial_period,
                     uint32_t update_period) {
    uint32_t group_count_offset;
    uint32_t symbol_table_offset;
    uint32_t slot_offset;
    uint32_t weight_offset;
    uint32_t limit_offset;
    uint32_t data_size;
    uint32_t weight_base;
    uint32_t i;
    int32_t j;

    if ((escape_bits < 1) || (escape_bits > 8) || (levels < 2) ||
        (levels > 24) || (initial_period == 0) || (update_period == 0) ||
        (initial_period > 0xffffU) || (update_period > 0xffffU)) {
        return false;
    }
    tree->valid = true;
    tree->packed = NULL;
    tree->packed_size = 0;
    tree->alphabet = 1U << (unsigned)escape_bits;

    group_count_offset = 0x34U;
    symbol_table_offset = (uint32_t)(levels * 2 + 0x34);
    slot_offset = (uint32_t)(levels * 6 + 0x38);
    weight_offset = (uint32_t)(levels * 6 + 0x40) + tree->alphabet * 4U;
    limit_offset = weight_offset + 4U + tree->alphabet * 2U;
    data_size = limit_offset + 0x1c0U;
    if (data_size > XX_RTPATCH_MODEL_MAX) return false;
    tree->data_size = data_size;
    xx_rt_memset(tree->data, 0, (size_t)data_size);

    rtp_write16(tree, 0x32, initial_period);
    rtp_write16(tree, 0x02, initial_period);
    rtp_write16(tree, 0x00, initial_period);
    rtp_write16(tree, 0x30, update_period);
    rtp_write16(tree, 0x2e, update_period);
    rtp_write16(tree, 0x2c, update_period);
    rtp_write16(tree, 0x06, (uint32_t)escape_bits);
    rtp_write16(tree, 0x04, (uint32_t)levels);
    rtp_write32(tree, 0x0c, group_count_offset);
    rtp_write32(tree, 0x20, symbol_table_offset);
    rtp_write32(tree, 0x1c, slot_offset);
    rtp_write32(tree, 0x18, weight_offset);
    rtp_write32(tree, 0x14, limit_offset);
    rtp_write32(tree, 0x10, limit_offset);

    rtp_write16(tree, 0x0a, 1);
    rtp_write16(tree, 0x08, 1);
    rtp_write16(tree, group_count_offset, 2);
    for (j = 1; j < levels; ++j) {
        rtp_write16(tree, (int64_t)group_count_offset + (int64_t)j * 2, 0);
    }
    rtp_write32(tree, symbol_table_offset, slot_offset);
    for (j = 1; j <= levels; ++j) {
        rtp_write32(tree, (int64_t)symbol_table_offset + (int64_t)j * 4,
                    slot_offset + 8U);
    }

    weight_base = weight_offset + tree->alphabet * 2U;
    rtp_write32(tree, slot_offset, weight_base);
    for (i = 1; i <= tree->alphabet + 1U; ++i) {
        rtp_write32(tree, (int64_t)slot_offset + (int64_t)i * 4,
                    weight_base + 2U);
    }
    /* Every symbol starts at 0x8000, i.e. "not yet seen": the top bit is what
     * the rebalancer sorts on, so this is deliberate, not a stray weight. */
    for (i = 0; i < tree->alphabet; ++i) {
        rtp_write16(tree, (int64_t)weight_offset + (int64_t)i * 2, 0x8000U);
    }
    rtp_write16(tree, (int64_t)weight_offset + (int64_t)tree->alphabet * 2, 0);
    rtp_write16(tree, (int64_t)weight_offset + (int64_t)tree->alphabet * 2 + 2,
                0);
    rtp_write16(tree, 0x24, tree->alphabet);
    for (i = 0; i < 0x30U; ++i) {
        rtp_write32(tree, (int64_t)limit_offset + (int64_t)i * 4, 0);
    }
    rtp_build_limits(tree, 0);
    return tree->valid;
}

static bool rtp_update_frequency(xx_rtpatch_tree *tree, uint32_t symbol) {
    uint32_t weight_offset;
    uint32_t weight;
    int32_t counter;
    if (symbol > tree->alphabet) {
        tree->valid = false;
        return false;
    }
    weight_offset = rtp_read32(tree, 0x18);
    weight = rtp_read16u(tree, (int64_t)weight_offset + (int64_t)symbol * 2);
    rtp_write16(tree, (int64_t)weight_offset + (int64_t)symbol * 2, weight + 1U);
    counter = (int32_t)rtp_read16s(tree, 0x00) - 1;
    rtp_write16(tree, 0x00, (uint32_t)counter);
    return (uint16_t)counter == 0;
}

static int32_t rtp_add_symbol(xx_rtpatch_tree *tree, uint32_t new_symbol) {
    uint32_t weight_offset;
    uint32_t slot_offset;
    uint32_t group_count_offset;
    uint32_t symbol_table_offset;
    uint32_t slot_count;
    uint32_t group_count;
    uint32_t levels;
    uint32_t group;
    uint32_t next;
    uint32_t i;

    if (new_symbol >= tree->alphabet) return -1;
    weight_offset = rtp_read32(tree, 0x18);
    slot_offset = rtp_read32(tree, 0x1c);
    group_count_offset = rtp_read32(tree, 0x0c);
    symbol_table_offset = rtp_read32(tree, 0x20);
    rtp_write16(tree, (int64_t)weight_offset + (int64_t)new_symbol * 2, 1);
    slot_count = rtp_read16u(tree, 0x08);
    if (!tree->valid || (slot_count > tree->alphabet)) return -1;
    rtp_write32(tree, (int64_t)slot_offset + (int64_t)slot_count * 4,
                weight_offset + new_symbol * 2U);
    ++slot_count;
    rtp_write16(tree, 0x08, slot_count);
    if (slot_count == 2) return tree->valid ? 0 : -1;

    group_count = rtp_read16u(tree, 0x0a);
    levels = rtp_read16u(tree, 0x04);
    if (!tree->valid || (group_count == 0) || (group_count > levels)) {
        return -1;
    }
    group = 0;
    if (group_count < levels) {
        group = (group_count - 1U) & 0xffffU;
        rtp_write16(tree, 0x0a, group_count + 1U);
    } else {
        if (group_count < 2) return -1;
        group = (group_count - 2U) & 0xffffU;
        for (i = 0;
             (i < levels) &&
             (rtp_read16s(tree, (int64_t)group_count_offset +
                                    (int64_t)group * 2) == 0);
             ++i) {
            if (group == 0) return -1;
            group = (group - 1U) & 0xffffU;
        }
    }
    rtp_write16(tree, (int64_t)group_count_offset + (int64_t)group * 2,
                (uint32_t)((int32_t)rtp_read16s(
                    tree, (int64_t)group_count_offset + (int64_t)group * 2) - 1));
    rtp_write16(tree, (int64_t)group_count_offset + (int64_t)(group + 1U) * 2,
                (uint32_t)((int32_t)rtp_read16s(
                    tree, (int64_t)group_count_offset +
                              (int64_t)(group + 1U) * 2) + 2));
    next = rtp_read32(tree, (int64_t)symbol_table_offset +
                                (int64_t)(group + 1U) * 4);
    if (next < 4) return -1;
    rtp_write32(tree, (int64_t)symbol_table_offset + (int64_t)(group + 1U) * 4,
                next - 4U);
    for (i = group + 2U; i <= levels; ++i) {
        rtp_write32(tree, (int64_t)symbol_table_offset + (int64_t)i * 4,
                    rtp_read32(tree, (int64_t)symbol_table_offset +
                                         (int64_t)i * 4) + 4U);
    }
    return tree->valid ? (int32_t)group : -1;
}

static void rtp_rebuild(xx_rtpatch_tree *tree) {
    uint32_t slot_offset = rtp_read32(tree, 0x1c);
    uint32_t symbol_table_offset = rtp_read32(tree, 0x20);
    uint32_t group_count_offset = rtp_read32(tree, 0x0c);
    uint32_t slot_count = rtp_read16u(tree, 0x08);
    int32_t update_counter = (int32_t)rtp_read16s(tree, 0x2c);
    uint32_t levels = rtp_read16u(tree, 0x04);
    uint32_t maximum_weight = 0;
    uint32_t level;
    uint32_t group_count;
    uint32_t last_group;
    int32_t moved = 0;
    int32_t guard = 0;
    uint32_t i;

    if (!tree->valid || (slot_count > tree->alphabet + 1U) || (levels == 0) ||
        (levels > 24)) {
        tree->valid = false;
        return;
    }
    rtp_write16(tree, 0x2c, (uint32_t)(update_counter - 1));

    for (i = 0; i < slot_count; ++i) {
        uint32_t pointer = rtp_read32(tree, (int64_t)slot_offset +
                                                (int64_t)i * 4);
        uint32_t weight = rtp_read16u(tree, (int64_t)pointer);
        /* The halving happens only when the rescale counter wraps to zero,
         * and it is applied after the decrement above -- keep that order. */
        if ((uint16_t)(update_counter - 1) == 0) {
            weight >>= 1;
            rtp_write16(tree, (int64_t)pointer, weight);
        }
        if (weight > maximum_weight) maximum_weight = weight;
    }
    if (!tree->valid) return;

    if (maximum_weight != 0) {
        uint32_t mask = 0x8000U;
        uint32_t position = 0;
        bool done = false;
        int32_t bit_guard;
        for (bit_guard = 0;
             ((maximum_weight & mask) == 0) && (bit_guard < 16); ++bit_guard) {
            mask = (mask >> 1) | 0x8000U;
        }
        while ((position < slot_count) && !done && tree->valid) {
            uint32_t current_pointer =
                rtp_read32(tree, (int64_t)slot_offset + (int64_t)position * 4);
            if ((rtp_read16u(tree, (int64_t)current_pointer) & mask) == 0) {
                uint32_t scan = position + 1U;
                uint32_t insertion = position;
                uint32_t last_insertion;
                uint32_t next_mask;
                if (scan >= slot_count) break;
                last_insertion = insertion;
                while (scan < slot_count) {
                    uint32_t scan_slot = slot_offset + scan * 4U;
                    uint32_t scan_pointer = rtp_read32(tree, (int64_t)scan_slot);
                    last_insertion = insertion;
                    if ((rtp_read16u(tree, (int64_t)scan_pointer) & mask) != 0) {
                        uint32_t saved;
                        last_insertion = insertion + 1U;
                        saved = rtp_read32(tree, (int64_t)slot_offset +
                                                     (int64_t)insertion * 4);
                        rtp_write32(tree, (int64_t)slot_offset +
                                              (int64_t)insertion * 4,
                                    scan_pointer);
                        rtp_write32(tree, (int64_t)scan_slot, saved);
                    }
                    ++scan;
                    insertion = last_insertion;
                }
                if (last_insertion != position) position = last_insertion - 1U;
                next_mask = mask >> 1;
                mask = next_mask | 0x8000U;
                if (next_mask & 1U) done = true;
            } else {
                ++position;
            }
        }
    }
    if (!tree->valid) return;

    level = 0;
    group_count = rtp_read16u(tree, 0x0a);
    if ((group_count == 0) || (group_count > levels)) {
        tree->valid = false;
        return;
    }
    last_group = (group_count - 1U) & 0xffffU;
    while ((level < group_count) && tree->valid) {
        uint32_t group_offset;
        uint32_t table_offset;
        uint32_t group_size;
        uint32_t table_end;
        uint32_t first_slot;
        uint32_t first_weight_pointer;
        uint32_t last_weight_pointer;
        uint32_t previous_weight_pointer;
        uint32_t first_weight;
        uint32_t last_weight;
        uint32_t previous_weight;

        if (++guard > 65536) {
            tree->valid = false;
            return;
        }
        group_offset = group_count_offset + level * 2U;
        table_offset = symbol_table_offset + level * 4U;
        group_size = rtp_read16u(tree, (int64_t)group_offset);
        if (group_size == 0) {
            ++level;
            continue;
        }
        table_end = rtp_read32(tree, (int64_t)table_offset + 4);
        if (table_end < 8) {
            tree->valid = false;
            return;
        }
        first_slot = rtp_read32(tree, (int64_t)table_offset);
        first_weight_pointer = rtp_read32(tree, (int64_t)first_slot);
        last_weight_pointer = rtp_read32(tree, (int64_t)table_end - 4);
        previous_weight_pointer = rtp_read32(tree, (int64_t)table_end - 8);
        first_weight = rtp_read16u(tree, (int64_t)first_weight_pointer);
        last_weight = rtp_read16u(tree, (int64_t)last_weight_pointer);
        previous_weight = rtp_read16u(tree, (int64_t)previous_weight_pointer);

        if ((group_size < 3) || (((levels - 1U) & 0xffffU) == level) ||
            (first_weight < last_weight + previous_weight)) {
            uint32_t moving_table_offset = table_offset + 4U;
            bool found = false;
            int32_t accumulator = (int32_t)last_weight;
            uint32_t target_level = level + 2U;
            uint32_t moving_value;
            uint32_t before_target;

            while (target_level < group_count) {
                uint32_t target_table =
                    symbol_table_offset + target_level * 4U;
                uint32_t target_slot = rtp_read32(tree, (int64_t)target_table);
                uint32_t target_group_size;
                uint32_t next_slot;
                uint32_t next_weight;
                accumulator =
                    (accumulator -
                     (int32_t)rtp_read16s(
                         tree, (int64_t)rtp_read32(tree, (int64_t)target_slot))) &
                    0xffff;
                target_group_size =
                    rtp_read16u(tree, (int64_t)group_count_offset +
                                          (int64_t)target_level * 2);
                next_slot = rtp_read32(tree, (int64_t)target_slot + 4);
                next_weight = rtp_read16u(tree, (int64_t)next_slot);
                if ((target_group_size > 1) &&
                    ((accumulator & 0x8000) ||
                     ((uint32_t)accumulator < next_weight))) {
                    found = true;
                    break;
                }
                ++target_level;
            }
            if (!found) {
                ++level;
                continue;
            }

            rtp_write16(tree, (int64_t)group_offset,
                        (uint32_t)((int32_t)rtp_read16s(
                            tree, (int64_t)group_offset) - 1));
            ++moved;
            ++level;
            moving_value = rtp_read32(tree, (int64_t)moving_table_offset);
            if (moving_value < 4) {
                tree->valid = false;
                return;
            }
            rtp_write32(tree, (int64_t)moving_table_offset, moving_value - 4U);
            rtp_write16(tree, (int64_t)group_count_offset + (int64_t)level * 2,
                        (uint32_t)((int32_t)rtp_read16s(
                            tree, (int64_t)group_count_offset +
                                      (int64_t)level * 2) + 2));
            rtp_write32(tree, (int64_t)table_offset + 8,
                        rtp_read32(tree, (int64_t)table_offset + 8) + 4U);
            before_target = (target_level - 1U) & 0xffffU;
            if (level < before_target) {
                uint32_t span = before_target - level;
                uint32_t k;
                level += span;
                for (k = 0; k < span; ++k) {
                    rtp_write32(tree, (int64_t)moving_table_offset + 8,
                                rtp_read32(tree,
                                           (int64_t)moving_table_offset + 8) +
                                    4U);
                    moving_table_offset += 4U;
                }
            }
            rtp_write16(tree, (int64_t)group_count_offset + (int64_t)level * 2,
                        (uint32_t)((int32_t)rtp_read16s(
                            tree, (int64_t)group_count_offset +
                                      (int64_t)level * 2) + 1));
            rtp_write32(tree, (int64_t)moving_table_offset + 4,
                        rtp_read32(tree, (int64_t)moving_table_offset + 4) + 4U);
            rtp_write16(tree,
                        (int64_t)group_count_offset + (int64_t)(level + 1U) * 2,
                        (uint32_t)((int32_t)rtp_read16s(
                            tree, (int64_t)group_count_offset +
                                      (int64_t)(level + 1U) * 2) - 2));
            if (rtp_read16s(tree, (int64_t)group_count_offset +
                                      (int64_t)last_group * 2) == 0) {
                if (group_count == 0) {
                    tree->valid = false;
                    return;
                }
                --group_count;
                rtp_write16(tree, 0x0a, group_count);
                last_group = (last_group - 1U) & 0xffffU;
            }
            /* Restarting from level 0 after every move is load-bearing: the
             * move can invalidate an earlier level's ordering. */
            level = 0;
        } else {
            uint32_t end;
            if (level == 0) {
                tree->valid = false;
                return;
            }
            ++moved;
            rtp_write16(tree, (int64_t)group_offset - 2,
                        (uint32_t)((int32_t)rtp_read16s(
                            tree, (int64_t)group_offset - 2) + 1));
            rtp_write16(tree, (int64_t)group_offset,
                        (uint32_t)((int32_t)rtp_read16s(
                            tree, (int64_t)group_offset) - 3));
            rtp_write16(tree, (int64_t)group_offset + 2,
                        (uint32_t)((int32_t)rtp_read16s(
                            tree, (int64_t)group_offset + 2) + 2));
            rtp_write32(tree, (int64_t)table_offset,
                        rtp_read32(tree, (int64_t)table_offset) + 4U);
            end = rtp_read32(tree, (int64_t)table_offset + 4);
            if (end < 8) {
                tree->valid = false;
                return;
            }
            rtp_write32(tree, (int64_t)table_offset + 4, end - 8U);
            if (last_group == level) {
                ++group_count;
                if (group_count > levels) {
                    tree->valid = false;
                    return;
                }
                rtp_write16(tree, 0x0a, group_count);
                last_group = (last_group + 1U) & 0xffffU;
            }
            level = 0;
        }
    }
    if (!tree->valid) return;

    if (moved < 0x10) {
        if ((moved < 8) && (rtp_read16u(tree, 0x2e) != 1)) {
            rtp_write16(tree, 0x02, (uint32_t)rtp_read16u(tree, 0x02) << 1);
            rtp_write16(tree, 0x2c, (uint32_t)rtp_read16u(tree, 0x2c) >> 1);
            rtp_write16(tree, 0x2e, (uint32_t)rtp_read16u(tree, 0x2e) >> 1);
        }
    } else {
        rtp_write16(tree, 0x02, rtp_read16u(tree, 0x32));
        rtp_write16(tree, 0x2e, rtp_read16u(tree, 0x30));
    }
    rtp_write16(tree, 0x00, rtp_read16u(tree, 0x02));
    if (rtp_read16u(tree, 0x2c) == 0) {
        rtp_write16(tree, 0x2c, rtp_read16u(tree, 0x2e));
    }
}

/* ----------------------------------------------------- symbol decode --- */

static bool rtp_decode_symbol(xx_rtpatch_tree *tree, xx_rtpatch_bits *reader,
                              uint16_t *symbol) {
    int32_t bits_left;
    uint8_t current;
    int32_t value;
    int32_t index;
    int32_t total_bits;
    int32_t count;
    int32_t new_bits_left = 0;
    uint32_t limit_offset;
    uint32_t symbol_table_offset;
    uint32_t weight_offset;
    uint32_t level;
    uint32_t slot_array;
    int32_t base;
    uint32_t slot_index;
    uint32_t weight_pointer;
    uint32_t decoded;

    if (!symbol || !tree->valid || (reader->position >= reader->size)) {
        return false;
    }
    bits_left = reader->bits_left;
    current = rtp_input_byte(tree, reader->position);
    if (!tree->valid) return false;
    value = (int32_t)(((1U << (unsigned)bits_left) - 1U) & (uint32_t)current);
    index = bits_left - 1;
    total_bits = bits_left;
    limit_offset = rtp_read32(tree, 0x10);
    if (!tree->valid) return false;

    /* Walk whole bytes until the accumulated code reaches this level's lower
     * limit.  A failed limit read yields 0, which ends the walk; the validity
     * check after the loop is what actually reports it. */
    if ((uint32_t)value <
        rtp_read16u(tree, (int64_t)limit_offset + (int64_t)index * 8)) {
        do {
            uint8_t next;
            if (!rtp_set_cursor(reader, reader->position + 1,
                                reader->bits_left) ||
                (reader->position >= reader->size)) {
                return false;
            }
            index += 8;
            total_bits += 8;
            next = rtp_input_byte(tree, reader->position);
            value = (int32_t)((((uint32_t)value & 0xffU) << 8) |
                              (uint32_t)next) &
                    0xffff;
        } while ((uint32_t)value <
                 rtp_read16u(tree, (int64_t)limit_offset + (int64_t)index * 8));
    }
    if (!tree->valid) return false;

    --index;
    count = (total_bits - 1) & 0xff;
    while (count != 0) {
        int32_t threshold;
        if (index < 0) return false;
        threshold = (int32_t)rtp_read16u(
            tree, (int64_t)limit_offset + 2 + (int64_t)index * 8);
        if (!tree->valid || (value < threshold)) break;
        --index;
        --count;
        value >>= 1;
        ++new_bits_left;
    }
    if (index < -1) return false;

    symbol_table_offset = rtp_read32(tree, 0x20);
    weight_offset = rtp_read32(tree, 0x18);
    level = (uint32_t)(index + 1) & 0xffffU;
    if (!tree->valid || (level > (uint32_t)rtp_read16u(tree, 0x04))) return false;
    slot_array = rtp_read32(tree, (int64_t)symbol_table_offset +
                                      (int64_t)level * 4);
    base = (int32_t)rtp_read16s(tree,
                                (int64_t)limit_offset + (int64_t)level * 8);
    slot_index = (uint32_t)(value - base) & 0xffffU;
    weight_pointer = rtp_read32(tree, (int64_t)slot_array +
                                          (int64_t)slot_index * 4);
    if (!tree->valid || (weight_pointer < weight_offset) ||
        ((weight_pointer - weight_offset) & 1U)) {
        return false;
    }
    decoded = (weight_pointer - weight_offset) >> 1;
    if (decoded > tree->alphabet) return false;

    if (new_bits_left == 0) {
        new_bits_left = 8;
        if (!rtp_set_cursor(reader, reader->position + 1, new_bits_left)) {
            return false;
        }
    } else if (!rtp_set_cursor(reader, reader->position, new_bits_left)) {
        return false;
    }

    if (rtp_update_frequency(tree, decoded)) {
        rtp_rebuild(tree);
        rtp_build_limits(tree, 0);
    }
    if (!tree->valid) return false;

    /* 0x24 holds the alphabet size, which doubles as the escape symbol: the
     * literal that follows is raw and is then inserted into the tree. */
    if (decoded == (uint32_t)rtp_read16u(tree, 0x24)) {
        uint32_t raw_symbol = 0;
        int32_t affected_level;
        if (!rtp_read_bits(reader, (int32_t)rtp_read16u(tree, 0x06),
                           &raw_symbol) ||
            (raw_symbol >= tree->alphabet)) {
            return false;
        }
        affected_level = rtp_add_symbol(tree, raw_symbol);
        if ((affected_level < 0) || !tree->valid) return false;
        rtp_build_limits(tree, affected_level);
        if (!tree->valid) return false;
        *symbol = (uint16_t)raw_symbol;
        return true;
    }

    *symbol = (uint16_t)decoded;
    return true;
}

/* ------------------------------------------------------- public API --- */

bool xx_rtpatch_decode_memory(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size,
                              size_t *written) {
    xx_rtpatch_state *state;
    xx_rtpatch_bits reader;
    uint32_t magic = 0;
    uint32_t use_raw_literals = 0;
    uint32_t reserved = 0;
    uint32_t initial_period = 0;
    uint32_t update_period = 0;
    uint32_t window_flag = 0;
    int32_t distance_bits;
    size_t produced = 0;
    bool ok = true;

    if (written) *written = 0;
    if (!input || (input_size < 8) || (!output && (output_size != 0))) {
        return false;
    }

    rtp_bits_init(&reader, input, input_size);
    if (!rtp_read_bits(&reader, 16, &magic) ||
        (magic != XX_RTPATCH_DIFF_MAGIC) ||
        !rtp_read_bits(&reader, 8, &use_raw_literals) ||
        !rtp_read_bits(&reader, 8, &reserved) ||
        !rtp_read_bits(&reader, 12, &initial_period) ||
        !rtp_read_bits(&reader, 12, &update_period) ||
        !rtp_read_bits(&reader, 4, &window_flag) ||
        (use_raw_literals > 1) || (reserved != 0xffU) ||
        (initial_period == 0) || (update_period == 0)) {
        return false;
    }

    distance_bits = (window_flag == XX_RTPATCH_WINDOW_FLAG_8K) ? 7 : 6;

    state = (xx_rtpatch_state *)xx_mem_alloc(sizeof(xx_rtpatch_state));
    if (!state) return false;

    if (((use_raw_literals == 0) &&
         !rtp_init(&state->literal, 8, 0x10, initial_period, update_period)) ||
        !rtp_init(&state->length, 6, 0x0c, initial_period, update_period) ||
        !rtp_init(&state->distance, 6, 0x0c, initial_period, update_period)) {
        xx_mem_free(state);
        return false;
    }
    state->literal.packed = input;
    state->literal.packed_size = input_size;
    state->length.packed = input;
    state->length.packed_size = input_size;
    state->distance.packed = input;
    state->distance.packed_size = input_size;

    while (ok && (produced < output_size)) {
        uint32_t flag = 0;
        if (!rtp_read_bit(&reader, &flag)) {
            ok = false;
            break;
        }
        if (flag == 0) {
            uint32_t symbol = 0;
            if (use_raw_literals == 0) {
                uint16_t decoded_symbol = 0;
                if (!rtp_decode_symbol(&state->literal, &reader,
                                       &decoded_symbol)) {
                    ok = false;
                    break;
                }
                symbol = decoded_symbol;
            } else if (!rtp_read_bits(&reader, 8, &symbol)) {
                ok = false;
                break;
            }
            if (symbol > 0xffU) {
                ok = false;
                break;
            }
            output[produced++] = (uint8_t)symbol;
        } else {
            uint32_t distance_low = 0;
            uint16_t distance_high = 0;
            uint32_t distance;
            uint16_t length_symbol = 0;
            uint32_t length;
            size_t back;
            uint32_t i;

            if (!rtp_read_bits(&reader, distance_bits, &distance_low) ||
                !rtp_decode_symbol(&state->distance, &reader,
                                   &distance_high)) {
                ok = false;
                break;
            }
            distance = ((uint32_t)distance_high << (unsigned)distance_bits) |
                       distance_low;
            /* Distance 0 is the end-of-stream marker. */
            if (distance == 0) break;
            if (!rtp_decode_symbol(&state->length, &reader, &length_symbol)) {
                ok = false;
                break;
            }
            /* Only seven bits of the length symbol are used; the reference
             * masks rather than validating, and so do we. */
            length = (uint32_t)length_symbol & 0x7fU;
            if ((size_t)length > output_size - produced) {
                ok = false;
                break;
            }
            back = (size_t)distance + 1U;
            for (i = 0; i < length; ++i) {
                /* A reference reaching before the start of output emits NUL
                 * instead of failing.  That is what the reference decoder
                 * does and real RTPatch streams rely on it for their first
                 * window, so it must not be "fixed" into an error. */
                output[produced] = (produced >= back)
                                       ? output[produced - back]
                                       : (uint8_t)0;
                ++produced;
            }
        }
    }

    xx_mem_free(state);

    /* The container stores the decoded length, so anything short of it is a
     * truncated stream, never a success. */
    if (!ok || (produced != output_size)) {
        return false;
    }
    if (written) *written = produced;
    return true;
}

bool xx_rtpatch_text_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written) {
    size_t position = 2;
    size_t produced = 0;
    uint32_t line_count;
    uint32_t line;

    if (written) *written = 0;
    if (!input || (input_size < 2) || (!output && (output_size != 0))) {
        return false;
    }

    line_count = (uint32_t)input[0] | ((uint32_t)input[1] << 8);
    if ((line_count == 0) || (line_count > XX_RTPATCH_TEXT_MAX_LINES)) {
        return false;
    }

    for (line = 0; line < line_count; ++line) {
        uint32_t length;
        uint32_t j;
        if (position >= input_size) return false;
        length = input[position++];
        /* The stored length counts the terminator, so it is never zero and
         * the final byte of the run must be the NUL. */
        /* position < input_size holds here, so input_size - position cannot
         * wrap; the reference's signed form of this test did the same. */
        if ((length == 0) || (length > input_size - position) ||
            (input[position + length - 1] != 0)) {
            return false;
        }
        for (j = 0; j + 1 < length; ++j) {
            if (input[position + j] == 0) return false;
        }
        if (((size_t)(length - 1) > output_size - produced) ||
            ((size_t)2 > output_size - produced - (size_t)(length - 1))) {
            return false;
        }
        xx_rt_memcpy(output + produced, input + position, (size_t)(length - 1));
        produced += (size_t)(length - 1);
        output[produced++] = 0x0d;
        output[produced++] = 0x0a;
        position += length;
    }

    if ((position != input_size) || (produced != output_size)) return false;
    if (written) *written = produced;
    return true;
}
