/* Independent LZP reconstruction from numeric interoperability facts.
 * SPDX-License-Identifier: MIT. */
#include "dgca_lzp.h"
#include "dgca_range.h"
#include <string.h>
#include <limits.h>

typedef struct dg_lzp_state {
    const dg_callbacks *callbacks;
    unsigned char *output;
    uint32_t *dictionary;
    size_t position;
    uint32_t context;
} dg_lzp_state;

static int dg_lzp_emit(dg_lzp_state *state, unsigned char value)
{
    const dg_callbacks *cb = state->callbacks;
    if ((state->position & 4095u) == 0 && cb->cancelled && cb->cancelled(cb->opaque))
        return 0;
    state->dictionary[state->context] = (uint32_t)state->position;
    state->output[state->position++] = value;
    state->context = ((state->context & 255u) << 8) | value;
    return 1;
}

dg_status dg_lzp_expand(const dg_callbacks *cb, const unsigned char *literals,
                  size_t literal_size, const unsigned char *length_data,
                  size_t length_size, unsigned char *output,
                  size_t output_size, uint32_t params)
{
    dg_range range;
    dg_lzp_state state;
    uint32_t literal_probabilities[32], match_probabilities[32];
    uint32_t literal_count, match_extra, minimum_match;
    unsigned shift = params >> 28;
    unsigned minimum_exponent = (params >> 24) & 15u;
    size_t literal_position = 0, count, source, i;
    dg_status status = DG_FORMAT;

    if (!cb || !cb->allocate || !cb->release || output_size > UINT32_MAX ||
        (literal_size && !literals) || (output_size && !output) ||
        shift > 15)
        return DG_FORMAT;
    if (!output_size)
        return literal_size ? DG_FORMAT :
            ((cb->cancelled && cb->cancelled(cb->opaque)) ? DG_CANCELLED : DG_OK);
    if (cb->cancelled && cb->cancelled(cb->opaque))
        return DG_CANCELLED;
    if (!dg_range_init(&range, length_data, length_size))
        return DG_FORMAT;
    minimum_match = 1u << minimum_exponent;
    state.callbacks = cb;
    state.output = output;
    state.position = 0;
    state.context = 0;
    state.dictionary = (uint32_t *)cb->allocate(cb->opaque, 65536u * sizeof(uint32_t));
    if (!state.dictionary)
        return DG_MEMORY;
    memset(state.dictionary, 0xff, 65536u * sizeof(uint32_t));
    for (i = 0; i < 32; ++i) {
        literal_probabilities[i] = 2048;
        match_probabilities[i] = 2048;
    }

    while (state.position < output_size) {
        if (!dg_range_integer(&range, literal_probabilities, shift, &literal_count))
            goto finished;
        count = literal_count;
        if (count > output_size - state.position || count > literal_size - literal_position)
            goto finished;
        for (i = 0; i < count; ++i)
            if (!dg_lzp_emit(&state, literals[literal_position++])) {
                status = DG_CANCELLED;
                goto finished;
            }
        if (state.position == output_size)
            break;

        if (!dg_range_integer(&range, match_probabilities, shift, &match_extra))
            goto finished;
        if (minimum_match > output_size - state.position ||
            match_extra > output_size - state.position - minimum_match)
            goto finished;
        count = (size_t)minimum_match + match_extra;
        source = state.dictionary[state.context];
        if (source >= state.position)
            goto finished;
        for (i = 0; i < count; ++i) {
            unsigned char value;
            if (source >= state.position)
                goto finished;
            value = output[source++];
            if (!dg_lzp_emit(&state, value)) {
                status = DG_CANCELLED;
                goto finished;
            }
        }
    }
    status = literal_position == literal_size ? DG_OK : DG_FORMAT;
    if (status == DG_OK && cb->cancelled && cb->cancelled(cb->opaque))
        status = DG_CANCELLED;
finished:
    cb->release(cb->opaque, state.dictionary);
    return status;
}
