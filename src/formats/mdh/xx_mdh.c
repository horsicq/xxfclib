/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout/validation parity: Formats/audio/xmdh.{h,cpp}.
 * Exposes the control fields, complete instrument table and original track
 * program. Track opcodes are retained verbatim, not synthesized as audio.
 */
#include "xxfclib/formats/mdh/xx_mdh.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint16_t codes[8] = {
        0x00f0, 0x01d2, 0x02b4, 0x0396, 0x0478, 0x0564, 0x0614, 0x070a
    };
    uint8_t header[74], end[2];
    unsigned i, j, active;
    int64_t available = pm_available(f);
    if ((pd && xx_pd_is_stopped(pd)) || available < 76 ||
        !pm_read(f, 0, header, sizeof(header)) ||
        xx_rt_memcmp(header, "MDH\0", 4) ||
        xx_data_get_u16(header, sizeof(header), 4, false) != 0 ||
        xx_data_get_u16(header, sizeof(header), 6, false) != 74 ||
        header[9] != 0) return false;
    active = header[8];
    if (active < 1 || active > 8) return false;
    for (i = 0; i < 8; ++i) {
        const uint8_t *record = header + 10 + i * 8;
        uint16_t code = xx_data_get_u16(record, 8, 1, false);
        if ((pd && xx_pd_is_stopped(pd)) || record[0] || record[3]) return false;
        if (i < active) {
            if (code != codes[i] || record[7] == 0) return false;
            for (j = 0; j < 4; ++j)
                if (record[4 + j] != ((unsigned)record[7] * (j + 1)) / 4)
                    return false;
        } else {
            if (code != 0) return false;
            for (j = 0; j < 4; ++j) if (record[4 + j]) return false;
        }
    }
    if (!pm_read(f, available - 2, end, sizeof(end)) || end[0] != 0x60 || end[1])
        return false;
    if (!pm_add(f, s, "control.bin", 0, 10) ||
        !pm_add(f, s, "record-table.bin", 10, 64) ||
        !pm_add(f, s, "track.bin", 74, available - 74)) return false;
    s->size = available;
    return true;
}

void xx_mdh_init(xx_mdh *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_MDH, "mdh");
        r->format.is_archive = false;
        r->format.format_type = XX_TYPE_RAW;
        r->format.endian = XX_ENDIAN_LITTLE;
        xx_format_set_mime_type(&r->format, "audio/x-parsec-mdh");
    }
}
xx_mdh *xx_mdh_create(xx_io_device *d, int64_t b)
{
    xx_mdh *r = (xx_mdh *)xx_mem_alloc(sizeof(*r));
    if (r) xx_mdh_init(r, d, b);
    return r;
}
void xx_mdh_destroy(xx_mdh *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_mdh_free(xx_mdh *r)
{
    if (r) { xx_mdh_destroy(r); xx_mem_free(r); }
}
bool xx_mdh_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_mdh_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
xx_file_type_t xx_mdh_detect(xx_io_device *d, int64_t b)
{
    xx_mdh reader;
    bool result;
    xx_mdh_init(&reader, d, b);
    result = pm_valid(&reader.format, NULL);
    xx_mdh_destroy(&reader);
    return result ? XX_FILE_TYPE_MDH : XX_FILE_TYPE_UNKNOWN;
}
