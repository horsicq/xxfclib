/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native implementation from Acorn's published FileCore old-map layout.
 */
#include "xxfclib/formats/acorn_adfs/xx_acorn_adfs.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include <stdio.h>
#ifdef ADFS
#define AD_TYPE XX_FILE_TYPE_ADFS
#else
#define AD_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define AD_SECTOR 256U
#define AD_DIR_SIZE 1280U
#define AD_MAX_MEMBERS 8192U
#define AD_MAX_DEPTH 64U
#define AD_MAX_PATH 4096U
#define AD_MAX_PATH_BYTES (UINT64_C(4) * 1024U * 1024U)
#define AD_MAX_WORK 2000000U
#define AD_COPY 256U
typedef struct ad_member_s {
    char *name;
    uint64_t header;
    uint32_t first, size, attributes;
    bool directory;
} ad_member;
typedef struct ad_view_s {
    xx_io_device *device;
    int64_t base;
    uint64_t bytes, path_bytes, retained_memory;
    uint32_t sectors, work;
    xx_acorn_adfs_variant variant;
    xx_acorn_adfs_order order;
    uint8_t free_map[320], claimed[320];
    ad_member *members;
    uint32_t *names;
    size_t count, index, capacity, name_capacity;
    char volume_name[16];
} ad_view;
static uint32_t ad_u24(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }
static uint32_t ad_u32(const uint8_t *p) { return ad_u24(p) | ((uint32_t)p[3] << 24); }
static bool ad_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool ad_work(ad_view *v, xx_pd_struct *pd) { return !ad_stopped(pd) && ++v->work <= AD_MAX_WORK; }
static bool ad_bit(const uint8_t *map, uint32_t sector) { return (map[sector / 8U] & (0x80U >> (sector % 8U))) != 0U; }
static void ad_set(uint8_t *map, uint32_t sector) { map[sector / 8U] |= (uint8_t)(0x80U >> (sector % 8U)); }
static uint32_t ad_physical(const ad_view *v, uint32_t sector) {
    uint32_t half;
    if (v->sectors != XX_ACORN_ADFS_L || v->order == XX_ACORN_ADFS_ORDER_LINEAR) return sector;
    half = sector < 1280U ? sector : sector - 1280U;
    return (half / 16U) * 32U + (sector < 1280U ? 0U : 16U) + half % 16U;
}
static bool ad_read_abs(ad_view *v, uint64_t offset, void *out, size_t size, xx_pd_struct *pd) {
    int64_t saved; size_t done = 0U; bool ok = false;
    if (!v || offset > v->bytes || size > v->bytes - offset || offset > (uint64_t)(INT64_MAX - v->base) || ad_stopped(pd)) return false;
    saved = xx_io_tell(v->device); if (saved < 0) return false;
    if (!xx_io_seek64(v->device, v->base + (int64_t)offset, SEEK_SET)) {
        while (done < size && !ad_stopped(pd)) {
            ssize_t got = xx_io_read(v->device, (uint8_t *)out + done, size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(v->device, saved, SEEK_SET)) ok = false;
    return ok && !ad_stopped(pd);
}
static bool ad_read_sector(ad_view *v, uint32_t sector, uint8_t out[AD_SECTOR], xx_pd_struct *pd) {
    if (sector >= v->sectors || !ad_work(v, pd)) return false;
    return ad_read_abs(v, (uint64_t)ad_physical(v, sector) * AD_SECTOR, out, AD_SECTOR, pd);
}
static uint8_t ad_checksum(const uint8_t data[AD_SECTOR]) {
    unsigned i, acc = 0U;
    for (i = AD_SECTOR - 1U; i-- > 0U;) {
        if (acc > 255U) acc = (acc + 1U) & 255U;
        acc += data[i];
    }
    return (uint8_t)(acc & 255U);
}
static bool ad_claim(ad_view *v, uint32_t first, uint32_t count, xx_pd_struct *pd) {
    uint32_t i;
    if (first > v->sectors || count > v->sectors - first || (count && first < 2U)) return false;
    for (i = 0U; i < count; ++i)
        if (!ad_work(v, pd) || ad_bit(v->free_map, first + i) || ad_bit(v->claimed, first + i)) return false;
    for (i = 0U; i < count; ++i) ad_set(v->claimed, first + i);
    return true;
}
static bool ad_equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}
static int ad_compare(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return x < y ? -1 : 1;
    }
    return *a == *b ? 0 : (*a ? 1 : -1);
}
static uint32_t ad_hash(const char *text) {
    uint32_t hash = UINT32_C(2166136261);
    while (*text) { unsigned char c = (unsigned char)*text++; if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; hash ^= c; hash *= UINT32_C(16777619); }
    return hash;
}
static bool ad_name(const uint8_t *raw, size_t limit, char *out) {
    size_t i, n = 0U; bool ended = false;
    for (i = 0U; i < limit; ++i) {
        uint8_t c = raw[i] & 127U;
        if (c == 0U || c == 13U) { ended = true; continue; }
        if (ended || c < 32U || c > 126U) return false;
        out[n++] = (char)c;
    }
    out[n] = 0; return n != 0U;
}
static bool ad_device_name(const char *name) {
    static const char *const devices[] = {"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"};
    char stem[16]; size_t i = 0U, j;
    while (name[i] && name[i] != '.' && i + 1U < sizeof(stem)) { stem[i] = name[i]; ++i; }
    stem[i] = 0;
    if (name[i] && name[i] != '.') return false;
    for (j = 0U; j < sizeof(devices) / sizeof(devices[0]); ++j) if (ad_equal(stem, devices[j])) return true;
    return i == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' || stem[0] == 'c') && (stem[1] == 'O' || stem[1] == 'o') && (stem[2] == 'M' || stem[2] == 'm') ||
         (stem[0] == 'L' || stem[0] == 'l') && (stem[1] == 'P' || stem[1] == 'p') && (stem[2] == 'T' || stem[2] == 't'));
}
static void ad_safe_name(const char *raw, char out[32]) {
    size_t i, n = 0U;
    for (i = 0U; raw[i] && n < 20U; ++i) {
        char c = raw[i];
        if (c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') c = '_';
        out[n++] = c;
    }
    while (n && (out[n - 1U] == '.' || out[n - 1U] == ' ')) out[n - 1U] = '_';
    out[n] = 0;
    if ((n == 1U && out[0] == '.') || (n == 2U && out[0] == '.' && out[1] == '.') || ad_device_name(out)) {
        for (i = n + 1U; i; --i) out[i] = out[i - 1U]; out[0] = '_';
    }
}
static bool ad_append(ad_view *v, const char *parent, const char *component, uint32_t first,
    uint32_t size, uint32_t attributes, uint64_t header, bool directory, char **path_out, xx_pd_struct *pd) {
    unsigned attempt;
    for (attempt = 0U; attempt < AD_MAX_MEMBERS; ++attempt) {
        char leaf[48], *path; size_t slot, bytes; bool taken = false;
        if (!ad_work(v, pd)) return false;
        if (attempt) xx_rt_snprintf(leaf, sizeof(leaf), "%s~%u", component, attempt + 1U);
        else xx_rt_snprintf(leaf, sizeof(leaf), "%s", component);
        path = *parent ? xx_str_concat3(parent, "/", leaf) : xx_str_dup(leaf);
        if (!path) return false;
        bytes = xx_str_len(path) + 1U;
        if (bytes > AD_MAX_PATH || bytes > AD_MAX_PATH_BYTES - v->path_bytes) { xx_str_free(path); return false; }
        slot = ad_hash(path) & (v->name_capacity - 1U);
        while (v->names[slot]) {
            if (!ad_work(v, pd)) { xx_str_free(path); return false; }
            if (ad_equal(path, v->members[v->names[slot] - 1U].name)) { taken = true; break; }
            slot = (slot + 1U) & (v->name_capacity - 1U);
        }
        if (!taken) {
            ad_member *member;
            if (v->count >= v->capacity) { xx_str_free(path); return false; }
            member = v->members + v->count; member->name = path; member->first = first;
            member->size = size; member->attributes = attributes; member->header = header; member->directory = directory;
            v->path_bytes += bytes; v->names[slot] = (uint32_t)++v->count;
            if (path_out) *path_out = path;
            return true;
        }
        xx_str_free(path);
    }
    return false;
}
static bool ad_scan_dir(ad_view *v, uint32_t sector, uint32_t parent_sector,
    const char *parent_path, const char *name, unsigned depth, xx_pd_struct *pd) {
    uint8_t raw[AD_DIR_SIZE]; char actual[16], previous[16] = "";
    uint32_t i, entries = 0U; bool found_end = false;
    if (depth > AD_MAX_DEPTH || !ad_claim(v, sector, 5U, pd)) return false;
    for (i = 0U; i < 5U; ++i) if (!ad_read_sector(v, sector + i, raw + i * AD_SECTOR, pd)) return false;
    if ((xx_mem_compare(raw + 1U, "Hugo", 4U) && xx_mem_compare(raw + 1U, "Nick", 4U)) ||
        xx_mem_compare(raw + 1U, raw + 0x4FBU, 4U) || raw[0] != raw[0x4FAU] ||
        raw[0x4CBU] != 0U || raw[0x4FFU] != 0U || ad_u24(raw + 0x4D6U) != parent_sector ||
        !ad_name(raw + 0x4CCU, 10U, actual) || !ad_equal(actual, name)) return false;
    for (i = 0x4ECU; i < 0x4FAU; ++i) if (raw[i]) return false;
    for (i = 0U; i < 47U; ++i) {
        const uint8_t *entry = raw + 5U + i * 26U;
        char entry_name[16], component[32]; uint32_t attributes = 0U, first, size, count;
        bool directory; size_t j; uint64_t header; char *child = NULL;
        if (!(entry[0] & 127U)) { found_end = true; break; }
        if (!ad_name(entry, 10U, entry_name)) return false;
        if (previous[0] && ad_compare(previous, entry_name) >= 0) return false;
        xx_rt_snprintf(previous, sizeof(previous), "%s", entry_name);
        for (j = 0U; j < 10U; ++j) if (entry[j] & 128U) attributes |= UINT32_C(1) << j;
        directory = (attributes & 8U) != 0U; first = ad_u24(entry + 22U); size = ad_u32(entry + 18U);
        if (directory) { if (size != AD_DIR_SIZE || first < 7U || first > v->sectors - 5U) return false; }
        else {
            if (size > v->bytes) return false;
            count = size / AD_SECTOR + (size % AD_SECTOR != 0U ? 1U : 0U);
            if (size && (!first || first < 7U || first > v->sectors || count > v->sectors - first || !ad_claim(v, first, count, pd))) return false;
            if (!size && first) return false;
        }
        ad_safe_name(entry_name, component);
        header = (uint64_t)ad_physical(v, sector + (5U + i * 26U) / AD_SECTOR) * AD_SECTOR + (5U + i * 26U) % AD_SECTOR;
        if (!ad_append(v, parent_path, component, first, directory ? 0U : size, attributes, header, directory, &child, pd)) return false;
        ++entries;
        if (directory && !ad_scan_dir(v, first, sector, child, entry_name, depth + 1U, pd)) return false;
    }
    if (!found_end && (raw[0x4CBU] & 127U)) return false;
    return entries <= 47U && !ad_stopped(pd);
}
static void ad_view_free(void *ptr) {
    ad_view *v = (ad_view *)ptr; size_t i;
    if (!v) return;
    for (i = 0U; i < v->count; ++i) xx_str_free(v->members[i].name);
    xx_mem_free(v->members); xx_mem_free(v->names); xx_mem_free(v);
}
static ad_view *ad_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_acorn_adfs *volume = (xx_acorn_adfs *)self; ad_view *v; uint8_t map0[AD_SECTOR], map1[AD_SECTOR];
    int64_t total; uint32_t sectors, prev = 7U, i, count; char volume_name[11];
    if (!self || !self->device || self->base_address < 0 || ad_stopped(pd) ||
        (total = xx_io_total_size(self->device)) < self->base_address ||
        total - self->base_address < (int64_t)(7U * AD_SECTOR)) return NULL;
    v = (ad_view *)xx_mem_alloc(sizeof(*v)); if (!v) return NULL; xx_mem_zero(v, sizeof(*v));
    v->device = self->device; v->base = self->base_address; v->bytes = (uint64_t)(total - v->base);
    v->sectors = 2U;
    if (!ad_read_sector(v, 0U, map0, pd) || !ad_read_sector(v, 1U, map1, pd) ||
        ad_checksum(map0) != map0[255] || ad_checksum(map1) != map1[255] || map0[246] ||
        (map1[254] % 3U) || map1[254] > 246U) goto fail;
    sectors = ad_u24(map0 + 252U);
    if (sectors != XX_ACORN_ADFS_S && sectors != XX_ACORN_ADFS_M && sectors != XX_ACORN_ADFS_L) goto fail;
    if (volume->variant != XX_ACORN_ADFS_AUTO && volume->variant != (xx_acorn_adfs_variant)sectors) goto fail;
    if ((uint64_t)sectors * AD_SECTOR > v->bytes) goto fail;
    v->bytes = (uint64_t)sectors * AD_SECTOR; v->sectors = sectors; v->variant = (xx_acorn_adfs_variant)sectors;
    v->order = sectors == XX_ACORN_ADFS_L ? volume->order : XX_ACORN_ADFS_ORDER_LINEAR;
    if (sectors == XX_ACORN_ADFS_L && v->order != XX_ACORN_ADFS_ORDER_LINEAR && v->order != XX_ACORN_ADFS_ORDER_L_TRACK_INTERLEAVED) goto fail;
    count = map1[254] / 3U;
    for (i = 0U; i < count; ++i) {
        uint32_t start = ad_u24(map0 + i * 3U), length = ad_u24(map1 + i * 3U), j;
        if (!length || start < prev || start > sectors || length > sectors - start) goto fail;
        for (j = 0U; j < length; ++j) { if (!ad_work(v, pd)) goto fail; ad_set(v->free_map, start + j); }
        prev = start + length;
    }
    for (i = 0U; i < 5U; ++i) { volume_name[i * 2U] = (char)(map0[247U + i] & 127U); volume_name[i * 2U + 1U] = (char)(map1[246U + i] & 127U); }
    volume_name[10] = 0;
    for (i = 0U; i < 10U; ++i) if (!volume_name[i] || volume_name[i] == 13) { volume_name[i] = 0; break; }
    xx_rt_snprintf(v->volume_name, sizeof(v->volume_name), "%s", volume_name);
    v->capacity = (size_t)sectors * 2U; if (v->capacity > AD_MAX_MEMBERS) v->capacity = AD_MAX_MEMBERS;
    v->members = (ad_member *)xx_mem_alloc(v->capacity * sizeof(*v->members));
    v->name_capacity = 8U; while (v->name_capacity < v->capacity * 2U) v->name_capacity *= 2U;
    v->names = (uint32_t *)xx_mem_alloc(v->name_capacity * sizeof(*v->names));
    if (!v->members || !v->names) goto fail;
    xx_mem_zero(v->members, v->capacity * sizeof(*v->members)); xx_mem_zero(v->names, v->name_capacity * sizeof(*v->names));
    ad_set(v->claimed, 0U); ad_set(v->claimed, 1U);
    if (!ad_scan_dir(v, 2U, 2U, "", "$", 0U, pd)) goto fail;
    v->retained_memory = sizeof(*v) + v->capacity * sizeof(*v->members) + v->name_capacity * sizeof(*v->names) + v->path_bytes;
    return v;
fail:
    ad_view_free(v); return NULL;
}
static void ad_vtable_destroy(Abstractformat *self) { xx_acorn_adfs_destroy((xx_acorn_adfs *)self); }
void xx_acorn_adfs_init_ex(xx_acorn_adfs *v, xx_io_device *device, int64_t base,
    xx_acorn_adfs_variant variant, xx_acorn_adfs_order order) {
    if (!v) return; xx_mem_zero(v, sizeof(*v)); xx_format_init(&v->format, device, base);
    v->variant = variant; v->order = order; v->format.endian = XX_ENDIAN_LITTLE; v->format.file_type = AD_TYPE;
    v->format.format_type = XX_TYPE_ARCHIVE; v->format.is_archive = true;
    xx_format_set_mime_type(&v->format, "application/x-acorn-adfs"); xx_format_set_extension(&v->format, "adf");
    v->format.check_is_valid = xx_acorn_adfs_check_is_valid; v->format.handle_base_info = xx_acorn_adfs_handle_base_info;
    v->format.get_format_size = xx_acorn_adfs_get_format_size; v->format.get_number_of_archive_records = xx_acorn_adfs_get_number_of_archive_records;
    v->format.create_archive_records_reading = xx_acorn_adfs_create_archive_records_reading;
    v->format.get_current_archive_record = xx_acorn_adfs_get_current_archive_record;
    v->format.archive_record_move_to_next = xx_acorn_adfs_archive_record_move_to_next;
    v->format.unpack_current_archive_record = xx_acorn_adfs_unpack_current_archive_record;
    v->format.free_archive_records_reading = xx_acorn_adfs_free_archive_records_reading;
    v->format.destroy = ad_vtable_destroy;
}
void xx_acorn_adfs_init(xx_acorn_adfs *v, xx_io_device *device, int64_t base) {
    xx_acorn_adfs_init_ex(v, device, base, XX_ACORN_ADFS_AUTO, XX_ACORN_ADFS_ORDER_L_TRACK_INTERLEAVED);
}
xx_acorn_adfs *xx_acorn_adfs_create(xx_io_device *device, int64_t base) {
    xx_acorn_adfs *v = (xx_acorn_adfs *)xx_mem_alloc(sizeof(*v)); if (v) xx_acorn_adfs_init(v, device, base); return v;
}
void xx_acorn_adfs_destroy(xx_acorn_adfs *v) { if (v) xx_format_cleanup_extra_parameters(&v->format); }
void xx_acorn_adfs_free(xx_acorn_adfs *v) { if (v) { xx_acorn_adfs_destroy(v); xx_mem_free(v); } }
bool xx_acorn_adfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) { ad_view *v = ad_parse(self, pd); bool valid = v != NULL; ad_view_free(v); return valid; }
bool xx_acorn_adfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_acorn_adfs *volume = (xx_acorn_adfs *)self; ad_view *v = ad_parse(self, pd); int64_t total, end;
    if (!self) return false;
    if (!v) { self->is_valid = false; self->base_info_handled = false; return false; }
    volume->number_of_records = v->count; volume->volume_size = v->bytes; volume->logical_sectors = v->sectors;
    volume->variant = v->variant; volume->order = v->order;
    xx_rt_snprintf(volume->volume_name, sizeof(volume->volume_name), "%s", v->volume_name);
    self->format_size = (int64_t)v->bytes; self->number_of_archive_records = v->count;
    total = xx_io_total_size(self->device); end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true; ad_view_free(v); return true;
}
int64_t xx_acorn_adfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) { return xx_acorn_adfs_handle_base_info(self, pd) ? self->format_size : -1; }
uint64_t xx_acorn_adfs_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) { return xx_acorn_adfs_handle_base_info(self, pd) ? ((xx_acorn_adfs *)self)->number_of_records : 0U; }
static bool ad_record(xx_archive_record *record, const ad_view *v, const ad_member *member) {
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)member->header; record->header_size = 26U;
    record->data_offset = member->size ? v->base + (int64_t)((uint64_t)ad_physical(v, member->first) * AD_SECTOR) : -1;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->attributes) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->directory);
}
static bool ad_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i; if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i); xx_meta copy;
        if (!item) continue; xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
