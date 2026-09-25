/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * VirtualBox Disk Image (VDI).  Everything is little endian.
 *
 * Pre-header (72 bytes at offset 0)
 *   +0x00  char[64] text banner, e.g. "<<< Oracle VM VirtualBox Disk Image >>>\n"
 *                   (older writers: "Sun xVM", "Sun", "innotek"; QEMU and
 *                   CloneVDI write their own names; it is never validated)
 *   +0x40  u32  signature 0xBEDA107F (bytes 7F 10 DA BE)
 *   +0x44  u32  version, major << 16 | minor; 0x00010001 for 1.1
 *
 * Version 1 header (at 0x48)
 *   +0x48  u32  header size: 0x180 (1.0 layout) or 0x190 (with LCHS geometry)
 *   +0x4C  u32  image type: 1 dynamic, 2 fixed, 3 undo, 4 differencing
 *   +0x50  u32  flags
 *   +0x54  char[256] comment
 *   +0x154 u32  block map offset
 *   +0x158 u32  data offset
 *   +0x15C u32  cylinders, heads, sectors, sector size (must be 512)
 *   +0x16C u32  unused
 *   +0x170 u64  guest disk size
 *   +0x178 u32  block size (VirtualBox and QEMU always write 1 MiB)
 *   +0x17C u32  per-block extra data size (0 in practice)
 *   +0x180 u32  block map entries
 *   +0x184 u32  allocated blocks
 *   +0x188 16   creation UUID       +0x198 16   modification UUID
 *   +0x1A8 16   parent (link) UUID  +0x1B8 16   parent modification UUID
 *   +0x1C8 16   LCHS geometry (header size 0x190 only)
 *
 * Block map entry i is the slot number of guest block i: its bytes live at
 * data_offset + slot * (block_size + extra) + extra, as VirtualBox reads
 * them (7-Zip and qemu-img ignore the extra field; every known writer sets
 * it to 0).  0xFFFFFFFF marks a free block and 0xFFFFFFFE an explicitly
 * zeroed one; both read back as zeros.
 * A differencing (type 4) or undo (type 3) image holds only the blocks that
 * changed relative to its parent.  Like the qcow reader with a backing file,
 * this reader still publishes the image's own blocks, with zeros where the
 * parent would supply data, and says so in the record comment.
 *
 * The header checks follow XArchive diskimages/xvirtualdiskarchive.cpp
 * (parseVDI, MIT licence, same author), widened to accept version 1.0
 * headers, undo/differencing images, extra map entries, two map entries
 * sharing one slot and per-block extra data the way VirtualBox itself lays
 * them out.  Version 0 headers (pre-2008 innotek images) are not accepted.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vdi/xx_vdi.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef VDI
#define XX_VDI_FILE_TYPE XX_FILE_TYPE_VDI
#else
#define XX_VDI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define VDI_PREHEADER_SIZE 72U
#define VDI_HEADER1_SIZE 384U
#define VDI_HEADER_READ (VDI_PREHEADER_SIZE + VDI_HEADER1_SIZE)
#define VDI_SIGNATURE UINT32_C(0xBEDA107F)
#define VDI_SECTOR_SIZE 512U
#define VDI_BLOCK_FREE UINT32_C(0xFFFFFFFF)
#define VDI_BLOCK_ZERO UINT32_C(0xFFFFFFFE)
#define VDI_MIN_BLOCK 512U
#define VDI_MAX_BLOCK (32U * 1024U * 1024U)
#define VDI_MAX_EXTRA (32U * 1024U * 1024U)
/* 16 M map entries is a 64 MiB map, read in small chunks and never held in
 * memory at once; with 1 MiB blocks it covers 16 TiB. */
#define VDI_MAX_BLOCKS (UINT32_C(1) << 24)
/* Guest disks larger than 16 TiB are refused, as in the qcow reader. */
#define VDI_MAX_DISK ((uint64_t)1 << 44)
#define VDI_MAP_CHUNK 4096U
#define VDI_IO_BUFFER (256U * 1024U)
#define VDI_MEMBER_NAME "disk.img"

