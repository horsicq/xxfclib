/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/zip_psc/xx_zip_psc.h"
#include "../xx_zip_borrowed_view.h"
#include "../xx_format_abstract_extractor_adapter.h"

static uint16_t zip_psc_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t zip_psc_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define PSC_COMPLEMENT_OFFSET INT64_C(12672)
/* PSC complements bytes from archive offset 12672; the ZIP view owns no input. */
static bool zip_psc_eocd(xx_io_device *d, int64_t b)
{
    int64_t total = xx_io_size(d), available;
    size_t n, i;
    uint8_t *tail;
    bool result = false;
    if (b < 0 || total < b || (available = total - b) < PSC_COMPLEMENT_OFFSET + 22) return false;
    n = (uint64_t)available > 65557U ? 65557U : (size_t)available;
    tail = (uint8_t *)xx_mem_alloc(n);
    if (!tail) return false;
    if (!xx_io_read_at(d, total - (int64_t)n, tail, n)) goto done;
    i = n - 22U;
    for (;;) {
        if (zip_psc_u32(tail + i) == 0xf9fab4afU && (size_t)(zip_psc_u16(tail + i + 20U) ^ 0xffffU) == n - i - 22U &&
            available - (int64_t)n + (int64_t)i >= PSC_COMPLEMENT_OFFSET) {
            result = true;
            break;
        }
        if (!i) break;
        --i;
    }
done:
    xx_mem_free(tail);
    return result;
}

Abstractformat *xx_zip_psc_create(xx_io_device *device, int64_t base)
{
    if (!device || base < 0 || xx_io_size(device) < base) return NULL;
    return zview_create(device, base, 0, XX_FILE_TYPE_ZIP_PSC, "psc", true, PSC_COMPLEMENT_OFFSET);
}
void xx_zip_psc_free(Abstractformat *format)
{
    if (format) {
        zview_destroy(format);
        xx_mem_free(format);
    }
}
xx_file_type_t xx_zip_psc_detect(xx_io_device *device, int64_t base)
{
    uint8_t magic[8];
    int64_t saved, total;
    Abstractformat *format;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total - base < 10) return result;
    saved = xx_io_tell(device);
    if (!xx_io_read_at(device, base, magic, sizeof(magic))) goto done;
    /* ZIP permits PK00 or the split-volume marker before the first local
     * header. PSC's complement boundary still counts from the outer start. */
    if (zip_psc_u32(magic) != 0x04034b50U && !((zip_psc_u32(magic) == 0x30304b50U || zip_psc_u32(magic) == 0x08074b50U) && zip_psc_u32(magic + 4) == 0x04034b50U))
        goto done;
    if (!zip_psc_eocd(device, base)) goto done;
    format = xx_zip_psc_create(device, base);
    if (format) {
        if (format->check_is_valid(format, NULL)) result = XX_FILE_TYPE_ZIP_PSC;
        xx_zip_psc_free(format);
    }
done:
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *zip_psc_open(xx_io_device *device)
{
    return xx_zip_psc_create(device, 0);
}
static const xx_file_type_t zip_psc_types[] = {XX_FILE_TYPE_ZIP_PSC};
static const xx_format_search_desc zip_psc_descriptor = {zip_psc_types, 1, NULL, 0, zip_psc_open, xx_zip_psc_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(zip_psc, zip_psc_descriptor)
