/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/libsndfile/libsndfile/blob/master/src/paf.c
 * Preserves stored PCM/packed 24-bit data; does not decode PARIS packing.
 */
#include "xxfclib/formats/ensoniq_paf/xx_ensoniq_paf.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
#ifndef ENSONIQ_PAF
#define XX_FILE_TYPE_ENSONIQ_PAF ((xx_file_type_t)1541)
#endif

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[24];
    bool be;
    uint32_t version, endian, rate, encoding, channels;
    int64_t available = pm_available(f), data_size;
    (void)pd;
    if (available <= 2048 || !pm_read(f, 0, h, sizeof(h))) return false;
    if (!xx_rt_memcmp(h, " paf", 4)) be = true;
    else if (!xx_rt_memcmp(h, "fap ", 4)) be = false;
    else return false;
    version = be ? xx_data_get_u32(h + 4, 4, 0, true) : xx_data_get_u32(h + 4, 4, 0, false);
    endian = be ? xx_data_get_u32(h + 8, 4, 0, true) : xx_data_get_u32(h + 8, 4, 0, false);
    rate = be ? xx_data_get_u32(h + 12, 4, 0, true) : xx_data_get_u32(h + 12, 4, 0, false);
    encoding = be ? xx_data_get_u32(h + 16, 4, 0, true) : xx_data_get_u32(h + 16, 4, 0, false);
    channels = be ? xx_data_get_u32(h + 20, 4, 0, true) : xx_data_get_u32(h + 20, 4, 0, false);
    if (version || endian > 1 || !rate || rate > 384000 || encoding > 2 || !channels || channels > 1024) return false;
    data_size = available - 2048;
    if (encoding == 1 && data_size < (int64_t)channels * 32) return false;
    if (encoding == 0 && data_size < (int64_t)channels * 2) return false;
    if (encoding == 2 && data_size < (int64_t)channels) return false;
    if (!pm_add(f, s, "header.bin", 0, 2048) || !pm_add(f, s, encoding == 1 ? "packed-24bit.pafdata" : "pcm.pafdata", 2048, data_size)) return false;
    s->size = available;
    return true;
}

void xx_ensoniq_paf_init(xx_ensoniq_paf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ENSONIQ_PAF, "paf");
    }
}
xx_ensoniq_paf *xx_ensoniq_paf_create(xx_io_device *d, int64_t b)
{
    xx_ensoniq_paf *r = (xx_ensoniq_paf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ensoniq_paf_init(r, d, b);
    return r;
}
void xx_ensoniq_paf_destroy(xx_ensoniq_paf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_ensoniq_paf_free(xx_ensoniq_paf *r)
{
    if (r) {
        xx_ensoniq_paf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_ensoniq_paf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_ensoniq_paf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
