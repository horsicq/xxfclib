/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Portable baseline encoder of the publicly documented CMPSC index/dictionary
 * representation. Training builds a small static LZ78 phrase dictionary; the
 * second pass codes longest matches against that frozen dictionary.
 */
#include "xxfclib/algo/cmpsc/xx_cmpsc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define CMPSC_ENC_ENTRIES 512U
#define CMPSC_ENC_WIDTH 9U
#define CMPSC_ENC_DICTIONARY_SIZE (CMPSC_ENC_ENTRIES * 8U)
#define CMPSC_ENC_HEADER_SIZE 6U
#define CMPSC_ENC_NO_NODE UINT16_MAX
#define CMPSC_ENC_MAX_PHRASE 256U

typedef struct cmpsc_encoder {
    uint16_t first_child[CMPSC_ENC_ENTRIES];
    uint16_t next_sibling[CMPSC_ENC_ENTRIES];
    uint16_t length[CMPSC_ENC_ENTRIES];
    uint8_t suffix[CMPSC_ENC_ENTRIES];
    uint8_t dictionary[CMPSC_ENC_DICTIONARY_SIZE];
    uint8_t compressed_dictionary[CMPSC_ENC_DICTIONARY_SIZE];
    unsigned nodes;
} cmpsc_encoder;

size_t xx_cmpsc_zip_encode_bound(size_t input_size)
{
    const size_t overhead = CMPSC_ENC_HEADER_SIZE + CMPSC_ENC_DICTIONARY_SIZE;
    size_t extra = input_size / 8U + (input_size % 8U != 0U);
    if (input_size > SIZE_MAX - overhead || extra > SIZE_MAX - overhead - input_size) return 0U;
    return overhead + input_size + extra;
}

static unsigned cmpsc_encoder_find(const cmpsc_encoder *state, unsigned parent, uint8_t suffix)
{
    unsigned node = state->first_child[parent];
    while (node != CMPSC_ENC_NO_NODE) {
        if (state->suffix[node] == suffix) return node;
        node = state->next_sibling[node];
    }
    return CMPSC_ENC_NO_NODE;
}

static void cmpsc_encoder_init(cmpsc_encoder *state)
{
    unsigned node;
    xx_rt_memset(state, 0, sizeof(*state));
    for (node = 0U; node < CMPSC_ENC_ENTRIES; ++node) {
        state->first_child[node] = CMPSC_ENC_NO_NODE;
        state->next_sibling[node] = CMPSC_ENC_NO_NODE;
        if (node < 256U) {
            state->length[node] = 1U;
            state->suffix[node] = (uint8_t)node;
            state->dictionary[node * 8U] = 1U;
            state->dictionary[node * 8U + 1U] = (uint8_t)node;
        }
    }
    state->nodes = 256U;
}

static bool cmpsc_encoder_train(cmpsc_encoder *state, const uint8_t *input, size_t input_size, xx_pd_struct *pd)
{
    size_t position = 0U;
    while (position < input_size && state->nodes < CMPSC_ENC_ENTRIES) {
        unsigned current = input[position++];
        for (;;) {
            unsigned next;
            if (pd && xx_pd_is_stopped(pd)) return false;
            if (position == input_size || state->length[current] >= CMPSC_ENC_MAX_PHRASE) break;
            next = cmpsc_encoder_find(state, current, input[position]);
            if (next == CMPSC_ENC_NO_NODE) {
                uint8_t *entry;
                unsigned node = state->nodes++;
                state->length[node] = state->length[current] + 1U;
                state->suffix[node] = input[position++];
                state->next_sibling[node] = state->first_child[current];
                state->first_child[current] = (uint16_t)node;
                /* Preceded entry: PSL=1, PPTR=current, EC=new byte,
                 * OFST=parent phrase length. All reserved/unused bytes=0. */
                entry = state->dictionary + node * 8U;
                entry[0] = (uint8_t)(0x20U | (current >> 8U));
                entry[1] = (uint8_t)current;
                entry[2] = state->suffix[node];
                entry[7] = (uint8_t)state->length[current];
                break;
            }
            current = next;
            ++position;
        }
    }
    return !pd || !xx_pd_is_stopped(pd);
}

bool xx_cmpsc_zip_encode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_capacity, size_t *written, xx_pd_struct *pd)
{
    cmpsc_encoder *state = NULL;
    const uint8_t *dictionary;
    size_t dictionary_size = CMPSC_ENC_DICTIONARY_SIZE;
    size_t compressed_size = 0U, source_position = 0U, position;
    uint32_t bits = 0U;
    unsigned bit_count = 0U;
    bool success = false;
    if (written) *written = 0U;
    if ((!input && input_size != 0U) || !output || output_capacity < CMPSC_ENC_HEADER_SIZE || xx_cmpsc_zip_encode_bound(input_size) == 0U || (pd && xx_pd_is_stopped(pd)))
        return false;
    state = (cmpsc_encoder *)xx_mem_alloc(sizeof(*state));
    if (!state) return false;
    cmpsc_encoder_init(state);
    if (!cmpsc_encoder_train(state, input, input_size, pd)) goto cleanup;
    dictionary = state->dictionary;
    /* The bounded attempt may fail for an incompressible dictionary. Raw
     * dictionary storage is always the valid fallback within encode_bound. */
    if (xx_deflate_compress_memory(state->dictionary, CMPSC_ENC_DICTIONARY_SIZE, state->compressed_dictionary, sizeof(state->compressed_dictionary), &compressed_size,
                                   XX_DEFLATE_LEVEL_DEFAULT, false) &&
        compressed_size != 0U && compressed_size < dictionary_size) {
        dictionary = state->compressed_dictionary;
        dictionary_size = compressed_size;
    }
    if ((pd && xx_pd_is_stopped(pd)) || dictionary_size > output_capacity - CMPSC_ENC_HEADER_SIZE) goto cleanup;
    output[0] = 1U;
    output[1] = (uint8_t)(0x80U | CMPSC_ENC_WIDTH | (dictionary == state->compressed_dictionary ? 0x40U : 0U));
    output[2] = (uint8_t)dictionary_size;
    output[3] = (uint8_t)(dictionary_size >> 8U);
    output[4] = (uint8_t)(dictionary_size >> 16U);
    output[5] = (uint8_t)(dictionary_size >> 24U);
    xx_rt_memcpy(output + CMPSC_ENC_HEADER_SIZE, dictionary, dictionary_size);
    position = CMPSC_ENC_HEADER_SIZE + dictionary_size;
    while (source_position < input_size) {
        unsigned current = input[source_position++];
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        while (source_position < input_size) {
            unsigned next = cmpsc_encoder_find(state, current, input[source_position]);
            if (next == CMPSC_ENC_NO_NODE) break;
            current = next;
            ++source_position;
        }
        bits = (bits << CMPSC_ENC_WIDTH) | current;
        bit_count += CMPSC_ENC_WIDTH;
        while (bit_count >= 8U) {
            if (position == output_capacity) goto cleanup;
            bit_count -= 8U;
            output[position++] = (uint8_t)(bits >> bit_count);
        }
        bits &= (1U << bit_count) - 1U;
    }
    if (bit_count != 0U) {
        if (position == output_capacity) goto cleanup;
        output[position++] = (uint8_t)(bits << (8U - bit_count));
    }
    if (pd && xx_pd_is_stopped(pd)) goto cleanup;
    if (written) *written = position;
    success = true;
cleanup:
    if (state) {
        xx_mem_zero(state, sizeof(*state));
        xx_mem_free(state);
    }
    return success;
}
