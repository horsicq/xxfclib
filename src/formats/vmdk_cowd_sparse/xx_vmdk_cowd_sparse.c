/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * VMware COWD sparse extent: the ESX 2/GSX/Workstation 4 "COWDisk" and the
 * ESX 3+ vmfsSparse snapshot delta disk (*-delta.vmdk).
 *
 * Header (2048 bytes at offset 0, little endian)
 *   +0x000  "COWD"
 *   +0x004  u32  version (1)
 *   +0x008  u32  flags (bit 0: root disk; clear on a delta/child disk)
 *   +0x00c  u32  capacity, in 512-byte sectors
 *   +0x010  u32  grain size, in sectors
 *   +0x014  u32  grain directory sector
 *   +0x018  u32  number of grain directory entries
 *   +0x01c  u32  next free sector (end of the used part of the extent)
 *   +0x020  root: u32 cylinders, heads, sectors /
 *           child: char parentFileName[1024], u32 parentGeneration
 *   +0x424  u32  generation, then name[60], description[512], ...
 *
 * Unlike the "KDMV" sparse extent there is no descriptor, no redundant
 * directory and the grain-table size is fixed: every grain table holds 4096
 * u32 entries.  Grain g lives at table GD[g / 4096], entry g % 4096; both
 * levels store absolute sector numbers and a zero at either level is a hole.
 * On a delta disk a hole means "read the parent"; the parent is not part of
 * this file, so holes are presented as zeros.
 *
 * Written from the header layout; structure cross-checked against qemu-img
 * and libvmdk (pyvmdk) output only, no code taken from either.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vmdk_cowd_sparse/xx_vmdk_cowd_sparse.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef VMDK_COWD_SPARSE
#define XX_VMDK_COWD_SPARSE_FILE_TYPE XX_FILE_TYPE_VMDK_COWD_SPARSE
#else
#define XX_VMDK_COWD_SPARSE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define COWD_SECTOR 512U
#define COWD_HEADER_READ 512U
#define COWD_GT_ENTRIES 4096U
#define COWD_MAX_GRAIN_SECTORS (1U << 16U)
#define COWD_MAX_GD_ENTRIES (1U << 22U)
#define COWD_FLAG_ROOT 1U

typedef struct cowd_stream_s {
    char *name;
    int64_t size;          /* bytes available from base_address */
    int64_t format_size;   /* bytes the extent claims (free sector) */
    uint64_t unpacked_size;
    uint64_t gd_offset;    /* relative to base_address */
    uint64_t gd_entries;   /* entries actually needed for the capacity */
    uint64_t grain_sectors;
    uint32_t flags;
    bool done;
} cowd_stream;

static uint32_t cowd_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static bool cowd_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool cowd_write_all(xx_io_device *device, const void *data, size_t size,
                           xx_pd_struct *pd) {
    size_t done = 0U;
    if (!data && size != 0U) return false;
    if (!device) return true; /* verify-only pass */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Working buffer shared by grain copies and zero runs. */
typedef struct cowd_buffer_s {
    uint8_t *data;
    size_t capacity;
} cowd_buffer;

static bool cowd_copy_range(xx_io_device *source, int64_t offset,
                            uint64_t size, xx_io_device *destination,
                            cowd_buffer *buffer, xx_pd_struct *pd) {
    uint64_t left = size;
    if (!source || offset < 0) return false;
    if (!destination) return true;
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (left != 0U) {
        size_t want = left < buffer->capacity ? (size_t)left : buffer->capacity;
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer->data + done,
                                        want - done);
            if (amount <= 0 || (size_t)amount > want - done) return false;
            done += (size_t)amount;
        }
        if (!cowd_write_all(destination, buffer->data, want, pd)) return false;
        left -= want;
    }
    return true;
}

static bool cowd_write_zeros(xx_io_device *destination, uint64_t size,
                             cowd_buffer *buffer, xx_pd_struct *pd) {
    uint64_t left = size;
    if (!destination || size == 0U) return true;
    xx_mem_zero(buffer->data, buffer->capacity);
    while (left != 0U) {
        size_t want = left < buffer->capacity ? (size_t)left : buffer->capacity;
        if (!cowd_write_all(destination, buffer->data, want, pd)) return false;
        left -= want;
    }
    return true;
}

