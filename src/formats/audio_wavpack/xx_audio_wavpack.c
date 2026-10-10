/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/audio_wavpack/xx_audio_wavpack.h"
#include "../common/xx_audio_components.h"
#ifndef AUDIO_WAVPACK
#define XX_FILE_TYPE_AUDIO_WAVPACK ((xx_file_type_t)820)
#endif
static bool audio_component_parse(audio_component_blob *c)
{
    size_t p = 0;
    uint32_t total = 0, next = 0;
    unsigned blocks = 0;
    uint16_t version = 0;
    while (p < c->n) {
        uint32_t z, samples, flags, n_total, index;
        size_t q, end;
        unsigned metadata = 0, bitstream = 0;
        if (audio_component_eq(c, p, "APETAGEX", 8)) {
            size_t tq = p, footer, tend;
            uint32_t size, count, tflags;
            unsigned i;
            if (c->n - p < 32 || !audio_component_eq(c, c->n - 32, "APETAGEX", 8)) return false;
            footer = c->n - 32;
            size = xx_data_get_u32(c->b + footer + 12, 4, 0, false);
            count = xx_data_get_u32(c->b + footer + 16, 4, 0, false);
            tflags = xx_data_get_u32(c->b + footer + 20, 4, 0, false);
            if (xx_data_get_u32(c->b + footer + 8, 4, 0, false) != 2000 || size < 32 || size > 1048576 || count > 128 || (tflags != 0 && tflags != 0x80000000U) ||
                !audio_component_zero(c, footer + 24, 8))
                return false;
            if (tflags) {
                if (size + 32U != c->n - p || xx_data_get_u32(c->b + p + 8, 4, 0, false) != 2000 || xx_data_get_u32(c->b + p + 12, 4, 0, false) != size ||
                    xx_data_get_u32(c->b + p + 16, 4, 0, false) != count || xx_data_get_u32(c->b + p + 20, 4, 0, false) != 0xa0000000U ||
                    !audio_component_zero(c, p + 24, 8))
                    return false;
                tq += 32;
            } else if (size != c->n - p) return false;
            tend = footer;
            for (i = 0; i < count; ++i) {
                uint32_t n;
                unsigned key = 0;
                if (tend - tq < 9 || (n = xx_data_get_u32(c->b + tq, 4, 0, false)) > tend - tq - 8 || xx_data_get_u32(c->b + tq + 4, 4, 0, false) & ~7U) return false;
                tq += 8;
                while (tq < tend && c->b[tq]) {
                    if (c->b[tq] < 32 || c->b[tq] > 126 || ++key > 255) return false;
                    ++tq;
                }
                if (!key || tq == tend) return false;
                ++tq;
                if (n > tend - tq) return false;
                tq += n;
            }
            if (tq != tend || !audio_component_add(c, "apev2-tags.bin", p, c->n - p)) return false;
            p = c->n;
            break;
        }
        if (++blocks > 4096 || !audio_component_range(c, p, 32) || !audio_component_eq(c, p, "wvpk", 4) || (z = xx_data_get_u32(c->b + p + 4, 4, 0, false)) < 24 ||
            z > c->n - p - 8 || xx_data_get_u16(c->b + p + 8, 2, 0, false) < 0x402 || xx_data_get_u16(c->b + p + 8, 2, 0, false) > 0x410 || c->b[p + 10] || c->b[p + 11])
            return false;
        n_total = xx_data_get_u32(c->b + p + 12, 4, 0, false);
        index = xx_data_get_u32(c->b + p + 16, 4, 0, false);
        samples = xx_data_get_u32(c->b + p + 20, 4, 0, false);
        flags = xx_data_get_u32(c->b + p + 24, 4, 0, false);
        if (!samples || (flags & 0x80000188U) || (flags & 0x1800) != 0x1800 || ((flags >> 23) & 15) == 15 || !n_total || n_total > 100000000 || index != next ||
            samples > n_total - index || (blocks > 1 && (n_total != total || xx_data_get_u16(c->b + p + 8, 2, 0, false) != version)))
            return false;
        total = n_total;
        version = xx_data_get_u16(c->b + p + 8, 2, 0, false);
        next = index + samples;
        end = p + 8U + z;
        q = p + 32;
        while (q < end) {
            unsigned id;
            uint32_t words;
            size_t len, header = 2;
            if (++metadata > 4096 || end - q < 2) return false;
            id = c->b[q];
            words = c->b[q + 1];
            if (id & 128) {
                if (end - q < 4) return false;
                words |= (uint32_t)c->b[q + 2] << 8 | (uint32_t)c->b[q + 3] << 16;
                header = 4;
            }
            len = (size_t)words * 2U;
            if ((id & 64) && !len) return false;
            if (len > end - q - header) return false;
            if ((id & 63) == 10) {
                if (++bitstream > 1 || !len) return false;
            }
            q += header + len;
        }
        if (q != end || bitstream != 1 || !audio_component_add(c, "encoded-wavpack-block.bin", p, 8U + z)) {
            return false;
        }
        p = end;
    }
    return blocks && p == c->n && next == total;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return audio_component_loaded(f, s, pd, audio_component_parse);
}
void xx_audio_wavpack_init(xx_audio_wavpack *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AUDIO_WAVPACK, "bin");
    }
}
xx_audio_wavpack *xx_audio_wavpack_create(xx_io_device *d, int64_t b)
{
    xx_audio_wavpack *r = (xx_audio_wavpack *)xx_mem_alloc(sizeof(*r));
    if (r) xx_audio_wavpack_init(r, d, b);
    return r;
}
void xx_audio_wavpack_destroy(xx_audio_wavpack *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_audio_wavpack_free(xx_audio_wavpack *r)
{
    if (r) {
        xx_audio_wavpack_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_audio_wavpack_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_audio_wavpack_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
