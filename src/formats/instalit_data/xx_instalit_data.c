/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * Layout: XArchive/core/xlegacystorearchive.cpp FT_INSTALIT_DATA branch.
 * Selected reader only: a raw DCL prefix does not identify its producer. */
#include "xxfclib/formats/instalit_data/xx_instalit_data.h"
#include "xxfclib/formats/dclraw/xx_dclraw.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "../xx_payload_members.h"
#define ID_MAX_INPUT (INT64_C(128) * 1024 * 1024)
#define ID_MAX_OUTPUT (UINT64_C(512) * 1024 * 1024)
#define ID_MAX_MEMBERS 65536U
#define ID_WINDOW 65536U
typedef struct id_name {
    int64_t offset;
    uint32_t run[2];
    char name[13];
} id_name;
static bool id_dos_char(unsigned c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || xx_rt_strchr("_$~!#%&(){}@'`^-", (int)c) != NULL;
}
static bool id_name_at(const uint8_t *p, size_t n, char *out)
{
    size_t i, length = 0;
    int dot = -1;
    if (n < 3 || p[0] != 0xf0) return false;
    while (length < 12 && length + 1 < n && p[length + 1]) ++length;
    if (!length || length + 1 >= n || p[length + 1]) return false;
    for (i = 0; i < length; ++i) {
        unsigned c = p[i + 1];
        if (c == '.') {
            if (dot >= 0 || i == 0 || i + 1 == length) return false;
            dot = (int)i;
        } else if (!id_dos_char(c)) return false;
        out[i] = (char)c;
    }
    if ((dot < 0 && length > 8) || dot > 8 || (dot >= 0 && length - (size_t)dot - 1 > 3)) return false;
    out[length] = 0;
    return true;
}
static size_t id_find(const id_name *names, size_t count, int64_t offset)
{
    size_t low = 0, high = count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (names[mid].offset < offset) low = mid + 1;
        else high = mid;
    }
    return low < count && names[low].offset == offset ? low : count;
}
static bool id_manifest(Abstractformat *f, pm_stream *s, int64_t start, int64_t total, xx_instalit_data_info *info, xx_pd_struct *pd)
{
    uint8_t *window = NULL;
    id_name *names = NULL;
    size_t count = 0, capacity = 0, best = 0;
    unsigned best_stride = 0;
    uint32_t best_count = 0;
    int64_t at = start;
    bool valid = false;
    if (start == total) return true;
    window = (uint8_t *)xx_mem_alloc(ID_WINDOW + 13U);
    if (!window) goto done;
    while (at < total) {
        size_t n = (uint64_t)(total - at) > ID_WINDOW ? ID_WINDOW : (size_t)(total - at);
        size_t read = n + (total - at - (int64_t)n > 13 ? 13 : (size_t)(total - at - (int64_t)n));
        size_t i;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, at, window, read)) goto done;
        for (i = 0; i < n; ++i) {
            char name[13];
            id_name *item;
            if (window[i] != 0xf0 || !id_name_at(window + i, read - i, name)) continue;
            if (count >= ID_MAX_MEMBERS) goto done;
            if (count == capacity) {
                size_t next = capacity ? capacity * 2 : 16;
                void *grown = xx_mem_realloc(names, next * sizeof(*names));
                if (!grown) goto done;
                names = (id_name *)grown;
                capacity = next;
            }
            item = &names[count++];
            item->offset = at + (int64_t)i;
            xx_mem_copy(item->name, name, xx_rt_strlen(name) + 1);
            item->run[0] = item->run[1] = 1;
        }
        at += (int64_t)n;
    }
    /* Dynamic programming over sorted candidate positions avoids quadratic
     * rescans of a long regular manifest. Equal-length runs select the earliest
     * marker, then 55-byte stride, making the choice reproducible. */
    for (size_t i = count; i > 0;) {
        size_t index = --i;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        for (unsigned k = 0; k < 2; ++k) {
            unsigned stride = k ? 59 : 55;
            size_t next = id_find(names, count, names[index].offset + stride);
            if (next < count) names[index].run[k] += names[next].run[k];
            if (names[index].run[k] > best_count || (names[index].run[k] == best_count && (!best_count || names[index].offset < names[best].offset ||
                                                                                           (names[index].offset == names[best].offset && stride < best_stride)))) {
                best = index;
                best_stride = stride;
                best_count = names[index].run[k];
            }
        }
    }
    if (best_count) {
        size_t last = id_find(names, count, names[best].offset + (int64_t)(best_count - 1) * best_stride);
        info->manifest_offset = f->base_address + names[best].offset;
        info->manifest_name_end = f->base_address + names[last].offset + 2 + (int64_t)xx_rt_strlen(names[last].name);
        info->manifest_stride = best_stride;
        info->manifest_run = best_count;
        if (best_count >= s->count) {
            for (size_t i = 0; i < s->count; ++i) {
                size_t index = id_find(names, count, names[best].offset + (int64_t)i * best_stride);
                char *name = xx_str_dup(names[index].name);
                if (!name) goto done;
                xx_str_free(s->items[i].display_name);
                s->items[i].display_name = name;
            }
            info->named_streams = s->count;
        }
    }
    valid = true;
