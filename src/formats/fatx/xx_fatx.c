/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent read-only FATX filesystem image implementation.
 * Layout evidence: https://xboxdevwiki.net/FATX and
 * https://github.com/mborgerson/fatx/tree/master/libfatx
 */
#include "xxfclib/formats/fatx/xx_fatx.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

#ifdef FATX
#define FX_TYPE XX_FILE_TYPE_FATX
#else
#define FX_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define FX_SUPER 4096U
#define FX_SECTOR 512U
#define FX_ENTRY 64U
#define FX_MAX_CLUSTERS 8000000U
#define FX_MAX_MEMBERS 65536U
#define FX_MAX_DEPTH 32U
#define FX_MAX_PATH 1024U
#define FX_MAX_PATH_BYTES (16U * 1024U * 1024U)
#define FX_MAX_WORK 16000000U
#define FX_COPY 65536U

typedef struct fx_member_s {
    char *name;
    uint64_t header_offset;
    uint32_t first_cluster, size;
    uint8_t attributes;
    bool directory;
} fx_member;
typedef struct fx_task_s {
    uint32_t cluster, depth;
    size_t member_index; /* SIZE_MAX means the root directory. */
} fx_task;
typedef struct fx_view_s {
    xx_io_device *device;
    int64_t base;
    uint64_t bytes, data_offset, retained_memory, path_bytes;
    uint32_t cluster_bytes, cluster_count, root_cluster, work;
    uint8_t fat_width;
    uint8_t *claimed, *dirbuf;
    fx_member *members;
    size_t count, capacity, index;
    fx_task *tasks;
    size_t task_count, task_capacity;
} fx_view;