typedef struct vdi_info_s {
    int64_t base;
    int64_t format_size;     /* off_data + allocated * stride */
    uint64_t disk_size;
    uint64_t off_blocks;
    uint64_t off_data;
    uint64_t stride;         /* block_size + block_extra */
    uint64_t needed;         /* blocks that cover disk_size */
    uint32_t version;
    uint32_t header_size;
    uint32_t type;
    uint32_t block_size;
    uint32_t block_extra;
    uint32_t blocks;
    uint32_t allocated;
    uint8_t parent_uuid[16];
} vdi_info;

typedef struct vdi_stream_s {
    vdi_info info;
    size_t index;
} vdi_stream;

static uint32_t vdi_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static uint64_t vdi_le64(const uint8_t *b) {
    return (uint64_t)vdi_le32(b) | ((uint64_t)vdi_le32(b + 4U) << 32U);
}

static bool vdi_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool vdi_write_all(xx_io_device *device, const uint8_t *data,
                          size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device) return true; /* verify-only pass */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool vdi_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

/* Header-only validation: one 456-byte read, no allocation.  This is all the
 * detector probe and handle_base_info ever do. */
static bool vdi_parse(Abstractformat *format, vdi_info *info) {
    uint8_t h[VDI_HEADER_READ];
    int64_t total;
    uint64_t size, header_end, map_end, data_bytes;
    uint32_t sector_size;

    if (!format || !format->device || !info || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < 0 || total < format->base_address) return false;
    size = (uint64_t)(total - format->base_address);
    if (size < VDI_HEADER_READ) return false;
    if (!vdi_read_at(format->device, format->base_address, h, sizeof(h)))
        return false;
    if (vdi_le32(h + 0x40U) != VDI_SIGNATURE) return false;

    xx_rt_memset(info, 0, sizeof(*info));
    info->base = format->base_address;
    info->version = vdi_le32(h + 0x44U);
    /* Only the version 1 header layout is defined here (1.0 and 1.1). */
    if ((info->version >> 16U) != 1U) return false;
    info->header_size = vdi_le32(h + 0x48U);
    info->type = vdi_le32(h + 0x4CU);
    info->off_blocks = vdi_le32(h + 0x154U);
    info->off_data = vdi_le32(h + 0x158U);
    sector_size = vdi_le32(h + 0x168U);
    info->disk_size = vdi_le64(h + 0x170U);
    info->block_size = vdi_le32(h + 0x178U);
    info->block_extra = vdi_le32(h + 0x17CU);
    info->blocks = vdi_le32(h + 0x180U);
    info->allocated = vdi_le32(h + 0x184U);
    xx_rt_memcpy(info->parent_uuid, h + 0x1A8U, sizeof(info->parent_uuid));

    if (info->header_size < VDI_HEADER1_SIZE || info->type < 1U ||
        info->type > 4U || sector_size != VDI_SECTOR_SIZE)
        return false;
    header_end = (uint64_t)VDI_PREHEADER_SIZE + info->header_size;
    if (info->off_blocks < header_end) return false;
    if (info->disk_size == 0U || info->disk_size > VDI_MAX_DISK ||
        (info->disk_size % VDI_SECTOR_SIZE) != 0U)
        return false;
    if (!vdi_power_of_two(info->block_size) ||
        info->block_size < VDI_MIN_BLOCK || info->block_size > VDI_MAX_BLOCK ||
        info->block_extra > VDI_MAX_EXTRA)
        return false;
    if (info->blocks == 0U || info->blocks > VDI_MAX_BLOCKS ||
        info->allocated > info->blocks)
        return false;
    info->needed = (info->disk_size - 1U) / info->block_size + 1U;
    if (info->needed > info->blocks) return false;
    /* The map must sit between the header and the data area. */
    map_end = info->off_blocks + (uint64_t)info->blocks * 4U;
    if (info->off_data < map_end) return false;
    /* Every allocated slot must be inside the file: stride <= 64 MiB and
     * allocated <= 16 M, so the product cannot overflow. */
    info->stride = (uint64_t)info->block_size + info->block_extra;
    data_bytes = (uint64_t)info->allocated * info->stride;
    if (info->off_data > size || data_bytes > size - info->off_data)
        return false;
    info->format_size = (int64_t)(info->off_data + data_bytes);
    return true;
}

