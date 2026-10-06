/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native Acorn BBC Micro DFS reader from the original catalogue layout.
 */
#include "xxfclib/formats/acorn_dfs/xx_acorn_dfs.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include <string.h>
#include <stdio.h>
#ifdef ACORN_DFS
#define CB_TYPE XX_FILE_TYPE_ACORN_DFS
#else
#define CB_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define CB_SECTOR 256U
#define CB_SECTORS_SIDE 800U
#define CB_MAX_FILES 62U
#define CB_MAX_WORK 100000U
#define CB_COPY 256U
typedef struct cb_member_s {
    char *name;
    uint64_t header;
    uint32_t size, start, load_address, exec_address;
    uint8_t side, locked, raw_name[8];
} cb_member;
typedef struct cb_view_s {
    xx_io_device *device;
    int64_t base;
    uint64_t bytes, path_bytes, retained_memory;
    uint32_t work, sectors_per_side, tracks, sides;
    xx_acorn_dfs_variant variant;
    uint8_t claimed[2][(CB_SECTORS_SIDE + 7U) / 8U];
    size_t count, index;
    cb_member members[CB_MAX_FILES];
    char disk_name[2][13];
} cb_view;
static bool cb_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool cb_work(cb_view *v, xx_pd_struct *pd) { return !cb_stopped(pd) && ++v->work <= CB_MAX_WORK; }
static bool cb_bit(const uint8_t *map, uint16_t sector) { return (map[sector / 8U] & (uint8_t)(1U << (sector % 8U))) != 0U; }
static void cb_set(uint8_t *map, uint16_t sector) { map[sector / 8U] |= (uint8_t)(1U << (sector % 8U)); }
static uint64_t cb_sector_offset(const cb_view *v, unsigned side, unsigned sector) {
    unsigned track = sector / 10U, sector_in_track = sector % 10U;
    unsigned physical = v->sides == 1U ? sector : (track * 2U + side) * 10U + sector_in_track;
    return (uint64_t)physical * CB_SECTOR;
}
static bool cb_read_abs(cb_view *v, uint64_t offset, void *output, size_t size, xx_pd_struct *pd) {
    int64_t cursor; size_t done = 0U; bool ok = false;
    if (!v || offset > v->bytes || size > v->bytes - offset ||
        offset > (uint64_t)(INT64_MAX - v->base) || cb_stopped(pd)) return false;
    cursor = xx_io_tell(v->device); if (cursor < 0) return false;
    if (!xx_io_seek64(v->device, v->base + (int64_t)offset, SEEK_SET)) {
        while (done < size && !cb_stopped(pd)) {
            ssize_t got = xx_io_read(v->device, (uint8_t *)output + done, size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(v->device, cursor, SEEK_SET)) ok = false;
    return ok && !cb_stopped(pd);
}
static bool cb_read_sector(cb_view *v, unsigned side, unsigned sector, uint8_t data[CB_SECTOR], xx_pd_struct *pd) {
    return side < v->sides && sector < v->sectors_per_side && cb_work(v, pd) &&
        cb_read_abs(v, cb_sector_offset(v, side, sector), data, CB_SECTOR, pd);
}
static bool cb_equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}
static bool cb_safe_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '$' || c == '!';
}
static void cb_escape(unsigned char c, char *out, size_t *used) {
    static const char hex[] = "0123456789ABCDEF";
    if (cb_safe_char(c)) out[(*used)++] = (char)c;
    else { out[(*used)++] = '~'; out[(*used)++] = hex[c >> 4U]; out[(*used)++] = hex[c & 15U]; }
}
static bool cb_name(const uint8_t *raw, char out[80], uint8_t identity[8]) {
    size_t used = 0U; unsigned i; bool padded = false;
    unsigned char dir = (unsigned char)(raw[7] & 0x7FU);
    if (dir < 0x21U || dir > 0x7EU || raw[0] == ' ') return false;
    for (i = 0U; i < 7U; ++i) {
        unsigned char c = raw[i];
        if (c == ' ') { padded = true; continue; }
        if (padded || c < 0x21U || c > 0x7EU) return false;
    }
    memcpy(identity, raw, 7U); identity[7] = dir;
    cb_escape(dir, out, &used); out[used++] = '.';
    for (i = 0U; i < 7U && raw[i] != ' '; ++i) cb_escape(raw[i], out, &used);
    out[used] = 0;
    return true;
}
static bool cb_claim(cb_view *v, unsigned side, unsigned sector) {
    if (side >= v->sides || sector >= v->sectors_per_side || cb_bit(v->claimed[side], (uint16_t)sector)) return false;
    cb_set(v->claimed[side], (uint16_t)sector); return true;
}
static bool cb_append(cb_view *v, unsigned side, const uint8_t raw[8], const char *stem,
    uint64_t header, uint32_t start, uint32_t size, uint32_t load, uint32_t exec, uint8_t locked,
    xx_pd_struct *pd) {
    unsigned suffix; size_t i;
    if (v->count >= CB_MAX_FILES) return false;
    for (i = 0U; i < v->count; ++i)
        if (v->members[i].side == side && !memcmp(v->members[i].raw_name, raw, 8U)) return false;
    for (suffix = 0U; suffix <= CB_MAX_FILES; ++suffix) {
        char leaf[96]; size_t bytes; char *name; bool taken = false;
        if (!cb_work(v, pd)) return false;
        if (v->sides == 2U) {
            if (suffix) xx_rt_snprintf(leaf, sizeof(leaf), "%u/%s~%u", side, stem, suffix + 1U);
            else xx_rt_snprintf(leaf, sizeof(leaf), "%u/%s", side, stem);
        } else {
            if (suffix) xx_rt_snprintf(leaf, sizeof(leaf), "%s~%u", stem, suffix + 1U);
            else xx_rt_snprintf(leaf, sizeof(leaf), "%s", stem);
        }
        for (i = 0U; i < v->count; ++i) if (cb_equal(leaf, v->members[i].name)) { taken = true; break; }
        if (taken) continue;
        bytes = xx_str_len(leaf) + 1U;
        if (v->path_bytes + bytes > 8192U) return false;
        name = xx_str_dup(leaf); if (!name) return false;
        v->members[v->count].name = name; v->members[v->count].header = header;
        v->members[v->count].size = size; v->members[v->count].start = start;
        v->members[v->count].load_address = load; v->members[v->count].exec_address = exec;
        v->members[v->count].side = (uint8_t)side; v->members[v->count].locked = locked;
        memcpy(v->members[v->count].raw_name, raw, 8U);
        ++v->count; v->path_bytes += bytes; return true;
    }
    return false;
}
static bool cb_catalogue(cb_view *v, unsigned side, xx_pd_struct *pd) {
    uint8_t names[CB_SECTOR], meta[CB_SECTOR]; unsigned sectors, entries, i;
    if (!cb_read_sector(v, side, 0U, names, pd) || !cb_read_sector(v, side, 1U, meta, pd)) return false;
    sectors = ((unsigned)(meta[6] & 3U) << 8U) | meta[7];
    entries = meta[5] >> 3U;
    if ((meta[5] & 7U) || entries > 31U || sectors != v->sectors_per_side ||
        (meta[6] & 0xCCU) || !cb_claim(v, side, 0U) || !cb_claim(v, side, 1U)) return false;
    for (i = 0U; i < 12U; ++i) {
        unsigned char c = i < 8U ? names[i] : meta[i - 8U];
        v->disk_name[side][i] = c >= 0x20U && c <= 0x7EU ? (char)c : ' ';
    }
    v->disk_name[side][12] = 0;
    for (i = 12U; i && v->disk_name[side][i - 1U] == ' '; --i) v->disk_name[side][i - 1U] = 0;
    for (i = 0U; i < entries; ++i) {
        const uint8_t *nr = names + (i + 1U) * 8U, *mr = meta + (i + 1U) * 8U;
        uint8_t raw[8]; char stem[80]; unsigned first, blocks, j;
        uint32_t size, load, exec; uint8_t high = mr[6];
        if (!cb_work(v, pd) || !cb_name(nr, stem, raw)) return false;
        size = (uint32_t)mr[4] | ((uint32_t)mr[5] << 8U) | ((uint32_t)((high >> 4U) & 3U) << 16U);
        load = (uint32_t)mr[0] | ((uint32_t)mr[1] << 8U) | ((uint32_t)((high >> 2U) & 3U) << 16U);
        exec = (uint32_t)mr[2] | ((uint32_t)mr[3] << 8U) | ((uint32_t)((high >> 6U) & 3U) << 16U);
        first = (unsigned)mr[7] | ((unsigned)(high & 3U) << 8U);
        blocks = (size + CB_SECTOR - 1U) / CB_SECTOR;
        if (first < 2U || first > sectors || blocks > sectors - first) return false;
        for (j = 0U; j < blocks; ++j) if (!cb_work(v, pd) || !cb_claim(v, side, first + j)) return false;
        if (!cb_append(v, side, raw, stem, cb_sector_offset(v, side, 1U) + (i + 1U) * 8U,
                       first, size, load, exec, (uint8_t)(nr[7] >> 7U), pd)) return false;
    }
    return true;
}
static void cb_view_free(void *ptr) {
    cb_view *v = (cb_view *)ptr; size_t i;
    if (!v) return;
    for (i = 0U; i < v->count; ++i) xx_str_free(v->members[i].name);
    xx_mem_free(v);
}
static cb_view *cb_parse_variant(Abstractformat *self, xx_acorn_dfs_variant variant, xx_pd_struct *pd) {
    cb_view *v; int64_t total; unsigned side;
    if (!self || !self->device || self->base_address < 0 || cb_stopped(pd) ||
        (total = xx_io_total_size(self->device)) < self->base_address ||
        variant < XX_ACORN_DFS_SSD40 || variant > XX_ACORN_DFS_DSD80) return NULL;
    v = (cb_view *)xx_mem_alloc(sizeof(*v)); if (!v) return NULL;
    xx_mem_zero(v, sizeof(*v)); v->device = self->device; v->base = self->base_address;
    v->variant = variant;
    v->tracks = variant == XX_ACORN_DFS_SSD40 || variant == XX_ACORN_DFS_DSD40 ? 40U : 80U;
    v->sides = variant == XX_ACORN_DFS_SSD40 || variant == XX_ACORN_DFS_SSD80 ? 1U : 2U;
    v->sectors_per_side = v->tracks * 10U;
    v->bytes = (uint64_t)v->sectors_per_side * v->sides * CB_SECTOR;
    if ((uint64_t)(total - v->base) < v->bytes) goto fail;
    for (side = 0U; side < v->sides; ++side) if (!cb_catalogue(v, side, pd)) goto fail;
    v->retained_memory = sizeof(*v) + v->path_bytes;
    return v;
fail:
    cb_view_free(v); return NULL;
}
static cb_view *cb_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_acorn_dfs *disk = (xx_acorn_dfs *)self; cb_view *chosen = NULL;
    int64_t total, remaining; unsigned pass, variant;
    if (!self || !self->device || self->base_address < 0 || cb_stopped(pd) ||
        (total = xx_io_total_size(self->device)) < self->base_address) return NULL;
    if (disk->variant != XX_ACORN_DFS_AUTO) return cb_parse_variant(self, disk->variant, pd);
    remaining = total - self->base_address;
    /* Prefer an exact image footprint. A nested image with an overlay must
     * resolve uniquely among all supported geometries. Empty images need an
     * explicit variant because their catalogues have no identifying magic. */
    for (pass = 0U; pass < 2U; ++pass) {
        for (variant = XX_ACORN_DFS_SSD40; variant <= XX_ACORN_DFS_DSD80; ++variant) {
            cb_view *candidate = cb_parse_variant(self, (xx_acorn_dfs_variant)variant, pd);
            if (!candidate) continue;
            if (!candidate->count || (pass == 0U ? candidate->bytes != (uint64_t)remaining :
                                                 candidate->bytes == (uint64_t)remaining)) {
                cb_view_free(candidate); continue;
            }
            if (chosen) {
                if (pass == 1U && candidate->bytes != chosen->bytes) {
                    if (candidate->bytes > chosen->bytes) { cb_view_free(chosen); chosen = candidate; }
                    else cb_view_free(candidate);
                    continue;
                }
                cb_view_free(candidate); cb_view_free(chosen); return NULL;
            }
            chosen = candidate;
        }
        if (chosen) return chosen;
    }
    return NULL;
}
static void cb_vtable_destroy(Abstractformat *self) { xx_acorn_dfs_destroy((xx_acorn_dfs *)self); }
void xx_acorn_dfs_init_ex(xx_acorn_dfs *v, xx_io_device *device, int64_t base, xx_acorn_dfs_variant variant) {
    if (!v) return;
    xx_mem_zero(v, sizeof(*v)); xx_format_init(&v->format, device, base);
    v->variant = variant;
    v->format.endian = XX_ENDIAN_LITTLE; v->format.file_type = CB_TYPE;
    v->format.format_type = XX_TYPE_ARCHIVE; v->format.is_archive = true;
    xx_format_set_mime_type(&v->format, "application/x-acorn-dfs");
    xx_format_set_extension(&v->format,
        variant == XX_ACORN_DFS_DSD40 || variant == XX_ACORN_DFS_DSD80 ? "dsd" : "ssd");
    v->format.check_is_valid = xx_acorn_dfs_check_is_valid;
    v->format.handle_base_info = xx_acorn_dfs_handle_base_info;
    v->format.get_format_size = xx_acorn_dfs_get_format_size;
    v->format.get_number_of_archive_records = xx_acorn_dfs_get_number_of_archive_records;
    v->format.create_archive_records_reading = xx_acorn_dfs_create_archive_records_reading;
    v->format.get_current_archive_record = xx_acorn_dfs_get_current_archive_record;
    v->format.archive_record_move_to_next = xx_acorn_dfs_archive_record_move_to_next;
    v->format.unpack_current_archive_record = xx_acorn_dfs_unpack_current_archive_record;
    v->format.free_archive_records_reading = xx_acorn_dfs_free_archive_records_reading;
    v->format.destroy = cb_vtable_destroy;
}
void xx_acorn_dfs_init(xx_acorn_dfs *v, xx_io_device *device, int64_t base) {
    xx_acorn_dfs_init_ex(v, device, base, XX_ACORN_DFS_AUTO);
}
xx_acorn_dfs *xx_acorn_dfs_create(xx_io_device *device, int64_t base) {
    xx_acorn_dfs *v = (xx_acorn_dfs *)xx_mem_alloc(sizeof(*v)); if (v) xx_acorn_dfs_init(v, device, base); return v;
}
void xx_acorn_dfs_destroy(xx_acorn_dfs *v) { if (v) xx_format_cleanup_extra_parameters(&v->format); }
void xx_acorn_dfs_free(xx_acorn_dfs *v) { if (v) { xx_acorn_dfs_destroy(v); xx_mem_free(v); } }
bool xx_acorn_dfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) { cb_view *v = cb_parse(self, pd); bool ok = v != NULL; cb_view_free(v); return ok; }
bool xx_acorn_dfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_acorn_dfs *disk = (xx_acorn_dfs *)self; cb_view *v = cb_parse(self, pd); int64_t total, end;
    if (!self) return false;
    if (!v) { self->is_valid = false; self->base_info_handled = false; return false; }
    disk->number_of_records = v->count;
    disk->variant = v->variant; disk->track_count = v->tracks; disk->side_count = v->sides;
    xx_format_set_extension(self, v->sides == 2U ? "dsd" : "ssd");
    xx_rt_snprintf(disk->disk_name[0], sizeof(disk->disk_name[0]), "%s", v->disk_name[0]);
    xx_rt_snprintf(disk->disk_name[1], sizeof(disk->disk_name[1]), "%s", v->disk_name[1]);
    self->format_size = (int64_t)v->bytes; self->number_of_archive_records = v->count;
    total = xx_io_total_size(self->device); end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true;
    cb_view_free(v); return true;
}
int64_t xx_acorn_dfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) { return xx_acorn_dfs_handle_base_info(self, pd) ? self->format_size : -1; }
uint64_t xx_acorn_dfs_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) { return xx_acorn_dfs_handle_base_info(self, pd) ? ((xx_acorn_dfs *)self)->number_of_records : 0U; }
static bool cb_record(xx_archive_record *record, const cb_view *v, const cb_member *m) {
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)m->header; record->header_size = 8U;
    record->data_offset = m->size ? v->base + (int64_t)cb_sector_offset(v, m->side, m->start) : -1;
    record->compressed_size = m->size;
    return xx_archive_record_set_original_name(record, m->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, m->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, m->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, m->locked);
}
static bool cb_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i; if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i); xx_meta copy;
        if (!item) { continue; } xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
