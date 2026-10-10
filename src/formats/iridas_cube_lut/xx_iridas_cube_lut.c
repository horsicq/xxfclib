/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/AcademySoftwareFoundation/OpenColorIO/main/src/OpenColorIO/fileformats/FileFormatIridasCube.cpp
 * IRIDAS CUBE LUT: complete finite1D/3D color tables, exact declared sample count, ordered finite domains and bounded quoted title. Original header/table exported; LUT
 * application unsupported. Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/iridas_cube_lut/xx_iridas_cube_lut.h"
#include "../common/xx_component_text.h"

static bool graphics_text_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool graphics_text_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(graphics_text, 33554432, )
static bool graphics_text_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b;
    return n >= 8 && pm_read(f, 0, &b, 1);
}
static bool graphics_text_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    uint32_t seen = 0, count = 0, need = 0;
    double lo[3] = {0, 0, 0}, hi[3] = {1, 1, 1};
    uint64_t data = 0;
    bool samples = false;
    if (!component_utf8(b, n, false, pd)) return false;
    while (q.p < n) {
        double v;
        unsigned j;
        if (xx_component_parser_stopped(pd) || !component_text_line(&q)) return false;
        if (component_text_done(&q)) continue;
        if (!samples && component_text_word(&q, "TITLE")) {
            if ((seen & 1) || !component_text_string(&q) || !component_text_done(&q)) return false;
            seen |= 1;
            continue;
        }
        if (!samples && (component_text_word(&q, "DOMAIN_MIN") || component_text_word(&q, "DOMAIN_MAX"))) {
            bool min = component_tag(b + q.t - 10, "DOMAIN_MIN", 10);
            uint32_t bit = min ? 2U : 4U;
            double *a = min ? lo : hi;
            if (seen & bit) return false;
            seen |= bit;
            for (j = 0; j < 3; ++j)
                if (!component_text_number_36_digits(&q, &a[j])) return false;
            if (!component_text_done(&q)) return false;
            continue;
        }
        if (!samples && (component_text_word(&q, "LUT_1D_SIZE") || component_text_word(&q, "LUT_3D_SIZE"))) {
            int32_t size;
            bool three = component_tag(b + q.t - 11, "LUT_3D_SIZE", 11);
            if ((seen & 8) || !component_text_integer(&q, &size) || size < 2 || size > (three ? 64 : 65536) || !component_text_done(&q)) return false;
            need = three ? (uint32_t)size * size * size : (uint32_t)size;
            seen |= 8;
            continue;
        }
        if (!(seen & 8)) {
            return false;
        }
        if (!samples) {
            data = q.start;
            samples = true;
        }
        for (j = 0; j < 3; ++j) {
            if (!component_text_number_36_digits(&q, &v)) return false;
        }
        if (!component_text_done(&q) || ++count > need) return false;
    }
    if (!samples || count != need) {
        return false;
    }
    for (count = 0; count < 3; ++count)
        if (lo[count] >= hi[count]) return false;
    if (!component_emit(f, s, "descriptor.cube", 0, data, n) || !component_emit(f, s, "table.cube", data, n - data, n)) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_iridas_cube_lut_init(xx_iridas_cube_lut *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_IRIDAS_CUBE_LUT, "cube");
    }
}
xx_iridas_cube_lut *xx_iridas_cube_lut_create(xx_io_device *d, int64_t at)
{
    xx_iridas_cube_lut *r = (xx_iridas_cube_lut *)xx_mem_alloc(sizeof(*r));
    if (r) xx_iridas_cube_lut_init(r, d, at);
    return r;
}
void xx_iridas_cube_lut_destroy(xx_iridas_cube_lut *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_iridas_cube_lut_free(xx_iridas_cube_lut *r)
{
    if (r) {
        xx_iridas_cube_lut_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_iridas_cube_lut_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_iridas_cube_lut_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