done:
    xx_mem_free(window);
    xx_mem_free(names);
    return valid;
}
static bool id_read_all(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    uint8_t *packed = NULL, *plain = NULL;
    size_t at = 0, written = 0;
    bool valid = false;
    const xx_var *limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (m->packed_size < 4 || m->size < 0 || (uint64_t)m->packed_size > ID_MAX_INPUT || (uint64_t)m->size > ID_MAX_OUTPUT ||
        (limit && (uint64_t)m->packed_size + (uint64_t)m->size > xx_var_get_u64(limit)) || (pd && xx_pd_is_stopped(pd)))
        return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)m->packed_size);
    plain = (uint8_t *)xx_mem_alloc(m->size ? (size_t)m->size : 1);
    if (!packed || !plain) goto done;
    while (at < (size_t)m->packed_size) {
        size_t n = (size_t)m->packed_size - at;
        if (n > ID_WINDOW) n = ID_WINDOW;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, m->offset - f->base_address + (int64_t)at, packed + at, n)) goto done;
        at += n;
    }
    if (!xx_dcl_decode_memory(packed, (size_t)m->packed_size, plain, (size_t)m->size, &written) || written != (size_t)m->size) goto done;
    for (at = 0; out && at < written;) {
        size_t n = written - at;
        ssize_t count;
        if (n > ID_WINDOW) n = ID_WINDOW;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        count = xx_io_write(out, plain + at, n);
        if (count <= 0 || (size_t)count > n) goto done;
        at += (size_t)count;
    }
    valid = !(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return valid;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    xx_instalit_data_info info;
    int64_t total = pm_available(f), at = 0;
    uint64_t raw_total = 0;
    uint8_t prelude[2];
    xx_mem_zero(&info, sizeof(info));
    info.manifest_offset = info.manifest_name_end = -1;
    if (total < 4 || total > ID_MAX_INPUT || (pd && xx_pd_is_stopped(pd)) || !pm_read(f, 0, prelude, 2) || prelude[0] > 1 || prelude[1] < 4 || prelude[1] > 6)
        return false;
    while (at < total) {
        int64_t packed = 0;
        size_t raw = 0;
        char name[32];
        pm_member *m;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_dclraw_measure_stream(f->device, f->base_address + at, total - at, (size_t)(ID_MAX_OUTPUT - raw_total), &packed, &raw, pd)) break;
        if (packed < 4 || packed > total - at || raw > ID_MAX_OUTPUT - raw_total || s->count >= ID_MAX_MEMBERS) return false;
        if (!pm_add(f, s, "payload", at, packed)) return false;
        m = &s->items[s->count - 1];
        m->size = (int64_t)raw;
        m->compression_method = 1;
        m->read_all = id_read_all;
        xx_rt_snprintf(name, sizeof(name), "stream_%04u.bin", (unsigned)s->count);
        m->display_name = xx_str_dup(name);
        if (!m->display_name) return false;
        at += packed;
        raw_total += raw;
    }
    if (!s->count || (pd && xx_pd_is_stopped(pd))) return false;
    info.complete_streams = s->count;
    info.data_end = f->base_address + at;
    info.trailing_size = total - at;
    if (!id_manifest(f, s, at, total, &info, pd)) return false;
    ((xx_instalit_data *)f)->info = info;
    s->size = total;
    return !(pd && xx_pd_is_stopped(pd));
}
static void id_record(xx_archive_record_state *st)
{
    st->current_record.header_offset = st->current_record.data_offset;
}
static xx_archive_record_state *id_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_archive_record_state *st = pm_create_records(f, opts, pd);
    if (st && st->has_record) id_record(st);
    return st;
}
static bool id_next(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd)
{
    if (!pm_next(f, st, pd)) return false;
    id_record(st);
    return true;
}
void xx_instalit_data_init(xx_instalit_data *r, xx_io_device *d, int64_t base)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        r->info.manifest_offset = r->info.manifest_name_end = -1;
        pm_init(&r->format, d, base, XX_FILE_TYPE_INSTALIT_DATA, "001");
        r->format.endian = XX_ENDIAN_LITTLE;
        xx_format_set_mime_type(&r->format, "application/x-instalit-data");
        r->format.create_archive_records_reading = id_records;
        r->format.archive_record_move_to_next = id_next;
    }
}
xx_instalit_data *xx_instalit_data_create(xx_io_device *d, int64_t base)
{
    xx_instalit_data *r = (xx_instalit_data *)xx_mem_alloc(sizeof(*r));
    if (r) xx_instalit_data_init(r, d, base);
    return r;
}
void xx_instalit_data_destroy(xx_instalit_data *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_instalit_data_free(xx_instalit_data *r)
{
    if (r) {
        xx_instalit_data_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_instalit_data_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_instalit_data_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
bool xx_instalit_data_get_info(xx_instalit_data *r, xx_instalit_data_info *info, xx_pd_struct *pd)
{
    pm_stream *s;
    if (!info) return false;
    xx_mem_zero(info, sizeof(*info));
    info->manifest_offset = info->manifest_name_end = -1;
    if (!r || !(s = pm_open(&r->format, pd))) return false;
    pm_free_stream(s);
    *info = r->info;
    return true;
}
xx_file_type_t xx_instalit_data_detect(xx_io_device *d, int64_t base)
{
    xx_instalit_data r;
    bool valid;
    xx_instalit_data_init(&r, d, base);
    valid = pm_valid(&r.format, NULL);
    xx_instalit_data_destroy(&r);
    return valid ? XX_FILE_TYPE_INSTALIT_DATA : XX_FILE_TYPE_UNKNOWN;
}
