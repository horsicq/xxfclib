/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Shared bounded I/O and archive callbacks for the native disk containers.
 * This private header is compiled separately into each reader.
 */
#ifndef XX_DISK_CONTAINERS_NATIVE_H
#define XX_DISK_CONTAINERS_NATIVE_H

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include <limits.h>

#ifndef DC_MAX_MEMBERS
#define DC_MAX_MEMBERS 64U
#endif
#define DC_MAX_IMAGE_SIZE (UINT64_C(1) << 44)
#define DC_MAX_MAP_ENTRIES UINT32_C(4194304)
#define DC_IO_CAPACITY 65536U

enum { DC_STORED = 0, DC_WUX_MAP = 1, DC_LAZY_TAIL = 2, DC_SAP_SECTORS = 3,
       DC_DMK_SECTORS = 4 };

typedef struct dc_member {
    char name[64];
    uint64_t offset;
    uint64_t size;
    uint64_t stored_size;
    uint64_t header_offset;
    uint64_t header_size;
    unsigned mode;
} dc_member;

typedef struct dc_image {
    dc_member members[DC_MAX_MEMBERS];
    uint32_t *map;
    uint32_t map_count;
    uint32_t block_size;
    uint64_t data_offset;
    uint64_t available;
    uint64_t extent;
    size_t count;
    size_t index;
} dc_image;

static bool dc_parse(Abstractformat *f, dc_image *image,
                     const xx_list_s *options, xx_pd_struct *pd);
#if defined(DC_NATIVE_DMK)
static bool dc_dmk_sector(Abstractformat *f, const dc_image *image,
                          uint32_t encoded, uint8_t *plain, xx_pd_struct *pd);
#endif

static uint16_t dc_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}
static uint32_t dc_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}
static uint64_t dc_le64(const uint8_t *p) {
    return (uint64_t)dc_le32(p) | ((uint64_t)dc_le32(p + 4U) << 32U);
}
static bool dc_span(uint64_t at, uint64_t size, uint64_t total) {
    return at <= total && size <= total - at;
}
static bool dc_zero(const uint8_t *p, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i) if (p[i]) return false;
    return true;
}
static bool dc_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}
static bool dc_error(xx_pd_struct *pd, const char *message) {
    xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, message);
    return false;
}
static bool dc_memory_limit(Abstractformat *f, const xx_list_s *options,
                            uint64_t bytes) {
    const xx_var *v = xx_format_resolve_extra_parameter(
        f, options, XX_META_ID_OPT_MEMORY_LIMIT);
    return !v || bytes <= xx_var_get_u64(v);
}

/* The enclosing operation restores the cursor once, including on failure.
 * Short positive reads are normal; zero/negative/oversized results fail.
 */
static bool dc_read(Abstractformat *f, const dc_image *image, uint64_t at,
                    void *data, size_t size, xx_pd_struct *pd) {
    size_t received = 0U;
    if (!f || !f->device || !data || !dc_span(at, size, image->available) ||
        dc_stopped(pd) ||
        xx_io_seek64(f->device, f->base_address + (int64_t)at, SEEK_SET))
        return false;
    while (received < size) {
        ssize_t amount;
        size_t request = size - received;
        if (dc_stopped(pd)) return false;
        amount = xx_io_read(f->device, (uint8_t *)data + received, request);
        if (dc_stopped(pd) || amount <= 0 || (size_t)amount > request)
            return false;
        received += (size_t)amount;
    }
    return true;
}

/* SAP stores sector headers unchanged, XORs payload bytes with 0xB3, and
 * appends a big-endian reflected CCITT checksum of the plaintext header+data.
 * Rechecking at extraction catches changes made after record creation.
 */
static bool dc_sap_sector(Abstractformat *f, const dc_image *image,
                          uint64_t sector, uint8_t *plain, xx_pd_struct *pd) {
    uint8_t record[262];
    xx_crc_context crc;
    uint16_t expected;
    size_t i;
    uint32_t size = image->block_size;
    if ((size != 128U && size != 256U) || sector >= 1280U ||
        !dc_read(f, image, image->data_offset + sector * (size + 6U),
                  record, size + 6U, pd) || record[0] || record[1] ||
        record[2] != sector / 16U || record[3] != sector % 16U + 1U) return false;
    expected = (uint16_t)((uint16_t)record[4U + size] << 8U) | record[5U + size];
    for (i = 0U; i < size; ++i)
        plain[i] = record[i + 4U] ^ UINT8_C(0xb3);
    if (!xx_crc_context_init_type(&crc, XX_CRC_TYPE_CRC16_MCRF4XX))
        return false;
    xx_crc_context_update(&crc, record, 4U);
    xx_crc_context_update(&crc, plain, size);
    return (uint16_t)xx_crc_context_final(&crc) == expected;
}

