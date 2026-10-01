/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native implementation of Digital Research's published CP/M layout.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cpm/xx_cpm.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#ifdef CPM
#define CPM_TYPE XX_FILE_TYPE_CPM
#else
#define CPM_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define CPM_COPY 65536U
#define CPM_WORK 4000000U
typedef struct cpm_extent_s {
    uint8_t name[11], user, attributes, byte_count;
    uint16_t pointers[16];
    uint32_t group, used, slot;
} cpm_extent;
typedef struct cpm_member_s {
    char name[40]; uint32_t size, first, count, slot; uint8_t attributes;
} cpm_member;
typedef struct cpm_view_s {
    xx_io_device *device; int64_t base; uint64_t bytes, heap, retained_memory;
    xx_cpm_geometry geometry;
    uint32_t block_size, blocks, slots, span, work, extent_count;
    unsigned pointer_count;
    uint8_t *claimed;
    cpm_extent *extents;
    cpm_member *members;
    uint32_t *names; size_t hash_capacity;
    size_t count, index;
    char volume_name[16];
} cpm_view;
static uint16_t cpm_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static bool cpm_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool cpm_work(cpm_view *v, xx_pd_struct *pd) { return !cpm_stopped(pd) && ++v->work <= CPM_WORK; }
static uint64_t cpm_physical(const cpm_view *v, uint64_t logical) {
    uint64_t track_bytes = (uint64_t)v->geometry.physical_sector_size * v->geometry.physical_sectors_per_track;
    uint32_t within = (uint32_t)(logical % track_bytes), sector = within / v->geometry.physical_sector_size;
    return logical - within + (uint64_t)v->geometry.sector_order[sector] * v->geometry.physical_sector_size + within % v->geometry.physical_sector_size;
}
static bool cpm_read_at(const cpm_view *v, uint64_t physical, void *buffer, size_t size, xx_pd_struct *pd) {
    int64_t saved; size_t done = 0U; bool ok = false;
    if (physical > v->bytes || size > v->bytes - physical || physical > (uint64_t)(INT64_MAX - v->base) || cpm_stopped(pd)) return false;
    saved = xx_io_tell(v->device); if (saved < 0) return false;
    if (!xx_io_seek64(v->device, v->base + (int64_t)physical, SEEK_SET)) {
        while (done < size && !cpm_stopped(pd)) {
            ssize_t got = xx_io_read(v->device, (uint8_t *)buffer + done, size - done);
            if (got <= 0 || (size_t)got > size - done) break; done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(v->device, saved, SEEK_SET)) ok = false;
    return ok && !cpm_stopped(pd);
}
static bool cpm_read(cpm_view *v, uint64_t logical, void *buffer, size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (logical > v->bytes || size > v->bytes - logical) return false;
    while (done < size) {
        size_t part = v->geometry.physical_sector_size - (size_t)(logical % v->geometry.physical_sector_size);
        if (part > size - done) part = size - done;
        if (!cpm_read_at(v, cpm_physical(v, logical), (uint8_t *)buffer + done, part, pd)) return false;
        done += part; logical += part;
    }
    return true;
}
static bool cpm_geometry_valid(cpm_view *v, int64_t available) {
    xx_cpm_geometry *g = &v->geometry; uint8_t seen[256]; uint64_t track; uint32_t expected, directory_blocks, i; uint16_t reserved;
    if (!g->tracks || g->tracks > 65535U || !g->physical_sectors_per_track || g->physical_sectors_per_track > 256U || g->physical_sector_size < 128U ||
        g->physical_sector_size > 4096U || (g->physical_sector_size & (g->physical_sector_size - 1U)) || g->bsh < 3U || g->bsh > 8U ||
        (g->version != XX_CPM_VERSION_22 && g->version != XX_CPM_VERSION_3) || (g->length_mode != XX_CPM_LENGTH_RECORDS && g->length_mode != XX_CPM_LENGTH_LAST_RECORD_USED)) return false;
    track = (uint64_t)g->physical_sectors_per_track * g->physical_sector_size;
    if (g->spt != track / 128U || g->blm != (1U << g->bsh) - 1U || g->off >= g->tracks) return false;
    v->bytes = track * g->tracks; v->heap = track * g->off; v->block_size = 128U << g->bsh; v->blocks = (uint32_t)g->dsm + 1U; v->slots = (uint32_t)g->drm + 1U;
    v->pointer_count = v->blocks <= 256U ? 16U : 8U;
    expected = v->pointer_count * v->block_size / 16384U;
    if (!expected || g->exm >= expected || ((g->exm + 1U) & g->exm) || v->bytes > (uint64_t)available || v->bytes > (uint64_t)(INT64_MAX - v->base) ||
        (uint64_t)v->blocks * v->block_size > v->bytes - v->heap) return false;
    v->span = ((uint32_t)g->exm + 1U) * 16384U;
    directory_blocks = (v->slots * 32U + v->block_size - 1U) / v->block_size;
    reserved = (uint16_t)(((uint16_t)g->al0 << 8) | g->al1);
    if (!directory_blocks || directory_blocks > 16U || directory_blocks > v->blocks) return false;
    for (i = 0U; i < 16U; ++i) if ((i < directory_blocks && !(reserved & (0x8000U >> i))) || (i >= v->blocks && (reserved & (0x8000U >> i)))) return false;
    xx_mem_zero(seen, sizeof(seen));
    for (i = 0U; i < g->physical_sectors_per_track; ++i) {
        uint16_t sector = g->sector_order[i]; if (sector >= g->physical_sectors_per_track || seen[sector]) return false; seen[sector] = 1U;
    }
    return true;
}
static bool cpm_raw_name(const uint8_t *entry, uint8_t name[11], uint8_t *attributes) {
    unsigned field, i; bool ended;
    *attributes = 0U;
    for (field = 0U; field < 2U; ++field) {
        unsigned start = field ? 8U : 0U, length = field ? 3U : 8U; ended = false;
        for (i = start; i < start + length; ++i) {
            uint8_t c = entry[1U + i] & 0x7FU;
            if (!field && (entry[1U + i] & 0x80U)) {
                if (i >= 4U) return false;
                *attributes |= (uint8_t)(8U << i);
            }
            if (field && (entry[1U + i] & 0x80U)) *attributes |= (uint8_t)(1U << (i - 8U));
            if (c < 0x20U || c > 0x7EU || c == '<' || c == '>' || c == '.' || c == ',' || c == ';' || c == ':' || c == '=' || c == '?' || c == '*' || c == '[' || c == ']') return false;
            if (c == ' ') ended = true; else if (ended) return false;
            if (c >= 'a' && c <= 'z') c -= 32U; name[i] = c;
        }
    }
    return name[0] != ' ';
}
static bool cpm_date(const uint8_t *p) {
    /* Unset formatter metadata can retain the medium's erased-byte pattern. */
    if (p[0] == 0xE5U && p[1] == 0xE5U && p[2] == 0xE5U && p[3] == 0xE5U) return true;
    if (p[2] > 0x23U || (p[2] & 15U) > 9U || p[3] > 0x59U || (p[3] & 15U) > 9U) return false;
    return cpm_u16(p) || (!p[2] && !p[3]);
}
static bool cpm_metadata(cpm_view *v, const uint8_t *entry, uint32_t slot, bool *label, const bool stamp_valid[3]) {
    unsigned i;
    if (v->geometry.version != XX_CPM_VERSION_3) return false;
    if (entry[0] == 32U) {
        size_t length = 11U;
        if (*label || !(entry[12] & 1U) || (entry[12] & 0x8EU) || ((entry[12] & 0x50U) == 0x50U) || !cpm_date(entry + 24) || !cpm_date(entry + 28)) return false;
        for (i = 1U; i <= 11U; ++i) if (entry[i] < 0x20U || entry[i] > 0x7EU) return false;
        while (length && entry[length] == ' ') --length;
        xx_mem_copy(v->volume_name, entry + 1, length); v->volume_name[length] = 0; *label = true; return true;
    }
    if (entry[0] == 33U) {
        if (slot % 4U != 3U) return false;
        /* The manual defines SFCB subfields only for first-extent file FCBs.
         * Deleted entries, labels and later extents leave their fields unused. */
        for (i = 0U; i < 3U; ++i) if (stamp_valid[i] && (!cpm_date(entry + 1U + i * 10U) || !cpm_date(entry + 5U + i * 10U) || entry[9U + i * 10U])) return false;
        return true;
    }
    return false;
}
static int cpm_extent_compare(const void *a, const void *b) {
    const cpm_extent *x = (const cpm_extent *)a, *y = (const cpm_extent *)b; int c;
    if (x->user != y->user) return x->user < y->user ? -1 : 1;
    c = xx_mem_compare(x->name, y->name, sizeof(x->name)); if (c) return c;
    return x->group == y->group ? 0 : (x->group < y->group ? -1 : 1);
}
static bool cpm_same_file(const cpm_extent *a, const cpm_extent *b) { return a->user == b->user && !xx_mem_compare(a->name, b->name, 11U); }
static bool cpm_equal(const char *a, const char *b) {
    while (*a && *b) { char x = *a++, y = *b++; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return false; }
    return !*a && !*b;
}
static bool cpm_device_name(const char *name) {
    static const char *const devices[] = {"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"}; char stem[16]; size_t n = 0U, i;
    while (name[n] && name[n] != '.' && n + 1U < sizeof(stem)) { stem[n] = name[n]; ++n; } stem[n] = 0;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i) if (cpm_equal(stem, devices[i])) return true;
    return n == 4U && stem[3] >= '0' && stem[3] <= '9' && ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') || (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T'));
}
static bool cpm_name(cpm_view *v, cpm_member *member, const cpm_extent *extent, xx_pd_struct *pd) {
    char component[16]; size_t at = 0U, i, field; unsigned attempt;
    for (field = 0U; field < 2U; ++field) {
        unsigned start = field ? 8U : 0U, end = field ? 11U : 8U;
        if (field && extent->name[start] != ' ') component[at++] = '.';
        for (i = start; i < end && extent->name[i] != ' '; ++i) { char c = (char)extent->name[i]; component[at++] = c == '/' || c == '\\' || c == '"' || c == '|' ? '_' : c; }
    }
    component[at] = 0;
    if (cpm_device_name(component)) { for (i = at + 1U; i; --i) component[i] = component[i - 1U]; component[0] = '_'; }
    for (attempt = 0U; attempt <= v->slots; ++attempt) {
        bool taken = false; uint32_t hash = UINT32_C(2166136261); size_t slot;
        if (!attempt) xx_rt_snprintf(member->name, sizeof(member->name), "USER%02u/%s", (unsigned)extent->user, component);
        else xx_rt_snprintf(member->name, sizeof(member->name), "USER%02u/%s~%u", (unsigned)extent->user, component, attempt + 1U);
        for (i = 0U; member->name[i]; ++i) { hash ^= (uint8_t)member->name[i]; hash *= UINT32_C(16777619); }
        slot = hash & (v->hash_capacity - 1U);
        while (v->names[slot]) {
            if (!cpm_work(v, pd)) return false;
            if (cpm_equal(member->name, v->members[v->names[slot] - 1U].name)) { taken = true; break; }
            slot = (slot + 1U) & (v->hash_capacity - 1U);
        }
        if (!taken) { v->names[slot] = (uint32_t)v->count + 1U; return true; }
    }
    return false;
}
static void cpm_view_free(void *pointer) {
    cpm_view *v = (cpm_view *)pointer; if (!v) return;
    xx_mem_free(v->claimed); xx_mem_free(v->extents); xx_mem_free(v->members); xx_mem_free(v->names); xx_mem_free(v);
}
static cpm_view *cpm_parse(Abstractformat *self, xx_pd_struct *pd) {
    cpm_view *v; int64_t total; uint32_t slot, i; bool label = false, stamp_valid[3] = {false, false, false};
    if (!self || !self->device || self->base_address < 0 || cpm_stopped(pd) || (total = xx_io_total_size(self->device)) < self->base_address) return NULL;
    v = (cpm_view *)xx_mem_alloc(sizeof(*v)); if (!v) return NULL; xx_mem_zero(v, sizeof(*v)); v->device = self->device; v->base = self->base_address; v->geometry = ((xx_cpm *)self)->geometry;
    if (!cpm_geometry_valid(v, total - v->base)) goto fail;
    xx_rt_snprintf(v->volume_name, sizeof(v->volume_name), "CP/M");
    v->claimed = (uint8_t *)xx_mem_alloc(v->blocks); v->extents = (cpm_extent *)xx_mem_alloc((size_t)v->slots * sizeof(*v->extents)); v->members = (cpm_member *)xx_mem_alloc((size_t)v->slots * sizeof(*v->members));
    v->hash_capacity = 8U; while (v->hash_capacity < (size_t)v->slots * 2U) v->hash_capacity *= 2U;
    v->names = (uint32_t *)xx_mem_alloc(v->hash_capacity * sizeof(*v->names));
    if (!v->claimed || !v->extents || !v->members || !v->names) goto fail;
    xx_mem_zero(v->names, v->hash_capacity * sizeof(*v->names));
    xx_mem_zero(v->claimed, v->blocks); xx_mem_zero(v->extents, (size_t)v->slots * sizeof(*v->extents)); xx_mem_zero(v->members, (size_t)v->slots * sizeof(*v->members));
    for (i = 0U; i < 16U && i < v->blocks; ++i) if ((((uint16_t)v->geometry.al0 << 8) | v->geometry.al1) & (0x8000U >> i)) v->claimed[i] = 1U;
    for (slot = 0U; slot < v->slots; ++slot) {
        uint8_t entry[32]; cpm_extent *extent; uint32_t logical, records;
        if (!cpm_work(v, pd) || !cpm_read(v, v->heap + (uint64_t)slot * 32U, entry, sizeof(entry), pd)) goto fail;
        if (slot % 4U < 3U) stamp_valid[slot % 4U] = false;
        if (entry[0] == 0xE5U) continue;
        if (entry[0] > 15U) { if (!cpm_metadata(v, entry, slot, &label, stamp_valid)) goto fail; continue; }
        extent = v->extents + v->extent_count;
        if (!cpm_raw_name(entry, extent->name, &extent->attributes) || entry[12] > 31U || entry[14] > (v->geometry.version == XX_CPM_VERSION_22 ? 15U : 63U) || (v->geometry.version == XX_CPM_VERSION_22 && entry[15] > 128U) ||
            (v->geometry.length_mode == XX_CPM_LENGTH_LAST_RECORD_USED && entry[13] > 128U)) goto fail;
        logical = ((uint32_t)entry[14] << 5) | entry[12]; extent->group = logical / ((uint32_t)v->geometry.exm + 1U);
        if (slot % 4U < 3U) stamp_valid[slot % 4U] = extent->group == 0U;
        records = entry[15] > 128U ? 128U : entry[15]; /* CP/M3 p2-14: saturated full logical extent. */
        extent->used = (((logical & v->geometry.exm) * 128U) + records) * 128U; extent->byte_count = entry[13]; extent->user = entry[0]; extent->slot = slot;
        if (extent->used > v->span || (!extent->used && extent->group)) goto fail;
        for (i = 0U; i < v->pointer_count; ++i) {
            uint32_t block = v->pointer_count == 16U ? entry[16U + i] : cpm_u16(entry + 16U + i * 2U);
            if (!block) continue;
            if (!cpm_work(v, pd) || block >= v->blocks || v->claimed[block]) goto fail;
            v->claimed[block] = 1U; extent->pointers[i] = (uint16_t)block;
        }
        ++v->extent_count;
    }
    xx_rt_qsort(v->extents, v->extent_count, sizeof(*v->extents), cpm_extent_compare);
    for (i = 0U; i < v->extent_count;) {
        uint32_t first = i, end = 0U, last = i; cpm_member *member = v->members + v->count;
        do {
            cpm_extent *extent = v->extents + i; uint32_t length = extent->group * v->span + extent->used;
            if (!cpm_work(v, pd) || (i > first && extent->group == v->extents[i - 1U].group)) goto fail;
            if (length >= end) { end = length; last = i; }
            member->attributes |= extent->attributes; ++i;
        } while (i < v->extent_count && cpm_same_file(v->extents + first, v->extents + i));
        member->first = first; member->count = i - first; member->slot = v->extents[first].slot; member->size = end;
        if (end && v->geometry.length_mode == XX_CPM_LENGTH_LAST_RECORD_USED && v->extents[last].byte_count && v->extents[last].byte_count < 128U) member->size -= 128U - v->extents[last].byte_count;
        if (!cpm_name(v, member, v->extents + first, pd)) goto fail; ++v->count;
    }
    v->retained_memory = sizeof(*v) + (uint64_t)v->slots * (sizeof(*v->extents) + sizeof(*v->members));
    xx_mem_free(v->claimed); v->claimed = NULL; xx_mem_free(v->names); v->names = NULL; return v;
fail:
    cpm_view_free(v); return NULL;
}
static const cpm_extent *cpm_extent_find(const cpm_view *v, const cpm_member *member, uint32_t group) {
    uint32_t lo = 0U, hi = member->count;
    while (lo < hi) { uint32_t mid = lo + (hi - lo) / 2U; const cpm_extent *e = v->extents + member->first + mid; if (e->group == group) return e; if (e->group > group) hi = mid; else lo = mid + 1U; }
    return NULL;
}
void xx_cpm_geometry_ibm3740(xx_cpm_geometry *g) {
    static const uint16_t order[26] = {0,6,12,18,24,4,10,16,22,2,8,14,20,1,7,13,19,25,5,11,17,23,3,9,15,21};
    if (!g) return; xx_mem_zero(g, sizeof(*g)); g->tracks = 77U; g->physical_sectors_per_track = 26U; g->physical_sector_size = 128U;
    g->spt = 26U; g->bsh = 3U; g->blm = 7U; g->dsm = 242U; g->drm = 63U; g->off = 2U; g->al0 = 0xC0U; g->version = XX_CPM_VERSION_22;
    xx_mem_copy(g->sector_order, order, sizeof(order));
}
static void cpm_vtable_destroy(Abstractformat *self) { xx_cpm_destroy((xx_cpm *)self); }
void xx_cpm_init_ex(xx_cpm *v, xx_io_device *device, int64_t base, const xx_cpm_geometry *geometry) {
    if (!v) return; xx_mem_zero(v, sizeof(*v)); xx_format_init(&v->format, device, base);
    if (geometry) v->geometry = *geometry;
    v->format.endian = XX_ENDIAN_LITTLE; v->format.file_type = CPM_TYPE; v->format.format_type = XX_TYPE_ARCHIVE; v->format.is_archive = true;
    xx_format_set_mime_type(&v->format, "application/x-cpm-fs"); xx_format_set_extension(&v->format, "img");
    v->format.check_is_valid = xx_cpm_check_is_valid; v->format.handle_base_info = xx_cpm_handle_base_info;
    v->format.get_format_size = xx_cpm_get_format_size; v->format.get_number_of_archive_records = xx_cpm_get_number_of_archive_records;
    v->format.create_archive_records_reading = xx_cpm_create_archive_records_reading; v->format.get_current_archive_record = xx_cpm_get_current_archive_record;
    v->format.archive_record_move_to_next = xx_cpm_archive_record_move_to_next; v->format.unpack_current_archive_record = xx_cpm_unpack_current_archive_record;
    v->format.free_archive_records_reading = xx_cpm_free_archive_records_reading; v->format.destroy = cpm_vtable_destroy;
}
void xx_cpm_init(xx_cpm *v, xx_io_device *device, int64_t base) { xx_cpm_geometry geometry; xx_cpm_geometry_ibm3740(&geometry); xx_cpm_init_ex(v, device, base, &geometry); }
xx_cpm *xx_cpm_create(xx_io_device *device, int64_t base) { xx_cpm *v = (xx_cpm *)xx_mem_alloc(sizeof(*v)); if (v) xx_cpm_init(v, device, base); return v; }
void xx_cpm_destroy(xx_cpm *v) { if (v) xx_format_cleanup_extra_parameters(&v->format); }
void xx_cpm_free(xx_cpm *v) { if (v) { xx_cpm_destroy(v); xx_mem_free(v); } }
bool xx_cpm_check_is_valid(Abstractformat *self, xx_pd_struct *pd) { cpm_view *v = cpm_parse(self, pd); bool ok = v != NULL; cpm_view_free(v); return ok; }
bool xx_cpm_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_cpm *volume = (xx_cpm *)self; cpm_view *v = cpm_parse(self, pd); int64_t total, end;
    if (!self) return false;
    if (!v) { self->is_valid = false; self->base_info_handled = false; return false; }
    volume->number_of_records = v->count; volume->volume_size = v->bytes; volume->allocation_block_size = v->block_size; volume->allocation_block_count = v->blocks;
    xx_mem_copy(volume->volume_name, v->volume_name, sizeof(volume->volume_name));
    self->format_size = (int64_t)v->bytes; self->number_of_archive_records = v->count; total = xx_io_total_size(self->device); end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0; self->is_valid = true; self->base_info_handled = true; cpm_view_free(v); return true;
}
int64_t xx_cpm_get_format_size(Abstractformat *self, xx_pd_struct *pd) { return xx_cpm_handle_base_info(self, pd) ? self->format_size : -1; }
uint64_t xx_cpm_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) { return xx_cpm_handle_base_info(self, pd) ? ((xx_cpm *)self)->number_of_records : 0U; }
static bool cpm_record(xx_archive_record *record, const cpm_view *v, const cpm_member *member) {
    const cpm_extent *first = cpm_extent_find(v, member, 0U);
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)cpm_physical(v, v->heap + (uint64_t)member->slot * 32U); record->header_size = 32U;
    record->data_offset = first && first->pointers[0] ? v->base + (int64_t)cpm_physical(v, v->heap + (uint64_t)first->pointers[0] * v->block_size) : -1;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->attributes) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}
