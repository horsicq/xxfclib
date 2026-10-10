/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/crx_native/xx_crx_native.h"
#include "../xx_zip_borrowed_view.h"
#include "../xx_format_abstract_extractor_adapter.h"

static uint16_t crx_native_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t crx_native_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* CRX2 header and CRX3 protobuf envelope; ZIP validation does not authenticate
 * the extension publisher. Envelope grammar: Chromium crx3.proto. */
static bool crx_native_varint(const uint8_t *p, size_t n, size_t *at, uint64_t *value)
{
    unsigned shift;
    uint64_t result = 0U;
    for (shift = 0U; shift < 70U; shift += 7U) {
        unsigned b;
        if (*at >= n) return false;
        b = p[(*at)++];
        if (shift == 63U && b > 1U) return false;
        result |= (uint64_t)(b & 127U) << shift;
        if (!(b & 128U)) {
            *value = result;
            return true;
        }
    }
    return false;
}

static bool crx_native_proto(const uint8_t *p, size_t n)
{
    size_t at = 0U;
    uint64_t tag, value;
    while (at < n) {
        if (!crx_native_varint(p, n, &at, &tag) || !(tag >> 3U) || (tag >> 3U) > 0x1fffffffU) return false;
        switch (tag & 7U) {
            case 0U:
                if (!crx_native_varint(p, n, &at, &value)) return false;
                break;
            case 1U:
                if (n - at < 8U) return false;
                at += 8U;
                break;
            case 2U:
                if (!crx_native_varint(p, n, &at, &value) || value > n - at) return false;
                at += (size_t)value;
                break;
            case 5U:
                if (n - at < 4U) return false;
                at += 4U;
                break;
            default: return false;
        }
    }
    return true;
}

static bool crx_native_crx_offset(xx_io_device *d, int64_t base, int64_t *offset)
{
    uint8_t h[16], magic[4], *header = NULL;
    uint32_t version;
    uint64_t at, available;
    int64_t total = xx_io_size(d);
    bool valid = false;
    if (base < 0 || total < base || total - base < 16 || !xx_io_read_at(d, base, h, sizeof(h)) || memcmp(h, "Cr24", 4U)) return false;
    available = (uint64_t)(total - base);
    version = crx_native_u32(h + 4U);
    if (version == 2U) {
        uint32_t key = crx_native_u32(h + 8U), signature = crx_native_u32(h + 12U);
        if (!key || !signature || key > 16U * 1024U * 1024U || signature > 16U * 1024U * 1024U) return false;
        at = 16U + (uint64_t)key + signature;
    } else if (version == 3U) {
        uint32_t size = crx_native_u32(h + 8U);
        if (!size || size > 16U * 1024U * 1024U || (uint64_t)size > available - 12U) return false;
        header = (uint8_t *)xx_mem_alloc(size);
        if (!header) return false;
        valid = xx_io_read_at(d, base + 12, header, size) && crx_native_proto(header, size);
        xx_mem_free(header);
        if (!valid) return false;
        at = 12U + (uint64_t)size;
    } else return false;
    if (at > available || available - at < 22U || !xx_io_read_at(d, base + (int64_t)at, magic, sizeof(magic)) ||
        (crx_native_u32(magic) != 0x04034b50U && crx_native_u32(magic) != 0x06054b50U))
        return false;
    *offset = (int64_t)at;
    return true;
}

Abstractformat *xx_crx_native_create(xx_io_device *device, int64_t base)
{
    int64_t skip = 0;
    bool valid;
    if (!device || base < 0 || xx_io_size(device) < base) return NULL;
    valid = crx_native_crx_offset(device, base, &skip);
    return zview_create(device, base, skip, XX_FILE_TYPE_CRX, "crx", valid, -1);
}
void xx_crx_native_free(Abstractformat *format)
{
    if (format) {
        zview_destroy(format);
        xx_mem_free(format);
    }
}
xx_file_type_t xx_crx_native_detect(xx_io_device *device, int64_t base)
{
    uint8_t magic[4];
    int64_t saved, total;
    Abstractformat *format;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total - base < 10) return result;
    saved = xx_io_tell(device);
    if (!xx_io_read_at(device, base, magic, sizeof(magic)) || memcmp(magic, "Cr24", 4U)) goto done;
    format = xx_crx_native_create(device, base);
    if (format) {
        if (format->check_is_valid(format, NULL)) result = XX_FILE_TYPE_CRX;
        xx_crx_native_free(format);
    }
done:
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *crx_native_open(xx_io_device *device)
{
    return xx_crx_native_create(device, 0);
}
static const xx_file_type_t crx_native_types[] = {XX_FILE_TYPE_CRX};
static const xx_format_search_desc crx_native_descriptor = {crx_native_types, 1, NULL, 0, crx_native_open, xx_crx_native_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(crx_native, crx_native_descriptor)