static void dc_free_image(void *pointer) {
    dc_image *image = (dc_image *)pointer;
    if (image) {
        if (image->map) xx_mem_free(image->map);
        xx_mem_free(image);
    }
}

static dc_image *dc_open(Abstractformat *f, const xx_list_s *options,
                         xx_pd_struct *pd) {
    dc_image *image;
    int64_t total, cursor;
    bool valid;
    if (!f || !f->device || f->base_address < 0 || dc_stopped(pd)) return NULL;
    total = xx_io_size(f->device);
    if (total < f->base_address ||
        !dc_memory_limit(f, options, sizeof(dc_image))) return NULL;
    cursor = xx_io_tell(f->device);
    image = (dc_image *)xx_mem_calloc(1U, sizeof(*image));
    if (!image) return NULL;
    image->available = (uint64_t)(total - f->base_address);
    valid = dc_parse(f, image, options, pd) && image->count != 0U &&
            image->extent <= image->available && !dc_stopped(pd);
    if (cursor >= 0 && xx_io_seek64(f->device, cursor, SEEK_SET)) valid = false;
    if (!valid) { dc_free_image(image); return NULL; }
    return image;
}

static bool dc_add(dc_image *image, const char *name, uint64_t offset,
                   uint64_t size, uint64_t stored_size, unsigned mode,
                   uint64_t header_offset, uint64_t header_size) {
    dc_member *member;
    if (image->count >= DC_MAX_MEMBERS || size > DC_MAX_IMAGE_SIZE ||
        !dc_span(offset, stored_size, image->available))
        return false;
    member = &image->members[image->count++];
    xx_rt_snprintf(member->name, sizeof(member->name), "%s", name);
    member->offset = offset;
    member->size = size;
    member->stored_size = stored_size;
    member->header_offset = header_offset;
    member->header_size = header_size;
    member->mode = mode;
    return true;
}

static bool dc_valid(Abstractformat *f, xx_pd_struct *pd) {
    dc_image *image = dc_open(f, NULL, pd);
    bool valid = image != NULL;
    dc_free_image(image);
    return valid;
}
static bool dc_handle(Abstractformat *f, xx_pd_struct *pd) {
    dc_image *image = dc_open(f, NULL, pd);
    if (!image) {
        if (f) { f->is_valid = false; f->base_info_handled = false; }
        return false;
    }
    f->format_size = (int64_t)image->extent;
    f->number_of_archive_records = image->count;
    f->overlay_size = (int64_t)(image->available - image->extent);
    f->overlay_offset = f->overlay_size > 0
        ? f->base_address + (int64_t)image->extent : -1;
    f->base_info_handled = true;
    f->is_valid = true;
    dc_free_image(image);
    return true;
}
static int64_t dc_size(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled || dc_handle(f, pd)) ? f->format_size : -1;
}
static uint64_t dc_count(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled || dc_handle(f, pd))
        ? f->number_of_archive_records : 0U;
}

static bool dc_record(xx_archive_record_state *state) {
    dc_image *image = (dc_image *)state->internal_state;
    const dc_member *member = &image->members[image->index];
    xx_archive_record *record = &state->current_record;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = state->format->base_address + (int64_t)member->header_offset;
    record->header_size = (int64_t)member->header_size;
    record->data_offset = state->format->base_address + (int64_t)member->offset;
    record->compressed_size = (int64_t)member->stored_size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->stored_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->mode) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static xx_archive_record_state *dc_create_records(Abstractformat *f,
    const xx_list_s *options, xx_pd_struct *pd) {
    dc_image *image = dc_open(f, options, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!image) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { dc_free_image(image); return NULL; }
    xx_archive_record_state_init(state, f);
    state->internal_state = image;
    state->free_internal = dc_free_image;
    state->total_records = image->count;
    for (i = 0U; options && i < options->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(options, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_state_free(state);
            return NULL;
        }
    }
    state->has_record = dc_record(state);
    if (!state->has_record) { xx_archive_record_state_free(state); return NULL; }
    return state;
}
static const xx_archive_record *dc_current(Abstractformat *f,
                                          xx_archive_record_state *state) {
    return f && state && state->format == f && state->has_record
        ? &state->current_record : NULL;
}
static bool dc_next(Abstractformat *f, xx_archive_record_state *state,
                     xx_pd_struct *pd) {
    dc_image *image;
    if (!f || !state || state->format != f || !state->has_record || dc_stopped(pd))
        return false;
    image = (dc_image *)state->internal_state;
    if (++image->index >= image->count) {
        state->has_record = false;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        return false;
    }
    ++state->current_index;
    state->has_record = dc_record(state);
    return state->has_record;
}