xx_archive_record_state *xx_acorn_dfs_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    cb_view *v = cb_parse(self, pd); xx_archive_record_state *state;
    if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state)); if (!state) { cb_view_free(v); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = v; state->free_internal = cb_view_free; state->total_records = (int64_t)v->count;
    if (!cb_options(&state->options, options) || (v->count && !cb_record(&state->current_record, v, v->members))) { xx_archive_record_state_free(state); return NULL; }
    state->has_record = v->count != 0U; state->current_index = v->count ? 0 : -1; return state;
}
const xx_archive_record *xx_acorn_dfs_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_acorn_dfs_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    cb_view *v;
    if (!self || !state || state->format != self || !state->has_record || !(v = (cb_view *)state->internal_state) || cb_stopped(pd)) return false;
    if (v->index + 1U >= v->count) {
        v->index = v->count; state->has_record = false; state->current_index = -1;
        xx_archive_record_cleanup(&state->current_record); xx_archive_record_init(&state->current_record); return false;
    }
    if (!cb_record(&state->current_record, v, v->members + v->index + 1U)) { state->has_record = false; return false; }
    ++v->index; ++state->current_index; return true;
}
static uint64_t cb_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback) {
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
static bool cb_limits(Abstractformat *self, xx_archive_record_state *state, const cb_view *v, const cb_member *m, size_t *buffer_size) {
    *buffer_size = m->size < CB_COPY ? m->size : CB_COPY;
    return m->size <= cb_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX) &&
        v->retained_memory + sizeof(*state) + *buffer_size <= cb_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
bool xx_acorn_dfs_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    cb_view *v; const cb_member *m; uint8_t *buffer; uint32_t done = 0U; size_t buffer_size; bool ok = true;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record ||
        !(v = (cb_view *)state->internal_state) || v->index >= v->count || cb_stopped(pd)) return false;
    m = v->members + v->index;
    if (!cb_limits(self, state, v, m, &buffer_size)) return false;
    if (!buffer_size) return true;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size); if (!buffer) return false;
    while (done < m->size) {
        size_t part = m->size - done, written = 0U;
        unsigned sector = m->start + done / CB_SECTOR, within = done % CB_SECTOR;
        if (part > CB_SECTOR - within) part = CB_SECTOR - within;
        if (part > buffer_size) part = buffer_size;
        if (!cb_work(v, pd) || !cb_read_abs(v, cb_sector_offset(v, m->side, sector) + within, buffer, part, pd)) { ok = false; break; }
        while (destination && written < part && !cb_stopped(pd)) {
            ssize_t got = xx_io_write(destination, buffer + written, part - written);
            if (got <= 0 || (size_t)got > part - written) { ok = false; break; }
            written += (size_t)got;
        }
        if (!ok || cb_stopped(pd)) { ok = false; break; }
        done += (uint32_t)part;
    }
    xx_mem_free(buffer); return ok && done == m->size && !cb_stopped(pd);
}
static xx_io_device *cb_stage(const char *destination, char **stage_path) {
    unsigned attempt; size_t i, parent = 0U; char *directory = xx_str_dup(destination);
    *stage_path = NULL; if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i) if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate; xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_acorn_dfs.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix); if (!candidate) break;
        if (cb_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; }
        xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_acorn_dfs_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    cb_view *v; const xx_var *option, *overwrite_option; const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage_path = NULL; size_t buffer_size; bool overwrite, ok = false;
    if (!self || !state || state->format != self || !state->has_record || !(v = (cb_view *)state->internal_state) || v->index >= v->count || cb_stopped(pd)) return false;
    if (!cb_limits(self, state, v, v->members + v->index, &buffer_size)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return xx_acorn_dfs_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) { owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base = owned; }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\')
        ? xx_str_concat3(base, "/", v->members[v->index].name) : xx_str_concat(base, v->members[v->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = cb_stage(path, &stage_path); if (!output) goto done;
        ok = xx_acorn_dfs_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output)) ok = false;
    }
    if (cb_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(stage_path, path, overwrite);
done:
    if (!ok && stage_path) xx_io_file_remove_a(stage_path);
    xx_str_free(stage_path); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_acorn_dfs_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) { (void)self; xx_archive_record_state_free(state); }
