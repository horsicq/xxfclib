/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout/validation parity: Formats/audio/xpma.{h,cpp}, explicitly identified
 * there as audio/x-palladix-pma. This is unrelated to the PMarc PMA archive.
 * Original channel programs/instruments are extracted without audio synthesis.
 */
#include "xxfclib/formats/palladix_pma/xx_palladix_pma.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

/* Sequential bounded cache avoids one device seek per event byte. Channel
 * offsets and instrument pointers remain relative to the format base. */
typedef struct pma_cursor {
    Abstractformat *format;
    int64_t total, at, cache_at;
    size_t cache_size;
    uint8_t cache[4096];
} pma_cursor;

static bool pma_byte(pma_cursor *c, uint8_t *value)
{
    if (c->at < 0 || c->at >= c->total) return false;
    if (c->at < c->cache_at || c->at - c->cache_at >= (int64_t)c->cache_size) {
        c->cache_at = c->at;
        c->cache_size = (size_t)(c->total - c->at < (int64_t)sizeof(c->cache) ? c->total - c->at : (int64_t)sizeof(c->cache));
        if (!pm_read(c->format, c->at, c->cache, c->cache_size)) return false;
    }
    *value = c->cache[(size_t)(c->at - c->cache_at)];
    ++c->at;
    return true;
}

static bool pma_skip(pma_cursor *c, unsigned count)
{
    if (c->at > c->total || (int64_t)count > c->total - c->at) return false;
    c->at += count;
    return true;
}

static bool pma_channel(Abstractformat *f, int64_t total, uint16_t offset, xx_pd_struct *pd)
{
    pma_cursor c;
    uint8_t flags, low, high, note;
    xx_mem_zero(&c, sizeof(c));
    c.format = f;
    c.total = total;
    c.at = offset;
    c.cache_at = -1;
    /* Every iteration consumes at least one byte; all skips and references
     * are checked against the actual remaining device size. */
    while (c.at < total) {
        if ((pd && xx_pd_is_stopped(pd)) || !pma_byte(&c, &flags)) return false;
        if (flags == 0) return true;
        if (flags & 0x80) {
            if (flags != 0x80 || !pma_skip(&c, 1)) return false;
            continue;
        }
        if (flags & 0x01) {
            uint16_t instrument;
            if (!pma_byte(&c, &low) || !pma_byte(&c, &high)) return false;
            instrument = (uint16_t)((uint16_t)low | (uint16_t)high << 8);
            if (instrument < 25 || instrument > total || total - instrument < 12) return false;
        }
        if ((flags & 0x02) && !pma_skip(&c, 1)) return false;
        if (flags & 0x08) {
            if (!pma_byte(&c, &note) || note >= 192 || (note & 1)) return false;
        }
        if ((flags & 0x10) && !pma_skip(&c, 2)) return false;
        if ((flags & 0x40) && !pma_skip(&c, 2)) return false;
        /* Bits 2/5 have no operands. Every ordinary event has a delay. */
        if (!pma_skip(&c, 1)) return false;
    }
    return false; /* Every active channel requires its zero terminator. */
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t header[25];
    unsigned i;
    bool active = false;
    int64_t available = pm_available(f);
    if ((pd && xx_pd_is_stopped(pd)) || available <= 25 || !pm_read(f, 0, header, sizeof(header)) || xx_rt_memcmp(header, "PLX", 3) || header[3] > 1) return false;
    for (i = 0; i < 9; ++i) {
        uint16_t offset = xx_data_get_u16(header, sizeof(header), 7 + i * 2, false);
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (offset == 0) continue;
        if (offset < 25 || offset >= available || !pma_channel(f, available, offset, pd)) return false;
        active = true;
    }
    if (!active || !pm_add(f, s, "control.bin", 0, 7) || !pm_add(f, s, "channel-table.bin", 7, 18) || !pm_add(f, s, "program.bin", 25, available - 25)) return false;
    s->size = available;
    return true;
}

void xx_palladix_pma_init(xx_palladix_pma *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PALLADIX_PMA, "pma");
        r->format.is_archive = false;
        r->format.format_type = XX_TYPE_RAW;
        r->format.endian = XX_ENDIAN_LITTLE;
        xx_format_set_mime_type(&r->format, "audio/x-palladix-pma");
    }
}
xx_palladix_pma *xx_palladix_pma_create(xx_io_device *d, int64_t b)
{
    xx_palladix_pma *r = (xx_palladix_pma *)xx_mem_alloc(sizeof(*r));
    if (r) xx_palladix_pma_init(r, d, b);
    return r;
}
void xx_palladix_pma_destroy(xx_palladix_pma *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_palladix_pma_free(xx_palladix_pma *r)
{
    if (r) {
        xx_palladix_pma_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_palladix_pma_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_palladix_pma_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
xx_file_type_t xx_palladix_pma_detect(xx_io_device *d, int64_t b)
{
    xx_palladix_pma reader;
    bool result;
    xx_palladix_pma_init(&reader, d, b);
    result = pm_valid(&reader.format, NULL);
    xx_palladix_pma_destroy(&reader);
    return result ? XX_FILE_TYPE_PALLADIX_PMA : XX_FILE_TYPE_UNKNOWN;
}
