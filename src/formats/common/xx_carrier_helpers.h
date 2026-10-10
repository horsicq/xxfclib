/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private bounded parsing utilities for the wrapper/game group.
 */
#ifndef XX_CARRIER_HELPERS_H
#define XX_CARRIER_HELPERS_H
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
static XXFC_MAYBE_UNUSED uint64_t carrier_u64(const uint8_t *p) {
    return (uint64_t)xx_data_get_u32(p + 4, 4, 0, false) << 32 | xx_data_get_u32(p, 4, 0, false);
}
static XXFC_MAYBE_UNUSED uint64_t carrier_be64(const uint8_t *p) {
    return (uint64_t)xx_data_get_u32(p, 4, 0, true) << 32 | xx_data_get_u32(p + 4, 4, 0, true);
}
static bool carrier_range(int64_t end, uint64_t at, uint64_t n) {
    return end >= 0 && at <= (uint64_t)end && n <= (uint64_t)end - at;
}
static bool carrier_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
typedef struct carrier_extent {
    int64_t lo, hi;
} carrier_extent;
static void carrier_sift(carrier_extent *r, size_t n, size_t p) {
    while (p < n / 2) {
        size_t c = p * 2 + 1;
        carrier_extent t;
        if (c + 1 < n && r[c + 1].lo > r[c].lo)
            ++c;
        if (r[p].lo >= r[c].lo)
            break;
        t = r[p];
        r[p] = r[c];
        r[c] = t;
        p = c;
    }
}
static bool carrier_extents(carrier_extent *r, size_t n, xx_pd_struct *pd) {
    size_t i;
    carrier_extent t;
    for (i = n / 2; i > 0; --i) {
        carrier_sift(r, n, i - 1);
    }
    for (i = n; i > 1; --i) {
        if (carrier_stop(pd))
            return false;
        t = r[0];
        r[0] = r[i - 1];
        r[i - 1] = t;
        carrier_sift(r, i - 1, 0);
    }
    for (i = 1; i < n; ++i) {
        if (r[i].lo < r[i - 1].hi)
            return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool carrier_members(pm_stream *s, xx_pd_struct *pd) {
    carrier_extent *r;
    size_t i, n = 0;
    bool ok;
    if (s->count < 2) {
        return true;
    }
    r = (carrier_extent *)xx_mem_alloc(s->count * sizeof(*r));
    if (!r)
        return false;
    for (i = 0; i < s->count; ++i)
        if (s->items[i].size) {
            r[n].lo = s->items[i].offset;
            r[n++].hi = s->items[i].offset + s->items[i].size;
        }
    ok = carrier_extents(r, n, pd);
    xx_mem_free(r);
    return ok;
}
static bool carrier_equal(Abstractformat *f, int64_t a, int64_t b, size_t n, xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *x, *y;
    bool result = true;
    if (!n) {
        return true;
    }
    if (capacity > n)
        capacity = n;
    x = (uint8_t *)xx_mem_alloc(capacity);
    y = (uint8_t *)xx_mem_alloc(capacity);
    if (!x || !y) {
        xx_mem_free(x);
        xx_mem_free(y);
        return false;
    }
    while (n) {
        size_t k = n > capacity ? capacity : n;
        if (carrier_stop(pd) || !pm_read(f, a, x, k) || !pm_read(f, b, y, k) || xx_rt_memcmp(x, y, k)) {
            result = false;
            break;
        }
        a += (int64_t)k;
        b += (int64_t)k;
        n -= k;
    }
    xx_mem_free(x);
    xx_mem_free(y);
    return result;
}
static XXFC_MAYBE_UNUSED bool carrier_sum(Abstractformat *f, int64_t at, int64_t bytes, uint32_t expected,
                                          xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *b;
    uint32_t sum = 0;
    bool result = false;
    if (bytes <= 0) {
        return bytes == 0 && !expected;
    }
    if ((uint64_t)bytes < capacity)
        capacity = (size_t)bytes;
    b = (uint8_t *)xx_mem_alloc(capacity);
    if (!b)
        return false;
    while (bytes) {
        size_t n = (uint64_t)(uint64_t)(bytes) > capacity ? capacity : (size_t)bytes, i;
        if (carrier_stop(pd) || !pm_read(f, at, b, n))
            goto done;
        for (i = 0; i < n; ++i) {
            sum += b[i];
        }
        at += (int64_t)n;
        bytes -= (int64_t)n;
    }
    result = sum == expected;
done:
    xx_mem_free(b);
    return result;
}
static bool carrier_zero(const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i])
            return false;
    return true;
}
static XXFC_MAYBE_UNUSED bool carrier_string(Abstractformat *f, int64_t *at, int64_t end, char *out, size_t cap) {
    size_t n = 0;
    /* The caller owns the complete string result. Read directly into it;
     * pm_read splits physical transfers at the global capacity. */
    while (*at < end && n + 1 < cap) {
        size_t take = end - *at > 256 ? 256U : (size_t)(end - *at), i;
        if (take > cap - n - 1) {
            take = cap - n - 1;
        }
        if (!pm_read(f, *at, out + n, take))
            return false;
        for (i = 0; i < take; ++i) {
            ++*at;
            if (!out[n++])
                return true;
        }
    }
    return false;
}
static XXFC_MAYBE_UNUSED bool carrier_name(Abstractformat *f, int64_t at, int64_t end, bool wide, size_t *budget,
                                           xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size(), used = 0, step = wide ? 2U : 1U;
    uint8_t *b, word[2];
    bool result = false;
    if (capacity > 512) {
        capacity = 512;
    }
    b = (uint8_t *)xx_mem_alloc(capacity);
    if (!b)
        return false;
    while (at < end && used < 4096U * step) {
        size_t n = end - at > 512 ? 512U : (size_t)(end - at), done = 0, local_budget = *budget;
        unsigned held = 0;
        int decision = 0;
        n -= n % step;
        if (!n || carrier_stop(pd))
            break;
        while (done < n) {
            size_t take = n - done > capacity ? capacity : n - done, i;
            if (!pm_read(f, at + (int64_t)done, b, take))
                goto finished;
            for (i = 0; i < take && !decision; ++i) {
                word[held++] = b[i];
                if (held < step)
                    continue;
                held = 0;
                if (local_budget < step || used >= 4096U * step) {
                    decision = -1;
                    break;
                }
                local_budget -= step;
                if (!word[0] && (!wide || !word[1])) {
                    decision = used ? 1 : -1;
                    break;
                }
                used += step;
            }
            done += take;
        }
        /* Preserve the original full logical read before committing grammar
         * decisions or its per-code-unit validation budget. */
        *budget = local_budget;
        if (decision) {
            result = decision > 0;
            break;
        }
        at += (int64_t)n;
    }
finished:
    xx_mem_free(b);
    return result;
}
static bool carrier_decimal(const char *p, size_t n, uint64_t *value) {
    uint64_t v = 0;
    size_t i;
    if (!n)
        return false;
    for (i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9' || v > ((uint64_t)INT64_MAX - (unsigned)(p[i] - '0')) / 10)
            return false;
        v = v * 10 + p[i] - '0';
    }
    *value = v;
    return true;
}
static bool carrier_assignment(const char *text, size_t len, const char *name, const char **value, size_t *n) {
    size_t at = 0, k = xx_rt_strlen(name);
    bool found = false;
    while (at < len) {
        size_t start = at, end;
        while (at < len && text[at] != '\n')
            ++at;
        end = at;
        if (at < len)
            ++at;
        if (end > start && text[end - 1] == '\r')
            --end;
        if (end - start > k && !xx_rt_memcmp(text + start, name, k) && text[start + k] == '=') {
            size_t a = start + k + 1, b = end;
            if (found)
                return false;
            found = true;
            while (a < b && (text[a] == ' ' || text[a] == '\t')) {
                ++a;
            }
            while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t'))
                --b;
            if (b - a >= 2 && text[a] == '"' && text[b - 1] == '"') {
                ++a;
                --b;
            }
            *value = text + a;
            *n = b - a;
        }
    }
    return found;
}
static XXFC_MAYBE_UNUSED bool carrier_number(const char *text, size_t len, const char *name, uint64_t *v) {
    const char *p;
    size_t n;
    return carrier_assignment(text, len, name, &p, &n) && carrier_decimal(p, n, v);
}
static XXFC_MAYBE_UNUSED char *carrier_shell(Abstractformat *f, size_t *len) {
    int64_t total = pm_available(f);
    size_t n;
    char *p;
    if (total < 10) {
        return NULL;
    }
    n = total > 65536 ? 65536U : (size_t)total;
    p = (char *)xx_mem_alloc(n + 1);
    if (!p || !pm_read(f, 0, p, n) ||
        (xx_rt_memcmp(p, "#!/bin/sh\n", 10) && xx_rt_memcmp(p, "#!/bin/sh\r", 10) &&
         (n < 12 || (xx_rt_memcmp(p, "#!/bin/bash\n", 12) && xx_rt_memcmp(p, "#!/bin/bash\r", 12))))) {
        xx_mem_free(p);
        return NULL;
    }
    p[n] = 0;
    *len = n;
    return p;
}
static XXFC_MAYBE_UNUSED bool carrier_lines(Abstractformat *f, uint64_t lines, int64_t *offset, xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *b = NULL;
    bool buffer_result = false;
    int64_t at = 0, limit = pm_available(f);
    uint64_t seen = 0;
    if (limit > 1048576)
        limit = 1048576;
    if (!lines || lines > 100000) {
        buffer_result = (false);
        goto buffer_done;
    }
    while (at < limit) {
        if (!b) {
            if ((uint64_t)(limit - at) < capacity)
                capacity = (size_t)(limit - at);
            b = (uint8_t *)xx_mem_alloc(capacity);
            if (!b) {
                buffer_result = false;
                goto buffer_done;
            }
        }
        size_t n = (uint64_t)(limit - at) > capacity ? capacity : (size_t)(limit - at), i;
        if (carrier_stop(pd) || !pm_read(f, at, b, n)) {
            buffer_result = (false);
            goto buffer_done;
        }
        for (i = 0; i < n; ++i)
            if (b[i] == '\n' && ++seen == lines) {
                *offset = at + (int64_t)i + 1;
                {
                    buffer_result = (true);
                    goto buffer_done;
                }
            }
        at += (int64_t)n;
    }
    {
        buffer_result = (false);
        goto buffer_done;
    }

buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static XXFC_MAYBE_UNUSED bool carrier_pe(Abstractformat *f, int64_t *overlay, int64_t *cabinet, int64_t *cabinet_end,
                                         xx_pd_struct *pd) {
    uint8_t h[64];
    uint32_t pe, headers;
    uint16_t count, opt, magic;
    int64_t end, table, limit = pm_available(f);
    unsigned i;
    carrier_extent sections[96];
    if (!pm_read(f, 0, h, 64) || h[0] != 'M' || h[1] != 'Z' || (pe = xx_data_get_u32(h + 60, 4, 0, false)) < 64 ||
        pe > 1048576 || !pm_read(f, pe, h, 24) || xx_rt_memcmp(h, "PE\0\0", 4))
        return false;
    count = xx_data_get_u16(h + 6, 2, 0, false);
    opt = xx_data_get_u16(h + 20, 2, 0, false);
    if (!count || count > 96 || opt < 64 || opt > 4096 || !pm_read(f, (int64_t)pe + 24, h, 64))
        return false;
    magic = xx_data_get_u16(h, 2, 0, false);
    if (magic != 0x10b && magic != 0x20b)
        return false;
    table = (int64_t)pe + 24 + opt;
    end = headers = xx_data_get_u32(h + 60, 4, 0, false);
    if (end < table + (int64_t)count * 40 || end > limit || !carrier_range(limit, table, (uint64_t)count * 40))
        return false;
    *cabinet = *cabinet_end = -1;
    for (i = 0; i < count; ++i) {
        uint32_t at, n;
        if (carrier_stop(pd) || !pm_read(f, table + (int64_t)i * 40, h, 40))
            return false;
        n = xx_data_get_u32(h + 16, 4, 0, false);
        at = xx_data_get_u32(h + 20, 4, 0, false);
        if (n && (at < headers || !carrier_range(limit, at, n)))
            return false;
        sections[i].lo = n ? at : limit;
        sections[i].hi = n ? (int64_t)at + n : limit;
        if (n && (int64_t)at + n > end)
            end = (int64_t)at + n;
        if (!xx_rt_memcmp(h, "_cabinet", 8)) {
            if (*cabinet >= 0 || !n)
                return false;
            *cabinet = at;
            *cabinet_end = (int64_t)at + n;
        }
    }
    if (!carrier_extents(sections, count, pd))
        return false;
    *overlay = end;
    return true;
}
static bool carrier_octal(const uint8_t *p, size_t n, uint64_t *value) {
    uint64_t v = 0;
    size_t i = 0;
    bool digits = false;
    while (i < n && p[i] == ' ')
        ++i;
    while (i < n && p[i] >= '0' && p[i] <= '7') {
        digits = true;
        if (v > UINT64_MAX / 8)
            return false;
        v = v * 8 + p[i++] - '0';
    }
    while (i < n) {
        if (p[i] != 0 && p[i] != ' ')
            return false;
        ++i;
    }
    *value = v;
    return digits;
}
static XXFC_MAYBE_UNUSED bool carrier_tar(Abstractformat *f, int64_t start, int64_t end, xx_pd_struct *pd) {
    uint8_t h[512];
    int64_t at = start;
    unsigned count = 0;
    if ((end - start) % 512)
        return false;
    while (at < end) {
        uint64_t size, stored, sum = 0;
        unsigned i;
        if (carrier_stop(pd) || end - at < 512 || !pm_read(f, at, h, 512))
            return false;
        if (carrier_zero(h, 512)) {
            if (!count || end - at < 1024)
                return false;
            at += 512;
            while (at < end) {
                if (carrier_stop(pd) || !pm_read(f, at, h, 512) || !carrier_zero(h, 512))
                    return false;
                at += 512;
            }
            return true;
        }
        if (++count > 65536 || (xx_rt_memcmp(h + 257, "ustar\0", 6) && xx_rt_memcmp(h + 257, "ustar ", 6)) ||
            !carrier_octal(h + 124, 12, &size) || !carrier_octal(h + 148, 8, &stored))
            return false;
        for (i = 0; i < 512; ++i) {
            sum += i >= 148 && i < 156 ? 32U : h[i];
        }
        if (sum != stored || size > INT64_MAX - 511)
            return false;
        size = (size + 511) & ~511ULL;
        if (!carrier_range(end, (uint64_t)at + 512, size))
            return false;
        at += 512 + (int64_t)size;
    }
    return false;
}
/* Authenticate the complete ZIP directory, offsets and local header extents.
 * Encoded member streams stay inside the returned nested archive component. */
static XXFC_MAYBE_UNUSED bool carrier_zip(Abstractformat *f, int64_t start, int64_t end, xx_pd_struct *pd) {
    uint8_t h[46];
    int64_t at = end - 22, low = end - start > 65557 ? end - 65557 : start, dir, stop, bias;
    uint16_t count, i;
    uint32_t bytes, off;
    bool found = false, ok = false;
    carrier_extent *ranges = NULL;
    if (end - start < 22)
        return false;
    for (; at >= low; --at) {
        if (carrier_stop(pd) || !pm_read(f, at, h, 4))
            return false;
        if (!xx_rt_memcmp(h, "PK\5\6", 4)) {
            if (!pm_read(f, at, h, 22) || at + 22 + xx_data_get_u16(h + 20, 2, 0, false) != end) {
                continue;
            }
            found = true;
            break;
        }
    }
    if (!found || xx_data_get_u16(h + 4, 2, 0, false) || xx_data_get_u16(h + 6, 2, 0, false) ||
        (count = xx_data_get_u16(h + 10, 2, 0, false)) != xx_data_get_u16(h + 8, 2, 0, false) || !count ||
        count == 65535)
        return false;
    bytes = xx_data_get_u32(h + 12, 4, 0, false);
    off = xx_data_get_u32(h + 16, 4, 0, false);
    if (bytes > (uint64_t)(at - start))
        return false;
    dir = at - bytes;
    bias = dir - (int64_t)off;
    stop = at;
    at = dir;
    ranges = (carrier_extent *)xx_mem_alloc((size_t)count * sizeof(*ranges));
    if (!ranges)
        return false;
    for (i = 0; i < count; ++i) {
        uint32_t packed, local;
        uint16_t fn, extra, comment;
        int64_t record, lp;
        uint8_t l[30];
        if (carrier_stop(pd) || stop - at < 46 || !pm_read(f, at, h, 46) || xx_rt_memcmp(h, "PK\1\2", 4) ||
            xx_data_get_u16(h + 34, 2, 0, false))
            goto done;
        fn = xx_data_get_u16(h + 28, 2, 0, false);
        extra = xx_data_get_u16(h + 30, 2, 0, false);
        comment = xx_data_get_u16(h + 32, 2, 0, false);
        packed = xx_data_get_u32(h + 20, 4, 0, false);
        local = xx_data_get_u32(h + 42, 4, 0, false);
        record = 46 + (int64_t)fn + extra + comment;
        if (!fn || record > stop - at || packed == UINT32_MAX || local == UINT32_MAX || bias > INT64_MAX - local) {
            goto done;
        }
        lp = bias + local;
        if (lp < start || lp > dir - 30 || !pm_read(f, lp, l, 30) || xx_rt_memcmp(l, "PK\3\4", 4) ||
            xx_data_get_u16(l + 6, 2, 0, false) != xx_data_get_u16(h + 8, 2, 0, false) ||
            xx_data_get_u16(l + 8, 2, 0, false) != xx_data_get_u16(h + 10, 2, 0, false) ||
            xx_data_get_u16(l + 26, 2, 0, false) != fn)
            goto done;
        if ((uint64_t)30 + xx_data_get_u16(l + 26, 2, 0, false) + xx_data_get_u16(l + 28, 2, 0, false) + packed >
                (uint64_t)(dir - lp) ||
            !carrier_equal(f, lp + 30, at + 46, fn, pd))
            goto done;
        if (!(xx_data_get_u16(l + 6, 2, 0, false) & 8) &&
            (xx_data_get_u32(l + 18, 4, 0, false) != packed ||
             xx_data_get_u32(l + 22, 4, 0, false) != xx_data_get_u32(h + 24, 4, 0, false) ||
             xx_data_get_u32(l + 14, 4, 0, false) != xx_data_get_u32(h + 16, 4, 0, false)))
            goto done;
        ranges[i].lo = lp;
        ranges[i].hi = lp + 30 + xx_data_get_u16(l + 26, 2, 0, false) + xx_data_get_u16(l + 28, 2, 0, false) + packed;
        if (xx_data_get_u16(l + 6, 2, 0, false) & 8) {
            uint8_t d[12];
            int64_t dp = ranges[i].hi;
            if (dir - dp < 12 || !pm_read(f, dp, d, 4)) {
                goto done;
            }
            if (!xx_rt_memcmp(d, "PK\7\10", 4))
                dp += 4;
            if (dir - dp < 12 || !pm_read(f, dp, d, 12) ||
                xx_data_get_u32(d, 4, 0, false) != xx_data_get_u32(h + 16, 4, 0, false) ||
                xx_data_get_u32(d + 4, 4, 0, false) != packed ||
                xx_data_get_u32(d + 8, 4, 0, false) != xx_data_get_u32(h + 24, 4, 0, false)) {
                goto done;
            }
            ranges[i].hi = dp + 12;
        }
        at += record;
    }
    ok = at == stop && carrier_extents(ranges, count, pd);
done:
    xx_mem_free(ranges);
    return ok;
}
static XXFC_MAYBE_UNUSED bool carrier_crc_mpeg(Abstractformat *f, int64_t at, int64_t bytes, uint32_t expected,
                                               xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *buf = (uint8_t *)xx_mem_alloc(capacity);
    xx_crc_context ctx;
    if (!buf)
        return false;
    if (!xx_crc_context_init_type(&ctx, XX_CRC_TYPE_CRC32_MPEG2)) {
        xx_mem_free(buf);
        return false;
    }
    while (bytes) {
        size_t n = (uint64_t)bytes > capacity ? capacity : (size_t)bytes;
        if (carrier_stop(pd) || !pm_read(f, at, buf, n)) {
            xx_mem_free(buf);
            return false;
        }
        xx_crc_context_update(&ctx, buf, n);
        at += n;
        bytes -= n;
    }
    xx_mem_free(buf);
    return (uint32_t)xx_crc_context_final(&ctx) == expected;
}
#endif
