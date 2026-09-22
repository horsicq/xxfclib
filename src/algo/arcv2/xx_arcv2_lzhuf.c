/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native decoder for the compact ARCV LZHUF member stream.  It is a clean C
 * implementation of the format's MSB-first adaptive Huffman/LZ model rather
 * than a bridge to an archive program or a third-party decoder.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/arcv2/xx_arcv2_lzhuf.h"

#include <string.h>

#define ARCV2_LZHUF_DICTIONARY_SIZE 4096U
#define ARCV2_LZHUF_MAX_FREQ 0x8000U
#define ARCV2_LZHUF_NCHAR_COMPACT 287U
#define ARCV2_LZHUF_NCHAR_WIDE 315U
#define ARCV2_LZHUF_T_MAX (ARCV2_LZHUF_NCHAR_WIDE * 2U - 1U)

typedef struct arcv2_lzhuf_bits_s {
    const uint8_t *input;
    size_t input_size;
    size_t bit_position;
} arcv2_lzhuf_bits;

typedef struct arcv2_lzhuf_model_s {
    uint32_t frequency[ARCV2_LZHUF_T_MAX + 1U];
    uint32_t parent[ARCV2_LZHUF_T_MAX + ARCV2_LZHUF_NCHAR_WIDE];
    uint32_t child[ARCV2_LZHUF_T_MAX];
    uint8_t position_length[256U];
    uint8_t position_code[256U];
    uint32_t character_count;
    uint32_t tree_size;
    uint32_t root;
} arcv2_lzhuf_model;

static int arcv2_lzhuf_read_bit(arcv2_lzhuf_bits *bits) {
    uint8_t byte;
    if (!bits || bits->bit_position / 8U >= bits->input_size) return -1;
    byte = bits->input[bits->bit_position / 8U];
    return (int)((byte >> (7U - (unsigned)(bits->bit_position++ & 7U))) & 1U);
}

static bool arcv2_lzhuf_reconstruct(arcv2_lzhuf_model *model) {
    uint32_t leaf_count = 0U;
    uint32_t index;
    if (!model) return false;
    for (index = 0U; index < model->tree_size; ++index) {
        if (model->child[index] >= model->tree_size) {
            if (leaf_count >= model->character_count) return false;
            model->frequency[leaf_count] = (model->frequency[index] + 1U) / 2U;
            model->child[leaf_count] = model->child[index];
            ++leaf_count;
        }
    }
    if (leaf_count != model->character_count) return false;
    for (index = 0U; index < model->character_count - 1U; ++index) {
        uint32_t node = model->character_count + index;
        uint32_t sum = model->frequency[index * 2U] +
                       model->frequency[index * 2U + 1U];
        uint32_t insertion = node;
        uint32_t move;
        while (insertion != 0U && sum < model->frequency[insertion - 1U])
            --insertion;
        for (move = node; move > insertion; --move) {
            model->frequency[move] = model->frequency[move - 1U];
            model->child[move] = model->child[move - 1U];
        }
        model->frequency[insertion] = sum;
        model->child[insertion] = index * 2U;
    }
    for (index = 0U; index < model->tree_size; ++index) {
        uint32_t node = model->child[index];
        if (node >= sizeof(model->parent) / sizeof(model->parent[0]))
            return false;
        model->parent[node] = index;
        if (node < model->tree_size) {
            if (node + 1U >= sizeof(model->parent) / sizeof(model->parent[0]))
                return false;
            model->parent[node + 1U] = index;
        }
    }
    return true;
}

static bool arcv2_lzhuf_update(arcv2_lzhuf_model *model,
                               uint32_t character) {
    uint32_t current;
    uint32_t guard = 0U;
    if (!model || character >= model->character_count) return false;
    if (model->frequency[model->root] == ARCV2_LZHUF_MAX_FREQ &&
        !arcv2_lzhuf_reconstruct(model))
        return false;
    current = model->parent[character + model->tree_size];
    do {
        uint32_t updated;
        uint32_t next;
        if (current >= model->tree_size || guard++ > model->tree_size)
            return false;
        updated = ++model->frequency[current];
        next = current + 1U;
        if (next > model->tree_size) return false;
        if (updated > model->frequency[next]) {
            while (next + 1U <= model->tree_size &&
                   updated > model->frequency[next + 1U])
                ++next;
            {
                uint32_t old_child = model->child[current];
                uint32_t new_child = model->child[next];
                if (old_child >= sizeof(model->parent) / sizeof(model->parent[0]) ||
                    new_child >= sizeof(model->parent) / sizeof(model->parent[0]))
                    return false;
                model->frequency[current] = model->frequency[next];
                model->frequency[next] = updated;
                model->parent[old_child] = next;
                if (old_child < model->tree_size) model->parent[old_child + 1U] = next;
                model->child[next] = old_child;
                model->parent[new_child] = current;
                if (new_child < model->tree_size) model->parent[new_child + 1U] = current;
                model->child[current] = new_child;
                current = next;
            }
        }
        current = model->parent[current];
    } while (current != 0U);
    return true;
}

static int arcv2_lzhuf_decode_character(arcv2_lzhuf_model *model,
                                        arcv2_lzhuf_bits *bits) {
    uint32_t current;
    uint32_t guard = 0U;
    if (!model || !bits) return -2;
    current = model->child[model->root];
    while (current < model->tree_size) {
        int bit;
        if (current + 1U >= model->tree_size || guard++ > model->tree_size)
            return -2;
        bit = arcv2_lzhuf_read_bit(bits);
        if (bit < 0) return -1;
        current = model->child[current + (uint32_t)bit];
    }
    if (current < model->tree_size ||
        current - model->tree_size >= model->character_count ||
        !arcv2_lzhuf_update(model, current - model->tree_size))
        return -2;
    return (int)(current - model->tree_size);
}

