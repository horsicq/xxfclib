/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.x.org/releases/X11R7.7/doc/libX11/libX11/libX11.html
 * X11 XBM: strict bounded ASCII width/height and optional paired hotspots, one unsigned char/char bitmap initializer with exact hex-byte count. Decoded LSB-first row
 * bytes plus original descriptor exported; no C execution. X10 short arrays and additional C declarations declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/x11_xbm/xx_x11_xbm.h"
#include "../common/xx_component_lexer.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b;
    return n >= 12 && pm_read(f, 0, &b, 1) && (b == '#' || b == '/' || b == 32 || b == 10 || b == 13);
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_lexer q = {b, 0, n, pd, 0, false, false, true};
    char base[128];
    uint64_t at, z, basez = 0, data;
    int32_t w = 0, h = 0, hx = -1, hy = -1;
    unsigned mask = 0, i;
    uint8_t *pixels = NULL;
    uint64_t count;
    if (!component_utf8(b, n, true, pd)) return false;
    while (component_lexer_char_hash_block_comments(&q, '#')) {
        int32_t value;
        unsigned bit;
        uint64_t suffix = 0;
        if (!component_lexer_keyword_hash_block_comments(&q, "define") || !component_lexer_identifier_hash_block_comments(&q, &at, &z) ||
            !component_lexer_integer_hash_block_comments(&q, &value))
            return false;
        if (z > 6 && component_tag(b + at + z - 6, "_width", 6)) {
            suffix = 6;
            bit = 1;
            w = value;
        } else if (z > 7 && component_tag(b + at + z - 7, "_height", 7)) {
            suffix = 7;
            bit = 2;
            h = value;
        } else if (z > 6 && component_tag(b + at + z - 6, "_x_hot", 6)) {
            suffix = 6;
            bit = 4;
            hx = value;
        } else if (z > 6 && component_tag(b + at + z - 6, "_y_hot", 6)) {
            suffix = 6;
            bit = 8;
            hy = value;
        } else return false;
        if (mask & bit || z - suffix >= sizeof(base)) {
            return false;
        }
        if (!basez) {
            basez = z - suffix;
            xx_rt_memcpy(base, b + at, (size_t)basez);
        } else if (basez != z - suffix || !component_tag(b + at, base, (size_t)basez)) return false;
        mask |= bit;
    }
    if ((mask & 3) != 3 || ((mask & 12) != 0 && (mask & 12) != 12) || w < 1 || h < 1 || w > 65536 || h > 65536 ||
        ((mask & 12) && (hx < 0 || hy < 0 || hx >= w || hy >= h)))
        return false;
    if (!component_lexer_keyword_hash_block_comments(&q, "static")) {
        return false;
    }
    (void)component_lexer_keyword_hash_block_comments(&q, "const");
    (void)component_lexer_keyword_hash_block_comments(&q, "unsigned");
    if (!component_lexer_keyword_hash_block_comments(&q, "char") || !component_lexer_identifier_hash_block_comments(&q, &at, &z) || z != basez + 5 ||
        !component_tag(b + at, base, (size_t)basez) || !component_tag(b + at + basez, "_bits", 5) || !component_lexer_char_hash_block_comments(&q, '['))
        return false;
    count = ((uint64_t)w + 7) / 8 * h;
    if (!count || count > 16000000) return false;
    if (!component_lexer_char_hash_block_comments(&q, ']')) {
        int32_t declared;
        if (!component_lexer_integer_hash_block_comments(&q, &declared) || declared != (int32_t)count || !component_lexer_char_hash_block_comments(&q, ']')) return false;
    }
    if (!component_lexer_char_hash_block_comments(&q, '=') || !component_lexer_char_hash_block_comments(&q, '{')) {
        return false;
    }
    data = q.p;
    pixels = (uint8_t *)xx_mem_alloc((size_t)count);
    if (!pixels) return false;
    for (i = 0; i < count; ++i) {
        uint32_t value = 0;
        unsigned digits = 0;
        if (xx_component_parser_stopped(pd) || !component_lexer_skip_hash_block_comments(&q) || !component_span(q.p, 2, n) || b[q.p] != '0' ||
            (b[q.p + 1] != 'x' && b[q.p + 1] != 'X'))
            goto fail;
        q.p += 2;
        while (q.p < n) {
            uint8_t c = b[q.p];
            unsigned v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
            if (v == 16) break;
            if (++digits > 2) goto fail;
            value = value * 16 + v;
            ++q.p;
        }
        if (!digits) {
            goto fail;
        }
        pixels[i] = (uint8_t)value;
        if (i + 1 < count && !component_lexer_char_hash_block_comments(&q, ',')) goto fail;
    }
    (void)component_lexer_char_hash_block_comments(&q, ',');
    if (!component_lexer_char_hash_block_comments(&q, '}') || !component_lexer_char_hash_block_comments(&q, ';') || !component_lexer_end_hash_block_comments(&q) ||
        !component_emit(f, s, "descriptor.xbm", 0, data, n))
        goto fail;
    if (!component_publish_memory_free_on_failure(f, s, "bitmap-lsb.bin", pixels, count)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
fail:
    xx_mem_free(pixels);
    return false;
}

void xx_x11_xbm_init(xx_x11_xbm *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_X11_XBM, "xbm");
    }
}
xx_x11_xbm *xx_x11_xbm_create(xx_io_device *d, int64_t at)
{
    xx_x11_xbm *r = (xx_x11_xbm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_x11_xbm_init(r, d, at);
    return r;
}
void xx_x11_xbm_destroy(xx_x11_xbm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_x11_xbm_free(xx_x11_xbm *r)
{
    if (r) {
        xx_x11_xbm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_x11_xbm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_x11_xbm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
