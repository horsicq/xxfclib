/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/project-gemmi/gemmi/master/include/gemmi/dsn6.hpp
 * DSN6/BRIX density-map family: complete typed binary/ASCII descriptor, valid positive grid/cell/scaling parameters and exact8x8x8 brick extents. Binary reserved
 * descriptor words must be zero; brick padding outside declared extents is retained unchanged. Original descriptor and encoded density bricks exported; no resampling and
 * no external map dependencies. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/dsn6_density/xx_dsn6_density.h"
#include "../common/xx_component_lexer.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[38];
    return n >= 1024 && pm_read(f, 0, b, sizeof(b)) &&
           (component_tag(b, ":-)", 3) || xx_data_get_u16(b + 36, 2, 0, true) == 100 || xx_data_get_u16(b + 36, 2, 0, false) == 100);
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    int32_t ext[3], grid[3], origin, v;
    double cell[6], prod, plus, sigma;
    uint64_t bricks = 1, i;
    unsigned k;
    bool ascii = component_tag(b, ":-)", 3);
    char name[48];
    if (n < 1024) return false;
    if (ascii) {
        component_lexer q = {b, 0, 512, pd, 0, false, false, false};
        if (!component_utf8(b, 512, true, pd) || !component_lexer_char_hash_bang_cpp_comments(&q, ':') || !component_lexer_char_hash_bang_cpp_comments(&q, '-') ||
            !component_lexer_char_hash_bang_cpp_comments(&q, ')') || !component_lexer_keyword_hash_bang_cpp_comments(&q, "origin"))
            return false;
        for (k = 0; k < 3; ++k)
            if (!component_lexer_integer_hash_bang_cpp_comments_delimited(&q, &origin) || origin < -32768 || origin > 32767) return false;
        if (!component_lexer_keyword_hash_bang_cpp_comments(&q, "extent")) return false;
        for (k = 0; k < 3; ++k)
            if (!component_lexer_integer_hash_bang_cpp_comments_delimited(&q, &ext[k])) return false;
        if (!component_lexer_keyword_hash_bang_cpp_comments(&q, "grid")) return false;
        for (k = 0; k < 3; ++k)
            if (!component_lexer_integer_hash_bang_cpp_comments_delimited(&q, &grid[k])) return false;
        if (!component_lexer_keyword_hash_bang_cpp_comments(&q, "cell")) return false;
        for (k = 0; k < 6; ++k)
            if (!component_lexer_number_hash_bang_cpp_comments(&q, &cell[k])) return false;
        if (!component_lexer_keyword_hash_bang_cpp_comments(&q, "prod") || !component_lexer_number_hash_bang_cpp_comments(&q, &prod) ||
            !component_lexer_keyword_hash_bang_cpp_comments(&q, "plus") || !component_lexer_integer_hash_bang_cpp_comments_delimited(&q, &v) ||
            !component_lexer_keyword_hash_bang_cpp_comments(&q, "sigma") || !component_lexer_number_hash_bang_cpp_comments(&q, &sigma) || sigma < 0 ||
            !component_lexer_end_hash_bang_cpp_comments(&q))
            return false;
        plus = v;
    } else {
        bool le = xx_data_get_u16(b + 36, 2, 0, false) == 100;
        int32_t scale = (int16_t)(le ? xx_data_get_u16(b + 34, 2, 0, false) : xx_data_get_u16(b + 34, 2, 0, true));
        if ((!le && xx_data_get_u16(b + 36, 2, 0, true) != 100) || scale <= 0) return false;
        for (k = 0; k < 3; ++k) {
            ext[k] = (int16_t)(le ? xx_data_get_u16(b + 6 + k * 2, 2, 0, false) : xx_data_get_u16(b + 6 + k * 2, 2, 0, true));
            grid[k] = (int16_t)(le ? xx_data_get_u16(b + 12 + k * 2, 2, 0, false) : xx_data_get_u16(b + 12 + k * 2, 2, 0, true));
        }
        for (k = 0; k < 6; ++k) cell[k] = (double)(int16_t)(le ? xx_data_get_u16(b + 18 + k * 2, 2, 0, false) : xx_data_get_u16(b + 18 + k * 2, 2, 0, true)) / scale;
        prod = (double)(int16_t)(le ? xx_data_get_u16(b + 30, 2, 0, false) : xx_data_get_u16(b + 30, 2, 0, true)) / 100;
        plus = (int16_t)(le ? xx_data_get_u16(b + 32, 2, 0, false) : xx_data_get_u16(b + 32, 2, 0, true));
        if (!component_zero(b + 38, 474)) return false;
    }
    if (prod <= 0 || prod > 1e12 || plus < -32768 || plus > 32767) {
        return false;
    }
    for (k = 0; k < 3; ++k) {
        if (ext[k] < 1 || ext[k] > 4096 || grid[k] < 1 || grid[k] > 32767 || ext[k] > grid[k] || cell[k] <= 0 || cell[k + 3] <= 0 || cell[k + 3] >= 180) return false;
        bricks *= (uint64_t)(ext[k] + 7) / 8;
        if (bricks > 4095) return false;
    }
    if (n != 512 + bricks * 512 || !component_emit(f, s, "descriptor.dsn6", 0, 512, n)) return false;
    for (i = 0; i < bricks; ++i) {
        if (xx_component_parser_stopped(pd)) return false;
        xx_rt_snprintf(name, sizeof(name), "density-brick-%llu.bin", (unsigned long long)i);
        if (!component_emit(f, s, name, 512 + i * 512, 512, n)) return false;
    }
    return true;
}

void xx_dsn6_density_init(xx_dsn6_density *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_DSN6_DENSITY, "dsn6");
    }
}
xx_dsn6_density *xx_dsn6_density_create(xx_io_device *d, int64_t at)
{
    xx_dsn6_density *r = (xx_dsn6_density *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dsn6_density_init(r, d, at);
    return r;
}
void xx_dsn6_density_destroy(xx_dsn6_density *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dsn6_density_free(xx_dsn6_density *r)
{
    if (r) {
        xx_dsn6_density_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dsn6_density_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_dsn6_density_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
