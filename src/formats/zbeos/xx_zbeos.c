/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/zbeos/xx_zbeos.h"
#include "../xx_bounded_deflate_members.h"
#include "../xx_format_abstract_extractor_adapter.h"
#include "xxfclib/algo/crc/xx_crc.h"
static bool zbeos_gzip_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[12], tail[8];
    bdm_buffer b = {0};
    int64_t size = pm_available(f), used = 0;
    bool result = false;
    if (size <= 131082 || !pm_read(f, 0, h, 12) || xx_mem_compare(h, "\xebs\x90\xf8sdfS\xb8\1\xde\xbe", 12) || !pm_read(f, 131072, h, 10) || h[0] != 31 || h[1] != 139 ||
        h[2] != 8 || h[3] != 0 || !bdm_inflate(f, 131082, size - 131082, false, &b, &used, pd) || !pm_read(f, 131082 + used, tail, 8) ||
        bdm_u32(tail + 4) != (uint32_t)b.size || bdm_u32(tail) != xx_crc32(XX_CRC_TYPE_CRC32, b.data, b.size) ||
        !bdm_room(f, s, (uint64_t)b.capacity + b.size + sizeof(pm_member) * 8U) || !bdm_add_memory(f, s, "beos-boot-image.img", b.data, b.size))
        goto done;
    s->items[0].packed_size = used + 18;
    s->items[0].compression_method = 8;
    s->size = size;
    result = true;
done:
    xx_mem_free(b.data);
    return result;
}

static bool pm_parse(Abstractformat *format, pm_stream *members, xx_pd_struct *pd)
{
    if ((pd && xx_pd_is_stopped(pd)) || format->file_type != XX_FILE_TYPE_ZBEOS) return false;
    return zbeos_gzip_parse(format, members, pd);
}

Abstractformat *xx_zbeos_create(xx_io_device *device, int64_t base)
{
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) pm_init(format, device, base, XX_FILE_TYPE_ZBEOS, "img");
    return format;
}
void xx_zbeos_free(Abstractformat *format)
{
    if (format) {
        xx_format_destroy(format);
        xx_mem_free(format);
    }
}

/* Detection keeps the inexpensive signature rule; the reader validates the grammar. */
xx_file_type_t xx_zbeos_detect(xx_io_device *device, int64_t base)
{
    uint8_t signature[12];
    int64_t saved, total;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    if (!device || base < 0 || (total = xx_io_size(device)) < base || total - base < 12) return result;
    saved = xx_io_tell(device);
    if (xx_io_read_at(device, base, signature, sizeof(signature)) && !xx_mem_compare(signature, "\xebs\x90\xf8sdfS\xb8\1\xde\xbe", sizeof(signature)))
        result = XX_FILE_TYPE_ZBEOS;
    if (saved >= 0 && xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return result;
}

static Abstractformat *zbeos_open(xx_io_device *device)
{
    return xx_zbeos_create(device, 0);
}
static const xx_file_type_t zbeos_types[] = {XX_FILE_TYPE_ZBEOS};
static const xx_format_search_desc zbeos_descriptor = {zbeos_types, 1, NULL, 0, zbeos_open, xx_zbeos_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(zbeos, zbeos_descriptor)
