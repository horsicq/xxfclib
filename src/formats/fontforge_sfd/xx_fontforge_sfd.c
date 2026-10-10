/* SPDX-License-Identifier: MIT
 * Primary reference: https://fontforge.org/docs/techref/sfdformat.html
 * FontForge SFD1.0 outline subset: typed font metrics and complete counted glyphs with checked encodings, finite move/line/cubic contours and balanced
 * spline/character/font terminators. Fore opens each legacy outline; one immediate optional SplineSet token is accepted. Original descriptor and glyph programs exported;
 * bitmap/layer/reference/kerning/lookup extensions declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/fontforge_sfd/xx_fontforge_sfd.h"
#include "../common/xx_component_text.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
static bool mesh_font_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[12];
    return n >= 24 && pm_read(f, 0, b, 12) && component_tag(b, "SplineFontDB:", 12);
}
static bool mesh_font_sfd_field(component_text_cursor *q, const char *key)
{
    size_t z = xx_rt_strlen(key);
    component_text_space(q);
    if (!component_span(q->t, z + 1, q->stop) || !component_tag(q->b + q->t, key, z) || q->b[q->t + z] != ':') return false;
    q->t += z + 1;
    component_text_space(q);
    return true;
}
static bool mesh_font_sfd_string(component_text_cursor *q)
{
    uint64_t z = q->stop - q->t;
    return z > 0 && z <= 1024;
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    component_id_set ids = {0};
    int32_t slots = 0, count = 0, i, value;
    uint32_t required = 0;
    bool ok = false, any = false;
    double v;
    uint64_t header = 0;
    if (!component_utf8(b, n, true, pd) || !component_text_next_poison_overflow(&q) || !mesh_font_sfd_field(&q, "SplineFontDB") ||
        !component_text_number_36_digits(&q, &v) || v != 1 || !component_text_done(&q))
        return false;
    while (component_text_next_poison_overflow(&q)) {
        if (xx_component_parser_stopped(pd)) return false;
        if (mesh_font_sfd_field(&q, "BeginChars")) {
            if (!component_text_integer(&q, &slots) || slots < 1 || slots > 1114112 || !component_text_integer(&q, &count) || count < 1 || count > 4000 ||
                count > slots || !component_text_done(&q))
                return false;
            header = q.p;
            break;
        }
        if (mesh_font_sfd_field(&q, "FontName")) {
            if (required & 1 || !mesh_font_sfd_string(&q)) return false;
            required |= 1;
        } else if (mesh_font_sfd_field(&q, "Ascent")) {
            if (required & 2 || !component_text_integer(&q, &value) || value < 1 || value > 1000000 || !component_text_done(&q)) return false;
            required |= 2;
        } else if (mesh_font_sfd_field(&q, "Descent")) {
            if (required & 4 || !component_text_integer(&q, &value) || value < 0 || value > 1000000 || !component_text_done(&q)) return false;
            required |= 4;
        } else if (mesh_font_sfd_field(&q, "FullName") || mesh_font_sfd_field(&q, "FamilyName") || mesh_font_sfd_field(&q, "Weight") ||
                   mesh_font_sfd_field(&q, "Copyright") || mesh_font_sfd_field(&q, "Comments") || mesh_font_sfd_field(&q, "Version") ||
                   mesh_font_sfd_field(&q, "Encoding")) {
            if (!mesh_font_sfd_string(&q)) return false;
        } else if (mesh_font_sfd_field(&q, "ItalicAngle") || mesh_font_sfd_field(&q, "UnderlinePosition") || mesh_font_sfd_field(&q, "UnderlineWidth")) {
            if (!component_text_number_36_digits(&q, &v) || !component_text_done(&q)) return false;
        } else if (mesh_font_sfd_field(&q, "NeedsXUIDChange") || mesh_font_sfd_field(&q, "AntiAlias")) {
            if (!component_text_integer(&q, &value) || value < 0 || value > 1 || !component_text_done(&q)) return false;
        } else if (mesh_font_sfd_field(&q, "DisplaySize")) {
            if (!component_text_integer(&q, &value) || value < -4096 || value > 4096 || !component_text_done(&q)) return false;
        } else return false;
    }
    if (!header || required != 7 || !component_emit(f, s, "font-descriptor.sfd", 0, header, n) || !component_ids_init(&ids, (uint32_t)count)) return false;
    for (i = 0; i < count; ++i) {
        uint64_t start;
        bool encoding = false, width = false, program = false, move = false, explicit_open = false;
        char label[48];
        if (!component_text_next_poison_overflow(&q) || !mesh_font_sfd_field(&q, "StartChar") || !mesh_font_sfd_string(&q)) {
            goto done;
        }
        start = q.start;
        while (component_text_next_poison_overflow(&q)) {
            if (xx_component_parser_stopped(pd)) goto done;
            if (component_text_word(&q, "EndChar")) {
                if (!component_text_done(&q) || !encoding || !width || program) goto done;
                break;
            }
            if (mesh_font_sfd_field(&q, "Encoding")) {
                int32_t unicode;
                if (encoding || !component_text_integer(&q, &value) || value < 0 || value >= slots || !component_id(&ids, (uint32_t)value + 1, true, pd) ||
                    !component_text_integer(&q, &unicode) || unicode < -1 || unicode > 1114111 || !component_text_done(&q))
                    goto done;
                encoding = true;
            } else if (mesh_font_sfd_field(&q, "Width") || mesh_font_sfd_field(&q, "VWidth")) {
                bool horizontal = component_tag(b + q.start, "Width:", 6);
                if (!component_text_integer(&q, &value) || value < 0 || value > 1000000 || !component_text_done(&q) || (horizontal && width)) goto done;
                if (horizontal) width = true;
            } else if (mesh_font_sfd_field(&q, "Flags")) {
                uint64_t j;
                if (!mesh_font_sfd_string(&q)) goto done;
                for (j = q.t; j < q.stop; ++j)
                    if (b[j] < 'A' || b[j] > 'Z') goto done;
            } else if (component_text_word(&q, "Fore")) {
                if (!component_text_done(&q) || program) goto done;
                program = true;
                move = false;
                explicit_open = false;
            } else if (component_text_word(&q, "SplineSet")) {
                if (!program || move || explicit_open || !component_text_done(&q)) goto done;
                explicit_open = true;
            } else if (component_text_word(&q, "EndSplineSet")) {
                if (!program || !move || !component_text_done(&q)) goto done;
                program = false;
                any = true;
            } else {
                double xy[6];
                unsigned k = 0;
                uint8_t command;
                if (!program) goto done;
                while (k < 6 && component_text_number_36_digits(&q, &xy[k])) ++k;
                component_text_space(&q);
                if (q.t == q.stop) goto done;
                command = b[q.t++];
                if ((command == 'm' || command == 'l') && k != 2) goto done;
                if (command == 'c' && k != 6) goto done;
                if (command != 'm' && command != 'l' && command != 'c') goto done;
                if (command != 'm' && !move) goto done;
                if (command == 'm') move = true;
                if (!component_text_integer(&q, &value) || value < 0 || value > 65535 || !component_text_done(&q)) goto done;
            }
        }
        if (program) {
            goto done;
        }
        xx_rt_snprintf(label, sizeof(label), "glyph-%u.sfd", (unsigned)i);
        if (!component_emit(f, s, label, start, q.p - start, n)) goto done;
    }
    if (!any || !component_text_next_poison_overflow(&q) || !component_text_word(&q, "EndChars") || !component_text_done(&q) ||
        !component_text_next_poison_overflow(&q) || !component_text_word(&q, "EndSplineFont") || !component_text_done(&q) ||
        !component_emit(f, s, "terminator.sfd", q.start, q.p - q.start, n) || component_text_next_poison_overflow(&q) || q.p != q.end ||
        !component_cover(f, s, "whitespace.sfd", n)) {
        goto done;
    }
    ok = true;
done:
    xx_mem_free(ids.values);
    return ok;
}

void xx_fontforge_sfd_init(xx_fontforge_sfd *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_FONTFORGE_SFD, "sfd");
    }
}
xx_fontforge_sfd *xx_fontforge_sfd_create(xx_io_device *d, int64_t at)
{
    xx_fontforge_sfd *r = (xx_fontforge_sfd *)xx_mem_alloc(sizeof(*r));
    if (r) xx_fontforge_sfd_init(r, d, at);
    return r;
}
void xx_fontforge_sfd_destroy(xx_fontforge_sfd *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_fontforge_sfd_free(xx_fontforge_sfd *r)
{
    if (r) {
        xx_fontforge_sfd_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_fontforge_sfd_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_fontforge_sfd_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
