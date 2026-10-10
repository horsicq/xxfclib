/* SPDX-License-Identifier: MIT. shared format private full-framing carrier helpers. */
#ifndef XX_ARCHIVE_CARRIER_READERS_H
#define XX_ARCHIVE_CARRIER_READERS_H
#include "xx_executable_carrier.h"
#include "xxfclib/formats/imp/xx_imp.h"
#include "xxfclib/formats/starkit/xx_starkit.h"
#include "xxfclib/formats/chm/xx_chm.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/ain/xx_ain.h"
#include "../sfx_imp/xx_ain_directory.h"
#define ARCHIVE_CARRIER_LIMIT 16777216U
#define ARCHIVE_CARRIER_COUNT 4096U
static XXFC_MAYBE_UNUSED uint8_t *archive_carrier_load(Abstractformat *f, size_t *n, xx_pd_struct *pd) {
    int64_t limit = pm_available(f);
    uint8_t *b;
    size_t i;
    if (limit <= 0 || limit > ARCHIVE_CARRIER_LIMIT || carrier_stop(pd))
        return NULL;
    *n = (size_t)limit;
    b = (uint8_t *)xx_mem_alloc(*n);
    if (!b)
        return NULL;
    for (i = 0; i < *n; i += 65536U) {
        size_t z = *n - i > 65536U ? 65536U : *n - i;
        if (carrier_stop(pd) || !pm_read(f, (int64_t)i, b + i, z)) {
            xx_mem_free(b);
            return NULL;
        }
    }
    return b;
}
static bool archive_carrier_range(size_t n, uint64_t p, uint64_t z) { return p <= n && z <= n - p; }
static uint16_t archive_carrier_crc16(const uint8_t *b, size_t n, uint16_t c, xx_pd_struct *pd) {
    size_t at = 0U;
    while (at < n) {
        size_t part = n - at > 4096U ? 4096U : n - at;
        if (carrier_stop(pd))
            return 0U;
        c = xx_crc16_ccitt_calc(c, b + at, part);
        at += part;
    }
    return c;
}
static bool archive_carrier_string(const uint8_t *b, size_t n, size_t *p, size_t maximum) {
    size_t start = *p;
    while (*p < n && b[*p]) {
        if (*p - start >= maximum)
            return false;
        ++*p;
    }
    if (*p == n)
        return false;
    ++*p;
    return true;
}
static bool archive_carrier_nested(Abstractformat *f, int64_t at, sfx_carrier_open open, sfx_carrier_close close,
                                   uint32_t expected, xx_pd_struct *pd) {
    Abstractformat *r = open(f->device, f->base_address + at);
    xx_archive_record_state *st = NULL;
    const xx_archive_record *rec;
    size_t count = 0;
    bool ok = false;
    int64_t size = pm_available(f) - at;
    if (!r) {
        return false;
    }
    if (!carrier_stop(pd) && xx_format_handle_base_info(r, pd) && r->format_size == size &&
        r->number_of_archive_records && r->number_of_archive_records <= ARCHIVE_CARRIER_COUNT) {
        st = xx_format_create_archive_records_reading(r, NULL, pd);
        if (st) {
            ok = true;
            while ((rec = xx_format_get_current_archive_record(r, st)) != NULL) {
                if (carrier_stop(pd) || ++count > ARCHIVE_CARRIER_COUNT || rec->compressed_size < 0 ||
                    rec->data_offset < f->base_address + at ||
                    !carrier_range(pm_available(f), rec->data_offset - f->base_address,
                                   (uint64_t)rec->compressed_size)) {
                    ok = false;
                    break;
                }
                if (!xx_format_archive_record_move_to_next(r, st, pd))
                    break;
            }
            if (!count || count != r->number_of_archive_records || (expected && count != expected))
                ok = false;
        }
    }
    if (st)
        xx_format_free_archive_records_reading(r, st);
    close(r);
    return ok;
}
/* Some legacy linkers retain SizeOfRawData for a pure BSS section whose
 * PointerToRawData is zero. The section has no initialized/code bytes, and
 * contributes no file extent. All physical PE ranges remain authenticated. */
