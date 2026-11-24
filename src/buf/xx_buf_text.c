/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/buf/xx_buf_text.h"

bool xx_buf_append_fill(xx_buf_t *buf, char c, size_t count)
{
    if (!buf) {
        return false;
    }

    if (buf->failed) {
        return false;
    }

    if (!count) {
        return true;
    }

    if (count > ((size_t)-1) - 1 - buf->size) {
        buf->failed = true;
        return false;
    }

    if (!xx_buf_reserve(buf, buf->size + count)) {
        return false;
    }

    xx_rt_memset(buf->data + buf->size, (unsigned char)c, count);
    buf->size += count;
    buf->data[buf->size] = '\0';

    return true;
}

bool xx_buf_append_u64(xx_buf_t *buf, uint64_t value)
{
    char digits[24];
    size_t position = sizeof(digits);

    do {
        digits[--position] = (char)('0' + (value % 10));
        value /= 10;
    } while (value);

    return xx_buf_append(buf, digits + position, sizeof(digits) - position);
}

bool xx_buf_append_i64(xx_buf_t *buf, int64_t value)
{
    if (value < 0) {
        return xx_buf_append_char(buf, '-') && xx_buf_append_u64(buf, (uint64_t)0 - (uint64_t)value);
    }

    return xx_buf_append_u64(buf, (uint64_t)value);
}

bool xx_buf_append_hex(xx_buf_t *buf, uint64_t value, bool upper)
{
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char digits[20];
    size_t position = sizeof(digits);

    do {
        digits[--position] = alphabet[value & 15];
        value >>= 4;
    } while (value);

    return xx_buf_append(buf, digits + position, sizeof(digits) - position);
}

const char *xx_buf_cstr(const xx_buf_t *buf)
{
    return (buf && buf->data) ? buf->data : "";
}

void xx_buf_truncate(xx_buf_t *buf, size_t size)
{
    if (buf && (size < buf->size)) {
        buf->size = size;
        buf->data[size] = '\0';
    }
}
