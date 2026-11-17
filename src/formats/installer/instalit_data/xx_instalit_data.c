/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * Layout: XArchive/core/xlegacystorearchive.cpp FT_INSTALIT_DATA branch.
 * Volumes with a trailer and a "[PVM]" directory are read through the directory
 * (detected automatically); footerless media fall back to the explicitly
 * selected DCL stream walk below: a raw DCL prefix does not identify its producer. */
#include "xxfclib/formats/installer/instalit_data/xx_instalit_data.h"
#include "xxfclib/formats/archive/dclraw/xx_dclraw.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "../../xx_payload_members.h"
#define ID_MAX_INPUT (INT64_C(128) * 1024 * 1024)
#define ID_MAX_OUTPUT (UINT64_C(512) * 1024 * 1024)
#define ID_MAX_MEMBERS 65536U
#define ID_WINDOW 65536U
typedef struct id_name {
    int64_t offset;
    uint32_t run[2];
    char name[13];
} id_name;
static bool id_dos_char(unsigned c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || xx_rt_strchr("_$~!#%&(){}@'`^-", (int)c) != NULL;
}
static bool id_name_at(const uint8_t *p, size_t n, char *out)
{
    size_t i, length = 0;
    int dot = -1;
    if (n < 3 || p[0] != 0xf0) return false;
    while (length < 12 && length + 1 < n && p[length + 1]) ++length;
    if (!length || length + 1 >= n || p[length + 1]) return false;
    for (i = 0; i < length; ++i) {
        unsigned c = p[i + 1];
        if (c == '.') {
            if (dot >= 0 || i == 0 || i + 1 == length) return false;
            dot = (int)i;
        } else if (!id_dos_char(c)) return false;
        out[i] = (char)c;
    }
    if ((dot < 0 && length > 8) || dot > 8 || (dot >= 0 && length - (size_t)dot - 1 > 3)) return false;
    out[length] = 0;
    return true;
}
static size_t id_find(const id_name *names, size_t count, int64_t offset)
{
    size_t low = 0, high = count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (names[mid].offset < offset) low = mid + 1;
        else high = mid;
    }
    return low < count && names[low].offset == offset ? low : count;
}
static bool id_manifest(Abstractformat *f, pm_stream *s, int64_t start, int64_t total, xx_instalit_data_info *info, xx_pd_struct *pd)
{
    uint8_t *window = NULL;
    id_name *names = NULL;
    size_t count = 0, capacity = 0, best = 0;
    unsigned best_stride = 0;
    uint32_t best_count = 0;
    int64_t at = start;
    bool valid = false;
    if (start == total) return true;
    window = (uint8_t *)xx_mem_alloc(ID_WINDOW + 13U);
    if (!window) goto done;
    while (at < total) {
        size_t n = (uint64_t)(total - at) > ID_WINDOW ? ID_WINDOW : (size_t)(total - at);
        size_t read = n + (total - at - (int64_t)n > 13 ? 13 : (size_t)(total - at - (int64_t)n));
        size_t i;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, at, window, read)) goto done;
        for (i = 0; i < n; ++i) {
            char name[13];
            id_name *item;
            if (window[i] != 0xf0 || !id_name_at(window + i, read - i, name)) continue;
            if (count >= ID_MAX_MEMBERS) goto done;
            if (count == capacity) {
                size_t next = capacity ? capacity * 2 : 16;
                void *grown = xx_mem_realloc(names, next * sizeof(*names));
                if (!grown) goto done;
                names = (id_name *)grown;
                capacity = next;
            }
            item = &names[count++];
            item->offset = at + (int64_t)i;
            xx_mem_copy(item->name, name, xx_rt_strlen(name) + 1);
            item->run[0] = item->run[1] = 1;
        }
        at += (int64_t)n;
    }
    /* Dynamic programming over sorted candidate positions avoids quadratic
     * rescans of a long regular manifest. Equal-length runs select the earliest
     * marker, then 55-byte stride, making the choice reproducible. */
    for (size_t i = count; i > 0;) {
        size_t index = --i;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        for (unsigned k = 0; k < 2; ++k) {
            unsigned stride = k ? 59 : 55;
            size_t next = id_find(names, count, names[index].offset + stride);
            if (next < count) names[index].run[k] += names[next].run[k];
            if (names[index].run[k] > best_count || (names[index].run[k] == best_count && (!best_count || names[index].offset < names[best].offset ||
                                                                                           (names[index].offset == names[best].offset && stride < best_stride)))) {
                best = index;
                best_stride = stride;
                best_count = names[index].run[k];
            }
        }
    }
    if (best_count) {
        size_t last = id_find(names, count, names[best].offset + (int64_t)(best_count - 1) * best_stride);
        info->manifest_offset = f->base_address + names[best].offset;
        info->manifest_name_end = f->base_address + names[last].offset + 2 + (int64_t)xx_rt_strlen(names[last].name);
        info->manifest_stride = best_stride;
        info->manifest_run = best_count;
        if (best_count >= s->count) {
            for (size_t i = 0; i < s->count; ++i) {
                size_t index = id_find(names, count, names[best].offset + (int64_t)i * best_stride);
                char *name = xx_str_dup(names[index].name);
                if (!name) goto done;
                xx_str_free(s->items[i].display_name);
                s->items[i].display_name = name;
            }
            info->named_streams = s->count;
        }
    }
    valid = true;