static bool archive_carrier_carrier(Abstractformat *f, int64_t *low, xx_pd_struct *pd) {
    uint8_t h[64];
    uint64_t image, headers;
    uint32_t pe, size;
    uint16_t count, opt;
    int64_t table, limit = pm_available(f);
    carrier_extent sections[96];
    unsigned i;
    if (executable_carrier_carrier(f, low, true, false, pd)) {
        return true;
    }
    if (!pm_read(f, 0, h, 64) || xx_rt_memcmp(h, "MZ", 2))
        return false;
    headers = (uint64_t)xx_data_get_u16(h + 8, 2, 0, false) * 16;
    image = xx_data_get_u16(h + 4, 2, 0, false);
    if (!image || xx_data_get_u16(h + 2, 2, 0, false) > 511 || headers < 28)
        return false;
    image = (image - 1) * 512 + (xx_data_get_u16(h + 2, 2, 0, false) ? xx_data_get_u16(h + 2, 2, 0, false) : 512);
    if (headers > image || image > (uint64_t)limit ||
        (uint64_t)xx_data_get_u16(h + 24, 2, 0, false) + 4U * xx_data_get_u16(h + 6, 2, 0, false) > headers)
        return false;
    pe = xx_data_get_u32(h + 60, 4, 0, false);
    if (pe < 64 || pe > 1048576 || !pm_read(f, pe, h, 24) || xx_rt_memcmp(h, "PE\0\0", 4))
        return false;
    count = xx_data_get_u16(h + 6, 2, 0, false);
    opt = xx_data_get_u16(h + 20, 2, 0, false);
    if (!count || count > 96 || opt < 64 || opt > 4096 || !pm_read(f, (int64_t)pe + 24, h, 64) ||
        (xx_data_get_u16(h, 2, 0, false) != 0x10b && xx_data_get_u16(h, 2, 0, false) != 0x20b))
        return false;
    table = (int64_t)pe + 24 + opt;
    size = xx_data_get_u32(h + 60, 4, 0, false);
    if (size < table + (int64_t)count * 40 || size > (uint64_t)limit ||
        !carrier_range(limit, table, (uint64_t)count * 40))
        return false;
    for (i = 0; i < count; ++i) {
        uint32_t raw, p, flags;
        if (carrier_stop(pd) || !pm_read(f, table + (int64_t)i * 40, h, 40))
            return false;
        raw = xx_data_get_u32(h + 16, 4, 0, false);
        p = xx_data_get_u32(h + 20, 4, 0, false);
        flags = xx_data_get_u32(h + 36, 4, 0, false);
        if (!p && (flags & 0xe0) == 0x80)
            raw = 0;
        if (raw && (p < size || !carrier_range(limit, p, raw)))
            return false;
        sections[i].lo = raw ? p : limit;
        sections[i].hi = raw ? (int64_t)p + raw : limit;
    }
    if (!carrier_extents(sections, count, pd))
        return false;
    *low = 64;
    return true;
}
typedef bool (*archive_carrier_validate)(Abstractformat *, int64_t, const uint8_t *, size_t, xx_pd_struct *);
/* A complete candidate is a bounded archive tail. A plausible first header
 * makes subsequent structural failure fatal, so a damaged archive cannot be
 * accepted as a later suffix. Weak signatures get a cheap grammar gate. */
