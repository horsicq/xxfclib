/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native implementation from the published Apple DOS disk layout.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/apple_dos33/xx_apple_dos33.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include "xxfclib/data/xx_data.h"
#ifdef APPLE_DOS33
#define DOS33_TYPE XX_FILE_TYPE_APPLE_DOS33
#else
#define DOS33_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define DOS33_SECTOR 256U
#define DOS33_TRACK 4096U
#define DOS33_MAX_SECTORS 1280U
#define DOS33_LOGICAL_SECTORS 65536U
/* DOS logical sector -> ProDOS logical half-block on one16-sector track. */
static const uint8_t dos33_prodos_sector[16] = {0,14,13,12,11,10,9,8,7,6,5,4,3,2,1,15};
typedef struct dos33_pair_s { uint32_t logical; uint16_t physical; } dos33_pair;
typedef struct dos33_member_s {
    char name[48];
    uint32_t header, size, skip, sectors;
    uint16_t pair_start, pair_count, declared_sectors;
    uint8_t first_track, first_sector, type;
    bool sparse;
} dos33_member;
typedef struct dos33_view_s {
    xx_io_device *device;
    int64_t base;
    uint32_t bytes, tracks;
    uint8_t volume, bitmap[200], claimed[DOS33_MAX_SECTORS];
    xx_apple_dos33_order order;
    xx_apple_dos33_mode mode;
    dos33_pair pairs[DOS33_MAX_SECTORS];
    uint16_t pair_count;
    dos33_member members[DOS33_MAX_SECTORS];
    size_t count, index;
} dos33_view;
static bool dos33_limit(Abstractformat *self, const xx_list_s *options, xx_meta_id_t id, uint64_t required) {
    const xx_var *value = xx_format_resolve_extra_parameter(self, options, id);
    return !value || required <= xx_var_get_u64(value);
}
static uint32_t dos33_physical(const dos33_view *view, uint16_t sector) {
    return (uint32_t)(sector / 16U) * DOS33_TRACK +
        (view->order == XX_APPLE_DOS33_ORDER_PRODOS ? dos33_prodos_sector[sector % 16U] : sector % 16U) * DOS33_SECTOR;
}
static bool dos33_read_at(const dos33_view *view, uint32_t offset, void *buffer,
                            size_t size, xx_pd_struct *pd) {
    int64_t saved; size_t done = 0U; bool ok = false;
    if (offset > view->bytes || size > view->bytes - offset || (pd && xx_pd_is_stopped(pd))) return false;
    saved = xx_io_tell(view->device); if (saved < 0) return false;
    if (!xx_io_seek64(view->device, view->base + offset, SEEK_SET)) {
        while (done < size && !(pd && xx_pd_is_stopped(pd))) {
            ssize_t got = xx_io_read(view->device, (uint8_t *)buffer + done, size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(view->device, saved, SEEK_SET)) ok = false;
    return ok && !(pd && xx_pd_is_stopped(pd));
}
static bool dos33_sector(const dos33_view *view, uint16_t sector, uint8_t *buffer, xx_pd_struct *pd) {
    return sector < view->tracks * 16U && dos33_read_at(view, dos33_physical(view, sector), buffer, DOS33_SECTOR, pd);
}
static bool dos33_ts(const dos33_view *view, uint8_t track, uint8_t sector, uint16_t *index) {
    if (!track || track >= view->tracks || sector >= 16U) return false;
    *index = (uint16_t)(track * 16U + sector); return true;
}
static bool dos33_claim(dos33_view *view, uint16_t index) {
    uint8_t track = (uint8_t)(index / 16U), sector = (uint8_t)(index % 16U);
    uint8_t byte;
    if (index >= view->tracks * 16U || index >= DOS33_MAX_SECTORS) return false;
    /* Basis 108 80-track volumes use two bitmap bytes per track; ordinary
     * volumes use the high two bytes of each four-byte track entry. */
    byte = view->bitmap[track * (view->tracks > 50U ? 2U : 4U) + 1U - sector / 8U];
    if (view->claimed[index] || (byte & (1U << (sector % 8U)))) return false;
    view->claimed[index] = 1U; return true;
}
static char dos33_fold(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }
static bool dos33_equal(const char *a, const char *b) {
    while (*a && *b && dos33_fold(*a) == dos33_fold(*b)) { ++a; ++b; }
    return !*a && !*b;
}
static bool dos33_device_name(const char *name) {
    static const char *const devices[] = {"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"};
    char stem[32]; size_t n = 0U, i;
    while (name[n] && name[n] != '.' && n + 1U < sizeof(stem)) { stem[n] = dos33_fold(name[n]); ++n; }
    stem[n] = 0;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i) if (dos33_equal(stem, devices[i])) return true;
    return n == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') || (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T'));
}
static bool dos33_name(dos33_view *view, dos33_member *member, const uint8_t *raw) {
    char original[32]; size_t n = 30U, i; unsigned attempt;
    while (n && (raw[n - 1U] & 0x7FU) == ' ') --n;
    if (!n) return false;
    for (i = 0U; i < n; ++i) {
        unsigned char c = raw[i] & 0x7FU;
        original[i] = c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' || c == '<' ||
                      c == '>' || c == '"' || c == '|' || c == '?' || c == '*' ? '_' : (char)c;
    }
    for (i = n; i && (original[i - 1U] == '.' || original[i - 1U] == ' '); --i) original[i - 1U] = '_';
    original[n] = 0;
    if (dos33_device_name(original)) {
        for (i = n + 1U; i; --i) { original[i] = original[i - 1U]; } original[0] = '_';
    }
    for (attempt = 0U; attempt <= view->count; ++attempt) {
        bool taken = false;
        if (!attempt) xx_rt_snprintf(member->name, sizeof(member->name), "%s", original);
        else xx_rt_snprintf(member->name, sizeof(member->name), "%s~%u", original, attempt + 1U);
        for (i = 0U; i < view->count; ++i) if (dos33_equal(member->name, view->members[i].name)) { taken = true; break; }
        if (!taken) return true;
    }
    return false;
}
static bool dos33_logical_read(const dos33_view *view, const dos33_member *member,
    uint32_t offset, void *buffer, size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (offset > member->sectors * DOS33_SECTOR || size > member->sectors * DOS33_SECTOR - offset) return false;
    while (done < size) {
        uint32_t at = offset + (uint32_t)done, logical = at / DOS33_SECTOR;
        size_t left = 0U, right = member->pair_count;
        size_t part = DOS33_SECTOR - at % DOS33_SECTOR;
        if (part > size - done) part = size - done;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (left < right) {
            size_t mid = left + (right - left) / 2U;
            if (view->pairs[member->pair_start + mid].logical < logical) left = mid + 1U;
            else right = mid;
        }
        if (left < member->pair_count && view->pairs[member->pair_start + left].logical == logical) {
            uint16_t physical = view->pairs[member->pair_start + left].physical;
            if (!dos33_read_at(view, dos33_physical(view, physical) + at % DOS33_SECTOR,
                (uint8_t *)buffer + done, part, pd)) return false;
        } else xx_mem_zero((uint8_t *)buffer + done, part);
        done += part;
    }
    return true;
}
static bool dos33_file(dos33_view *view, dos33_member *member, xx_pd_struct *pd) {
    uint16_t list;
    uint32_t lists = 0U, sectors = 0U;
    uint8_t bytes[DOS33_SECTOR];
    if (!dos33_ts(view, member->first_track, member->first_sector, &list)) return false;
    member->pair_start = view->pair_count;
    for (;;) {
        uint32_t offset, i;
        if (!dos33_claim(view, list) || !dos33_sector(view, list, bytes, pd)) return false;
        ++sectors; offset = xx_data_get_u16(bytes + 5, 2, 0, false);
        if (offset != lists * 122U || offset >= DOS33_LOGICAL_SECTORS) return false;
        ++lists;
        for (i = 0U; i < 122U; ++i) {
            uint8_t track = bytes[12U + i * 2U], sector = bytes[13U + i * 2U];
            uint16_t index;
            if (!track) continue;
            if (offset + i >= DOS33_LOGICAL_SECTORS || !dos33_ts(view, track, sector, &index) ||
                !dos33_claim(view, index) || view->pair_count >= DOS33_MAX_SECTORS) return false;
            view->pairs[view->pair_count].logical = offset + i;
            view->pairs[view->pair_count++].physical = index;
            ++member->pair_count; ++sectors; member->sectors = offset + i + 1U;
        }
        if (!bytes[1]) break;
        if (!dos33_ts(view, bytes[1], bytes[2], &list)) return false;
    }
    if (sectors != member->declared_sectors) return false;
    member->sparse = member->pair_count != member->sectors;
    member->size = member->sectors * DOS33_SECTOR; member->skip = 0U;
    if (view->mode == XX_APPLE_DOS33_MODE_RAW_SECTORS || !member->sectors) return true;
    if ((member->type & 0x7FU) == 1U || (member->type & 0x7FU) == 2U || (member->type & 0x7FU) == 4U) {
        uint32_t prefix = (member->type & 0x7FU) == 4U ? 4U : 2U;
        if (!member->pair_count || view->pairs[member->pair_start].logical != 0U ||
            !dos33_logical_read(view, member, 0U, bytes, prefix, pd)) return false;
        member->skip = prefix; member->size = xx_data_get_u16(bytes + prefix - 2U, 2, 0, false);
        return member->size <= member->sectors * DOS33_SECTOR - prefix;
    }
    if (!(member->type & 0x7FU) && !member->sparse) {
        uint32_t at;
        for (at = 0U; at < member->sectors * DOS33_SECTOR; at += DOS33_SECTOR) {
            size_t i;
            if (!dos33_logical_read(view, member, at, bytes, sizeof(bytes), pd)) return false;
            for (i = 0U; i < sizeof(bytes); ++i)
                if (!bytes[i]) { member->size = at + (uint32_t)i; return true; }
        }
    }
    return true;
}
static bool dos33_parse_order(Abstractformat *self, xx_apple_dos33_order order,
    xx_apple_dos33_mode mode, dos33_view *view, xx_pd_struct *pd) {
    uint8_t vtoc[DOS33_SECTOR], catalog[DOS33_SECTOR];
    uint16_t next;
    uint32_t i;
    int64_t total;
    bool finished = false;
    if (!self || !self->device || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address || total - self->base_address < 18U * DOS33_TRACK) return false;
    xx_mem_zero(view, sizeof(*view)); view->device = self->device; view->base = self->base_address;
    view->bytes = 18U * DOS33_TRACK; view->tracks = 18U; view->order = order; view->mode = mode;
    if (!dos33_sector(view, 17U * 16U, vtoc, pd)) return false;
    view->tracks = vtoc[52]; view->volume = vtoc[6]; view->bytes = view->tracks * DOS33_TRACK;
    if (vtoc[3] != 3U || vtoc[39] != 122U || view->tracks < 18U || view->tracks > 80U ||
        vtoc[53] != 16U || xx_data_get_u16(vtoc + 54, 2, 0, false) != DOS33_SECTOR || !view->volume || view->volume == 255U ||
        vtoc[48] >= view->tracks || (vtoc[49] != 1U && vtoc[49] != 255U) ||
        view->bytes > (uint64_t)(total - view->base)) return false;
    xx_mem_copy(view->bitmap, vtoc + 56, sizeof(view->bitmap));
    if (!dos33_claim(view, 17U * 16U) || !dos33_ts(view, vtoc[1], vtoc[2], &next)) return false;
    /* Claim all catalog sectors before files, including linked sectors after
     * the first never-used entry. Garbage catalog slots are never listed. */
    for (;;) {
        size_t slot;
        if (!dos33_claim(view, next) || !dos33_sector(view, next, catalog, pd)) return false;
        for (slot = 0U; slot < 7U && !finished; ++slot) {
            const uint8_t *entry = catalog + 11U + slot * 35U;
            dos33_member *member;
            uint8_t type = entry[2] & 0x7FU;
            if (!entry[0]) { finished = true; break; }
            if (entry[0] == 255U) continue;
            if (view->count >= DOS33_MAX_SECTORS || (type && (type & (type - 1U)))) return false;
            member = &view->members[view->count];
            member->header = dos33_physical(view, next) + 11U + (uint32_t)slot * 35U;
            member->first_track = entry[0]; member->first_sector = entry[1]; member->type = entry[2];
            member->declared_sectors = xx_data_get_u16(entry + 33, 2, 0, false);
            if (!member->declared_sectors || !dos33_name(view, member, entry + 3U)) return false;
            ++view->count;
        }
        if (!catalog[1]) break;
        if (!dos33_ts(view, catalog[1], catalog[2], &next)) return false;
    }
    for (i = 0U; i < view->count; ++i) if (!dos33_file(view, &view->members[i], pd)) return false;
    return !(pd && xx_pd_is_stopped(pd));
}
static dos33_view *dos33_parse(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_apple_dos33 *volume = (xx_apple_dos33 *)self;
    dos33_view *view, *other;
    bool dos_ok, prodos_ok;
    if (!self || (volume->mode != XX_APPLE_DOS33_MODE_LOGICAL && volume->mode != XX_APPLE_DOS33_MODE_RAW_SECTORS)) return NULL;
    if (!dos33_limit(self, options, XX_META_ID_OPT_MEMORY_LIMIT,
        sizeof(*view) * (volume->requested_order == XX_APPLE_DOS33_ORDER_AUTO ? 2U : 1U))) return NULL;
    view = (dos33_view *)xx_mem_alloc(sizeof(*view)); if (!view) return NULL;
    if (volume->requested_order == XX_APPLE_DOS33_ORDER_DOS || volume->requested_order == XX_APPLE_DOS33_ORDER_PRODOS) {
        if (dos33_parse_order(self, volume->requested_order, volume->mode, view, pd)) return view;
        xx_mem_free(view); return NULL;
    }
    if (volume->requested_order != XX_APPLE_DOS33_ORDER_AUTO) { xx_mem_free(view); return NULL; }
    other = (dos33_view *)xx_mem_alloc(sizeof(*other)); if (!other) { xx_mem_free(view); return NULL; }
    dos_ok = dos33_parse_order(self, XX_APPLE_DOS33_ORDER_DOS, volume->mode, view, pd);
    prodos_ok = dos33_parse_order(self, XX_APPLE_DOS33_ORDER_PRODOS, volume->mode, other, pd);
    if (!dos_ok && prodos_ok) *view = *other;
    xx_mem_free(other);
    if (dos_ok == prodos_ok || (pd && xx_pd_is_stopped(pd))) { xx_mem_free(view); return NULL; }
    return view;
}
static void dos33_vtable_destroy(Abstractformat *self) { xx_apple_dos33_destroy((xx_apple_dos33 *)self); }
void xx_apple_dos33_init_ex(xx_apple_dos33 *volume, xx_io_device *device,
    int64_t base_address, xx_apple_dos33_order order, xx_apple_dos33_mode mode) {
    if (!volume) return;
    xx_mem_zero(volume, sizeof(*volume)); xx_format_init(&volume->format, device, base_address);
    volume->requested_order = order; volume->mode = mode;
    volume->format.endian = XX_ENDIAN_LITTLE; volume->format.file_type = DOS33_TYPE;
    volume->format.format_type = XX_TYPE_ARCHIVE; volume->format.is_archive = true;
    xx_format_set_mime_type(&volume->format, "application/x-apple-dos33-fs"); xx_format_set_extension(&volume->format, "do");
    volume->format.check_is_valid = xx_apple_dos33_check_is_valid; volume->format.handle_base_info = xx_apple_dos33_handle_base_info;
    volume->format.get_format_size = xx_apple_dos33_get_format_size;
    volume->format.get_number_of_archive_records = xx_apple_dos33_get_number_of_archive_records;
    volume->format.create_archive_records_reading = xx_apple_dos33_create_archive_records_reading;
    volume->format.get_current_archive_record = xx_apple_dos33_get_current_archive_record;
    volume->format.archive_record_move_to_next = xx_apple_dos33_archive_record_move_to_next;
    volume->format.unpack_current_archive_record = xx_apple_dos33_unpack_current_archive_record;
    volume->format.free_archive_records_reading = xx_apple_dos33_free_archive_records_reading;
    volume->format.destroy = dos33_vtable_destroy;
}
void xx_apple_dos33_init(xx_apple_dos33 *volume, xx_io_device *device, int64_t base_address) {
    xx_apple_dos33_init_ex(volume, device, base_address, XX_APPLE_DOS33_ORDER_AUTO, XX_APPLE_DOS33_MODE_LOGICAL);
}
xx_apple_dos33 *xx_apple_dos33_create(xx_io_device *device, int64_t base_address) {
    xx_apple_dos33 *volume = (xx_apple_dos33 *)xx_mem_alloc(sizeof(*volume));
    if (volume) { xx_apple_dos33_init(volume, device, base_address); } return volume;
}
void xx_apple_dos33_destroy(xx_apple_dos33 *volume) { if (volume) xx_format_cleanup_extra_parameters(&volume->format); }
void xx_apple_dos33_free(xx_apple_dos33 *volume) { if (volume) { xx_apple_dos33_destroy(volume); xx_mem_free(volume); } }
bool xx_apple_dos33_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    dos33_view *view = dos33_parse(self, NULL, pd); bool ok = view != NULL; xx_mem_free(view); return ok;
}
bool xx_apple_dos33_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_apple_dos33 *volume = (xx_apple_dos33 *)self; dos33_view *view = dos33_parse(self, NULL, pd); int64_t total, end;
    if (!self) return false;
    if (!view) { self->is_valid = false; self->base_info_handled = false; return false; }
    volume->number_of_records = view->count; volume->track_count = view->tracks; volume->volume_number = view->volume;
    volume->detected_order = view->order; self->format_size = view->bytes; self->number_of_archive_records = view->count;
    total = xx_io_total_size(self->device); end = self->base_address + view->bytes;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true; xx_mem_free(view); return true;
}
int64_t xx_apple_dos33_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return xx_apple_dos33_handle_base_info(self, pd) ? self->format_size : -1;
}
uint64_t xx_apple_dos33_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) {
    return xx_apple_dos33_handle_base_info(self, pd) ? ((xx_apple_dos33 *)self)->number_of_records : 0U;
}
static bool dos33_record(xx_archive_record *record, const dos33_view *view, const dos33_member *member) {
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = view->base + member->header; record->header_size = 35;
    record->data_offset = member->pair_count ? view->base + dos33_physical(view,
        view->pairs[member->pair_start].physical) + member->skip : -1;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->type) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}
