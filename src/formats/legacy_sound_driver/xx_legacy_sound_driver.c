/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout validation adapted from Formats XDTC, XDMA, XMUS and XSND.
 * These are DOS driver images; records expose their encoded regions. */
#include "xxfclib/formats/legacy_sound_driver/xx_legacy_sound_driver.h"
#include "xxfclib/data/xx_data.h"
#include "../xx_payload_members.h"
#include <string.h>

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t header[24], table[50], footer[20], padding[15];
    const char *magic, *embedded;
    unsigned version, count, revision, flags, i, j, footer_at, module_at;
    int64_t available = pm_available(f), logical, size;
    if (f->is_mapped || (pd && xx_pd_is_stopped(pd)) || !pm_read(f, 0, header, sizeof(header))) return false;
    switch (f->file_type) {
        case XX_FILE_TYPE_PARSEC_DTC:
            magic = "DTC";
            embedded = "DTC";
            version = 0x100;
            count = 12;
            revision = 4;
            flags = 0;
            break;
        case XX_FILE_TYPE_PARSEC_DMA:
            magic = "DMA";
            embedded = "DMA";
            version = 0x102;
            count = 25;
            revision = 4;
            flags = 0xffff;
            break;
        case XX_FILE_TYPE_PALLADIX_MUS:
            magic = "MUS";
            embedded = "PMA";
            version = 0x100;
            count = 15;
            revision = 7;
            flags = 0;
            break;
        case XX_FILE_TYPE_PALLADIX_SND:
            magic = "SND";
            embedded = "SND";
            version = 0x100;
            count = 16;
            revision = 6;
            flags = 0;
            break;
        default: return false;
    }
    if (memcmp(header, magic, 4) || memcmp(header + 12, embedded, 4) || xx_data_get_u16(header, sizeof(header), 4, false) != version ||
        xx_data_get_u16(header, sizeof(header), 6, false) != count || xx_data_get_u16(header, sizeof(header), 8, false) != revision ||
        xx_data_get_u16(header, sizeof(header), 16, false) != flags)
        return false;
    for (i = 18; i < sizeof(header); ++i)
        if (header[i]) return false;
    footer_at = xx_data_get_u16(header, sizeof(header), 10, false);
    module_at = 24 + 2 * count;
    logical = (int64_t)footer_at + 20;
    size = (logical + 15) & ~INT64_C(15);
    if (footer_at <= module_at || size != available || !pm_read(f, 24, table, 2 * count) || !pm_read(f, footer_at, footer, sizeof(footer))) return false;
    for (i = 0; i < count; ++i) {
        unsigned entry = xx_data_get_u16(table, 2 * count, 2 * i, false);
        if ((pd && xx_pd_is_stopped(pd)) || entry < module_at || entry >= footer_at) return false;
        for (j = 0; j < i; ++j)
            if (entry == xx_data_get_u16(table, 2 * count, 2 * j, false)) return false;
    }
    for (i = 0; i < sizeof(footer); ++i)
        if (footer[i] != (i % 2 ? 'S' : 'N')) return false;
    if (!pm_read(f, logical, padding, (size_t)(size - logical))) return false;
    for (i = 0; i < (unsigned)(size - logical); ++i)
        if (padding[i]) return false;
    if (!pm_add(f, s, "header.bin", 0, 24) || !pm_add(f, s, "entry-points.bin", 24, 2 * count) || !pm_add(f, s, "driver-image.bin", module_at, footer_at - module_at) ||
        !pm_add(f, s, "footer.bin", footer_at, size - footer_at))
        return false;
    s->size = size;
    return true;
}

void xx_legacy_sound_driver_init(xx_legacy_sound_driver *r, xx_io_device *d, int64_t base, xx_file_type_t type, const char *ext)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, base, type, ext);
    r->format.is_executable = true;
    r->format.is_archive = false;
    r->format.format_type = XX_TYPE_DRIVER;
    r->format.arch = XX_ARCH_X86_16;
    r->format.os = XX_OS_DOS;
    r->format.endian = XX_ENDIAN_LITTLE;
    xx_format_set_version(&r->format, type == XX_FILE_TYPE_PARSEC_DMA ? "1.2" : "1.0");
    xx_format_set_mime_type(&r->format, "application/x-dos-sound-driver");
}
void xx_legacy_sound_driver_destroy(xx_legacy_sound_driver *r)
{
    if (!r) return;
    xx_format_cleanup_extra_parameters(&r->format);
    xx_format_invalidate_memory_map(&r->format);
}

xx_file_type_t xx_legacy_sound_driver_detect(xx_io_device *device, int64_t base)
{
    uint8_t magic[4];
    xx_file_type_t type;
    xx_legacy_sound_driver reader;
    bool valid;
    if (!device || base < 0 || !xx_io_read_at(device, base, magic, sizeof(magic))) return XX_FILE_TYPE_UNKNOWN;
    if (!memcmp(magic, "DTC", 4)) type = XX_FILE_TYPE_PARSEC_DTC;
    else if (!memcmp(magic, "DMA", 4)) type = XX_FILE_TYPE_PARSEC_DMA;
    else if (!memcmp(magic, "MUS", 4)) type = XX_FILE_TYPE_PALLADIX_MUS;
    else if (!memcmp(magic, "SND", 4)) type = XX_FILE_TYPE_PALLADIX_SND;
    else return XX_FILE_TYPE_UNKNOWN;
    xx_legacy_sound_driver_init(&reader, device, base, type, "bin");
    valid = xx_format_is_valid(&reader.format, NULL);
    xx_legacy_sound_driver_destroy(&reader);
    return valid ? type : XX_FILE_TYPE_UNKNOWN;
}