static bool archive_carrier_gate(unsigned kind, const uint8_t *b, size_t n) {
    switch (kind) {
    case 1:
        return n >= 39 && b[2] == 1 && b[3] >= 39;
    case 2:
        return n >= 21 && xx_data_get_u16(b + 2, 2, 0, false) > 0 &&
               xx_data_get_u16(b + 2, 2, 0, false) <= ARCHIVE_CARRIER_COUNT && ((b[4] >> 4) == 2 || b[4] == 255);
    case 3:
        return n >= 42 && (b[11 + 10] == 0 || b[11 + 10] == 2);
    case 4:
        return n >= 25 && b[2] == 'R' && xx_data_get_u16(b + 5, 2, 0, false) == 25;
    case 5: {
        uint16_t sum = 0;
        uint32_t directory;
        size_t i;
        if (n < 24 || (b[1] & 15) < 1 || (b[1] & 15) > 4 || (b[1] >> 4) < 1 || (b[1] >> 4) > 3 ||
            !xx_data_get_u16(b + 8, 2, 0, false))
            return false;
        directory = xx_data_get_u32(b + 14, 4, 0, false);
        if (directory < 24 || directory >= n)
            return false;
        for (i = 0; i < 22; ++i)
            sum = (uint16_t)(sum + b[i]);
        return sum == (uint16_t)(xx_data_get_u16(b + 22, 2, 0, false) ^ 0x5555U);
    }
    case 9:
        return n >= 28 && (xx_data_get_u16(b + 6, 2, 0, true) == 0x130 || xx_data_get_u16(b + 6, 2, 0, true) == 0x140 ||
                           xx_data_get_u16(b + 6, 2, 0, true) == 0x160);
    default:
        return true;
    }
}
static XXFC_MAYBE_UNUSED bool archive_carrier_carried(Abstractformat *f, pm_stream *s, const uint8_t *sig,
                                                      size_t siglen, int adjust, unsigned kind,
                                                      archive_carrier_validate validate, const char *label,
                                                      xx_pd_struct *pd) {
    int64_t low, limit = pm_available(f);
    size_t n, i;
    unsigned attempts = 0;
    uint8_t *b;
    bool ok = false;
    if (!archive_carrier_carrier(f, &low, pd) || low >= limit || limit - low > ARCHIVE_CARRIER_LIMIT) {
        return false;
    }
    n = (size_t)(limit - low);
    b = (uint8_t *)xx_mem_alloc(n);
    if (!b)
        return false;
    for (i = 0; i < n; i += 65536U) {
        size_t z = n - i > 65536U ? 65536U : n - i;
        if (carrier_stop(pd) || !pm_read(f, low + (int64_t)i, b + i, z))
            goto done;
    }
    for (i = 0; i + siglen <= n; ++i) {
        int64_t p = low + (int64_t)i + adjust;
        size_t off;
        if ((i & 4095U) == 0 && carrier_stop(pd))
            goto done;
        if (xx_rt_memcmp(b + i, sig, siglen) || p < low)
            continue;
        off = (size_t)(p - low);
        if (!archive_carrier_gate(kind, b + off, n - off))
            continue;
        if (++attempts > 8)
            break;
        if (validate(f, p, b + off, n - off, pd)) {
            ok = executable_carrier_component(f, s, p, (int64_t)(n - off), label);
            break;
        }
        /* RED 16-bit loaders can contain an earlier embedded engine I.EXE;
         * only that exact carrier helper may be skipped after its full CRC. */
        if (kind == 1 && n - off >= 41 && archive_carrier_range(n, off, b[off + 3]) &&
            !xx_rt_memcmp(b + off + 26, "I.EXE", 5) &&
            archive_carrier_crc16(b + off + 2, b[off + 3] - 4, 65535, pd) ==
                xx_data_get_u16(b + off + b[off + 3] - 2, 2, 0, true))
            continue;
        break;
    }
done:
    xx_mem_free(b);
    return ok && !carrier_stop(pd);
}
static Abstractformat *archive_carrier_imp_open(xx_io_device *d, int64_t b) {
    xx_imp *r = xx_imp_create(d, b);
    return r ? &r->format : NULL;
}
static void archive_carrier_imp_close(Abstractformat *f) { xx_imp_free((xx_imp *)f); }
static XXFC_MAYBE_UNUSED bool archive_carrier_imp(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                  xx_pd_struct *pd) {
    uint8_t h[42];
    uint32_t count, dir;
    size_t pos;
    unsigned chunks = 0;
    uint16_t expected;
    if (n < 42)
        return false;
    count = xx_data_get_u32(b + 8, 4, 0, false);
    dir = xx_data_get_u32(b + 4, 4, 0, false);
    if (!count || count > 1024 || dir < 42 || !archive_carrier_range(n, dir, 12) || n - dir > 1048576 ||
        xx_data_get_u16(b + 38, 2, 0, false) & 5 || xx_rt_memcmp(b + dir, "IMPDE\0", 6))
        return false;
    xx_rt_memcpy(h, b, 42);
    expected = xx_data_get_u16(h + 40, 2, 0, false);
    h[40] = h[41] = 0;
    if ((executable_carrier_crc(h, 42) & 65535) != expected)
        return false;
    pos = dir;
    while (pos < n) {
        uint64_t bits = 0;
        uint32_t plain, packed;
        unsigned i;
        if (carrier_stop(pd) || ++chunks > 4096 || !archive_carrier_range(n, pos, 12) ||
            xx_rt_memcmp(b + pos, "IMPDE\0", 6))
            return false;
        for (i = 0; i < 6; ++i)
            bits |= (uint64_t)b[pos + 6 + i] << (8 * i);
        plain = (uint32_t)((bits >> 5) & 0xfffff);
        packed = (uint32_t)((bits >> 25) & 0xfffff);
        if ((bits & 15) > 1 || !plain || plain > 8192 || packed < 6 || !archive_carrier_range(n, pos + 6, packed) ||
            (!(bits & 15) && packed < 6U + plain))
            return false;
        pos += 6U + packed;
    }
    return archive_carrier_nested(f, at, archive_carrier_imp_open, archive_carrier_imp_close, count, pd);
}
static XXFC_MAYBE_UNUSED bool archive_carrier_red(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                  xx_pd_struct *pd) {
    size_t p = 0;
    unsigned count = 0;
    (void)f;
    (void)at;
    while (p < n) {
        size_t hs;
        uint32_t packed, raw;
        uint16_t method;
        if (carrier_stop(pd) || ++count > ARCHIVE_CARRIER_COUNT || !archive_carrier_range(n, p, 39) || b[p] != 'R' ||
            b[p + 1] != 'R' || b[p + 2] != 1)
            return false;
        hs = b[p + 3];
        if (hs < 39 || !archive_carrier_range(n, p, hs) ||
            archive_carrier_crc16(b + p + 2, hs - 4, 65535, pd) != xx_data_get_u16(b + p + hs - 2, 2, 0, true) ||
            xx_data_get_u16(b + p + 20, 2, 0, false) || xx_data_get_u16(b + p + 22, 2, 0, false) != 1)
            return false;
        packed = xx_data_get_u32(b + p + 8, 4, 0, false);
        raw = xx_data_get_u32(b + p + 12, 4, 0, false);
        method = xx_data_get_u16(b + p + 24, 2, 0, false);
        if ((method != 1 && method != 9 && method != 11) || (method == 1 && packed != raw) || packed > INT32_MAX ||
            raw > INT32_MAX || !archive_carrier_range(n, p + hs, packed) || !b[p + 26] ||
            !xx_rt_memchr(b + p + 26, 0, hs - 28))
            return false;
        p += hs + packed;
    }
    return count != 0;
}
static XXFC_MAYBE_UNUSED bool archive_carrier_ha(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                 xx_pd_struct *pd) {
    size_t p = 4;
    unsigned i, count;
    (void)f;
    (void)at;
    if (n < 4 || (count = xx_data_get_u16(b + 2, 2, 0, false)) == 0 || count > ARCHIVE_CARRIER_COUNT)
        return false;
    for (i = 0; i < count; ++i) {
        uint32_t packed, raw;
        uint8_t type, machine;
        size_t data;
        if (carrier_stop(pd) || !archive_carrier_range(n, p, 17))
            return false;
        type = b[p];
        packed = xx_data_get_u32(b + p + 1, 4, 0, false);
        raw = xx_data_get_u32(b + p + 5, 4, 0, false);
        if (type == 255 || (type >> 4) != 2 || ((type & 15) > 2 && (type & 15) != 14 && (type & 15) != 15) ||
            packed > INT32_MAX || raw > INT32_MAX)
            return false;
        p += 17;
        if (!archive_carrier_string(b, n, &p, 1024) || !archive_carrier_string(b, n, &p, 1024) || p == n)
            return false;
        machine = b[p++];
        data = p + machine;
        if (!archive_carrier_range(n, p, machine) || !archive_carrier_range(n, data, packed))
            return false;
        if ((type & 15) == 0 && packed != raw) {
            return false;
        }
        if ((type & 15) >= 14 && (packed || raw))
            return false;
        p = data + packed;
    }
    return p == n;
}
static XXFC_MAYBE_UNUSED bool archive_carrier_lzx(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                  xx_pd_struct *pd) {
    size_t p = 10;
    unsigned count = 0;
    uint64_t group = 0;
    uint8_t method = 0;
    (void)f;
    (void)at;
    if (n < 10)
        return false;
    while (p < n) {
        uint8_t h[541];
        size_t z;
        uint32_t raw, packed, crc;
        if (carrier_stop(pd) || ++count > ARCHIVE_CARRIER_COUNT || !archive_carrier_range(n, p, 31))
            return false;
        raw = xx_data_get_u32(b + p + 2, 4, 0, false);
        packed = xx_data_get_u32(b + p + 6, 4, 0, false);
        z = 31U + b[p + 30] + b[p + 14];
        if (!b[p + 30] || (b[p + 11] != 0 && b[p + 11] != 2) || (b[p + 12] & ~1U) || !archive_carrier_range(n, p, z) ||
            !archive_carrier_range(n, p + z, packed))
            return false;
        xx_rt_memcpy(h, b + p, z);
        crc = xx_data_get_u32(h + 26, 4, 0, false);
        xx_rt_memset(h + 26, 0, 4);
        if (!executable_carrier_crc_checked(h, z, crc, pd))
            return false;
        if (group && method != b[p + 11]) {
            return false;
        }
        method = b[p + 11];
        group += raw;
        if (group > ARCHIVE_CARRIER_LIMIT)
            return false;
        if (packed) {
            if (!method && (group != packed || !executable_carrier_crc_checked(
                                                   b + p + z, packed, xx_data_get_u32(b + p + 22, 4, 0, false), pd)))
                return false;
            group = 0;
        } else if (!(b[p + 12] & 1) && raw)
            return false;
        p += z + packed;
    }
    return count && !group;
}
static XXFC_MAYBE_UNUSED bool archive_carrier_sqx(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                  xx_pd_struct *pd) {
    size_t p = 25;
    unsigned count = 0;
    (void)f;
    (void)at;
    if (n < 25 || b[2] != 'R' || xx_data_get_u16(b + 5, 2, 0, false) != 25 || xx_rt_memcmp(b + 7, "-sqx-", 5) ||
        xx_data_get_u16(b + 3, 2, 0, false) & 0x10)
        return false;
    while (p < n) {
        uint8_t type;
        uint16_t flags, hs;
        uint64_t packed, raw;
        size_t cursor, name;
        if (carrier_stop(pd) || !archive_carrier_range(n, p, 7))
            return false;
        type = b[p + 2];
        flags = xx_data_get_u16(b + p + 3, 2, 0, false);
        hs = xx_data_get_u16(b + p + 5, 2, 0, false);
        if (hs < 7 || !archive_carrier_range(n, p, hs))
            return false;
        if (type == 'A' || type == 'S' || type == 'X')
            return count && hs == 7 && !flags && p + 7 == n;
        if (type != 'D' || ++count > ARCHIVE_CARRIER_COUNT || flags & 0x8008 || hs < 35) {
            return false;
        }
        cursor = 26;
        packed = xx_data_get_u32(b + p + 25, 4, 0, false);
        raw = xx_data_get_u32(b + p + 29, 4, 0, false);
        if (flags & 0x80) {
            if (hs < 43)
                return false;
            packed |= (uint64_t)xx_data_get_u32(b + p + 33, 4, 0, false) << 32;
            raw |= (uint64_t)xx_data_get_u32(b + p + 37, 4, 0, false) << 32;
            cursor = 34;
        }
        name = xx_data_get_u16(b + p + 7 + cursor, 2, 0, false);
        if (name > 4096 || 7 + cursor + 2 + name > hs || b[p + 12] > 4 || packed > INT64_MAX || raw > INT64_MAX ||
            !archive_carrier_range(n, p + hs, packed))
            return false;
        p += hs + (size_t)packed;
    }
    return false;
}
static XXFC_MAYBE_UNUSED bool archive_carrier_ain(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                  xx_pd_struct *pd) {
    uint16_t count, sum = 0;
    uint32_t off;
    size_t i, pos = 0, written = 0, rc = 0;
    carrier_extent ranges[1024];
    uint8_t *dir;
    uint64_t group = 0, skip = 0;
    bool active = false, ok = false;
    (void)f;
    (void)at;
    if (n < 24 || b[0] != '!' || (b[1] & 15) < 1 || (b[1] & 15) > 4 || (b[1] >> 4) < 1 || (b[1] >> 4) > 3 ||
        xx_data_get_u16(b + 2, 2, 0, false)) {
        return false;
    }
    for (i = 0; i < 22; ++i)
        sum = (uint16_t)(sum + b[i]);
    count = xx_data_get_u16(b + 8, 2, 0, false);
    off = xx_data_get_u32(b + 14, 4, 0, false);
    if (sum != (uint16_t)(xx_data_get_u16(b + 22, 2, 0, false) ^ 0x5555) || !count || count > 1024 || off < 24 ||
        off >= n || n - off > 1048576)
        return false;
    dir = (uint8_t *)xx_mem_alloc(1048576);
    if (!dir)
        return false;
    if (!archive_carrier_ain_directory(b + off, n - off, 0, dir, 1048576, &written, pd))
        goto done;
    for (i = 0; i < count; ++i) {
        const uint8_t *r;
        uint8_t flags;
        uint32_t raw, stored, member;
        if (carrier_stop(pd) || !archive_carrier_range(written, pos, 29))
            goto done;
        r = dir + pos;
        pos += 29;
        flags = r[22];
        raw = xx_data_get_u32(r + 5, 4, 0, false);
        stored = xx_data_get_u32(r + 9, 4, 0, false);
        member = xx_data_get_u32(r + 13, 4, 0, false);
        if (flags & 0xe7 || raw > ARCHIVE_CARRIER_LIMIT || !archive_carrier_string(dir, written, &pos, 1024) ||
            pos >= written || dir[pos++] || !r[29])
            goto done;
        if (flags & 16) {
            if (active)
                goto done;
            group = member;
            skip = 0;
            active = true;
        }
        if (!active || group < 24 || group >= off || raw > ARCHIVE_CARRIER_LIMIT - skip)
            goto done;
        if ((b[1] & 15) == 4 && !archive_carrier_range(off, group + skip, raw))
            goto done;
        skip += raw;
        if (flags & 8) {
            if ((skip && !stored) || !archive_carrier_range(off, group, stored))
                goto done;
            if ((b[1] & 15) == 4) {
                if (stored != skip)
                    goto done;
            } else if (skip) {
                uint8_t *plain = (uint8_t *)xx_mem_alloc(skip ? (size_t)skip : 1U);
                size_t decoded = 0;
                bool valid;
                if (!plain)
                    goto done;
                valid = xx_ain_decode_memory(b + group, stored, 0, plain, (size_t)skip, &decoded) && decoded == skip &&
                        !carrier_stop(pd);
                xx_mem_free(plain);
                if (!valid)
                    goto done;
            }
            ranges[rc].lo = (int64_t)group;
            ranges[rc].hi = (int64_t)(group + stored);
            ++rc;
            active = false;
        }
    }
    ok = !active && pos == written && carrier_extents(ranges, rc, pd);
done:
    xx_mem_free(dir);
    return ok;
}
static XXFC_MAYBE_UNUSED bool archive_carrier_hap(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                  xx_pd_struct *pd) {
    size_t p = 15;
    unsigned i, count = 0;
    (void)f;
    (void)at;
    if (n < 15)
        return false;
    for (i = 4; i < 15; ++i)
        if (b[i])
            return false;
    while (p < n) {
        uint32_t packed, raw;
        uint8_t method;
        if (carrier_stop(pd) || ++count > ARCHIVE_CARRIER_COUNT || !archive_carrier_range(n, p, 40) ||
            xx_data_get_u32(b + p, 4, 0, false) != 0x574a688e || b[p + 16] || !b[p + 26] ||
            !xx_rt_memchr(b + p + 26, 0, 13))
            return false;
        packed = xx_data_get_u32(b + p + 4, 4, 0, false);
        raw = xx_data_get_u32(b + p + 22, 4, 0, false);
        method = b[p + 39];
        if (packed > INT32_MAX || raw > INT32_MAX || (method != 0x15 && method != 0x16) ||
            !archive_carrier_range(n, p + 40, packed))
            return false;
        if (method == 0x15 && (packed != raw || !executable_carrier_crc_checked(
                                                    b + p + 40, packed, xx_data_get_u32(b + p + 8, 4, 0, false), pd)))
            return false;
        p += 40 + packed;
    }
    return count != 0;
}
static XXFC_MAYBE_UNUSED bool archive_carrier_zoo(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                  xx_pd_struct *pd) {
    carrier_extent ranges[ARCHIVE_CARRIER_COUNT * 2 + 2];
    size_t rc = 0, p;
    unsigned count = 0;
    uint32_t first;
    (void)f;
    (void)at;
    if (n < 34 || xx_data_get_u32(b + 20, 4, 0, false) != 0xfdc4a7dc ||
        (first = xx_data_get_u32(b + 24, 4, 0, false)) < 34 || first + xx_data_get_u32(b + 28, 4, 0, false) != 0)
        return false;
    ranges[rc].lo = 0;
    ranges[rc].hi = ranges[rc].lo + (34);
    ++rc;
    p = first;
    for (;;) {
        size_t hs = 51;
        uint32_t next, data, packed, raw;
        if (carrier_stop(pd) || ++count > ARCHIVE_CARRIER_COUNT || !archive_carrier_range(n, p, 51) ||
            xx_data_get_u32(b + p, 4, 0, false) != 0xfdc4a7dc || (b[p + 4] != 1 && b[p + 4] != 2) || b[p + 5] > 2)
            return false;
        next = xx_data_get_u32(b + p + 6, 4, 0, false);
        data = xx_data_get_u32(b + p + 10, 4, 0, false);
        raw = xx_data_get_u32(b + p + 20, 4, 0, false);
        packed = xx_data_get_u32(b + p + 24, 4, 0, false);
        if (b[p + 4] == 2) {
            if (!archive_carrier_range(n, p, 53))
                return false;
            hs = 53U + xx_data_get_u16(b + p + 51, 2, 0, false);
            if (!archive_carrier_range(n, p, hs))
                return false;
            if (hs >= 58 && 58U + b[p + 56] + b[p + 57] > hs)
                return false;
        }
        ranges[rc].lo = (int64_t)p;
        ranges[rc].hi = ranges[rc].lo + ((int64_t)hs);
        ++rc;
        if (!next) {
            if (data || packed || raw)
                return false;
            break;
        }
        if (!b[p + 38] || !data || !archive_carrier_range(n, data, packed) || (b[p + 5] == 0 && raw != packed) ||
            !archive_carrier_range(n, xx_data_get_u32(b + p + 32, 4, 0, false),
                                   xx_data_get_u16(b + p + 36, 2, 0, false)))
            return false;
        if (packed) {
            ranges[rc].lo = data;
            ranges[rc].hi = ranges[rc].lo + (packed);
            ++rc;
        }
        p = next;
    }
    return count > 1 && carrier_extents(ranges, rc, pd);
}
static XXFC_MAYBE_UNUSED bool archive_carrier_cazip(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                    xx_pd_struct *pd) {
    uint8_t *raw;
    size_t used = 0, wrote = 0;
    bool ok = false;
    (void)f;
    (void)at;
    if (n <= 22 || b[8] < '0' || b[8] > '9' || b[9] < '0' || b[9] > '9' || xx_data_get_u16(b + 10, 2, 0, false) != 1 ||
        xx_data_get_u16(b + 12, 2, 0, false) != 1 || b[18] || b[19] || b[20] > 1 || b[21] < 4 || b[21] > 6 ||
        carrier_stop(pd))
        return false;
    if (!xx_dcl_scan_memory(b + 20, n - 20, ARCHIVE_CARRIER_LIMIT, &used, &wrote) || used != n - 20 ||
        wrote > ARCHIVE_CARRIER_LIMIT || carrier_stop(pd)) {
        return false;
    }
    raw = (uint8_t *)xx_mem_alloc(wrote ? wrote : 1);
    if (!raw)
        return false;
    if (xx_dcl_decode_memory(b + 20, n - 20, raw, wrote, &wrote) && used == n - 20 && !carrier_stop(pd) &&
        executable_carrier_crc_checked(raw, wrote, xx_data_get_u32(b + 14, 4, 0, false), pd))
        ok = true;
    xx_mem_free(raw);
    return ok;
}
/* The extent table holds up to 2*W7_COUNT+1 entries (128 KiB), so it lives
 * on the heap: this validator runs beneath the content detector, whose own
 * frame already takes most of a 1 MiB thread stack. */