static bool dos33_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i); xx_meta copy;
        if (!item) { continue; } xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
static void dos33_view_free(void *view) { xx_mem_free(view); }
xx_archive_record_state *xx_apple_dos33_create_archive_records_reading(Abstractformat *self,
    const xx_list_s *options, xx_pd_struct *pd) {
    dos33_view *view; xx_archive_record_state *state;
    if (!self || !dos33_limit(self, options, XX_META_ID_OPT_MEMORY_LIMIT,
        sizeof(dos33_view) + sizeof(*state))) return NULL;
    view = dos33_parse(self, options, pd);
    if (!view) { return NULL; } state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { xx_mem_free(view); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = view; state->free_internal = dos33_view_free;
    state->total_records = (int64_t)view->count;
    if (!dos33_options(&state->options, options) || (view->count && !dos33_record(&state->current_record, view, view->members))) {
        xx_archive_record_state_free(state); return NULL;
    }
    state->has_record = view->count != 0U; state->current_index = view->count ? 0 : -1; return state;
}
const xx_archive_record *xx_apple_dos33_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_apple_dos33_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    dos33_view *view;
    if (!self || !state || state->format != self || !state->has_record ||
        !(view = (dos33_view *)state->internal_state) || (pd && xx_pd_is_stopped(pd))) return false;
    if (view->index + 1U >= view->count) {
        xx_archive_record_cleanup(&state->current_record); xx_archive_record_init(&state->current_record);
        state->has_record = false; return false;
    }
    if (!dos33_record(&state->current_record, view, &view->members[view->index + 1U])) { state->has_record = false; return false; }
    ++view->index; ++state->current_index; return true;
}
static bool dos33_extraction_limits(Abstractformat *self, xx_archive_record_state *state,
    const dos33_view *view, size_t *buffer_size) {
    const dos33_member *member = &view->members[view->index];
    *buffer_size = member->size;
    if (*buffer_size > 65536U) *buffer_size = 65536U;
    return dos33_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, member->size) &&
        dos33_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT,
            sizeof(*view) + sizeof(*state) + *buffer_size);
}
bool xx_apple_dos33_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd) {
    dos33_view *view; const dos33_member *member; uint8_t *buffer; uint32_t done = 0U;
    size_t buffer_size; bool ok = true;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record ||
        !(view = (dos33_view *)state->internal_state) || view->index >= view->count || (pd && xx_pd_is_stopped(pd))) return false;
    member = &view->members[view->index];
    if (!dos33_extraction_limits(self, state, view, &buffer_size)) return false;
    if (!buffer_size) return true;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size); if (!buffer) return false;
    while (done < member->size) {
        size_t part = member->size - done, written = 0U;
        if (part > buffer_size) part = buffer_size;
        if (!dos33_logical_read(view, member, member->skip + done, buffer, part, pd)) { ok = false; break; }
        while (destination && written < part && !(pd && xx_pd_is_stopped(pd))) {
            ssize_t got = xx_io_write(destination, buffer + written, part - written);
            if (got <= 0 || (size_t)got > part - written) { ok = false; break; } written += (size_t)got;
        }
        if (!ok || (pd && xx_pd_is_stopped(pd))) { ok = false; break; } done += (uint32_t)part;
    }
    xx_mem_free(buffer); return ok;
}
static xx_io_device *dos33_stage(const char *destination, char **stage_path) {
    unsigned attempt; size_t i, parent = 0U; char *directory = xx_str_dup(destination);
    *stage_path = NULL; if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i) if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate; xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_dos33.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix); if (!candidate) break;
        if (dos33_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; } xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_apple_dos33_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    dos33_view *view; const xx_var *option, *overwrite_option; const char *base = NULL;
    char *owned = NULL, *path = NULL, *staged = NULL; bool ok = false, overwrite;
    size_t buffer_size;
    if (!self || !self->device || !state || state->format != self || !state->has_record ||
        !(view = (dos33_view *)state->internal_state) || view->index >= view->count || (pd && xx_pd_is_stopped(pd))) return false;
    if (!dos33_extraction_limits(self, state, view, &buffer_size)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return xx_apple_dos33_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base = owned;
    }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\')
        ? xx_str_concat3(base, "/", view->members[view->index].name) : xx_str_concat(base, view->members[view->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = dos33_stage(path, &staged); if (!output) goto done;
        ok = xx_apple_dos33_extract_record_to_device(self, state, output, pd); if (xx_io_close(output)) ok = false;
    }
    if (pd && xx_pd_is_stopped(pd)) { ok = false; } if (ok) ok = xx_io_file_replace_a(staged, path, overwrite);
done:
    if (!ok && staged) xx_io_file_remove_a(staged);
    xx_str_free(staged); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_apple_dos33_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) {
    (void)self; xx_archive_record_state_free(state);
}
