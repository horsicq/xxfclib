/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://docs.oracle.com/en/java/javase/24/docs/specs/serialization/protocol.html */
#include "xxfclib/formats/java_serialization/xx_java_serialization.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_protocol_framing.h"

typedef struct js_handle {
    uint8_t kind, array;
    uint64_t start, n;
} js_handle;
typedef struct js_state {
    Abstractformat *f;
    pm_stream *s;
    memory_blob *b;
    js_handle *h;
    unsigned count, work;
} js_state;
static bool handle(js_state *j, uint8_t kind, uint64_t start, uint64_t n, uint8_t array, unsigned *index)
{
    if (j->count >= 4096) return false;
    *index = j->count;
    j->h[j->count].kind = kind;
    j->h[j->count].start = start;
    j->h[j->count].n = n;
    j->h[j->count++].array = array;
    return true;
}
static bool ref(js_state *j, uint64_t *at, uint8_t kind, unsigned *index)
{
    if (!protocol_take(j->b, at, j->b->n, 4)) return false;
    uint32_t r = xx_data_get_u32(j->b->p + (size_t)*at - 4, 4, 0, true);
    if (r < 0x7e0000U || r - 0x7e0000U >= j->count) return false;
    *index = r - 0x7e0000U;
    return !kind || j->h[*index].kind == kind;
}
static bool cls(js_state *j, uint64_t *at, unsigned *index)
{
    memory_blob *b = j->b;
    uint64_t start = *at, name, n;
    unsigned h;
    if (!protocol_take(b, at, b->n, 1)) return false;
    uint8_t c = b->p[(size_t)*at - 1];
    if (c == 0x71) return ref(j, at, 1, index);
    if (c != 0x72 || !protocol_utf16string(b, at, b->n, &name, &n) || n < 2 || b->p[(size_t)name] != '[' || !protocol_take(b, at, b->n, 8)) return false;
    uint8_t type = b->p[(size_t)name + 1];
    if (type == 'L') {
        if (!protocol_eq(b, name, n, "[Ljava.lang.String;")) return false;
    } else if (n != 2 || (type != 'B' && type != 'C' && type != 'D' && type != 'F' && type != 'I' && type != 'J' && type != 'S' && type != 'Z')) return false;
    if (!handle(j, 1, name, n, type, &h) || !protocol_take(b, at, b->n, 5) || b->p[(size_t)*at - 5] != 2 || xx_data_get_u16(b->p + (size_t)*at - 4, 2, 0, true) ||
        b->p[(size_t)*at - 2] != 0x78 || b->p[(size_t)*at - 1] != 0x70 || !blob_add(j->f, j->s, b, "array-class-descriptor", start, *at - start))
        return false;
    *index = h;
    return true;
}
static bool value(js_state *j, uint64_t *at, unsigned depth)
{
    memory_blob *b = j->b;
    uint64_t start = *at, name, n;
    unsigned h;
    if (depth > 32 || ++j->work > 65536 || !protocol_take(b, at, b->n, 1)) return false;
    uint8_t c = b->p[(size_t)*at - 1];
    if (c == 0x70) return blob_add(j->f, j->s, b, "null", start, 1);
    if (c == 0x71) return ref(j, at, 0, &h) && j->h[h].kind != 1 && blob_add(j->f, j->s, b, "object-reference", start, 5);
    if (c == 0x74 || c == 0x7c) {
        if (c == 0x74) {
            if (!protocol_utf16string(b, at, b->n, &name, &n)) return false;
        } else {
            if (!protocol_take(b, at, b->n, 8)) return false;
            n = xx_data_get_u64(b->p + (size_t)*at - 8, 8, 0, true);
            name = *at;
            if (n > 65536 || !protocol_take(b, at, b->n, n) || !protocol_mutf(b, name, n)) return false;
        }
        return handle(j, 2, name, n, 0, &h) && blob_add(j->f, j->s, b, "modified-utf-string", name, n);
    }
    if (c != 0x75 || !cls(j, at, &h)) {
        return false;
    }
    uint8_t type = j->h[h].array;
    if (!protocol_take(b, at, b->n, 4)) return false;
    n = xx_data_get_u32(b->p + (size_t)*at - 4, 4, 0, true);
    if (n > 65536 || !handle(j, 3, start, n, type, &h) || !blob_add(j->f, j->s, b, "array-header", start, *at - start)) return false;
    name = *at;
    if (type == 'L') {
        for (uint64_t i = 0; i < n; ++i) {
            if (!blob_span(b, *at, 1)) return false;
            uint8_t tok = b->p[(size_t)*at];
            if (tok != 0x70 && tok != 0x71 && tok != 0x74 && tok != 0x7c) return false;
            if (tok == 0x71) {
                if (!blob_span(b, *at, 5)) return false;
                uint32_t id = xx_data_get_u32(b->p + (size_t)*at + 1, 4, 0, true);
                if (id < 0x7e0000U || id - 0x7e0000U >= j->count || j->h[id - 0x7e0000U].kind != 2) return false;
            }
            if (!value(j, at, depth + 1)) return false;
        }
        return true;
    }
    unsigned width = type == 'B' || type == 'Z' ? 1 : type == 'C' || type == 'S' ? 2 : type == 'F' || type == 'I' ? 4 : 8;
    if (!protocol_take(b, at, b->n, n * width)) return false;
    if (type == 'Z')
        for (uint64_t i = 0; i < n; ++i)
            if (b->p[(size_t)(name + i)] > 1) return false;
    return blob_add(j->f, j->s, b, "primitive-array", name, n * width);
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    js_state j;
    uint64_t at = 4;
    unsigned objects = 0;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    xx_mem_zero(&j, sizeof(j));
    j.f = f;
    j.s = s;
    j.b = &b;
    j.h = (js_handle *)xx_mem_alloc(4096 * sizeof(js_handle));
    BLOB_NEED(j.h && b.n >= 6 && xx_data_get_u32(b.p, 4, 0, true) == 0xaced0005U && blob_add(f, s, &b, "serialization-header", 0, 4));
    while (at < b.n) BLOB_NEED(++objects <= 1024 && value(&j, &at, 0));
    BLOB_NEED(objects >= 1 && j.count >= 1);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(j.h);
    xx_mem_free(b.p);
    return ok;
}

void xx_java_serialization_init(xx_java_serialization *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_JAVA_SERIALIZATION, "bin");
    }
}
xx_java_serialization *xx_java_serialization_create(xx_io_device *d, int64_t b)
{
    xx_java_serialization *r = (xx_java_serialization *)xx_mem_alloc(sizeof(*r));
    if (r) xx_java_serialization_init(r, d, b);
    return r;
}
void xx_java_serialization_destroy(xx_java_serialization *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_java_serialization_free(xx_java_serialization *r)
{
    if (r) {
        xx_java_serialization_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_java_serialization_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_java_serialization_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