xx_archive_record_state *xx_acorn_adfs_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    ad_view *v = ad_parse(self, pd); xx_archive_record_state *state;
    if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state)); if (!state) { ad_view_free(v); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = v; state->free_internal = ad_view_free; state->total_records = (int64_t)v->count;
    if (!ad_options(&state->options, options) || (v->count && !ad_record(&state->current_record, v, v->members))) { xx_archive_record_state_free(state); return NULL; }
    state->has_record = v->count != 0U; state->current_index = v->count ? 0 : -1; return state;
}
const xx_archive_record *xx_acorn_adfs_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_acorn_adfs_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    ad_view *v;
    if (!self || !state || state->format != self || !state->has_record || !(v = (ad_view *)state->internal_state) || ad_stopped(pd)) return false;
    if (v->index + 1U >= v->count) {
        v->index = v->count; state->has_record = false; state->current_index = -1;
        xx_archive_record_cleanup(&state->current_record); xx_archive_record_init(&state->current_record); return false;
    }
    if (!ad_record(&state->current_record, v, v->members + v->index + 1U)) { state->has_record = false; return false; }
    ++v->index; ++state->current_index; return true;
}
static uint64_t ad_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback) {
    const xx_var *value = xx_format_resolve_extra_parameter(self, options, id);
    if (!value) return fallback;
    switch (value->type) {
    case XX_VAR_TYPE_UINT8: case XX_VAR_TYPE_UINT16: case XX_VAR_TYPE_UINT32: case XX_VAR_TYPE_UINT64: return xx_var_get_u64(value);
    case XX_VAR_TYPE_INT8: case XX_VAR_TYPE_INT16: case XX_VAR_TYPE_INT32: case XX_VAR_TYPE_INT64: {
        int64_t n = xx_var_get_i64(value); return n < 0 ? fallback : (uint64_t)n;
    }
    default: return fallback;
    }
}
static bool ad_limits(Abstractformat *self, xx_archive_record_state *state, const ad_view *v, const ad_member *member, size_t *buffer_size) {
    *buffer_size = member->directory ? 0U : member->size < AD_COPY ? member->size : AD_COPY;
    return member->size <= ad_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX) &&
        v->retained_memory + sizeof(*state) + *buffer_size <= ad_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