static bool fx_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool fx_work(fx_view *v, xx_pd_struct *pd)
{
    return !fx_stopped(pd) && ++v->work <= FX_MAX_WORK;
}
static bool fx_read(fx_view *v, uint64_t offset, void *data, size_t size, xx_pd_struct *pd)
{
    int64_t cursor;
    size_t done = 0U;
    bool ok = false;
    if (!v || offset > v->bytes || size > v->bytes - offset || offset > (uint64_t)(INT64_MAX - v->base) || fx_stopped(pd)) return false;
    cursor = xx_io_tell(v->device);
    if (cursor < 0) return false;
    if (!xx_io_seek64(v->device, v->base + (int64_t)offset, SEEK_SET)) {
        while (done < size && !fx_stopped(pd)) {
            ssize_t got = xx_io_read(v->device, (uint8_t *)data + done, size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(v->device, cursor, SEEK_SET)) ok = false;
    return ok && !fx_stopped(pd);
}
static bool fx_cluster_offset(const fx_view *v, uint32_t cluster, uint64_t *offset)
{
    uint64_t at;
    if (!v || cluster < 1U || cluster > v->cluster_count) return false;
    at = v->data_offset + (uint64_t)(cluster - 1U) * v->cluster_bytes;
    if (at > v->bytes || v->cluster_bytes > v->bytes - at) return false;
    *offset = at;
    return true;
}
static bool fx_claim(fx_view *v, uint32_t cluster)
{
    uint8_t mask;
    if (!v || cluster < 1U || cluster > v->cluster_count) return false;
    mask = (uint8_t)(1U << (cluster & 7U));
    if (v->claimed[cluster >> 3U] & mask) return false;
    v->claimed[cluster >> 3U] |= mask;
    return true;
}
static bool fx_next(fx_view *v, uint32_t cluster, uint32_t *next, bool *end, xx_pd_struct *pd)
{
    uint8_t raw[4];
    uint32_t value;
    uint64_t offset = FX_SUPER + (uint64_t)cluster * v->fat_width;
    if (!next || !end || cluster < 1U || cluster > v->cluster_count || offset > v->data_offset || v->fat_width > v->data_offset - offset || !fx_work(v, pd) ||
        !fx_read(v, offset, raw, v->fat_width, pd))
        return false;
    value = v->fat_width == 2U ? xx_data_get_u16(raw, 2, 0, false) : xx_data_get_u32(raw, 4, 0, false);
    *end = value == (v->fat_width == 2U ? UINT32_C(0xffff) : UINT32_MAX);
    if (!*end && (value < 1U || value > v->cluster_count)) return false;
    *next = value;
    return true;
}
static bool fx_case_equal(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return false;
    }
    return !*a && !*b;
}
static int fx_compare_names(const void *left, const void *right)
{
    const char *a = *(const char *const *)left;
    const char *b = *(const char *const *)right;
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return x < y ? -1 : 1;
    }
    return *a ? 1 : *b ? -1 : 0;
}
static bool fx_reserved_name(const char *name)
{
    static const char *const reserved[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    char stem[16];
    size_t i = 0U, j;
    while (name[i] && name[i] != '.' && i + 1U < sizeof(stem)) {
        stem[i] = name[i];
        ++i;
    }
    stem[i] = 0;
    for (j = 0U; j < sizeof(reserved) / sizeof(reserved[0]); ++j)
        if (fx_case_equal(stem, reserved[j])) return true;
    return i == 4U && stem[3] >= '0' && stem[3] <= '9' &&
           ((fx_case_equal(stem, "COM0") || fx_case_equal(stem, "COM1") || fx_case_equal(stem, "COM2") || fx_case_equal(stem, "COM3") || fx_case_equal(stem, "COM4") ||
             fx_case_equal(stem, "COM5") || fx_case_equal(stem, "COM6") || fx_case_equal(stem, "COM7") || fx_case_equal(stem, "COM8") || fx_case_equal(stem, "COM9")) ||
            (fx_case_equal(stem, "LPT0") || fx_case_equal(stem, "LPT1") || fx_case_equal(stem, "LPT2") || fx_case_equal(stem, "LPT3") || fx_case_equal(stem, "LPT4") ||
             fx_case_equal(stem, "LPT5") || fx_case_equal(stem, "LPT6") || fx_case_equal(stem, "LPT7") || fx_case_equal(stem, "LPT8") || fx_case_equal(stem, "LPT9")));
}
static bool fx_component(const uint8_t *raw, unsigned length, char out[132])
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0U;
    unsigned i;
    if (!length || length > 42U) return false;
    for (i = 0U; i < length; ++i) {
        unsigned char c = raw[i];
        bool safe = c >= 0x20U && c <= 0x7eU && c != '/' && c != '\\' && c != ':' && c != '<' && c != '>' && c != '"' && c != '|' && c != '?' && c != '*' && c != '~' &&
                    !(i == length - 1U && (c == '.' || c == ' '));
        if (safe) out[used++] = (char)c;
        else {
            out[used++] = '~';
            out[used++] = hex[c >> 4U];
            out[used++] = hex[c & 15U];
        }
    }
    out[used] = 0;
    if (fx_reserved_name(out)) {
        if (used + 1U >= 132U) return false;
        memmove(out + 1U, out, used + 1U);
        out[0] = '_';
    }
    return true;
}
static bool fx_append_task(fx_view *v, uint32_t cluster, uint32_t depth, size_t member_index)
{
    fx_task *grown;
    size_t capacity;
    if (v->task_count >= FX_MAX_MEMBERS + 1U || depth > FX_MAX_DEPTH) return false;
    if (v->task_count == v->task_capacity) {
        capacity = v->task_capacity ? v->task_capacity * 2U : 64U;
        if (capacity > FX_MAX_MEMBERS + 1U) capacity = FX_MAX_MEMBERS + 1U;
        grown = (fx_task *)xx_mem_realloc(v->tasks, capacity * sizeof(*grown));
        if (!grown) return false;
        v->tasks = grown;
        v->task_capacity = capacity;
    }
    v->tasks[v->task_count].cluster = cluster;
    v->tasks[v->task_count].depth = depth;
    v->tasks[v->task_count].member_index = member_index;
    ++v->task_count;
    return true;
}
static bool fx_append_member(fx_view *v, const char *parent, const char *leaf, uint64_t header, uint32_t first, uint32_t size, uint8_t attributes, bool directory,
                             size_t *new_index)
{
    fx_member *grown;
    char *name;
    size_t prefix = xx_str_len(parent), part = xx_str_len(leaf), bytes;
    size_t capacity;
    if (v->count >= FX_MAX_MEMBERS || part > FX_MAX_PATH || prefix > FX_MAX_PATH - part - 1U) return false;
    bytes = prefix + (prefix ? 1U : 0U) + part + 1U;
    if (bytes > FX_MAX_PATH) return false;
    if (bytes > FX_MAX_PATH_BYTES - v->path_bytes) return false;
    name = (char *)xx_mem_alloc(bytes);
    if (!name) return false;
    if (prefix) xx_rt_snprintf(name, bytes, "%s/%s", parent, leaf);
    else xx_rt_snprintf(name, bytes, "%s", leaf);
    if (v->count == v->capacity) {
        capacity = v->capacity ? v->capacity * 2U : 64U;
        if (capacity > FX_MAX_MEMBERS) capacity = FX_MAX_MEMBERS;
        grown = (fx_member *)xx_mem_realloc(v->members, capacity * sizeof(*grown));
        if (!grown) {
            xx_mem_free(name);
            return false;
        }
        v->members = grown;
        v->capacity = capacity;
    }
    *new_index = v->count;
    v->members[v->count].name = name;
    v->members[v->count].header_offset = header;
    v->members[v->count].first_cluster = first;
    v->members[v->count].size = size;
    v->members[v->count].attributes = attributes;
    v->members[v->count].directory = directory;
    ++v->count;
    v->path_bytes += bytes;
    return true;
}
static bool fx_verify_file(fx_view *v, uint32_t first, uint32_t size, xx_pd_struct *pd)
{
    uint32_t needed, cluster = first, i;
    if (!size) {
        uint32_t next;
        bool end;
        return !first || (fx_claim(v, first) && fx_next(v, first, &next, &end, pd) && end);
    }
    needed = (uint32_t)(((uint64_t)size + v->cluster_bytes - 1U) / v->cluster_bytes);
    if (needed > v->cluster_count || !first) return false;
    for (i = 0U; i < needed; ++i) {
        uint32_t next;
        bool end;
        if (!fx_claim(v, cluster) || !fx_next(v, cluster, &next, &end, pd)) return false;
        if (i + 1U == needed) {
            if (!end) return false;
        } else {
            if (end) return false;
            cluster = next;
        }
    }
    return true;
}
static bool fx_finish_directory_chain(fx_view *v, uint32_t cluster, xx_pd_struct *pd)
{
    uint32_t walked;
    for (walked = 0U; walked < v->cluster_count; ++walked) {
        uint32_t next;
        bool end;
        if (!fx_next(v, cluster, &next, &end, pd)) return false;
        if (end) return true;
        if (!fx_claim(v, next)) return false;
        cluster = next;
    }
    return false;
}
static bool fx_walk_directory(fx_view *v, const fx_task *task, xx_pd_struct *pd)
{
    uint32_t cluster = task->cluster;
    const char *parent = task->member_index == SIZE_MAX ? "" : v->members[task->member_index].name;
    uint32_t walked = 0U;
    for (;;) {
        uint64_t at;
        size_t i;
        uint32_t next;
        bool end;
        if (++walked > v->cluster_count || !fx_work(v, pd) || !fx_claim(v, cluster) || !fx_cluster_offset(v, cluster, &at) ||
            !fx_read(v, at, v->dirbuf, v->cluster_bytes, pd))
            return false;
        for (i = 0U; i < v->cluster_bytes / FX_ENTRY; ++i) {
            const uint8_t *entry = v->dirbuf + i * FX_ENTRY;
            uint8_t length = entry[0], attr = entry[1];
            uint32_t first, size;
            char leaf[132];
            size_t member_index;
            bool directory;
            if (length == 0xffU || length == 0U) return fx_finish_directory_chain(v, cluster, pd);
            if (length == 0xe5U) continue;
            if ((length == 1U && entry[2] == '.') || (length == 2U && entry[2] == '.' && entry[3] == '.')) continue;
            if (length > 42U || !fx_component(entry + 2U, length, leaf)) return false;
            first = xx_data_get_u32(entry + 44U, 4, 0, false);
            size = xx_data_get_u32(entry + 48U, 4, 0, false);
            directory = (attr & 0x10U) != 0U;
            if (directory ? (first < 1U || first > v->cluster_count) : !fx_verify_file(v, first, size, pd)) return false;
            if (!fx_append_member(v, parent, leaf, at + i * FX_ENTRY, first, size, attr, directory, &member_index)) return false;
            if (directory && !fx_append_task(v, first, task->depth + 1U, member_index)) return false;
        }
        if (!fx_next(v, cluster, &next, &end, pd) || end) return false;
        cluster = next;
    }
}
static void fx_view_free(void *data)
{
    fx_view *v = (fx_view *)data;
    size_t i;
    if (!v) return;
    for (i = 0U; i < v->count; ++i) xx_mem_free(v->members[i].name);
    xx_mem_free(v->members);
    xx_mem_free(v->tasks);
    xx_mem_free(v->claimed);
    xx_mem_free(v->dirbuf);
    xx_mem_free(v);
}
static fx_view *fx_parse(Abstractformat *self, xx_pd_struct *pd)
{
    fx_view *v;
    uint8_t header[FX_SUPER];
    uint32_t sectors, root;
    uint64_t nominal, fat_bytes, cluster_count, data_offset;
    int64_t total;
    size_t task_index, i;
    char **names = NULL;
    if (!self || !self->device || self->base_address < 0 || fx_stopped(pd) || (total = xx_io_total_size(self->device)) < self->base_address) return NULL;
    v = (fx_view *)xx_mem_calloc(1U, sizeof(*v));
    if (!v) return NULL;
    v->device = self->device;
    v->base = self->base_address;
    v->bytes = (uint64_t)(total - v->base);
    if (v->bytes < FX_SUPER + FX_SUPER + FX_SECTOR || v->bytes % FX_SECTOR || !fx_read(v, 0U, header, sizeof(header), pd) || memcmp(header, "FATX", 4U)) goto fail;
    sectors = xx_data_get_u32(header + 8U, 4, 0, false);
    root = xx_data_get_u32(header + 12U, 4, 0, false);
    if (!sectors || sectors > 1024U || (sectors & (sectors - 1U)) || root < 1U) goto fail;
    v->cluster_bytes = sectors * FX_SECTOR;
    nominal = v->bytes / v->cluster_bytes + 1U;
    v->fat_width = nominal < UINT64_C(0xfff0) ? 2U : 4U;
    if (nominal > UINT64_MAX / v->fat_width) goto fail;
    fat_bytes = nominal * v->fat_width;
    fat_bytes = (fat_bytes + FX_SUPER - 1U) & ~(uint64_t)(FX_SUPER - 1U);
    data_offset = FX_SUPER + fat_bytes;
    if (data_offset >= v->bytes) goto fail;
    cluster_count = (v->bytes - data_offset) / v->cluster_bytes;
    if (cluster_count < 1U || cluster_count > FX_MAX_CLUSTERS || root > cluster_count || FX_SUPER + (cluster_count + 1U) * v->fat_width > data_offset) goto fail;
    v->cluster_count = (uint32_t)cluster_count;
    v->root_cluster = root;
    v->data_offset = data_offset;
    v->claimed = (uint8_t *)xx_mem_calloc((size_t)(cluster_count / 8U) + 1U, 1U);
    v->dirbuf = (uint8_t *)xx_mem_alloc(v->cluster_bytes);
    if (!v->claimed || !v->dirbuf || !fx_append_task(v, root, 0U, SIZE_MAX)) goto fail;
    for (task_index = 0U; task_index < v->task_count; ++task_index) {
        /* Append-task may grow its array; never retain a pointer to it. */
        fx_task task = v->tasks[task_index];
        if (!fx_walk_directory(v, &task, pd)) goto fail;
    }
    if (v->count) {
        names = (char **)xx_mem_alloc(v->count * sizeof(*names));
        if (!names) goto fail;
        for (i = 0U; i < v->count; ++i) names[i] = v->members[i].name;
        qsort(names, v->count, sizeof(*names), fx_compare_names);
        for (i = 1U; i < v->count; ++i)
            if (fx_case_equal(names[i - 1U], names[i])) goto fail;
    }
    xx_mem_free(names);
    xx_mem_free(v->dirbuf);
    v->dirbuf = NULL;
    v->retained_memory = sizeof(*v) + ((uint64_t)cluster_count / 8U + 1U) + v->capacity * sizeof(*v->members) + v->task_capacity * sizeof(*v->tasks) + v->path_bytes;
    return v;
fail:
    xx_mem_free(names);
    fx_view_free(v);
    return NULL;
}
static void fx_vtable_destroy(Abstractformat *self)
{
    xx_fatx_destroy((xx_fatx *)self);
}
void xx_fatx_init(xx_fatx *volume, xx_io_device *device, int64_t base)
{
    if (!volume) return;
    xx_mem_zero(volume, sizeof(*volume));
    xx_format_init(&volume->format, device, base);
    volume->format.endian = XX_ENDIAN_LITTLE;
    volume->format.file_type = FX_TYPE;
    volume->format.format_type = XX_TYPE_ARCHIVE;
    volume->format.is_archive = true;
    xx_format_set_mime_type(&volume->format, "application/x-xbox-fatx");
    xx_format_set_extension(&volume->format, "fatx");
    volume->format.check_is_valid = xx_fatx_check_is_valid;
    volume->format.handle_base_info = xx_fatx_handle_base_info;
    volume->format.get_format_size = xx_fatx_get_format_size;
    volume->format.get_number_of_archive_records = xx_fatx_get_number_of_archive_records;
    volume->format.create_archive_records_reading = xx_fatx_create_archive_records_reading;
    volume->format.get_current_archive_record = xx_fatx_get_current_archive_record;
    volume->format.archive_record_move_to_next = xx_fatx_archive_record_move_to_next;
    volume->format.unpack_current_archive_record = xx_fatx_unpack_current_archive_record;
    volume->format.free_archive_records_reading = xx_fatx_free_archive_records_reading;
    volume->format.destroy = fx_vtable_destroy;
}
xx_fatx *xx_fatx_create(xx_io_device *device, int64_t base)
{
    xx_fatx *volume = (xx_fatx *)xx_mem_alloc(sizeof(*volume));
    if (volume) xx_fatx_init(volume, device, base);
    return volume;
}
void xx_fatx_destroy(xx_fatx *volume)
{
    if (volume) xx_format_cleanup_extra_parameters(&volume->format);
}
void xx_fatx_free(xx_fatx *volume)
{
    if (volume) {
        xx_fatx_destroy(volume);
        xx_mem_free(volume);
    }
}
bool xx_fatx_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    fx_view *v = fx_parse(self, pd);
    bool valid = v != NULL;
    fx_view_free(v);
    return valid;
}
bool xx_fatx_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_fatx *volume = (xx_fatx *)self;
    fx_view *v = fx_parse(self, pd);
    int64_t total, end;
    if (!self) return false;
    if (!v) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    volume->number_of_records = v->count;
    volume->bytes_per_cluster = v->cluster_bytes;
    volume->root_cluster = v->root_cluster;
    volume->fat_width = v->fat_width;
    self->format_size = (int64_t)v->bytes;
    self->number_of_archive_records = v->count;
    total = xx_io_total_size(self->device);
    end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1;
    self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true;
    self->base_info_handled = true;
    fx_view_free(v);
    return true;
}
int64_t xx_fatx_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return xx_fatx_handle_base_info(self, pd) ? self->format_size : -1;
}
uint64_t xx_fatx_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    return xx_fatx_handle_base_info(self, pd) ? ((xx_fatx *)self)->number_of_records : 0U;
}
static bool fx_record(xx_archive_record *record, const fx_view *v, const fx_member *m)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)m->header_offset;
    record->header_size = FX_ENTRY;
    record->data_offset = m->first_cluster ? v->base + (int64_t)(v->data_offset + (uint64_t)(m->first_cluster - 1U) * v->cluster_bytes) : -1;
    record->compressed_size = m->directory ? 0 : m->size;
    return xx_archive_record_set_original_name(record, m->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, m->directory ? 0U : m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, m->directory ? 0U : m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, m->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, m->directory);
}
static bool fx_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
xx_archive_record_state *xx_fatx_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    fx_view *v = fx_parse(self, pd);
    xx_archive_record_state *state;
    if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        fx_view_free(v);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = v;
    state->free_internal = fx_view_free;
    state->total_records = (int64_t)v->count;
    if (!fx_options(&state->options, options) || (v->count && !fx_record(&state->current_record, v, v->members))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = v->count != 0U;
    state->current_index = v->count ? 0 : -1;
    return state;
}
const xx_archive_record *xx_fatx_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_fatx_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    fx_view *v;
    if (!self || !state || state->format != self || !state->has_record || !(v = (fx_view *)state->internal_state) || fx_stopped(pd)) return false;
    if (v->index + 1U >= v->count) {
        v->index = v->count;
        state->has_record = false;
        state->current_index = -1;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        return false;
    }
    if (!fx_record(&state->current_record, v, v->members + v->index + 1U)) {
        state->has_record = false;
        return false;
    }
    ++v->index;
    ++state->current_index;
    return true;
}
static uint64_t fx_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback)
{
    const xx_var *value = xx_format_resolve_extra_parameter(self, options, id);
    if (!value) return fallback;
    switch (value->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(value);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t n = xx_var_get_i64(value);
            return n < 0 ? fallback : (uint64_t)n;
        }
        default: return fallback;
    }
}
static bool fx_limits(Abstractformat *self, xx_archive_record_state *state, const fx_view *v, const fx_member *m, size_t *buffer_size)
{
    *buffer_size = m->directory ? 0U : m->size < FX_COPY ? (size_t)m->size : FX_COPY;
    return m->size <= fx_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX) &&
           v->retained_memory + sizeof(*state) + *buffer_size <= fx_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