static void cowd_stream_free(void *opaque) {
    cowd_stream *stream = (cowd_stream *)opaque;
    if (!stream) return;
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static char *cowd_make_name(void) {
    static const char name[] = "disk.img";
    char *result = (char *)xx_mem_alloc(sizeof(name));
    if (result) xx_mem_copy(result, name, sizeof(name));
    return result;
}

/* Header validation only: one 512-byte read, no allocation beyond the
 * stream itself.  This is what the detector's probe runs. */
static bool cowd_parse(Abstractformat *format, cowd_stream **result) {
    uint8_t header[COWD_HEADER_READ];
    cowd_stream *stream;
    int64_t total, size;
    uint64_t capacity, grain, gd_sector, gd_entries, span, needed, gd_end;
    uint64_t free_end;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    *result = NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)COWD_HEADER_READ ||
        !cowd_read_at(format->device, format->base_address, header,
                      sizeof(header)) ||
        xx_rt_memcmp(header, "COWD", 4U) != 0 || cowd_le32(header + 4U) != 1U)
        return false;

    capacity = cowd_le32(header + 0x0cU);
    grain = cowd_le32(header + 0x10U);
    gd_sector = cowd_le32(header + 0x14U);
    gd_entries = cowd_le32(header + 0x18U);
    if (capacity == 0U || grain == 0U || grain > COWD_MAX_GRAIN_SECTORS ||
        gd_sector == 0U || gd_entries == 0U ||
        gd_entries > COWD_MAX_GD_ENTRIES)
        return false;
    span = grain * COWD_GT_ENTRIES;          /* <= 2^28 */
    needed = (capacity + span - 1U) / span;  /* <= 2^20 */
    if (needed == 0U || gd_entries < needed) return false;
    gd_end = gd_sector * COWD_SECTOR + needed * 4U; /* < 2^42 */
    if (gd_end > (uint64_t)size) return false;

    stream = (cowd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->name = cowd_make_name();
    if (!stream->name) {
        cowd_stream_free(stream);
        return false;
    }
    stream->size = size;
    free_end = (uint64_t)cowd_le32(header + 0x1cU) * COWD_SECTOR;
    stream->format_size = (free_end >= gd_end && free_end <= (uint64_t)size)
                              ? (int64_t)free_end
                              : size;
    stream->unpacked_size = capacity * COWD_SECTOR;
    stream->gd_offset = gd_sector * COWD_SECTOR;
    stream->gd_entries = needed;
    stream->grain_sectors = grain;
    stream->flags = cowd_le32(header + 8U);
    *result = stream;
    return true;
}

static bool cowd_write_disk(Abstractformat *format, const cowd_stream *stream,
                            xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *directory = NULL;
    uint8_t *table = NULL;
    cowd_buffer buffer = {NULL, 0U};
    uint64_t grain_bytes, produced = 0U, pending_zero = 0U, d;
    const uint64_t table_bytes = (uint64_t)COWD_GT_ENTRIES * 4U;
    const int64_t base = format->base_address;
    bool result = false;

    grain_bytes = stream->grain_sectors * COWD_SECTOR;
    if (stream->gd_entries > (uint64_t)(SIZE_MAX / 4U)) return false;
    buffer.capacity = xx_get_file_buffer_size();
    if (buffer.capacity < COWD_SECTOR) buffer.capacity = COWD_SECTOR;
    if (buffer.capacity > (size_t)1U << 24U) buffer.capacity = (size_t)1U << 24U;
    directory = (uint8_t *)xx_mem_alloc((size_t)stream->gd_entries * 4U);
    table = (uint8_t *)xx_mem_alloc((size_t)table_bytes);
    buffer.data = (uint8_t *)xx_mem_alloc(buffer.capacity);
    if (!directory || !table || !buffer.data ||
        !cowd_read_at(format->device, base + (int64_t)stream->gd_offset,
                      directory, (size_t)stream->gd_entries * 4U))
        goto done;

    for (d = 0U; d < stream->gd_entries &&
                 produced + pending_zero < stream->unpacked_size; ++d) {
        uint64_t table_offset = (uint64_t)cowd_le32(directory + d * 4U) *
                                COWD_SECTOR;
        uint32_t t;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (table_offset == 0U) {
            uint64_t left = stream->unpacked_size - produced - pending_zero;
            uint64_t run = grain_bytes * COWD_GT_ENTRIES;
            pending_zero += left < run ? left : run;
            continue;
        }
        if (table_offset > (uint64_t)stream->size ||
            table_bytes > (uint64_t)stream->size - table_offset ||
            !cowd_read_at(format->device, base + (int64_t)table_offset, table,
                          (size_t)table_bytes))
            goto done;
        for (t = 0U; t < COWD_GT_ENTRIES &&
                     produced + pending_zero < stream->unpacked_size; ++t) {
            uint64_t left = stream->unpacked_size - produced - pending_zero;
            uint64_t output = left < grain_bytes ? left : grain_bytes;
            uint64_t grain_offset = (uint64_t)cowd_le32(table + t * 4U) *
                                    COWD_SECTOR;
            if (grain_offset == 0U) {
                pending_zero += output;
                continue;
            }
            if (grain_offset > (uint64_t)stream->size ||
                output > (uint64_t)stream->size - grain_offset)
                goto done;
            if (!cowd_write_zeros(destination, pending_zero, &buffer, pd))
                goto done;
            produced += pending_zero;
            pending_zero = 0U;
            if (!cowd_copy_range(format->device, base + (int64_t)grain_offset,
                                 output, destination, &buffer, pd))
                goto done;
            produced += output;
        }
    }
    if (!cowd_write_zeros(destination, pending_zero, &buffer, pd)) goto done;
    produced += pending_zero;
    result = produced == stream->unpacked_size;
done:
    if (directory) xx_mem_free(directory);
    if (table) xx_mem_free(table);
    if (buffer.data) xx_mem_free(buffer.data);
    return result;
}

