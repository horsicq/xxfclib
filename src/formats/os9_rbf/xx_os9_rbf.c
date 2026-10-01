/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original implementation of the classic Microware OS-9/6809 RBF structures.
 */
#include "xxfclib/formats/os9_rbf/xx_os9_rbf.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include <stdio.h>
#ifdef OS9_RBF
#define RB_TYPE XX_FILE_TYPE_OS9_RBF
#else
#define RB_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define RB_SECTOR 256U
#define RB_MAX_SECTORS 65536U
#define RB_MAX_MEMBERS 4096U
#define RB_MAX_DEPTH 32U
#define RB_MAX_DIR_BYTES (RB_MAX_MEMBERS * 32U)
#define RB_MAX_PATH 1024U
#define RB_MAX_PATH_BYTES 1048576U
#define RB_MAX_WORK 4000000U
#define RB_SEGMENTS 48U
typedef struct rb_segment_s { uint32_t start; uint16_t count; } rb_segment;
typedef struct rb_member_s {
    char *path;
    char raw[30];
    uint32_t fd, parent_fd, size;
    uint8_t attr, link_count, refs, segment_count;
    bool directory;
    rb_segment segments[RB_SEGMENTS];
} rb_member;
typedef struct rb_view_s {
    xx_io_device *device;
    int64_t base;
    uint64_t bytes, path_bytes, retained_memory;
    uint32_t sectors, cluster, root_fd, work;
    uint16_t map_bytes;
    uint8_t *bitmap, *dir_seen;
    uint32_t *owner, *fd_cache;
    rb_member *members;
    size_t count, capacity, index;
    char volume_name[40];
} rb_view;
static uint16_t rb_be16(const uint8_t *p) { return (uint16_t)(((unsigned)p[0] << 8U) | p[1]); }
static uint32_t rb_be24(const uint8_t *p) { return ((uint32_t)p[0] << 16U) | ((uint32_t)p[1] << 8U) | p[2]; }
static uint32_t rb_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | p[3]; }
static bool rb_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool rb_work(rb_view *v, xx_pd_struct *pd) { return !rb_stopped(pd) && ++v->work <= RB_MAX_WORK; }
static bool rb_read(rb_view *v, uint64_t offset, void *dst, size_t size, xx_pd_struct *pd) {
    int64_t cursor; size_t done = 0U; bool ok = false;
    if (!v || !dst || offset > v->bytes || size > v->bytes - offset ||
        offset > (uint64_t)(INT64_MAX - v->base) || rb_stopped(pd)) return false;
    cursor = xx_io_tell(v->device); if (cursor < 0) return false;
    if (!xx_io_seek64(v->device, v->base + (int64_t)offset, SEEK_SET)) {
        while (done < size && !rb_stopped(pd)) {
            ssize_t n = xx_io_read(v->device, (uint8_t *)dst + done, size - done);
            if (n <= 0 || (size_t)n > size - done) break;
            done += (size_t)n;
        }
        ok = done == size;
    }
    if (xx_io_seek64(v->device, cursor, SEEK_SET)) ok = false;
    return ok && !rb_stopped(pd);
}
static bool rb_sector(rb_view *v, uint32_t lsn, uint8_t out[RB_SECTOR], xx_pd_struct *pd) {
    return lsn < v->sectors && rb_work(v, pd) && rb_read(v, (uint64_t)lsn * RB_SECTOR, out, RB_SECTOR, pd);
}
static bool rb_equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}
static bool rb_device_name(const char *name) {
    static const char *const devices[] = {"CON","PRN","AUX","NUL","CONIN$","CONOUT$","CLOCK$"};
    char stem[32]; size_t i = 0U, j;
    while (name[i] && name[i] != '.' && i + 1U < sizeof(stem)) { stem[i] = name[i]; ++i; }
    stem[i] = 0; if (name[i] && name[i] != '.') return false;
    for (j = 0U; j < sizeof(devices) / sizeof(devices[0]); ++j) if (rb_equal(stem, devices[j])) return true;
    return i == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (((stem[0] == 'C' || stem[0] == 'c') && (stem[1] == 'O' || stem[1] == 'o') && (stem[2] == 'M' || stem[2] == 'm')) ||
         ((stem[0] == 'L' || stem[0] == 'l') && (stem[1] == 'P' || stem[1] == 'p') && (stem[2] == 'T' || stem[2] == 't')));
}
/* OS-9 sets bit 7 on the final 7-bit character of each directory name. */
static bool rb_name(const uint8_t source[29], char raw[30], char safe[96]) {
    unsigned i; size_t length = 0U, used = 0U; bool final = false;
    for (i = 0U; i < 29U; ++i) {
        uint8_t c = source[i];
        if (c == 0U) return false;
        c &= 0x7FU;
        if (c < 0x20U || c > 0x7EU || c == '/') return false;
        raw[length++] = (char)c;
        if (source[i] & 0x80U) { final = true; break; }
    }
    if (!final || !length) return false;
    raw[length] = 0;
    for (i = 0U; i < length; ++i) {
        unsigned char c = (unsigned char)raw[i];
        if (c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') c = '_';
        safe[used++] = (char)c;
    }
    while (used && (safe[used - 1U] == ' ' || safe[used - 1U] == '.')) safe[used - 1U] = '_';
    safe[used] = 0;
    if (!used) return false;
    if (rb_device_name(safe)) {
        size_t j; for (j = used + 1U; j; --j) safe[j] = safe[j - 1U]; safe[0] = '_';
    }
    return true;
}
static bool rb_allocated(const rb_view *v, uint32_t lsn) {
    uint32_t bit = lsn / v->cluster;
    return bit / 8U < v->map_bytes && (v->bitmap[bit / 8U] & (uint8_t)(0x80U >> (bit % 8U))) != 0U;
}
static bool rb_claim(rb_view *v, uint32_t lsn, uint32_t owner) {
    if (lsn >= v->sectors || !rb_allocated(v, lsn) || v->owner[lsn]) return false;
    v->owner[lsn] = owner; return true;
}
static bool rb_variant_ok(xx_os9_rbf_variant variant, const uint8_t id[RB_SECTOR], uint32_t sectors, uint32_t cluster) {
    unsigned geometry = id[16] & 0x0FU;
    if (variant == XX_OS9_RBF_CLASSIC) return true;
    if (id[3] != 18U || rb_be16(id + 17U) != 18U) {
        return variant == XX_OS9_RBF_HD4096_C4 && sectors == 4096U && cluster == 4U;
    }
    switch (variant) {
    case XX_OS9_RBF_35SS_C1: return sectors == 630U && cluster == 1U && geometry == 2U;
    case XX_OS9_RBF_40DS_C1: return sectors == 1440U && cluster == 1U && geometry == 3U;
    case XX_OS9_RBF_40DS_C2: return sectors == 1440U && cluster == 2U && geometry == 3U;
    case XX_OS9_RBF_80DS_C1: return sectors == 2880U && cluster == 1U && geometry == 7U;
    case XX_OS9_RBF_HD4096_C4: return sectors == 4096U && cluster == 4U;
    default: return false;
    }
}
static bool rb_fd(rb_view *v, uint32_t lsn, bool directory, rb_member *out, xx_pd_struct *pd) {
    uint8_t data[RB_SECTOR]; uint64_t capacity = 0U; unsigned i; bool zero = false;
    if (lsn < 1U || lsn >= v->sectors || !rb_claim(v, lsn, lsn) || !rb_sector(v, lsn, data, pd)) return false;
    out->fd = lsn; out->attr = data[0]; out->link_count = data[8]; out->size = rb_be32(data + 9U);
    out->directory = (data[0] & 0x80U) != 0U;
    if (!out->link_count || out->directory != directory ||
        (directory && (out->size == 0U || out->size > RB_MAX_DIR_BYTES || out->size % 32U))) return false;
    for (i = 0U; i < RB_SEGMENTS; ++i) {
        uint32_t first = rb_be24(data + 16U + i * 5U);
        uint16_t count = rb_be16(data + 19U + i * 5U); uint32_t j;
        if (!first && !count) { zero = true; continue; }
        if (zero || !first || !count || first >= v->sectors || count > v->sectors - first) return false;
        out->segments[out->segment_count].start = first;
        out->segments[out->segment_count].count = count;
        ++out->segment_count;
        capacity += (uint64_t)count * RB_SECTOR;
        for (j = first; j < first + count; ++j) if (!rb_work(v, pd) || !rb_claim(v, j, lsn)) return false;
    }
    return capacity >= out->size;
}
static bool rb_logical_read(rb_view *v, const rb_member *m, uint32_t offset, void *out, size_t size, xx_pd_struct *pd) {
    uint64_t logical = 0U; unsigned i; size_t done = 0U;
    if (offset > m->size || size > m->size - offset) return false;
    for (i = 0U; i < m->segment_count && done < size; ++i) {
        uint64_t span = (uint64_t)m->segments[i].count * RB_SECTOR;
        if ((uint64_t)offset >= logical + span) { logical += span; continue; }
        {
            uint64_t start = (uint64_t)offset > logical ? (uint64_t)offset - logical : 0U;
            size_t part = (size_t)(span - start);
            if (part > size - done) part = size - done;
            if (!rb_work(v, pd) || !rb_read(v, ((uint64_t)m->segments[i].start * RB_SECTOR) + start,
                (uint8_t *)out + done, part, pd)) return false;
            done += part; offset += (uint32_t)part; logical += span;
        }
    }
    return done == size;
}
static bool rb_reserve_member(rb_view *v) {
    if (v->count >= RB_MAX_MEMBERS) return false;
    if (v->count == v->capacity) {
        size_t next = v->capacity ? v->capacity * 2U : 32U;
        rb_member *array;
        if (next > RB_MAX_MEMBERS) next = RB_MAX_MEMBERS;
        array = (rb_member *)xx_mem_realloc(v->members, next * sizeof(*array)); if (!array) return false;
        v->members = array; v->capacity = next;
    }
    return true;
}
static char *rb_path(rb_view *v, const char *prefix, const char *safe, xx_pd_struct *pd) {
    unsigned suffix; size_t parent = xx_str_len(prefix);
    for (suffix = 0U; suffix < RB_MAX_MEMBERS; ++suffix) {
        char leaf[128], *path; size_t i, bytes; bool taken = false;
        if (!rb_work(v, pd)) return NULL;
        if (suffix) xx_rt_snprintf(leaf, sizeof(leaf), "%s~%u", safe, suffix + 1U);
        else xx_rt_snprintf(leaf, sizeof(leaf), "%s", safe);
        bytes = parent + (parent ? 1U : 0U) + xx_str_len(leaf) + 1U;
        if (bytes > RB_MAX_PATH || bytes > RB_MAX_PATH_BYTES - v->path_bytes) return NULL;
        path = parent ? xx_str_concat3(prefix, "/", leaf) : xx_str_dup(leaf);
        if (!path) return NULL;
        for (i = 0U; i < v->count; ++i) {
            if (!rb_work(v, pd)) { xx_str_free(path); return NULL; }
            if (rb_equal(path, v->members[i].path)) { taken = true; break; }
        }
        if (!taken) { v->path_bytes += bytes; return path; }
        xx_str_free(path);
    }
    return NULL;
}
static bool rb_visit(rb_view *v, uint32_t fd_lsn, uint32_t parent_fd, const char *prefix,
    unsigned depth, xx_pd_struct *pd) {
    rb_member directory; uint32_t position; bool dot = false, dotdot = false;
    if (depth > RB_MAX_DEPTH || fd_lsn >= v->sectors || v->dir_seen[fd_lsn]) return false;
    xx_mem_zero(&directory, sizeof(directory)); v->dir_seen[fd_lsn] = 1U;
    if (!rb_fd(v, fd_lsn, true, &directory, pd)) return false;
    for (position = 0U; position < directory.size; position += 32U) {
        uint8_t entry[32]; char raw[30], safe[96], *path; uint32_t child; size_t i;
        rb_member member;
        if (!rb_work(v, pd) || !rb_logical_read(v, &directory, position, entry, sizeof(entry), pd)) return false;
        if (!entry[0]) continue; /* An unused/deleted directory slot. */
        if (!rb_name(entry, raw, safe)) return false;
        child = rb_be24(entry + 29U);
        if (child == 0U || child >= v->sectors) return false;
        if (!xx_str_cmp(raw, ".")) { if (dot || child != fd_lsn) return false; dot = true; continue; }
        if (!xx_str_cmp(raw, "..")) { if (dotdot || child != parent_fd) return false; dotdot = true; continue; }
        for (i = 0U; i < v->count; ++i) {
            if (!rb_work(v, pd)) return false;
            if (v->members[i].parent_fd == fd_lsn && !xx_str_cmp(v->members[i].raw, raw)) return false;
        }
        if (!rb_reserve_member(v)) return false;
        path = rb_path(v, prefix, safe, pd); if (!path) return false;
        xx_mem_zero(&member, sizeof(member)); member.path = path; member.parent_fd = fd_lsn;
        xx_rt_snprintf(member.raw, sizeof(member.raw), "%s", raw);
        if (v->fd_cache[child]) {
            rb_member *first = v->members + v->fd_cache[child] - 1U;
            if (first->directory || first->refs >= first->link_count) { xx_str_free(path); return false; }
            {
                char saved[30]; xx_rt_snprintf(saved, sizeof(saved), "%s", member.raw);
                member = *first; member.path = path; member.parent_fd = fd_lsn;
                xx_rt_snprintf(member.raw, sizeof(member.raw), "%s", saved);
            }
            ++first->refs;
        } else {
            uint8_t descriptor[RB_SECTOR]; bool is_dir;
            if (!rb_sector(v, child, descriptor, pd)) { xx_str_free(path); return false; }
            is_dir = (descriptor[0] & 0x80U) != 0U;
            if (is_dir) {
                member.fd = child; member.attr = descriptor[0]; member.directory = true;
                member.link_count = descriptor[8]; member.size = rb_be32(descriptor + 9U);
                if (v->dir_seen[child]) { xx_str_free(path); return false; }
            } else {
                if (!rb_fd(v, child, false, &member, pd)) { xx_str_free(path); return false; }
                member.refs = 1U;
                v->fd_cache[child] = (uint32_t)(v->count + 1U);
            }
        }
        v->members[v->count++] = member;
        if (member.directory && !rb_visit(v, child, fd_lsn, path, depth + 1U, pd)) return false;
    }
    return dot && dotdot;
}
static void rb_view_free(void *ptr) {
    rb_view *v = (rb_view *)ptr; size_t i;
    if (!v) return;
    for (i = 0U; i < v->count; ++i) xx_str_free(v->members[i].path);
    xx_mem_free(v->members); xx_mem_free(v->owner); xx_mem_free(v->fd_cache);
    xx_mem_free(v->dir_seen); xx_mem_free(v->bitmap); xx_mem_free(v);
}
static rb_view *rb_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_os9_rbf *disk = (xx_os9_rbf *)self; rb_view *v; uint8_t id[RB_SECTOR];
    int64_t total; uint32_t map_sectors, i, clusters;
    if (!self || !self->device || self->base_address < 0 || rb_stopped(pd) ||
        (total = xx_io_total_size(self->device)) < self->base_address) return NULL;
    v = (rb_view *)xx_mem_alloc(sizeof(*v)); if (!v) return NULL;
    xx_mem_zero(v, sizeof(*v)); v->device = self->device; v->base = self->base_address;
    v->bytes = (uint64_t)(total - v->base);
    if (v->bytes < RB_SECTOR || !rb_read(v, 0U, id, sizeof(id), pd)) goto fail;
    v->sectors = rb_be24(id); v->cluster = rb_be16(id + 6U);
    v->map_bytes = rb_be16(id + 4U); v->root_fd = rb_be24(id + 8U);
    if (v->sectors < 8U || v->sectors > RB_MAX_SECTORS || v->bytes < (uint64_t)v->sectors * RB_SECTOR ||
        !v->cluster || v->cluster > 32U || (v->cluster & (v->cluster - 1U)) ||
        !id[3] || !rb_be16(id + 17U) || (id[16] & 0xF0U) ||
        (id[96] == 'C' && id[97] == 'R' && id[98] == 'U' && id[99] == 'Z') ||
        !rb_variant_ok(disk->variant, id, v->sectors, v->cluster)) goto fail;
    clusters = (v->sectors + v->cluster - 1U) / v->cluster;
    if (v->map_bytes != (clusters + 7U) / 8U) goto fail;
    map_sectors = (v->map_bytes + RB_SECTOR - 1U) / RB_SECTOR;
    if (v->root_fd <= map_sectors || v->root_fd >= v->sectors) goto fail;
    v->bytes = (uint64_t)v->sectors * RB_SECTOR;
    v->bitmap = (uint8_t *)xx_mem_alloc(v->map_bytes);
    v->owner = (uint32_t *)xx_mem_alloc((size_t)v->sectors * sizeof(uint32_t));
    v->fd_cache = (uint32_t *)xx_mem_alloc((size_t)v->sectors * sizeof(uint32_t));
    v->dir_seen = (uint8_t *)xx_mem_alloc(v->sectors);
    if (!v->bitmap || !v->owner || !v->fd_cache || !v->dir_seen) goto fail;
    xx_mem_zero(v->owner, (size_t)v->sectors * sizeof(uint32_t));
    xx_mem_zero(v->fd_cache, (size_t)v->sectors * sizeof(uint32_t));
    xx_mem_zero(v->dir_seen, v->sectors);
    if (!rb_read(v, RB_SECTOR, v->bitmap, v->map_bytes, pd)) goto fail;
    for (i = 0U; i <= map_sectors; ++i) if (!rb_claim(v, i, UINT32_MAX)) goto fail;
    {
        char raw[30], safe[96]; uint8_t volume[29];
        for (i = 0U; i < 29U; ++i) volume[i] = id[31U + i];
        if (rb_name(volume, raw, safe)) xx_rt_snprintf(v->volume_name, sizeof(v->volume_name), "%s", raw);
    }
    if (!rb_visit(v, v->root_fd, v->root_fd, "", 0U, pd)) goto fail;
    for (i = 0U; i < v->count; ++i) {
        rb_member *m = v->members + i;
        if (!m->directory && v->fd_cache[m->fd] == i + 1U && m->refs != m->link_count) goto fail;
    }
    v->retained_memory = sizeof(*v) + v->map_bytes + (uint64_t)v->sectors * (sizeof(uint32_t) * 2U + 1U) +
        v->capacity * sizeof(rb_member) + v->path_bytes;
    return v;
fail:
    rb_view_free(v); return NULL;
}
static void rb_vtable_destroy(Abstractformat *self) { xx_os9_rbf_destroy((xx_os9_rbf *)self); }
void xx_os9_rbf_init_ex(xx_os9_rbf *disk, xx_io_device *device, int64_t base, xx_os9_rbf_variant variant) {
    if (!disk) return;
    xx_mem_zero(disk, sizeof(*disk)); xx_format_init(&disk->format, device, base);
    disk->variant = variant; disk->format.endian = XX_ENDIAN_BIG;
    disk->format.file_type = RB_TYPE; disk->format.format_type = XX_TYPE_ARCHIVE; disk->format.is_archive = true;
    xx_format_set_mime_type(&disk->format, "application/x-os9-rbf");
    xx_format_set_extension(&disk->format, "dsk");
    disk->format.check_is_valid = xx_os9_rbf_check_is_valid;
    disk->format.handle_base_info = xx_os9_rbf_handle_base_info;
    disk->format.get_format_size = xx_os9_rbf_get_format_size;
    disk->format.get_number_of_archive_records = xx_os9_rbf_get_number_of_archive_records;
    disk->format.create_archive_records_reading = xx_os9_rbf_create_archive_records_reading;
    disk->format.get_current_archive_record = xx_os9_rbf_get_current_archive_record;
    disk->format.archive_record_move_to_next = xx_os9_rbf_archive_record_move_to_next;
    disk->format.unpack_current_archive_record = xx_os9_rbf_unpack_current_archive_record;
    disk->format.free_archive_records_reading = xx_os9_rbf_free_archive_records_reading;
    disk->format.destroy = rb_vtable_destroy;
}
void xx_os9_rbf_init(xx_os9_rbf *disk, xx_io_device *device, int64_t base) {
    xx_os9_rbf_init_ex(disk, device, base, XX_OS9_RBF_CLASSIC);
}
xx_os9_rbf *xx_os9_rbf_create_ex(xx_io_device *device, int64_t base, xx_os9_rbf_variant variant) {
    xx_os9_rbf *disk = (xx_os9_rbf *)xx_mem_alloc(sizeof(*disk));
    if (disk) xx_os9_rbf_init_ex(disk, device, base, variant); return disk;
}
xx_os9_rbf *xx_os9_rbf_create(xx_io_device *device, int64_t base) {
    return xx_os9_rbf_create_ex(device, base, XX_OS9_RBF_CLASSIC);
}
void xx_os9_rbf_destroy(xx_os9_rbf *disk) { if (disk) xx_format_cleanup_extra_parameters(&disk->format); }
void xx_os9_rbf_free(xx_os9_rbf *disk) { if (disk) { xx_os9_rbf_destroy(disk); xx_mem_free(disk); } }
bool xx_os9_rbf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    rb_view *v = rb_parse(self, pd); bool ok = v != NULL; rb_view_free(v); return ok;
}
bool xx_os9_rbf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_os9_rbf *disk = (xx_os9_rbf *)self; rb_view *v = rb_parse(self, pd); int64_t total, end;
    if (!self) return false;
    if (!v) { self->is_valid = false; self->base_info_handled = false; return false; }
    disk->total_sectors = v->sectors; disk->sectors_per_cluster = v->cluster;
    disk->number_of_records = v->count;
    xx_rt_snprintf(disk->volume_name, sizeof(disk->volume_name), "%s", v->volume_name);
    self->format_size = (int64_t)v->bytes; self->number_of_archive_records = v->count;
    total = xx_io_total_size(self->device); end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1; self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true; rb_view_free(v); return true;
}
int64_t xx_os9_rbf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return xx_os9_rbf_handle_base_info(self, pd) ? self->format_size : -1;
}
uint64_t xx_os9_rbf_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) {
    return xx_os9_rbf_handle_base_info(self, pd) ? ((xx_os9_rbf *)self)->number_of_records : 0U;
}
static bool rb_record(xx_archive_record *record, const rb_view *v, const rb_member *m) {
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)m->fd * RB_SECTOR; record->header_size = RB_SECTOR;
    record->data_offset = m->segment_count ? v->base + (int64_t)m->segments[0].start * RB_SECTOR : -1;
    record->compressed_size = m->directory ? 0U : m->size;
    return xx_archive_record_set_original_name(record, m->path) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, m->directory ? 0U : m->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, m->directory ? 0U : m->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, m->attr) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, m->directory);
}
static bool rb_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i; if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i); xx_meta copy;
        if (!item) continue; xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) { xx_meta_cleanup(&copy); return false; }
    }
    return true;
}
xx_archive_record_state *xx_os9_rbf_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    rb_view *v = rb_parse(self, pd); xx_archive_record_state *state;
    if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state)); if (!state) { rb_view_free(v); return NULL; }
    xx_archive_record_state_init(state, self); state->internal_state = v; state->free_internal = rb_view_free;
    state->total_records = (int64_t)v->count;
    if (!rb_options(&state->options, options) || (v->count && !rb_record(&state->current_record, v, v->members))) {
        xx_archive_record_state_free(state); return NULL;
    }
    state->has_record = v->count != 0U; state->current_index = v->count ? 0 : -1; return state;
}
const xx_archive_record *xx_os9_rbf_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}
bool xx_os9_rbf_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    rb_view *v;
    if (!self || !state || state->format != self || !state->has_record || !(v = (rb_view *)state->internal_state) || rb_stopped(pd)) return false;
    if (v->index + 1U >= v->count) {
        v->index = v->count; state->has_record = false; state->current_index = -1;
        xx_archive_record_cleanup(&state->current_record); xx_archive_record_init(&state->current_record); return false;
    }
    if (!rb_record(&state->current_record, v, v->members + v->index + 1U)) { state->has_record = false; return false; }
    ++v->index; ++state->current_index; return true;
}
static uint64_t rb_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback) {
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
static bool rb_limits(Abstractformat *self, xx_archive_record_state *state, const rb_view *v, const rb_member *m) {
    uint64_t copy = m->directory ? 0U : RB_SECTOR;
    return (m->directory || m->size <= rb_limit(self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX)) &&
        v->retained_memory + sizeof(*state) + copy <= rb_limit(self, &state->options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX);
}
bool xx_os9_rbf_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    rb_view *v; const rb_member *m; uint8_t buffer[RB_SECTOR]; uint32_t done = 0U; unsigned i;
    if (!self || !self->device || destination == self->device || !state || state->format != self ||
        !state->has_record || !(v = (rb_view *)state->internal_state) || v->index >= v->count || rb_stopped(pd)) return false;
    m = v->members + v->index;
    if (!rb_limits(self, state, v, m) || m->directory) return false;
    for (i = 0U; i < m->segment_count && done < m->size; ++i) {
        uint32_t j;
        for (j = 0U; j < m->segments[i].count && done < m->size; ++j) {
            uint32_t remaining = m->size - done; size_t part = remaining < RB_SECTOR ? remaining : RB_SECTOR, written = 0U;
            if (!rb_sector(v, m->segments[i].start + j, buffer, pd)) return false;
            while (destination && written < part && !rb_stopped(pd)) {
                ssize_t n = xx_io_write(destination, buffer + written, part - written);
                if (n <= 0 || (size_t)n > part - written) return false;
                written += (size_t)n;
            }
            if (rb_stopped(pd)) return false;
            done += (uint32_t)part;
        }
    }
    return done == m->size && !rb_stopped(pd);
}
static xx_io_device *rb_stage(const char *destination, char **stage_path) {
    unsigned attempt; size_t i, parent = 0U; char *directory = xx_str_dup(destination);
    *stage_path = NULL; if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i) if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate; xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_os9_rbf.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix); if (!candidate) break;
        if (rb_equal(candidate, destination)) { xx_str_free(candidate); continue; }
        output = xx_io_file_open(candidate, "wbx");
        if (output) { *stage_path = candidate; xx_str_free(directory); return output; }
        xx_str_free(candidate);
    }
    xx_str_free(directory); return NULL;
}
bool xx_os9_rbf_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    rb_view *v; const rb_member *m; const xx_var *option, *overwrite_option; const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage_path = NULL; bool overwrite, ok = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(v = (rb_view *)state->internal_state) || v->index >= v->count || rb_stopped(pd)) return false;
    m = v->members + v->index;
    if (!rb_limits(self, state, v, m)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return m->directory || xx_os9_rbf_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base = owned;
    }
    if (!base) goto done;
    path = (*base && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\')
        ? xx_str_concat3(base, "/", m->path) : xx_str_concat(base, m->path);
    if (!path) goto done;
    if (m->directory) { ok = !rb_stopped(pd) && xx_store_create_dirs_a(path, true); goto done; }
    if ((!overwrite && xx_io_file_exists_a(path)) || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = rb_stage(path, &stage_path); if (!output) goto done;
        ok = xx_os9_rbf_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output)) ok = false;
    }
    if (rb_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(stage_path, path, overwrite);
done:
    if (!ok && stage_path) xx_io_file_remove_a(stage_path);
    xx_str_free(stage_path); xx_str_free(path); xx_str_free(owned); return ok;
}
void xx_os9_rbf_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) {
    (void)self; xx_archive_record_state_free(state);
}
