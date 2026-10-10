/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/ape.c
 * Monkey Audio3980/3990 modern descriptor and header, complete seek table, stored WAVE header/tail and encoded frame extents, plus typed optional APEv2 item tables.
 * Integer8/16/24-bit mono/stereo and classic compression levels1000-5000 only. Encoded audio and its decoded CRC/descriptor MD5 remain uninterpreted; older layouts,
 * unknown header extensions and decoding are unsupported. File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/audio_monkeys_ape/xx_audio_monkeys_ape.h"
#include "../common/xx_audiovisual_components.h"
static bool ape_tag(const uint8_t *b, uint64_t at, uint64_t n)
{
    uint64_t footer, p, end;
    uint32_t count, size, flags, i;
    if (!audiovisual_span(at, 32, n)) return false;
    footer = n - 32;
    if (xx_rt_memcmp(b + footer, "APETAGEX", 8) || xx_data_get_u32(b + footer + 8, 4, 0, false) != 2000 || (size = xx_data_get_u32(b + footer + 12, 4, 0, false)) < 32 ||
        (count = xx_data_get_u32(b + footer + 16, 4, 0, false)) > 1024 || !audiovisual_zero(b + footer + 24, 8))
        return false;
    flags = xx_data_get_u32(b + footer + 20, 4, 0, false);
    if (flags & 0x3fffffffU) return false;
    if (size > n - at) return false;
    p = n - size;
    end = footer;
    if (flags & 0x80000000U) {
        if (p < 32 || p - 32 != at || xx_rt_memcmp(b + at, b + footer, 20) || xx_data_get_u32(b + at + 20, 4, 0, false) != (flags | 0x20000000U) ||
            !audiovisual_zero(b + at + 24, 8))
            return false;
    } else if (p != at) return false;
    for (i = 0; i < count; ++i) {
        uint32_t bytes;
        uint64_t start;
        if (!audiovisual_span(p, 8, end) || (bytes = xx_data_get_u32(b + p, 4, 0, false)) > 4194304 || xx_data_get_u32(b + p + 4, 4, 0, false) & ~7U) return false;
        p += 8;
        start = p;
        while (p < end && b[p]) {
            if (p - start >= 255 || b[p] < 32 || b[p] > 126 || b[p] == '=') return false;
            ++p;
        }
        if (p == end || p == start || !audiovisual_span(++p, bytes, end)) return false;
        p += bytes;
    }
    return p == end;
}
static bool audiovisual_quick(Abstractformat *f, uint64_t n)
{
    uint8_t h[52];
    return audiovisual_probe(f, n, h, 52) && !xx_rt_memcmp(h, "MAC ", 4) &&
           (xx_data_get_u16(h + 4, 2, 0, false) == 3980 || xx_data_get_u16(h + 4, 2, 0, false) == 3990) && xx_data_get_u32(h + 8, 4, 0, false) == 52 &&
           xx_data_get_u32(h + 12, 4, 0, false) == 24;
}
static bool audiovisual_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint32_t frames, seek, wav, tail, audio, block, final, bps, ch, compression, rate, i;
    uint64_t first, end, last;
    char label[40];
    if (n < 80 || xx_rt_memcmp(b, "MAC ", 4) || (xx_data_get_u16(b + 4, 2, 0, false) != 3980 && xx_data_get_u16(b + 4, 2, 0, false) != 3990) ||
        xx_data_get_u16(b + 6, 2, 0, false) || xx_data_get_u32(b + 8, 4, 0, false) != 52 || xx_data_get_u32(b + 12, 4, 0, false) != 24 ||
        xx_data_get_u32(b + 28, 4, 0, false) || (frames = xx_data_get_u32(b + 64, 4, 0, false)) < 1 || frames > 4092 ||
        (seek = xx_data_get_u32(b + 16, 4, 0, false)) != frames * 4 || (wav = xx_data_get_u32(b + 20, 4, 0, false)) > 65536 ||
        (audio = xx_data_get_u32(b + 24, 4, 0, false)) < 4 || (tail = xx_data_get_u32(b + 32, 4, 0, false)) > 65536)
        return false;
    compression = xx_data_get_u16(b + 52, 2, 0, false);
    block = xx_data_get_u32(b + 56, 4, 0, false);
    final = xx_data_get_u32(b + 60, 4, 0, false);
    bps = xx_data_get_u16(b + 68, 2, 0, false);
    ch = xx_data_get_u16(b + 70, 2, 0, false);
    rate = xx_data_get_u32(b + 72, 4, 0, false);
    if (compression < 1000 || compression > 5000 || compression % 1000 || xx_data_get_u16(b + 54, 2, 0, false) || (block != 73728 && block != 294912) || !final ||
        final > block || (bps != 8 && bps != 16 && bps != 24) || ch < 1 || ch > 2 || rate < 8000 || rate > 192000)
        return false;
    first = 76 + (uint64_t)seek + wav;
    end = first + audio;
    if (!audiovisual_span(76, seek, n) || !audiovisual_span(first, audio, n) || !audiovisual_span(end, tail, n) || xx_data_get_u32(b + 76, 4, 0, false) != first)
        return false;
    if (wav) {
        if (wav < 12 || !audiovisual_span(76 + (uint64_t)seek, wav, n) || xx_rt_memcmp(b + 76 + seek, "RIFF", 4) || xx_rt_memcmp(b + 76 + seek + 8, "WAVE", 4))
            return false;
    }
    if (!audiovisual_emit(f, s, "descriptor-header-seek.bin", 0, 76 + (uint64_t)seek, n)) return false;
    if (wav && !audiovisual_emit(f, s, "wave-header.bin", 76 + (uint64_t)seek, wav, n)) return false;
    last = first;
    for (i = 0; i < frames; ++i) {
        uint64_t next = i + 1 < frames ? xx_data_get_u32(b + 76 + (uint64_t)(i + 1) * 4, 4, 0, false) : end;
        if (audiovisual_stop(pd) || next < last + 4 || next > end) return false;
        xx_rt_snprintf(label, sizeof(label), "frame-%u.ape", i);
        if (!audiovisual_emit(f, s, label, last, next - last, n)) return false;
        last = next;
    }
    if (tail && !audiovisual_emit(f, s, "wave-tail.bin", end, tail, n)) {
        return false;
    }
    end += tail;
    if (end < n) {
        if (!ape_tag(b, end, n) || !audiovisual_emit(f, s, "apev2-tag.bin", end, n - end, n)) return false;
        end = n;
    }
    s->size = (int64_t)end;
    return true;
}

void xx_audio_monkeys_ape_init(xx_audio_monkeys_ape *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_AUDIO_MONKEYS_APE, "ape");
    }
}
xx_audio_monkeys_ape *xx_audio_monkeys_ape_create(xx_io_device *d, int64_t at)
{
    xx_audio_monkeys_ape *r = (xx_audio_monkeys_ape *)xx_mem_alloc(sizeof(*r));
    if (r) xx_audio_monkeys_ape_init(r, d, at);
    return r;
}
void xx_audio_monkeys_ape_destroy(xx_audio_monkeys_ape *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_audio_monkeys_ape_free(xx_audio_monkeys_ape *r)
{
    if (r) {
        xx_audio_monkeys_ape_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_audio_monkeys_ape_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_audio_monkeys_ape_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
