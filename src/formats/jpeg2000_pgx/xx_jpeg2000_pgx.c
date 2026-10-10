/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/uclouvain/openjpeg/master/src/bin/jp2/convert.c
 * PGX gray planes: exact ML/LM signed or unsigned1..16bit descriptor, bounded dimensions and precision-consistent encoded sample range. Original encoded plane and
 * descriptor exported; no color rendering. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/jpeg2000_pgx/xx_jpeg2000_pgx.h"
#include "../common/xx_component_text.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[3];
    return n >= 14 && pm_read(f, 0, b, 3) && component_tag(b, "PG ", 3);
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    int32_t prec, w, h;
    bool be, sign;
    uint64_t i, count, z;
    unsigned bytes;
    if (!component_text_line(&q) || !component_text_word(&q, "PG")) {
        return false;
    }
    if (component_text_word(&q, "ML")) be = true;
    else if (component_text_word(&q, "LM")) be = false;
    else return false;
    component_text_space(&q);
    if (q.t == q.stop || (q.b[q.t] != '+' && q.b[q.t] != '-')) return false;
    sign = q.b[q.t++] == '-';
    if (!component_text_integer(&q, &prec) || prec < 1 || prec > 16 || !component_text_integer(&q, &w) || !component_text_integer(&q, &h) || w < 1 || h < 1 ||
        w > 65536 || h > 65536 || !component_text_done(&q))
        return false;
    count = (uint64_t)w * h;
    bytes = prec <= 8 ? 1 : 2;
    z = count * bytes;
    if (count > 16000000 || z != n - q.p) return false;
    for (i = 0; i < count; ++i) {
        uint32_t u = bytes == 1 ? b[q.p + i] : be ? xx_data_get_u16(b + q.p + i * 2, 2, 0, true) : xx_data_get_u16(b + q.p + i * 2, 2, 0, false);
        int32_t v = sign ? (bytes == 1 ? (int32_t)(int8_t)u : (int32_t)(int16_t)u) : (int32_t)u;
        if ((i & 4095) == 0 && xx_component_parser_stopped(pd)) {
            return false;
        }
        if (sign ? (v < -(1 << (prec - 1)) || v > (1 << (prec - 1)) - 1) : (u > ((1U << prec) - 1))) return false;
    }
    if (!component_emit(f, s, "descriptor.pgx", 0, q.p, n) || !component_emit(f, s, "plane.pgx", q.p, z, n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_jpeg2000_pgx_init(xx_jpeg2000_pgx *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_JPEG2000_PGX, "pgx");
    }
}
xx_jpeg2000_pgx *xx_jpeg2000_pgx_create(xx_io_device *d, int64_t at)
{
    xx_jpeg2000_pgx *r = (xx_jpeg2000_pgx *)xx_mem_alloc(sizeof(*r));
    if (r) xx_jpeg2000_pgx_init(r, d, at);
    return r;
}
void xx_jpeg2000_pgx_destroy(xx_jpeg2000_pgx *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_jpeg2000_pgx_free(xx_jpeg2000_pgx *r)
{
    if (r) {
        xx_jpeg2000_pgx_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_jpeg2000_pgx_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_jpeg2000_pgx_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
