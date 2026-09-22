/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for Disk eXPress (".DXP") floppy images.  Ported from
 * XArchive's diskimages/xdskexp.cpp - the header layout, the track framing,
 * the geometry table and the CRC seeds are all that module's.
 *
 * 512-byte header:
 *
 *   +0    2    "AS"
 *   +2    u8   major version (1 or 2)
 *   +3    u8   minor version (1 or 4 for v1, 0 or 30 for v2)
 *   +4    u8   release stamp: 0x20, 'A' or 'a'
 *   +5    u8   disk type, 3..7 -> the geometry table below
 *   +6    u32  CRC-32 of the decoded image
 *   +10   u8   compression method: 0 = stored whole, else the major version
 *   +11   u8   last cylinder
 *   +12   u8   last head
 *   +13   u8   unused
 *   +14   u8   flags (only bit 0 is defined)
 *   +15   289  reserved
 *   +304  u32  header CRC
 *   +308  200  description, four 50-byte lines
 *   +508  u32  description CRC
 *
 * Track count is lastCylinder * 2 + min(lastHead, 1) + 1 and the track size is
 * sectors-per-track * 512.  With method 0 the payload is the image itself and
 * must run exactly to the end of file.  Otherwise each track is a u16 length
 * then that many bytes: a length of 1 means the whole track is that one byte
 * repeated, a length equal to the track size means the track is stored, and
 * anything else is an LHA stream - "-lh1-" for major version 1, "-lh5-" for
 * major version 2.  The track chain must also end exactly at end of file.
 *
 * The image is verified against the header CRC-32: the EDB88320 table with a
 * seed of 0x59D and no final inversion, or 0x31E, which is the other seed the
 * writer used.  Version 1 checksums the compressed tracks instead, so its
 * compressed output is not checked here - the same exception the reference
 * makes.
 *
 * Self-extracting Disk eXPress packages - the same header behind an MZ stub -
 * are deliberately NOT accepted: SFX containers are out of scope.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/diskexpress/xx_diskexpress.h"

#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef DISKEXPRESS
#define XX_DISKEXPRESS_FILE_TYPE XX_FILE_TYPE_DISKEXPRESS
#else
#define XX_DISKEXPRESS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DXP_HEADER_SIZE 512
#define DXP_SECTOR_SIZE 512
#define DXP_MAX_TRACKS 200
#define DXP_DESCRIPTION_OFFSET 308
#define DXP_DESCRIPTION_SIZE 200
#define DXP_CRC_SEED_PRIMARY UINT32_C(0x0000059d)
#define DXP_CRC_SEED_ALTERNATE UINT32_C(0x0000031e)

typedef struct dxp_track_s {
    int64_t offset;
    int32_t size;
} dxp_track;

typedef struct dxp_info_s {
    int64_t data_offset;
    int64_t image_size;
    int64_t packed_size;
    int64_t archive_end;
    int32_t track_size;
    int32_t track_count;
    uint32_t data_crc;
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t disk_type;
    uint8_t method;
    uint8_t flags;
    dxp_track *tracks; /* NULL when the method is 0 */
} dxp_info;

typedef struct dxp_stream_s {
    dxp_info info;
    char *name;
    bool consumed;
} dxp_stream;

static uint16_t dxp_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t dxp_le32(const uint8_t *bytes) {
    return (uint32_t)dxp_le16(bytes) | ((uint32_t)dxp_le16(bytes + 2U) << 16U);
}

static bool dxp_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* The writer's own CRC: the EDB88320 table, a non-standard seed and no final
 * inversion. */
static uint32_t dxp_crc32(uint32_t seed, const uint8_t *data, size_t size) {
    uint32_t crc = seed;
    size_t index;
    for (index = 0U; index < size; ++index) {
        unsigned bit;
        crc ^= (uint32_t)data[index];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 1U) ? ((crc >> 1U) ^ UINT32_C(0xEDB88320))
                             : (crc >> 1U);
    }
    return crc;
}

static void dxp_stream_free(void *opaque) {
    dxp_stream *stream = (dxp_stream *)opaque;
    if (!stream) return;
    if (stream->info.tracks) xx_mem_free(stream->info.tracks);
    if (stream->name) xx_str_free(stream->name);
    xx_mem_free(stream);
}

static bool dxp_geometry(uint8_t disk_type, int32_t *sectors_per_track) {
    switch (disk_type) {
        case 3: *sectors_per_track = 9; return true;
        case 4: *sectors_per_track = 9; return true;
        case 5: *sectors_per_track = 15; return true;
        case 6: *sectors_per_track = 18; return true;
        case 7: *sectors_per_track = 36; return true;
        default: return false;
    }
}

