/* SPDX-License-Identifier: MIT
 * Primary reference: https://gdal.org/en/stable/drivers/raster/dted.html
 * DTED levels0/1/2: complete UHL/DSI/ACC framing, exact declared columns/rows, consecutive per-column record identities, signed-magnitude elevation samples and verified additive record checksums. Original descriptor and encoded elevation columns exported; sparse/truncated/nonconformant variants declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/dted_elevation/xx_dted_elevation.h"
#include "../common/xx_component_binary.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
static bool mesh_font_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[4];
    return n >= 3428 && pm_read(f, 0, b, 4) && component_tag(b, "UHL1", 4);
}
static bool mesh_font_dted_digits(const uint8_t *b, unsigned z, uint32_t *v) {
    unsigned i;
    uint32_t u = 0;
    for (i = 0; i < z; ++i) {
        if (b[i] < '0' || b[i] > '9')
            return false;
        u = u * 10 + b[i] - '0';
    }
    *v = u;
    return true;
}
static bool mesh_font_dted_origin(const uint8_t *b, bool lat) {
    uint32_t degrees, minutes, seconds;
    return mesh_font_dted_digits(b, 3, &degrees) && degrees <= (lat ? 90U : 180U) &&
           mesh_font_dted_digits(b + 3, 2, &minutes) && minutes < 60 && mesh_font_dted_digits(b + 5, 2, &seconds) &&
           seconds < 60 && (degrees < (lat ? 90U : 180U) || (!minutes && !seconds)) &&
           (lat ? (b[7] == 'N' || b[7] == 'S') : (b[7] == 'E' || b[7] == 'W'));
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    uint32_t nx, ny, dx, dy, x;
    uint64_t p = 3428, z, i;
    char label[48];
    if (n < 3428 || !component_tag(b, "UHL1", 4) || !component_tag(b + 80, "DSI", 3) ||
        !component_tag(b + 728, "ACC", 3) || !mesh_font_dted_origin(b + 4, false) ||
        !mesh_font_dted_origin(b + 12, true) || !mesh_font_dted_digits(b + 20, 4, &dx) || !dx ||
        !mesh_font_dted_digits(b + 24, 4, &dy) || !dy || !mesh_font_dted_digits(b + 47, 4, &nx) || nx < 2 ||
        nx > 3601 || !mesh_font_dted_digits(b + 51, 4, &ny) || ny < 2 || ny > 3601)
        return false;
    for (i = 0; i < 3428; ++i)
        if (b[i] < 32 || b[i] > 126)
            return false;
    if ((ny != 121 && ny != 1201 && ny != 3601) || (uint64_t)(nx - 1) * dx != 36000 ||
        (uint64_t)(ny - 1) * dy != 36000) {
        return false;
    }
    z = 12 + (uint64_t)ny * 2;
    if ((uint64_t)nx * z != n - 3428 || !component_emit(f, s, "descriptor.dt0", 0, 3428, n))
        return false;
    for (x = 0; x < nx; ++x) {
        uint32_t sum = 0;
        if (xx_component_parser_stopped(pd) || b[p] != 0xaa || component_uint_be(b + p + 1, 3) != x ||
            xx_data_get_u16(b + p + 4, 2, 0, true) != x || xx_data_get_u16(b + p + 6, 2, 0, true))
            return false;
        for (i = 0; i < z - 4; ++i) {
            if ((i & 4095) == 0 && xx_component_parser_stopped(pd))
                return false;
            sum += b[p + i];
        }
        if (sum != xx_data_get_u32(b + p + z - 4, 4, 0, true))
            return false;
        xx_rt_snprintf(label, sizeof(label), "elevation-column-%u.dt0", x);
        if (!component_emit(f, s, label, p, z, n))
            return false;
        p += z;
    }
    return p == n;
}

void xx_dted_elevation_init(xx_dted_elevation *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_DTED_ELEVATION, "dt0");
    }
}
xx_dted_elevation *xx_dted_elevation_create(xx_io_device *d, int64_t at) {
    xx_dted_elevation *r = (xx_dted_elevation *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_dted_elevation_init(r, d, at);
    return r;
}
void xx_dted_elevation_destroy(xx_dted_elevation *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dted_elevation_free(xx_dted_elevation *r) {
    if (r) {
        xx_dted_elevation_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dted_elevation_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_dted_elevation_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
