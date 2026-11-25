/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/sig/xx_sig.h"
#include "xxfclib/strings/xx_string.h"

char *xx_sig_normalize(const char *text)
{
    int inside_quote = 0;
    size_t i;

    /* The general notation accepts partial literals. Complete signature
     * validation follows XBinary::convertSignature and rejects them. */
    if (text) {
        for (i = 0; text[i]; i++) {
            if (text[i] == '\'') inside_quote = !inside_quote;
        }
    }
    if (inside_quote) return xx_str_dup("");
    return xx_data_sig_normalize(text);
}

int xx_sig_valid(const char *normalized, xx_data_signature *signature, int *parser_error)
{
    int valid = xx_data_signature_parse(signature, normalized) ? 1 : 0;
    size_t i;
    int j;

    if (parser_error) *parser_error = !valid;
    if (!normalized || !signature) return 0;

    /* The general parser retains partial records. A complete signature must
     * not match a successfully parsed prefix of an invalid record. */
    for (i = 0; normalized[i]; i++) {
        char c = normalized[i];

        if (c == '.' || c == '*' || c == '$') {
            size_t start = i;
            while (normalized[i + 1] == c) i++;
            if ((i - start + 1) % 2) valid = 0;
        } else if (c == '+') {
            while (normalized[i + 1] == '+') i++;
            if (!((normalized[i + 1] >= '0' && normalized[i + 1] <= '9') || (normalized[i + 1] >= 'a' && normalized[i + 1] <= 'f'))) valid = 0;
        } else if (c == '%') {
            if (normalized[i + 1] != '%' && normalized[i + 1] != '&') {
                valid = 0;
            } else {
                i++;
            }
        } else if (c == '!' || c == '_') {
            if (normalized[i + 1] != '%') {
                valid = 0;
            } else {
                i++;
            }
        } else if (c == '#') {
            size_t address_chars = 0;
            int in_base = 0;
            size_t k = i;

            for (; normalized[k]; k++) {
                char address_char = normalized[k];
                if (address_char == '#') {
                    address_chars++;
                } else if (address_char == '[') {
                    if (in_base) valid = 0;
                    in_base = 1;
                } else if (address_char == ']') {
                    if (!in_base) valid = 0;
                    in_base = 0;
                } else if (in_base) {
                    if (!((address_char >= '0' && address_char <= '9') || (address_char >= 'a' && address_char <= 'f'))) valid = 0;
                } else {
                    break;
                }
            }
            if (in_base || address_chars % 2) valid = 0;
            i = k - 1;
        } else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
            size_t start = i;
            while ((normalized[i + 1] >= '0' && normalized[i + 1] <= '9') || (normalized[i + 1] >= 'a' && normalized[i + 1] <= 'f')) i++;
            if ((i - start + 1) % 2) {
                valid = 0;
                if (parser_error) *parser_error = 1;
            }
        } else {
            valid = 0;
        }
    }
    for (j = 0; j < signature->count; j++) {
        const xx_data_sig_record *record = &signature->records[j];
        if ((record->kind == XX_DATA_SIG_REL_OFFSET || record->kind == XX_DATA_SIG_ADDRESS) && record->address_size != 1 && record->address_size != 2 &&
            record->address_size != 4 && record->address_size != 8)
            valid = 0;
    }
    return valid;
}

int xx_sig_compare_nibbles(xx_io_device *device, int64_t offset, const char *normalized, size_t length)
{
    static const char digits[] = "0123456789abcdef";
    size_t total = length / 2 + length % 2, done = 0;

    if (length && !normalized) return 0;
    while (done < total) {
        size_t count = total - done, i;
        const unsigned char *view = xx_io_buffer_view(device, offset + (int64_t)done, &count);
        if (!view) return 0;
        for (i = 0; i < count; ++i) {
            size_t position = (done + i) * 2;
            if (normalized[position] != '.' && normalized[position] != digits[view[i] >> 4]) return 0;
            if (position + 1 < length && normalized[position + 1] != '.' && normalized[position + 1] != digits[view[i] & 15]) return 0;
        }
        done += count;
    }
    return 1;
}
