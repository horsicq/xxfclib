/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native Atari DOS 2.x reader from published ATR and DOS sector structures.
 */
#include "xxfclib/formats/atari_dos2/xx_atari_dos2.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include <string.h>
#include <stdio.h>
#ifdef ATARI_DOS2
#define CB_TYPE XX_FILE_TYPE_ATARI_DOS2
#else
#define CB_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define CB_MAX_SECTORS 1040U
#define CB_MAX_FILES 64U
#define CB_MAX_WORK 100000U
#define CB_COPY 256U
typedef struct cb_member_s {
    char *name;
    uint64_t header;
    uint32_t size;
    uint16_t first_chain, blocks, first_sector;
    uint8_t flags, raw_name[11];
} cb_member;
typedef struct cb_view_s {
    xx_io_device *device;
    int64_t base;
    uint64_t bytes, path_bytes, retained_memory;
    uint32_t work, sectors, sector_size, last_data;
    xx_atari_dos2_variant variant;
    uint8_t free_map[(CB_MAX_SECTORS + 1U + 7U) / 8U], claimed[(CB_MAX_SECTORS + 1U + 7U) / 8U];
    uint16_t chain[CB_MAX_SECTORS + 1U];
    size_t chain_count, count, index;
    cb_member members[CB_MAX_FILES];
} cb_view;
static bool cb_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool cb_work(cb_view *v, xx_pd_struct *pd) { return !cb_stopped(pd) && ++v->work <= CB_MAX_WORK; }
static bool cb_bit(const uint8_t *map, uint16_t sector) { return (map[sector / 8U] & (uint8_t)(1U << (sector % 8U))) != 0U; }
static void cb_set(uint8_t *map, uint16_t sector) { map[sector / 8U] |= (uint8_t)(1U << (sector % 8U)); }
static uint64_t cb_sector_offset(const cb_view *v, unsigned sector) {
    if (sector <= 3U || v->sector_size == 128U) return 16U + (uint64_t)(sector - 1U) * 128U;
    return 16U + 384U + (uint64_t)(sector - 4U) * 256U;
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
static bool cb_read_sector(cb_view *v, unsigned sector, uint8_t data[CB_COPY], xx_pd_struct *pd) {
    return sector >= 4U && sector <= v->sectors && cb_work(v, pd) &&
        cb_read_abs(v, cb_sector_offset(v, sector), data, v->sector_size, pd);
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
    static const char *const fixed[] = {"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"};
    char stem[16]; size_t i = 0U, j;
    while (name[i] && name[i] != '.' && i + 1U < sizeof(stem)) { stem[i] = name[i]; ++i; }
    stem[i] = 0;
    for (j = 0U; j < sizeof(fixed) / sizeof(fixed[0]); ++j) if (cb_equal(stem, fixed[j])) return true;
    return i == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') ||
         (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T'));
}
static bool cb_name(const uint8_t raw[11], char out[32]) {
    size_t used = 0U; unsigned i; bool padded = false;
    if (raw[0] < 'A' || raw[0] > 'Z') return false;
    for (i = 0U; i < 8U; ++i) {
        unsigned char c = raw[i];
        if (c == ' ') { padded = true; continue; }
        if (padded || !((c >= 'A' && c <= 'Z') || (i && c >= '0' && c <= '9'))) return false;
        out[used++] = (char)c;
    }
    padded = false;
    for (i = 8U; i < 11U; ++i) {
        unsigned char c = raw[i];
        if (c == ' ') { padded = true; continue; }
        if (padded || !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
        if (i == 8U) out[used++] = '.';
        out[used++] = (char)c;
    }
    out[used] = 0;
    if (cb_device_name(out)) {
        memmove(out + 1U, out, used + 1U); out[0] = '_';
    }
    return true;
}
static bool cb_claim(cb_view *v, unsigned sector) {
    if (!sector || sector > v->last_data || (v->variant == XX_ATARI_DOS2_ED && sector == 720U) ||
        cb_bit(v->free_map, (uint16_t)sector) ||
        cb_bit(v->claimed, (uint16_t)sector)) return false;
    cb_set(v->claimed, (uint16_t)sector); return true;
}
static bool cb_append(cb_view *v, const uint8_t raw[11], const char *base,
    uint64_t header, uint16_t first, uint16_t blocks, uint32_t size, uint16_t chain_start,
    uint8_t flags, xx_pd_struct *pd) {
    unsigned suffix; size_t i;
    if (v->count >= CB_MAX_FILES) return false;
    for (i = 0U; i < v->count; ++i)
        if (!memcmp(v->members[i].raw_name, raw, 11U)) return false;
    for (suffix = 0U; suffix <= CB_MAX_FILES; ++suffix) {
        char leaf[48]; size_t bytes; char *name; bool taken = false;
        if (!cb_work(v, pd)) return false;
        if (suffix) {
            const char *dot = strchr(base, '.');
            if (dot) xx_rt_snprintf(leaf, sizeof(leaf), "%.*s~%u%s", (int)(dot - base), base, suffix + 1U, dot);
            else xx_rt_snprintf(leaf, sizeof(leaf), "%s~%u", base, suffix + 1U);
        } else xx_rt_snprintf(leaf, sizeof(leaf), "%s", base);
        for (i = 0U; i < v->count; ++i) if (cb_equal(leaf, v->members[i].name)) { taken = true; break; }
        if (taken) continue;
        bytes = xx_str_len(leaf) + 1U;
        if (v->path_bytes + bytes > 4096U) return false;
        name = xx_str_dup(leaf); if (!name) return false;
        v->members[v->count].name = name; v->members[v->count].header = header;
        v->members[v->count].size = size; v->members[v->count].first_sector = first;
        v->members[v->count].blocks = blocks; v->members[v->count].first_chain = chain_start;
        v->members[v->count].flags = flags; memcpy(v->members[v->count].raw_name, raw, 11U);
        ++v->count; v->path_bytes += bytes; return true;
    }
    return false;
}
static bool cb_file(cb_view *v, const uint8_t entry[16], unsigned slot, uint64_t header, xx_pd_struct *pd) {
    uint8_t status = entry[0], data[CB_COPY]; uint16_t declared, first, current, start;
    uint32_t size = 0U; unsigned block; char name[32];
    if (!(status & 0x40U) || (status & 0x80U) || (status & 0x1CU) ||
        ((status & 1U) && v->variant != XX_ATARI_DOS2_ED) || !cb_name(entry + 5U, name)) return false;
    declared = (uint16_t)((unsigned)entry[1] | ((unsigned)entry[2] << 8U));
    first = (uint16_t)((unsigned)entry[3] | ((unsigned)entry[4] << 8U));
    if (!declared || declared > v->last_data || first < 4U || first > v->last_data) return false;
    start = (uint16_t)v->chain_count; current = first;
    for (block = 0U; block < declared; ++block) {
        unsigned next, used;
        if (!cb_work(v, pd) || !cb_claim(v, current) ||
            v->chain_count >= CB_MAX_SECTORS || !cb_read_sector(v, current, data, pd)) return false;
        v->chain[v->chain_count++] = current;
        if ((unsigned)(data[v->sector_size - 3U] >> 2U) != slot) return false;
        next = ((unsigned)(data[v->sector_size - 3U] & 3U) << 8U) | data[v->sector_size - 2U];
        used = data[v->sector_size - 1U];
        if (v->sector_size == 128U) {
            if ((used & 0x80U) && next) return false;
            used &= 0x7FU;
        }
        if (used > v->sector_size - 3U || size > UINT32_MAX - used) return false;
        size += used;
        if (block + 1U == declared) {
            if (next) return false;
        } else {
            if (next < 4U || next > v->last_data) return false;
            current = (uint16_t)next;
        }
    }
    return cb_append(v, entry + 5U, name, header, first, declared, size, start, status, pd);
}
static bool cb_directory(cb_view *v, xx_pd_struct *pd) {
    unsigned sector, slot = 0U; bool after_unused = false;
    for (sector = 361U; sector <= 368U; ++sector) {
        uint8_t data[CB_COPY]; unsigned i;
        if (!cb_read_sector(v, sector, data, pd)) return false;
        for (i = 0U; i < 8U; ++i, ++slot) {
            const uint8_t *entry = data + i * 16U; uint8_t status = entry[0];
            if (!cb_work(v, pd)) return false;
            if (!status) { after_unused = true; continue; }
            if (status == 0x80U) continue;
            if (after_unused || !cb_file(v, entry, slot,
                cb_sector_offset(v, sector) + i * 16U, pd)) return false;
        }
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
    xx_atari_dos2 *disk = (xx_atari_dos2 *)self;
    cb_view *v; int64_t total; uint8_t head[16], vtoc[CB_COPY], vtoc2[CB_COPY];
    uint64_t payload; uint32_t paragraphs, free_primary = 0U, free_extended = 0U;
    unsigned sector, expected_total;
    if (!self || !self->device || self->base_address < 0 || cb_stopped(pd) ||
        (total = xx_io_total_size(self->device)) < self->base_address ||
        (uint64_t)(total - self->base_address) < 16U) return NULL;
    v = (cb_view *)xx_mem_alloc(sizeof(*v)); if (!v) return NULL;
    xx_mem_zero(v, sizeof(*v)); v->device = self->device; v->base = self->base_address;
    v->bytes = (uint64_t)(total - v->base);
    if (!cb_read_abs(v, 0U, head, sizeof(head), pd) ||
        head[0] != 0x96U || head[1] != 0x02U) goto fail;
    paragraphs = (uint32_t)head[2] | ((uint32_t)head[3] << 8U) | ((uint32_t)head[6] << 16U);
    payload = (uint64_t)paragraphs * 16U;
    v->sector_size = (unsigned)head[4] | ((unsigned)head[5] << 8U);
    if (v->sector_size == 128U && payload == 720U * 128U) v->variant = XX_ATARI_DOS2_SD;
    else if (v->sector_size == 128U && payload == 1040U * 128U) v->variant = XX_ATARI_DOS2_ED;
    else if (v->sector_size == 256U && payload == 384U + 717U * 256U) v->variant = XX_ATARI_DOS2_DD;
    else goto fail;
    if (disk->variant != XX_ATARI_DOS2_AUTO && disk->variant != v->variant) goto fail;
    if (payload > v->bytes - 16U) goto fail;
    v->bytes = 16U + payload;
    v->sectors = v->variant == XX_ATARI_DOS2_ED ? 1040U : 720U;
    v->last_data = v->variant == XX_ATARI_DOS2_ED ? 1023U : 719U;
    if (!cb_read_sector(v, 360U, vtoc, pd) ||
        vtoc[0] != 2U) goto fail;
    expected_total = v->variant == XX_ATARI_DOS2_ED ? 1010U : 707U;
    if (((unsigned)vtoc[1] | ((unsigned)vtoc[2] << 8U)) != expected_total) goto fail;
    for (sector = 1U; sector <= 719U; ++sector) {
        if (vtoc[10U + sector / 8U] & (uint8_t)(0x80U >> (sector % 8U))) {
            cb_set(v->free_map, (uint16_t)sector); ++free_primary;
        }
    }
    if (((unsigned)vtoc[3] | ((unsigned)vtoc[4] << 8U)) != free_primary) goto fail;
    if (v->variant == XX_ATARI_DOS2_ED) {
        if (!cb_read_sector(v, 1024U, vtoc2, pd) ||
            memcmp(vtoc + 16U, vtoc2, 84U)) goto fail;
        for (sector = 721U; sector <= 1023U; ++sector) {
            if (vtoc2[84U + (sector - 720U) / 8U] &
                (uint8_t)(0x80U >> (sector % 8U))) {
                cb_set(v->free_map, (uint16_t)sector); ++free_extended;
            }
        }
        if (((unsigned)vtoc2[122] | ((unsigned)vtoc2[123] << 8U)) != free_extended) goto fail;
    }
    for (sector = 1U; sector <= 3U; ++sector) if (!cb_claim(v, sector)) goto fail;
    for (sector = 360U; sector <= 368U; ++sector) if (!cb_claim(v, sector)) goto fail;
    if (!cb_directory(v, pd)) goto fail;
    v->retained_memory = sizeof(*v) + v->path_bytes;
    return v;
fail:
    cb_view_free(v); return NULL;
}
static void cb_vtable_destroy(Abstractformat *self) { xx_atari_dos2_destroy((xx_atari_dos2 *)self); }
void xx_atari_dos2_init_ex(xx_atari_dos2 *v, xx_io_device *device, int64_t base, xx_atari_dos2_variant variant) {
    if (!v) return;
    xx_mem_zero(v, sizeof(*v)); xx_format_init(&v->format, device, base);
    v->variant = variant;
    v->format.endian = XX_ENDIAN_LITTLE; v->format.file_type = CB_TYPE;
    v->format.format_type = XX_TYPE_ARCHIVE; v->format.is_archive = true;
    xx_format_set_mime_type(&v->format, "application/x-atari-atr");
    xx_format_set_extension(&v->format, "atr");
    v->format.check_is_valid = xx_atari_dos2_check_is_valid;
    v->format.handle_base_info = xx_atari_dos2_handle_base_info;
    v->format.get_format_size = xx_atari_dos2_get_format_size;
    v->format.get_number_of_archive_records = xx_atari_dos2_get_number_of_archive_records;
    v->format.create_archive_records_reading = xx_atari_dos2_create_archive_records_reading;
    v->format.get_current_archive_record = xx_atari_dos2_get_current_archive_record;
    v->format.archive_record_move_to_next = xx_atari_dos2_archive_record_move_to_next;
    v->format.unpack_current_archive_record = xx_atari_dos2_unpack_current_archive_record;
    v->format.free_archive_records_reading = xx_atari_dos2_free_archive_records_reading;
    v->format.destroy = cb_vtable_destroy;
}
void xx_atari_dos2_init(xx_atari_dos2 *v, xx_io_device *device, int64_t base) {
    xx_atari_dos2_init_ex(v, device, base, XX_ATARI_DOS2_AUTO);
}
xx_atari_dos2 *xx_atari_dos2_create(xx_io_device *device, int64_t base) {
    xx_atari_dos2 *v = (xx_atari_dos2 *)xx_mem_alloc(sizeof(*v)); if (v) xx_atari_dos2_init(v, device, base); return v;
}
void xx_atari_dos2_destroy(xx_atari_dos2 *v) { if (v) xx_format_cleanup_extra_parameters(&v->format); }
void xx_atari_dos2_free(xx_atari_dos2 *v) { if (v) { xx_atari_dos2_destroy(v); xx_mem_free(v); } }
bool xx_atari_dos2_check_is_valid(Abstractformat *self, xx_pd_struct *pd) { cb_view *v = cb_parse(self, pd); bool ok = v != NULL; cb_view_free(v); return ok; }
bool xx_atari_dos2_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_atari_dos2 *disk = (xx_atari_dos2 *)self; cb_view *v = cb_parse(self, pd); int64_t total, end;
    if (!self) return false;
    if (!v) { self->is_valid = false; self->base_info_handled = false; return false; }
    disk->number_of_records = v->count;
    disk->variant = v->variant; disk->sector_count = v->sectors; disk->sector_size = v->sector_size;
    self->format_size = (int64_t)v->bytes; self->number_of_archive_records = v->count;
    total = xx_io_total_size(self->device); end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true;
    cb_view_free(v); return true;
}
int64_t xx_atari_dos2_get_format_size(Abstractformat *self, xx_pd_struct *pd) { return xx_atari_dos2_handle_base_info(self, pd) ? self->format_size : -1; }
uint64_t xx_atari_dos2_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) { return xx_atari_dos2_handle_base_info(self, pd) ? ((xx_atari_dos2 *)self)->number_of_records : 0U; }
static bool cb_record(xx_archive_record *record, const cb_view *v, const cb_member *m) {
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)m->header; record->header_size = 16U;
    record->data_offset = v->base + (int64_t)cb_sector_offset(v, m->first_sector);
    record->compressed_size = m->size;
    return xx_archive_record_set_original_name(record, m->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, m->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, m->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, (m->flags & 0x20U) ? 1U : 0U);
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
xx_archive_record_state *xx_atari_dos2_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    cb_view *v = cb_parse(self, pd); xx_archive_record_state *state;
    if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state)); if (!state) { cb_view_free(v); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = v; state->free_internal = cb_view_free; state->total_records = (int64_t)v->count;
    if (!cb_options(&state->options, options) || (v->count && !cb_record(&state->current_record, v, v->members))) { xx_archive_record_state_free(state); return NULL; }
    state->has_record = v->count != 0U; state->current_index = v->count ? 0 : -1; return state;
}
const xx_archive_record *xx_atari_dos2_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_atari_dos2_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
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
bool xx_atari_dos2_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    cb_view *v; const cb_member *m; uint8_t *buffer = NULL;
    uint32_t done = 0U; size_t buffer_size; unsigned block; bool ok = true;
    if (!self || !self->device || destination == self->device || !state ||
        state->format != self || !state->has_record ||
        !(v = (cb_view *)state->internal_state) || v->index >= v->count || cb_stopped(pd)) return false;
    m = v->members + v->index;
    if (!cb_limits(self, state, v, m, &buffer_size)) return false;
    if (buffer_size) { buffer = (uint8_t *)xx_mem_alloc(buffer_size); if (!buffer) return false; }
    for (block = 0U; block < m->blocks && ok && !cb_stopped(pd); ++block) {
        unsigned sector = v->chain[m->first_chain + block]; uint8_t trailer;
        size_t used, within = 0U;
        uint64_t start = cb_sector_offset(v, sector);
        if (!cb_work(v, pd) ||
            !cb_read_abs(v, start + v->sector_size - 1U, &trailer, 1U, pd)) { ok = false; break; }
        used = v->sector_size == 128U ? (size_t)(trailer & 0x7FU) : trailer;
        if (used > m->size - done || (used && !buffer)) { ok = false; break; }
        while (within < used && ok && !cb_stopped(pd)) {
            size_t part = used - within, written = 0U;
            if (part > buffer_size) part = buffer_size;
            if (!cb_work(v, pd) || !cb_read_abs(v, start + within, buffer, part, pd)) { ok = false; break; }
            while (destination && written < part && !cb_stopped(pd)) {
                ssize_t got = xx_io_write(destination, buffer + written, part - written);
                if (got <= 0 || (size_t)got > part - written) { ok = false; break; }
                written += (size_t)got;
            }
            if (cb_stopped(pd)) { ok = false; break; }
            within += part; done += (uint32_t)part;
        }
    }
    xx_mem_free(buffer);
    return ok && block == m->blocks && done == m->size && !cb_stopped(pd);
}
static xx_io_device *cb_stage(const char *destination, char **stage_path) {
    unsigned attempt; size_t i, parent = 0U; char *directory = xx_str_dup(destination);
    *stage_path = NULL; if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i) if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate; xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_atari_dos2.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix); if (!candidate) break;
        if (cb_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; }
        xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_atari_dos2_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    cb_view *v; const xx_var *option, *overwrite_option; const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage_path = NULL; size_t buffer_size; bool overwrite, ok = false;
    if (!self || !state || state->format != self || !state->has_record || !(v = (cb_view *)state->internal_state) || v->index >= v->count || cb_stopped(pd)) return false;
    if (!cb_limits(self, state, v, v->members + v->index, &buffer_size)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return xx_atari_dos2_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) { owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base = owned; }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\')
        ? xx_str_concat3(base, "/", v->members[v->index].name) : xx_str_concat(base, v->members[v->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = cb_stage(path, &stage_path); if (!output) goto done;
        ok = xx_atari_dos2_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output)) ok = false;
    }
    if (cb_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(stage_path, path, overwrite);
done:
    if (!ok && stage_path) xx_io_file_remove_a(stage_path);
    xx_str_free(stage_path); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_atari_dos2_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) { (void)self; xx_archive_record_state_free(state); }
