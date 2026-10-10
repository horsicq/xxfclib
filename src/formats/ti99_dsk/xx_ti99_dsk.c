/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native TI-99/4A sector-based DSK filesystem reader from published TI structures.
 */
#include "xxfclib/formats/ti99_dsk/xx_ti99_dsk.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include <string.h>
#include <stdio.h>
#ifdef TI99_DSK
#define CB_TYPE XX_FILE_TYPE_TI99_DSK
#else
#define CB_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define CB_MAX_SECTORS 1440U
#define CB_MAX_FILES 127U
#define CB_MAX_WORK 100000U
#define CB_COPY 256U
typedef struct cb_member_s {
    char *name;
    uint8_t raw_name[10], flags, eof, record_length, records_per_sector;
    uint16_t fdr, first_chain, sectors, records;
    uint32_t size;
} cb_member;
typedef struct cb_view_s {
    xx_io_device *device;
    int64_t base;
    uint64_t bytes, path_bytes, retained_memory;
    uint32_t work, sectors;
    xx_ti99_dsk_variant variant;
    uint8_t allocated[(CB_MAX_SECTORS + 7U) / 8U], claimed[(CB_MAX_SECTORS + 7U) / 8U];
    uint16_t chain[CB_MAX_SECTORS];
    size_t chain_count, count, index;
    cb_member members[CB_MAX_FILES];
} cb_view;
static bool cb_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool cb_work(cb_view *v, xx_pd_struct *pd)
{
    return !cb_stopped(pd) && ++v->work <= CB_MAX_WORK;
}
static bool cb_bit(const uint8_t *map, unsigned sector)
{
    return (map[sector / 8U] & (uint8_t)(1U << (sector % 8U))) != 0U;
}
static void cb_set(uint8_t *map, unsigned sector)
{
    map[sector / 8U] |= (uint8_t)(1U << (sector % 8U));
}
static bool cb_read_abs(cb_view *v, uint64_t offset, void *output, size_t size, xx_pd_struct *pd)
{
    int64_t cursor;
    size_t done = 0U;
    bool ok = false;
    if (!v || offset > v->bytes || size > v->bytes - offset || offset > (uint64_t)(INT64_MAX - v->base) || cb_stopped(pd)) return false;
    cursor = xx_io_tell(v->device);
    if (cursor < 0) return false;
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
static bool cb_read_sector(cb_view *v, unsigned sector, uint8_t data[CB_COPY], xx_pd_struct *pd)
{
    return sector < v->sectors && cb_work(v, pd) && cb_read_abs(v, (uint64_t)sector * 256U, data, CB_COPY, pd);
}
static bool cb_equal(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}
static bool cb_device_name(const char *name)
{
    static const char *const fixed[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    char stem[16];
    size_t i = 0U, j;
    while (name[i] && name[i] != '.' && i + 1U < sizeof(stem)) {
        stem[i] = name[i];
        ++i;
    }
    stem[i] = 0;
    for (j = 0U; j < sizeof(fixed) / sizeof(fixed[0]); ++j)
        if (cb_equal(stem, fixed[j])) return true;
    return i == 4U && stem[3] >= '0' && stem[3] <= '9' && ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') || (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T'));
}
static bool cb_name(const uint8_t raw[10], char out[48])
{
    size_t used = 0U;
    unsigned i;
    bool padded = false;
    for (i = 0U; i < 10U; ++i) {
        unsigned char c = raw[i];
        if (c == ' ') {
            padded = true;
            continue;
        }
        if (padded || c < 0x21U || c > 0x7EU) return false;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '.') {
            out[used++] = '_';
        } else out[used++] = (char)c;
    }
    if (!used || out[used - 1U] == '.') return false;
    out[used] = 0;
    if (cb_device_name(out)) {
        memmove(out + 1U, out, used + 1U);
        out[0] = '_';
    }
    return true;
}
static bool cb_claim(cb_view *v, unsigned sector)
{
    if (sector >= v->sectors || !cb_bit(v->allocated, sector) || cb_bit(v->claimed, sector)) return false;
    cb_set(v->claimed, sector);
    return true;
}
static bool cb_append(cb_view *v, const uint8_t raw[10], const char *base, uint16_t fdr, uint16_t start, uint16_t sectors, uint16_t records, uint32_t size, uint8_t flags,
                      uint8_t eof, uint8_t reclen, uint8_t rps, xx_pd_struct *pd)
{
    unsigned suffix;
    size_t i;
    if (v->count >= CB_MAX_FILES) return false;
    for (i = 0U; i < v->count; ++i)
        if (!memcmp(v->members[i].raw_name, raw, 10U)) return false;
    for (suffix = 0U; suffix <= CB_MAX_FILES; ++suffix) {
        char leaf[48];
        size_t bytes;
        char *name;
        bool taken = false;
        if (!cb_work(v, pd)) return false;
        if (suffix) xx_rt_snprintf(leaf, sizeof(leaf), "%s~%u", base, suffix + 1U);
        else xx_rt_snprintf(leaf, sizeof(leaf), "%s", base);
        for (i = 0U; i < v->count; ++i)
            if (cb_equal(leaf, v->members[i].name)) {
                taken = true;
                break;
            }
        if (taken) continue;
        bytes = xx_str_len(leaf) + 1U;
        if (v->path_bytes + bytes > 8192U) return false;
        name = xx_str_dup(leaf);
        if (!name) return false;
        v->members[v->count].name = name;
        v->members[v->count].fdr = fdr;
        v->members[v->count].first_chain = start;
        v->members[v->count].sectors = sectors;
        v->members[v->count].records = records;
        v->members[v->count].size = size;
        v->members[v->count].flags = flags;
        v->members[v->count].eof = eof;
        v->members[v->count].record_length = reclen;
        v->members[v->count].records_per_sector = rps;
        memcpy(v->members[v->count].raw_name, raw, 10U);
        ++v->count;
        v->path_bytes += bytes;
        return true;
    }
    return false;
}
static bool cb_variable_size(cb_view *v, size_t chain_start, unsigned sectors, unsigned max_record, uint8_t flags, uint8_t eof, uint32_t *size, xx_pd_struct *pd)
{
    unsigned block;
    uint32_t total = 0U;
    for (block = 0U; block < sectors; ++block) {
        uint8_t data[CB_COPY];
        unsigned pos = 0U;
        if (!cb_read_sector(v, v->chain[chain_start + block], data, pd)) return false;
        while (pos < CB_COPY) {
            unsigned length = data[pos];
            if (length == 0xFFU && !(pos == 0U && max_record == 255U)) break;
            if (length > max_record || length > 255U - pos || total > UINT32_MAX - length - 1U) return false;
            total += length + 1U;
            pos += length + 1U;
        }
        if (block + 1U == sectors && pos != eof && !(pos == CB_COPY && eof == 0U)) return false;
        (void)flags;
    }
    *size = total;
    return true;
}
static bool cb_file(cb_view *v, unsigned fdr_sector, const uint8_t data[CB_COPY], xx_pd_struct *pd)
{
    char name[48];
    unsigned blocks, i;
    int previous = -1;
    unsigned flags = data[12], rps = data[13], eof = data[16], reclen = data[17];
    unsigned records = (unsigned)data[18] | ((unsigned)data[19] << 8U);
    uint32_t size = 0U;
    size_t chain_start = v->chain_count;
    if (!cb_name(data, name) || (flags & 0x74U) || (flags & 3U) == 3U) return false;
    blocks = ((unsigned)data[14] << 8U) | data[15];
    if (blocks > v->sectors - 2U || blocks > CB_MAX_SECTORS - v->chain_count) return false;
    for (i = 0U; i < 76U && v->chain_count - chain_start < blocks; ++i) {
        unsigned start = (unsigned)data[28U + 3U * i] | ((unsigned)(data[29U + 3U * i] & 0x0FU) << 8U);
        unsigned end = ((unsigned)data[29U + 3U * i] >> 4U) | ((unsigned)data[30U + 3U * i] << 4U);
        unsigned length, n;
        if (start < 2U || start >= v->sectors || (int)end <= previous) return false;
        length = (unsigned)((int)end - previous);
        if (length > blocks - (unsigned)(v->chain_count - chain_start) || length > v->sectors - start) return false;
        for (n = 0U; n < length; ++n) {
            if (!cb_work(v, pd) || !cb_claim(v, start + n)) return false;
            v->chain[v->chain_count++] = (uint16_t)(start + n);
        }
        previous = end;
    }
    if (v->chain_count - chain_start != blocks) return false;
    if (flags & 1U) {
        if ((flags & 0x80U) || rps || reclen || records || (blocks == 0U && eof)) return false;
        size = blocks ? (uint32_t)((blocks - 1U) * 256U + (eof ? eof : 256U)) : 0U;
    } else if (!(flags & 0x80U)) {
        unsigned expected_rps, last_records;
        if (!reclen || !blocks || records > blocks * 256U) return false;
        expected_rps = 256U / reclen;
        if (rps != (expected_rps & 255U) || !expected_rps || records > blocks * expected_rps || (records && records <= (blocks - 1U) * expected_rps)) return false;
        last_records = records ? records - (blocks - 1U) * expected_rps : 0U;
        if (eof != ((last_records * reclen) & 255U)) return false;
        size = (uint32_t)records * reclen;
    } else {
        if (!reclen || records != blocks || !cb_variable_size(v, chain_start, blocks, reclen, (uint8_t)flags, (uint8_t)eof, &size, pd)) return false;
    }
    return cb_append(v, data, name, (uint16_t)fdr_sector, (uint16_t)chain_start, (uint16_t)blocks, (uint16_t)records, size, (uint8_t)flags, (uint8_t)eof, (uint8_t)reclen,
                     (uint8_t)rps, pd);
}
static void cb_view_free(void *ptr)
{
    cb_view *v = (cb_view *)ptr;
    size_t i;
    if (!v) return;
    for (i = 0U; i < v->count; ++i) xx_str_free(v->members[i].name);
    xx_mem_free(v);
}
static cb_view *cb_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_ti99_dsk *disk = (xx_ti99_dsk *)self;
    cb_view *v;
    int64_t total;
    uint8_t vib[CB_COPY], directory[CB_COPY], fdr[CB_COPY], prior[10];
    unsigned total_sectors, expected_spt, expected_sides, expected_density, slot;
    bool after_end = false, has_prior = false;
    if (!self || !self->device || self->base_address < 0 || cb_stopped(pd) || (total = xx_io_total_size(self->device)) < self->base_address ||
        (uint64_t)(total - self->base_address) < 512U)
        return NULL;
    v = (cb_view *)xx_mem_alloc(sizeof(*v));
    if (!v) return NULL;
    xx_mem_zero(v, sizeof(*v));
    v->device = self->device;
    v->base = self->base_address;
    v->bytes = (uint64_t)(total - v->base);
    if (!cb_read_abs(v, 0U, vib, sizeof(vib), pd) || memcmp(vib + 13U, "DSK", 3U)) goto fail;
    total_sectors = ((unsigned)vib[10] << 8U) | vib[11];
    if (total_sectors == 360U) {
        v->variant = XX_TI99_DSK_SS_SD;
        expected_spt = 9U;
        expected_sides = 1U;
        expected_density = 1U;
    } else if (total_sectors == 720U) {
        v->variant = XX_TI99_DSK_DS_SD;
        expected_spt = 9U;
        expected_sides = 2U;
        expected_density = 1U;
    } else if (total_sectors == 1440U) {
        v->variant = XX_TI99_DSK_DS_DD;
        expected_spt = 18U;
        expected_sides = 2U;
        expected_density = 2U;
    } else goto fail;
    if ((disk->variant != XX_TI99_DSK_AUTO && disk->variant != v->variant) || vib[12] != expected_spt || vib[17] != 40U || vib[18] != expected_sides ||
        vib[19] != expected_density || (uint64_t)total_sectors * 256U > v->bytes)
        goto fail;
    v->sectors = total_sectors;
    v->bytes = (uint64_t)total_sectors * 256U;
    memcpy(v->allocated, vib + 0x38U, sizeof(v->allocated));
    if (!cb_claim(v, 0U) || !cb_claim(v, 1U) || !cb_read_sector(v, 1U, directory, pd)) goto fail;
    for (slot = 0U; slot < 128U; ++slot) {
        unsigned fdr_sector = ((unsigned)directory[2U * slot] << 8U) | directory[2U * slot + 1U];
        if (!fdr_sector) {
            after_end = true;
            continue;
        }
        if (after_end || slot >= CB_MAX_FILES || fdr_sector < 2U || !cb_claim(v, fdr_sector) || !cb_read_sector(v, fdr_sector, fdr, pd)) goto fail;
        if (has_prior && memcmp(prior, fdr, 10U) >= 0) goto fail;
        memcpy(prior, fdr, 10U);
        has_prior = true;
        if (!cb_file(v, fdr_sector, fdr, pd)) goto fail;
    }
    v->retained_memory = sizeof(*v) + v->path_bytes;
    return v;