/* A destination requests every guest byte. Verification consumes actual
 * stored bytes too, while a format-defined lazy zero tail needs no I/O.
 */
static bool dc_write_member(Abstractformat *f, const dc_image *image,
    size_t index, xx_io_device *output, const xx_list_s *options, xx_pd_struct *pd) {
    const dc_member *member;
    const xx_var *limit;
    uint8_t *buffer;
    uint64_t position = 0U;
    size_t capacity = xx_get_file_buffer_size();
    int64_t cursor;
    int level;
    bool valid = true;
    if (!f || index >= image->count || output == f->device || dc_stopped(pd)) return false;
    member = &image->members[index];
    limit = xx_format_resolve_extra_parameter(f, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && member->size > xx_var_get_u64(limit)) return false;
    if (!capacity || capacity > DC_IO_CAPACITY) capacity = DC_IO_CAPACITY;
    if (!dc_memory_limit(f, options, sizeof(*image) +
                        (uint64_t)image->map_count * sizeof(uint32_t) + capacity)) return false;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    cursor = xx_io_tell(f->device);
    level = xx_pd_enter_level(pd, member->size, "Extract disk container");
    while (position < member->size) {
        uint64_t remaining = member->size - position;
        uint64_t at;
        size_t piece = remaining < capacity ? (size_t)remaining : capacity;
        size_t written = 0U;
        if (dc_stopped(pd)) { valid = false; break; }
        if (member->mode == DC_WUX_MAP) {
            uint64_t block = position / image->block_size;
            uint32_t within = (uint32_t)(position % image->block_size);
            uint32_t left = image->block_size - within;
            if (block >= image->map_count) { valid = false; break; }
            if (piece > left) piece = left;
            at = image->data_offset + (uint64_t)image->map[(size_t)block] * image->block_size + within;
            if (!dc_read(f, image, at, buffer, piece, pd)) { valid = false; break; }
        }
#if defined(DC_NATIVE_DMK)
        else if (member->mode == DC_DMK_SECTORS) {
            uint8_t sector[8192];
            uint64_t block = position / image->block_size;
            uint32_t within = (uint32_t)(position % image->block_size);
            uint32_t left = image->block_size - within;
            if (piece > left) piece = left;
            if (block >= image->map_count ||
                !dc_dmk_sector(f, image, image->map[(size_t)block], sector, pd)) {
                valid = false; break;
            }
            xx_rt_memcpy(buffer, sector + within, piece);
        }
#endif
        else if (member->mode == DC_SAP_SECTORS) {
            uint8_t sector[256];
            uint64_t block = position / image->block_size;
            uint32_t within = (uint32_t)(position % image->block_size);
            uint32_t left = image->block_size - within;
            if (piece > left) piece = left;
            if (!dc_sap_sector(f, image, block, sector, pd)) { valid = false; break; }
            xx_rt_memcpy(buffer, sector + within, piece);
        } else if (member->mode == DC_LAZY_TAIL && position >= member->stored_size) {
            if (!output) { position = member->size; break; }
            xx_mem_zero(buffer, piece);
        } else {
            uint64_t stored_left = member->stored_size - position;
            if (piece > stored_left) piece = (size_t)stored_left;
            at = member->offset + position;
            if (!piece || !dc_read(f, image, at, buffer, piece, pd)) { valid = false; break; }
        }
        while (output && written < piece) {
            size_t request = piece - written;
            ssize_t amount;
            if (dc_stopped(pd)) { valid = false; break; }
            amount = xx_io_write(output, buffer + written, request);
            if (dc_stopped(pd) || amount <= 0 || (size_t)amount > request) {
                valid = false;
                break;
            }
            written += (size_t)amount;
        }
        if (!valid) break;
        position += piece;
        xx_pd_set_current(pd, level, position);
    }
    if (dc_stopped(pd)) valid = false;
    xx_pd_leave_level(pd, level);
    if (cursor >= 0 && xx_io_seek64(f->device, cursor, SEEK_SET)) valid = false;
    xx_mem_free(buffer);
    return valid;
}

