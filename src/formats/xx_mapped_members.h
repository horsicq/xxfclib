/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Streaming logical-image extents; no image-sized allocation or temporary file. */
#ifndef XX_MAPPED_MEMBERS_H
#define XX_MAPPED_MEMBERS_H
#include "xx_payload_members.h"
typedef struct mm_extent {
    int64_t source;
    uint64_t length;
} mm_extent;
typedef struct mm_map {
    mm_extent *extents;
    size_t count, capacity;
    uint64_t size;
} mm_map;
static void mm_free(void *p)
{
    mm_map *m = (mm_map *)p;
    if (m) {
        xx_mem_free(m->extents);
        xx_mem_free(m);
    }
}
static mm_map *mm_create(void)
{
    mm_map *m = (mm_map *)xx_mem_alloc(sizeof(*m));
    if (m) xx_mem_zero(m, sizeof(*m));
    return m;
}
static bool mm_add(Abstractformat *f, mm_map *m, int64_t source, uint64_t size)
{
    int64_t available = pm_available(f);
    mm_extent *next;
    if (!m || !size || available < 0 || source < -1 || (source >= 0 && ((uint64_t)source > (uint64_t)available || size > (uint64_t)available - (uint64_t)source)) ||
        size > INT64_MAX - m->size || m->count >= 1000000U)
        return false;
    if (m->count == m->capacity) {
        size_t cap = m->capacity ? m->capacity * 2U : 8U;
        next = (mm_extent *)xx_mem_realloc(m->extents, cap * sizeof(*next));
        if (!next) return false;
        m->extents = next;
        m->capacity = cap;
    }
    m->extents[m->count].source = source;
    m->extents[m->count++].length = size;
    m->size += size;
    return true;
}
static bool mm_read(Abstractformat *f, pm_member *member, uint64_t at, void *data, size_t size, xx_pd_struct *pd)
{
    mm_map *m = (mm_map *)member->context;
    uint64_t start = 0;
    size_t i, done = 0;
    if (!m || at > m->size || size > m->size - at) return false;
    for (i = 0; i < m->count && done < size; ++i) {
        mm_extent *e = &m->extents[i];
        uint64_t skip;
        size_t chunk;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (at >= start + e->length) {
            start += e->length;
            continue;
        }
        skip = at - start;
        chunk = (size_t)((e->length - skip) < (size - done) ? (e->length - skip) : (size - done));
        if (e->source < 0) xx_mem_zero((uint8_t *)data + done, chunk);
        else if (!pm_read(f, e->source + (int64_t)skip, (uint8_t *)data + done, chunk)) return false;
        at += chunk;
        done += chunk;
        start += e->length;
    }
    return done == size && (!pd || !xx_pd_is_stopped(pd));
}
static bool mm_emit(Abstractformat *f, pm_stream *s, const char *name, mm_map *m)
{
    pm_member *member;
    if (!m || !pm_add(f, s, name, 0, 0)) {
        mm_free(m);
        return false;
    }
    member = &s->items[s->count - 1U];
    member->size = (int64_t)m->size;
    member->packed_size = 0;
    member->offset = -1;
    member->context = m;
    member->free_context = mm_free;
    member->read_range = mm_read;
    return true;
}
static bool mm_text(Abstractformat *f, pm_stream *s, const char *name, const char *text)
{
    size_t n = xx_rt_strlen(text);
    uint8_t *copy = (uint8_t *)xx_mem_alloc(n ? n : 1);
    if (!copy) return false;
    xx_rt_memcpy(copy, text, n);
    if (!pm_add(f, s, name, 0, 0)) {
        xx_mem_free(copy);
        return false;
    }
    s->items[s->count - 1U].offset = -1;
    s->items[s->count - 1U].memory = copy;
    s->items[s->count - 1U].size = (int64_t)n;
    return true;
}
#endif
