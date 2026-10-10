/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/alp.c
 * Preserves LEGO Racers ALP IMA-ADPCM bytes without decoding.
 */
#include "xxfclib/formats/lego_alp/xx_lego_alp.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
#ifndef LEGO_ALP
#define XX_FILE_TYPE_LEGO_ALP ((xx_file_type_t)1543)
#endif

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[20];
    uint32_t header_size, rate, channels;
    int64_t available = pm_available(f);
    (void)pd;
    if (available <= 16 || !pm_read(f, 0, h, 16) || xx_rt_memcmp(h, "ALP ", 4) || xx_rt_memcmp(h + 8, "ADPCM", 5) || h[13] != 0) return false;
    header_size = xx_data_get_u32(h + 4, 4, 0, false);
    if (header_size != 8 && header_size != 12) return false;
    channels = h[15];
    if (channels < 1 || channels > 2 || available <= 8 + (int64_t)header_size) return false;
    if (header_size == 12) {
        if (!pm_read(f, 16, h + 16, 4)) return false;
        rate = xx_data_get_u32(h + 16, 4, 0, false);
        if (!rate || rate > 44100) return false;
    } else rate = 22050;
    (void)rate;
    if (!pm_add(f, s, "header.bin", 0, 8 + header_size) || !pm_add(f, s, "audio.alp-adpcm", 8 + header_size, available - 8 - header_size)) return false;
    s->size = available;
    return true;
}

void xx_lego_alp_init(xx_lego_alp *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LEGO_ALP, "tun");
    }
}
xx_lego_alp *xx_lego_alp_create(xx_io_device *d, int64_t b)
{
    xx_lego_alp *r = (xx_lego_alp *)xx_mem_alloc(sizeof(*r));
    if (r) xx_lego_alp_init(r, d, b);
    return r;
}
void xx_lego_alp_destroy(xx_lego_alp *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lego_alp_free(xx_lego_alp *r)
{
    if (r) {
        xx_lego_alp_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lego_alp_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_lego_alp_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
