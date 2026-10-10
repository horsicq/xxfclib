/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/aros-development-team/AROS/master/workbench/libs/icon/diskobjio.c
 * Classic Amiga68k DiskObject v1: complete Gadget flags, optional old/new drawer descriptor, planar Image descriptors/data, length-prefixed terminated default/tool/window strings and counted tooltypes. Original encoded icon components exported; NewIcons/ColorIcons/extra extensions and chained imagery declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/amiga_diskobject/xx_amiga_diskobject.h"
#include "../common/xx_component_binary.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[4];
    return n >= 78 && pm_read(f, 0, b, 4) && component_tag(b, "\xe3\x10\0\1", 4);
}
static bool icon_string(Abstractformat *f, pm_stream *s, component_binary_cursor *q, const char *label) {
    const uint8_t *p;
    uint64_t start = q->p;
    uint32_t z, i;
    if (!component_binary_take(q, 4, &p) || (z = xx_data_get_u32(p, 4, 0, true)) < 1 || z > 8192 ||
        !component_binary_take(q, z, &p) || p[z - 1])
        return false;
    for (i = 0; i + 1 < z; ++i)
        if (!p[i] || p[i] < 32)
            return false;
    return component_emit(f, s, label, start, q->p - start, q->n);
}
static bool icon_image(Abstractformat *f, pm_stream *s, component_binary_cursor *q, const char *label) {
    const uint8_t *p;
    uint64_t start = q->p, z;
    uint16_t w, h, d;
    unsigned i, bits = 0;
    if (!component_binary_take(q, 20, &p))
        return false;
    w = xx_data_get_u16(p + 4, 2, 0, true);
    h = xx_data_get_u16(p + 6, 2, 0, true);
    d = xx_data_get_u16(p + 8, 2, 0, true);
    if (!w || !h || w > 8192 || h > 8192 || d < 1 || d > 8 || !xx_data_get_u32(p + 10, 4, 0, true) ||
        xx_data_get_u32(p + 16, 4, 0, true))
        return false;
    for (i = 0; i < 8; ++i)
        bits += (p[14] >> i) & 1;
    if (bits > d)
        return false;
    z = ((uint64_t)w + 15) / 16 * 2 * h * d;
    return component_binary_take(q, z, NULL) && component_emit(f, s, label, start, q->p - start, q->n);
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_binary_cursor q = {b, 78, n, pd};
    const uint8_t *p;
    uint32_t types, i;
    uint16_t flags;
    bool drawer;
    if (n < 78 || !component_tag(b, "\xe3\x10\0\1", 4) || xx_data_get_u32(b + 4, 4, 0, true) ||
        xx_data_get_u16(b + 12, 2, 0, true) < 1 || xx_data_get_u16(b + 14, 2, 0, true) < 1 || b[48] < 1 || b[48] > 8 ||
        xx_data_get_u32(b + 30, 4, 0, true) || xx_data_get_u32(b + 38, 4, 0, true) ||
        !xx_data_get_u32(b + 22, 4, 0, true) || xx_data_get_u32(b + 74, 4, 0, true) > 16777216)
        return false;
    flags = xx_data_get_u16(b + 16, 2, 0, true);
    drawer = xx_data_get_u32(b + 66, 4, 0, true) != 0;
    if (!(flags & 4) || ((flags & 2) == 0 && xx_data_get_u32(b + 26, 4, 0, true)))
        return false;
    if (!component_emit(f, s, "diskobject.info", 0, 78, n))
        return false;
    if (drawer) {
        uint64_t start = q.p;
        if (!component_binary_take(&q, 56, &p) || xx_data_get_u32(p + 26, 4, 0, true) ||
            xx_data_get_u32(p + 30, 4, 0, true) || xx_data_get_u32(p + 34, 4, 0, true) ||
            !component_emit(f, s, "drawer.info", start, 56, n))
            return false;
    }
    if (!icon_image(f, s, &q, "normal-planar.info") ||
        (xx_data_get_u32(b + 26, 4, 0, true) && !icon_image(f, s, &q, "selected-planar.info")))
        return false;
    if (xx_data_get_u32(b + 50, 4, 0, true) && !icon_string(f, s, &q, "default-tool.info"))
        return false;
    if (xx_data_get_u32(b + 54, 4, 0, true)) {
        uint64_t start = q.p;
        if (!component_binary_take(&q, 4, &p) || (types = xx_data_get_u32(p, 4, 0, true)) < 4 || types > 4 * 2048 ||
            (types & 3) || !component_emit(f, s, "tooltypes-count.info", start, 4, n))
            return false;
        types = types / 4 - 1;
        for (i = 0; i < types; ++i)
            if (!icon_string(f, s, &q, "tooltype.info"))
                return false;
    }
    if (xx_data_get_u32(b + 70, 4, 0, true) && !icon_string(f, s, &q, "tool-window.info"))
        return false;
    if (drawer && (xx_data_get_u32(b + 44, 4, 0, true) & 255)) {
        uint64_t start = q.p;
        if ((xx_data_get_u32(b + 44, 4, 0, true) & 255) != 1 || !component_binary_take(&q, 6, &p) ||
            xx_data_get_u32(p, 4, 0, true) > 2 || xx_data_get_u16(p + 4, 2, 0, true) > 5 ||
            !component_emit(f, s, "new-drawer.info", start, 6, n))
            return false;
    }
    if (q.p != n) {
        return false;
    }
    s->size = (int64_t)n;
    return true;
}

void xx_amiga_diskobject_init(xx_amiga_diskobject *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_AMIGA_DISKOBJECT, "info");
    }
}
xx_amiga_diskobject *xx_amiga_diskobject_create(xx_io_device *d, int64_t at) {
    xx_amiga_diskobject *r = (xx_amiga_diskobject *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_amiga_diskobject_init(r, d, at);
    return r;
}
void xx_amiga_diskobject_destroy(xx_amiga_diskobject *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_amiga_diskobject_free(xx_amiga_diskobject *r) {
    if (r) {
        xx_amiga_diskobject_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_amiga_diskobject_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_amiga_diskobject_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
