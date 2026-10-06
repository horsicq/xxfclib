/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native implementation of Apple's published MFS disk layout.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mfs/xx_mfs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#ifdef MFS
#define MFS_TYPE XX_FILE_TYPE_MFS
#else
#define MFS_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define MFS_BLOCK 512U
#define MFS_MDB 1024U
#define MFS_UNITS 640U
#define MFS_NAME 256U
#define MFS_COPY 65536U
#define MFS_NAME_WORK 1000000U
static const uint16_t mfs_macroman[128] = {
    0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
    0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
    0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
    0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
    0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
    0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
    0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
    0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
    0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
    0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
    0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
    0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
    0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
    0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
    0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
    0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7
};
typedef struct mfs_member_s {
    char name[MFS_NAME];
    uint64_t header;
    uint32_t size, physical, id;
    uint32_t type, creator;
    uint16_t chain_start, chain_count, header_size;
    uint8_t flags, version;
    bool resource;
} mfs_member;
typedef struct mfs_view_s {
    xx_io_device *device;
    int64_t base;
    uint64_t bytes, heap;
    uint32_t unit_size;
    uint16_t units, files;
    uint16_t map[MFS_UNITS], chain[MFS_UNITS];
    uint8_t claimed[MFS_UNITS];
    uint16_t chain_count;
    char volume_name[82];
    mfs_member *members;
    size_t count, index, capacity;
    uint32_t *names, *ids;
    size_t hash_capacity;
    uint32_t name_work;
} mfs_view;
static uint16_t mfs_u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t mfs_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static bool mfs_read(const mfs_view *view, uint64_t offset, void *buffer,
                       size_t size, xx_pd_struct *pd) {
    int64_t saved;
    size_t done = 0U;
    bool ok = false;
    if (!view || !view->device || offset > view->bytes || size > view->bytes - offset ||
        offset > (uint64_t)(INT64_MAX - view->base) || (pd && xx_pd_is_stopped(pd)))
        return false;
    saved = xx_io_tell(view->device);
    if (saved < 0) return false;
    if (!xx_io_seek64(view->device, view->base + (int64_t)offset, SEEK_SET)) {
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
static size_t mfs_utf8(char *output, uint32_t c) {
    if (c < 0x80U) { output[0] = (char)c; return 1U; }
    if (c < 0x800U) {
        output[0] = (char)(0xC0U | (c >> 6)); output[1] = (char)(0x80U | (c & 63U)); return 2U;
    }
    output[0] = (char)(0xE0U | (c >> 12)); output[1] = (char)(0x80U | ((c >> 6) & 63U));
    output[2] = (char)(0x80U | (c & 63U)); return 3U;
}
static uint32_t mfs_next(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    uint32_t c = *p++;
    if (c >= 0xE0U) { c = (c & 15U) << 12; c |= (*p++ & 63U) << 6; c |= *p++ & 63U; }
    else if (c >= 0xC0U) { c = (c & 31U) << 6; c |= *p++ & 63U; }
    *text = (const char *)p;
    if (c >= 'a' && c <= 'z') c -= 32U;
    else if (c >= 0xE0U && c <= 0xFEU && c != 0xF7U) c -= 32U;
    else if (c == 0xFFU) c = 0x178U;
    else if (c == 0x153U) c = 0x152U;
    else if (c == 0x131U) c = 'I';
    else if (c == 0x3C0U) c = 0x3A0U;
    return c;
}
static uint32_t mfs_hash(const char *name) {
    uint32_t hash = UINT32_C(2166136261);
    while (*name) { hash ^= mfs_next(&name); hash *= UINT32_C(16777619); }
    return hash;
}
static bool mfs_equal(const char *a, const char *b) {
    while (*a && *b) if (mfs_next(&a) != mfs_next(&b)) return false;
    return !*a && !*b;
}
static bool mfs_device_name(const char *name) {
    static const char *const devices[] = {"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"};
    char stem[16]; size_t n = 0U, i;
    while (name[n] && name[n] != '.' && n + 1U < sizeof(stem)) { stem[n] = name[n]; ++n; }
    stem[n] = 0;
    if (name[n] && name[n] != '.') return false;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (mfs_equal(stem, devices[i])) return true;
    return n == 4U && stem[3] >= '0' && stem[3] <= '9' &&
           (((stem[0] == 'C' || stem[0] == 'c') && (stem[1] == 'O' || stem[1] == 'o') &&
             (stem[2] == 'M' || stem[2] == 'm')) ||
            ((stem[0] == 'L' || stem[0] == 'l') && (stem[1] == 'P' || stem[1] == 'p') &&
             (stem[2] == 'T' || stem[2] == 't')));
}
static bool mfs_name(const uint8_t *raw, size_t n, char *output, bool host) {
    size_t i, at = 0U;
    bool truncated = false;
    if (!n || n > (host ? 255U : 27U)) return false;
    for (i = 0U; i < n; ++i) {
        uint32_t c = raw[i] < 0x80U ? raw[i] : mfs_macroman[raw[i] - 0x80U];
        char encoded[3]; size_t length;
        if (!c) return false;
        if (host && (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')) c = '_';
        length = mfs_utf8(encoded, c);
        if (host && at + length > 240U) truncated = true;
        if (truncated) continue;
        xx_mem_copy(output + at, encoded, length); at += length;
    }
    if (host) {
        for (i = at; i && (output[i - 1U] == '.' || output[i - 1U] == ' '); --i) output[i - 1U] = '_';
    }
    output[at] = 0;
    if (host && mfs_device_name(output)) {
        for (i = at + 1U; i; --i) output[i] = output[i - 1U];
        output[0] = '_';
    }
    return true;
}
static void mfs_view_free(void *pointer) {
    mfs_view *view = (mfs_view *)pointer;
    if (!view) return;
    xx_mem_free(view->members); xx_mem_free(view->names); xx_mem_free(view->ids); xx_mem_free(view);
}
static bool mfs_id(mfs_view *view, uint32_t id) {
    size_t slot = (id * UINT32_C(2654435761)) & (view->hash_capacity - 1U), visited = 0U;
    if (!id) return false;
    while (view->ids[slot]) {
        if (view->ids[slot] == id || ++visited >= view->hash_capacity) return false;
        slot = (slot + 1U) & (view->hash_capacity - 1U);
    }
    view->ids[slot] = id; return true;
}
static bool mfs_append(mfs_view *view, mfs_member *member, const char *component,
                         xx_pd_struct *pd) {
    unsigned attempt = 0U;
    if (view->count >= view->capacity) return false;
    for (;;) {
        size_t slot;
        bool taken = false;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!attempt) xx_rt_snprintf(member->name, sizeof(member->name), "%s%s", component, member->resource ? ".rsrc" : "");
        else xx_rt_snprintf(member->name, sizeof(member->name), "%s%s~%u", component, member->resource ? ".rsrc" : "", attempt + 1U);
        slot = mfs_hash(member->name) & (view->hash_capacity - 1U);
        while (view->names[slot]) {
            if (++view->name_work > MFS_NAME_WORK) return false;
            if (mfs_equal(member->name, view->members[view->names[slot] - 1U].name)) { taken = true; break; }
            slot = (slot + 1U) & (view->hash_capacity - 1U);
        }
        if (!taken) {
            view->members[view->count] = *member;
            view->names[slot] = (uint32_t)(++view->count); return true;
        }
        if (++attempt > view->count) return false;
    }
}
static bool mfs_chain(mfs_view *view, mfs_member *member, uint16_t first,
                        uint32_t logical, uint32_t physical, xx_pd_struct *pd) {
    uint32_t count, i;
    uint16_t block = first;
    if (logical > physical || physical % view->unit_size ||
        physical / view->unit_size > view->units || (!physical ? first != 0U : first < 2U)) return false;
    count = physical / view->unit_size;
    member->size = logical; member->physical = physical;
    member->chain_start = view->chain_count; member->chain_count = (uint16_t)count;
    for (i = 0U; i < count; ++i) {
        uint16_t next;
        if ((pd && xx_pd_is_stopped(pd)) || block < 2U || block - 2U >= view->units ||
            view->claimed[block - 2U] || view->chain_count >= MFS_UNITS) return false;
        next = view->map[block - 2U];
        if (!next || next == 0xFFFU || (i + 1U == count ? next != 1U : next < 2U)) return false;
        view->claimed[block - 2U] = 1U; view->chain[view->chain_count++] = block;
        block = next;
    }
    return true;
}
static mfs_view *mfs_parse(Abstractformat *self, xx_pd_struct *pd) {
    mfs_view *view = NULL;
    uint8_t mdb[MFS_MDB], sector[MFS_BLOCK];
    uint64_t available, directory, directory_end, end;
    uint32_t sectors, seen = 0U, free_blocks = 0U;
    uint32_t i;
    int64_t total;
    if (!self || !self->device || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address || total - self->base_address < 4 * MFS_BLOCK) return NULL;
    available = (uint64_t)(total - self->base_address);
    view = (mfs_view *)xx_mem_alloc(sizeof(*view));
    if (!view) return NULL;
    xx_mem_zero(view, sizeof(*view)); view->device = self->device; view->base = self->base_address; view->bytes = available;
    if (!mfs_read(view, 2U * MFS_BLOCK, mdb, sizeof(mdb), pd)) goto fail;
    view->files = mfs_u16(mdb + 12); view->units = mfs_u16(mdb + 18);
    view->unit_size = mfs_u32(mdb + 20); view->heap = (uint64_t)mfs_u16(mdb + 28) * MFS_BLOCK;
    directory = (uint64_t)mfs_u16(mdb + 14) * MFS_BLOCK;
    sectors = mfs_u16(mdb + 16); directory_end = directory + (uint64_t)sectors * MFS_BLOCK;
    if (mfs_u16(mdb) != 0xD2D7U || !view->units || view->units > MFS_UNITS ||
        !view->unit_size || view->unit_size % MFS_BLOCK || mfs_u32(mdb + 24) % view->unit_size ||
        !sectors || directory < 4U * MFS_BLOCK || directory_end > available || view->heap < 4U * MFS_BLOCK ||
        view->heap + (uint64_t)view->units * view->unit_size > available ||
        view->files > (uint64_t)sectors * 9U || mfs_u16(mdb + 34) > view->units ||
        !mfs_name(mdb + 37, mdb[36], view->volume_name, false)) goto fail;
    end = view->heap + (uint64_t)view->units * view->unit_size;
    if (directory_end > end) end = directory_end;
    for (i = 0U; i < view->units; ++i) {
        size_t p = 64U + (i / 2U) * 3U;
        uint16_t value = i & 1U ? (uint16_t)(((mdb[p + 1U] & 15U) << 8) | mdb[p + 2U])
                               : (uint16_t)((mdb[p] << 4) | (mdb[p + 1U] >> 4));
        uint64_t start = view->heap + (uint64_t)i * view->unit_size;
        bool reserved = start < directory_end && start + view->unit_size > directory;
        if (value > 1U && value != 0xFFFU && (value < 2U || value - 2U >= view->units)) goto fail;
        if (reserved) { if (value != 0xFFFU) goto fail; view->claimed[i] = 1U; }
        view->map[i] = value;
        if (!value) ++free_blocks;
    }
    if (free_blocks != mfs_u16(mdb + 34)) goto fail;
    /* Recognize a matching backup MDB immediately after described storage.
     * Missing or stale backups do not supply an alternate primary directory. */
    if (available - end >= MFS_MDB && mfs_read(view, end, sector, sizeof(sector), pd) &&
        mfs_u16(sector) == 0xD2D7U && mfs_u16(sector + 14) == mfs_u16(mdb + 14) &&
        mfs_u16(sector + 16) == sectors && mfs_u16(sector + 18) == view->units &&
        mfs_u32(sector + 20) == view->unit_size && mfs_u16(sector + 28) == mfs_u16(mdb + 28))
        end += MFS_MDB;
    view->capacity = (size_t)view->files * 2U;
    view->hash_capacity = 8U;
    while (view->hash_capacity < view->capacity * 2U) view->hash_capacity *= 2U;
    if (view->capacity > SIZE_MAX / sizeof(mfs_member) || view->hash_capacity > SIZE_MAX / sizeof(uint32_t)) goto fail;
    if (view->capacity) {
        view->members = (mfs_member *)xx_mem_alloc(view->capacity * sizeof(mfs_member));
        if (!view->members) goto fail;
    }
    view->names = (uint32_t *)xx_mem_alloc(view->hash_capacity * sizeof(uint32_t));
    view->ids = (uint32_t *)xx_mem_alloc(view->hash_capacity * sizeof(uint32_t));
    if (!view->names || !view->ids) goto fail;
    xx_mem_zero(view->names, view->hash_capacity * sizeof(uint32_t));
    xx_mem_zero(view->ids, view->hash_capacity * sizeof(uint32_t));
    for (i = 0U; i < sectors; ++i) {
        size_t at = 0U;
        if (!mfs_read(view, directory + (uint64_t)i * MFS_BLOCK, sector, sizeof(sector), pd)) goto fail;
        while (at < MFS_BLOCK && sector[at]) {
            const uint8_t *entry = sector + at;
            size_t entry_size;
            mfs_member data, resource;
            char name[MFS_NAME];
            if ((pd && xx_pd_is_stopped(pd)) || MFS_BLOCK - at < 51U ||
                !entry[50] || entry[50] > MFS_BLOCK - at - 51U) goto fail;
            entry_size = (51U + entry[50] + 1U) & ~(size_t)1U;
            if (entry_size > MFS_BLOCK - at) goto fail;
            if (!(entry[0] & 0x80U)) { at += entry_size; continue; }
            if (++seen > view->files || !mfs_name(entry + 51, entry[50], name, true)) goto fail;
            xx_mem_zero(&data, sizeof(data)); data.id = mfs_u32(entry + 18);
            if (!mfs_id(view, data.id)) goto fail;
            data.header = directory + (uint64_t)i * MFS_BLOCK + at;
            data.header_size = (uint16_t)entry_size;
            data.flags = entry[0]; data.version = entry[1];
            data.type = mfs_u32(entry + 2); data.creator = mfs_u32(entry + 6);
            resource = data; resource.resource = true;
            if (!mfs_chain(view, &data, mfs_u16(entry + 22), mfs_u32(entry + 24), mfs_u32(entry + 28), pd) ||
                !mfs_chain(view, &resource, mfs_u16(entry + 32), mfs_u32(entry + 34), mfs_u32(entry + 38), pd) ||
                !mfs_append(view, &data, name, pd) ||
                (resource.physical && !mfs_append(view, &resource, name, pd))) goto fail;
            at += entry_size;
        }
    }
    if (seen != view->files || (pd && xx_pd_is_stopped(pd))) goto fail;
    view->bytes = end;
    xx_mem_free(view->names); view->names = NULL;
    xx_mem_free(view->ids); view->ids = NULL;
    return view;
fail:
    mfs_view_free(view); return NULL;
}
static void mfs_vtable_destroy(Abstractformat *self) { xx_mfs_destroy((xx_mfs *)self); }
void xx_mfs_init(xx_mfs *volume, xx_io_device *device, int64_t base_address) {
    if (!volume) return;
    xx_mem_zero(volume, sizeof(*volume)); xx_format_init(&volume->format, device, base_address);
    volume->format.endian = XX_ENDIAN_BIG; volume->format.file_type = MFS_TYPE;
    volume->format.format_type = XX_TYPE_ARCHIVE; volume->format.is_archive = true;
    xx_format_set_mime_type(&volume->format, "application/x-mfs-fs"); xx_format_set_extension(&volume->format, "img");
    volume->format.check_is_valid = xx_mfs_check_is_valid; volume->format.handle_base_info = xx_mfs_handle_base_info;
    volume->format.get_format_size = xx_mfs_get_format_size;
    volume->format.get_number_of_archive_records = xx_mfs_get_number_of_archive_records;
    volume->format.create_archive_records_reading = xx_mfs_create_archive_records_reading;
    volume->format.get_current_archive_record = xx_mfs_get_current_archive_record;
    volume->format.archive_record_move_to_next = xx_mfs_archive_record_move_to_next;
    volume->format.unpack_current_archive_record = xx_mfs_unpack_current_archive_record;
    volume->format.free_archive_records_reading = xx_mfs_free_archive_records_reading;
    volume->format.destroy = mfs_vtable_destroy;
}
xx_mfs *xx_mfs_create(xx_io_device *device, int64_t base_address) {
    xx_mfs *volume = (xx_mfs *)xx_mem_alloc(sizeof(*volume));
    if (volume) { xx_mfs_init(volume, device, base_address); } return volume;
}
void xx_mfs_destroy(xx_mfs *volume) { if (volume) xx_format_cleanup_extra_parameters(&volume->format); }
void xx_mfs_free(xx_mfs *volume) { if (volume) { xx_mfs_destroy(volume); xx_mem_free(volume); } }
bool xx_mfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    mfs_view *view = mfs_parse(self, pd); bool ok = view != NULL; mfs_view_free(view); return ok;
}
bool xx_mfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_mfs *volume = (xx_mfs *)self;
    mfs_view *view = mfs_parse(self, pd);
    int64_t total, end;
    if (!self) return false;
    if (!view) { self->is_valid = false; self->base_info_handled = false; return false; }
    volume->number_of_records = view->count; volume->volume_size = view->bytes;
    volume->allocation_block_size = view->unit_size; volume->allocation_block_count = view->units; volume->file_count = view->files;
    xx_mem_copy(volume->volume_name, view->volume_name, sizeof(volume->volume_name));
    self->format_size = (int64_t)view->bytes; self->number_of_archive_records = view->count;
    total = xx_io_total_size(self->device); end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true; mfs_view_free(view); return true;
}
int64_t xx_mfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return xx_mfs_handle_base_info(self, pd) ? self->format_size : -1;
}
uint64_t xx_mfs_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) {
    return xx_mfs_handle_base_info(self, pd) ? ((xx_mfs *)self)->number_of_records : 0U;
}
static bool mfs_record(xx_archive_record *record, const mfs_view *view, const mfs_member *member) {
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = view->base + (int64_t)member->header; record->header_size = member->header_size;
    record->data_offset = member->chain_count ? view->base + (int64_t)(view->heap +
        (uint64_t)(view->chain[member->chain_start] - 2U) * view->unit_size) : -1;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->flags) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}
