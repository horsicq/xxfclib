/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native implementation from PKWARE APPNOTE 5.17 and IBM CSRYCMPD dictionary
 * mappings / z/Architecture Principles of Operation, CMPSC expansion process.
 * No Hercules or other third-party implementation source is incorporated.
 */
#include "xxfclib/algo/cmpsc/xx_cmpsc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/data/xx_data.h"

#define CMPSC_HEADER_SIZE 6U
#define CMPSC_ENTRY_SIZE 8U
#define CMPSC_MAX_SYMBOL_SIZE 260U

typedef struct cmpsc_bits {
    const uint8_t *input;
    size_t size;
    size_t offset;
    uint32_t bits;
    unsigned count;
} cmpsc_bits;

static bool cmpsc_get_symbol(cmpsc_bits *reader, unsigned width, unsigned *symbol)
{
    while (reader->count < width) {
        if (reader->offset == reader->size) return false;
        reader->bits = (reader->bits << 8U) | reader->input[reader->offset++];
        reader->count += 8U;
    }
    reader->count -= width;
    *symbol = (reader->bits >> reader->count) & ((1U << width) - 1U);
    /* A maximum of seven unused bits is retained after each symbol. */
    reader->bits &= (1U << reader->count) - 1U;
    return true;
}

/* Materialize one symbol independently of destination data. IBM permits
 * malformed dictionaries to have unpredictable results. We require a complete
 * nonoverlapping partition of the symbol and reject cyclic/out-of-range chains
 * rather than exposing uninitialized bytes or overwriting adjacent output.
 */
static bool cmpsc_expand_symbol(const uint8_t *dictionary, size_t entries, unsigned symbol, uint8_t *expanded, size_t *length)
{
    uint8_t covered[CMPSC_MAX_SYMBOL_SIZE];
    size_t symbol_size = 0U, remaining = CMPSC_MAX_SYMBOL_SIZE;
    unsigned traversed = 0U;
    if (symbol < 256U) {
        expanded[0] = (uint8_t)symbol;
        *length = 1U;
        return true;
    }
    xx_rt_memset(covered, 0, sizeof(covered));
    for (;;) {
        const uint8_t *entry;
        unsigned partial, complete;
        size_t offset, index;
        if (symbol >= entries || ++traversed > CMPSC_MAX_SYMBOL_SIZE) return false;
        entry = dictionary + (size_t)symbol * CMPSC_ENTRY_SIZE;
        partial = entry[0] >> 5U;
        if (partial == 0U) {
            complete = entry[0] & 7U;
            /* IBM reserves bits 3 and 4 in an unpreceded entry. */
            if (complete == 0U || (entry[0] & 0x18U) != 0U) return false;
            if (symbol_size == 0U) symbol_size = complete;
            if (complete > symbol_size) return false;
            for (index = 0U; index < complete; ++index) {
                if (covered[index]) return false;
                expanded[index] = entry[index + 1U];
                covered[index] = 1U;
            }
            break;
        }
        if (partial > 5U) return false;
        offset = entry[7];
        if (symbol_size == 0U) symbol_size = offset + partial;
        if (symbol_size > CMPSC_MAX_SYMBOL_SIZE || offset > symbol_size || partial > symbol_size - offset || partial > remaining) return false;
        remaining -= partial;
        for (index = 0U; index < partial; ++index) {
            if (covered[offset + index]) return false;
            expanded[offset + index] = entry[index + 2U];
            covered[offset + index] = 1U;
        }
        symbol = ((unsigned)(entry[0] & 31U) << 8U) | entry[1];
        if (symbol < 256U) {
            if (covered[0]) return false;
            expanded[0] = (uint8_t)symbol;
            covered[0] = 1U;
            break;
        }
    }
    for (remaining = 0U; remaining < symbol_size; ++remaining) {
        if (!covered[remaining]) return false;
    }
    *length = symbol_size;
    return true;
}

bool xx_cmpsc_zip_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written, xx_pd_struct *pd)
{
    const uint8_t *dictionary;
    uint8_t *allocated_dictionary = NULL;
    uint8_t expanded[CMPSC_MAX_SYMBOL_SIZE];
    cmpsc_bits reader;
    size_t stored_dictionary_size, dictionary_size, entries, produced = 0U;
    unsigned flags, width;
    bool success = false;
    if (written) *written = 0U;
    if (!input || input_size < CMPSC_HEADER_SIZE || (!output && output_size != 0U) || (pd && xx_pd_is_stopped(pd))) return false;
    flags = input[1];
    width = flags & 15U;
    if (input[0] != 1U || (flags & 0x80U) == 0U || (flags & 0x30U) != 0U || width < 9U || width > 13U) return false;
    entries = (size_t)1U << width;
    dictionary_size = entries * CMPSC_ENTRY_SIZE;
    stored_dictionary_size = xx_data_get_u32(input + 2U, 4, 0, false);
    if (stored_dictionary_size == 0U || stored_dictionary_size > input_size - CMPSC_HEADER_SIZE) return false;
    dictionary = input + CMPSC_HEADER_SIZE;
    if ((flags & 0x40U) != 0U) {
        xx_io_device *destination;
        size_t consumed = 0U;
        allocated_dictionary = (uint8_t *)xx_mem_alloc(dictionary_size);
        if (!allocated_dictionary) return false;
        destination = xx_io_mem_open(allocated_dictionary, dictionary_size);
        if (!destination) goto cleanup;
        success = xx_deflate_unpack_memory_to_device_ex(dictionary, stored_dictionary_size, destination, &consumed, false, pd) && consumed == stored_dictionary_size &&
                  xx_io_tell(destination) == (int64_t)dictionary_size;
        xx_io_close(destination);
        if (!success) goto cleanup;
        dictionary = allocated_dictionary;
    } else if (stored_dictionary_size != dictionary_size) {
        return false;
    }
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.input = input + CMPSC_HEADER_SIZE + stored_dictionary_size;
    reader.size = input_size - CMPSC_HEADER_SIZE - stored_dictionary_size;
    success = false;
    while (produced < output_size) {
        unsigned symbol;
        size_t length = 0U;
        if ((pd && xx_pd_is_stopped(pd)) || !cmpsc_get_symbol(&reader, width, &symbol) || !cmpsc_expand_symbol(dictionary, entries, symbol, expanded, &length) ||
            length > output_size - produced)
            goto cleanup;
        xx_rt_memcpy(output + produced, expanded, length);
        produced += length;
    }
    /* CMPSC has no end marker. The ZIP size ends the stream. Only the unused
     * remainder of its last byte may follow, never another complete byte. */
    if (reader.offset != reader.size || (pd && xx_pd_is_stopped(pd))) goto cleanup;
    if (written) *written = produced;
    success = true;
cleanup:
    xx_mem_free(allocated_dictionary);
    return success;
}
