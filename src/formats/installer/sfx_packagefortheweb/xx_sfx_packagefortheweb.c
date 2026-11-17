/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/installers/xpftw.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/installer/sfx_packagefortheweb/xx_sfx_packagefortheweb.h"
#include "../../common/xx_carrier_helpers.h"

static bool pw_cab(Abstractformat *f, int64_t at, int64_t end, int64_t *stop, xx_pd_struct *pd)
{
    uint8_t h[36];
    uint32_t bytes, filesat;
    uint16_t folders, files, flags, i;
    int64_t table, first_data;
    uint8_t reserve = 0, data_reserve = 0;
    uint16_t header_reserve = 0;
    if (end - at < 36 || !pm_read(f, at, h, 36) || xx_rt_memcmp(h, "MSCF", 4) || xx_data_get_u32(h + 4, 4, 0, false) || xx_data_get_u32(h + 12, 4, 0, false) ||
        xx_data_get_u32(h + 20, 4, 0, false) || h[24] != 3 || h[25] != 1)
        return false;
    bytes = xx_data_get_u32(h + 8, 4, 0, false);
    filesat = xx_data_get_u32(h + 16, 4, 0, false);
    folders = xx_data_get_u16(h + 26, 2, 0, false);
    files = xx_data_get_u16(h + 28, 2, 0, false);
    flags = xx_data_get_u16(h + 30, 2, 0, false);
    if (bytes < 36 || !carrier_range(end, at, bytes) || !folders || folders > 4096 || !files || (flags & ~4U)) {
        return false;
    }
    *stop = at + bytes;
    table = at + 36;
    first_data = *stop;
    if (flags & 4) {
        if (!carrier_range(*stop, table, 4) || !pm_read(f, table, h, 4)) return false;
        header_reserve = xx_data_get_u16(h, 2, 0, false);
        reserve = h[2];
        data_reserve = h[3];
        table += 4 + header_reserve;
    }
    if (table > *stop || (uint64_t)folders * (8U + reserve) > (uint64_t)(*stop - table) || filesat < (uint64_t)(table - at) + (uint64_t)folders * (8U + reserve) ||
        filesat >= bytes)
        return false;
    for (i = 0; i < folders; ++i) {
        uint32_t off;
        uint16_t chunks, j;
        int64_t p;
        if (carrier_stop(pd) || !pm_read(f, table + (int64_t)i * (8 + reserve), h, 8)) {
            return false;
        }
        off = xx_data_get_u32(h, 4, 0, false);
        chunks = xx_data_get_u16(h + 4, 2, 0, false);
        if (off >= bytes || !chunks || (xx_data_get_u16(h + 6, 2, 0, false) & 15) > 3) return false;
        p = at + off;
        if (p < first_data) first_data = p;
        for (j = 0; j < chunks; ++j) {
            uint16_t packed;
            if (*stop - p < 8 + data_reserve || !pm_read(f, p, h, 8) || !(packed = xx_data_get_u16(h + 4, 2, 0, false)) ||
                packed > (uint64_t)(*stop - p - 8 - data_reserve))
                return false;
            p += 8 + data_reserve + packed;
        }
    }
    table = at + filesat;
    for (i = 0; i < files; ++i) {
        char name[4097];
        if (carrier_stop(pd) || first_data - table < 16 || !pm_read(f, table, h, 16) || xx_data_get_u16(h + 8, 2, 0, false) >= folders) return false;
        table += 16;
        if (!carrier_string(f, &table, first_data, name, sizeof(name)) || !name[0]) return false;
    }
    return table <= first_data;
}
/* The settings block names the package length itself: decode it (new header:
 * nibble swap then XOR 0x9E - i % 26; old header: XOR 0x61 + i % 26), take the
 * u32 after the 4-byte (old 3-byte "2!$" form: 3-byte) tag and require the
 * remaining settings to be a well-formed record list: 0x12 + u32, a byte below
 * 0x80 + u16 length + data, or a byte >= 0x80 + one byte, consuming the block
 * exactly. Used when the package is not a plain, intact cabinet (encoded
 * packages, cabinets with damaged blocks): the package is then published as
 * one stored member. */