static bool dc_unpack(Abstractformat *f, xx_archive_record_state *state,
                       xx_pd_struct *pd) {
    dc_image *image;
    const xx_var *option;
    const char *base;
    char *owned_base = NULL, *path = NULL, *stage_path = NULL;
    xx_io_device *output = NULL;
    bool valid = false, overwrite = false;
    unsigned attempt;
    size_t stage_capacity;
    if (!f || !state || state->format != f || !state->has_record || dc_stopped(pd)) return false;
    image = (dc_image *)state->internal_state;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return dc_write_member(f, image, image->index, NULL, &state->options, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW)
        base = owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
    else return false;
    if (!base) goto done;
    path = base[0] ? xx_str_concat3(base, "/", image->members[image->index].name)
                   : xx_str_dup(image->members[image->index].name);
    if (!path) goto done;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    if ((!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    if (xx_str_len(path) > SIZE_MAX - 50U) goto done;
    stage_capacity = xx_str_len(path) + 50U;
    stage_path = (char *)xx_mem_alloc(stage_capacity);
    if (!stage_path) goto done;
    /* Exclusive sibling staging preserves an existing destination on every
     * failure and allows publication to check overwrite atomically. */
    for (attempt = 0U; attempt < 128U && !dc_stopped(pd); ++attempt) {
        int length = xx_rt_snprintf(stage_path, stage_capacity,
            "%s.xxfc-disk-%u-%u.tmp", path, (unsigned)image->index, attempt);
        if (length <= 0 || (size_t)length >= stage_capacity) goto done;
        output = xx_io_file_open(stage_path, "wbx");
        if (output) break;
    }
    if (!output) goto done;
    valid = dc_write_member(f, image, image->index, output, &state->options, pd);
    if (xx_io_close(output)) valid = false;
    output = NULL;
    if (valid && !dc_stopped(pd)) valid = xx_io_file_replace_a(stage_path, path, overwrite);
    else valid = false;
    if (!valid) (void)xx_io_file_remove_a(stage_path);
done:
    if (output) { (void)xx_io_close(output); (void)xx_io_file_remove_a(stage_path); }
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return valid;
}

static void dc_free_records(Abstractformat *f, xx_archive_record_state *state) {
    (void)f;
    xx_archive_record_state_free(state);
}
static bool dc_unpack_device(Abstractformat *f, uint64_t index,
                             xx_io_device *output, xx_pd_struct *pd) {
    dc_image *image;
    bool valid;
    if (!output || !f || output == f->device || index >= DC_MAX_MEMBERS) return false;
    image = dc_open(f, NULL, pd);
    if (!image) return false;
    valid = dc_write_member(f, image, (size_t)index, output, NULL, pd);
    dc_free_image(image);
    return valid;
}

#define XX_DC_IMPLEMENT(name, type, extension, mime) \
static void name##_vtable_destroy(Abstractformat *f) { xx_##name##_destroy((xx_##name *)f); } \
void xx_##name##_init(xx_##name *r, xx_io_device *d, int64_t b) { \
    if (!r) return; \
    xx_mem_zero(r, sizeof(*r)); xx_format_init(&r->format, d, b); \
    r->format.endian = XX_ENDIAN_LITTLE; r->format.file_type = type; \
    r->format.format_type = XX_TYPE_ARCHIVE; r->format.is_archive = true; \
    xx_format_set_extension(&r->format, extension); xx_format_set_mime_type(&r->format, mime); \
    r->format.check_is_valid = xx_##name##_check_is_valid; \
    r->format.handle_base_info = xx_##name##_handle_base_info; \
    r->format.get_format_size = dc_size; r->format.get_number_of_archive_records = dc_count; \
    r->format.create_archive_records_reading = dc_create_records; \
    r->format.get_current_archive_record = dc_current; r->format.archive_record_move_to_next = dc_next; \
    r->format.unpack_current_archive_record = dc_unpack; r->format.free_archive_records_reading = dc_free_records; \
    r->format.destroy = name##_vtable_destroy; \
} \
xx_##name *xx_##name##_create(xx_io_device *d, int64_t b) { \
    xx_##name *r = (xx_##name *)xx_mem_alloc(sizeof(*r)); if (r) xx_##name##_init(r, d, b); return r; \
} \
void xx_##name##_destroy(xx_##name *r) { if (r) xx_format_cleanup_extra_parameters(&r->format); } \
void xx_##name##_free(xx_##name *r) { if (r) { xx_##name##_destroy(r); xx_mem_free(r); } } \
bool xx_##name##_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return dc_valid(f, pd); } \
bool xx_##name##_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return dc_handle(f, pd); } \
bool xx_##name##_unpack_to_device(xx_##name *r, uint64_t index, xx_io_device *out, xx_pd_struct *pd) { \
    return dc_unpack_device(r ? &r->format : NULL, index, out, pd); \
}

#endif
