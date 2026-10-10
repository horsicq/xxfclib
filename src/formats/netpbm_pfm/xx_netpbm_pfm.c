/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/opencv/opencv/4.x/modules/imgcodecs/src/grfmt_pfm.cpp
 * PF/Pf floating-point images with bounded complete dimension/scale grammar, exact finite float raster in declared endian order and no trailing bytes. Original encoded
 * float raster exported; display conversion and nonfinite values are unsupported. Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/netpbm_pfm/xx_netpbm_pfm.h"
#include "../common/xx_texture_font_components.h"
static bool texture_font_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[3];
    return texture_font_probe(f, n, b, 3) && b[0] == 'P' && (b[1] == 'F' || b[1] == 'f') && b[2] == 10;
}
static bool texture_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    texture_font_text text = {b, 3, n, 0, 0};
    int32_t dim[2];
    uint64_t p, end, values, i;
    bool negative = false, nonzero = false, dot = false, exp = false;
    unsigned digits = 0;
    int32_t exponent = 0;
    if (!texture_font_line(&text) || !texture_font_ints(b, text.start, text.start + text.len, dim, 2) || dim[0] < 1 || dim[0] > 16384 || dim[1] < 1 || dim[1] > 16384 ||
        !texture_font_line(&text))
        return false;
    p = text.start;
    end = p + text.len;
    if (p < end && (b[p] == '-' || b[p] == '+')) {
        negative = b[p] == '-';
        ++p;
    }
    while (p < end) {
        uint8_t c = b[p++];
        if (c >= '0' && c <= '9') {
            nonzero |= c != '0';
            ++digits;
            if (digits > 32) return false;
        } else if (c == '.' && !dot && !exp) {
            dot = true;
        } else if ((c == 'e' || c == 'E') && !exp && digits) {
            int32_t e;
            bool sign = false;
            exp = true;
            if (p < end && (b[p] == '+' || b[p] == '-')) {
                sign = b[p] == '-';
                ++p;
            }
            if (!texture_font_ints(b, p, end, &e, 1) || e < 0 || e > 38) return false;
            exponent = sign ? -e : e;
            p = end;
        } else return false;
    }
    if (!digits || !nonzero || exponent > 38 || exponent < -38) return false;
    values = (uint64_t)(uint32_t)dim[0] * (uint32_t)dim[1] * (b[1] == 'F' ? 3U : 1U);
    p = text.p;
    if (values > 16777216 || !texture_font_span(p, values * 4, n) || p + values * 4 != n) return false;
    for (i = 0; i < values; ++i) {
        uint32_t v;
        if ((i & 1023) == 0 && texture_font_stop(pd)) return false;
        v = negative ? xx_data_get_u32(b + p + i * 4, 4, 0, false) : xx_data_get_u32(b + p + i * 4, 4, 0, true);
        if ((v & 0x7f800000U) == 0x7f800000U) return false;
    }
    if (!texture_font_emit(f, s, "pfm-header.bin", 0, p, n) || !texture_font_emit(f, s, "float-raster.bin", p, n - p, n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_netpbm_pfm_init(xx_netpbm_pfm *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_NETPBM_PFM, "pfm");
    }
}
xx_netpbm_pfm *xx_netpbm_pfm_create(xx_io_device *d, int64_t at)
{
    xx_netpbm_pfm *r = (xx_netpbm_pfm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_netpbm_pfm_init(r, d, at);
    return r;
}
void xx_netpbm_pfm_destroy(xx_netpbm_pfm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_netpbm_pfm_free(xx_netpbm_pfm *r)
{
    if (r) {
        xx_netpbm_pfm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_netpbm_pfm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_netpbm_pfm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