static bool dxp_parse(Abstractformat *format, dxp_stream **result) {
    uint8_t header[DXP_HEADER_SIZE];
    dxp_stream *stream = NULL;
    int64_t total, size;
    int32_t sectors_per_track = 0;
    uint8_t release, last_cylinder, last_head;
    bool version_ok;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < DXP_HEADER_SIZE + 2) return false;
    if (!dxp_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;
    if (header[0] != 'A' || header[1] != 'S') return false;

    release = header[4];
    last_cylinder = header[11];
    last_head = header[12];
    version_ok = (header[2] == 1U && (header[3] == 1U || header[3] == 4U)) ||
                 (header[2] == 2U && (header[3] == 0U || header[3] == 30U));
    if (!version_ok) return false;
    if (release != 0x20U && release != 'A' && release != 'a') return false;
    if (header[10] != 0U && header[10] != header[2]) return false;
    if ((header[14] & 0xfeU) != 0U) return false;
    if (!dxp_geometry(header[5], &sectors_per_track)) return false;

    stream = (dxp_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->info.major_version = header[2];
    stream->info.minor_version = header[3];
    stream->info.disk_type = header[5];
    stream->info.data_crc = dxp_le32(header + 6U);
    stream->info.method = header[10];
    stream->info.flags = header[14];
    stream->info.track_size = sectors_per_track * DXP_SECTOR_SIZE;
    stream->info.track_count =
        (int32_t)last_cylinder * 2 + (last_head > 1 ? 1 : (int32_t)last_head) +
        1;
    if (stream->info.track_count < 1 ||
        stream->info.track_count > DXP_MAX_TRACKS ||
        stream->info.track_size <= 0)
        goto fail;
    stream->info.image_size =
        (int64_t)stream->info.track_count * stream->info.track_size;
    stream->info.data_offset = format->base_address + DXP_HEADER_SIZE;

    if (stream->info.method == 0U) {
        /* The stored image must run exactly to the end of file. */
        if (size - DXP_HEADER_SIZE != stream->info.image_size) goto fail;
        stream->info.packed_size = stream->info.image_size;
    } else {
        int64_t cursor = DXP_HEADER_SIZE;
        int64_t packed = 0;
        int32_t index;
        stream->info.tracks = (dxp_track *)xx_mem_calloc(
            (size_t)stream->info.track_count, sizeof(dxp_track));
        if (!stream->info.tracks) goto fail;
        for (index = 0; index < stream->info.track_count; ++index) {
            uint8_t length[2];
            int32_t chunk;
            if (cursor > size - 2) goto fail;
            if (!dxp_read_at(format->device, format->base_address + cursor,
                             length, sizeof(length)))
                goto fail;
            chunk = (int32_t)dxp_le16(length);
            cursor += 2;
            /* Bound the chunk against what is actually left in the file. */
            if (chunk <= 0 || (int64_t)chunk > size - cursor) goto fail;
            stream->info.tracks[index].offset = format->base_address + cursor;
            stream->info.tracks[index].size = chunk;
            packed += chunk;
            cursor += chunk;
        }
        if (cursor != size) goto fail;
        stream->info.packed_size = packed;
    }
    stream->info.archive_end = size;

    stream->name = xx_str_dup("disk.img");
    if (!stream->name) goto fail;
    *result = stream;
    return true;
fail:
    dxp_stream_free(stream);
    return false;
}

static bool dxp_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *dxp_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool dxp_set_record(xx_archive_record *record,
                           const dxp_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = stream->info.data_offset - DXP_HEADER_SIZE;
    record->header_size = DXP_HEADER_SIZE;
    record->data_offset = stream->info.data_offset;
    record->compressed_size = stream->info.packed_size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->info.packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)stream->info.image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          stream->info.method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          stream->info.data_crc) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          stream->info.flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool dxp_decode_image(Abstractformat *format, const dxp_info *info,
                             uint8_t **plain, size_t *plain_size) {
    uint8_t *output = NULL;
    uint8_t *packed = NULL;
    size_t image_size;
    if (!format || !info || !plain || !plain_size) return false;
    if (info->image_size <= 0 || (uint64_t)info->image_size > SIZE_MAX)
        return false;
    image_size = (size_t)info->image_size;
    output = (uint8_t *)xx_mem_alloc(image_size);
    if (!output) return false;

    if (info->method == 0U) {
        if (!dxp_read_at(format->device, info->data_offset, output,
                         image_size))
            goto fail;
    } else {
        int32_t index;
        if (!info->tracks) goto fail;
        packed = (uint8_t *)xx_mem_alloc(0x10000U);
        if (!packed) goto fail;
        for (index = 0; index < info->track_count; ++index) {
            const dxp_track *track = &info->tracks[index];
            uint8_t *target = output + (size_t)index * info->track_size;
            size_t written = 0U;
            if ((size_t)track->size > 0x10000U) goto fail;
            if (!dxp_read_at(format->device, track->offset, packed,
                             (size_t)track->size))
                goto fail;
            if (track->size == 1) {
                xx_rt_memset(target, packed[0], (size_t)info->track_size);
            } else if (track->size == info->track_size) {
                xx_rt_memcpy(target, packed, (size_t)info->track_size);
            } else {
                bool decoded =
                    (info->major_version == 1U)
                        ? xx_lzh1_decode_memory(packed, (size_t)track->size,
                                                target,
                                                (size_t)info->track_size,
                                                &written)
                        : xx_lzh5_decode_memory(packed, (size_t)track->size,
                                                target,
                                                (size_t)info->track_size, 5,
                                                &written);
                if (!decoded || written != (size_t)info->track_size) goto fail;
            }
        }
        xx_mem_free(packed);
        packed = NULL;
    }

    /* Version 1 checksums its compressed tracks rather than the image, so its
     * compressed members are not verified here. */
    if ((info->major_version >= 2U) || (info->method == 0U)) {
        uint32_t crc = dxp_crc32(DXP_CRC_SEED_PRIMARY, output, image_size);
        if (crc != info->data_crc) {
            crc = dxp_crc32(DXP_CRC_SEED_ALTERNATE, output, image_size);
            if (crc != info->data_crc) goto fail;
        }
    }

    *plain = output;
    *plain_size = image_size;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    xx_mem_free(output);
    return false;
}

