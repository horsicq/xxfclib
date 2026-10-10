/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: http://www.libpng.org/pub/mng/spec/mng-1.0-20010209-pdg.html
 * MNG1 simple complete PNG frame sequences: CRC32-checked MHDR/IHDR/PLTE/tRNS/IDAT/IEND/MEND chunks, required dimensions, typed ancillary records and complete frame
 * ordering. Encoded IDAT is framed with an RFC1950 header but DEFLATE/Adler integrity and raster samples are not decoded. Original encoded chunks exported;
 * JNG/delta/object control/loops and pixel decoding unsupported. Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/mng_animation/xx_mng_animation.h"
#include "../common/xx_component_binary.h"

static bool model_image_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool model_image_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(model_image, 67108864, )
#include "xxfclib/data/xx_data.h"
static bool model_image_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[8];
    return n >= 70 && pm_read(f, 0, b, 8) && component_tag(b, "\x8aMNG\r\n\x1a\n", 8);
}
static bool mg_depth(uint8_t depth, uint8_t color)
{
    return color == 0   ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)
           : color == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8)
                        : (color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16);
}
static bool model_image_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t p = 8, idatbytes = 0;
    uint32_t chunks = 0, w = 0, h = 0, profile = 0, frames = 0, nominal = 0, ticks = 0, palette = 0, globalpalette = 0;
    uint8_t color = 0, zh[2] = {0};
    bool image = false, data = false, closed = false, trans = false, term = false;
    char label[64];
    if (!component_tag(b, "\x8aMNG\r\n\x1a\n", 8) || !component_emit(f, s, "signature.mng", 0, 8, n)) return false;
    while (p < n) {
        uint64_t start = p, end;
        uint32_t z, i;
        const uint8_t *tag;
        if (xx_component_parser_stopped(pd) || ++chunks > 4095 || !component_span(p, 12, n)) {
            return false;
        }
        z = xx_data_get_u32(b + p, 4, 0, true);
        tag = b + p + 4;
        if (!component_span(p, (uint64_t)z + 12, n)) return false;
        end = p + 12 + z;
        for (i = 0; i < 4; ++i)
            if (!((tag[i] >= 'A' && tag[i] <= 'Z') || (tag[i] >= 'a' && tag[i] <= 'z'))) return false;
        if ((tag[2] & 32) || component_crc32_wide_offset(tag, (uint64_t)z + 4, pd) != xx_data_get_u32(b + p + 8 + z, 4, 0, true)) return false;
        if (chunks == 1) {
            if (!component_tag(tag, "MHDR", 4) || z != 28 || (w = xx_data_get_u32(b + p + 8, 4, 0, true)) < 1 || (h = xx_data_get_u32(b + p + 12, 4, 0, true)) < 1 ||
                w > 16384 || h > 16384 || (uint64_t)w * h > 16777216)
                return false;
            ticks = xx_data_get_u32(b + p + 16, 4, 0, true);
            nominal = xx_data_get_u32(b + p + 24, 4, 0, true);
            profile = xx_data_get_u32(b + p + 32, 4, 0, true);
            if ((profile && !(profile & 1)) || (profile & 0x8000fc00U) || (!(profile & 64) && (profile & 896)) || (!(profile & 8) && (profile & 256)) ||
                nominal > 1000000)
                return false;
        } else if (component_tag(tag, "MEND", 4)) {
            if (z || image || !frames || end != n || (nominal && nominal != frames) || (frames > 1 && !ticks)) return false;
            if (!component_emit(f, s, "terminator.mng", start, end - start, n)) {
                return false;
            }
            s->size = (int64_t)n;
            return true;
        } else if (component_tag(tag, "IHDR", 4)) {
            uint32_t iw, ih;
            if (image || z != 13 || (iw = xx_data_get_u32(b + p + 8, 4, 0, true)) < 1 || (ih = xx_data_get_u32(b + p + 12, 4, 0, true)) < 1 || iw > w || ih > h ||
                !mg_depth(b[p + 16], color = b[p + 17]) || b[p + 18] || b[p + 19] || b[p + 20] > 1)
                return false;
            if (++frames > 1024) {
                return false;
            }
            image = true;
            data = closed = trans = false;
            idatbytes = 0;
            palette = globalpalette;
            zh[0] = zh[1] = 0;
        } else if (component_tag(tag, "IDAT", 4)) {
            if (!image || closed || !z) return false;
            for (i = 0; i < z && idatbytes + i < 2; ++i) zh[idatbytes + i] = b[p + 8 + i];
            idatbytes += z;
            data = true;
        } else if (component_tag(tag, "IEND", 4)) {
            if (!image || z || !data || idatbytes < 6 || (zh[0] & 15) != 8 || (zh[0] >> 4) > 7 || ((uint32_t)zh[0] * 256 + zh[1]) % 31 || (zh[1] & 32) ||
                (color == 3 && !palette))
                return false;
            image = false;
            data = closed = false;
        } else if (component_tag(tag, "PLTE", 4)) {
            if (data || !z || z % 3 || z > 768) return false;
            palette = z / 3;
            if (!image) globalpalette = palette;
        } else if (component_tag(tag, "tRNS", 4)) {
            if (!image || data || trans) return false;
            trans = true;
            if ((color == 0 && z != 2) || (color == 2 && z != 6) || (color == 3 && (!palette || !z || z > palette)) || (color != 0 && color != 2 && color != 3))
                return false;
        } else if (component_tag(tag, "gAMA", 4)) {
            if (data || z != 4 || !xx_data_get_u32(b + p + 8, 4, 0, true)) return false;
        } else if (component_tag(tag, "cHRM", 4)) {
            if (data || z != 32) return false;
        } else if (component_tag(tag, "sRGB", 4)) {
            if (data || z != 1 || b[p + 8] > 3) return false;
        } else if (component_tag(tag, "pHYs", 4)) {
            if (data || z != 9 || b[p + 16] > 1) return false;
        } else if (component_tag(tag, "bKGD", 4)) {
            if (!image || data || z != (color == 3 ? 1U : color == 0 || color == 4 ? 2U : 6U) || (color == 3 && b[p + 8] >= palette)) return false;
        } else if (component_tag(tag, "tEXt", 4)) {
            uint32_t key = 0;
            while (key < z && b[p + 8 + key]) ++key;
            if (!key || key > 79 || key == z) return false;
        } else if (component_tag(tag, "TERM", 4)) {
            if (image || term || frames || z != 1 || b[p + 8] > 2) return false;
            term = true;
        } else if (component_tag(tag, "BACK", 4)) {
            if (image || (z != 6 && z != 7) || (z == 7 && b[p + 14] > 1)) return false;
        } else if (component_tag(tag, "FRAM", 4)) {
            if (image || z != 1 || (b[p + 8] != 1 && b[p + 8] != 3) || (profile && !(profile & 2))) return false;
        } else if (component_tag(tag, "DEFI", 4)) {
            if (image || z != 2 || xx_data_get_u16(b + p + 8, 2, 0, true) || (profile && !(profile & 2))) return false;
        } else return false;
        if (image && data && !component_tag(tag, "IDAT", 4)) closed = true;
        xx_rt_snprintf(label, sizeof(label), "chunk-%u-%c%c%c%c.mng", chunks - 1, tag[0], tag[1], tag[2], tag[3]);
        if (!component_emit(f, s, label, start, end - start, n)) return false;
        p = end;
    }
    return false;
}

void xx_mng_animation_init(xx_mng_animation *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_MNG_ANIMATION, "mng");
    }
}
xx_mng_animation *xx_mng_animation_create(xx_io_device *d, int64_t at)
{
    xx_mng_animation *r = (xx_mng_animation *)xx_mem_alloc(sizeof(*r));
    if (r) xx_mng_animation_init(r, d, at);
    return r;
}
void xx_mng_animation_destroy(xx_mng_animation *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_mng_animation_free(xx_mng_animation *r)
{
    if (r) {
        xx_mng_animation_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_mng_animation_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_mng_animation_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
