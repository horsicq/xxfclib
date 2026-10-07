/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * VMware seSparse ("space-efficient sparse") VMDK extent, the binary
 * *-sesparse.vmdk file ESXi 6.5+ uses for snapshot deltas and SE disks.
 * The text descriptor that names it (extent type SESPARSE) is not needed:
 * the extent carries its own capacity.
 *
 * Constant header (sector 0, 26 little-endian u64, then zero padding):
 *   +0x00 magic 0x00000000CAFEBABE     +0x08 version 0x0000000200000001
 *   +0x10 capacity (sectors)           +0x18 grain size (sectors, 8)
 *   +0x20 grain table size (sectors, 64)  +0x28 flags (0)
 *   +0x30..+0x48 reserved
 *   +0x50/+0x58 volatile header offset/size    +0x60/+0x68 journal header
 *   +0x70/+0x78 journal                        +0x80/+0x88 grain directory
 *   +0x90/+0x98 grain tables                   +0xa0/+0xa8 free bitmap
 *   +0xb0/+0xb8 back map                       +0xc0/+0xc8 grains
 *   (all offsets and sizes in 512-byte sectors)
 * Volatile header: u64 magic 0x00000000CAFECAFE, free GT number, next
 * transaction number, replay-journal flag.  A set replay flag means the
 * metadata is dirty; such an image is refused, as VMware and qemu do.
 *
 * Mapping (4 KiB grains, 4096 u64 entries per 32 KiB grain table, so one
 * table covers 16 MiB):
 *   directory entry: 0 = no table (whole 16 MiB unallocated);
 *                    0x10000000_iiiiiiii = grain table number i, found at
 *                    gt_offset + i * 64 sectors; anything else is corrupt.
 *   table entry, by top nibble:
 *                    0 = unallocated (entry must be exactly 0)
 *                    1 = SCSI-unmapped, 2 = zeroed: both read as zero
 *                    3 = allocated; grain number = bits 48..59 as the low
 *                        12 bits and bits 0..47 as the high bits, found at
 *                        grains_offset + number * 8 sectors
 *                    other = corrupt
 * Unallocated space reads as zero (a delta's parent is not consulted).
 *
 * Written from the format structure; qemu block/vmdk.c (GPL) was used only
 * to understand the layout, no code was taken from it.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vmdk_sesparse/xx_vmdk_sesparse.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

#ifdef VMDK_SESPARSE
#define XX_VMDK_SESPARSE_FILE_TYPE XX_FILE_TYPE_VMDK_SESPARSE
#else
#define XX_VMDK_SESPARSE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SES_SECTOR 512U
#define SES_HEADER_SIZE 512U
#define SES_CONST_MAGIC UINT64_C(0x00000000CAFEBABE)
#define SES_VOLATILE_MAGIC UINT64_C(0x00000000CAFECAFE)
#define SES_VERSION UINT64_C(0x0000000200000001)
#define SES_GRAIN_SECTORS 8U
#define SES_GRAIN_BYTES (SES_GRAIN_SECTORS * SES_SECTOR)       /* 4096 */
#define SES_GT_SECTORS 64U
#define SES_GT_BYTES (SES_GT_SECTORS * SES_SECTOR)             /* 32768 */
#define SES_GT_ENTRIES (SES_GT_BYTES / 8U)                     /* 4096 */
#define SES_GT_COVER_SECTORS ((uint64_t)SES_GT_ENTRIES * SES_GRAIN_SECTORS)
/* 64 TiB, the largest seSparse disk VMware defines. */
#define SES_MAX_CAPACITY ((uint64_t)1U << 37U)
#define SES_MAX_OFFSET_SECTORS ((uint64_t)1U << 44U)
#define SES_ZERO_CHUNK 65536U

#define SES_TAG_MASK UINT64_C(0xF000000000000000)
#define SES_GD_HIGH_MASK UINT64_C(0xFFFFFFFF00000000)
#define SES_GD_ALLOCATED UINT64_C(0x1000000000000000)

typedef struct ses_info_s {
    int64_t size;            /* bytes from base to end of device */
    uint64_t capacity;       /* sectors */
    uint64_t gd_offset;      /* bytes, relative to base */
    uint64_t gd_entries;     /* directory entries actually needed */
    uint64_t gt_offset;      /* bytes, relative to base */
    uint64_t grains_offset;  /* bytes, relative to base */
} ses_info;

typedef struct ses_stream_s {
    ses_info info;
    char *name;
    size_t index;
} ses_stream;

static bool ses_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool ses_write_all(xx_io_device *device, const void *data, size_t size,
                          xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device) return true; /* verify-only pass */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool ses_write_zeros(xx_io_device *destination, const uint8_t *zeros,
                            uint64_t size, xx_pd_struct *pd) {
    if (!destination) return true;
    while (size != 0U) {
        size_t want = size < SES_ZERO_CHUNK ? (size_t)size : SES_ZERO_CHUNK;
        if (!ses_write_all(destination, zeros, want, pd)) return false;
        size -= want;
    }
    return true;
}

/* A byte range [offset, offset + length) that must lie inside the extent. */
static bool ses_range_ok(uint64_t offset, uint64_t length, int64_t size) {
    return size >= 0 && offset <= (uint64_t)size &&
           length <= (uint64_t)size - offset;
}

static bool ses_region_offset(uint64_t sectors, uint64_t *bytes) {
    if (sectors == 0U || sectors > SES_MAX_OFFSET_SECTORS) return false;
    *bytes = sectors * SES_SECTOR;
    return true;
}

static bool ses_parse(Abstractformat *format, ses_info *info) {
    uint8_t header[SES_HEADER_SIZE];
    uint8_t volatile_header[SES_HEADER_SIZE];
    int64_t total;
    uint64_t volatile_offset, gd_sectors, gd_bytes_avail;

    if (!format || !format->device || !info || format->base_address < 0)
        return false;
    xx_mem_zero(info, sizeof(*info));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    info->size = total - format->base_address;
    if (info->size < (int64_t)(2U * SES_HEADER_SIZE) ||
        !ses_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;
    if (xx_data_get_u64(header, 8, 0, false) != SES_CONST_MAGIC ||
        xx_data_get_u64(header + 0x08U, 8, 0, false) != SES_VERSION ||
        xx_data_get_u64(header + 0x18U, 8, 0, false) != SES_GRAIN_SECTORS ||
        xx_data_get_u64(header + 0x20U, 8, 0, false) != SES_GT_SECTORS ||
        xx_data_get_u64(header + 0x28U, 8, 0, false) != 0U)
        return false;

    info->capacity = xx_data_get_u64(header + 0x10U, 8, 0, false);
    if (info->capacity == 0U || info->capacity > SES_MAX_CAPACITY) return false;

    /* Volatile header: must exist, carry its magic and be clean. */
    if (!ses_region_offset(xx_data_get_u64(header + 0x50U, 8, 0, false), &volatile_offset) ||
        !ses_range_ok(volatile_offset, SES_HEADER_SIZE, info->size) ||
        !ses_read_at(format->device,
                     format->base_address + (int64_t)volatile_offset,
                     volatile_header, sizeof(volatile_header)) ||
        xx_data_get_u64(volatile_header, 8, 0, false) != SES_VOLATILE_MAGIC ||
        xx_data_get_u64(volatile_header + 0x18U, 8, 0, false) != 0U)
        return false;

    /* Grain directory: enough entries for the capacity, all in the file. */
    info->gd_entries =
        (info->capacity + SES_GT_COVER_SECTORS - 1U) / SES_GT_COVER_SECTORS;
    gd_sectors = xx_data_get_u64(header + 0x88U, 8, 0, false);
    if (gd_sectors > SES_MAX_OFFSET_SECTORS ||
        gd_sectors * (SES_SECTOR / 8U) < info->gd_entries)
        return false;
    if (!ses_region_offset(xx_data_get_u64(header + 0x80U, 8, 0, false), &info->gd_offset))
        return false;
    gd_bytes_avail = info->gd_entries * 8U;
    if (!ses_range_ok(info->gd_offset, gd_bytes_avail, info->size))
        return false;

    /* Table and grain areas: only their start is fixed here; every table
     * and grain is range-checked when it is used. */
    if (!ses_region_offset(xx_data_get_u64(header + 0x90U, 8, 0, false), &info->gt_offset) ||
        !ses_region_offset(xx_data_get_u64(header + 0xc0U, 8, 0, false), &info->grains_offset) ||
        info->gt_offset >= (uint64_t)info->size)
        return false;
    return true;
}

/* Walk the whole map.  With destination == NULL nothing is written and no
 * grain data is read, but every directory and table entry is still read and
 * validated, so a verify pass fails exactly where an extraction would. */
static bool ses_write_disk(Abstractformat *format, const ses_info *info,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *table = NULL;
    uint8_t *zeros = NULL;
    uint8_t *grain = NULL;
    uint64_t total_bytes = info->capacity * SES_SECTOR;
    uint64_t produced = 0U;
    uint64_t directory_index;
    bool result = false;

    table = (uint8_t *)xx_mem_alloc(SES_GT_BYTES);
    zeros = (uint8_t *)xx_mem_alloc(SES_ZERO_CHUNK);
    grain = (uint8_t *)xx_mem_alloc(SES_GRAIN_BYTES);
    if (!table || !zeros || !grain) goto done;
    xx_mem_zero(zeros, SES_ZERO_CHUNK);

    for (directory_index = 0U; directory_index < info->gd_entries;
         ++directory_index) {
        uint8_t raw[8];
        uint64_t entry, cover, table_number, table_offset;
        uint32_t index;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        cover = total_bytes - produced;
        if (cover > (uint64_t)SES_GT_ENTRIES * SES_GRAIN_BYTES)
            cover = (uint64_t)SES_GT_ENTRIES * SES_GRAIN_BYTES;
        if (!ses_read_at(format->device,
                         format->base_address +
                             (int64_t)(info->gd_offset + directory_index * 8U),
                         raw, sizeof(raw)))
            goto done;
        entry = xx_data_get_u64(raw, 8, 0, false);
        if (entry == 0U) {
            if (!ses_write_zeros(destination, zeros, cover, pd)) goto done;
            produced += cover;
            continue;
        }
        if ((entry & SES_GD_HIGH_MASK) != SES_GD_ALLOCATED) goto done;
        table_number = entry & UINT64_C(0xFFFFFFFF);
        table_offset = info->gt_offset + table_number * SES_GT_BYTES;
        if (!ses_range_ok(table_offset, SES_GT_BYTES, info->size) ||
            !ses_read_at(format->device,
                         format->base_address + (int64_t)table_offset, table,
                         SES_GT_BYTES))
            goto done;

        for (index = 0U; index < SES_GT_ENTRIES && produced < total_bytes;
             ++index) {
            uint64_t value = xx_data_get_u64(table + (size_t)index * 8U, 8, 0, false);
            uint64_t left = total_bytes - produced;
            uint64_t output = left < SES_GRAIN_BYTES ? left : SES_GRAIN_BYTES;
            switch (value & SES_TAG_MASK) {
            case UINT64_C(0x0000000000000000):
                if (value != 0U) goto done;
                /* fall through - unallocated reads as zero */
            case UINT64_C(0x1000000000000000):
            case UINT64_C(0x2000000000000000):
                if (!ses_write_zeros(destination, zeros, output, pd))
                    goto done;
                break;
            case UINT64_C(0x3000000000000000): {
                uint64_t number = ((value >> 48U) & 0xFFFU) |
                                  ((value & UINT64_C(0x0000FFFFFFFFFFFF))
                                   << 12U);
                uint64_t offset;
                if (number > (UINT64_MAX - info->grains_offset) /
                                 SES_GRAIN_BYTES)
                    goto done;
                offset = info->grains_offset + number * SES_GRAIN_BYTES;
                if (!ses_range_ok(offset, output, info->size)) goto done;
                if (destination) {
                    if (!ses_read_at(format->device,
                                     format->base_address + (int64_t)offset,
                                     grain, (size_t)output) ||
                        !ses_write_all(destination, grain, (size_t)output, pd))
                        goto done;
                }
                break;
            }
            default:
                goto done;
            }
            produced += output;
        }
    }
    result = produced == total_bytes;
done:
    xx_mem_free(table);
    xx_mem_free(zeros);
    xx_mem_free(grain);
    return result;
}

static bool ses_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0]) return false;
    for (at = name; *at; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    return !(name[0] == '.' && (name[1] == 0 || (name[1] == '.' && !name[2])));
}

