/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native TransCopy TC table/track reader. Original PCE is used for oracles.
 */
#include "xxfclib/formats/pce_tc/xx_pce_tc.h"
#include "../xx_payload_members.h"
#include <string.h>
#include "xxfclib/data/xx_data.h"

#ifdef PCE_TC
#define TC_FILE_TYPE XX_FILE_TYPE_PCE_TC
#else
#define TC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define TC_HEADER_SIZE 16384U
#define TC_MAX_TRACKS 198U
#define TC_MAX_FILE (32U * 1024U * 1024U)

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    uint8_t header[TC_HEADER_SIZE];
    int64_t available = pm_available(format);
    uint32_t cylinders, heads, cylinder, head;
    uint64_t used_start[TC_MAX_TRACKS], used_end[TC_MAX_TRACKS];
    size_t tracks = 0U;
    int64_t end = TC_HEADER_SIZE;
    if (available < TC_HEADER_SIZE || available > TC_MAX_FILE ||
        (pd && xx_pd_is_stopped(pd)) ||
        !pm_read(format, 0, header, sizeof(header)) ||
        header[0] != 0x5aU || header[1] != 0xa5U) return false;
    cylinders = (uint32_t)header[0x102U] + 1U;
    heads = header[0x103U];
    if (cylinders == 0U || cylinders > 99U ||
        heads == 0U || heads > 2U) return false;
    for (cylinder = 0U; cylinder < cylinders; ++cylinder) {
        for (head = 0U; head < heads; ++head) {
            uint32_t index = cylinder * 2U + head;
            uint32_t raw_offset = xx_data_get_u16(header + 0x305U + index * 2U, 2, 0, true);
            uint32_t length = xx_data_get_u16(header + 0x505U + index * 2U, 2, 0, false);
            uint64_t offset = (uint64_t)raw_offset << 8U;
            size_t prior;
            char name[64];
            if ((pd && xx_pd_is_stopped(pd)) || tracks >= TC_MAX_TRACKS)
                return false;
            /* PCE's writer leaves 0x3333 in length slots for absent tracks;
             * an offset of zero is their actual absence marker. */
            if (raw_offset == 0U) continue;
            if (length == 0U ||
                offset < TC_HEADER_SIZE ||
                offset > (uint64_t)available ||
                length > (uint64_t)available - offset) return false;
            for (prior = 0U; prior < tracks; ++prior)
                if (offset < used_end[prior] &&
                    offset + length > used_start[prior]) return false;
            used_start[tracks] = offset;
            used_end[tracks] = offset + length;
            ++tracks;
            (void)xx_rt_snprintf(name, sizeof(name), "track%03u_%u.bit",
                                 cylinder, head);
            if (!pm_add(format, stream, name, (int64_t)offset, length))
                return false;
            if (end < (int64_t)(offset + length))
                end = (int64_t)(offset + length);
        }
    }
    if (tracks == 0U) return false;
    stream->size = end;
    return true;
}

void xx_pce_tc_init(xx_pce_tc *reader, xx_io_device *device,
                    int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, TC_FILE_TYPE, "tc");
}

xx_pce_tc *xx_pce_tc_create(xx_io_device *device, int64_t base_address) {
    xx_pce_tc *reader = (xx_pce_tc *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pce_tc_init(reader, device, base_address);
    return reader;
}

void xx_pce_tc_destroy(xx_pce_tc *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pce_tc_free(xx_pce_tc *reader) {
    if (reader) {
        xx_pce_tc_destroy(reader);
        xx_mem_free(reader);
    }
}