static bool cpm_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i; if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i); xx_meta copy; if (!item) continue; xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
xx_archive_record_state *xx_cpm_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    cpm_view *v = cpm_parse(self, pd); xx_archive_record_state *state; if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state)); if (!state) { cpm_view_free(v); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = v; state->free_internal = cpm_view_free; state->total_records = (int64_t)v->count;
    if (!cpm_options(&state->options, options) || (v->count && !cpm_record(&state->current_record, v, v->members))) { xx_archive_record_state_free(state); return NULL; }
    state->has_record = v->count != 0U; state->current_index = v->count ? 0 : -1; return state;
}
const xx_archive_record *xx_cpm_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) { return self && state && state->format == self && state->has_record ? &state->current_record : NULL; }
bool xx_cpm_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    cpm_view *v;
    if (!self || !state || state->format != self || !state->has_record || !(v = (cpm_view *)state->internal_state) || cpm_stopped(pd)) return false;
    if (v->index + 1U >= v->count) { xx_archive_record_cleanup(&state->current_record); xx_archive_record_init(&state->current_record); state->has_record = false; return false; }
    if (!cpm_record(&state->current_record, v, v->members + v->index + 1U)) { state->has_record = false; return false; }
    ++v->index; ++state->current_index; return true;
}
static uint64_t cpm_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback) {
    const xx_var *var = xx_format_resolve_extra_parameter(self, options, id); if (!var) return fallback;
    switch (var->type) {
        case XX_VAR_TYPE_UINT8: case XX_VAR_TYPE_UINT16: case XX_VAR_TYPE_UINT32: case XX_VAR_TYPE_UINT64: return xx_var_get_u64(var);
        case XX_VAR_TYPE_INT8: case XX_VAR_TYPE_INT16: case XX_VAR_TYPE_INT32: case XX_VAR_TYPE_INT64: { int64_t n = xx_var_get_i64(var); return n < 0 ? fallback : (uint64_t)n; }
        default: return fallback;
    }
}
static bool cpm_extract_limits(Abstractformat *self, xx_archive_record_state *state, const cpm_member *member, size_t *buffer_size) {
    const cpm_view *v = (const cpm_view *)state->internal_state;
    *buffer_size = member->size < CPM_COPY ? member->size : CPM_COPY;
    return member->size <= cpm_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX) &&
        v->retained_memory + sizeof(*state) + *buffer_size <= cpm_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
