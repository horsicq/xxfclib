/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * AnaDisk sector stream, independently checked against PCE writer/reader.
 */
#include "xxfclib/formats/pce_anadisk/xx_pce_anadisk.h"
#include "../xx_payload_members.h"

#ifdef PCE_ANADISK
#define ANADISK_FILE_TYPE XX_FILE_TYPE_PCE_ANADISK
#else
#define ANADISK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ANADISK_PROBE_MAX_FILE (64U * 1024U * 1024U)
#define ANADISK_PROBE_MAX_SECTORS 8192U
#define ANADISK_MAX_SECTORS 65536U
#define ANADISK_PROBE_MAX_SECTOR_SIZE 16384U

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    int64_t available = pm_available(format), cursor = 0;
    uint32_t sectors = 0U, nonempty = 0U;
    bool conservative = ((const xx_pce_anadisk *)format)->conservative_probe;
    if (available < 0 ||
        (conservative && available > ANADISK_PROBE_MAX_FILE) ||
        (pd && xx_pd_is_stopped(pd))) return false;
    if (available == 0) {
        stream->size = 0;
        return !conservative;
    }
    if (available < 8) return false;
    while (cursor < available) {
        uint8_t header[8];
        uint32_t physical_c, physical_h, logical_c, logical_h, sector_id;
        uint32_t mfm_size, payload_size;
        char name[64];
        if (available - cursor < 8 || sectors >= ANADISK_MAX_SECTORS ||
            (conservative && sectors >= ANADISK_PROBE_MAX_SECTORS) ||
            (pd && xx_pd_is_stopped(pd)) ||
            !pm_read(format, cursor, header, sizeof(header))) return false;
        physical_c = header[0];
        physical_h = header[1];
        logical_c = header[2];
        logical_h = header[3];
        sector_id = header[4];
        mfm_size = header[5];
        payload_size = pm_le16(header + 6U);
        /* The PCE writer stores these header bytes verbatim. Restrict the
         * ranges only when guessing a format with no file signature. */
        if (payload_size > (uint64_t)(available - cursor - 8) ||
            (conservative &&
             (physical_c > 99U || physical_h > 3U || logical_c > 99U ||
              logical_h > 3U || sector_id > 63U || mfm_size > 7U ||
              payload_size > ANADISK_PROBE_MAX_SECTOR_SIZE))) return false;
        (void)xx_rt_snprintf(name, sizeof(name),
                             "c%03u_h%u_s%03u.bin",
                             physical_c, physical_h, sector_id);
        if (!pm_add(format, stream, name, cursor + 8,
                    (int64_t)payload_size)) return false;
        cursor += 8 + (int64_t)payload_size;
        ++sectors;
        if (payload_size != 0U) ++nonempty;
    }
    if (sectors == 0U || (conservative && nonempty < 2U)) return false;
    stream->size = cursor;
    return true;
}

void xx_pce_anadisk_init(xx_pce_anadisk *reader, xx_io_device *device,
                         int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address,
            ANADISK_FILE_TYPE, "ana");
}

xx_pce_anadisk *xx_pce_anadisk_create(xx_io_device *device,
                                       int64_t base_address) {
    xx_pce_anadisk *reader =
        (xx_pce_anadisk *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pce_anadisk_init(reader, device, base_address);
    return reader;
}

void xx_pce_anadisk_destroy(xx_pce_anadisk *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pce_anadisk_free(xx_pce_anadisk *reader) {
    if (reader) {
        xx_pce_anadisk_destroy(reader);
        xx_mem_free(reader);
    }
}

void xx_pce_anadisk_set_conservative_probe(xx_pce_anadisk *reader,
                                            bool enabled) {
    if (reader) reader->conservative_probe = enabled;
}