bool xx_acorn_adfs_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    ad_view *v; const ad_member *member; uint8_t *buffer; uint32_t done = 0U; size_t buffer_size; bool ok = true;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record ||
        !(v = (ad_view *)state->internal_state) || v->index >= v->count || ad_stopped(pd)) return false;
    member = v->members + v->index;
    if (!ad_limits(self, state, v, member, &buffer_size)) return false;
    if (member->directory || !buffer_size) return true;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size); if (!buffer) return false;
    while (done < member->size) {
        uint32_t sector = member->first + done / AD_SECTOR;
        size_t within = done % AD_SECTOR, part = AD_SECTOR - within, written = 0U;
        if (part > buffer_size) part = buffer_size;
        if (part > member->size - done) part = member->size - done;
        if (!ad_work(v, pd) || !ad_read_abs(v, (uint64_t)ad_physical(v, sector) * AD_SECTOR + within, buffer, part, pd)) { ok = false; break; }
        while (destination && written < part && !ad_stopped(pd)) {
            ssize_t got = xx_io_write(destination, buffer + written, part - written);
            if (got <= 0 || (size_t)got > part - written) { ok = false; break; }
            written += (size_t)got;
        }
        if (!ok || ad_stopped(pd)) { ok = false; break; }
        done += (uint32_t)part;
    }
    xx_mem_free(buffer); return ok && !ad_stopped(pd);
}
static xx_io_device *ad_stage(const char *destination, char **stage_path) {
    unsigned attempt; size_t i, parent = 0U; char *directory = xx_str_dup(destination);
    *stage_path = NULL; if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i) if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate; xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_adfs.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix); if (!candidate) break;
        if (ad_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; }
        xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_acorn_adfs_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    ad_view *v; const xx_var *option, *overwrite_option; const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage_path = NULL; size_t buffer_size; bool overwrite, ok = false;
    if (!self || !state || state->format != self || !state->has_record || !(v = (ad_view *)state->internal_state) || v->index >= v->count || ad_stopped(pd)) return false;
    if (!ad_limits(self, state, v, v->members + v->index, &buffer_size)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return xx_acorn_adfs_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) { owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base = owned; }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\')
        ? xx_str_concat3(base, "/", v->members[v->index].name) : xx_str_concat(base, v->members[v->index].name);
    if (!path) goto done;
    if (v->members[v->index].directory) { ok = !ad_stopped(pd) && xx_store_create_dirs_a(path, true); goto done; }
    if ((!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = ad_stage(path, &stage_path); if (!output) goto done;
        ok = xx_acorn_adfs_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output)) ok = false;
    }
    if (ad_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(stage_path, path, overwrite);
done:
    if (!ok && stage_path) xx_io_file_remove_a(stage_path);
    xx_str_free(stage_path); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_acorn_adfs_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) { (void)self; xx_archive_record_state_free(state); }