bool xx_cpm_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    cpm_view *v; const cpm_member *member; uint8_t *buffer; uint32_t done = 0U; size_t buffer_size; bool ok = true;
    if (!self || !self->device || destination == self->device || !state || state->format != self || !state->has_record || !(v = (cpm_view *)state->internal_state) || v->index >= v->count || cpm_stopped(pd)) return false;
    member = v->members + v->index; if (!cpm_extract_limits(self, state, member, &buffer_size)) return false; if (!buffer_size) return !cpm_stopped(pd);
    buffer = (uint8_t *)xx_mem_alloc(buffer_size); if (!buffer) return false;
    while (done < member->size) {
        uint32_t group = done / v->span, within = done % v->span, block_index = within / v->block_size;
        const cpm_extent *extent = cpm_extent_find(v, member, group); size_t part = v->block_size - within % v->block_size, written = 0U;
        if (part > buffer_size) part = buffer_size; if (part > member->size - done) part = member->size - done;
        if (extent && within < extent->used) {
            if (part > extent->used - within) part = extent->used - within;
            if (block_index >= v->pointer_count) { ok = false; break; }
            if (extent->pointers[block_index]) {
                if (!cpm_read(v, v->heap + (uint64_t)extent->pointers[block_index] * v->block_size + within % v->block_size, buffer, part, pd)) { ok = false; break; }
            } else xx_mem_zero(buffer, part);
        } else xx_mem_zero(buffer, part);
        while (destination && written < part && !cpm_stopped(pd)) {
            ssize_t got = xx_io_write(destination, buffer + written, part - written); if (got <= 0 || (size_t)got > part - written) { ok = false; break; } written += (size_t)got;
        }
        if (!ok || cpm_stopped(pd)) { ok = false; break; } done += (uint32_t)part;
    }
    xx_mem_free(buffer); return ok && !cpm_stopped(pd);
}
static xx_io_device *cpm_stage(const char *destination, char **stage_path) {
    unsigned attempt; size_t i, parent = 0U; char *directory = xx_str_dup(destination); *stage_path = NULL; if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i) if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U; directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate; xx_io_device *output; xx_rt_snprintf(suffix, sizeof(suffix), ".xx_cpm.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix); if (!candidate) break;
        if (cpm_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; } xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_cpm_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    cpm_view *v; const xx_var *parameter, *overwrite_parameter; const char *base = NULL;
    char *owned = NULL, *path = NULL, *staged = NULL; size_t buffer_size; bool ok = false, overwrite;
    if (!self || !state || state->format != self || !state->has_record || !(v = (cpm_view *)state->internal_state) || v->index >= v->count || cpm_stopped(pd)) return false;
    if (!cpm_extract_limits(self, state, v->members + v->index, &buffer_size)) return false;
    parameter = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH); overwrite_parameter = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_parameter && xx_var_get_bool(overwrite_parameter); if (!parameter) return xx_cpm_extract_record_to_device(self, state, NULL, pd);
    if (parameter->type == XX_VAR_TYPE_STRING || parameter->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(parameter);
    else if (parameter->type == XX_VAR_TYPE_WSTRING || parameter->type == XX_VAR_TYPE_WSTRING_VIEW) { owned = xx_str_unicode_to_utf8(xx_var_get_wstr(parameter)); base = owned; }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", v->members[v->index].name) : xx_str_concat(base, v->members[v->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    { xx_io_device *output = cpm_stage(path, &staged); if (!output) goto done; ok = xx_cpm_extract_record_to_device(self, state, output, pd); if (xx_io_close(output)) ok = false; }
    if (cpm_stopped(pd)) ok = false; if (ok) ok = xx_io_file_replace_a(staged, path, overwrite);
done:
    if (!ok && staged) xx_io_file_remove_a(staged); xx_str_free(staged); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_cpm_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) { (void)self; xx_archive_record_state_free(state); }
