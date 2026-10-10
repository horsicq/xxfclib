/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent MONSTER.SOU VCTL/VTLK/VOC walker. Layout references:
 * https://github.com/scummvm/scummvm-tools/blob/master/engines/scumm/compress_scumm_sou.cpp
 * https://github.com/scummvm/scummvm/blob/master/engines/scumm/sound.cpp
 * Exports each original VOC plus its mouth-sync data; no speech decoding.
 */
#include "xxfclib/formats/audio_scumm_sou/xx_audio_scumm_sou.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#ifndef XX_FILE_TYPE_AUDIO_SCUMM_SOU
#define XX_FILE_TYPE_AUDIO_SCUMM_SOU ((xx_file_type_t)1522)
#endif

static bool sou_voc(Abstractformat *f, uint64_t at, uint64_t limit, uint64_t *end, xx_pd_struct *pd)
{
    uint8_t h[26], bh[4];
    uint64_t p;
    unsigned blocks = 0, has_audio = 0;
    uint16_t version, checksum, header_size;
    if (at > limit || limit - at < 26 || !pm_read(f, (int64_t)at, h, sizeof(h)) || xx_rt_memcmp(h, "Creative Voice File", 19) || h[19] != 0x1a) return false;
    header_size = xx_data_get_u16(h + 20, 2, 0, false);
    version = xx_data_get_u16(h + 22, 2, 0, false);
    checksum = xx_data_get_u16(h + 24, 2, 0, false);
    if (header_size < 26 || header_size > 256 || header_size > limit - at || (uint16_t)(~version + 0x1234U) != checksum) return false;
    p = at + header_size;
    while (p < limit && ++blocks <= 4096U) {
        uint32_t size;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!pm_read(f, (int64_t)p, bh, 1)) return false;
        if (bh[0] == 0) {
            *end = p + 1;
            return has_audio != 0;
        }
        if (bh[0] > 9 || limit - p < 4 || !pm_read(f, (int64_t)p, bh, 4)) return false;
        size = (uint32_t)bh[1] | ((uint32_t)bh[2] << 8) | ((uint32_t)bh[3] << 16);
        if (size > limit - p - 4) return false;
        if (bh[0] == 1 || bh[0] == 2 || bh[0] == 9) has_audio = 1;
        p += 4U + size;
    }
    return false;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t header[8], ch[8];
    uint64_t p = 8, limit;
    unsigned voices = 0;
    char label[48];
    int64_t available = pm_available(f);
    if (available < 8 || !pm_read(f, 0, header, 8) || xx_rt_memcmp(header, "SOU ", 4) || xx_data_get_u32(header + 4, 4, 0, true)) return false;
    limit = (uint64_t)available;
    while (p < limit) {
        uint32_t vctl_size;
        uint64_t voc_at, voc_end, voc_payload_end;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (limit - p < 8 || !pm_read(f, (int64_t)p, ch, 8) || (xx_rt_memcmp(ch, "VCTL", 4) && xx_rt_memcmp(ch, "VTTL", 4))) return false;
        vctl_size = xx_data_get_u32(ch + 4, 4, 0, true);
        if (vctl_size < 8 || vctl_size > 1024 || (vctl_size & 1U) || vctl_size > limit - p || voices >= 2048U) return false;
        (void)xx_rt_snprintf(label, sizeof(label), "voice-%04u-sync.bin", voices);
        if (!pm_add(f, s, label, (int64_t)(p + 8), vctl_size - 8U)) return false;
        p += vctl_size;
        if (p == limit) break; /* Some MONSTER.SOU files end with an unpaired VCTL. */
        if (limit - p >= 8 && pm_read(f, (int64_t)p, ch, 8) && !xx_rt_memcmp(ch, "VTLK", 4)) {
            uint32_t vt_size = xx_data_get_u32(ch + 4, 4, 0, true);
            if (vt_size < 34 || vt_size > limit - p) return false;
            voc_at = p + 8;
            if (!sou_voc(f, voc_at, p + vt_size, &voc_end, pd)) return false;
            voc_payload_end = voc_end;
            while (voc_end < p + vt_size) {
                uint8_t z;
                if (p + vt_size - voc_end > 3 || !pm_read(f, (int64_t)voc_end, &z, 1) || z) return false;
                ++voc_end;
            }
            p += vt_size;
        } else {
            voc_at = p;
            if (!sou_voc(f, voc_at, limit, &voc_end, pd)) return false;
            voc_payload_end = voc_end;
            p = voc_end;
        }
        (void)xx_rt_snprintf(label, sizeof(label), "voice-%04u.voc", voices);
        if (!pm_add(f, s, label, (int64_t)voc_at, (int64_t)(voc_payload_end - voc_at))) return false;
        ++voices;
    }
    if (!voices || p != limit) return false;
    s->size = (int64_t)limit;
    return true;
}

void xx_audio_scumm_sou_init(xx_audio_scumm_sou *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AUDIO_SCUMM_SOU, "sou");
    }
}
xx_audio_scumm_sou *xx_audio_scumm_sou_create(xx_io_device *d, int64_t b)
{
    xx_audio_scumm_sou *r = (xx_audio_scumm_sou *)xx_mem_alloc(sizeof(*r));
    if (r) xx_audio_scumm_sou_init(r, d, b);
    return r;
}
void xx_audio_scumm_sou_destroy(xx_audio_scumm_sou *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_audio_scumm_sou_free(xx_audio_scumm_sou *r)
{
    if (r) {
        xx_audio_scumm_sou_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_audio_scumm_sou_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_audio_scumm_sou_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