/* Walk the block map for the blocks that cover the guest disk, checking
 * every entry, and (with a destination) write the guest disk.  With no
 * destination nothing is read beyond the map: it is the map validation used
 * before a record is published and by verify-only unpacks. */
static bool vdi_emit(Abstractformat *format, const vdi_info *info,
                     xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *map = NULL;
    uint8_t *data = NULL;
    uint8_t *zeros = NULL;
    uint64_t index, produced = 0U;
    uint64_t chunk_first = 0U, chunk_count = 0U;
    bool result = false;

    if (!format || !format->device || !info) return false;
    map = (uint8_t *)xx_mem_alloc(VDI_MAP_CHUNK * 4U);
    if (!map) goto done;
    if (destination) {
        data = (uint8_t *)xx_mem_alloc(VDI_IO_BUFFER);
        zeros = (uint8_t *)xx_mem_calloc(1U, VDI_IO_BUFFER);
        if (!data || !zeros) goto done;
    }
    for (index = 0U; index < info->needed; ++index) {
        uint64_t left = info->disk_size - produced;
        uint64_t output = left < info->block_size ? left : info->block_size;
        uint32_t entry;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (index >= chunk_first + chunk_count) {
            uint64_t want = info->needed - index;
            if (want > VDI_MAP_CHUNK) want = VDI_MAP_CHUNK;
            if (!vdi_read_at(format->device,
                             info->base + (int64_t)(info->off_blocks + index * 4U),
                             map, (size_t)want * 4U))
                goto done;
            chunk_first = index;
            chunk_count = want;
        }
        entry = vdi_le32(map + (size_t)(index - chunk_first) * 4U);
        if (entry == VDI_BLOCK_FREE || entry == VDI_BLOCK_ZERO) {
            uint64_t rest = output;
            while (rest != 0U && destination) {
                size_t step = rest < VDI_IO_BUFFER ? (size_t)rest : VDI_IO_BUFFER;
                if (!vdi_write_all(destination, zeros, step, pd)) goto done;
                rest -= step;
            }
        } else {
            uint64_t offset, rest = output;
            /* parse() proved allocated * stride fits in the file. */
            if (entry >= info->allocated) goto done;
            offset = info->off_data + (uint64_t)entry * info->stride +
                     info->block_extra;
            while (rest != 0U && destination) {
                size_t step = rest < VDI_IO_BUFFER ? (size_t)rest : VDI_IO_BUFFER;
                if (!vdi_read_at(format->device, info->base + (int64_t)offset,
                                 data, step) ||
                    !vdi_write_all(destination, data, step, pd))
                    goto done;
                offset += step;
                rest -= step;
            }
        }
        produced += output;
    }
    result = produced == info->disk_size;
done:
    if (map) xx_mem_free(map);
    if (data) xx_mem_free(data);
    if (zeros) xx_mem_free(zeros);
    return result;
}

static void vdi_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool vdi_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *vdi_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* XX_META_ID_OPT_MAX_MEMBER_SIZE, when given, caps the guest disk size an
 * unpack will write.  Absent means unlimited (parse() already caps at
 * VDI_MAX_DISK). */
static bool vdi_size_allowed(Abstractformat *format, const xx_list_s *options,
                             uint64_t size) {
    const xx_var *limit = xx_format_resolve_extra_parameter(
        format, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!limit) return true;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return size <= xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value < 0 || size <= (uint64_t)value;
        }
        default: return true;
    }
}

/* "differencing image; parent {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}".  The
 * UUID is stored in the little-endian GUID layout. */
