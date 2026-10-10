/* SPDX-License-Identifier: MIT. Checked primitives for the shared format. */
#ifndef XX_BINARY_RECORDS_H
#define XX_BINARY_RECORDS_H
#include "xx_numeric_values.h"
#include "xxfclib/data/xx_data.h"
static bool record_span(uint64_t at, uint64_t n, uint64_t end) { return at <= end && n <= end - at; }
static bool record_take(Abstractformat *f, uint64_t *at, uint64_t end, void *p, size_t n, xx_pd_struct *pd) {
    if ((pd && xx_pd_is_stopped(pd)) || !record_span(*at, n, end) || !pm_read(f, (int64_t)*at, p, n))
        return false;
    *at += n;
    return true;
}
static bool record_word(Abstractformat *f, uint64_t *at, uint64_t end, bool be, uint32_t *v, xx_pd_struct *pd) {
    uint8_t p[4];
    if (!record_take(f, at, end, p, 4, pd))
        return false;
    *v = xx_data_get_u32(p, 4, 0, be);
    return true;
}
static XXFC_MAYBE_UNUSED bool record_float_array(Abstractformat *f, uint64_t at, uint64_t bytes, unsigned width,
                                                 bool be, xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size(), i;
    uint64_t done = 0;
    unsigned used = 0;
    uint8_t *p, word[8];
    bool result = true;
    if ((width != 4 && width != 8) || bytes % width || !record_span(at, bytes, (uint64_t)pm_available(f)))
        return false;
    if (!bytes)
        return true;
    if (capacity > bytes)
        capacity = (size_t)bytes;
    p = (uint8_t *)xx_mem_alloc(capacity);
    if (!p)
        return false;
    while (done < bytes) {
        size_t n = bytes - done > capacity ? capacity : (size_t)(bytes - done);
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)(at + done), p, n)) {
            result = false;
            break;
        }
        for (i = 0; i < n; ++i) {
            word[used++] = p[i];
            if (used == width) {
                if (width == 4 ? (xx_data_get_u32(word, 4, 0, be) & 0x7f800000U) == 0x7f800000U
                               : !numeric_finite64(xx_data_get_u64(word, 8, 0, be))) {
                    result = false;
                    break;
                }
                used = 0;
            }
        }
        if (!result)
            break;
        done += n;
    }
    xx_mem_free(p);
    return result;
}
static XXFC_MAYBE_UNUSED bool record_utf16_end(Abstractformat *f, uint64_t at, uint64_t end, xx_pd_struct *pd) {
    uint8_t p[2];
    unsigned i;
    bool high = false;
    for (i = 0; i < 4096; ++i) {
        uint16_t ch;
        if (!record_take(f, &at, end, p, 2, pd))
            return false;
        ch = xx_data_get_u16(p, 2, 0, false);
        if (!ch)
            return !high;
        if (high) {
            if (ch < 0xdc00 || ch > 0xdfff)
                return false;
            high = false;
        } else if (ch >= 0xd800 && ch <= 0xdbff)
            high = true;
        else if (ch >= 0xdc00 && ch <= 0xdfff)
            return false;
    }
    return false;
}
static XXFC_MAYBE_UNUSED bool record_i32_string(Abstractformat *f, uint64_t *at, uint64_t end, bool be, bool empty,
                                                xx_pd_struct *pd) {
    unsigned i;
    uint32_t c;
    for (i = 0; i < 256; ++i) {
        if (!record_word(f, at, end, be, &c, pd))
            return false;
        if (!c)
            return empty || i != 0;
        if (c < 32 || c > 126)
            return false;
    }
    return false;
}
static XXFC_MAYBE_UNUSED bool record_no_overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m) {
    return !n || !m || a >= b + m || b >= a + n;
}
typedef struct record_bytes {
    Abstractformat *f;
    xx_pd_struct *pd;
    uint64_t at, end, start;
    size_t count, capacity;
    uint8_t *bytes;
} record_bytes;
static bool record_byte(record_bytes *r, uint8_t *b) {
    if (binary_stop(r->pd) || r->at >= r->end)
        return false;
    if (!r->bytes) {
        if ((uint64_t)(r->end - r->at) < r->capacity)
            r->capacity = (size_t)(r->end - r->at);
        r->bytes = (uint8_t *)xx_mem_alloc(r->capacity);
        if (!r->bytes)
            return false;
    }
    if (!r->count || r->at < r->start || r->at - r->start >= r->count) {
        uint64_t left = r->end - r->at;
        r->start = r->at;
        r->count = left > r->capacity ? r->capacity : (size_t)left;
        if (!pm_read(r->f, (int64_t)r->start, r->bytes, r->count))
            return false;
    }
    *b = r->bytes[(size_t)(r->at - r->start)];
    ++r->at;
    return true;
}
/* Validate bounded JPEG marker/scan framing; entropy-coded pixels stay encoded. */
static XXFC_MAYBE_UNUSED bool record_jpeg(Abstractformat *f, uint64_t at, uint64_t end, xx_pd_struct *pd) {
    record_bytes r;
    bool buffer_result = false;
    uint8_t a, b, marker = 0;
    unsigned segments = 0;
    bool sof = false, scan = false;
    xx_mem_zero(&r, sizeof(r));
    r.f = f;
    r.pd = pd;
    r.at = at;
    r.end = end;
    r.capacity = xx_get_file_buffer_size();
    if (!record_byte(&r, &a) || !record_byte(&r, &b) || a != 255 || b != 216) {
        buffer_result = (false);
        goto buffer_done;
    }
    while ((r.at < end || marker) && ++segments <= 4096) {
        uint32_t n;
        uint64_t next;
        if (!marker) {
            if (!record_byte(&r, &a) || a != 255) {
                buffer_result = (false);
                goto buffer_done;
            }
            do {
                if (!record_byte(&r, &marker)) {
                    buffer_result = (false);
                    goto buffer_done;
                }
            } while (marker == 255);
        }
        if (marker == 217) {
            buffer_result = (sof && scan && r.at == end);
            goto buffer_done;
        }
        if (!marker || marker == 216 || (marker >= 208 && marker <= 215) || marker == 1 || !record_byte(&r, &a) ||
            !record_byte(&r, &b)) {
            buffer_result = (false);
            goto buffer_done;
        }
        n = (uint32_t)a * 256 + b;
        if (n < 2 || !record_span(r.at, n - 2, end)) {
            buffer_result = (false);
            goto buffer_done;
        }
        next = r.at + n - 2;
        if (marker == 192 || marker == 193 || marker == 194) {
            uint8_t p[6];
            unsigned i;
            for (i = 0; i < 6; ++i)
                if (!record_byte(&r, p + i)) {
                    buffer_result = (false);
                    goto buffer_done;
                }
            if (sof || p[0] != 8 || !xx_data_get_u16(p + 1, 2, 0, true) || !xx_data_get_u16(p + 3, 2, 0, true) ||
                !p[5] || p[5] > 4 || n != 8U + 3U * p[5]) {
                buffer_result = (false);
                goto buffer_done;
            }
            sof = true;
        } else if (marker >= 192 && marker <= 207 && marker != 196 && marker != 200 && marker != 204) {
            buffer_result = (false);
            goto buffer_done;
        }
        if (marker == 218) {
            uint8_t channels;
            if (!sof || !record_byte(&r, &channels) || !channels || channels > 4 || n != 6U + 2U * channels) {
                buffer_result = (false);
                goto buffer_done;
            }
            scan = true;
            r.at = next;
            marker = 0;
            while (r.at < end) {
                if (!record_byte(&r, &a)) {
                    buffer_result = (false);
                    goto buffer_done;
                }
                if (a != 255)
                    continue;
                do {
                    if (!record_byte(&r, &b)) {
                        buffer_result = (false);
                        goto buffer_done;
                    }
                } while (b == 255);
                if (!b || (b >= 208 && b <= 215))
                    continue;
                marker = b;
                break;
            }
            if (!marker) {
                buffer_result = (false);
                goto buffer_done;
            }
        } else {
            r.at = next;
            marker = 0;
        }
    }
    {
        buffer_result = (false);
        goto buffer_done;
    }

buffer_done:
    xx_mem_free(r.bytes);
    return buffer_result;
}
#endif
