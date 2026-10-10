/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Humongous Entertainment TLKB speech packs, adapted from XArchive XHETLKB.
 * Root and child sizes are big endian and include their eight-byte headers.
 * Members expose decoded child payloads; no synthesized sound header is added.
 */
#include "xxfclib/formats/he_tlkb/xx_he_tlkb.h"
#include "../xx_payload_members.h"

static uint32_t he_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool he_header(Abstractformat *f, int64_t at, uint8_t header[8], xx_pd_struct *pd)
{
    unsigned i;
    if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, at, header, 8U)) return false;
    for (i = 0; i < 8U; ++i) header[i] ^= 0x69;
    return !(pd && xx_pd_is_stopped(pd));
}

static bool he_payload(Abstractformat *f, pm_member *m, uint64_t at, void *buffer, size_t amount, xx_pd_struct *pd)
{
    size_t done = 0, i;
    uint8_t *bytes = (uint8_t *)buffer;
    if (!f || !m || (!buffer && amount) || at > (uint64_t)m->size || amount > (uint64_t)m->size - at || (pd && xx_pd_is_stopped(pd)) ||
        xx_io_seek64(f->device, m->offset + (int64_t)at, SEEK_SET) != 0)
        return false;
    while (done < amount) {
        ssize_t got;
        if (pd && xx_pd_is_stopped(pd)) return false;
        got = xx_io_read(f->device, bytes + done, amount - done);
        if (got <= 0 || (size_t)got > amount - done) return false;
        done += (size_t)got;
    }
    for (i = 0; i < amount; ++i) bytes[i] ^= 0x69;
    return !(pd && xx_pd_is_stopped(pd));
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t size = pm_available(f), at = 8;
    uint8_t header[8];
    if (size < 16 || size > UINT32_MAX || !he_header(f, 0, header, pd) || header[0] != 'T' || header[1] != 'L' || header[2] != 'K' || header[3] != 'B' ||
        he_be32(header + 4) != (uint64_t)size)
        return false;
    while (at <= size - 8) {
        uint32_t length;
        unsigned i;
        char tag[5], label[48];
        if (!he_header(f, at, header, pd)) return false;
        for (i = 0; i < 4U; ++i) {
            uint8_t c = header[i];
            if (c < 0x20 || c > 0x7e) return false;
            tag[i] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        }
        tag[4] = 0;
        length = he_be32(header + 4);
        if (length < 8U || length > (uint64_t)(size - at)) return false;
        (void)xx_rt_snprintf(label, sizeof(label), "%s_%04u.%s", tag, (unsigned)s->count + 1U, tag);
        if (!pm_add(f, s, label, at + 8, length - 8U)) return false;
        s->items[s->count - 1U].read_range = he_payload;
        s->items[s->count - 1U].compression_method = XX_HE_TLKB_METHOD_XOR_69;
        at += length;
    }
    if (at != size || !s->count || (pd && xx_pd_is_stopped(pd))) return false;
    s->size = size;
    return true;
}

void xx_he_tlkb_init(xx_he_tlkb *r, xx_io_device *d, int64_t base)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, base, XX_FILE_TYPE_HE_TLKB, "tlk");
    xx_format_set_mime_type(&r->format, "application/octet-stream");
}

xx_he_tlkb *xx_he_tlkb_create(xx_io_device *d, int64_t base)
{
    xx_he_tlkb *r = (xx_he_tlkb *)xx_mem_alloc(sizeof(*r));
    if (r) xx_he_tlkb_init(r, d, base);
    return r;
}

void xx_he_tlkb_destroy(xx_he_tlkb *r)
{
    if (r) {
        xx_format_cleanup_extra_parameters(&r->format);
        xx_format_invalidate_memory_map(&r->format);
    }
}

void xx_he_tlkb_free(xx_he_tlkb *r)
{
    if (r) {
        xx_he_tlkb_destroy(r);
        xx_mem_free(r);
    }
}

xx_file_type_t xx_he_tlkb_detect(xx_io_device *d, int64_t base)
{
    uint8_t header[8];
    int64_t size, saved;
    unsigned i;
    bool valid;
    xx_he_tlkb r;
    if (!d || base < 0 || (size = xx_io_size(d)) < base || size - base < 16 || size - base > UINT32_MAX || (saved = xx_io_tell(d)) < 0 ||
        !xx_io_read_at(d, base, header, sizeof(header)))
        return XX_FILE_TYPE_UNKNOWN;
    for (i = 0; i < 8U; ++i) header[i] ^= 0x69;
    if (header[0] != 'T' || header[1] != 'L' || header[2] != 'K' || header[3] != 'B' || he_be32(header + 4) != (uint64_t)(size - base)) return XX_FILE_TYPE_UNKNOWN;
    xx_he_tlkb_init(&r, d, base);
    valid = pm_valid(&r.format, NULL);
    xx_he_tlkb_destroy(&r);
    if (xx_io_seek64(d, saved, SEEK_SET) != 0) valid = false;
    return valid ? XX_FILE_TYPE_HE_TLKB : XX_FILE_TYPE_UNKNOWN;
}
