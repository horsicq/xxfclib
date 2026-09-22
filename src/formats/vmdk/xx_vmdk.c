/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * VMware VMDK, monolithic sparse extent.
 *
 * Sparse header (512 bytes at offset 0, little endian)
 *   +0x00  "KDMV"
 *   +0x04  u32  version
 *   +0x08  u32  flags
 *   +0x0c  u64  capacity, in 512-byte sectors
 *   +0x14  u64  grain size, in sectors
 *   +0x1c  u64  descriptor offset   +0x24  u64  descriptor size
 *   +0x2c  u32  grain-table entries per grain table
 *   +0x30  u64  redundant grain directory sector
 *   +0x38  u64  grain directory sector
 *   +0x40  u64  overhead
 *   +0x48  u8   unclean shutdown
 *   +0x49  4    '\n', ' ', '\r', '\n'  (the newline-damage detector)
 *   +0x4d  u16  compression algorithm; non-zero is a stream-optimized
 *                image with a different layout and is rejected here
 *
 * Grain g lives at grain table GD[g / GTEs], entry g % GTEs; a zero at
 * either level is a hole and reads back as zero, which is how a sparse
 * extent describes far more disk than the file holds.  Entry value 1 is
 * the explicit all-zero marker.
 *
 * Ported from XArchive diskimages/xvmdkarchive.cpp; U3 implements the same
 * format as archive/13 (class ofa, VMT 0x0049c188).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vmdk/xx_vmdk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef VMDK
#define XX_VMDK_FILE_TYPE XX_FILE_TYPE_VMDK
#else
#define XX_VMDK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define VMDK_MAX_MEMBERS 16U

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct vmdk_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint64_t timestamp;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
    uint32_t method;
    uint32_t crc32;
    uint32_t attributes;
    uint32_t flags;
    bool has_crc;
    bool encrypted;
    bool folder;
} vmdk_member;

typedef struct vmdk_stream_s {
    vmdk_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} vmdk_stream;

static uint16_t vmdk_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t vmdk_le32(const uint8_t *b) {
    return (uint32_t)vmdk_le16(b) | ((uint32_t)vmdk_le16(b + 2U) << 16U);
}

static uint64_t vmdk_le64(const uint8_t *b) {
    return (uint64_t)vmdk_le32(b) | ((uint64_t)vmdk_le32(b + 4U) << 32U);
}

static uint32_t vmdk_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t vmdk_be64(const uint8_t *b) {
    return ((uint64_t)vmdk_be32(b) << 32U) | (uint64_t)vmdk_be32(b + 4U);
}

