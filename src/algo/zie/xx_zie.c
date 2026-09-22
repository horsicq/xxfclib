/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZIE / ProtectIt-2 encrypted ZIP wrapper.  Ported from the reference decoder
 * XArchive/Algos/xziedecoder.cpp; see the header for the layout, the key
 * derivation and the phase rule.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/zie/xx_zie.h"

/* The two 16-byte tables in the reference's data section XOR to this. */
static const char ZIE_CONSTANT[17] = "ProtectIt/2 OS/2";
static const char ZIE_ZIP_PLAIN[4] = {'P', 'K', 0x03, 0x04};

/* The reference's 8.3-ish file name sanity check, ported branch for branch.
 * It never steps past the 13-byte field: the length counter is decremented in
 * lock step with the cursor and the function returns before the read that
 * would leave the field.
 *
 * Two of its branches look wrong and are deliberate.  (1) The first loop gives
 * up after eight name characters: it breaks with i == 10 having already
 * established that the current byte is not '.', so the !dot_seen re-check that
 * follows can only fail -- a name whose stem runs past eight characters with
 * no dot is rejected.  (2) The extension loop returns true for a field that
 * simply runs out after exactly three extension characters (i == 4), which is
 * how a name that fills the field without a NUL is accepted. */
static bool zie_name_ok(const uint8_t *name, int32_t size) {
    int32_t n;
    int32_t p;
    int32_t i;
    bool dot_seen;

    if (!name || (size < 1)) return false;
    n = size;
    p = 0;
    if (!((name[0] >= 0x20U) && (name[0] != 0x2EU))) return false;

    i = 2;
    dot_seen = false;
    for (;;) {
        p++;
        n--;
        if (n < 1) return false;
        if (p >= size) return false;
        if (name[p] == 0U) return true;
        if (name[p] < 0x20U) return false;
        if (name[p] == 0x2EU) {
            dot_seen = true;
            break;
        }
        i++;
        if (i == 10) break;
    }
    if (!dot_seen) {
        if (p >= size) return false;
        if (name[p] != 0x2EU) return false;
    }

    i = 1;
    for (;;) {
        p++;
        n--;
        if (n < 1) return (i == 4);
        if (p >= size) return false;
        if (name[p] == 0U) return true;
        if (name[p] < 0x20U) return false;
        i++;
        if (i == 5) return false;
    }
}

bool xx_zie_is_valid_header(const uint8_t *header, size_t header_size) {
    if (!header || (header_size < XX_ZIE_HEADER_SIZE)) return false;
    if ((header[0] != 'P') || (header[1] != 'I') || (header[2] != 'T') ||
        (header[3] != '2')) {
        return false;
    }

    return zie_name_ok(header + XX_ZIE_NAME_OFFSET, (int32_t)XX_ZIE_NAME_SIZE);
}

bool xx_zie_file_name(const uint8_t *header, size_t header_size, char *output,
                      size_t output_size, size_t *length) {
    size_t i;
    if (length) *length = 0U;
    if (!header || (header_size < XX_ZIE_HEADER_SIZE)) return false;
    if (!output || (output_size < (XX_ZIE_NAME_SIZE + 1U))) return false;

    for (i = 0U; i < XX_ZIE_NAME_SIZE; ++i) {
        uint8_t c = header[XX_ZIE_NAME_OFFSET + i];
        if (c == 0U) break;
        output[i] = (char)c;
    }
    output[i] = '\0';
    if (length) *length = i;

    return true;
}

bool xx_zie_resolve_method(const uint8_t *header, size_t header_size,
                           const uint8_t *probe, size_t probe_size,
                           uint64_t payload_size, xx_zie_method *method) {
    xx_zie_method work;
    uint64_t size;
    uint32_t length_base;
    uint32_t candidates[2];
    int32_t index;
    int32_t i;

    if (!method) return false;
    if (!xx_zie_is_valid_header(header, header_size)) return false;
    if ((payload_size < 4U) || !probe || (probe_size < 4U)) return false;

    xx_rt_memset(&work, 0, sizeof(work));
    for (i = 0; i < 16; ++i) {
        work.key[i] = (uint8_t)(header[4 + i] ^ (uint8_t)ZIE_CONSTANT[i]);
    }

    size = payload_size & ~(uint64_t)3U;
    if (size < 4U) return false;

    length_base = (uint32_t)((0x10U - (uint32_t)(size & 0xFU)) & 0xFU);
    candidates[0] = length_base;
    candidates[1] = 0U;

    for (index = 0; index < 2; ++index) {
        const uint32_t base = candidates[index];
        /* Position 0..3 always sits inside the first 0xC0000 bytes, so the
         * large-payload phase shift applies to the probe whenever it applies
         * at all. */
        const uint32_t rot = (size >= (uint64_t)XX_ZIE_LARGE_PAYLOAD_SIZE)
                                 ? ((base + 8U) & 0xFU)
                                 : base;
        bool match = true;
        for (i = 0; i < 4; ++i) {
            const uint8_t plain =
                (uint8_t)(probe[i] ^ work.key[((uint32_t)i + rot) & 0xFU]);
            if (plain != (uint8_t)ZIE_ZIP_PLAIN[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            work.base = base;
            work.recovered = (index != 0);
            *method = work;
            return true;
        }
        if ((index == 0) && (length_base == 0U)) break;
    }

    return false;
}

static void zie_xor_range(const uint8_t *input, uint8_t *output,
                          const uint8_t *key, uint32_t base, size_t from,
                          size_t to) {
    size_t position;
    for (position = from; position < to; ++position) {
        output[position] =
            (uint8_t)(input[position] ^ key[(position + base) & 0xFU]);
    }
}

bool xx_zie_decode_method(const uint8_t *input, size_t input_size,
                          const xx_zie_method *method, uint8_t *output,
                          size_t output_size, size_t *written) {
    size_t size;
    uint32_t base;
    size_t position;

    if (written) *written = 0U;
    if (!written || !method) return false;
    if (!input || (input_size == 0U)) return false;
    if (!output || (output_size < input_size)) return false;

    size = input_size & ~(size_t)3U;
    base = method->base & 0xFU;

    if (size >= XX_ZIE_LARGE_PAYLOAD_SIZE) {
        zie_xor_range(input, output, method->key, (base + 8U) & 0xFU, 0U,
                      XX_ZIE_LARGE_PAYLOAD_SIZE);
        zie_xor_range(input, output, method->key, base,
                      XX_ZIE_LARGE_PAYLOAD_SIZE, size);
    } else {
        zie_xor_range(input, output, method->key, base, 0U, size);
    }

    /* The tail bytes (payload % 4) are never encrypted. */
    for (position = size; position < input_size; ++position) {
        output[position] = input[position];
    }

    *written = input_size;

    return true;
}

bool xx_zie_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written) {
    xx_zie_method method;
    size_t payload_size;

    if (written) *written = 0U;
    if (!written) return false;
    if (!input || (input_size <= XX_ZIE_HEADER_SIZE)) return false;

    payload_size = input_size - XX_ZIE_HEADER_SIZE;
    if (!xx_zie_resolve_method(input, input_size, input + XX_ZIE_HEADER_SIZE,
                               payload_size, (uint64_t)payload_size,
                               &method)) {
        return false;
    }

    return xx_zie_decode_method(input + XX_ZIE_HEADER_SIZE, payload_size,
                                &method, output, output_size, written);
}