static bool mfs_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i); xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
xx_archive_record_state *xx_mfs_create_archive_records_reading(Abstractformat *self,
    const xx_list_s *options, xx_pd_struct *pd) {
    mfs_view *view = mfs_parse(self, pd); xx_archive_record_state *state;
    if (!view) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { mfs_view_free(view); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = view; state->free_internal = mfs_view_free;
    state->total_records = (int64_t)view->count;
    if (!mfs_options(&state->options, options) || (view->count && !mfs_record(&state->current_record, view, view->members))) {
        xx_archive_record_state_free(state); return NULL;
    }
    state->has_record = view->count != 0U; state->current_index = view->count ? 0 : -1; return state;
}
const xx_archive_record *xx_mfs_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_mfs_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    mfs_view *view;
    if (!self || !state || state->format != self || !state->has_record ||
        !(view = (mfs_view *)state->internal_state) || (pd && xx_pd_is_stopped(pd))) return false;
    if (view->index + 1U >= view->count) {
        xx_archive_record_cleanup(&state->current_record); xx_archive_record_init(&state->current_record);
        state->has_record = false; return false;
    }
    if (!mfs_record(&state->current_record, view, &view->members[view->index + 1U])) { state->has_record = false; return false; }
    ++view->index; ++state->current_index; return true;
}
static uint64_t mfs_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback) {
    const xx_var *var = xx_format_resolve_extra_parameter(self, options, id);
    if (!var) return fallback;
    switch (var->type) {
        case XX_VAR_TYPE_UINT8: case XX_VAR_TYPE_UINT16: case XX_VAR_TYPE_UINT32: case XX_VAR_TYPE_UINT64: return xx_var_get_u64(var);
        case XX_VAR_TYPE_INT8: case XX_VAR_TYPE_INT16: case XX_VAR_TYPE_INT32: case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(var); return value < 0 ? fallback : (uint64_t)value;
        }
        default: return fallback;
    }
}
static bool mfs_extract_limits(Abstractformat *self, xx_archive_record_state *state, const mfs_member *member, size_t *buffer_size) {
    const mfs_view *view = (const mfs_view *)state->internal_state;
    *buffer_size = member->size < MFS_COPY ? member->size : MFS_COPY;
    return member->size <= mfs_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX) &&
        (uint64_t)sizeof(*view) + (uint64_t)view->capacity * sizeof(*view->members) + sizeof(*state) + *buffer_size <=
            mfs_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
