/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Shockwave Audio v3 with embedded MPEG Layer III frames. The fixed header
 * fields and sample boundary were verified on the creator's sample files:
 * https://github.com/thorsted/PRONOM_Research/tree/main/Submissions/Shockwave%20Audio/Samples
 * https://preservation.tylerthorsted.com/2023/08/04/shockwave-audio/
 */
#include "xxfclib/formats/audio_shockwave_swa/xx_audio_shockwave_swa.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#ifndef XX_FILE_TYPE_AUDIO_SHOCKWAVE_SWA
#define XX_FILE_TYPE_AUDIO_SHOCKWAVE_SWA ((xx_file_type_t)1525)
#endif

static bool swa_mpeg_frame(const uint8_t *p, uint32_t expected_rate, unsigned *expected_version, uint32_t *length)
{
    static const uint16_t mpeg1_bitrates[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const uint16_t mpeg2_bitrates[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const uint32_t rates[4][3] = {{11025, 12000, 8000}, {0, 0, 0}, {22050, 24000, 16000}, {44100, 48000, 32000}};
    uint32_t word = xx_data_get_u32(p, 4, 0, true), rate, bitrate;
    unsigned version = (word >> 19) & 3U, layer = (word >> 17) & 3U;
    unsigned bitrate_index = (word >> 12) & 15U, rate_index = (word >> 10) & 3U;
    unsigned padding = (word >> 9) & 1U;
    if ((word & 0xffe00000U) != 0xffe00000U || version == 1 || layer != 1 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3) return false;
    rate = rates[version][rate_index];
    if (rate != expected_rate || (*expected_version != 4U && version != *expected_version)) return false;
    bitrate = (version == 3U ? mpeg1_bitrates : mpeg2_bitrates)[bitrate_index];
    *length = (version == 3U ? 144000U : 72000U) * bitrate / rate + padding;
    if (*length < 24U) return false;
    *expected_version = version;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t header[41], frame[4];
    int64_t available = pm_available(f), at;
    uint32_t header_length, rate, frame_length;
    unsigned version = 4U;
    uint64_t frames = 0;
    if (available < 64 || !pm_read(f, 0, header, sizeof(header))) return false;
    header_length = xx_data_get_u32(header, 4, 0, true);
    rate = xx_data_get_u32(header + 8, 4, 0, true);
    if (header_length < 60U || header_length > 1048576U || xx_data_get_u32(header + 4, 4, 0, true) != 3U || xx_rt_memcmp(header + 36, "MACRZ", 5) ||
        (uint64_t)header_length + 8U > (uint64_t)available)
        return false;
    at = (int64_t)header_length + 4;
    while (at < available) {
        if ((pd && xx_pd_is_stopped(pd)) || available - at < 4 || !pm_read(f, at, frame, sizeof(frame)) || !swa_mpeg_frame(frame, rate, &version, &frame_length) ||
            (uint64_t)frame_length > (uint64_t)(available - at))
            return false;
        at += frame_length;
        ++frames;
    }
    if (frames < 2 || !pm_add(f, s, "header.bin", 0, (int64_t)header_length + 4) ||
        !pm_add(f, s, "audio.mp3", (int64_t)header_length + 4, available - ((int64_t)header_length + 4)))
        return false;
    s->size = available;
    return true;
}

void xx_audio_shockwave_swa_init(xx_audio_shockwave_swa *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AUDIO_SHOCKWAVE_SWA, "swa");
    }
}
xx_audio_shockwave_swa *xx_audio_shockwave_swa_create(xx_io_device *d, int64_t b)
{
    xx_audio_shockwave_swa *r = (xx_audio_shockwave_swa *)xx_mem_alloc(sizeof(*r));
    if (r) xx_audio_shockwave_swa_init(r, d, b);
    return r;
}
void xx_audio_shockwave_swa_destroy(xx_audio_shockwave_swa *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_audio_shockwave_swa_free(xx_audio_shockwave_swa *r)
{
    if (r) {
        xx_audio_shockwave_swa_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_audio_shockwave_swa_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_audio_shockwave_swa_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
