/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/mpegaudiodecheader.c
 * MPEG1 LayerIII unprotected frames, stable sample rate/channel count, complete side-information grammar and bounded bit-reservoir/part lengths. Optional unflagged
 * ID3v2.3/2.4 frames and padding are fully length-framed. Original metadata and encoded audio frames are exported; MPEG2/2.5, free bitrate, protected frames,
 * ID3v1/extended/unsynchronised tags and audio decoding are unsupported. File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/audio_mpeg_mp3/xx_audio_mpeg_mp3.h"
#include "../common/xx_audiovisual_components.h"
static __inline uint32_t mp_syncsafe(const uint8_t *p)
{
    return (uint32_t)p[0] << 21 | (uint32_t)p[1] << 14 | (uint32_t)p[2] << 7 | p[3];
}
static bool mp_id3(const uint8_t *b, uint64_t at, uint64_t n, uint64_t *end)
{
    uint64_t p, limit;
    unsigned version;
    uint32_t bytes;
    if (!audiovisual_span(at, 10, n) || xx_rt_memcmp(b + at, "ID3", 3) || (version = b[at + 3]) < 3 || version > 4 || b[at + 4] == 255 || b[at + 5] ||
        (b[at + 6] | b[at + 7] | b[at + 8] | b[at + 9]) & 128)
        return false;
    bytes = mp_syncsafe(b + at + 6);
    p = at + 10;
    limit = p + bytes;
    if (!audiovisual_span(p, bytes, n) || bytes > 4194304) return false;
    while (p < limit) {
        uint32_t len;
        unsigned i;
        if (!b[p]) {
            if (!audiovisual_zero(b + p, limit - p)) return false;
            p = limit;
            break;
        }
        if (!audiovisual_span(p, 10, limit)) return false;
        for (i = 0; i < 4; ++i)
            if (!((b[p + i] >= 'A' && b[p + i] <= 'Z') || (b[p + i] >= '0' && b[p + i] <= '9'))) return false;
        if (b[p + 8] || b[p + 9]) {
            return false;
        }
        if (version == 4) {
            if ((b[p + 4] | b[p + 5] | b[p + 6] | b[p + 7]) & 128) return false;
            len = mp_syncsafe(b + p + 4);
        } else len = xx_data_get_u32(b + p + 4, 4, 0, true);
        if (!len || !audiovisual_span(p + 10, len, limit)) {
            return false;
        }
        p += 10 + len;
    }
    *end = limit;
    return true;
}
static bool audiovisual_quick(Abstractformat *f, uint64_t n)
{
    uint8_t h[4];
    return audiovisual_probe(f, n, h, 4) && ((!xx_rt_memcmp(h, "ID3", 3) && (h[3] == 3 || h[3] == 4)) || (h[0] == 255 && h[1] == 0xfb));
}
static bool audiovisual_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    static const uint16_t bitrates[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const uint32_t rates[3] = {44100, 48000, 32000};
    uint64_t at = 0, mainbytes = 0;
    unsigned frames = 0, sr = 99, ch = 99;
    char label[40];
    if (n >= 3 && !xx_rt_memcmp(b, "ID3", 3)) {
        if (!mp_id3(b, 0, n, &at) || !audiovisual_emit(f, s, "id3v2.bin", 0, at, n)) return false;
    }
    while (at < n) {
        uint32_t r, br, size, channels, side, reservoir, value, partbits = 0;
        unsigned gr, c, j;
        audiovisual_bits q;
        if (audiovisual_stop(pd) || !audiovisual_span(at, 4, n) || b[at] != 255 || b[at + 1] != 0xfb || (br = b[at + 2] >> 4) < 1 || br > 14 ||
            (r = (b[at + 2] >> 2) & 3) >= 3 || (b[at + 3] & 3) == 2)
            return false;
        channels = (b[at + 3] >> 6) == 3 ? 1 : 2;
        side = channels == 1 ? 17 : 32;
        size = 144000U * bitrates[br] / rates[r] + ((b[at + 2] >> 1) & 1);
        if (size <= 4 + side || !audiovisual_span(at, size, n)) {
            return false;
        }
        if (sr == 99) {
            sr = r;
            ch = channels;
        } else if (sr != r || ch != channels) return false;
        q.b = b + at + 4;
        q.bit = 0;
        q.end = (uint64_t)side * 8;
        if (!audiovisual_bits_get(&q, 9, &reservoir) || reservoir > mainbytes || !audiovisual_bits_skip(&q, channels == 1 ? 5 : 3) ||
            !audiovisual_bits_skip(&q, channels * 4))
            return false;
        for (gr = 0; gr < 2; ++gr)
            for (c = 0; c < channels; ++c) {
                uint32_t window, region;
                if (!audiovisual_bits_get(&q, 12, &value)) {
                    return false;
                }
                partbits += value;
                if (!audiovisual_bits_get(&q, 9, &value) || value > 288 || !audiovisual_bits_skip(&q, 12) || !audiovisual_bits_get(&q, 1, &window)) return false;
                if (window) {
                    if (!audiovisual_bits_get(&q, 2, &value) || !value || !audiovisual_bits_skip(&q, 1)) return false;
                    for (j = 0; j < 2; ++j)
                        if (!audiovisual_bits_get(&q, 5, &value) || value == 4 || value == 14) return false;
                    if (!audiovisual_bits_skip(&q, 9)) return false;
                } else {
                    for (j = 0; j < 3; ++j)
                        if (!audiovisual_bits_get(&q, 5, &value) || value == 4 || value == 14) return false;
                    if (!audiovisual_bits_get(&q, 4, &region) || !audiovisual_bits_get(&q, 3, &value) || region + value > 20) return false;
                }
                if (!audiovisual_bits_skip(&q, 3)) return false;
            }
        if (q.bit != q.end || partbits > ((uint64_t)reservoir + size - 4 - side) * 8) return false;
        mainbytes += size - 4 - side;
        xx_rt_snprintf(label, sizeof(label), "frame-%u.mp3", frames++);
        if (!audiovisual_emit(f, s, label, at, size, n)) return false;
        at += size;
    }
    s->size = (int64_t)at;
    return frames > 0;
}

void xx_audio_mpeg_mp3_init(xx_audio_mpeg_mp3 *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_AUDIO_MPEG_MP3, "mp3");
    }
}
xx_audio_mpeg_mp3 *xx_audio_mpeg_mp3_create(xx_io_device *d, int64_t at)
{
    xx_audio_mpeg_mp3 *r = (xx_audio_mpeg_mp3 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_audio_mpeg_mp3_init(r, d, at);
    return r;
}
void xx_audio_mpeg_mp3_destroy(xx_audio_mpeg_mp3 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_audio_mpeg_mp3_free(xx_audio_mpeg_mp3 *r)
{
    if (r) {
        xx_audio_mpeg_mp3_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_audio_mpeg_mp3_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_audio_mpeg_mp3_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
