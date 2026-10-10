/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/solarispkg_zip/xx_solarispkg_zip.h"
#include "../xx_bounded_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
static bool solarispkg_zip_solaris_member(Abstractformat *f, pm_member *m, xx_io_device *output, xx_pd_struct *pd)
{
    uint8_t h[12], e[12];
    uint32_t count, i;
    pm_stream *s = (pm_stream *)m->context;
    int64_t base = m->offset - f->base_address;
    uint64_t total = 0;
    if (!pm_read(f, base, h, 12) || xx_mem_compare(h, "\x19\x9eTG", 4)) return false;
    count = bdm_u32(h + 4);
    if (!count || count > 262144U) return false;
    for (i = 0; i < count; ++i) {
        bdm_buffer b = {0};
        int64_t used = 0;
        uint32_t plain, packed, offset;
        bool okay;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, base + 12 + (int64_t)i * 12, e, 12)) return false;
        plain = bdm_u32(e);
        packed = bdm_u32(e + 4);
        offset = bdm_u32(e + 8);
        if (!plain || plain > BDM_BLOCK_LIMIT || !packed || offset < 12U + count * 12U || (uint64_t)offset + packed > (uint64_t)m->packed_size ||
            total + plain > (uint64_t)m->size)
            return false;
        /* This install-media dialect counts one alignment byte after each
         * raw Deflate stream. That byte is not compressed input or a CRC. */
        okay = bdm_inflate_reserved(f, base + offset, packed, false, &b, &used, pd, s ? (uint64_t)s->capacity * sizeof(pm_member) : 0) &&
               (used == packed || used == packed - 1U) && b.size == plain && bdm_write(output, b.data, b.size, pd);
        xx_mem_free(b.data);
        if (!okay) return false;
        total += plain;
    }
    return total == (uint64_t)m->size;
}

static bool solarispkg_zip_solaris_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[25], e[12];
    int64_t size = pm_available(f), position = 0;
    unsigned objects = 0;
    if (size < 1024 || !pm_read(f, 0, h, 25) || xx_mem_compare(h, "# PaCkAgE DaTaStReAm:zip\n", 25)) return false;
    while (position <= size - 512) {
        uint32_t length, count, block, i;
        int64_t body;
        uint64_t total = 0;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, position, h, 12)) return false;
        if (xx_mem_compare(h, "\x19\x9eTL", 4)) {
            if (!objects) {
                position += 512;
                continue;
            }
            /* Install media commonly pad the remainder with FF or zero. */
            if (h[0] != 0 && h[0] != 255U) return false;
            position += 512;
            continue;
        }
        length = bdm_u32(h + 4);
        body = position + 512;
        if (length < 12U || (uint64_t)body + length > (uint64_t)size || !pm_read(f, body, h, 12) || xx_mem_compare(h, "\x19\x9eTG", 4)) return false;
        count = bdm_u32(h + 4);
        block = bdm_u32(h + 8);
        if (!count || count > 262144U || !block || block > BDM_BLOCK_LIMIT || (uint64_t)count * 12U + 12U > length) return false;
        for (i = 0; i < count; ++i) {
            uint32_t plain, packed, offset;
            if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, body + 12 + (int64_t)i * 12, e, 12)) return false;
            plain = bdm_u32(e);
            packed = bdm_u32(e + 4);
            offset = bdm_u32(e + 8);
            if (!plain || plain > block || !packed || offset < count * 12U + 12U || (uint64_t)offset + packed > length || total + plain > BDM_MEMORY_LIMIT) return false;
            total += plain;
        }
        if (!bdm_add(f, s, "package.cpio", body, length)) return false;
        s->items[s->count - 1U].size = (int64_t)total;
        s->items[s->count - 1U].read_all = solarispkg_zip_solaris_member;
        s->items[s->count - 1U].context = s;
        s->items[s->count - 1U].compression_method = 8;
        position = body + (((int64_t)length + 511) & ~511LL);
        if (++objects > 4096U) return false;
    }
    s->size = size;
    return objects != 0;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd)
{
    if ((pd && xx_pd_is_stopped(pd)) || format->file_type != XX_FILE_TYPE_SOLARISPKG_ZIP) return false;
    return solarispkg_zip_solaris_parse(format, members, pd);
}

Abstractformat *xx_solarispkg_zip_create(xx_io_device *device, int64_t base)
{
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) pm_init(format, device, base, XX_FILE_TYPE_SOLARISPKG_ZIP, "image");
    return format;
}
void xx_solarispkg_zip_free(Abstractformat *format)
{
    if (format) {
        xx_format_destroy(format);
        xx_mem_free(format);
    }
}

/* Detection keeps the inexpensive signature rule; the reader validates the grammar. */
xx_file_type_t xx_solarispkg_zip_detect(xx_io_device *device, int64_t base)
{
    uint8_t signature[25];
    int64_t saved, total;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total - base < 25) return result;
    saved = xx_io_tell(device);
    if (xx_io_read_at(device, base, signature, sizeof(signature)) && !xx_mem_compare(signature, "# PaCkAgE DaTaStReAm:zip\n", sizeof(signature)))
        result = XX_FILE_TYPE_SOLARISPKG_ZIP;
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *solarispkg_zip_open(xx_io_device *device)
{
    return xx_solarispkg_zip_create(device, 0);
}
static const xx_file_type_t solarispkg_zip_types[] = {XX_FILE_TYPE_SOLARISPKG_ZIP};
static const xx_format_search_desc solarispkg_zip_descriptor = {solarispkg_zip_types, 1, NULL, 0, solarispkg_zip_open, xx_solarispkg_zip_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(solarispkg_zip, solarispkg_zip_descriptor)