static int arcv2_lzhuf_decode_position(const arcv2_lzhuf_model *model,
                                       arcv2_lzhuf_bits *bits) {
    uint32_t first = 0U;
    uint32_t index;
    uint32_t shifted;
    uint32_t remaining;
    if (!model || !bits) return -1;
    for (index = 0U; index < 8U; ++index) {
        int bit = arcv2_lzhuf_read_bit(bits);
        if (bit < 0) return -1;
        first = (first << 1U) | (uint32_t)bit;
    }
    if (model->position_length[first] < 2U) return -1;
    shifted = first;
    remaining = (uint32_t)model->position_length[first] - 2U;
    while (remaining-- != 0U) {
        int bit = arcv2_lzhuf_read_bit(bits);
        if (bit < 0) return -1;
        shifted = (shifted << 1U) | (uint32_t)bit;
    }
    return (int)(((uint32_t)model->position_code[first] << 6U) |
                 (shifted & 0x3fU));
}

static bool arcv2_lzhuf_init(arcv2_lzhuf_model *model, bool wide) {
    static const uint8_t symbols_per_length[6U] = { 1U, 3U, 8U,
                                                      12U, 24U, 16U };
    uint32_t prefix = 0U;
    uint32_t symbol = 0U;
    uint32_t length;
    uint32_t index;
    if (!model) return false;
    xx_rt_memset(model, 0, sizeof(*model));
    model->character_count = wide ? ARCV2_LZHUF_NCHAR_WIDE :
                                    ARCV2_LZHUF_NCHAR_COMPACT;
    model->tree_size = model->character_count * 2U - 1U;
    model->root = model->tree_size - 1U;
    for (length = 3U; length <= 8U; ++length) {
        uint32_t span = 1U << (8U - length);
        uint32_t group;
        for (group = 0U; group < symbols_per_length[length - 3U]; ++group) {
            uint32_t repeat;
            for (repeat = 0U; repeat < span; ++repeat) {
                if (prefix >= 256U) return false;
                model->position_length[prefix] = (uint8_t)length;
                model->position_code[prefix] = (uint8_t)symbol;
                ++prefix;
            }
            ++symbol;
        }
    }
    if (prefix != 256U || symbol != 64U) return false;
    for (index = 0U; index < model->character_count; ++index) {
        model->frequency[index] = 1U;
        model->child[index] = index + model->tree_size;
        model->parent[index + model->tree_size] = index;
    }
    for (index = 0U; index < model->character_count - 1U; ++index) {
        uint32_t node = model->character_count + index;
        model->frequency[node] = model->frequency[index * 2U] +
                                 model->frequency[index * 2U + 1U];
        model->child[node] = index * 2U;
        model->parent[index * 2U] = node;
        model->parent[index * 2U + 1U] = node;
    }
    model->frequency[model->tree_size] = UINT32_MAX;
    model->parent[model->root] = 0U;
    return true;
}

bool xx_arcv2_lzhuf_decode_memory(const uint8_t *input, size_t input_size,
                                  uint8_t *output, size_t output_size,
                                  bool wide, size_t *written) {
    arcv2_lzhuf_model model;
    arcv2_lzhuf_bits bits;
    uint8_t dictionary[ARCV2_LZHUF_DICTIONARY_SIZE];
    size_t produced = 0U;
    uint32_t write_position;
    uint32_t maximum_length;
    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        input_size == 0U || output_size == 0U ||
        !arcv2_lzhuf_init(&model, wide))
        return false;
    xx_rt_memset(&bits, 0, sizeof(bits));
    bits.input = input;
    bits.input_size = input_size;
    xx_rt_memset(dictionary, 0x20, sizeof(dictionary));
    write_position = ARCV2_LZHUF_DICTIONARY_SIZE - model.tree_size;
    maximum_length = wide ? 60U : 32U;
    while (produced < output_size) {
        int character = arcv2_lzhuf_decode_character(&model, &bits);
        if (character < 0 || character == 256) goto done;
        if (character < 256) {
            output[produced++] = (uint8_t)character;
            dictionary[write_position] = (uint8_t)character;
            write_position = (write_position + 1U) &
                             (ARCV2_LZHUF_DICTIONARY_SIZE - 1U);
        } else {
            int encoded_position = arcv2_lzhuf_decode_position(&model, &bits);
            uint32_t length = (uint32_t)character - 254U;
            uint32_t source;
            uint32_t index;
            if (encoded_position < 0 || length < 3U ||
                length > maximum_length || length > output_size - produced)
                goto done;
            source = (write_position - (uint32_t)encoded_position - 1U) &
                     (ARCV2_LZHUF_DICTIONARY_SIZE - 1U);
            for (index = 0U; index < length; ++index) {
                uint8_t value = dictionary[(source + index) &
                                           (ARCV2_LZHUF_DICTIONARY_SIZE - 1U)];
                output[produced++] = value;
                dictionary[write_position] = value;
                write_position = (write_position + 1U) &
                                 (ARCV2_LZHUF_DICTIONARY_SIZE - 1U);
            }
        }
    }
    if (written) *written = produced;
    return true;
done:
    if (written) *written = produced;
    return false;
}

bool xx_arcv2_xor_delta_decode(uint8_t *data, size_t size, uint8_t seed) {
    size_t index;
    uint8_t previous = seed;
    if (!data && size != 0U) return false;
    for (index = 0U; index < size; ++index) {
        previous ^= data[index];
        data[index] = previous;
    }
    return true;
}
