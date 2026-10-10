/* SPDX-License-Identifier: MIT
 * Primary reference: https://tug.ctan.org/info/knuth-pdf/etc/vftovp.pdf
 * TeX VF202: complete preamble/font definitions/short and long character packets/postamble, unique font and glyph IDs, finite positive design size, bounded valid DVI
 * packet commands with balanced stack and resolved font selections. Original encoded font/character programs exported; no DVI replay or font loading. Bounded32MiB
 * input,4096 components and bounded work.
 */
#include "xxfclib/formats/tex_vf/xx_tex_vf.h"
#include "../common/xx_component_binary.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[2];
    return n >= 12 && pm_read(f, 0, b, 2) && b[0] == 247 && b[1] == 202;
}
static bool vf_packet(const uint8_t *b, uint64_t n, component_id_set *fonts, uint32_t first, xx_pd_struct *pd)
{
    uint64_t p = 0;
    unsigned depth = 0;
    uint32_t selected = first;
    while (p < n) {
        uint8_t c = b[p++];
        unsigned z = 0;
        if (xx_component_parser_stopped(pd)) return false;
        if (c <= 127) {
            if (!selected) return false;
            continue;
        }
        if (c >= 128 && c <= 131) {
            if (!selected) return false;
            z = c - 127;
        } else if (c == 132 || c == 137) z = 8;
        else if (c >= 133 && c <= 136) {
            if (!selected) return false;
            z = c - 132;
        } else if (c == 138) continue;
        else if (c == 141) {
            if (++depth > 64) return false;
            continue;
        } else if (c == 142) {
            if (!depth) return false;
            --depth;
            continue;
        } else if (c >= 143 && c <= 146) z = c - 142;
        else if (c >= 147 && c <= 151) z = c - 147;
        else if (c >= 152 && c <= 156) z = c - 152;
        else if (c >= 157 && c <= 160) z = c - 156;
        else if (c >= 161 && c <= 165) z = c - 161;
        else if (c >= 166 && c <= 170) z = c - 166;
        else if (c >= 171 && c <= 234) {
            selected = (uint32_t)c - 170;
            if (!component_id(fonts, selected, false, pd)) return false;
            continue;
        } else if (c >= 235 && c <= 238) {
            uint32_t v;
            z = c - 234;
            if (!component_span(p, z, n) || (v = component_uint_be(b + p, z)) > 2147483646 || !component_id(fonts, v + 1, false, pd)) return false;
            selected = v + 1;
            p += z;
            continue;
        } else if (c >= 239 && c <= 242) {
            uint32_t size;
            z = c - 238;
            if (!component_span(p, z, n)) return false;
            size = component_uint_be(b + p, z);
            p += z;
            if (size > 1048576 || !component_span(p, size, n)) return false;
            p += size;
            continue;
        } else return false;
        if (!component_span(p, z, n)) {
            return false;
        }
        p += z;
    }
    return !depth;
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_binary_cursor q = {b, 3, n, pd};
    const uint8_t *p;
    component_id_set fonts = {0}, glyphs = {0};
    uint32_t first = 0, characters = 0;
    bool ok = false, packets = false;
    char label[48];
    if (n < 12 || b[0] != 247 || b[1] != 202 || !component_binary_take(&q, (uint64_t)b[2] + 8, &p) || xx_data_get_u32(p + b[2] + 4, 4, 0, true) < 1 ||
        xx_data_get_u32(p + b[2] + 4, 4, 0, true) > 0x7fffffff || !component_emit(f, s, "preamble.vf", 0, q.p, n) || !component_ids_init(&fonts, 4096) ||
        !component_ids_init(&glyphs, 4096))
        goto done;
    while (q.p < n) {
        uint64_t start = q.p;
        uint8_t c = b[q.p++];
        uint32_t id, z;
        if (xx_component_parser_stopped(pd)) goto done;
        if (c == 248) {
            while (q.p < n)
                if (b[q.p++] != 248) goto done;
            if (!characters || !component_emit(f, s, "postamble.vf", start, q.p - start, n)) goto done;
            s->size = (int64_t)n;
            ok = true;
            goto done;
        }
        if (c >= 243 && c <= 246) {
            if (packets || !component_binary_take(&q, c - 242, &p) || (id = component_uint_be(p, c - 242)) > 2147483646 || !component_id(&fonts, id + 1, true, pd) ||
                !component_binary_take(&q, 14, &p) || !xx_data_get_u32(p + 4, 4, 0, true) || xx_data_get_u32(p + 4, 4, 0, true) > 0x7fffffff ||
                !xx_data_get_u32(p + 8, 4, 0, true) || xx_data_get_u32(p + 8, 4, 0, true) > 0x7fffffff)
                goto done;
            z = (uint32_t)p[12] + p[13];
            if (!p[13] || !component_binary_take(&q, z, &p) || !component_utf8(p, z, false, pd)) goto done;
            if (!first) first = id + 1;
            xx_rt_snprintf(label, sizeof(label), "font-%u.vf", id);
            if (!component_emit(f, s, label, start, q.p - start, n)) goto done;
            continue;
        }
        packets = true;
        if (c <= 241) {
            z = c;
            if (!component_binary_take(&q, 4, &p)) goto done;
            id = p[0];
        } else if (c == 242) {
            if (!component_binary_take(&q, 12, &p)) goto done;
            z = xx_data_get_u32(p, 4, 0, true);
            id = xx_data_get_u32(p + 4, 4, 0, true);
        } else goto done;
        if (z > 1048576 || id > 2147483646 || ++characters > 4093 || !component_id(&glyphs, id + 1, true, pd) || !component_binary_take(&q, z, &p) ||
            !vf_packet(p, z, &fonts, first, pd))
            goto done;
        xx_rt_snprintf(label, sizeof(label), "character-%u.vf", id);
        if (!component_emit(f, s, label, start, q.p - start, n)) goto done;
    }
done:
    if (fonts.values) xx_mem_free(fonts.values);
    if (glyphs.values) xx_mem_free(glyphs.values);
    return ok;
}

void xx_tex_vf_init(xx_tex_vf *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_TEX_VF, "vf");
    }
}
xx_tex_vf *xx_tex_vf_create(xx_io_device *d, int64_t at)
{
    xx_tex_vf *r = (xx_tex_vf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tex_vf_init(r, d, at);
    return r;
}
void xx_tex_vf_destroy(xx_tex_vf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tex_vf_free(xx_tex_vf *r)
{
    if (r) {
        xx_tex_vf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tex_vf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tex_vf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
