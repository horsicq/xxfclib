/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/rdfz/xx_rdfz.h"
#include "../xx_bounded_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
#include "../xx_bounded_member_cursor.h"
static bool rdfz_zlib_member(Abstractformat *f, pm_member *m, xx_io_device *output, xx_pd_struct *pd)
{
    bdm_buffer b = {0};
    int64_t used = 0;
    pm_stream *s = (pm_stream *)m->context;
    bool result = bdm_inflate_reserved(f, m->offset - f->base_address, m->packed_size, true, &b, &used, pd, s ? (uint64_t)s->capacity * sizeof(pm_member) : 0) &&
                  used == m->packed_size && b.size == (uint64_t)m->size && bdm_write(output, b.data, b.size, pd);
    xx_mem_free(b.data);
    return result;
}

static bool rdfz_rdfz_strings(bdm_cursor *c, const uint8_t ***strings, uint32_t *count, uint64_t room)
{
    uint32_t i, n;
    const uint8_t **list;
    if (!bdm_word(c, &n) || n > 1000000U || n > (c->size - c->at) / 4U || (uint64_t)n * sizeof(*list) > room) return false;
    list = (const uint8_t **)xx_mem_calloc(n ? n : 1U, sizeof(*list));
    if (!list) return false;
    *strings = list;
    *count = n;
    for (i = 0; i < n; ++i) {
        uint32_t length;
        list[i] = c->data + c->at;
        if (!bdm_word(c, &length) || length > 4096U || !bdm_take(c, length, NULL)) return false;
    }
    return true;
}

static bool rdfz_rdfz_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[20], e[20], small[4];
    bdm_buffer b = {0};
    bdm_cursor c;
    const uint8_t **extensions = NULL, **names = NULL;
    uint32_t packed, expected, ext_count = 0, name_count = 0, count, i;
    int64_t size = pm_available(f), used, table;
    bool result = false;
    if (size < 20 || !pm_read(f, 0, h, 20) || xx_mem_compare(h, "RDFZ\4\0\0\0Zlib", 12)) return false;
    packed = bdm_u32(h + 12);
    expected = bdm_u32(h + 16);
    if (packed < 10U || expected > BDM_MEMORY_LIMIT || (uint64_t)packed + 20U > (uint64_t)size || !bdm_inflate(f, 20, packed - 4, true, &b, &used, pd) ||
        used != packed - 4 || b.size != expected || b.size < 24U)
        goto done;
    c.data = b.data;
    c.size = b.size;
    c.at = 16;
    if (bdm_u32(b.data) != 2U || bdm_u32(b.data + 4) != 0U || bdm_u32(b.data + 8) != 4U || xx_mem_compare(b.data + 12, "Zlib", 4) || b.capacity > bdm_budget(f) ||
        !rdfz_rdfz_strings(&c, &extensions, &ext_count, bdm_budget(f) - b.capacity) ||
        !rdfz_rdfz_strings(&c, &names, &name_count, bdm_budget(f) - b.capacity - (uint64_t)ext_count * sizeof(*extensions)) || c.at != c.size)
        goto done;
    table = 16 + (int64_t)packed;
    if (!pm_read(f, table, small, 4)) goto done;
    count = bdm_u32(small);
    table += 4;
    if (!count || count > 1000000U || (uint64_t)table + (uint64_t)count * 20U > (uint64_t)size) goto done;
    for (i = 0; i < count; ++i) {
        uint32_t offset, length, ext_index, name_index, method, plain;
        char label[96], name[65], extension[24];
        size_t j, n;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, table + (int64_t)i * 20, e, 20)) goto done;
        offset = bdm_u32(e);
        length = bdm_u32(e + 4);
        ext_index = bdm_u32(e + 8);
        name_index = bdm_u32(e + 12);
        method = bdm_u32(e + 16);
        if (offset < (uint64_t)table + (uint64_t)count * 20U || ext_index >= ext_count || name_index >= name_count || method > 1U ||
            (uint64_t)offset + length > (uint64_t)size)
            goto done;
        n = bdm_u32(names[name_index]);
        if (n > 64U) n = 64U;
        for (j = 0; j < n; ++j) {
            uint8_t v = names[name_index][4 + j];
            name[j] = (v >= 32 && v < 127) ? (char)v : '_';
        }
        name[n] = 0;
        n = bdm_u32(extensions[ext_index]);
        if (n > 23U) n = 23U;
        for (j = 0; j < n; ++j) {
            uint8_t v = extensions[ext_index][4 + j];
            extension[j] = (v >= 32 && v < 127) ? (char)v : '_';
        }
        extension[n] = 0;
        (void)xx_rt_snprintf(label, sizeof(label), "%s.%s", name, extension);
        plain = length;
        if (method) {
            if (length < 10U || !pm_read(f, offset, small, 4)) goto done;
            plain = bdm_u32(small);
            offset += 4;
            length -= 4;
            if (plain > BDM_MEMORY_LIMIT) goto done;
        }
        if (!bdm_room(f, s,
                      (uint64_t)b.capacity + (ext_count + (uint64_t)name_count) * sizeof(*extensions) +
                          (s->count == s->capacity ? (uint64_t)(s->capacity ? s->capacity : 8U) * sizeof(pm_member) : 0)) ||
            !bdm_add(f, s, label, offset, length))
            goto done;
        s->items[s->count - 1U].size = plain;
        if (method) {
            s->items[s->count - 1U].read_all = rdfz_zlib_member;
            s->items[s->count - 1U].context = s;
            s->items[s->count - 1U].compression_method = 8;
        }
    }
    s->size = size;
    result = true;
done:
    xx_mem_free(extensions);
    xx_mem_free(names);
    xx_mem_free(b.data);
    return result;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd)
{
    if ((pd && xx_pd_is_stopped(pd)) || format->file_type != XX_FILE_TYPE_RDFZ) return false;
    return rdfz_rdfz_parse(format, members, pd);
}

Abstractformat *xx_rdfz_create(xx_io_device *device, int64_t base)
{
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) pm_init(format, device, base, XX_FILE_TYPE_RDFZ, "rdfz");
    return format;
}
void xx_rdfz_free(Abstractformat *format)
{
    if (format) {
        xx_format_destroy(format);
        xx_mem_free(format);
    }
}

/* Detection keeps the inexpensive signature rule; the reader validates the grammar. */
xx_file_type_t xx_rdfz_detect(xx_io_device *device, int64_t base)
{
    uint8_t signature[12];
    int64_t saved, total;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total - base < 12) return result;
    saved = xx_io_tell(device);
    if (xx_io_read_at(device, base, signature, sizeof(signature)) && !xx_mem_compare(signature, "RDFZ\4\0\0\0Zlib", sizeof(signature))) result = XX_FILE_TYPE_RDFZ;
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *rdfz_open(xx_io_device *device)
{
    return xx_rdfz_create(device, 0);
}
static const xx_file_type_t rdfz_types[] = {XX_FILE_TYPE_RDFZ};
static const xx_format_search_desc rdfz_descriptor = {rdfz_types, 1, NULL, 0, rdfz_open, xx_rdfz_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(rdfz, rdfz_descriptor)
