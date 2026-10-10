/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/mpeg.c
 * MPEG1/2 program packs, bounded system/stream-map/PES headers (PTS/DTS optional fields only) and complete declared packet lengths; stream-map CRC bytes remain encoded.
 * Encoded packets are exported; clean packet-boundary EOF or a terminal program-end marker is required. Zero-length PES, encryption, DVD private substream interpretation
 * and elementary codec decoding are unsupported. File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/mpeg_program_stream/xx_mpeg_program_stream.h"
#include "../common/xx_audiovisual_components.h"
static bool ps_value(audiovisual_bits *q, unsigned count, uint32_t expect)
{
    uint32_t v;
    return audiovisual_bits_get(q, count, &v) && v == expect;
}
static bool ps_timestamp(const uint8_t *b, unsigned tag)
{
    return (unsigned)(b[0] >> 4) == tag && (b[0] & 1) && (b[2] & 1) && (b[4] & 1);
}
static bool ps_pack(const uint8_t *b, uint64_t at, uint64_t n, uint64_t *end)
{
    audiovisual_bits q;
    uint32_t rate, stuff;
    unsigned i;
    if (!audiovisual_span(at, 12, n)) return false;
    q.b = b + at;
    q.bit = 32;
    q.end = (n - at) * 8;
    if ((b[at + 4] >> 4) == 2) {
        if (!ps_value(&q, 4, 2) || !audiovisual_bits_skip(&q, 3) || !ps_value(&q, 1, 1) || !audiovisual_bits_skip(&q, 15) || !ps_value(&q, 1, 1) ||
            !audiovisual_bits_skip(&q, 15) || !ps_value(&q, 1, 1) || !ps_value(&q, 1, 1) || !audiovisual_bits_get(&q, 22, &rate) || !rate || !ps_value(&q, 1, 1))
            return false;
        *end = at + 12;
        return true;
    }
    if (!audiovisual_span(at, 14, n) || !ps_value(&q, 2, 1) || !audiovisual_bits_skip(&q, 3) || !ps_value(&q, 1, 1) || !audiovisual_bits_skip(&q, 15) ||
        !ps_value(&q, 1, 1) || !audiovisual_bits_skip(&q, 15) || !ps_value(&q, 1, 1) || !audiovisual_bits_get(&q, 9, &rate) || rate > 299 || !ps_value(&q, 1, 1) ||
        !audiovisual_bits_get(&q, 22, &rate) || !rate || !ps_value(&q, 1, 1) || !ps_value(&q, 1, 1) || !ps_value(&q, 5, 31) || !audiovisual_bits_get(&q, 3, &stuff) ||
        !audiovisual_span(at + 14, stuff, n)) {
        return false;
    }
    for (i = 0; i < stuff; ++i)
        if (b[at + 14 + i] != 255) return false;
    *end = at + 14 + stuff;
    return true;
}
static bool ps_pes(const uint8_t *b, uint64_t p, uint64_t end)
{
    unsigned stuff = 0;
    if (p >= end) return false;
    if ((b[p] & 0xc0) == 0x80) {
        unsigned flags, need, len;
        if (!audiovisual_span(p, 3, end) || b[p] & 0x30 || (b[p + 1] & 63)) return false;
        flags = b[p + 1] >> 6;
        len = b[p + 2];
        need = flags == 2 ? 5 : flags == 3 ? 10 : 0;
        if (flags == 1 || len < need || !audiovisual_span(p + 3, len, end)) return false;
        p += 3;
        if (need && (!ps_timestamp(b + p, flags == 2 ? 2 : 3) || (need == 10 && !ps_timestamp(b + p + 5, 1)))) return false;
        p += need;
        for (; need < len; ++need)
            if (b[p++] != 255) return false;
        return p < end;
    }
    while (p < end && b[p] == 255) {
        if (++stuff > 16) return false;
        ++p;
    }
    if (p >= end) return false;
    if ((b[p] & 0xc0) == 0x40) {
        if (!audiovisual_span(p, 2, end)) return false;
        p += 2;
    }
    if (p >= end) return false;
    if (b[p] == 15) {
        return p + 1 < end;
    }
    if ((b[p] >> 4) == 2) return audiovisual_span(p, 5, end) && ps_timestamp(b + p, 2) && p + 5 < end;
    if ((b[p] >> 4) == 3) return audiovisual_span(p, 10, end) && ps_timestamp(b + p, 3) && ps_timestamp(b + p + 5, 1) && p + 10 < end;
    return false;
}
static bool audiovisual_quick(Abstractformat *f, uint64_t n)
{
    uint8_t h[5];
    return audiovisual_probe(f, n, h, 5) && !xx_rt_memcmp(h, "\0\0\1\xba", 4) && ((h[4] >> 4) == 2 || (h[4] >> 6) == 1);
}
static bool audiovisual_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t at = 0;
    unsigned packs = 0, pes = 0, records = 0;
    char label[40];
    while (at < n) {
        uint8_t type;
        uint64_t end;
        uint32_t len;
        if (audiovisual_stop(pd) || !audiovisual_span(at, 4, n) || b[at] || b[at + 1] || b[at + 2] != 1) return false;
        type = b[at + 3];
        if (type == 0xb9) {
            if (!packs || !pes || at + 4 != n || !audiovisual_emit(f, s, "program-end.bin", at, 4, n)) return false;
            at += 4;
            break;
        }
        if (type == 0xba) {
            if (!ps_pack(b, at, n, &end)) return false;
            ++packs;
        } else {
            if (!packs || !audiovisual_span(at, 6, n) || !(len = xx_data_get_u16(b + at + 4, 2, 0, true)) || !audiovisual_span(at + 6, len, n)) return false;
            end = at + 6 + len;
            if (type == 0xbb) {
                audiovisual_bits q;
                uint32_t v;
                uint64_t p;
                if (len < 6 || (len - 6) % 3) return false;
                q.b = b + at + 6;
                q.bit = 0;
                q.end = 48;
                if (!ps_value(&q, 1, 1) || !audiovisual_bits_get(&q, 22, &v) || !v || !ps_value(&q, 1, 1) || !audiovisual_bits_skip(&q, 10) || !ps_value(&q, 1, 1) ||
                    !audiovisual_bits_skip(&q, 6) || !ps_value(&q, 7, 127))
                    return false;
                for (p = at + 12; p < end; p += 3)
                    if ((b[p] != 0xb8 && b[p] != 0xb9 && b[p] != 0xbd && (b[p] < 0xc0 || b[p] > 0xef)) || (b[p + 1] & 0xc0) != 0xc0) return false;
            } else if (type == 0xbc) {
                uint64_t p = at + 6, finish;
                uint32_t bytes;
                if (len < 10 || (b[p] & 0x60) != 0x60 || b[p + 1] != 255) return false;
                p += 2;
                bytes = xx_data_get_u16(b + p, 2, 0, true);
                p += 2;
                if (!audiovisual_span(p, bytes, end - 4)) return false;
                p += bytes;
                if (!audiovisual_span(p, 2, end - 4)) return false;
                bytes = xx_data_get_u16(b + p, 2, 0, true);
                p += 2;
                finish = p + bytes;
                if (finish != end - 4) return false;
                while (p < finish) {
                    if (!audiovisual_span(p, 4, finish) || !b[p] || (b[p + 1] != 0xbd && (b[p + 1] < 0xc0 || b[p + 1] > 0xef))) return false;
                    bytes = xx_data_get_u16(b + p + 2, 2, 0, true);
                    p += 4;
                    if (!audiovisual_span(p, bytes, finish)) return false;
                    p += bytes;
                }
            } else if (type == 0xbe) {
                uint64_t p = at + 6;
                if (b[p] == 15) ++p;
                for (; p < end; ++p)
                    if (b[p] != 255) return false;
            } else if (type == 0xbf) {
                if (len < 1) return false;
            } else if (type == 0xbd || (type >= 0xc0 && type <= 0xef)) {
                if (!ps_pes(b, at + 6, end)) return false;
                ++pes;
            } else return false;
        }
        xx_rt_snprintf(label, sizeof(label), "packet-%u-%02x.mpg", records++, type);
        if (!audiovisual_emit(f, s, label, at, end - at, n)) return false;
        at = end;
    }
    s->size = (int64_t)at;
    return packs > 0 && pes > 0;
}

void xx_mpeg_program_stream_init(xx_mpeg_program_stream *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_MPEG_PROGRAM_STREAM, "mpg");
    }
}
xx_mpeg_program_stream *xx_mpeg_program_stream_create(xx_io_device *d, int64_t at)
{
    xx_mpeg_program_stream *r = (xx_mpeg_program_stream *)xx_mem_alloc(sizeof(*r));
    if (r) xx_mpeg_program_stream_init(r, d, at);
    return r;
}
void xx_mpeg_program_stream_destroy(xx_mpeg_program_stream *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_mpeg_program_stream_free(xx_mpeg_program_stream *r)
{
    if (r) {
        xx_mpeg_program_stream_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_mpeg_program_stream_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_mpeg_program_stream_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