static bool vmdk_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool vmdk_write_all(xx_io_device *device, const void *data, size_t size,
                          xx_pd_struct *pd) {
    size_t done = 0U;
    if (!data && size != 0U) return false;
    if (!device) return true; /* verify-only pass: nothing is materialized */
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

/* Copy a run of source bytes straight through to the destination. */
static bool vmdk_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!source || offset < 0) return false;
    if (!destination) return true;
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer + done, want - done);
            if (amount <= 0 || (size_t)amount > want - done) return false;
            done += (size_t)amount;
        }
        if (!vmdk_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool vmdk_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_mem_zero(buffer, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!vmdk_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *vmdk_make_name(const char *prefix, int64_t index,
                           const char *suffix) {
    char buffer[96];
    size_t used = 0U;
    size_t at;
    char *result;
    for (at = 0U; prefix && prefix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[at];
    }
    if (index >= 0) {
        char digits[24];
        size_t count = 0U;
        int64_t value = index;
        do {
            digits[count++] = (char)('0' + (int)(value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 4U && count < sizeof(digits)) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    for (at = 0U; suffix && suffix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[at];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

/* Names that DO come from the container are normalized here: separators are
 * unified, traversal components are removed and anything a filesystem would
 * choke on becomes '_'. */
static char *vmdk_clean_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start, end, component_start;
        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && bytes[start] == '.'))
            continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|' || c == 0U)
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool vmdk_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void vmdk_stream_free(void *opaque) {
    vmdk_stream *stream = (vmdk_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool vmdk_add_member(vmdk_stream *stream, const vmdk_member *member) {
    vmdk_member *grown;
    if (!stream || !member || stream->count >= VMDK_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (vmdk_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define VMDK_SECTOR 512
#define VMDK_HEADER_SIZE 512
#define VMDK_MAX_GD_ENTRIES (1U << 22U)
#define VMDK_MAX_GTES (1U << 20U)
#define VMDK_MAX_GRAIN_SECTORS (1U << 16U)

/* VMware sparse extent ("KDMV").  capacity, grainSize and numGTEsPerGT give a
 * two-level grain directory: directory entry -> grain table sector, table
 * entry -> grain sector.  A zero at either level is a hole, which is why a
 * sparse extent can describe far more disk than the file holds. */
static bool vmdk_parse(Abstractformat *format, vmdk_stream **result) {
    uint8_t header[VMDK_HEADER_SIZE];
    vmdk_stream *stream = NULL;
    vmdk_member member;
    int64_t total, size;
    uint64_t capacity, grain_sectors, gd_sector, span, entries, gd_bytes;
    uint32_t gtes;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= VMDK_HEADER_SIZE ||
        !vmdk_read_at(format->device, format->base_address, header,
                      sizeof(header)) ||
        xx_rt_memcmp(header, "KDMV", 4U) != 0)
        return false;

    /* A compressed (stream-optimized) extent is a different layout and is not
     * presentable as a flat disk, so it is rejected rather than guessed at. */
    if (vmdk_le16(header + 0x4dU) != 0U) return false;

    capacity = vmdk_le64(header + 0x0cU);
    grain_sectors = vmdk_le64(header + 0x14U);
    gtes = vmdk_le32(header + 0x2cU);
    gd_sector = vmdk_le64(header + 0x38U);

    /* Every one of these came out of the file: none may be trusted to be
     * positive, in range, or free of overflow when multiplied. */
    if (grain_sectors == 0U || grain_sectors > VMDK_MAX_GRAIN_SECTORS ||
        gtes == 0U || gtes > VMDK_MAX_GTES || capacity == 0U ||
        capacity > ((uint64_t)1U << 40U))
        return false;
    if (gd_sector == 0U || gd_sector > (uint64_t)(size / VMDK_SECTOR))
        return false;
    span = grain_sectors * gtes;
    if (span == 0U) return false;
    entries = (capacity + span - 1U) / span;
    if (entries == 0U || entries > VMDK_MAX_GD_ENTRIES) return false;
    gd_bytes = entries * 4U;
    if (gd_sector * VMDK_SECTOR > (uint64_t)size ||
        gd_bytes > (uint64_t)size - gd_sector * VMDK_SECTOR)
        return false;

    stream = (vmdk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->aux0 = gd_sector * VMDK_SECTOR;
    stream->aux1 = grain_sectors;
    stream->aux2 = gtes;

    xx_mem_zero(&member, sizeof(member));
    member.name = vmdk_make_name("disk", -1, ".img");
    if (!member.name) goto fail;
    member.header_offset = format->base_address;
    member.header_size = VMDK_HEADER_SIZE;
    member.data_offset = format->base_address;
    member.packed_size = size;
    member.unpacked_size = capacity * VMDK_SECTOR;
    member.method = 1U; /* sparse extent: grain directory + grain tables */
    member.aux0 = entries;
    member.flags = vmdk_le32(header + 8U);
    if (!vmdk_add_member(stream, &member)) {
        xx_mem_free(member.name);
        goto fail;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    vmdk_stream_free(stream);
    return false;
}

static bool vmdk_write_member(Abstractformat *format, vmdk_stream *stream,
                              const vmdk_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *directory = NULL;
    uint8_t *table = NULL;
    uint64_t grain_sectors, gtes, gd_offset, entries;
    uint64_t grain_bytes, grains, grain, produced = 0U;
    uint64_t cached = UINT64_C(0xffffffffffffffff);
    bool result = false;

    if (!format || !stream || !member) return false;
    gd_offset = stream->aux0;
    grain_sectors = stream->aux1;
    gtes = stream->aux2;
    entries = member->aux0;
    if (grain_sectors == 0U || gtes == 0U || entries == 0U) return false;
    grain_bytes = grain_sectors * VMDK_SECTOR;
    grains = (member->unpacked_size + grain_bytes - 1U) / grain_bytes;
    if (entries > (uint64_t)SIZE_MAX / 4U || gtes > (uint64_t)SIZE_MAX / 4U)
        return false;

    directory = (uint8_t *)xx_mem_alloc((size_t)entries * 4U);
    table = (uint8_t *)xx_mem_alloc((size_t)gtes * 4U);
    if (!directory || !table ||
        !vmdk_read_at(format->device, member->data_offset + (int64_t)gd_offset,
                      directory, (size_t)entries * 4U))
        goto done;

    for (grain = 0U; grain < grains; ++grain) {
        uint64_t directory_index = grain / gtes;
        uint64_t table_index = grain % gtes;
        uint64_t left = member->unpacked_size - produced;
        uint64_t output = left < grain_bytes ? left : grain_bytes;
        uint32_t table_sector, grain_sector;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (directory_index >= entries) goto done;
        table_sector = vmdk_le32(directory + directory_index * 4U);
        if (table_sector == 0U) {
            if (!vmdk_write_zeros(destination, output, pd)) goto done;
            produced += output;
            continue;
        }
        if (cached != table_sector) {
            uint64_t offset = (uint64_t)table_sector * VMDK_SECTOR;
            uint64_t bytes = gtes * 4U;
            if (offset > (uint64_t)member->packed_size ||
                bytes > (uint64_t)member->packed_size - offset)
                goto done;
            if (!vmdk_read_at(format->device,
                              member->data_offset + (int64_t)offset, table,
                              (size_t)bytes))
                goto done;
            cached = table_sector;
        }
        grain_sector = vmdk_le32(table + table_index * 4U);
        if (grain_sector == 0U || grain_sector == 1U) {
            /* 0 is unallocated and 1 is the sparse-extent "all zero" marker. */
            if (!vmdk_write_zeros(destination, output, pd)) goto done;
        } else {
            uint64_t offset = (uint64_t)grain_sector * VMDK_SECTOR;
            if (offset > (uint64_t)member->packed_size ||
                output > (uint64_t)member->packed_size - offset)
                goto done;
            if (!vmdk_copy_range(format->device,
                                 member->data_offset + (int64_t)offset, output,
                                 destination, pd))
                goto done;
        }
        produced += output;
    }
    if (produced != member->unpacked_size) goto done;
    result = true;
done:
    if (directory) xx_mem_free(directory);
    if (table) xx_mem_free(table);
    return result;
}

static bool vmdk_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *vmdk_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool vmdk_set_record(xx_archive_record *record,
                           const vmdk_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

void xx_vmdk_init(xx_vmdk *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VMDK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vmdk");
    xx_format_set_extension(&archive->format, "vmdk");
    archive->format.check_is_valid = xx_vmdk_check_is_valid;
    archive->format.handle_base_info = xx_vmdk_handle_base_info;
    archive->format.get_format_size = xx_vmdk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vmdk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vmdk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vmdk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vmdk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vmdk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vmdk_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_vmdk *xx_vmdk_create(xx_io_device *device, int64_t base_address) {
    xx_vmdk *archive = (xx_vmdk *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vmdk_init(archive, device, base_address);
    return archive;
}

void xx_vmdk_destroy(xx_vmdk *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_vmdk_free(xx_vmdk *archive) {
    if (!archive) return;
    xx_vmdk_destroy(archive);
    xx_mem_free(archive);
}

bool xx_vmdk_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    vmdk_stream *stream;
    (void)pd;
    if (!vmdk_parse(format, &stream)) return false;
    vmdk_stream_free(stream);
    return true;
}

bool xx_vmdk_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    vmdk_stream *stream;
    xx_vmdk *archive;
    (void)pd;
    if (!format || !vmdk_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_vmdk *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_VMDK_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    vmdk_stream_free(stream);
    return true;
}

int64_t xx_vmdk_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmdk_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_vmdk_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmdk_handle_base_info(format, pd))
               ? ((xx_vmdk *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_vmdk_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    vmdk_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!vmdk_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        vmdk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = vmdk_stream_free;
    state->total_records = stream->count;
    if (!vmdk_copy_options(&state->options, options) ||
        !vmdk_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_vmdk_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vmdk_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    vmdk_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (vmdk_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        vmdk_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_vmdk_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    vmdk_stream *stream;
    vmdk_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (vmdk_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!vmdk_safe_output_name(member->name)) return false;
    path_option = vmdk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return vmdk_write_member(format, stream, member, NULL, pd);
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    if (!destination) goto done;
    result = vmdk_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vmdk_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