done:
    xx_mem_free(window);
    xx_mem_free(names);
    return valid;
}
static bool id_read_all(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    uint8_t *packed = NULL, *plain = NULL;
    size_t at = 0, written = 0;
    bool valid = false;
    const xx_var *limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (m->packed_size < 4 || m->size < 0 || (uint64_t)m->packed_size > ID_MAX_INPUT || (uint64_t)m->size > ID_MAX_OUTPUT ||
        (limit && (uint64_t)m->packed_size + (uint64_t)m->size > xx_var_get_u64(limit)) || (pd && xx_pd_is_stopped(pd)))
        return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)m->packed_size);
    plain = (uint8_t *)xx_mem_alloc(m->size ? (size_t)m->size : 1);
    if (!packed || !plain) goto done;
    while (at < (size_t)m->packed_size) {
        size_t n = (size_t)m->packed_size - at;
        if (n > ID_WINDOW) n = ID_WINDOW;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, m->offset - f->base_address + (int64_t)at, packed + at, n)) goto done;
        at += n;
    }
    if (!xx_dcl_decode_memory(packed, (size_t)m->packed_size, plain, (size_t)m->size, &written) || written != (size_t)m->size) goto done;
    for (at = 0; out && at < written;) {
        size_t n = written - at;
        ssize_t count;
        if (n > ID_WINDOW) n = ID_WINDOW;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        count = xx_io_write(out, plain + at, n);
        if (count <= 0 || (size_t)count > n) goto done;
        at += (size_t)count;
    }
    valid = !(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return valid;
}
/* Volumes with a directory (the common case). A 12-byte trailer at the end of
 * the file holds i32 directory end, i32 0, i32 directory start; the directory
 * starts with 05 "[PVM]" and holds one record per member stored in this
 * volume, while the area between the directory end and the trailer is the
 * installer script. Records (little-endian):
 *   +0x00 u8 tag: 0x1C (0x37-byte records) or 0x1A (0x2F-byte records); the
 *              first record's tag selects the layout
 *   +0x01 i32 absolute data offset   +0x06 char name[13] (12 used)
 *   +0x14 i32 packed size            +0x18 u16 DOS date, +0x1A u16 DOS time
 *   +0x1C u32 CRC-32 (0x37 layout) or u16 CRC-16/ARC (0x2F layout)
 *   +0x23 / +0x21 i32 unpacked size
 * 0x37-layout members are PKWARE DCL implode streams. 0x2F-layout members use
 * an older LZ77 coder: LSB-first bits; u16 n (2..0x275) and u8 w (< 11), then
 * n tree entries of w bits (a value v >= n is the leaf for symbol v - n); a
 * symbol is decoded from node n - 2, each bit added to the node index before
 * following the entry. Symbols below 0x100 are literals, 0x100 ends, others
 * copy symbol - 0xFE bytes; the distance's high part comes from a 256-entry
 * prefix table indexed by the next 8 bits (consuming 3..8 of them), its low
 * part has 7 bits, or fewer while the 8 KiB window (filled with spaces) is
 * still being filled for the first time: bit_length((pos + 0x3C) >> 5) - 1.
 * Every member is checked against its stored CRC. */
