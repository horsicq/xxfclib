/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TARX v1 is a QNX transport that encrypts an otherwise ordinary POSIX TAR
 * with a stateful 32-bit byte cipher.  The first encrypted 100-byte TAR
 * header contains enough structure to recover the stream seed deterministically.
 */

#include "xxfclib/algo/tarx/xx_tarx.h"

#include <string.h>

#define XX_TARX1_KEY_WINDOW_SIZE 100U
#define XX_TARX1_ANCHOR_OFFSET 95U
#define XX_TARX1_CHECK_OFFSET 99U
#define XX_TARX1_CHUNK_SIZE 4096U
#define XX_TARX1_CRC_POLYNOMIAL UINT32_C(0xedb88320)

typedef struct xx_tarx1_tables_s {
    uint32_t transform[256];
    uint8_t inverse_top_byte[256];
} xx_tarx1_tables;

static void xx_tarx1_make_tables(xx_tarx1_tables *tables) {
    unsigned index;
    if (!tables) return;
    for (index = 0U; index < 256U; ++index) {
        uint32_t value = index;
        unsigned bit;
        for (bit = 0U; bit < 8U; ++bit) {
            value = (value >> 1U) ^
                    ((value & 1U) != 0U ? XX_TARX1_CRC_POLYNOMIAL : 0U);
        }
        tables->transform[index] = value;
    }
    for (index = 0U; index < 256U; ++index) {
        tables->inverse_top_byte[tables->transform[index] >> 24U] =
            (uint8_t)index;
    }
}

static uint32_t xx_tarx1_read_u32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

/* Reverse the one-byte carry fold used while the seed is recovered. */
static uint8_t xx_tarx1_reverse_fold(uint32_t candidate_low,
                                     uint32_t table_index,
                                     uint32_t encrypted_byte) {
    uint32_t value = (candidate_low ^ table_index ^ encrypted_byte) & 255U;
    uint32_t carry = 0U;
    unsigned bit;
    for (bit = 1U; bit <= 8U; ++bit) {
        uint32_t width = (UINT32_C(1) << bit) & 255U;
        uint32_t mask = (width - 1U) & 255U;
        carry = value ^ carry;
        value = carry;
        carry = (carry + (candidate_low & mask) +
                 ((encrypted_byte & mask) ^ carry)) & width;
    }
    return (uint8_t)value;
}

static bool xx_tarx1_recover_seed(const uint8_t *window, uint32_t *seed) {
    xx_tarx1_tables tables;
    uint32_t anchor;
    uint32_t found_seed = 0U;
    unsigned matches = 0U;
    unsigned candidate;
    if (!window || !seed) return false;
    xx_tarx1_make_tables(&tables);
    anchor = xx_tarx1_read_u32le(window + XX_TARX1_ANCHOR_OFFSET);
    for (candidate = 0U; candidate < 256U; ++candidate) {
        uint32_t state_words[4];
        uint32_t signature;
        uint32_t state;
        int position;
        unsigned index;
        for (index = 0U; index < 4U; ++index) {
            uint32_t value = (anchor >> (index * 8U)) & 255U;
            state_words[index] = tables.transform[
                ((value + candidate) ^ value) & 255U];
        }
        signature = ((state_words[0] >> 24U) ^ (state_words[1] >> 16U) ^
                     (state_words[2] >> 8U) ^ state_words[3]) & 255U;
        if (signature != window[XX_TARX1_CHECK_OFFSET]) continue;
        state = (state_words[0] << 8U) ^ (state_words[1] << 16U) ^
                (state_words[2] << 24U) ^ anchor;
        for (position = (int)XX_TARX1_ANCHOR_OFFSET - 1; position >= 0;
             --position) {
            uint8_t index_byte = tables.inverse_top_byte[state >> 24U];
            uint8_t folded = xx_tarx1_reverse_fold(candidate, index_byte,
                                                     window[position]);
            state = ((state ^ tables.transform[index_byte]) << 8U) + folded;
        }
        if ((state & 255U) == candidate) {
            found_seed = state;
            ++matches;
        }
    }
    if (matches != 1U) return false;
    *seed = found_seed;
    return true;
}

static bool xx_tarx1_read_exact_at(xx_io_device *device, int64_t offset,
                                   void *data, size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_tarx1_write_all(xx_io_device *device, const uint8_t *data,
                               size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

bool xx_tarx1_has_header(const uint8_t *data, size_t size) {
    return data && size >= XX_TARX1_MAGIC_SIZE &&
           data[0] == (uint8_t)'T' && data[1] == (uint8_t)'a' &&
           data[2] == (uint8_t)'R' && data[3] == (uint8_t)'x';
}

bool xx_tarx1_decode_device(xx_io_device *source, int64_t source_offset,
                            int64_t source_size, xx_io_device *destination,
                            int64_t *output_size, xx_pd_struct *pd) {
    uint8_t magic[XX_TARX1_MAGIC_SIZE];
    uint8_t key_window[XX_TARX1_KEY_WINDOW_SIZE];
    uint8_t input[XX_TARX1_CHUNK_SIZE];
    uint8_t output[XX_TARX1_CHUNK_SIZE];
    xx_tarx1_tables tables;
    int64_t total_size;
    int64_t cipher_offset;
    int64_t cipher_size;
    int64_t remaining;
    uint32_t seed;
    uint32_t state;

    if (output_size) *output_size = -1;
    if (!source || !destination || source_offset < 0 ||
        source_size < (int64_t)(XX_TARX1_MAGIC_SIZE +
                                 XX_TARX1_KEY_WINDOW_SIZE) ||
        (source_size & 511) != (int64_t)XX_TARX1_MAGIC_SIZE ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(source);
    if (total_size < source_offset || source_size > total_size - source_offset ||
        !xx_tarx1_read_exact_at(source, source_offset, magic, sizeof(magic)) ||
        !xx_tarx1_has_header(magic, sizeof(magic))) {
        return false;
    }
    cipher_offset = source_offset + (int64_t)sizeof(magic);
    cipher_size = source_size - (int64_t)sizeof(magic);
    if (cipher_size <= 0 ||
        !xx_tarx1_read_exact_at(source, cipher_offset, key_window,
                                 sizeof(key_window)) ||
        !xx_tarx1_recover_seed(key_window, &seed) ||
        xx_io_seek64(source, cipher_offset, SEEK_SET) != 0) {
        return false;
    }
    xx_tarx1_make_tables(&tables);
    state = seed;
    remaining = cipher_size;
    while (remaining != 0) {
        size_t requested = remaining > (int64_t)sizeof(input)
                               ? sizeof(input)
                               : (size_t)remaining;
        ssize_t amount = xx_io_read(source, input, requested);
        size_t index;
        if ((pd && xx_pd_is_stopped(pd)) || amount <= 0 ||
            (size_t)amount > requested) {
            return false;
        }
        for (index = 0U; index < (size_t)amount; ++index) {
            uint32_t plain = input[index] ^ (state & 255U);
            uint32_t table_index = ((state + seed + plain) ^ state) & 255U;
            output[index] = (uint8_t)plain;
            state = (state >> 8U) ^ tables.transform[table_index];
        }
        if (!xx_tarx1_write_all(destination, output, (size_t)amount)) {
            return false;
        }
        remaining -= amount;
    }
    if (output_size) *output_size = cipher_size;
    return true;
}
