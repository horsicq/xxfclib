/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout/validation parity: Formats/audio/xsm8.{h,cpp}.
 * Stored mono unsigned 8-bit PCM; the original PIT divisor is retained in
 * the separately extractable header. No playback or PCM conversion occurs.
 */
#include "xxfclib/formats/sm8/xx_sm8.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t magic[6] = {'S','M','8',0,0,1};
    uint8_t header[10];
    uint16_t pcm_size;
    int64_t available = pm_available(f);
    if ((pd && xx_pd_is_stopped(pd)) || available < 10 ||
        !pm_read(f, 0, header, sizeof(header)) ||
        xx_rt_memcmp(header, magic, sizeof(magic))) return false;
    pcm_size = xx_data_get_u16(header, sizeof(header), 6, false);
    if (available != 10 + (int64_t)pcm_size) return false;
    if (!pm_add(f, s, "header.bin", 0, 10) ||
        !pm_add(f, s, "samples-u8.pcm", 10, pcm_size)) return false;
    s->size = available;
    return true;
}

void xx_sm8_init(xx_sm8 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SM8, "sm8");
        r->format.is_archive = false;
        r->format.format_type = XX_TYPE_RAW;
        r->format.endian = XX_ENDIAN_LITTLE;
        xx_format_set_mime_type(&r->format, "audio/x-sm8");
    }
}
xx_sm8 *xx_sm8_create(xx_io_device *d, int64_t b)
{
    xx_sm8 *r = (xx_sm8 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sm8_init(r, d, b);
    return r;
}
void xx_sm8_destroy(xx_sm8 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sm8_free(xx_sm8 *r)
{
    if (r) { xx_sm8_destroy(r); xx_mem_free(r); }
}
bool xx_sm8_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_sm8_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
xx_file_type_t xx_sm8_detect(xx_io_device *d, int64_t b)
{
    xx_sm8 reader;
    bool result;
    xx_sm8_init(&reader, d, b);
    result = pm_valid(&reader.format, NULL);
    xx_sm8_destroy(&reader);
    return result ? XX_FILE_TYPE_SM8 : XX_FILE_TYPE_UNKNOWN;
}