bool xx_mfs_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd) {
    mfs_view *view; const mfs_member *member; uint8_t *buffer;
    uint32_t done = 0U; size_t buffer_size;
    bool ok = true;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record ||
        !(view = (mfs_view *)state->internal_state) || view->index >= view->count || (pd && xx_pd_is_stopped(pd))) return false;
    member = &view->members[view->index];
    if (!mfs_extract_limits(self, state, member, &buffer_size)) return false;
    if (!buffer_size) return true;
    buffer = (uint8_t *)xx_mem_alloc(buffer_size);
    if (!buffer) return false;
    while (done < member->size) {
        uint32_t unit = done / view->unit_size, within = done % view->unit_size;
        uint64_t at;
        size_t part = view->unit_size - within, written = 0U;
        if (unit >= member->chain_count) { ok = false; break; }
        at = view->heap + (uint64_t)(view->chain[member->chain_start + unit] - 2U) * view->unit_size + within;
        if (part > buffer_size) part = buffer_size;
        if (part > member->size - done) part = member->size - done;
        if (!mfs_read(view, at, buffer, part, pd)) { ok = false; break; }
        while (destination && written < part && !(pd && xx_pd_is_stopped(pd))) {
            ssize_t got = xx_io_write(destination, buffer + written, part - written);
            if (got <= 0 || (size_t)got > part - written) { ok = false; break; }
            written += (size_t)got;
        }
        if (!ok || (pd && xx_pd_is_stopped(pd))) { ok = false; break; }
        done += (uint32_t)part;
    }
    xx_mem_free(buffer); return ok && !(pd && xx_pd_is_stopped(pd));
}
static xx_io_device *mfs_stage(const char *destination, char **stage_path) {
    unsigned attempt;
    size_t i, parent = 0U;
    char *directory = xx_str_dup(destination);
    *stage_path = NULL;
    if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i)
        if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate; xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_mfs.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (mfs_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; }
        xx_mem_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}
