/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent PVF1 binary/PVF2 ASCII stored-sample reader. References:
 * https://github.com/libsndfile/libsndfile/blob/master/src/pvf.c
 * https://sources.debian.org/src/mgetty/1.1.33-2/voice/doc/Readme.pvftools/
 */
#include "xxfclib/formats/audio_pvf/xx_audio_pvf.h"
#include "../xx_payload_members.h"

#ifndef XX_FILE_TYPE_AUDIO_PVF
#define XX_FILE_TYPE_AUDIO_PVF ((xx_file_type_t)1524)
#endif

static bool pvf_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
static bool pvf_num(const uint8_t *p, size_t end, size_t *cursor, uint32_t *out)
{
    uint64_t n = 0;
    size_t i = *cursor, start;
    while (i < end && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r')) ++i;
    start = i;
    while (i < end && p[i] >= '0' && p[i] <= '9') {
        n = n * 10U + (unsigned)(p[i] - '0');
        if (n > 1000000U) return false;
        ++i;
    }
    if (i == start) return false;
    *out = (uint32_t)n;
    *cursor = i;
    return true;
}

static bool pvf_ascii(Abstractformat *f, uint64_t at, uint64_t limit, unsigned channels, xx_pd_struct *pd)
{
    uint8_t buffer[4096];
    uint64_t count = 0, value = 0;
    bool digit = false, negative = false;
    while (at < limit) {
        size_t i, n = (size_t)(limit - at > sizeof(buffer) ? sizeof(buffer) : limit - at);
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)at, buffer, n)) return false;
        for (i = 0; i < n; ++i) {
            unsigned char c = buffer[i];
            if (c >= '0' && c <= '9') {
                digit = true;
                value = value * 10U + (unsigned)(c - '0');
                if (value > (negative ? 2147483648ULL : 2147483647ULL)) return false;
            } else if (c == '-' && !digit && !negative) {
                negative = true;
            } else if (pvf_space(c)) {
                if (negative && !digit) return false;
                if (digit) {
                    ++count;
                    value = 0;
                    digit = false;
                    negative = false;
                }
            } else return false;
        }
        at += n;
    }
    if (negative && !digit) return false;
    if (digit) ++count;
    return count != 0 && count % channels == 0;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[128];
    size_t at = 5, n, i;
    uint32_t channels, rate, bits;
    bool ascii;
    int64_t available = pm_available(f);
    if (available < 10) return false;
    n = (size_t)(available < (int64_t)sizeof(h) ? available : (int64_t)sizeof(h));
    if (!pm_read(f, 0, h, n) || xx_rt_memcmp(h, "PVF", 3) || (h[3] != '1' && h[3] != '2') || h[4] != '\n') return false;
    ascii = h[3] == '2';
    for (i = at; i < n && h[i] != '\n'; ++i) {
        if (h[i] < 0x20 && h[i] != '\t' && h[i] != '\r') return false;
    }
    if (i == n) return false;
    if (!pvf_num(h, i, &at, &channels) || !pvf_num(h, i, &at, &rate) || !pvf_num(h, i, &at, &bits)) return false;
    while (at < i && (h[at] == ' ' || h[at] == '\t' || h[at] == '\r')) ++at;
    if (at != i || !channels || channels > 32 || rate < 1000 || rate > 384000 || (bits != 8 && bits != 16 && bits != 32)) return false;
    ++i; /* sample data begins after metadata newline */
    if ((uint64_t)available <= i) return false;
    if (ascii) {
        if (!pvf_ascii(f, i, (uint64_t)available, channels, pd)) return false;
    } else {
        uint64_t frame_size = (uint64_t)channels * (bits / 8U);
        if (((uint64_t)available - i) % frame_size) return false;
    }
    if (!pm_add(f, s, "header.txt", 0, (int64_t)i) || !pm_add(f, s,
                                                              ascii        ? "samples-ascii.txt"
                                                              : bits == 8  ? "samples-s8.pcm"
                                                              : bits == 16 ? "samples-s16be.pcm"
                                                                           : "samples-s32be.pcm",
                                                              (int64_t)i, available - (int64_t)i))
        return false;
    s->size = available;
    return true;
}

void xx_audio_pvf_init(xx_audio_pvf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AUDIO_PVF, "pvf");
    }
}
xx_audio_pvf *xx_audio_pvf_create(xx_io_device *d, int64_t b)
{
    xx_audio_pvf *r = (xx_audio_pvf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_audio_pvf_init(r, d, b);
    return r;
}
void xx_audio_pvf_destroy(xx_audio_pvf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_audio_pvf_free(xx_audio_pvf *r)
{
    if (r) {
        xx_audio_pvf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_audio_pvf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_audio_pvf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