static const uint8_t id_b_high[256] = {
    0x00, 0x04, 0x01, 0x18, 0x02, 0x0c, 0x05, 0x19, 0x00, 0x0d, 0x06, 0x1a, 0x03, 0x0e, 0x07, 0x30, 0x00, 0x08, 0x01, 0x1b, 0x02, 0x0f, 0x09, 0x1c, 0x00, 0x10, 0x0a, 0x1d, 0x03, 0x11, 0x0b, 0x31,
    0x00, 0x04, 0x01, 0x1e, 0x02, 0x12, 0x05, 0x1f, 0x00, 0x13, 0x06, 0x20, 0x03, 0x14, 0x07, 0x32, 0x00, 0x08, 0x01, 0x21, 0x02, 0x15, 0x09, 0x22, 0x00, 0x16, 0x0a, 0x23, 0x03, 0x17, 0x0b, 0x33,
    0x00, 0x04, 0x01, 0x24, 0x02, 0x0c, 0x05, 0x25, 0x00, 0x0d, 0x06, 0x26, 0x03, 0x0e, 0x07, 0x34, 0x00, 0x08, 0x01, 0x27, 0x02, 0x0f, 0x09, 0x28, 0x00, 0x10, 0x0a, 0x29, 0x03, 0x11, 0x0b, 0x35,
    0x00, 0x04, 0x01, 0x2a, 0x02, 0x12, 0x05, 0x2b, 0x00, 0x13, 0x06, 0x2c, 0x03, 0x14, 0x07, 0x36, 0x00, 0x08, 0x01, 0x2d, 0x02, 0x15, 0x09, 0x2e, 0x00, 0x16, 0x0a, 0x2f, 0x03, 0x17, 0x0b, 0x37,
    0x00, 0x04, 0x01, 0x18, 0x02, 0x0c, 0x05, 0x19, 0x00, 0x0d, 0x06, 0x1a, 0x03, 0x0e, 0x07, 0x38, 0x00, 0x08, 0x01, 0x1b, 0x02, 0x0f, 0x09, 0x1c, 0x00, 0x10, 0x0a, 0x1d, 0x03, 0x11, 0x0b, 0x39,
    0x00, 0x04, 0x01, 0x1e, 0x02, 0x12, 0x05, 0x1f, 0x00, 0x13, 0x06, 0x20, 0x03, 0x14, 0x07, 0x3a, 0x00, 0x08, 0x01, 0x21, 0x02, 0x15, 0x09, 0x22, 0x00, 0x16, 0x0a, 0x23, 0x03, 0x17, 0x0b, 0x3b,
    0x00, 0x04, 0x01, 0x24, 0x02, 0x0c, 0x05, 0x25, 0x00, 0x0d, 0x06, 0x26, 0x03, 0x0e, 0x07, 0x3c, 0x00, 0x08, 0x01, 0x27, 0x02, 0x0f, 0x09, 0x28, 0x00, 0x10, 0x0a, 0x29, 0x03, 0x11, 0x0b, 0x3d,
    0x00, 0x04, 0x01, 0x2a, 0x02, 0x12, 0x05, 0x2b, 0x00, 0x13, 0x06, 0x2c, 0x03, 0x14, 0x07, 0x3e, 0x00, 0x08, 0x01, 0x2d, 0x02, 0x15, 0x09, 0x2e, 0x00, 0x16, 0x0a, 0x2f, 0x03, 0x17, 0x0b, 0x3f};
