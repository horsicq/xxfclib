/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.gnu.org/software/grub/manual/grub-dev/html_node/File-Structure.html
 * GNU GRUB PFF2: complete typed sections, sorted unique Unicode index with resolved nonoverlapping offsets and complete uncompressed glyph dimensions/bitmap extents. Original descriptor/index and glyph records exported; compressed glyph flags declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/grub_pff2/xx_grub_pff2.h"
#include "../common/xx_component_binary.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
static bool mesh_font_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[12];
    return n >= 32 && pm_read(f, 0, b, 12) && component_tag(b, "FILE\0\0\0\4PFF2", 12);
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    uint64_t p = 12, data = 0, index = 0, indexlen = 0;
    uint32_t seen = 0, glyphs = 0, lastcode = 0;
    uint16_t maxw = 0, maxh = 0;
    unsigned i;
    char label[48];
    if (n < 12 || !component_tag(b, "FILE\0\0\0\4PFF2", 12) || !component_emit(f, s, "file-type.pf2", 0, 12, n))
        return false;
    while (p < n) {
        uint32_t z, bit = 0;
        if (xx_component_parser_stopped(pd) || !component_span(p, 8, n))
            return false;
        z = xx_data_get_u32(b + p + 4, 4, 0, true);
        if (component_tag(b + p, "DATA", 4)) {
            if (z != 0xffffffffU || !index || !maxw || !maxh)
                return false;
            data = p + 8;
            if (!component_emit(f, s, "data-header.pf2", p, 8, n))
                return false;
            break;
        }
        if (!component_span(p + 8, z, n))
            return false;
        if (component_tag(b + p, "NAME", 4))
            bit = 1;
        else if (component_tag(b + p, "FAMI", 4))
            bit = 2;
        else if (component_tag(b + p, "WEIG", 4))
            bit = 4;
        else if (component_tag(b + p, "SLAN", 4))
            bit = 8;
        else if (component_tag(b + p, "PTSZ", 4))
            bit = 16;
        else if (component_tag(b + p, "MAXW", 4))
            bit = 32;
        else if (component_tag(b + p, "MAXH", 4))
            bit = 64;
        else if (component_tag(b + p, "ASCE", 4))
            bit = 128;
        else if (component_tag(b + p, "DESC", 4))
            bit = 256;
        else if (component_tag(b + p, "CHIX", 4))
            bit = 512;
        else
            return false;
        if (seen & bit) {
            return false;
        }
        seen |= bit;
        if (bit <= 8) {
            uint32_t k;
            if (!z || z > 1024 || b[p + 8 + z - 1])
                return false;
            for (k = 0; k + 1 < z; ++k)
                if (b[p + 8 + k] < 32 || b[p + 8 + k] > 126)
                    return false;
        } else if (bit == 512) {
            if (!z || z % 9 || z / 9 > 4000)
                return false;
            index = p + 8;
            indexlen = z;
            glyphs = z / 9;
        } else {
            uint16_t v;
            if (z != 2)
                return false;
            v = xx_data_get_u16(b + p + 8, 2, 0, true);
            if (bit == 32)
                maxw = v;
            if (bit == 64)
                maxh = v;
            if ((bit == 16 || bit == 32 || bit == 64) && (!v || v > 4096))
                return false;
        }
        xx_rt_snprintf(label, sizeof(label), "section-%u.pf2", (unsigned)s->count);
        if (!component_emit(f, s, label, p, (uint64_t)z + 8, n))
            return false;
        p += (uint64_t)z + 8;
    }
    if (!data || !(seen & 1) || !indexlen || (seen & (32 | 64 | 128 | 256)) != (32 | 64 | 128 | 256)) {
        return false;
    }
    p = data;
    for (i = 0; i < glyphs; ++i) {
        uint64_t at = index + (uint64_t)i * 9, z;
        uint32_t code = xx_data_get_u32(b + at, 4, 0, true), offset = xx_data_get_u32(b + at + 5, 4, 0, true);
        uint16_t w, h;
        if ((i && code <= lastcode) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) || b[at + 4] ||
            offset != p || !component_span(p, 10, n))
            return false;
        lastcode = code;
        w = xx_data_get_u16(b + p, 2, 0, true);
        h = xx_data_get_u16(b + p + 2, 2, 0, true);
        if (w > maxw || h > maxh)
            return false;
        z = ((uint64_t)w * h + 7) / 8;
        if (!component_span(p + 10, z, n))
            return false;
        xx_rt_snprintf(label, sizeof(label), "glyph-%08x.pf2", code);
        if (!component_emit(f, s, label, p, z + 10, n))
            return false;
        p += z + 10;
    }
    return p == n;
}

void xx_grub_pff2_init(xx_grub_pff2 *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_GRUB_PFF2, "pf2");
    }
}
xx_grub_pff2 *xx_grub_pff2_create(xx_io_device *d, int64_t at) {
    xx_grub_pff2 *r = (xx_grub_pff2 *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_grub_pff2_init(r, d, at);
    return r;
}
void xx_grub_pff2_destroy(xx_grub_pff2 *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_grub_pff2_free(xx_grub_pff2 *r) {
    if (r) {
        xx_grub_pff2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_grub_pff2_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_grub_pff2_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
