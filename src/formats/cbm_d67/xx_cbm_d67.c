/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native Commodore 2040 DOS 1 reader from published sector structures.
 */
#include "xxfclib/formats/cbm_d67/xx_cbm_d67.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include <stdio.h>
#ifdef CBM_D67
#define CB_TYPE XX_FILE_TYPE_CBM_D67
#else
#define CB_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define CB_SECTOR 256U
#define CB_SECTORS 690U
#define CB_MAX_FILES 152U
#define CB_MAX_WORK 200000U
#define CB_COPY 256U
typedef struct cb_member_s {
    char *name;
    uint64_t header;
    uint32_t size;
    uint16_t first_chain, blocks, first_sector;
    uint8_t type, locked;
} cb_member;
typedef struct cb_view_s {
    xx_io_device *device;
    int64_t base;
    uint64_t bytes, path_bytes, retained_memory;
    uint32_t work, sectors, tracks;
    uint8_t free_map[(CB_SECTORS + 7U) / 8U], claimed[(CB_SECTORS + 7U) / 8U];
    uint16_t chain[CB_SECTORS];
    size_t chain_count, count, index;
    cb_member members[CB_MAX_FILES];
    char disk_name[64], disk_id[8];
} cb_view;
static bool cb_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool cb_work(cb_view *v, xx_pd_struct *pd) { return !cb_stopped(pd) && ++v->work <= CB_MAX_WORK; }
static bool cb_bit(const uint8_t *map, uint16_t sector) { return (map[sector / 8U] & (uint8_t)(1U << (sector % 8U))) != 0U; }
static void cb_set(uint8_t *map, uint16_t sector) { map[sector / 8U] |= (uint8_t)(1U << (sector % 8U)); }
static unsigned cb_spt(unsigned track) {
    return track <= 17U ? 21U : track <= 24U ? 20U : track <= 30U ? 18U : 17U;
}
static bool cb_index(unsigned track, unsigned sector, uint16_t *index) {
    unsigned t, value = 0U;
    if (track < 1U || track > 35U || sector >= cb_spt(track)) return false;
    for (t = 1U; t < track; ++t) value += cb_spt(t);
    value += sector;
    if (value >= CB_SECTORS) return false;
    *index = (uint16_t)value; return true;
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
static bool cb_read_sector(cb_view *v, uint16_t index, uint8_t data[CB_SECTOR], xx_pd_struct *pd) {
    return index < v->sectors && cb_work(v, pd) && cb_read_abs(v, (uint64_t)index * CB_SECTOR, data, CB_SECTOR, pd);
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
static bool cb_device_name(const char *name) {
    static const char *const devices[] = {"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"};
    char stem[32]; size_t i = 0U, j;
    while (name[i] && name[i] != '.' && i + 1U < sizeof(stem)) { stem[i] = name[i]; ++i; }
    stem[i] = 0;
    if (name[i] && name[i] != '.') return false;
    for (j = 0U; j < sizeof(devices) / sizeof(devices[0]); ++j) if (cb_equal(stem, devices[j])) return true;
    return i == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (((stem[0] == 'C' || stem[0] == 'c') && (stem[1] == 'O' || stem[1] == 'o') && (stem[2] == 'M' || stem[2] == 'm')) ||
         ((stem[0] == 'L' || stem[0] == 'l') && (stem[1] == 'P' || stem[1] == 'p') && (stem[2] == 'T' || stem[2] == 't')));
}
static bool cb_name(const uint8_t raw[16], char out[80]) {
    unsigned i; size_t used = 0U; bool padded = false;
    for (i = 0U; i < 16U; ++i) {
        unsigned char c = raw[i]; char decoded;
        if (c == 0xA0U) { padded = true; continue; }
        if (padded || c == 0U) return false;
        if (c >= 0xC1U && c <= 0xDAU) c = (unsigned char)(c - 0xC1U + 'A');
        else if (c >= 0x41U && c <= 0x5AU) c = (unsigned char)(c - 0x41U + 'A');
        decoded = (char)c;
        if (c < 0x20U || c > 0x7EU) {
            static const char digits[] = "0123456789ABCDEF";
            out[used++] = '~'; out[used++] = digits[c >> 4U]; out[used++] = digits[c & 15U];
        } else {
            if (decoded == '/' || decoded == '\\' || decoded == ':' || decoded == '<' || decoded == '>' ||
                decoded == '"' || decoded == '|' || decoded == '?' || decoded == '*') decoded = '_';
            out[used++] = decoded;
        }
    }
    while (used && (out[used - 1U] == ' ' || out[used - 1U] == '.')) out[used - 1U] = '_';
    out[used] = 0;
    if (!used) return false;
    if ((used == 1U && out[0] == '.') || (used == 2U && out[0] == '.' && out[1] == '.') || cb_device_name(out)) {
        size_t j;
        for (j = used + 1U; j; --j) out[j] = out[j - 1U];
        out[0] = '_';
    }
    return true;
}
static bool cb_append(cb_view *v, const char *stem, const char *extension, uint64_t header,
    uint8_t type, uint8_t locked, uint16_t first, uint16_t blocks, uint32_t size, uint16_t chain_start, xx_pd_struct *pd) {
    unsigned suffix;
    if (v->count >= CB_MAX_FILES) return false;
    for (suffix = 0U; suffix < CB_MAX_FILES; ++suffix) {
        char leaf[96]; size_t i, bytes; char *name; bool taken = false;
        if (!cb_work(v, pd)) return false;
        if (suffix) xx_rt_snprintf(leaf, sizeof(leaf), "%s~%u.%s", stem, suffix + 1U, extension);
        else xx_rt_snprintf(leaf, sizeof(leaf), "%s.%s", stem, extension);
        for (i = 0U; i < v->count; ++i) if (cb_equal(leaf, v->members[i].name)) { taken = true; break; }
        if (taken) continue;
        bytes = xx_str_len(leaf) + 1U;
        if (bytes > UINT64_MAX - v->path_bytes || v->path_bytes + bytes > 16384U) return false;
        name = xx_str_dup(leaf); if (!name) return false;
        v->members[v->count].name = name; v->members[v->count].header = header;
        v->members[v->count].type = type; v->members[v->count].locked = locked;
        v->members[v->count].first_sector = first; v->members[v->count].blocks = blocks;
        v->members[v->count].size = size; v->members[v->count].first_chain = chain_start;
        ++v->count; v->path_bytes += bytes; return true;
    }
    return false;
}
static bool cb_claim(cb_view *v, uint16_t index) {
    if (index >= v->sectors || cb_bit(v->free_map, index) || cb_bit(v->claimed, index)) return false;
    cb_set(v->claimed, index); return true;
}
static bool cb_file(cb_view *v, const uint8_t *entry, uint64_t header, xx_pd_struct *pd) {
    uint8_t kind = entry[0] & 7U, track = entry[1], sector = entry[2];
    uint16_t declared = (uint16_t)((unsigned)entry[28] | ((unsigned)entry[29] << 8U));
    uint16_t start = (uint16_t)v->chain_count, first = 0U; uint32_t size = 0U;
    const char *extension = kind == 1U ? "seq" : kind == 2U ? "prg" : "usr";
    char stem[80];
    if (kind < 1U || kind > 3U || !(entry[0] & 0x80U) || (entry[0] & 0x38U) || !cb_name(entry + 3U, stem)) return false;
    if (!track) {
        if (sector || declared) return false;
    } else {
        while (track) {
            uint16_t index; uint8_t data[CB_SECTOR];
            if (!cb_work(v, pd) || !cb_index(track, sector, &index) || !cb_claim(v, index) ||
                v->chain_count >= v->sectors || !cb_read_sector(v, index, data, pd)) return false;
            if (v->chain_count == start) first = index;
            v->chain[v->chain_count++] = index;
            if (!data[0]) {
                if (data[1] < 1U) return false;
                size = (uint32_t)(v->chain_count - start - 1U) * 254U + (uint32_t)data[1] - 1U;
                break;
            }
            track = data[0]; sector = data[1];
        }
        if (declared != v->chain_count - start) return false;
    }
    return cb_append(v, stem, extension, header, kind, (uint8_t)((entry[0] & 0x40U) != 0U), first,
        declared, size, start, pd);
}
static bool cb_directory(cb_view *v, unsigned track, unsigned sector, xx_pd_struct *pd) {
    unsigned visited = 0U;
    while (track) {
        uint16_t index; uint8_t data[CB_SECTOR]; unsigned slot;
        if (++visited > 19U || track != 18U || sector == 0U || !cb_index(track, sector, &index) ||
            !cb_claim(v, index) || !cb_read_sector(v, index, data, pd)) return false;
        for (slot = 0U; slot < 8U; ++slot) {
            const uint8_t *entry = data + slot * 32U + 2U;
            if ((entry[0] & 7U) == 0U) continue;
            if (!cb_file(v, entry, (uint64_t)index * CB_SECTOR + slot * 32U + 2U, pd)) return false;
        }
        track = data[0]; sector = data[1];
        if (!track && sector != 255U) return false;
    }
    return true;
}
static void cb_view_free(void *ptr) {
    cb_view *v = (cb_view *)ptr; size_t i;
    if (!v) return;
    for (i = 0U; i < v->count; ++i) xx_str_free(v->members[i].name);
    xx_mem_free(v);
}
static cb_view *cb_parse(Abstractformat *self, xx_pd_struct *pd) {
    cb_view *v; uint8_t bam[CB_SECTOR]; int64_t total; unsigned track;
    uint16_t bam_index;
    if (!self || !self->device || self->base_address < 0 || cb_stopped(pd) ||
        (total = xx_io_total_size(self->device)) < self->base_address) return NULL;
    v = (cb_view *)xx_mem_alloc(sizeof(*v)); if (!v) return NULL;
    xx_mem_zero(v, sizeof(*v)); v->device = self->device; v->base = self->base_address;
    v->tracks = 35U; v->sectors = CB_SECTORS;
    v->bytes = (uint64_t)v->sectors * CB_SECTOR;
    if ((uint64_t)(total - v->base) < v->bytes) goto fail;
    if (!cb_index(18U, 0U, &bam_index) || !cb_read_sector(v, bam_index, bam, pd) ||
        bam[0] != 18U || bam[1] != 1U || bam[2] != 1U || bam[3] != 0U) goto fail;
    for (track = 1U; track <= v->tracks; ++track) {
        unsigned j, free_count = 0U, sectors = cb_spt(track);
        uint8_t *entry = bam + 4U + (track - 1U) * 4U;
        for (j = 0U; j < 24U; ++j) {
            bool free = (entry[1U + j / 8U] & (1U << (j % 8U))) != 0U;
            uint16_t index;
            if (j >= sectors) { if (free) goto fail; continue; }
            if (!cb_work(v, pd) || !cb_index(track, j, &index)) goto fail;
            if (free) { cb_set(v->free_map, index); ++free_count; }
        }
        if (free_count != entry[0]) goto fail;
    }
    if (!cb_claim(v, bam_index) || !cb_directory(v, bam[0], bam[1], pd)) goto fail;
    {
        char name[80], id[80]; uint8_t raw_name[16], raw_id[16]; unsigned i;
        for (i = 0U; i < 16U; ++i) raw_name[i] = bam[0x90U + i];
        for (i = 0U; i < 16U; ++i) raw_id[i] = i < 2U ? bam[0xA2U + i] : 0xA0U;
        if (cb_name(raw_name, name)) xx_rt_snprintf(v->disk_name, sizeof(v->disk_name), "%s", name);
        if (cb_name(raw_id, id)) xx_rt_snprintf(v->disk_id, sizeof(v->disk_id), "%s", id);
    }
    v->retained_memory = sizeof(*v) + v->path_bytes;
    return v;
fail:
    cb_view_free(v); return NULL;
}
static void cb_vtable_destroy(Abstractformat *self) { xx_cbm_d67_destroy((xx_cbm_d67 *)self); }
void xx_cbm_d67_init(xx_cbm_d67 *v, xx_io_device *device, int64_t base) {
    if (!v) return;
    xx_mem_zero(v, sizeof(*v)); xx_format_init(&v->format, device, base);
    v->format.endian = XX_ENDIAN_LITTLE; v->format.file_type = CB_TYPE;
    v->format.format_type = XX_TYPE_ARCHIVE; v->format.is_archive = true;
    xx_format_set_mime_type(&v->format, "application/x-commodore-d67");
    xx_format_set_extension(&v->format, "d67");
    v->format.check_is_valid = xx_cbm_d67_check_is_valid;
    v->format.handle_base_info = xx_cbm_d67_handle_base_info;
    v->format.get_format_size = xx_cbm_d67_get_format_size;
    v->format.get_number_of_archive_records = xx_cbm_d67_get_number_of_archive_records;
    v->format.create_archive_records_reading = xx_cbm_d67_create_archive_records_reading;
    v->format.get_current_archive_record = xx_cbm_d67_get_current_archive_record;
    v->format.archive_record_move_to_next = xx_cbm_d67_archive_record_move_to_next;
    v->format.unpack_current_archive_record = xx_cbm_d67_unpack_current_archive_record;
    v->format.free_archive_records_reading = xx_cbm_d67_free_archive_records_reading;
    v->format.destroy = cb_vtable_destroy;
}
xx_cbm_d67 *xx_cbm_d67_create(xx_io_device *device, int64_t base) {
    xx_cbm_d67 *v = (xx_cbm_d67 *)xx_mem_alloc(sizeof(*v)); if (v) xx_cbm_d67_init(v, device, base); return v;
}
void xx_cbm_d67_destroy(xx_cbm_d67 *v) { if (v) xx_format_cleanup_extra_parameters(&v->format); }
void xx_cbm_d67_free(xx_cbm_d67 *v) { if (v) { xx_cbm_d67_destroy(v); xx_mem_free(v); } }
bool xx_cbm_d67_check_is_valid(Abstractformat *self, xx_pd_struct *pd) { cb_view *v = cb_parse(self, pd); bool ok = v != NULL; cb_view_free(v); return ok; }
bool xx_cbm_d67_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_cbm_d67 *disk = (xx_cbm_d67 *)self; cb_view *v = cb_parse(self, pd); int64_t total, end;
    if (!self) return false;
    if (!v) { self->is_valid = false; self->base_info_handled = false; return false; }
    disk->number_of_records = v->count;
    disk->track_count = v->tracks;
    xx_rt_snprintf(disk->disk_name, sizeof(disk->disk_name), "%s", v->disk_name);
    xx_rt_snprintf(disk->disk_id, sizeof(disk->disk_id), "%s", v->disk_id);
    self->format_size = (int64_t)v->bytes; self->number_of_archive_records = v->count;
    total = xx_io_total_size(self->device); end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true;
    cb_view_free(v); return true;
}
int64_t xx_cbm_d67_get_format_size(Abstractformat *self, xx_pd_struct *pd) { return xx_cbm_d67_handle_base_info(self, pd) ? self->format_size : -1; }
uint64_t xx_cbm_d67_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) { return xx_cbm_d67_handle_base_info(self, pd) ? ((xx_cbm_d67 *)self)->number_of_records : 0U; }
static bool cb_record(xx_archive_record *record, const cb_view *v, const cb_member *m) {
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)m->header; record->header_size = 30U;
    record->data_offset = m->blocks ? v->base + (int64_t)m->first_sector * CB_SECTOR + 2 : -1;
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
xx_archive_record_state *xx_cbm_d67_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    cb_view *v = cb_parse(self, pd); xx_archive_record_state *state;
    if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state)); if (!state) { cb_view_free(v); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = v; state->free_internal = cb_view_free; state->total_records = (int64_t)v->count;
    if (!cb_options(&state->options, options) || (v->count && !cb_record(&state->current_record, v, v->members))) { xx_archive_record_state_free(state); return NULL; }
    state->has_record = v->count != 0U; state->current_index = v->count ? 0 : -1; return state;
}
const xx_archive_record *xx_cbm_d67_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_cbm_d67_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
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
bool xx_cbm_d67_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    cb_view *v; const cb_member *m; uint8_t *buffer; uint32_t done = 0U; size_t buffer_size; bool ok = true; unsigned block;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record ||
        !(v = (cb_view *)state->internal_state) || v->index >= v->count || cb_stopped(pd)) return false;
    m = v->members + v->index;
    if (!cb_limits(self, state, v, m, &buffer_size)) return false;
    if (!buffer_size) return true;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size); if (!buffer) return false;
    for (block = 0U; block < m->blocks && done < m->size; ++block) {
        size_t part = m->size - done, written = 0U;
        uint16_t sector = v->chain[(size_t)m->first_chain + block];
        if (part > 254U) part = 254U;
        if (part > buffer_size) part = buffer_size;
        if (!cb_work(v, pd) || !cb_read_abs(v, (uint64_t)sector * CB_SECTOR + 2U, buffer, part, pd)) { ok = false; break; }
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
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_cbm_d67.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix); if (!candidate) break;
        if (cb_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; }
        xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_cbm_d67_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    cb_view *v; const xx_var *option, *overwrite_option; const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage_path = NULL; size_t buffer_size; bool overwrite, ok = false;
    if (!self || !state || state->format != self || !state->has_record || !(v = (cb_view *)state->internal_state) || v->index >= v->count || cb_stopped(pd)) return false;
    if (!cb_limits(self, state, v, v->members + v->index, &buffer_size)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return xx_cbm_d67_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) { owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base = owned; }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\')
        ? xx_str_concat3(base, "/", v->members[v->index].name) : xx_str_concat(base, v->members[v->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = cb_stage(path, &stage_path); if (!output) goto done;
        ok = xx_cbm_d67_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output)) ok = false;
    }
    if (cb_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(stage_path, path, overwrite);
done:
    if (!ok && stage_path) xx_io_file_remove_a(stage_path);
    xx_str_free(stage_path); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_cbm_d67_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) { (void)self; xx_archive_record_state_free(state); }