static unsigned id_b_bits(unsigned high)
{
    return high < 1U ? 3U : high < 4U ? 4U : high < 12U ? 5U : high < 24U ? 6U : high < 48U ? 7U : 8U;
}
typedef struct id_bits {
    const uint8_t *p;
    size_t n, at;
    uint32_t acc;
    unsigned cnt;
} id_bits;
static bool id_peek(id_bits *b, unsigned n, uint32_t *v)
{
    while (b->cnt < n) {
        if (b->at >= b->n) return false;
        b->acc |= (uint32_t)b->p[b->at++] << b->cnt;
        b->cnt += 8U;
    }
    *v = b->acc & ((1U << n) - 1U);
    return true;
}
static bool id_get(id_bits *b, unsigned n, uint32_t *v)
{
    if (!n) {
        *v = 0;
        return true;
    }
    if (!id_peek(b, n, v)) return false;
    b->acc >>= n;
    b->cnt -= n;
    return true;
}
static bool id_codec_b(const uint8_t *in, size_t packed, uint8_t *out, size_t size, xx_pd_struct *pd)
{
    uint16_t tree[0x276];
    uint8_t win[0x2000];
    uint32_t n, w, v, i, p = 0;
    size_t at = 0;
    bool wrapped = false;
    id_bits b;
    b.p = in;
    b.n = packed;
    b.at = 0;
    b.acc = 0;
    b.cnt = 0;
    if (!id_get(&b, 16, &n) || n < 2U || n >= 0x276U || !id_get(&b, 8, &w) || w >= 11U) return false;
    for (i = 0; i < n; ++i) {
        if (!id_get(&b, w, &v)) return false;
        tree[i] = (uint16_t)(v >= n ? ((v - n) | 0x8000U) : v);
    }
    xx_rt_memset(win, 0x20, sizeof(win));
    for (;;) {
        uint32_t s = n - 2U, bit, high, low, k, length, src;
        unsigned guard = 0;
        if ((at & 0xFFFFU) == 0 && pd && xx_pd_is_stopped(pd)) return false;
        while (!(s & 0x8000U)) {
            if (++guard > 64U || !id_get(&b, 1, &bit)) return false;
            s += bit;
            if (s >= n) return false;
            s = tree[s];
        }
        s &= 0x7FFFU;
        if (s < 0x100U) {
            if (at >= size) return false;
            out[at++] = win[p] = (uint8_t)s;
            p = (p + 1U) & 0x1FFFU;
            if (!p) wrapped = true;
            continue;
        }
        if (s == 0x100U) break;
        length = s - 0xFEU;
        if (!id_peek(&b, 8, &v)) return false;
        high = id_b_high[v];
        if (!id_get(&b, id_b_bits(high), &v)) return false;
        if (wrapped || p + 0x3CU > 0x1FFFU) k = 7;
        else {
            uint32_t x = (p + 0x3CU) >> 5;
            k = 0;
            while (x >> (k + 1U)) ++k;
        }
        if (!id_get(&b, k, &low)) return false;
        src = (p - ((high << k) + low + 1U)) & 0x1FFFU;
        if (length > size - at) return false;
        while (length--) {
            uint8_t c = win[src];
            src = (src + 1U) & 0x1FFFU;
            out[at++] = win[p] = c;
            p = (p + 1U) & 0x1FFFU;
            if (!p) wrapped = true;
        }
    }
    return at == size;
}
static uint16_t id_crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0;
    size_t i;
    unsigned k;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (k = 0; k < 8U; ++k) c = (uint16_t)((c >> 1) ^ ((c & 1U) ? 0xA001U : 0));
    }
    return c;
}
static uint32_t id_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFU;
    size_t i;
    unsigned k;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (k = 0; k < 8U; ++k) c = (c >> 1) ^ ((c & 1U) ? UINT32_C(0xEDB88320) : 0);
    }
    return ~c;
}
/* Trailer + directory check without decoding; *start/*end/*b receive the
 * directory span and layout. */
