/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CAB name encoding is selected from its UTF-8 attribute or an unambiguous
 * Japanese PE resource locale. Never guess an encoding from filename bytes.
 */
#ifndef XX_CAB_NAMES_PRIVATE_H
#define XX_CAB_NAMES_PRIVATE_H
#include "xx_cab_cp932.inc"
static bool cab_name_utf8_valid(const char *name)
{
    const uint8_t *p = (const uint8_t *)name;
    while (*p) {
        uint32_t c = *p++;
        unsigned need, i;
        uint32_t minimum;
        if (c < 128) continue;
        if (c >= 0xc2 && c <= 0xdf) {
            need = 1;
            c &= 31;
            minimum = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            need = 2;
            c &= 15;
            minimum = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            need = 3;
            c &= 7;
            minimum = 65536;
        } else return false;
        for (i = 0; i < need; ++i) {
            if (*p < 0x80 || *p > 0xbf) return false;
            c = (c << 6) | (*p++ & 63);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
static char *cab_name_cp932(const char *raw)
{
    size_t n = xx_str_len(raw), at = 0, wrote = 0;
    char *out = (char *)xx_mem_alloc(n * 3 + 1);
    if (!out) return NULL;
    while (at < n) {
        uint32_t c = (uint8_t)raw[at++];
        if (c >= 0xa1 && c <= 0xdf) c = 0xff61 + c - 0xa1;
        else if (c >= 0x81 && (c <= 0x9f || (c >= 0xe0 && c <= 0xfc))) {
            unsigned lead = c, trail, index;
            if (at == n) goto fail;
            trail = (uint8_t)raw[at++];
            if (trail < 0x40 || trail == 0x7f || trail > 0xfc) goto fail;
            index = trail < 0x7f ? trail - 0x40 : trail - 0x80 + 63;
            /* CP932 reserves ten rows for application-defined BMP glyphs. */
            if (lead >= 0xf0 && lead <= 0xf9) c = 0xe000 + (lead - 0xf0) * 188 + index;
            else c = cab_cp932_pairs[(lead <= 0x9f ? lead - 0x81 : lead - 0xe0 + 31) * 188 + index];
            if (!c) goto fail;
        } else if (c >= 128) goto fail;
        if (c < 128) out[wrote++] = (char)c;
        else if (c < 2048) {
            out[wrote++] = (char)(0xc0 | (c >> 6));
            out[wrote++] = (char)(0x80 | (c & 63));
        } else {
            out[wrote++] = (char)(0xe0 | (c >> 12));
            out[wrote++] = (char)(0x80 | ((c >> 6) & 63));
            out[wrote++] = (char)(0x80 | (c & 63));
        }
    }
    out[wrote] = 0;
    return out;
fail:
    xx_mem_free(out);
    return NULL;
}
static uint16_t cab_name_u16(const uint8_t *p)
{
    return xx_data_get_u16(p, 2, 0, false);
}
static uint32_t cab_name_u32(const uint8_t *p)
{
    return xx_data_get_u32(p, 4, 0, false);
}
static bool cab_name_span(size_t at, size_t size, size_t end)
{
    return at <= end && size <= end - at;
}
typedef struct cab_name_locale {
    unsigned entries, japanese;
    bool version;
} cab_name_locale;
static bool cab_name_resource_locale(const uint8_t *b, size_t size, uint32_t at, unsigned depth, bool version, cab_name_locale *locale)
{
    uint32_t count, i;
    if (depth > 2 || !cab_name_span(at, 16, size)) return false;
    count = (uint32_t)cab_name_u16(b + at + 12) + cab_name_u16(b + at + 14);
    if (count > 4096 - locale->entries || !cab_name_span((size_t)at + 16, (size_t)count * 8, size)) return false;
    locale->entries += count;
    for (i = 0; i < count; ++i) {
        const uint8_t *r = b + at + 16 + (size_t)i * 8;
        uint32_t id = cab_name_u32(r), target = cab_name_u32(r + 4);
        bool is_version = depth ? version : id == 16;
        if (target & 0x80000000U) {
            if (depth == 2 || !cab_name_resource_locale(b, size, target & 0x7fffffffU, depth + 1, is_version, locale)) return false;
        } else {
            uint32_t language = id & 0xffffU;
            if (depth != 2 || id > 0xffffU || !cab_name_span(target, 16, size)) return false;
            if (language == 0 || language == 0x400) continue;
            if (language != 0x411) return false;
            ++locale->japanese;
            if (is_version) locale->version = true;
        }
    }
    return true;
}
static unsigned cab_name_pe_codepage(Abstractformat *f)
{
    uint8_t dos[64], pe[24], optional[240], section[40], *resources = NULL;
    uint32_t pe_at, rva, size;
    uint16_t sections, optional_size;
    unsigned i, result = 0;
    uint64_t section_at;
    cab_name_locale locale = {0, 0, false};
    if (f->base_address < 64 || !cab_read(f->device, 0, dos, sizeof(dos)) || memcmp(dos, "MZ", 2)) return 0;
    pe_at = cab_name_u32(dos + 60);
    if (pe_at < 64 || (uint64_t)pe_at + 24 > (uint64_t)f->base_address || !cab_read(f->device, pe_at, pe, sizeof(pe)) || memcmp(pe, "PE\0\0", 4)) return 0;
    sections = cab_name_u16(pe + 6);
    optional_size = cab_name_u16(pe + 20);
    section_at = (uint64_t)pe_at + 24 + optional_size;
    if (!sections || sections > 96 || optional_size < 120 || optional_size > sizeof(optional) || section_at + 40ULL * sections > (uint64_t)f->base_address ||
        !cab_read(f->device, (int64_t)pe_at + 24, optional, optional_size))
        return 0;
    {
        unsigned directory = cab_name_u16(optional) == 0x10b ? 96 : cab_name_u16(optional) == 0x20b ? 112 : 0;
        if (!directory || directory + 24 > optional_size || cab_name_u32(optional + directory - 4) < 3) return 0;
        rva = cab_name_u32(optional + directory + 16);
        size = cab_name_u32(optional + directory + 20);
    }
    if (!rva || size < 16 || size > 4U * 1024U * 1024U) return 0;
    for (i = 0; i < sections; ++i) {
        uint32_t virtual_at, raw_at, raw_size;
        uint64_t at;
        if (!cab_read(f->device, (int64_t)(section_at + 40ULL * i), section, sizeof(section))) return 0;
        virtual_at = cab_name_u32(section + 12);
        raw_size = cab_name_u32(section + 16);
        raw_at = cab_name_u32(section + 20);
        if (rva < virtual_at || rva - virtual_at > raw_size || size > raw_size - (rva - virtual_at)) continue;
        at = (uint64_t)raw_at + (rva - virtual_at);
        if (at > (uint64_t)f->base_address || size > (uint64_t)f->base_address - at) return 0;
        resources = (uint8_t *)xx_mem_alloc(size);
        if (!resources) return 0;
        if (cab_read(f->device, (int64_t)at, resources, size) && cab_name_resource_locale(resources, size, 0, 0, false, &locale) && locale.japanese && locale.version)
            result = 932;
        xx_mem_free(resources);
        break;
    }
    return result;
}
static char *cab_name_decode(const char *raw, uint16_t attributes, unsigned codepage)
{
    if (attributes & 0x80U) return cab_name_utf8_valid(raw) ? xx_str_dup(raw) : NULL;
    if (codepage == 932) return cab_name_cp932(raw);
    return xx_str_dup(raw);
}
#endif