static bool pw_declared(const uint8_t *raw, uint32_t size, uint32_t *length)
{
    uint8_t *d;
    uint32_t i, head, left;
    bool ok = false;
    if (size < 8) return false;
    d = (uint8_t *)xx_mem_alloc(size);
    if (!d) return false;
    if (!xx_rt_memcmp(raw, "\xdc\xed\xbd\xa9", 4)) {
        for (i = 0; i < size; ++i) d[i] = (uint8_t)((uint8_t)((raw[i] << 4) | (raw[i] >> 4)) ^ (uint8_t)(0x9E - i % 26));
        head = 8;
    } else if (!xx_rt_memcmp(raw, "2!$", 3)) {
        for (i = 0; i < size; ++i) d[i] = (uint8_t)(raw[i] ^ (uint8_t)(0x61 + i % 26));
        head = raw[3] == 'e' ? 8U : 7U;
    } else
        goto done;
    *length = xx_data_get_u32(d + head - 4, 4, 0, false);
    for (i = head, left = size - head; left;) {
        uint8_t c = d[i];
        uint32_t step;
        if (c == 0x12U) step = 5;
        else if (!(c & 0x80U)) step = left >= 3U ? 3U + xx_data_get_u16(d + i + 1, 2, 0, false) : 4U;
        else step = 2;
        if (step > left) goto done;
        i += step;
        left -= step;
    }
    ok = *length > 0 && *length <= 0x7FFFFFFFU;
done:
    xx_mem_free(d);
    return ok;
}
static bool carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t overlay, cab, section_end, end, limit = pm_available(f);
    uint8_t h[16];
    unsigned attempt;
    uint32_t size, declared = 0;
    uint8_t *settings = NULL;
    bool ok = false;
    if (!carrier_pe(f, &overlay, &cab, &section_end, pd)) return false;
    for (attempt = 0; attempt < (cab >= 0 ? 2U : 1U); ++attempt) {
        int64_t start = cab >= 0 ? cab + (attempt ? 12 : 0) : overlay, region_end = cab >= 0 ? section_end : limit;
        unsigned i;
        bool new_header;
        if (!pm_read(f, start, h, 4) || (size = xx_data_get_u32(h, 4, 0, false)) < 16 || size > 1048576 || !carrier_range(region_end, start + 4, (uint64_t)size + 36))
            continue;
        settings = (uint8_t *)xx_mem_alloc(size);
        if (!settings || !pm_read(f, start + 4, settings, size)) goto done;
        /* Later PackageForTheWeb writers replaced the XOR/SCG settings
         * grammar with this opaque header. The length still places a fully
         * bounded CAB immediately after it; a generic PE/CAB is insufficient
         * to claim this producer. Do not present opaque settings as SCG. */
        new_header = !xx_rt_memcmp(settings, "\xdc\xed\xbd\xa9", 4);
        if (pw_declared(settings, size, &declared) && declared <= (uint64_t)(limit - start - 4 - size) &&
            !pw_cab(f, start + 4 + size, region_end, &end, pd)) {
            /* Not an intact plain cabinet: publish the declared package. */
            xx_mem_free(settings);
            settings = NULL;
            if (!pm_add(f, s, "pftw1.pkg", start + 4 + size, declared) || !(s->items[s->count - 1].display_name = xx_str_dup("pftw1.pkg"))) goto done;
            s->size = start + 4 + size + declared;
            ok = true;
            break;
        }
        if (!new_header)
            for (i = 0; i < size; ++i) settings[i] ^= (uint8_t)(0x61 + i % 26);
        if ((!new_header && xx_rt_memcmp(settings, "SCG", 3)) || !pw_cab(f, start + 4 + size, region_end, &end, pd) ||
            (!new_header && xx_data_get_u32(settings + 4, 4, 0, false) != (uint64_t)(end - start - 4 - size))) {
            xx_mem_free(settings);
            settings = NULL;
            continue;
        }
        if (new_header) {
            xx_mem_free(settings);
            settings = NULL;
        } else {
            if (!pm_add(f, s, "settings.scg", start + 4, size)) goto done;
            s->items[s->count - 1].memory = settings;
            settings = NULL;
        }
        if (!pm_add(f, s, "payload.cab", start + 4 + size, end - start - 4 - size)) {
            goto done;
        }
        if (new_header && !(s->items[s->count - 1].display_name = xx_str_dup("pftw1.pkg"))) goto done;
        s->size = cab >= 0 && overlay > end ? overlay : end;
        ok = true;
        break;
    }
done:
    if (settings) xx_mem_free(settings);
    return ok;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_sfx_packagefortheweb_init(xx_sfx_packagefortheweb *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_PACKAGEFORTHEWEB, "exe");
    }
}
xx_sfx_packagefortheweb *xx_sfx_packagefortheweb_create(xx_io_device *d, int64_t b)
{
    xx_sfx_packagefortheweb *r = (xx_sfx_packagefortheweb *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_packagefortheweb_init(r, d, b);
    return r;
}
void xx_sfx_packagefortheweb_destroy(xx_sfx_packagefortheweb *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_packagefortheweb_free(xx_sfx_packagefortheweb *r)
{
    if (r) {
        xx_sfx_packagefortheweb_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_packagefortheweb_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sfx_packagefortheweb_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