fail:
    cb_view_free(v);
    return NULL;
}
static void cb_vtable_destroy(Abstractformat *self)
{
    xx_ti99_dsk_destroy((xx_ti99_dsk *)self);
}
void xx_ti99_dsk_init_ex(xx_ti99_dsk *v, xx_io_device *device, int64_t base, xx_ti99_dsk_variant variant)
{
    if (!v) return;
    xx_mem_zero(v, sizeof(*v));
    xx_format_init(&v->format, device, base);
    v->variant = variant;
    v->format.endian = XX_ENDIAN_LITTLE;
    v->format.file_type = CB_TYPE;
    v->format.format_type = XX_TYPE_ARCHIVE;
    v->format.is_archive = true;
    xx_format_set_mime_type(&v->format, "application/x-ti99-dsk");
    xx_format_set_extension(&v->format, "dsk");
    v->format.check_is_valid = xx_ti99_dsk_check_is_valid;
    v->format.handle_base_info = xx_ti99_dsk_handle_base_info;
    v->format.get_format_size = xx_ti99_dsk_get_format_size;
    v->format.get_number_of_archive_records = xx_ti99_dsk_get_number_of_archive_records;
    v->format.create_archive_records_reading = xx_ti99_dsk_create_archive_records_reading;
    v->format.get_current_archive_record = xx_ti99_dsk_get_current_archive_record;
    v->format.archive_record_move_to_next = xx_ti99_dsk_archive_record_move_to_next;
    v->format.unpack_current_archive_record = xx_ti99_dsk_unpack_current_archive_record;
    v->format.free_archive_records_reading = xx_ti99_dsk_free_archive_records_reading;
    v->format.destroy = cb_vtable_destroy;
}
void xx_ti99_dsk_init(xx_ti99_dsk *v, xx_io_device *device, int64_t base)
{
    xx_ti99_dsk_init_ex(v, device, base, XX_TI99_DSK_AUTO);
}
xx_ti99_dsk *xx_ti99_dsk_create(xx_io_device *device, int64_t base)
{
    xx_ti99_dsk *v = (xx_ti99_dsk *)xx_mem_alloc(sizeof(*v));
    if (v) xx_ti99_dsk_init(v, device, base);
    return v;
}
void xx_ti99_dsk_destroy(xx_ti99_dsk *v)
{
    if (v) xx_format_cleanup_extra_parameters(&v->format);
}
void xx_ti99_dsk_free(xx_ti99_dsk *v)
{
    if (v) {
        xx_ti99_dsk_destroy(v);
        xx_mem_free(v);
    }
}
bool xx_ti99_dsk_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    cb_view *v = cb_parse(self, pd);
    bool ok = v != NULL;
    cb_view_free(v);
    return ok;
}
bool xx_ti99_dsk_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_ti99_dsk *disk = (xx_ti99_dsk *)self;
    cb_view *v = cb_parse(self, pd);
    int64_t total, end;
    if (!self) return false;
    if (!v) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    disk->number_of_records = v->count;
    disk->variant = v->variant;
    disk->sector_count = v->sectors;
    self->format_size = (int64_t)v->bytes;
    self->number_of_archive_records = v->count;
    total = xx_io_total_size(self->device);
    end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1;
    self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true;
    self->base_info_handled = true;
    cb_view_free(v);
    return true;
}
int64_t xx_ti99_dsk_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return xx_ti99_dsk_handle_base_info(self, pd) ? self->format_size : -1;
}
uint64_t xx_ti99_dsk_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    return xx_ti99_dsk_handle_base_info(self, pd) ? ((xx_ti99_dsk *)self)->number_of_records : 0U;
}
static bool cb_record(xx_archive_record *record, const cb_view *v, const cb_member *m)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)m->fdr * 256;
    record->header_size = 256U;
    record->data_offset = m->sectors ? v->base + (int64_t)v->chain[m->first_chain] * 256 : -1;
    record->compressed_size = m->size;
    return xx_archive_record_set_original_name(record, m->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, m->size) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, (m->flags & 0x08U) ? 1U : 0U);
}
static bool cb_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i);
        xx_meta copy;
        if (!item) {
            continue;
        }
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
xx_archive_record_state *xx_ti99_dsk_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    cb_view *v = cb_parse(self, pd);
    xx_archive_record_state *state;
    if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        cb_view_free(v);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = v;
    state->free_internal = cb_view_free;
    state->total_records = (int64_t)v->count;
    if (!cb_options(&state->options, options) || (v->count && !cb_record(&state->current_record, v, v->members))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = v->count != 0U;
    state->current_index = v->count ? 0 : -1;
    return state;
}
const xx_archive_record *xx_ti99_dsk_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_ti99_dsk_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    cb_view *v;
    if (!self || !state || state->format != self || !state->has_record || !(v = (cb_view *)state->internal_state) || cb_stopped(pd)) return false;
    if (v->index + 1U >= v->count) {
        v->index = v->count;
        state->has_record = false;
        state->current_index = -1;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        return false;
    }
    if (!cb_record(&state->current_record, v, v->members + v->index + 1U)) {
        state->has_record = false;
        return false;
    }
    ++v->index;
    ++state->current_index;
    return true;
}
static uint64_t cb_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback)
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
static bool cb_limits(Abstractformat *self, xx_archive_record_state *state, const cb_view *v, const cb_member *m, size_t *buffer_size)
{
    *buffer_size = m->sectors ? CB_COPY : 0U;
    return m->size <= cb_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX) &&
           v->retained_memory + sizeof(*state) + *buffer_size <= cb_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