static void vdi_parent_comment(const vdi_info *info, char *out, size_t size) {
    static const char hex[] = "0123456789abcdef";
    static const uint8_t order[16] = {3, 2, 1, 0, 5, 4, 7, 6,
                                      8, 9, 10, 11, 12, 13, 14, 15};
    static const char prefix[] = "differencing image; parent {";
    size_t used = 0U, at;
    bool any = false;
    if (!out || size < 80U) return;
    for (at = 0U; at < 16U; ++at)
        if (info->parent_uuid[at] != 0U) any = true;
    if (!any) {
        static const char none[] = "differencing image; parent not recorded";
        xx_rt_memcpy(out, none, sizeof(none));
        return;
    }
    xx_rt_memcpy(out, prefix, sizeof(prefix) - 1U);
    used = sizeof(prefix) - 1U;
    for (at = 0U; at < 16U; ++at) {
        uint8_t value = info->parent_uuid[order[at]];
        if (at == 4U || at == 6U || at == 8U || at == 10U) out[used++] = '-';
        out[used++] = hex[value >> 4U];
        out[used++] = hex[value & 15U];
    }
    out[used++] = '}';
    out[used] = 0;
}

static bool vdi_set_record(xx_archive_record *record, const vdi_info *info) {
    uint64_t data_bytes = (uint64_t)info->format_size - info->off_data;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = info->base;
    record->header_size = (int64_t)VDI_PREHEADER_SIZE + info->header_size;
    record->data_offset = info->base + (int64_t)info->off_data;
    record->compressed_size = (int64_t)data_bytes;
    if (!xx_archive_record_set_original_name(record, VDI_MEMBER_NAME) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        data_bytes) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        info->disk_size) ||
        /* The method slot carries the VDI image type (1..4). */
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        info->type) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (info->type == 3U || info->type == 4U) {
        char comment[96];
        vdi_parent_comment(info, comment, sizeof(comment));
        if (!xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                            comment))
            return false;
    }
    return true;
}

void xx_vdi_init(xx_vdi *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VDI_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-virtualbox-vdi");
    xx_format_set_extension(&archive->format, "vdi");
    archive->format.check_is_valid = xx_vdi_check_is_valid;
    archive->format.handle_base_info = xx_vdi_handle_base_info;
    archive->format.get_format_size = xx_vdi_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vdi_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vdi_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vdi_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vdi_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vdi_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vdi_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_vdi *xx_vdi_create(xx_io_device *device, int64_t base_address) {
    xx_vdi *archive = (xx_vdi *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vdi_init(archive, device, base_address);
    return archive;
}

void xx_vdi_destroy(xx_vdi *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_vdi_free(xx_vdi *archive) {
    if (!archive) return;
    xx_vdi_destroy(archive);
    xx_mem_free(archive);
}

bool xx_vdi_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    vdi_info info;
    (void)pd;
    return vdi_parse(format, &info);
}

bool xx_vdi_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    vdi_info info;
    xx_vdi *archive;
    (void)pd;
    if (!format || !vdi_parse(format, &info)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_vdi *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + info.format_size;
    archive->disk_size = info.disk_size;
    archive->image_type = info.type;
    archive->block_size = info.block_size;
    archive->blocks = info.blocks;
    archive->blocks_allocated = info.allocated;
    archive->version = info.version;
    format->number_of_archive_records = 1U;
    format->format_size = info.format_size;
    format->file_type = XX_VDI_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_vdi_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vdi_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_vdi_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vdi_handle_base_info(format, pd))
               ? ((xx_vdi *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_vdi_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    vdi_stream *stream;
    xx_archive_record_state *state;
    stream = (vdi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    /* A block map entry that points past the allocated slots is refused
     * here, before anything is published or written. */
    if (!vdi_parse(format, &stream->info) ||
        !vdi_emit(format, &stream->info, NULL, pd)) {
        vdi_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        vdi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = vdi_stream_free;
    state->total_records = 1U;
    if (!vdi_copy_options(&state->options, options) ||
        !vdi_set_record(&state->current_record, &stream->info)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_vdi_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vdi_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    vdi_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (vdi_stream *)state->internal_state))
        return false;
    /* There is exactly one member, so the first step is always the last. */
    stream->index = 1U;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_vdi_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    vdi_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (vdi_stream *)state->internal_state) ||
        stream->index != 0U || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!vdi_size_allowed(format, &state->options, stream->info.disk_size))
        return false;
    path_option = vdi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return vdi_emit(format, &stream->info, NULL, pd);
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
               ? xx_str_concat3(base, "/", VDI_MEMBER_NAME)
               : xx_str_concat(base, VDI_MEMBER_NAME);
    if (!path) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    result = vdi_emit(format, &stream->info, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vdi_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