void xx_diskexpress_init(xx_diskexpress *archive, xx_io_device *device,
                         int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DISKEXPRESS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-diskexpress");
    xx_format_set_extension(&archive->format, "dxp");
    archive->format.check_is_valid = xx_diskexpress_check_is_valid;
    archive->format.handle_base_info = xx_diskexpress_handle_base_info;
    archive->format.get_format_size = xx_diskexpress_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_diskexpress_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_diskexpress_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_diskexpress_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_diskexpress_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_diskexpress_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_diskexpress_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_diskexpress *xx_diskexpress_create(xx_io_device *device,
                                      int64_t base_address) {
    xx_diskexpress *archive =
        (xx_diskexpress *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_diskexpress_init(archive, device, base_address);
    return archive;
}

void xx_diskexpress_destroy(xx_diskexpress *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_diskexpress_free(xx_diskexpress *archive) {
    if (!archive) return;
    xx_diskexpress_destroy(archive);
    xx_mem_free(archive);
}

bool xx_diskexpress_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    dxp_stream *stream;
    (void)pd;
    if (!dxp_parse(format, &stream)) return false;
    dxp_stream_free(stream);
    return true;
}

bool xx_diskexpress_handle_base_info(Abstractformat *format,
                                     xx_pd_struct *pd) {
    dxp_stream *stream;
    xx_diskexpress *archive;
    (void)pd;
    if (!format || !dxp_parse(format, &stream)) return false;
    archive = (xx_diskexpress *)format;
    archive->archive_end = format->base_address + stream->info.archive_end;
    archive->image_size = stream->info.image_size;
    archive->data_crc = stream->info.data_crc;
    archive->major_version = stream->info.major_version;
    archive->minor_version = stream->info.minor_version;
    archive->disk_type = stream->info.disk_type;
    archive->compression_method = stream->info.method;
    format->number_of_archive_records = 1U;
    format->format_size = stream->info.archive_end;
    format->is_valid = true;
    format->base_info_handled = true;
    dxp_stream_free(stream);
    return true;
}

int64_t xx_diskexpress_get_format_size(Abstractformat *format,
                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_diskexpress_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_diskexpress_get_number_of_archive_records(Abstractformat *format,
                                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_diskexpress_handle_base_info(format, pd))
               ? 1U : 0U;
}

xx_archive_record_state *xx_diskexpress_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    dxp_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!dxp_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        dxp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = dxp_stream_free;
    state->total_records = 1;
    if (!dxp_copy_options(&state->options, options) ||
        !dxp_set_record(&state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_diskexpress_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_diskexpress_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    (void)pd;
    (void)format;
    if (state) state->has_record = false;
    return false;
}

bool xx_diskexpress_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    dxp_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (dxp_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!dxp_decode_image(format, &stream->info, &plain, &plain_size))
        goto done;
    path_option = dxp_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
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
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_diskexpress_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