static bool id_directory_header(Abstractformat *f, int64_t total, uint32_t *start, uint32_t *end, bool *layout_b)
{
    uint8_t t[12], m[7];
    int32_t dir_end, zero, dir_start;
    if (total < 19 || total > ID_MAX_INPUT || !pm_read(f, total - 12, t, 12)) return false;
    dir_end = (int32_t)xx_data_get_u32(t, 4, 0, false);
    zero = (int32_t)xx_data_get_u32(t + 4, 4, 0, false);
    dir_start = (int32_t)xx_data_get_u32(t + 8, 4, 0, false);
    if (dir_end <= 0 || dir_end >= total || zero || dir_start <= 0 || dir_start >= dir_end || !pm_read(f, dir_start, m, 7) || xx_rt_memcmp(m, "\x05[PVM]", 6)) return false;
    *start = (uint32_t)dir_start + 6U;
    *end = (uint32_t)dir_end;
    *layout_b = m[6] < 0x1BU;
    return m[6] == 0x1AU || m[6] == 0x1CU;
}
static bool id_directory(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t total = pm_available(f);
    uint32_t at, end, rec;
    bool layout_b;
    uint8_t r[0x37];
    if (!id_directory_header(f, total, &at, &end, &layout_b)) return false;
    rec = layout_b ? 0x2FU : 0x37U;
    while (at < end && s->count < ID_MAX_MEMBERS) {
        int32_t offset, packed, size;
        uint8_t *in, *out;
        char name[16];
        unsigned i;
        bool ok;
        pm_member *m;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if ((int64_t)at + rec > total || !pm_read(f, at, r, rec) || r[0] != (layout_b ? 0x1AU : 0x1CU)) break;
        offset = (int32_t)xx_data_get_u32(r + 1, 4, 0, false);
        packed = (int32_t)xx_data_get_u32(r + 0x14, 4, 0, false);
        size = (int32_t)xx_data_get_u32(r + (layout_b ? 0x21 : 0x23), 4, 0, false);
        if (offset < 0 || packed < 0 || size < 0) break;
        if ((int64_t)offset + packed > total || (uint64_t)size > ID_MAX_OUTPUT) return false;
        for (i = 0; i < 12U && r[6 + i]; ++i) name[i] = (char)(r[6 + i] > 0x20U && r[6 + i] < 0x7FU && r[6 + i] != '/' && r[6 + i] != 0x5CU && r[6 + i] != ':' ? r[6 + i] : '_');
        name[i] = 0;
        if (!i) return false;
        in = (uint8_t *)xx_mem_alloc((size_t)packed + 1U);
        out = (uint8_t *)xx_mem_alloc((size_t)size + 1U);
        ok = in && out && pm_read(f, offset, in, (size_t)packed);
        if (ok && layout_b) ok = id_codec_b(in, (size_t)packed, out, (size_t)size, pd) && id_crc16(out, (size_t)size) == xx_data_get_u16(r + 0x1C, 2, 0, false);
        else if (ok) {
            size_t written = 0;
            ok = (!size || (xx_dcl_decode_memory_zero_history(in, (size_t)packed, out, (size_t)size, &written) && written == (size_t)size)) &&
                id_crc32(out, (size_t)size) == xx_data_get_u32(r + 0x1C, 4, 0, false);
        }
        xx_mem_free(in);
        if (!ok || !pm_add(f, s, name, offset, packed)) {
            xx_mem_free(out);
            if (pd && !pd->last_error) xx_pd_set_error(pd, 1, "Instalit member does not decode or fails its CRC");
            return false;
        }
        m = &s->items[s->count - 1];
        m->memory = out;
        m->size = size;
        m->packed_size = packed;
        m->compression_method = layout_b ? 2U : 1U;
        xx_rt_snprintf(m->name, sizeof(m->name), "%s", name);
        at += rec;
    }
    if (!s->count) return false;
    s->size = total;
    return true;
}
bool xx_instalit_data_has_directory(xx_io_device *d, int64_t base)
{
    xx_instalit_data r;
    uint32_t start, end;
    bool layout_b, valid;
    xx_instalit_data_init(&r, d, base);
    valid = id_directory_header(&r.format, pm_available(&r.format), &start, &end, &layout_b);
    xx_instalit_data_destroy(&r);
    return valid;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    xx_instalit_data_info info;
    int64_t total = pm_available(f), at = 0;
    uint64_t raw_total = 0;
    uint8_t prelude[2];
    xx_mem_zero(&info, sizeof(info));
    info.manifest_offset = info.manifest_name_end = -1;
    if (id_directory(f, s, pd)) {
        info.complete_streams = info.named_streams = s->count;
        info.data_end = f->base_address + total;
        ((xx_instalit_data *)f)->info = info;
        return true;
    }
    if (s->count || (pd && pd->last_error)) return false;
    if (total < 4 || total > ID_MAX_INPUT || (pd && xx_pd_is_stopped(pd)) || !pm_read(f, 0, prelude, 2) || prelude[0] > 1 || prelude[1] < 4 || prelude[1] > 6)
        return false;
    while (at < total) {
        int64_t packed = 0;
        size_t raw = 0;
        char name[32];
        pm_member *m;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_dclraw_measure_stream(f->device, f->base_address + at, total - at, (size_t)(ID_MAX_OUTPUT - raw_total), &packed, &raw, pd)) break;
        if (packed < 4 || packed > total - at || raw > ID_MAX_OUTPUT - raw_total || s->count >= ID_MAX_MEMBERS) return false;
        if (!pm_add(f, s, "payload", at, packed)) return false;
        m = &s->items[s->count - 1];
        m->size = (int64_t)raw;
        m->compression_method = 1;
        m->read_all = id_read_all;
        xx_rt_snprintf(name, sizeof(name), "stream_%04u.bin", (unsigned)s->count);
        m->display_name = xx_str_dup(name);
        if (!m->display_name) return false;
        at += packed;
        raw_total += raw;
    }
    if (!s->count || (pd && xx_pd_is_stopped(pd))) return false;
    info.complete_streams = s->count;
    info.data_end = f->base_address + at;
    info.trailing_size = total - at;
    if (!id_manifest(f, s, at, total, &info, pd)) return false;
    ((xx_instalit_data *)f)->info = info;
    s->size = total;
    return !(pd && xx_pd_is_stopped(pd));
}
static void id_record(xx_archive_record_state *st)
{
    st->current_record.header_offset = st->current_record.data_offset;
}
static xx_archive_record_state *id_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_archive_record_state *st = pm_create_records(f, opts, pd);
    if (st && st->has_record) id_record(st);
    return st;
}
static bool id_next(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd)
{
    if (!pm_next(f, st, pd)) return false;
    id_record(st);
    return true;
}
void xx_instalit_data_init(xx_instalit_data *r, xx_io_device *d, int64_t base)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        r->info.manifest_offset = r->info.manifest_name_end = -1;
        pm_init(&r->format, d, base, XX_FILE_TYPE_INSTALIT_DATA, "001");
        r->format.endian = XX_ENDIAN_LITTLE;
        xx_format_set_mime_type(&r->format, "application/x-instalit-data");
        r->format.create_archive_records_reading = id_records;
        r->format.archive_record_move_to_next = id_next;
    }
}
xx_instalit_data *xx_instalit_data_create(xx_io_device *d, int64_t base)
{
    xx_instalit_data *r = (xx_instalit_data *)xx_mem_alloc(sizeof(*r));
    if (r) xx_instalit_data_init(r, d, base);
    return r;
}
void xx_instalit_data_destroy(xx_instalit_data *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_instalit_data_free(xx_instalit_data *r)
{
    if (r) {
        xx_instalit_data_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_instalit_data_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_instalit_data_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
bool xx_instalit_data_get_info(xx_instalit_data *r, xx_instalit_data_info *info, xx_pd_struct *pd)
{
    pm_stream *s;
    if (!info) return false;
    xx_mem_zero(info, sizeof(*info));
    info->manifest_offset = info->manifest_name_end = -1;
    if (!r || !(s = pm_open(&r->format, pd))) return false;
    pm_free_stream(s);
    *info = r->info;
    return true;
}
xx_file_type_t xx_instalit_data_detect(xx_io_device *d, int64_t base)
{
    xx_instalit_data r;
    bool valid;
    xx_instalit_data_init(&r, d, base);
    valid = pm_valid(&r.format, NULL);
    xx_instalit_data_destroy(&r);
    return valid ? XX_FILE_TYPE_INSTALIT_DATA : XX_FILE_TYPE_UNKNOWN;
}
