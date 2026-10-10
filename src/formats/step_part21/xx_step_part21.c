/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/stepcode/stepcode/develop/src/clstepcore/read_func.cc
 * STEP Part21 cleartext edition1/2 framing: mandatory typed HEADER records, complete DATA entities/recursive parameters, unique positive IDs and resolved local references. Original encoded descriptor/entity records exported. Schema-specific CAD evaluation, SCOPE, ANCHOR/REFERENCE/signature sections and encoded-string directives declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/step_part21/xx_step_part21.h"
#include "../common/xx_component_lexer.h"

static bool palette_cad_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool palette_cad_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(palette_cad, 33554432, )
static bool palette_cad_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[13];
    return n >= 24 && pm_read(f, 0, b, 13) && component_tag(b, "ISO-10303-21;", 13);
}
typedef struct step_state {
    component_lexer q;
    component_id_set ids;
    uint32_t *refs, refcount, refcap;
} step_state;
static bool step_ref(step_state *v, uint32_t id) {
    uint32_t cap;
    void *p;
    if (v->refcount == v->refcap) {
        cap = v->refcap ? v->refcap * 2 : 64;
        if (cap > 262144)
            return false;
        p = xx_mem_realloc(v->refs, (size_t)cap * 4);
        if (!p)
            return false;
        v->refs = (uint32_t *)p;
        v->refcap = cap;
    }
    v->refs[v->refcount++] = id;
    return true;
}
static bool step_value(step_state *, unsigned, bool);
static bool step_params(step_state *v, unsigned depth, bool header) {
    unsigned count = 0;
    if (depth > 32 || !component_lexer_char_hash_block_comments(&v->q, '('))
        return false;
    if (component_lexer_char_hash_block_comments(&v->q, ')'))
        return true;
    do {
        if (++count > 100000 || !step_value(v, depth + 1, header))
            return false;
    } while (component_lexer_char_hash_block_comments(&v->q, ','));
    return component_lexer_char_hash_block_comments(&v->q, ')');
}
static bool step_value(step_state *v, unsigned depth, bool header) {
    component_lexer *q = &v->q;
    double number;
    int32_t id;
    uint64_t p, z;
    if (depth > 32 || !component_lexer_skip_hash_block_comments(q) || q->p == q->n)
        return false;
    if (q->b[q->p] == '(')
        return step_params(v, depth, header);
    if (q->b[q->p] == '\'')
        return component_lexer_quoted_hash_block_comments(q, '\'', NULL, NULL);
    if (component_lexer_char_hash_block_comments(q, '$') || component_lexer_char_hash_block_comments(q, '*'))
        return !header;
    if (component_lexer_char_hash_block_comments(q, '#'))
        return !header && component_lexer_integer_hash_block_comments(q, &id) && id > 0 && step_ref(v, (uint32_t)id);
    if (q->b[q->p] == '.' && component_span(q->p, 2, q->n) &&
        ((q->b[q->p + 1] >= 'A' && q->b[q->p + 1] <= 'Z') || (q->b[q->p + 1] >= 'a' && q->b[q->p + 1] <= 'z'))) {
        ++q->p;
        return component_lexer_identifier_hash_block_comments(q, &p, &z) &&
               component_lexer_char_hash_block_comments(q, '.');
    }
    if (q->b[q->p] == '"') {
        unsigned used = 0;
        uint8_t c;
        ++q->p;
        if (q->p == q->n || (c = q->b[q->p++]) < '0' || c > '3')
            return false;
        while (q->p < q->n && q->b[q->p] != '"') {
            c = q->b[q->p++];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) || ++used > 8192)
                return false;
        }
        return used && component_lexer_char_hash_block_comments(q, '"');
    }
    if ((q->b[q->p] >= 'A' && q->b[q->p] <= 'Z') || (q->b[q->p] >= 'a' && q->b[q->p] <= 'z'))
        return !header && component_lexer_identifier_hash_block_comments(q, &p, &z) && step_params(v, depth + 1, false);
    return component_lexer_number_hash_block_comments(q, &number);
}
static bool step_string_list(component_lexer *q) {
    unsigned count = 0;
    if (!component_lexer_char_hash_block_comments(q, '('))
        return false;
    do {
        if (++count > 1024 || !component_lexer_quoted_hash_block_comments(q, '\'', NULL, NULL))
            return false;
    } while (component_lexer_char_hash_block_comments(q, ','));
    return component_lexer_char_hash_block_comments(q, ')');
}
static bool step_header(step_state *v, const char *name, unsigned count) {
    unsigned i;
    component_lexer *q = &v->q;
    if (!component_lexer_keyword_hash_block_comments(q, name) || !component_lexer_char_hash_block_comments(q, '('))
        return false;
    for (i = 0; i < count; ++i) {
        bool list = count == 1 || (count == 2 && i == 0) || (count == 7 && (i == 2 || i == 3));
        if (i && !component_lexer_char_hash_block_comments(q, ','))
            return false;
        if (list) {
            if (!step_string_list(q))
                return false;
        } else if (!component_lexer_quoted_hash_block_comments(q, '\'', NULL, NULL))
            return false;
    }
    return component_lexer_char_hash_block_comments(q, ')') && component_lexer_char_hash_block_comments(q, ';');
}
static bool palette_cad_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    step_state v = {{b, 0, n, pd, 0, false, false, true}, {0}, NULL, 0, 0};
    component_lexer *q = &v.q;
    bool ok = false;
    uint32_t count = 0, i;
    char label[48];
    if (!component_utf8(b, n, true, pd) || !component_ids_init(&v.ids, 4096) ||
        !component_lexer_keyword_hash_block_comments(q, "ISO-10303-21") ||
        !component_lexer_char_hash_block_comments(q, ';') ||
        !component_lexer_keyword_hash_block_comments(q, "HEADER") ||
        !component_lexer_char_hash_block_comments(q, ';') || !step_header(&v, "FILE_DESCRIPTION", 2) ||
        !step_header(&v, "FILE_NAME", 7) || !step_header(&v, "FILE_SCHEMA", 1) ||
        !component_lexer_keyword_hash_block_comments(q, "ENDSEC") ||
        !component_lexer_char_hash_block_comments(q, ';') || !component_lexer_keyword_hash_block_comments(q, "DATA") ||
        !component_lexer_char_hash_block_comments(q, ';') || !component_emit(f, s, "descriptor.step", 0, q->p, n))
        goto done;
    while (!component_lexer_keyword_hash_block_comments(q, "ENDSEC")) {
        int32_t id;
        uint64_t start = q->p, at, z;
        if (++count > 4093 || !component_lexer_char_hash_block_comments(q, '#') ||
            !component_lexer_integer_hash_block_comments(q, &id) || id < 1 ||
            !component_id(&v.ids, (uint32_t)id, true, pd) || !component_lexer_char_hash_block_comments(q, '='))
            goto done;
        if (component_lexer_char_hash_block_comments(q, '(')) {
            unsigned types = 0;
            do {
                if (++types > 1024 || !component_lexer_identifier_hash_block_comments(q, &at, &z) ||
                    !step_params(&v, 0, false))
                    goto done;
            } while (!component_lexer_char_hash_block_comments(q, ')'));
        } else if (!component_lexer_identifier_hash_block_comments(q, &at, &z) || !step_params(&v, 0, false))
            goto done;
        if (!component_lexer_char_hash_block_comments(q, ';')) {
            goto done;
        }
        xx_rt_snprintf(label, sizeof(label), "entity-%u.step", (unsigned)id);
        if (!component_emit(f, s, label, start, q->p - start, n))
            goto done;
    }
    if (!count || !component_lexer_char_hash_block_comments(q, ';') ||
        !component_lexer_keyword_hash_block_comments(q, "END-ISO-10303-21") ||
        !component_lexer_char_hash_block_comments(q, ';') || !component_lexer_end_hash_block_comments(q))
        goto done;
    for (i = 0; i < v.refcount; ++i)
        if (!component_id(&v.ids, v.refs[i], false, pd))
            goto done;
    if (!component_cover(f, s, "syntax.step", n)) {
        goto done;
    }
    s->size = (int64_t)n;
    ok = true;
done:
    if (v.ids.values)
        xx_mem_free(v.ids.values);
    if (v.refs)
        xx_mem_free(v.refs);
    return ok;
}

void xx_step_part21_init(xx_step_part21 *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_STEP_PART21, "step");
    }
}
xx_step_part21 *xx_step_part21_create(xx_io_device *d, int64_t at) {
    xx_step_part21 *r = (xx_step_part21 *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_step_part21_init(r, d, at);
    return r;
}
void xx_step_part21_destroy(xx_step_part21 *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_step_part21_free(xx_step_part21 *r) {
    if (r) {
        xx_step_part21_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_step_part21_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_step_part21_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
