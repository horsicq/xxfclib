/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component-preservation helpers. Each reader owns its own grammar;
 * no HxC implementation is copied or linked.
 */
#ifndef XX_HXC_SECTOR_HELPERS_H
#define XX_HXC_SECTOR_HELPERS_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#include "xx_payload_members.h"
#define HC_MAX_BYTES (16U * 1024U * 1024U)
#define HC_MAX_MEMBERS 4096U
typedef struct hc_blob { uint8_t *p; uint32_t n; uint64_t used, limit; xx_pd_struct *pd; } hc_blob;
typedef struct hc_span { uint32_t at, size; } hc_span;
static bool hc_poll(const hc_blob *b) { return !b->pd || !xx_pd_is_stopped(b->pd); }
static bool hc_span_ok(const hc_blob *b, uint32_t at, uint32_t size) { return at <= b->n && size <= b->n - at; }
static const xx_var *hc_option(Abstractformat *f, xx_meta_id_t id) {
    return xx_format_resolve_extra_parameter(f, ((xx_hxc_sector_info *)f)->parse_options, id);
}
static bool hc_load(Abstractformat *f, hc_blob *b, xx_pd_struct *pd) {
    int64_t size = pm_available(f); uint32_t at = 0U; const xx_var *v;
    xx_mem_zero(b, sizeof(*b)); b->pd = pd; b->limit = UINT64_C(64) * 1024U * 1024U;
    v = hc_option(f, XX_META_ID_OPT_MEMORY_LIMIT); if (v) b->limit = xx_var_get_u64(v);
    if (size <= 0 || size > HC_MAX_BYTES || (uint64_t)size > b->limit || !hc_poll(b)) return false;
    b->p = (uint8_t *)xx_mem_alloc((size_t)size); if (!b->p) return false;
    b->n = (uint32_t)size; b->used = (uint64_t)size;
    while (at < b->n) {
        uint32_t chunk = b->n - at; if (chunk > 4096U) chunk = 4096U;
        if (!hc_poll(b) || !pm_read(f, at, b->p + at, chunk) || !hc_poll(b)) {
            xx_mem_free(b->p); b->p = NULL; return false;
        }
        at += chunk;
    }
    return true;
}
static uint8_t *hc_alloc(hc_blob *b, uint32_t size) {
    uint8_t *p;
    if (!size || size > HC_MAX_BYTES || b->used > b->limit || size > b->limit - b->used || !hc_poll(b)) return NULL;
    p = (uint8_t *)xx_mem_alloc(size); if (p) b->used += size; return p;
}
static bool hc_emit(Abstractformat *f, pm_stream *s, const hc_blob *b, const char *name, uint32_t at, uint32_t size) {
    const xx_var *v = hc_option(f, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    return s->count < HC_MAX_MEMBERS && hc_poll(b) && hc_span_ok(b, at, size) &&
        (!v || size <= xx_var_get_u64(v)) && pm_add(f, s, name, at, size);
}
static bool hc_memory(Abstractformat *f, pm_stream *s, const hc_blob *b, const char *name, uint8_t *p, uint32_t size) {
    const xx_var *v = hc_option(f, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if ((v && size > xx_var_get_u64(v)) || !hc_emit(f, s, b, name, 0U, 0U)) { xx_mem_free(p); return false; }
    s->items[s->count - 1U].memory = p; s->items[s->count - 1U].size = size;
    s->items[s->count - 1U].packed_size = 0; s->items[s->count - 1U].offset = -1; return true;
}
static bool hc_disjoint(hc_span *spans, uint32_t *count, uint32_t capacity, uint32_t at, uint32_t size) {
    uint32_t i;
    if (!size) return true;
    if (*count >= capacity || at > UINT32_MAX - size) return false;
    for (i = 0U; i < *count; ++i)
        if (at < spans[i].at + spans[i].size && spans[i].at < at + size) return false;
    spans[*count].at = at; spans[*count].size = size; ++*count; return true;
}
static bool hc_ascii_name(const uint8_t *p, uint32_t size) {
    uint32_t i; bool nonblank = false;
    for (i = 0U; i < size; ++i) { if (p[i] < 32U || p[i] > 126U) return false; if (p[i] != ' ') nonblank = true; }
    return nonblank;
}
static bool hc_geometry(xx_hxc_sector_info *r, uint32_t c, uint32_t h, uint32_t s, uint32_t bytes, uint32_t *total) {
    uint64_t n = (uint64_t)c * h * s * bytes;
    if (!c || c > 255U || !h || h > 2U || !s || s > 64U || bytes < 64U || bytes > 8192U ||
        (bytes & (bytes - 1U)) || !n || n > HC_MAX_BYTES) return false;
    r->cylinders = c; r->heads = h; r->sectors_per_track = s; r->sector_size = bytes; *total = (uint32_t)n; return true;
}
static uint16_t hc_crc16(const uint8_t *p, uint32_t n, uint16_t crc) {
    uint32_t i; unsigned bit;
    for (i = 0U; i < n; ++i) { crc ^= (uint16_t)p[i] << 8U; for (bit = 0U; bit < 8U; ++bit)
        crc = (uint16_t)((crc << 1U) ^ ((crc & 0x8000U) ? 0x1021U : 0U)); }
    return crc;
}
static xx_archive_record_state *hc_create_records(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    xx_hxc_sector_info *r = (xx_hxc_sector_info *)f; const xx_list_s *previous;
    xx_archive_record_state *state;
    if (!r) return NULL;
    previous = r->parse_options; r->parse_options = options;
    state = pm_create_records(f, options, pd); r->parse_options = previous; return state;
}
static const xx_archive_record *hc_current(Abstractformat *f, xx_archive_record_state *state) {
    const xx_archive_record *record = pm_current(f, state); xx_hxc_sector_info *r = (xx_hxc_sector_info *)f;
    char comment[256];
    if (!record) return NULL;
    xx_rt_snprintf(comment, sizeof(comment), "cylinders=%u heads=%u sectors/track=%u sector-size=%u; %s",
        r->cylinders, r->heads, r->sectors_per_track, r->sector_size, r->note ? r->note : "original image components");
    if (!xx_archive_record_set_meta_str(&state->current_record, XX_META_ID_COMMENT, comment)) return NULL;
    return record;
}
#define HC_DEFINE_READER(stem, type, extension) \
void xx_##stem##_init(xx_##stem *r, xx_io_device *d, int64_t base) { \
    if (r) { xx_mem_zero(r, sizeof(*r)); pm_init(&r->format, d, base, type, extension); \
        r->format.create_archive_records_reading = hc_create_records; r->format.get_current_archive_record = hc_current; } } \
xx_##stem *xx_##stem##_create(xx_io_device *d, int64_t base) { \
    xx_##stem *r = (xx_##stem *)xx_mem_alloc(sizeof(*r)); if (r) xx_##stem##_init(r, d, base); return r; } \
void xx_##stem##_destroy(xx_##stem *r) { if (r) xx_format_cleanup_extra_parameters(&r->format); } \
void xx_##stem##_free(xx_##stem *r) { if (r) { xx_##stem##_destroy(r); xx_mem_free(r); } }
#define HC_PARSE_WRAPPER(parser) \
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) { \
    hc_blob b; bool ok; xx_hxc_sector_info *r = (xx_hxc_sector_info *)f; \
    r->incomplete = false; r->note = NULL; r->number_of_records = 0U; \
    r->cylinders = r->heads = r->sectors_per_track = r->sector_size = 0U; \
    if (!hc_load(f, &b, pd)) return false; \
    ok = parser(f, s, &b) && hc_poll(&b); \
    if (ok) { s->size = b.n; r->number_of_records = s->count; } \
    xx_mem_free(b.p); return ok; }
#endif