static bool cb_emit(xx_io_device *destination, const uint8_t *data, size_t size, uint32_t *done, uint32_t limit, xx_pd_struct *pd)
{
    size_t written = 0U;
    if (size > limit - *done || cb_stopped(pd)) return false;
    while (destination && written < size && !cb_stopped(pd)) {
        ssize_t got = xx_io_write(destination, data + written, size - written);
        if (got <= 0 || (size_t)got > size - written) return false;
        written += (size_t)got;
    }
    if (cb_stopped(pd)) return false;
    *done += (uint32_t)size;
    return true;
}
bool xx_ti99_dsk_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd)
{
    cb_view *v;
    const cb_member *m;
    uint8_t *buffer = NULL;
    uint32_t done = 0U;
    size_t buffer_size;
    bool ok = true;
    unsigned block;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record || !(v = (cb_view *)state->internal_state) ||
        v->index >= v->count || cb_stopped(pd))
        return false;
    m = v->members + v->index;
    if (!cb_limits(self, state, v, m, &buffer_size)) return false;
    if (buffer_size) {
        buffer = (uint8_t *)xx_mem_alloc(buffer_size);
        if (!buffer) return false;
    }
    if (m->flags & 1U) {
        for (block = 0U; block < m->sectors && ok; ++block) {
            size_t used = block + 1U == m->sectors && m->eof ? m->eof : 256U;
            if (!cb_read_sector(v, v->chain[m->first_chain + block], buffer, pd) || !cb_emit(destination, buffer, used, &done, m->size, pd)) ok = false;
        }
    } else if (!(m->flags & 0x80U)) {
        unsigned record, current_sector = CB_MAX_SECTORS;
        unsigned rps = m->records_per_sector ? m->records_per_sector : 256U;
        for (record = 0U; record < m->records && ok; ++record) {
            unsigned sector_index = record / rps, inside = record % rps * m->record_length;
            if (sector_index >= m->sectors || inside + m->record_length > 256U) {
                ok = false;
                break;
            }
            if (sector_index != current_sector) {
                if (!cb_read_sector(v, v->chain[m->first_chain + sector_index], buffer, pd)) {
                    ok = false;
                    break;
                }
                current_sector = sector_index;
            }
            if (!cb_emit(destination, buffer + inside, m->record_length, &done, m->size, pd)) ok = false;
        }
    } else {
        for (block = 0U; block < m->sectors && ok; ++block) {
            unsigned pos = 0U;
            if (!cb_read_sector(v, v->chain[m->first_chain + block], buffer, pd)) {
                ok = false;
                break;
            }
            while (pos < 256U && ok) {
                unsigned length = buffer[pos];
                uint8_t prefix;
                if (length == 0xFFU && !(pos == 0U && m->record_length == 255U)) break;
                if (length > m->record_length || length > 255U - pos) {
                    ok = false;
                    break;
                }
                prefix = (m->flags & 2U) ? (uint8_t)length : (uint8_t)'\n';
                if ((m->flags & 2U) && !cb_emit(destination, &prefix, 1U, &done, m->size, pd)) {
                    ok = false;
                    break;
                }
                if (!cb_emit(destination, buffer + pos + 1U, length, &done, m->size, pd)) {
                    ok = false;
                    break;
                }
                if (!(m->flags & 2U) && !cb_emit(destination, &prefix, 1U, &done, m->size, pd)) {
                    ok = false;
                    break;
                }
                pos += length + 1U;
            }
        }
    }
    xx_mem_free(buffer);
    return ok && done == m->size && !cb_stopped(pd);
}
static xx_io_device *cb_stage(const char *destination, char **stage_path)
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
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_ti99_dsk.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (cb_equal(candidate, destination)) {
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
bool xx_ti99_dsk_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    cb_view *v;
    const xx_var *option, *overwrite_option;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage_path = NULL;
    size_t buffer_size;
    bool overwrite, ok = false;
    if (!self || !state || state->format != self || !state->has_record || !(v = (cb_view *)state->internal_state) || v->index >= v->count || cb_stopped(pd)) return false;
    if (!cb_limits(self, state, v, v->members + v->index, &buffer_size)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return xx_ti99_dsk_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", v->members[v->index].name)
                                                                                                : xx_str_concat(base, v->members[v->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = cb_stage(path, &stage_path);
        if (!output) goto done;
        ok = xx_ti99_dsk_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output)) ok = false;
    }
    if (cb_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(stage_path, path, overwrite);
done:
    if (!ok && stage_path) xx_io_file_remove_a(stage_path);
    xx_str_free(stage_path);
    xx_str_free(path);
    xx_str_free(owned);
    return ok;
}
void xx_ti99_dsk_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
