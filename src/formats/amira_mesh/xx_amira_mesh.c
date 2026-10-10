/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.csc.kth.se/~weinkauf/notes/amiramesh.html
 * AmiraMesh2.1 BINARY-LITTLE-ENDIAN uniform float lattice: complete dimensions/bounding-box/coordinate-type/field declaration and exact finite interleaved scalar/vector
 * payload with optional single LF or CRLF terminator. Original ASCII descriptor and typed lattice planes exported; compressed/ASCII/unstructured/multiple-field
 * extensions declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/amira_mesh/xx_amira_mesh.h"
#include "../common/xx_component_lexer.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[43];
    return n >= 64 && pm_read(f, 0, b, sizeof(b)) && component_tag(b, "# AmiraMesh BINARY-LITTLE-ENDIAN 2.1", 36);
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor t = {b, 0, n, 0, 0, 0};
    component_lexer q;
    uint64_t at = 0, p, plane, bytes;
    int32_t dim[3], channels = 1;
    double box[6];
    unsigned i;
    char label[48];
    if (!component_text_line_poison_overflow(&t) || t.stop != 36 || !component_tag(b, "# AmiraMesh BINARY-LITTLE-ENDIAN 2.1", 36)) return false;
    at = t.p;
    while (t.p < n && t.p < 65536) {
        if (!component_text_line_poison_overflow(&t)) return false;
        if (false) {
            at = t.p;
            break;
        }
        if (t.stop - t.start == 2 && component_tag(b + t.start, "@1", 2)) {
            at = t.p;
            break;
        }
    }
    if (at <= 36 || at > 65536 || !component_utf8(b, at, true, pd)) return false;
    q.b = b;
    q.p = 36;
    q.n = at;
    q.pd = pd;
    q.work = 0;
    q.hash = true;
    q.commas = true;
    q.comments = false;
    if (!component_lexer_keyword_hash_bang_cpp_comments(&q, "define") || !component_lexer_keyword_hash_bang_cpp_comments(&q, "Lattice")) return false;
    for (i = 0; i < 3; ++i)
        if (!component_lexer_integer_hash_bang_cpp_comments_delimited(&q, &dim[i]) || dim[i] < 1 || dim[i] > 4096) return false;
    if (dim[2] > 4095 || !component_lexer_keyword_hash_bang_cpp_comments(&q, "Parameters") || !component_lexer_char_hash_bang_cpp_comments(&q, '{') ||
        !component_lexer_keyword_hash_bang_cpp_comments(&q, "BoundingBox"))
        return false;
    for (i = 0; i < 6; ++i)
        if (!component_lexer_number_hash_bang_cpp_comments(&q, &box[i])) return false;
    for (i = 0; i < 3; ++i)
        if (box[i * 2] >= box[i * 2 + 1]) return false;
    if (!component_lexer_keyword_hash_bang_cpp_comments(&q, "CoordType") || !component_lexer_quoted_hash_bang_cpp_comments(&q, '"', &p, &bytes) || bytes != 7 ||
        !component_tag(b + p, "uniform", 7) || !component_lexer_char_hash_bang_cpp_comments(&q, '}') || !component_lexer_keyword_hash_bang_cpp_comments(&q, "Lattice") ||
        !component_lexer_char_hash_bang_cpp_comments(&q, '{') || !component_lexer_keyword_hash_bang_cpp_comments(&q, "float"))
        return false;
    if (component_lexer_skip_hash_bang_cpp_comments(&q) && q.p < q.n && b[q.p] == '[') {
        if (!component_lexer_char_hash_bang_cpp_comments(&q, '[') || !component_lexer_integer_hash_bang_cpp_comments_delimited(&q, &channels) || channels < 1 ||
            channels > 16 || !component_lexer_char_hash_bang_cpp_comments(&q, ']'))
            return false;
    }
    {
        int32_t marker;
        if (!component_lexer_keyword_hash_bang_cpp_comments(&q, "Data") || !component_lexer_char_hash_bang_cpp_comments(&q, '}') ||
            !component_lexer_char_hash_bang_cpp_comments(&q, '@') || !component_lexer_integer_hash_bang_cpp_comments_delimited(&q, &marker) || marker != 1 ||
            !component_lexer_char_hash_bang_cpp_comments(&q, '@') || !component_lexer_integer_hash_bang_cpp_comments_delimited(&q, &marker) || marker != 1 ||
            !component_lexer_end_hash_bang_cpp_comments(&q))
            return false;
    }
    plane = (uint64_t)dim[0] * dim[1] * (unsigned)channels * 4;
    bytes = plane * (unsigned)dim[2];
    if (bytes > 33554432 || (n - at != bytes && !(n - at == bytes + 1 && b[n - 1] == 10) && !(n - at == bytes + 2 && b[n - 2] == 13 && b[n - 1] == 10)) ||
        !component_emit(f, s, "descriptor.am", 0, at, n))
        return false;
    for (p = at; p < at + bytes; p += 4)
        if (((p & 4095) == 0 && xx_component_parser_stopped(pd)) || !component_is_finite32(xx_data_get_u32(b + p, 4, 0, false))) return false;
    for (i = 0; i < (unsigned)dim[2]; ++i) {
        xx_rt_snprintf(label, sizeof(label), "lattice-plane-%u.f32le", i);
        if (!component_emit(f, s, label, at + (uint64_t)i * plane, plane, n)) return false;
    }
    return n == at + bytes || component_emit(f, s, "terminator.am", at + bytes, n - at - bytes, n);
}

void xx_amira_mesh_init(xx_amira_mesh *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_AMIRA_MESH, "am");
    }
}
xx_amira_mesh *xx_amira_mesh_create(xx_io_device *d, int64_t at)
{
    xx_amira_mesh *r = (xx_amira_mesh *)xx_mem_alloc(sizeof(*r));
    if (r) xx_amira_mesh_init(r, d, at);
    return r;
}
void xx_amira_mesh_destroy(xx_amira_mesh *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_amira_mesh_free(xx_amira_mesh *r)
{
    if (r) {
        xx_amira_mesh_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_amira_mesh_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_amira_mesh_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
