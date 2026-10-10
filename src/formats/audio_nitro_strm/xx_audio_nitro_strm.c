/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded reader for NitroStudio2's STRM/HEAD/DATA layout:
 * https://github.com/Gota7/NitroStudio2/blob/master/docs/specs/stream.md
 * Extracts each stored channel block, without ADPCM/PCM conversion.
 */
#include "xxfclib/formats/audio_nitro_strm/xx_audio_nitro_strm.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#ifndef XX_FILE_TYPE_AUDIO_NITRO_STRM
#define XX_FILE_TYPE_AUDIO_NITRO_STRM ((xx_file_type_t)1520)
#endif

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[0x40], data_header[8];
    uint64_t total, data_at, data_size, audio_at, expected, at, count;
    uint32_t blocks, block_size, last_size, samples_per_block, last_samples, total_samples;
    uint32_t b, head_size;
    unsigned ch, channels, encoding;
    bool be;
    char name[64];
    int64_t available = pm_available(f);
    if (available < 0x68 || !pm_read(f, 0, h, sizeof(h)) || xx_rt_memcmp(h, "STRM", 4)) return false;
    be = h[4] == 0xfe && h[5] == 0xff;
    if (!be && !(h[4] == 0xff && h[5] == 0xfe)) return false;
    if (xx_data_get_u16(h + 6, 2, 0, be) != 0x0100 || xx_data_get_u16(h + 12, 2, 0, be) != 0x10 || xx_data_get_u16(h + 14, 2, 0, be) != 2 ||
        xx_rt_memcmp(h + 0x10, "HEAD", 4))
        return false;
    total = xx_data_get_u32(h + 8, 4, 0, be);
    head_size = xx_data_get_u32(h + 0x14, 4, 0, be);
    if (total > (uint64_t)available || total < 0x68 || head_size < 0x50 || head_size > total - 0x10) return false;
    data_at = 0x10U + head_size;
    if (data_at > total - 8 || !pm_read(f, (int64_t)data_at, data_header, 8) || xx_rt_memcmp(data_header, "DATA", 4)) return false;
    data_size = xx_data_get_u32(data_header + 4, 4, 0, be);
    if (data_size < 8 || data_size > total - data_at || data_at + data_size != total) return false;
    encoding = h[0x18];
    channels = h[0x1a];
    if (encoding > 2 || h[0x19] > 1 || !channels || channels > 16 || h[0x1b] || xx_data_get_u16(h + 0x1c, 2, 0, be) < 4000 ||
        xx_data_get_u32(h + 0x28, 4, 0, be) != data_at + 8)
        return false;
    total_samples = xx_data_get_u32(h + 0x24, 4, 0, be);
    blocks = xx_data_get_u32(h + 0x2c, 4, 0, be);
    block_size = xx_data_get_u32(h + 0x30, 4, 0, be);
    samples_per_block = xx_data_get_u32(h + 0x34, 4, 0, be);
    last_size = xx_data_get_u32(h + 0x38, 4, 0, be);
    last_samples = xx_data_get_u32(h + 0x3c, 4, 0, be);
    if (!blocks || !block_size || !samples_per_block || !last_size || last_size > block_size || !last_samples || last_samples > samples_per_block ||
        (uint64_t)(blocks - 1) * samples_per_block + last_samples != total_samples || (uint64_t)blocks * channels > 8191U)
        return false;
    expected = ((uint64_t)(blocks - 1) * block_size + last_size) * channels;
    audio_at = data_at + 8;
    if (expected != data_size - 8) return false;
    if (!pm_add(f, s, "head.bin", 0x10, head_size)) return false;
    at = audio_at;
    count = 0;
    for (b = 0; b < blocks; ++b) {
        uint32_t size = b + 1U == blocks ? last_size : block_size;
        for (ch = 0; ch < channels; ++ch) {
            if (pd && xx_pd_is_stopped(pd)) return false;
            (void)xx_rt_snprintf(name, sizeof(name), "block-%05u-channel-%02u.%s", b, ch, encoding == 2 ? "ima" : encoding == 1 ? "pcm16" : "pcm8");
            if (!pm_add(f, s, name, (int64_t)at, size)) return false;
            at += size;
            ++count;
        }
    }
    if (at != total || count != (uint64_t)blocks * channels) return false;
    s->size = (int64_t)total;
    return true;
}

void xx_audio_nitro_strm_init(xx_audio_nitro_strm *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AUDIO_NITRO_STRM, "strm");
    }
}
xx_audio_nitro_strm *xx_audio_nitro_strm_create(xx_io_device *d, int64_t b)
{
    xx_audio_nitro_strm *r = (xx_audio_nitro_strm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_audio_nitro_strm_init(r, d, b);
    return r;
}
void xx_audio_nitro_strm_destroy(xx_audio_nitro_strm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_audio_nitro_strm_free(xx_audio_nitro_strm *r)
{
    if (r) {
        xx_audio_nitro_strm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_audio_nitro_strm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_audio_nitro_strm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
