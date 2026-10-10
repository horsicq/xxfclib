/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://learn.microsoft.com/en-us/typography/opentype/spec/otff
 * TTC/OTC 1.0/2.0: per-font tables, checksums and safe names; no glyph rendering.
 */
#include "xxfclib/formats/sfnt_collection/xx_sfnt_collection.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool ttc_checksum(Abstractformat *f, uint32_t at, uint32_t size, uint32_t tag, uint32_t expected, xx_pd_struct *pd)
{
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *buffer;
    uint32_t sum = 0, offset = 0, word = 0;
    unsigned word_bytes = 0;
    if (!size) return expected == 0;
    if (capacity > size) capacity = size;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (offset < size) {
        size_t n = size - offset > capacity ? capacity : size - offset, i;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)at + offset, buffer, n)) {
            xx_mem_free(buffer);
            return false;
        }
        for (i = 0; i < n; ++i) {
            uint8_t byte = (tag == 0x68656164U && offset + i >= 8 && offset + i < 12) ? 0 : buffer[i];
            word = (word << 8) | byte;
            if (++word_bytes == 4) {
                sum += word;
                word = 0;
                word_bytes = 0;
            }
        }
        offset += (uint32_t)n;
    }
    if (word_bytes) sum += word << ((4 - word_bytes) * 8);
    xx_mem_free(buffer);
    return sum == expected;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[12], e[16];
    uint32_t version, fonts, i, j, k, header, dsig_at = 0, dsig_size = 0;
    int64_t end;
    uint32_t dirs[256], dir_sizes[256], tags[4096], offs[4096], sizes[4096], checks[4096], tables = 0;
    uint64_t processed = 0;
    if (!pm_read(f, 0, h, 12) || xx_rt_memcmp(h, "ttcf", 4)) return false;
    version = xx_data_get_u32(h + 4, 4, 0, true);
    fonts = xx_data_get_u32(h + 8, 4, 0, true);
    if ((version != 0x10000 && version != 0x20000) || !fonts || fonts > 256) return false;
    header = 12 + fonts * 4 + (version == 0x20000 ? 12 : 0);
    end = header;
    if (header > pm_available(f)) return false;
    if (version == 0x20000) {
        if (!pm_read(f, 12 + (int64_t)fonts * 4, e, 12)) return false;
        dsig_size = xx_data_get_u32(e + 4, 4, 0, true);
        dsig_at = xx_data_get_u32(e + 8, 4, 0, true);
        if (!xx_data_get_u32(e, 4, 0, true)) {
            if (dsig_size || dsig_at) return false;
        } else {
            if (xx_data_get_u32(e, 4, 0, true) != 0x44534947U || !dsig_size || dsig_at % 4 || dsig_at < header || dsig_at > pm_available(f) ||
                dsig_size > (uint64_t)(pm_available(f) - dsig_at))
                return false;
            end = (int64_t)dsig_at + dsig_size;
        }
    }
    for (i = 0; i < fonts; ++i) {
        uint32_t flavor, count, power = 1, log = 0;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 12 + (int64_t)i * 4, e, 4)) return false;
        dirs[i] = xx_data_get_u32(e, 4, 0, true);
        if (dirs[i] < header || dirs[i] % 4 || !pm_read(f, dirs[i], h, 12)) return false;
        flavor = xx_data_get_u32(h, 4, 0, true);
        count = xx_data_get_u16(h + 4, 2, 0, true);
        if ((flavor != 0x10000 && flavor != 0x4f54544fU && flavor != 0x74727565U && flavor != 0x74797031U) || !count || count > 4095) return false;
        while (power * 2 <= count) {
            power *= 2;
            ++log;
        }
        if (xx_data_get_u16(h + 6, 2, 0, true) != power * 16 || xx_data_get_u16(h + 8, 2, 0, true) != log ||
            xx_data_get_u16(h + 10, 2, 0, true) != count * 16 - power * 16)
            return false;
        dir_sizes[i] = 12 + count * 16;
        if (dirs[i] > pm_available(f) || dir_sizes[i] > (uint64_t)(pm_available(f) - dirs[i])) return false;
        for (j = 0; j < i; ++j)
            if (dirs[i] < (uint64_t)dirs[j] + dir_sizes[j] && dirs[j] < (uint64_t)dirs[i] + dir_sizes[i]) return false;
        if (dsig_size && (uint64_t)dirs[i] + dir_sizes[i] > dsig_at) return false;
        if ((int64_t)dirs[i] + dir_sizes[i] > end) end = (int64_t)dirs[i] + dir_sizes[i];
    }
    for (i = 0; i < fonts; ++i) {
        uint32_t count = (dir_sizes[i] - 12) / 16, previous = 0;
        if (tables + count > 4096) return false;
        for (j = 0; j < count; ++j) {
            uint32_t tag, off, size, checksum;
            unsigned c;
            bool space = false;
            char name[64];
            if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)dirs[i] + 12 + (int64_t)j * 16, e, 16)) return false;
            if (e[0] == ' ') return false;
            for (c = 0; c < 4; ++c) {
                if (e[c] < 32 || e[c] > 126 || (space && e[c] != ' ')) return false;
                if (e[c] == ' ') space = true;
            }
            tag = xx_data_get_u32(e, 4, 0, true);
            checksum = xx_data_get_u32(e + 4, 4, 0, true);
            off = xx_data_get_u32(e + 8, 4, 0, true);
            size = xx_data_get_u32(e + 12, 4, 0, true);
            if ((j && tag <= previous) || off < header || off % 4 || off > pm_available(f) || size > (uint64_t)(pm_available(f) - off)) return false;
            previous = tag;
            for (k = 0; k < fonts; ++k)
                if (size && off < (uint64_t)dirs[k] + dir_sizes[k] && dirs[k] < (uint64_t)off + size) return false;
            if (dsig_size && (uint64_t)off + size > dsig_at) return false;
            for (k = 0; k < tables; ++k) {
                if (!(k & 255) && pd && xx_pd_is_stopped(pd)) return false;
                if (size && sizes[k] && off < (uint64_t)offs[k] + sizes[k] && offs[k] < (uint64_t)off + size &&
                    (off != offs[k] || size != sizes[k] || tag != tags[k] || checksum != checks[k]))
                    return false;
            }
            processed += size;
            if (processed > 512U * 1024U * 1024U || !ttc_checksum(f, off, size, tag, checksum, pd)) return false;
            tags[tables] = tag;
            offs[tables] = off;
            sizes[tables] = size;
            checks[tables++] = checksum;
            xx_rt_snprintf(name, sizeof(name), "font-%u-table-%08x.bin", (unsigned)i, tag);
            if (!pm_add(f, s, name, off, size)) return false;
            if ((int64_t)off + size > end) end = (int64_t)off + size;
        }
    }
    if (dsig_size && !pm_add(f, s, "signature.dsig", dsig_at, dsig_size)) return false;
    if (end > 256 * 1024 * 1024) return false;
    s->size = end;
    return true;
}

void xx_sfnt_collection_init(xx_sfnt_collection *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFNT_COLLECTION, "ttc");
    }
}
xx_sfnt_collection *xx_sfnt_collection_create(xx_io_device *d, int64_t b)
{
    xx_sfnt_collection *r = (xx_sfnt_collection *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfnt_collection_init(r, d, b);
    return r;
}
void xx_sfnt_collection_destroy(xx_sfnt_collection *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfnt_collection_free(xx_sfnt_collection *r)
{
    if (r) {
        xx_sfnt_collection_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfnt_collection_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sfnt_collection_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
