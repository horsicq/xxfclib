/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/BVH/BVHLoader.cpp
 * Biovision BVH: complete single-root hierarchy with bounded joints/end sites, unique channels, finite offsets and exact counted finite motion frames. Original hierarchy and motion table exported; multiple roots and nonstandard channels declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/bvh_motion/xx_bvh_motion.h"
#include "../common/xx_component_lexer.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
static bool mesh_font_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[9];
    return n >= 24 && pm_read(f, 0, b, 9) && component_tag(b, "HIERARCHY", 9);
}
static bool mesh_font_bvh_node(component_lexer *q, unsigned depth, unsigned *joints, unsigned *channels, bool root,
                               bool site) {
    uint64_t at, z;
    int32_t count;
    double v;
    unsigned i, mask = 0;
    bool child = false;
    if (depth > 64 || ++*joints > 4096)
        return false;
    if (site) {
        if (!component_lexer_keyword_hash_bang_cpp_comments(q, "End") ||
            !component_lexer_keyword_hash_bang_cpp_comments(q, "Site"))
            return false;
    } else if (!(root ? component_lexer_keyword_hash_bang_cpp_comments(q, "ROOT")
                      : component_lexer_keyword_hash_bang_cpp_comments(q, "JOINT")) ||
               !component_lexer_identifier_hash_bang_cpp_comments(q, &at, &z))
        return false;
    if (!component_lexer_char_hash_bang_cpp_comments(q, '{') ||
        !component_lexer_keyword_hash_bang_cpp_comments(q, "OFFSET") ||
        !component_lexer_number_hash_bang_cpp_comments(q, &v) ||
        !component_lexer_number_hash_bang_cpp_comments(q, &v) || !component_lexer_number_hash_bang_cpp_comments(q, &v))
        return false;
    if (!site) {
        if (!component_lexer_keyword_hash_bang_cpp_comments(q, "CHANNELS") ||
            !component_lexer_integer_hash_bang_cpp_comments(q, &count) || count < 1 || count > 6)
            return false;
        for (i = 0; i < (unsigned)count; ++i) {
            unsigned bit;
            if (component_lexer_keyword_hash_bang_cpp_comments(q, "Xposition"))
                bit = 1;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Yposition"))
                bit = 2;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Zposition"))
                bit = 4;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Xrotation"))
                bit = 8;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Yrotation"))
                bit = 16;
            else if (component_lexer_keyword_hash_bang_cpp_comments(q, "Zrotation"))
                bit = 32;
            else
                return false;
            if (mask & bit)
                return false;
            mask |= bit;
        }
        *channels += (unsigned)count;
        if (*channels > 24576)
            return false;
        while (component_lexer_skip_hash_bang_cpp_comments(q) && q->p < q->n && q->b[q->p] != '}') {
            component_lexer save = *q;
            bool end = component_lexer_keyword_hash_bang_cpp_comments(q, "End");
            *q = save;
            if (!mesh_font_bvh_node(q, depth + 1, joints, channels, false, end))
                return false;
            child = true;
        }
        if (!child)
            return false;
    }
    return component_lexer_char_hash_bang_cpp_comments(q, '}');
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    component_lexer q = {b, 0, n, pd, 0, false, false, false};
    unsigned joints = 0, channels = 0;
    int32_t frames;
    double dt, v;
    uint64_t p, i, total;
    if (!component_utf8(b, n, true, pd) || !component_lexer_keyword_hash_bang_cpp_comments(&q, "HIERARCHY") ||
        !mesh_font_bvh_node(&q, 0, &joints, &channels, true, false) ||
        !component_emit(f, s, "hierarchy.bvh", 0, q.p, n)) {
        return false;
    }
    p = q.p;
    if (!component_lexer_keyword_hash_bang_cpp_comments(&q, "MOTION") ||
        !component_lexer_keyword_hash_bang_cpp_comments(&q, "Frames") ||
        !component_lexer_char_hash_bang_cpp_comments(&q, ':') ||
        !component_lexer_integer_hash_bang_cpp_comments(&q, &frames) || frames < 1 || frames > 1000000 ||
        !component_lexer_keyword_hash_bang_cpp_comments(&q, "Frame") ||
        !component_lexer_keyword_hash_bang_cpp_comments(&q, "Time") ||
        !component_lexer_char_hash_bang_cpp_comments(&q, ':') ||
        !component_lexer_number_hash_bang_cpp_comments(&q, &dt) || dt <= 0 || dt > 60 ||
        !component_emit(f, s, "motion-descriptor.bvh", p, q.p - p, n)) {
        return false;
    }
    p = q.p;
    total = (uint64_t)frames * channels;
    if (total > 16000000)
        return false;
    for (i = 0; i < total; ++i)
        if (!component_lexer_number_hash_bang_cpp_comments(&q, &v))
            return false;
    return component_emit(f, s, "motion-frames.bvh", p, q.p - p, n) && component_lexer_end_hash_bang_cpp_comments(&q) &&
           component_cover(f, s, "whitespace.bvh", n);
}

void xx_bvh_motion_init(xx_bvh_motion *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_BVH_MOTION, "bvh");
    }
}
xx_bvh_motion *xx_bvh_motion_create(xx_io_device *d, int64_t at) {
    xx_bvh_motion *r = (xx_bvh_motion *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_bvh_motion_init(r, d, at);
    return r;
}
void xx_bvh_motion_destroy(xx_bvh_motion *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_bvh_motion_free(xx_bvh_motion *r) {
    if (r) {
        xx_bvh_motion_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_bvh_motion_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_bvh_motion_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