bool xx_mfs_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    mfs_view *view;
    const xx_var *parameter, *overwrite_parameter;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *staged = NULL;
    size_t buffer_size;
    bool ok = false, overwrite;
    if (!self || !state || state->format != self || !state->has_record ||
        !(view = (mfs_view *)state->internal_state) || view->index >= view->count ||
        (pd && xx_pd_is_stopped(pd))) return false;
    if (!mfs_extract_limits(self, state, view->members + view->index, &buffer_size)) return false;
    parameter = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_parameter = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_parameter && xx_var_get_bool(overwrite_parameter);
    if (!parameter) return xx_mfs_extract_record_to_device(self, state, NULL, pd);
    if (parameter->type == XX_VAR_TYPE_STRING || parameter->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(parameter);
    else if (parameter->type == XX_VAR_TYPE_WSTRING || parameter->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(parameter)); base = owned;
    }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\')
        ? xx_str_concat3(base, "/", view->members[view->index].name)
        : xx_str_concat(base, view->members[view->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = mfs_stage(path, &staged);
        if (!output) goto done;
        ok = xx_mfs_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output)) ok = false;
    }
    if (pd && xx_pd_is_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(staged, path, overwrite);
done:
    if (!ok && staged) xx_io_file_remove_a(staged);
    xx_str_free(staged); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_mfs_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) {
    (void)self; xx_archive_record_state_free(state);
}
