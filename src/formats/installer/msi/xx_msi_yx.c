/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <xxfclib/formats/installer/msi/xx_msi_yx.h>

#include <stdlib.h>
#include <string.h>

XXFC_API bool xx_msi_yx_name_is(const xx_olecf_yx_stream *entry,
                                const char *expected)
{
    const uint8_t *name;
    size_t length, expected_length, i;
    if (!entry || !expected) return false;
    name = (const uint8_t *)entry->name;
    length = entry->name_length;
    expected_length = strlen(expected);
    while (length && *name < 0x20) {
        ++name;
        --length;
    }
    if (length != expected_length) return false;
    for (i = 0; i < length; ++i) {
        uint8_t left = name[i];
        uint8_t right = (uint8_t)expected[i];
        if (left >= 'A' && left <= 'Z') left = (uint8_t)(left + 32);
        if (right >= 'A' && right <= 'Z') right = (uint8_t)(right + 32);
        if (left != right) return false;
    }
    return true;
}

XXFC_API bool xx_msi_yx_hash_input(const uint8_t *data, size_t size,
                                   xx_msi_yx_update_fn update, void *state)
{
    static const uint8_t zeroes[20] = {0};
    xx_olecf_yx_document *document;
    size_t sector_size, count, i;
    if (!update || (!data && size)) return false;
    document = xx_olecf_yx_open(data, size);
    if (!document) return false;
    sector_size = xx_olecf_yx_sector_size(document);
    count = xx_olecf_yx_entry_count(document);
    if (sector_size && size >= sector_size) {
        update(state, data, 8);
        update(state, zeroes, sizeof(zeroes));
        update(state, data + 28, sector_size - 28);
        for (i = 0; i < count; ++i) {
            const xx_olecf_yx_stream *entry =
                xx_olecf_yx_entry(document, i);
            const uint8_t *bytes;
            size_t length;
            uint8_t *owner;
            if (!entry || xx_msi_yx_name_is(entry, "DigitalSignature") ||
                xx_msi_yx_name_is(entry, "MsiDigitalSignatureEx"))
                continue;
            if (!xx_olecf_yx_entry_data(document, i, &bytes, &length,
                                        &owner))
                continue;
            if (length) update(state, bytes, length);
            free(owner);
        }
    }
    xx_olecf_yx_close(document);
    return true;
}