bool xx_fatx_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd)
{
    fx_view *v;
    const fx_member *m;
    uint8_t *buffer;
    uint32_t cluster, done = 0U;
    size_t buffer_size;
    bool ok = true;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record || !(v = (fx_view *)state->internal_state) ||
        v->index >= v->count || fx_stopped(pd))
        return false;
    m = v->members + v->index;
    if (!fx_limits(self, state, v, m, &buffer_size)) return false;
    if (m->directory || !buffer_size) return true;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size);
    if (!buffer) return false;
    cluster = m->first_cluster;
    while (done < m->size) {
        uint64_t at;
        uint32_t remaining = m->size - done;
        size_t amount = remaining < v->cluster_bytes ? remaining : v->cluster_bytes;
        size_t copied = 0U;
        uint32_t next;
        bool end;
        if (!fx_cluster_offset(v, cluster, &at)) {
            ok = false;
            break;
        }
        while (copied < amount) {
            size_t part = amount - copied, written = 0U;
            if (part > buffer_size) part = buffer_size;
            if (!fx_work(v, pd) || !fx_read(v, at + copied, buffer, part, pd)) {
                ok = false;
                break;
            }
            while (destination && written < part && !fx_stopped(pd)) {
                ssize_t got = xx_io_write(destination, buffer + written, part - written);
                if (got <= 0 || (size_t)got > part - written) {
                    ok = false;
                    break;
                }
                written += (size_t)got;
            }
            if (!ok || fx_stopped(pd)) {
                ok = false;
                break;
            }
            copied += part;
        }
        if (!ok || !fx_next(v, cluster, &next, &end, pd) || (remaining <= v->cluster_bytes) != end) {
            ok = false;
            break;
        }
        cluster = next;
        done += (uint32_t)amount;
    }
    xx_mem_free(buffer);
    return ok && done == m->size && !fx_stopped(pd);
}
static xx_io_device *fx_stage(const char *destination, char **stage_path)
{
    unsigned attempt;
    size_t i, parent = 0U;
    char *directory = xx_str_dup(destination);
    *stage_path = NULL;
    if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i)
        if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate;
        xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_fatx.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (fx_case_equal(candidate, destination)) {
            xx_str_free(candidate);
            continue;
        }
        output = xx_io_file_open(candidate, "wbx");
        if (output) {
            *stage_path = candidate;
            xx_str_free(directory);
            return output;
        }
        xx_str_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}
bool xx_fatx_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    fx_view *v;
    const xx_var *option, *overwrite_option;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage_path = NULL;
    size_t buffer_size;
    bool overwrite, ok = false;
    if (!self || !state || state->format != self || !state->has_record || !(v = (fx_view *)state->internal_state) || v->index >= v->count || fx_stopped(pd)) return false;
    if (!fx_limits(self, state, v, v->members + v->index, &buffer_size)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return xx_fatx_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", v->members[v->index].name)
                                                                                                : xx_str_concat(base, v->members[v->index].name);
    if (!path) goto done;
    if (v->members[v->index].directory) {
        ok = !fx_stopped(pd) && xx_store_create_dirs_a(path, true);
        goto done;
    }
    if ((!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = fx_stage(path, &stage_path);
        if (!output) goto done;
        ok = xx_fatx_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output)) ok = false;
    }
    if (fx_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(stage_path, path, overwrite);
done:
    if (!ok && stage_path) xx_io_file_remove_a(stage_path);
    xx_str_free(stage_path);
    xx_str_free(path);
    xx_str_free(owned);
    return ok;
}
void xx_fatx_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