static bool cowd_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *cowd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool cowd_set_record(Abstractformat *format, xx_archive_record *record,
                            const cowd_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = COWD_HEADER_READ * 4U;
    record->data_offset = format->base_address;
    record->compressed_size = stream->format_size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->format_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          stream->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          stream->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

void xx_vmdk_cowd_sparse_init(xx_vmdk_cowd_sparse *archive,
                              xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VMDK_COWD_SPARSE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vmdk");
    xx_format_set_extension(&archive->format, "vmdk");
    archive->format.check_is_valid = xx_vmdk_cowd_sparse_check_is_valid;
    archive->format.handle_base_info = xx_vmdk_cowd_sparse_handle_base_info;
    archive->format.get_format_size = xx_vmdk_cowd_sparse_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vmdk_cowd_sparse_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vmdk_cowd_sparse_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vmdk_cowd_sparse_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vmdk_cowd_sparse_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vmdk_cowd_sparse_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vmdk_cowd_sparse_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_vmdk_cowd_sparse *xx_vmdk_cowd_sparse_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_vmdk_cowd_sparse *archive =
        (xx_vmdk_cowd_sparse *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vmdk_cowd_sparse_init(archive, device, base_address);
    return archive;
}

void xx_vmdk_cowd_sparse_destroy(xx_vmdk_cowd_sparse *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_vmdk_cowd_sparse_free(xx_vmdk_cowd_sparse *archive) {
    if (!archive) return;
    xx_vmdk_cowd_sparse_destroy(archive);
    xx_mem_free(archive);
}

bool xx_vmdk_cowd_sparse_check_is_valid(Abstractformat *format,
                                        xx_pd_struct *pd) {
    cowd_stream *stream;
    (void)pd;
    if (!cowd_parse(format, &stream)) return false;
    cowd_stream_free(stream);
    return true;
}

bool xx_vmdk_cowd_sparse_handle_base_info(Abstractformat *format,
                                          xx_pd_struct *pd) {
    cowd_stream *stream;
    xx_vmdk_cowd_sparse *archive;
    (void)pd;
    if (!format || !cowd_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_vmdk_cowd_sparse *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + stream->format_size;
    format->number_of_archive_records = 1U;
    format->format_size = stream->format_size;
    format->file_type = XX_VMDK_COWD_SPARSE_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    cowd_stream_free(stream);
    return true;
}

int64_t xx_vmdk_cowd_sparse_get_format_size(Abstractformat *format,
                                            xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmdk_cowd_sparse_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_vmdk_cowd_sparse_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmdk_cowd_sparse_handle_base_info(format, pd))
               ? ((xx_vmdk_cowd_sparse *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_vmdk_cowd_sparse_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    cowd_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!cowd_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        cowd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = cowd_stream_free;
    state->total_records = 1U;
    if (!cowd_copy_options(&state->options, options) ||
        !cowd_set_record(format, &state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_vmdk_cowd_sparse_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vmdk_cowd_sparse_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    cowd_stream *stream;
    (void)pd;
    if (format && state && state->format == format &&
        (stream = (cowd_stream *)state->internal_state) != NULL)
        stream->done = true;
    if (state) state->has_record = false;
    return false;
}

bool xx_vmdk_cowd_sparse_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    cowd_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (cowd_stream *)state->internal_state) || stream->done ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = cowd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return cowd_write_disk(format, stream, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* The member name is the constant "disk.img", never taken from the file. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    if (!destination) goto done;
    created = true;
    result = cowd_write_disk(format, stream, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vmdk_cowd_sparse_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
