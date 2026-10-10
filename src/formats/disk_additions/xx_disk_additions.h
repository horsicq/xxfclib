/* SPDX-License-Identifier: MIT. Original streaming run-map engine.
 * Reuses the project's MIT payload lifecycle/atomic output primitives.
 * Runs describe physical stored slices or explicit repeated fill bytes;
 * no disk-size allocation and NULL-destination passes still read all data.
 */
#ifndef XX_DISK_ADDITIONS_PRIVATE_H
#define XX_DISK_ADDITIONS_PRIVATE_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#include "../hxc_afi/xx_hxc_tracks.h"
#define DA_MAP_METHOD 65535U
#define DA_MAX_RUNS 262144U
#define DA_MAX_LOGICAL UINT64_C(8796093022208)
typedef struct da_run {
    uint64_t at, bytes, count, stride;
    int fill;
    xx_io_device *source;
} da_run;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4200)
#endif
typedef struct da_map {
    uint64_t count, bytes;
    da_run runs[];
} da_map;
#ifdef _MSC_VER
#pragma warning(pop)
#endif
static bool da_poll(xx_pd_struct *pd)
{
    return !pd || !xx_pd_is_stopped(pd);
}
static XXFC_MAYBE_UNUSED bool da_read(Abstractformat *f, uint64_t at, void *p, size_t n, xx_pd_struct *pd)
{
    size_t done = 0;
    while (done < n) {
        size_t z = n - done > 65536U ? 65536U : n - done;
        if (!da_poll(pd) || at > INT64_MAX - done || !pm_read(f, (int64_t)at + done, (uint8_t *)p + done, z)) return false;
        done += z;
    }
    return da_poll(pd);
}
static bool da_budget(Abstractformat *f, pm_stream *s, uint64_t extra)
{
    size_t i;
    uint64_t total = 65536U + sizeof(*s), capacity = s->capacity;
    if (s->count == capacity) capacity = capacity ? capacity * 2U : 8U;
    total += capacity * sizeof(pm_member);
    for (i = 0; i < s->count; ++i)
        if (s->items[i].memory) {
            uint64_t n =
                s->items[i].compression_method == DA_MAP_METHOD ? sizeof(da_map) + ((da_map *)s->items[i].memory)->count * sizeof(da_run) : (uint64_t)s->items[i].size;
            if (n > UINT64_MAX - total) return false;
            total += n;
        }
    return extra <= UINT64_MAX - total && hx_limit(f, XX_META_ID_OPT_MEMORY_LIMIT, total + extra);
}
static bool da_add(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n)
{
    return at <= INT64_MAX && n <= INT64_MAX && da_budget(f, s, 0) && hx_limit(f, XX_META_ID_OPT_MAX_MEMBER_SIZE, n) && pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static bool da_map_add(Abstractformat *f, pm_stream *s, const char *name, const da_run *runs, size_t count)
{
    uint64_t total = 0, packed = 0;
    size_t i, n;
    da_map *map;
    if (!count || count > DA_MAX_RUNS || count > (SIZE_MAX - sizeof(*map)) / sizeof(*runs)) return false;
    n = sizeof(*map) + count * sizeof(*runs);
    if (!da_budget(f, s, n)) return false;
    for (i = 0; i < count; ++i) {
        const da_run *r = runs + i;
        uint64_t extent, sz;
        int64_t limit;
        if (!r->bytes || !r->count || r->bytes > DA_MAX_LOGICAL / r->count || r->fill > 255 || r->fill < -1) return false;
        sz = r->bytes * r->count;
        if (sz > DA_MAX_LOGICAL - total) return false;
        total += sz;
        if (r->fill < 0) {
            if (r->count > 1U && r->stride > (UINT64_MAX - r->bytes) / (r->count - 1U)) return false;
            extent = r->bytes + (r->count - 1U) * r->stride;
            limit = r->source ? xx_io_size(r->source) : pm_available(f);
            if (limit < 0 || r->at > (uint64_t)limit || extent > (uint64_t)limit - r->at || sz > UINT64_MAX - packed) {
                return false;
            }
            packed += sz;
        }
    }
    if (!hx_limit(f, XX_META_ID_OPT_MAX_MEMBER_SIZE, total) || !pm_add(f, s, name, 0, 0)) return false;
    map = (da_map *)xx_mem_alloc(n);
    if (!map) {
        --s->count;
        return false;
    }
    map->count = count;
    map->bytes = total;
    xx_rt_memcpy(map->runs, runs, count * sizeof(*runs));
    s->items[s->count - 1U].memory = (uint8_t *)map;
    s->items[s->count - 1U].size = (int64_t)total;
    s->items[s->count - 1U].packed_size = (int64_t)packed;
    s->items[s->count - 1U].compression_method = DA_MAP_METHOD;
    return true;
}
static XXFC_MAYBE_UNUSED bool da_one(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t bytes, uint64_t count, uint64_t stride, int fill,
                                     xx_io_device *source)
{
    da_run r;
    r.at = at;
    r.bytes = bytes;
    r.count = count;
    r.stride = stride;
    r.fill = fill;
    r.source = source;
    return da_map_add(f, s, name, &r, 1U);
}
static const xx_archive_record *da_current(Abstractformat *f, xx_archive_record_state *st)
{
    const xx_archive_record *r = pm_current(f, st);
    if (r && !xx_archive_record_set_meta_u64(&st->current_record, XX_META_ID_COMPRESSION_METHOD, 0)) return NULL;
    return r;
}
static bool da_unpack(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd)
{
    pm_stream *s;
    pm_member *m;
    da_map *map;
    const xx_var *v;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage = NULL;
    xx_io_device *out = NULL;
    uint8_t *buf = NULL;
    size_t i;
    bool ok = false, overwrite = false;
    int64_t cursor;
    if (!f || !st || st->format != f || !st->has_record || !da_poll(pd)) {
        return false;
    }
    s = (pm_stream *)st->internal_state;
    m = s->items + s->index;
    if (m->compression_method != DA_MAP_METHOD) return pm_unpack(f, st, pd);
    map = (da_map *)m->memory;
    v = xx_format_resolve_extra_parameter(f, &st->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v && (uint64_t)m->size > xx_var_get_u64(v)) return false;
    v = xx_format_resolve_extra_parameter(f, &st->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (v && sizeof(*map) + map->count * sizeof(da_run) + 65536U > xx_var_get_u64(v)) return false;
    v = xx_format_resolve_extra_parameter(f, &st->options, XX_META_ID_OPT_UNPACK_PATH);
    if (v) {
        if (v->type == XX_VAR_TYPE_STRING || v->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(v);
        else if (v->type == XX_VAR_TYPE_WSTRING || v->type == XX_VAR_TYPE_WSTRING_VIEW) base = owned = xx_str_unicode_to_utf8(xx_var_get_wstr(v));
        if (!base) goto done;
        path = base[0] ? xx_str_concat3(base, "/", m->name) : xx_str_dup(m->name);
        if (!path || !xx_store_create_dirs_a(path, false)) goto done;
        v = xx_format_resolve_extra_parameter(f, &st->options, XX_META_ID_OPT_OVERWRITE);
        overwrite = v && xx_var_get_bool(v);
        if (!overwrite && xx_io_file_exists_a(path)) goto done;
        out = pm_stage(path, &stage);
        if (!out) goto done;
    }
    buf = (uint8_t *)xx_mem_alloc(65536U);
    if (!buf) goto done;
    cursor = xx_io_tell(f->device);
    ok = true;
    for (i = 0; ok && i < map->count; ++i) {
        da_run *r = map->runs + i;
        uint64_t repetition;
        xx_io_device *src = r->source ? r->source : f->device;
        int64_t saved = xx_io_tell(src);
        if (!out && r->fill >= 0) {
            if (!da_poll(pd)) ok = false;
            continue;
        }
        for (repetition = 0; ok && repetition < r->count; ++repetition) {
            uint64_t at = r->at + repetition * r->stride, left = r->bytes;
            if (!r->source) at += (uint64_t)f->base_address;
            while (ok && left) {
                size_t z = left > 65536U ? 65536U : (size_t)left, got = 0, written = 0;
                if (!da_poll(pd)) {
                    ok = false;
                    break;
                }
                if (r->fill >= 0) xx_rt_memset(buf, r->fill, z);
                else {
                    if (at > INT64_MAX || xx_io_seek64(src, (int64_t)at, SEEK_SET) != 0) {
                        ok = false;
                        break;
                    }
                    while (got < z) {
                        ssize_t n;
                        if (!da_poll(pd)) {
                            ok = false;
                            break;
                        }
                        n = xx_io_read(src, buf + got, z - got);
                        if (n <= 0 || (size_t)n > z - got) {
                            ok = false;
                            break;
                        }
                        got += (size_t)n;
                    }
                }
                while (ok && out && written < z) {
                    ssize_t n = xx_io_write(out, buf + written, z - written);
                    if (n <= 0 || (size_t)n > z - written) {
                        ok = false;
                        break;
                    }
                    written += (size_t)n;
                }
                at += z;
                left -= z;
            }
        }
        (void)xx_io_seek64(src, saved, SEEK_SET);
    }
    (void)xx_io_seek64(f->device, cursor, SEEK_SET);
    if (!da_poll(pd)) ok = false;
done:
    if (buf) {
        xx_mem_free(buf);
    }
    if (out && xx_io_close(out) != 0) ok = false;
    if (ok && stage) ok = da_poll(pd) && xx_io_file_replace_a(stage, path, overwrite);
    if (stage) {
        if (!ok) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    if (path) xx_str_free(path);
    if (owned) xx_str_free(owned);
    return ok;
}
static xx_archive_record_state *da_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_disk_additions_info parse;
    xx_archive_record_state *st = NULL;
    unsigned i;
    const uint32_t ids[] = {XX_META_ID_OPT_MEMORY_LIMIT, XX_META_ID_OPT_MAX_MEMBER_SIZE};
    if (!f) {
        return NULL;
    }
    parse = *(xx_disk_additions_info *)f;
    xx_format_init(&parse.format, f->device, f->base_address);
    parse.format.file_type = f->file_type;
    parse.reading_records = true;
    for (i = 0; i < 2U; ++i) {
        const xx_var *v = xx_format_resolve_extra_parameter(f, opts, ids[i]);
        if (v && !xx_format_set_extra_parameter(&parse.format, ids[i], v)) goto done;
    }
    if (!hx_limit(&parse.format, XX_META_ID_OPT_MEMORY_LIMIT, 65536U) || !hx_limit(&parse.format, XX_META_ID_OPT_MAX_MEMBER_SIZE, 4U)) goto done;
    st = pm_create_records(&parse.format, opts, pd);
    if (st) {
        st->format = f;
        ((xx_disk_additions_info *)f)->incomplete = parse.incomplete;
    }
done:
    xx_format_cleanup_extra_parameters(&parse.format);
    return st;
}
#define DA_API(stem, type, ext)                                                                                                         \
    void xx_##stem##_init(xx_##stem *r, xx_io_device *d, int64_t a)                                                                     \
    {                                                                                                                                   \
        if (r) {                                                                                                                        \
            xx_mem_zero(r, sizeof(*r));                                                                                                 \
            pm_init(&r->format, d, a, type, ext);                                                                                       \
            r->format.check_is_valid = xx_##stem##_check_is_valid;                                                                      \
            r->format.handle_base_info = xx_##stem##_handle_base_info;                                                                  \
            r->format.create_archive_records_reading = da_records;                                                                      \
            r->format.get_current_archive_record = da_current;                                                                          \
            r->format.unpack_current_archive_record = da_unpack;                                                                        \
            r->sample_hz = 16000000U;                                                                                                   \
            r->index_bit = 3U;                                                                                                          \
        }                                                                                                                               \
    }                                                                                                                                   \
    xx_##stem *xx_##stem##_create(xx_io_device *d, int64_t a)                                                                           \
    {                                                                                                                                   \
        xx_##stem *r = (xx_##stem *)xx_mem_alloc(sizeof(*r));                                                                           \
        if (r) xx_##stem##_init(r, d, a);                                                                                               \
        return r;                                                                                                                       \
    }                                                                                                                                   \
    void xx_##stem##_destroy(xx_##stem *r)                                                                                              \
    {                                                                                                                                   \
        if (r) {                                                                                                                        \
            unsigned di;                                                                                                                \
            for (di = 0; di < 256U; ++di)                                                                                               \
                if (r->owns_tracks[di] && r->track_sources[di]) xx_io_close(r->track_sources[di]);                                      \
            if (r->owns_companion && r->companion) xx_io_close(r->companion);                                                           \
            if (r->owns_subchannel && r->subchannel) xx_io_close(r->subchannel);                                                        \
            xx_format_cleanup_extra_parameters(&r->format);                                                                             \
        }                                                                                                                               \
    }                                                                                                                                   \
    void xx_##stem##_free(xx_##stem *r)                                                                                                 \
    {                                                                                                                                   \
        if (r) {                                                                                                                        \
            xx_##stem##_destroy(r);                                                                                                     \
            xx_mem_free(r);                                                                                                             \
        }                                                                                                                               \
    }                                                                                                                                   \
    bool xx_##stem##_check_is_valid(Abstractformat *f, xx_pd_struct *pd)                                                                \
    {                                                                                                                                   \
        return hx_limit(f, XX_META_ID_OPT_MEMORY_LIMIT, 65536U) && hx_limit(f, XX_META_ID_OPT_MAX_MEMBER_SIZE, 4U) && pm_valid(f, pd);  \
    }                                                                                                                                   \
    bool xx_##stem##_handle_base_info(Abstractformat *f, xx_pd_struct *pd)                                                              \
    {                                                                                                                                   \
        return hx_limit(f, XX_META_ID_OPT_MEMORY_LIMIT, 65536U) && hx_limit(f, XX_META_ID_OPT_MAX_MEMBER_SIZE, 4U) && pm_handle(f, pd); \
    }
#endif