static char *ses_dup(const char *text) {
    size_t length = xx_str_len(text);
    char *copy = (char *)xx_mem_alloc(length + 1U);
    if (copy) xx_mem_copy(copy, text, length + 1U);
    return copy;
}

static void ses_stream_free(void *opaque) {
    ses_stream *stream = (ses_stream *)opaque;
    if (!stream) return;
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool ses_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ses_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ses_set_record(Abstractformat *format, xx_archive_record *record,
                           const ses_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = SES_HEADER_SIZE;
    record->data_offset = format->base_address;
    record->compressed_size = stream->info.size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->info.size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          stream->info.capacity * SES_SECTOR) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

void xx_vmdk_sesparse_init(xx_vmdk_sesparse *archive, xx_io_device *device,
                           int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VMDK_SESPARSE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vmdk");
    xx_format_set_extension(&archive->format, "vmdk");
    archive->format.check_is_valid = xx_vmdk_sesparse_check_is_valid;
    archive->format.handle_base_info = xx_vmdk_sesparse_handle_base_info;
    archive->format.get_format_size = xx_vmdk_sesparse_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vmdk_sesparse_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vmdk_sesparse_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vmdk_sesparse_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vmdk_sesparse_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vmdk_sesparse_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vmdk_sesparse_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_vmdk_sesparse *xx_vmdk_sesparse_create(xx_io_device *device,
                                          int64_t base_address) {
    xx_vmdk_sesparse *archive =
        (xx_vmdk_sesparse *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vmdk_sesparse_init(archive, device, base_address);
    return archive;
}

void xx_vmdk_sesparse_destroy(xx_vmdk_sesparse *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_vmdk_sesparse_free(xx_vmdk_sesparse *archive) {
    if (!archive) return;
    xx_vmdk_sesparse_destroy(archive);
    xx_mem_free(archive);
}

bool xx_vmdk_sesparse_check_is_valid(Abstractformat *format,
                                     xx_pd_struct *pd) {
    ses_info info;
    (void)pd;
    return ses_parse(format, &info);
}

bool xx_vmdk_sesparse_handle_base_info(Abstractformat *format,
                                       xx_pd_struct *pd) {
    ses_info info;
    xx_vmdk_sesparse *archive;
    (void)pd;
    if (!format || !ses_parse(format, &info)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_vmdk_sesparse *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + info.size;
    format->number_of_archive_records = 1U;
    format->format_size = info.size;
    format->file_type = XX_VMDK_SESPARSE_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_vmdk_sesparse_get_format_size(Abstractformat *format,
                                         xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmdk_sesparse_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_vmdk_sesparse_get_number_of_archive_records(Abstractformat *format,
                                                        xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmdk_sesparse_handle_base_info(format, pd))
               ? ((xx_vmdk_sesparse *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_vmdk_sesparse_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ses_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    stream = (ses_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!ses_parse(format, &stream->info) ||
        !(stream->name = ses_dup("disk.img"))) {
        ses_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ses_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ses_stream_free;
    state->total_records = 1U;
    if (!ses_copy_options(&state->options, options) ||
        !ses_set_record(format, &state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_vmdk_sesparse_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vmdk_sesparse_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ses_stream *stream;
    (void)pd;
    if (state) state->has_record = false;
    if (!format || !state || state->format != format ||
        !(stream = (ses_stream *)state->internal_state))
        return false;
    stream->index = 1U;
    return false;
}

bool xx_vmdk_sesparse_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ses_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ses_stream *)state->internal_state) ||
        stream->index != 0U || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!ses_safe_output_name(stream->name)) return false;
    path_option = ses_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return ses_write_disk(format, &stream->info, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    if (!destination) goto done;
    created = true;
    result = ses_write_disk(format, &stream->info, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vmdk_sesparse_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