static bool archive_carrier_tgcf_walk(const uint8_t *b, size_t n, carrier_extent *ranges, xx_pd_struct *pd) {
    size_t p, initial;
    bool extended;
    size_t rc = 0;
    unsigned count = 0;
    uint16_t version, name;
    if (n < 28)
        return false;
    version = xx_data_get_u16(b + 6, 2, 0, true);
    extended = version == 0x160;
    if (version != 0x130 && version != 0x140 && !extended)
        return false;
    name = xx_data_get_u16(b + 26, 2, 0, true);
    if (!name || name > 1024 || !archive_carrier_range(n, 28, name + 4U + (extended ? 4U : 0U)))
        return false;
    initial = 28U + name + 4U + (extended ? 4U : 0U);
    p = extended ? xx_data_get_u32(b + 28 + name, 4, 0, true) : initial;
    if (p < initial || p > n)
        return false;
    ranges[rc].lo = 0;
    ranges[rc].hi = ranges[rc].lo + ((int64_t)initial);
    ++rc;
    while (p < n) {
        size_t q, data;
        uint32_t packed, raw;
        uint16_t method;
        if (n - p == 10 && !xx_rt_memcmp(b + p, "TGCF", 4) && xx_data_get_u32(b + p + 4, 4, 0, true) > 0 &&
            xx_data_get_u32(b + p + 4, 4, 0, true) <= ARCHIVE_CARRIER_COUNT) {
            p = n;
            break;
        }
        if (carrier_stop(pd) || ++count > ARCHIVE_CARRIER_COUNT || !archive_carrier_range(n, p, 36) ||
            xx_rt_memcmp(b + p, "TGCF", 4) || xx_data_get_u16(b + p + 12, 2, 0, false) == 2)
            return false;
        method = xx_data_get_u16(b + p + 14, 2, 0, false);
        packed = xx_data_get_u32(b + p + 20, 4, 0, true);
        raw = xx_data_get_u32(b + p + 24, 4, 0, true);
        if (method != 0 && method != 4)
            return false;
        q = p + 36;
        if (!archive_carrier_string(b, n, &q, 1024) || !archive_carrier_string(b, n, &q, 1024) ||
            !archive_carrier_range(n, q, 5U + (extended ? 4U : 0U)))
            return false;
        ++q;
        data = extended ? xx_data_get_u32(b + q, 4, 0, true) : q + 4;
        if (extended)
            q += 4;
        q += 4;
        if (!archive_carrier_range(n, data, packed) || (method == 0 && packed != raw))
            return false;
        ranges[rc].lo = (int64_t)p;
        ranges[rc].hi = ranges[rc].lo + ((int64_t)(q - p));
        ++rc;
        if (packed) {
            ranges[rc].lo = (int64_t)data;
            ranges[rc].hi = ranges[rc].lo + (packed);
            ++rc;
        }
        p = extended ? q : data + packed;
    }
    return count && carrier_extents(ranges, rc, pd);
}
static XXFC_MAYBE_UNUSED bool archive_carrier_tgcf(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                   xx_pd_struct *pd) {
    carrier_extent *ranges;
    bool ok;
    (void)f;
    (void)at;
    if (n < 28)
        return false;
    ranges = (carrier_extent *)xx_mem_alloc((ARCHIVE_CARRIER_COUNT * 2U + 1U) * sizeof(*ranges));
    if (!ranges)
        return false;
    ok = archive_carrier_tgcf_walk(b, n, ranges, pd);
    xx_mem_free(ranges);
    return ok;
}
static bool archive_carrier_var(const uint8_t *b, size_t n, size_t *p, uint64_t *v) {
    unsigned i;
    uint64_t r = 0;
    for (i = 0; i < 9 && *p < n; ++i) {
        uint8_t c = b[(*p)++];
        r = (r << 7) | (c & 127);
        if (c & 128) {
            *v = r;
            return true;
        }
    }
    return false;
}
static bool archive_carrier_col(const uint8_t *b, size_t n, size_t *p, uint64_t *size, uint64_t *pos) {
    *pos = 0;
    return archive_carrier_var(b, n, p, size) && (!*size || archive_carrier_var(b, n, p, pos));
}
static bool archive_carrier_prop(const uint8_t *b, size_t n, size_t *p) {
    uint64_t size, pos;
    if (!archive_carrier_col(b, n, p, &size, &pos))
        return false;
    if (size && !archive_carrier_col(b, n, p, &size, &pos))
        return false;
    return archive_carrier_col(b, n, p, &size, &pos);
}
static Abstractformat *archive_carrier_starkit_open(xx_io_device *d, int64_t b) {
    xx_starkit *r = xx_starkit_create(d, b);
    return r ? &r->format : NULL;
}
static void archive_carrier_starkit_close(Abstractformat *f) { xx_starkit_free((xx_starkit *)f); }
static XXFC_MAYBE_UNUSED bool archive_carrier_starkit(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                      xx_pd_struct *pd) {
    static const char schema[] = "dirs[name:S,parent:I,files[name:S,size:I,date:I,contents:B]]";
    size_t p, q;
    uint64_t root, size, pos, value, dirs, views, viewsize, rows, total = 0, i;
    if (n < 24 || xx_data_get_u32(b + 4, 4, 0, true) != n || xx_data_get_u32(b + n - 16, 4, 0, true) != 0x80000000 ||
        xx_data_get_u32(b + n - 12, 4, 0, true) != n - 16 || !(xx_data_get_u32(b + n - 8, 4, 0, true) & 0x80000000))
        return false;
    size = xx_data_get_u32(b + n - 8, 4, 0, true) & 0x7fffffff;
    root = xx_data_get_u32(b + n - 4, 4, 0, true);
    if (size > 4096 || !archive_carrier_range(n - 16, root, size) || size < 2 + sizeof(schema) - 1 || b[root] != 128 ||
        b[root + 1] != (128 + sizeof(schema) - 1) || xx_rt_memcmp(b + root + 2, schema, sizeof(schema) - 1))
        return false;
    p = (size_t)root + 2 + sizeof(schema) - 1;
    if (!archive_carrier_var(b, (size_t)(root + size), &p, &value) || value != 1 ||
        !archive_carrier_col(b, (size_t)(root + size), &p, &size, &pos) || !size || size > 65536 ||
        !archive_carrier_range(n - 16, pos, size))
        return false;
    p = (size_t)pos;
    q = (size_t)(pos + size);
    if (!archive_carrier_var(b, q, &p, &value) || value || !archive_carrier_var(b, q, &p, &dirs) || !dirs ||
        dirs > 256 || !archive_carrier_prop(b, q, &p) || !archive_carrier_col(b, q, &p, &size, &pos) ||
        !archive_carrier_col(b, q, &p, &viewsize, &views) || p != q || viewsize > 1048576 ||
        !archive_carrier_range(n - 16, views, viewsize))
        return false;
    p = (size_t)views;
    q = (size_t)(views + viewsize);
    for (i = 0; i < dirs; ++i) {
        if (carrier_stop(pd) || !archive_carrier_var(b, q, &p, &value) || value ||
            !archive_carrier_var(b, q, &p, &rows) || rows > ARCHIVE_CARRIER_COUNT - total)
            return false;
        total += rows;
        if (rows && (!archive_carrier_prop(b, q, &p) || !archive_carrier_col(b, q, &p, &size, &pos) ||
                     !archive_carrier_col(b, q, &p, &size, &pos) || !archive_carrier_prop(b, q, &p)))
            return false;
    }
    if (p != q || !total)
        return false;
    return archive_carrier_nested(f, at, archive_carrier_starkit_open, archive_carrier_starkit_close, (uint32_t)total,
                                  pd);
}
static XXFC_MAYBE_UNUSED bool archive_carrier_alz(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                  xx_pd_struct *pd) {
    size_t p = 8;
    unsigned count = 0;
    (void)f;
    (void)at;
    if (n < 12 || xx_rt_memcmp(b, "ALZ\1", 4))
        return false;
    while (p < n) {
        uint32_t magic;
        size_t name, width;
        uint64_t packed = 0, raw = 0;
        if (carrier_stop(pd) || !archive_carrier_range(n, p, 4))
            return false;
        magic = xx_data_get_u32(b + p, 4, 0, false);
        p += 4;
        if (magic == 0x015a4c43) {
            if (!archive_carrier_range(n, p, 8))
                return false;
            p += 8;
            continue;
        }
        if (magic == 0x025a4c43)
            return count && p == n;
        if (magic != 0x015a4c42 || ++count > ARCHIVE_CARRIER_COUNT || !archive_carrier_range(n, p, 9))
            return false;
        name = xx_data_get_u16(b + p, 2, 0, false);
        width = b[p + 7] >> 4;
        if ((b[p + 7] & 15) || !name || name > 4096 ||
            (width != 0 && width != 1 && width != 2 && width != 4 && width != 8))
            return false;
        p += 9;
        if (width) {
            unsigned i;
            if (!archive_carrier_range(n, p, 6 + 2 * width) || b[p] > 2 || b[p + 1])
                return false;
            for (i = 0; i < width; ++i) {
                packed |= (uint64_t)b[p + 6 + i] << (8 * i);
                raw |= (uint64_t)b[p + 6 + width + i] << (8 * i);
            }
            if (!b[p] && packed != raw)
                return false;
            p += 6 + 2 * width;
        }
        if (!archive_carrier_range(n, p, name) || !archive_carrier_range(n, p + name, packed))
            return false;
        p += name + (size_t)packed;
    }
    return false;
}
static Abstractformat *archive_carrier_chm_open(xx_io_device *d, int64_t b) {
    xx_chm *r = xx_chm_create(d, b);
    return r ? &r->format : NULL;
}
static void archive_carrier_chm_close(Abstractformat *f) { xx_chm_free((xx_chm *)f); }
static XXFC_MAYBE_UNUSED bool archive_carrier_chm(Abstractformat *f, int64_t at, const uint8_t *b, size_t n,
                                                  xx_pd_struct *pd) {
    uint64_t section, dir, size, content;
    uint32_t chunk, count, i;
    size_t total = 0;
    carrier_extent ext[3];
    if (n < 96 || xx_data_get_u32(b + 4, 4, 0, false) != 3 || xx_data_get_u32(b + 8, 4, 0, false) != 96)
        return false;
    section = xx_data_get_u64(b + 56, 8, 0, false);
    dir = xx_data_get_u64(b + 72, 8, 0, false);
    size = xx_data_get_u64(b + 80, 8, 0, false);
    content = xx_data_get_u64(b + 88, 8, 0, false);
    if (!archive_carrier_range(n, section, 24) || xx_data_get_u64(b + 64, 8, 0, false) != 24 ||
        xx_data_get_u32(b + (size_t)section, 4, 0, false) != 0x1fe ||
        xx_data_get_u64(b + (size_t)section + 8, 8, 0, false) != n || size > 262144 || size < 84 ||
        !archive_carrier_range(n, dir, size) || content < dir + size || content > n ||
        xx_rt_memcmp(b + (size_t)dir, "ITSP", 4) || xx_data_get_u32(b + (size_t)dir + 8, 4, 0, false) != 84)
        return false;
    chunk = xx_data_get_u32(b + (size_t)dir + 16, 4, 0, false);
    count = xx_data_get_u32(b + (size_t)dir + 44, 4, 0, false);
    if (chunk < 32 || chunk > 65536 || !count || count > 4096 || 84 + (uint64_t)count * chunk != size)
        return false;
    ext[0].lo = 0;
    ext[0].hi = 96;
    ext[1].lo = (int64_t)section;
    ext[1].hi = ext[1].lo + 24;
    ext[2].lo = (int64_t)dir;
    ext[2].hi = ext[2].lo + (int64_t)size;
    if (!carrier_extents(ext, 3, pd))
        return false;
    for (i = 0; i < count; ++i) {
        size_t p = (size_t)dir + 84 + (size_t)i * chunk;
        if (carrier_stop(pd))
            return false;
        if (!xx_rt_memcmp(b + p, "PMGL", 4)) {
            uint32_t free = xx_data_get_u32(b + p + 4, 4, 0, false);
            if (free < 2 || free > chunk - 20)
                return false;
            total += xx_data_get_u16(b + p + chunk - 2, 2, 0, false);
            if (total > ARCHIVE_CARRIER_COUNT)
                return false;
        } else if (xx_rt_memcmp(b + p, "PMGI", 4))
            return false;
    }
    return total && archive_carrier_nested(f, at, archive_carrier_chm_open, archive_carrier_chm_close, 0, pd);
}
#endif
