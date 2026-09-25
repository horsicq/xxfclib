/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Raw CD sector image.  xx_rawcd.h carries the sector layouts and the
 * member rules.
 *
 * Written from the ECMA-130 / CD-ROM XA sector layouts.  Deark's cd_raw
 * module (MIT, modules/iso9660.c) was the design reference for the layout
 * table and the volume-descriptor test (a type byte in 0..3 or 255 before
 * "CD001" / "CD-I ", or "CDROM" eight bytes further on for High Sierra);
 * libmirage's image-iso parser (GPL) was read only to confirm which
 * sector-plus-subchannel sizes producers write.  No code was taken from
 * either.
 *
 * Nothing here trusts a count from the file: the only numbers are the
 * device size and the sector size, and every loop runs over whole sectors
 * that exist.  Finding where the data track ends reads one sector header
 * per step of a binary search, never the whole image.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rawcd/xx_rawcd.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as RAWCD is registered there. */
#ifdef RAWCD
#define XX_RAWCD_FILE_TYPE XX_FILE_TYPE_RAWCD
#else
#define XX_RAWCD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RAWCD_SYNC_SIZE 12U
#define RAWCD_HEADER_SIZE 16U
#define RAWCD_MODE2_SIZE 2336U
#define RAWCD_RAW_SIZE 2352U
#define RAWCD_PQ_SIZE 2368U
#define RAWCD_PW_SIZE 2448U
/* The largest stored sector, for buffers. */
#define RAWCD_MAX_SECTOR RAWCD_PW_SIZE
/* 00:02:00, the address of the first sector of a disc's first track. */
#define RAWCD_DISC_START_FRAMES 150U
/* Volume descriptors start at sector 16 of a track. */
#define RAWCD_VD_SECTOR 16
/* A sync-less image needs sectors 16 and 17 complete. */
#define RAWCD_SYNCLESS_MIN_SECTORS 18
#define RAWCD_MIN_SECTORS 2
/* Sectors read and written per step when streaming a member. */
#define RAWCD_CHUNK_SECTORS 32U
/* After the binary search finds a data-to-other edge, this many sectors
 * past it are checked for sync: a blank hole inside the data track is not
 * its end. */
#define RAWCD_HOLE_PROBE 16
/* Each round either ends the search or moves its low end forward. */
#define RAWCD_MAX_ROUNDS 64
#define RAWCD_NRG_FOOTER 12U

static const uint8_t rawcd_sync[RAWCD_SYNC_SIZE] = {
    0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
    0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U};

/* Layouts tried when sector 0 carries no sync: a descriptor pair at sectors
 * 16 and 17 decides.  Sector sizes with sync also need sync at sector 16. */
typedef struct rawcd_layout_s {
    uint32_t sector_size;
    uint32_t data_offset;
    bool has_sync;
} rawcd_layout;

static const rawcd_layout rawcd_syncless_layouts[] = {
    {RAWCD_MODE2_SIZE, 8U, false},
    {RAWCD_RAW_SIZE, 16U, true},
    {RAWCD_RAW_SIZE, 24U, true},
    {RAWCD_PW_SIZE, 16U, true},
    {RAWCD_PW_SIZE, 24U, true}
};

/* Strides tried after a synced sector 0, most common first. */
static const uint32_t rawcd_strides[] = {RAWCD_RAW_SIZE, RAWCD_PW_SIZE,
                                         RAWCD_PQ_SIZE};

typedef struct rawcd_geometry_s {
    uint32_t sector_size;
    uint32_t data_offset;
    bool has_sync;
    int64_t first_sync; /* a sector known to carry sync, -1 for 2336 */
    int64_t size;       /* device bytes from the image start */
    int64_t data_sectors;
    int64_t audio_sectors;
} rawcd_geometry;

typedef struct rawcd_member_s {
    const char *name;
    const char *comment;
    int64_t offset;   /* device offset of the member's first sector */
    int64_t sectors;
    int64_t packed_size;
    uint64_t unpacked_size;
    bool audio;
} rawcd_member;

typedef struct rawcd_stream_s {
    rawcd_member items[2];
    size_t count;
    size_t index;
} rawcd_stream;

static bool rawcd_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool rawcd_write_all(xx_io_device *device, const void *data,
                            size_t size) {
    size_t done = 0U;
    if (!device) return true; /* verify-only pass */
    while (done < size) {
        ssize_t amount = xx_io_write(device, (const uint8_t *)data + done,
                                     size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool rawcd_is_sync(const uint8_t *bytes) {
    return xx_rt_memcmp(bytes, rawcd_sync, RAWCD_SYNC_SIZE) == 0;
}

/* One BCD byte below `limit` (itself given in BCD, e.g. 0x60). */
static bool rawcd_bcd_ok(uint8_t value, uint8_t limit) {
    return (value & 0x0FU) <= 9U && (value >> 4U) <= 9U && value < limit;
}

/* Sync plus a BCD address; returns the address in frames, or -1. */
static int32_t rawcd_header_frames(const uint8_t *header) {
    uint32_t minute, second, frame;
    if (!rawcd_is_sync(header) || !rawcd_bcd_ok(header[12], 0xA0U) ||
        !rawcd_bcd_ok(header[13], 0x60U) || !rawcd_bcd_ok(header[14], 0x75U))
        return -1;
    minute = (uint32_t)(header[12] >> 4U) * 10U + (header[12] & 0x0FU);
    second = (uint32_t)(header[13] >> 4U) * 10U + (header[13] & 0x0FU);
    frame = (uint32_t)(header[14] >> 4U) * 10U + (header[14] & 0x0FU);
    return (int32_t)(minute * 4500U + second * 75U + frame);
}

/* A volume descriptor at `offset`: ISO 9660 / CD-i ("CD001" / "CD-I "
 * after a type byte of 0..3 or 255, version 1), UDF's "BEA01" (type 0), or
 * High Sierra ("CDROM" after an 8-byte block number and a type byte). */
static bool rawcd_volume_descriptor_at(xx_io_device *device, int64_t offset,
                                       bool iso_only) {
    uint8_t bytes[16];
    if (!rawcd_read_at(device, offset, bytes, sizeof(bytes))) return false;
    if ((xx_rt_memcmp(bytes + 1U, "CD001", 5U) == 0 ||
         xx_rt_memcmp(bytes + 1U, "CD-I ", 5U) == 0) &&
        (bytes[0] <= 3U || bytes[0] == 0xFFU) && bytes[6] == 1U)
        return true;
    if (iso_only) return false;
    if (xx_rt_memcmp(bytes + 1U, "BEA01", 5U) == 0 && bytes[0] == 0U &&
        bytes[6] == 1U)
        return true;
    return xx_rt_memcmp(bytes + 9U, "CDROM", 5U) == 0 &&
           (bytes[8] <= 3U || bytes[8] == 0xFFU) && bytes[14] == 1U;
}

/* The descriptor inside a synced sector, at +16 (mode 1) or +24 (mode 2). */
static bool rawcd_synced_descriptor(xx_io_device *device, int64_t sector,
                                    int32_t mode, bool iso_only) {
    uint8_t header[RAWCD_HEADER_SIZE];
    uint32_t offset;
    if (!rawcd_read_at(device, sector, header, sizeof(header)) ||
        rawcd_header_frames(header) < 0 || header[15] != (uint8_t)mode)
        return false;
    offset = mode == 1 ? 16U : 24U;
    return rawcd_volume_descriptor_at(device, sector + offset, iso_only);
}

static bool rawcd_sector_synced(xx_io_device *device, int64_t offset) {
    uint8_t bytes[RAWCD_SYNC_SIZE];
    return rawcd_read_at(device, offset, bytes, sizeof(bytes)) &&
           rawcd_is_sync(bytes);
}

/* A Nero image keeps raw track data from byte 0 and a footer at the very
 * end ("NER5" + u64, or "NERO" + u32); those sectors belong to that reader. */
static bool rawcd_has_nrg_footer(xx_io_device *device, int64_t end) {
    uint8_t footer[RAWCD_NRG_FOOTER];
    if (end < (int64_t)RAWCD_NRG_FOOTER ||
        !rawcd_read_at(device, end - (int64_t)RAWCD_NRG_FOOTER, footer,
                       sizeof(footer)))
        return false;
    return xx_rt_memcmp(footer, "NER5", 4U) == 0 ||
           xx_rt_memcmp(footer + 4U, "NERO", 4U) == 0;
}

/* Sector 0 synced: sector 1 at the stride must be synced with the next
 * address.  The image must then start a disc (00:02:00) or hold a volume
 * descriptor at its sector 16, which rules out a start in mid-track. */
static bool rawcd_probe_synced(xx_io_device *device, int64_t base,
                               int64_t size, rawcd_geometry *geometry) {
    uint8_t first[RAWCD_HEADER_SIZE];
    int32_t frames;
    size_t index;

    if (!rawcd_read_at(device, base, first, sizeof(first))) return false;
    frames = rawcd_header_frames(first);
    if (frames < 0 || (first[15] != 1U && first[15] != 2U)) return false;

    for (index = 0U; index < sizeof(rawcd_strides) / sizeof(rawcd_strides[0]);
         ++index) {
        uint32_t stride = rawcd_strides[index];
        uint8_t next[RAWCD_HEADER_SIZE];
        bool start_of_disc, descriptor = false;
        if (size < (int64_t)stride * RAWCD_MIN_SECTORS ||
            !rawcd_read_at(device, base + stride, next, sizeof(next)) ||
            rawcd_header_frames(next) != frames + 1 || next[15] > 2U)
            continue;
        start_of_disc = frames == (int32_t)RAWCD_DISC_START_FRAMES;
        if (!start_of_disc &&
            size >= (int64_t)stride * (RAWCD_VD_SECTOR + 1)) {
            int64_t sector = base + (int64_t)stride * RAWCD_VD_SECTOR;
            descriptor = rawcd_synced_descriptor(device, sector, 1, false) ||
                         rawcd_synced_descriptor(device, sector, 2, false);
        }
        if (!start_of_disc && !descriptor) return false;
        geometry->sector_size = stride;
        geometry->data_offset = first[15] == 1U ? 16U : 24U;
        geometry->has_sync = true;
        geometry->first_sync = 0;
        return true;
    }
    return false;
}

/* No sync at sector 0: the device must be whole sectors of one layout and
 * sectors 16 and 17 must both hold ISO 9660 / CD-i descriptors. */
static bool rawcd_probe_syncless(xx_io_device *device, int64_t base,
                                 int64_t size, rawcd_geometry *geometry) {
    size_t index;
    for (index = 0U; index < sizeof(rawcd_syncless_layouts) /
                                 sizeof(rawcd_syncless_layouts[0]);
         ++index) {
        const rawcd_layout *layout = &rawcd_syncless_layouts[index];
        int64_t stride = (int64_t)layout->sector_size;
        int64_t sector16 = base + stride * RAWCD_VD_SECTOR;
        int64_t sector17 = sector16 + stride;
        if (size % stride != 0 || size < stride * RAWCD_SYNCLESS_MIN_SECTORS)
            continue;
        if (layout->has_sync) {
            int32_t mode = layout->data_offset == 16U ? 1 : 2;
            if (!rawcd_synced_descriptor(device, sector16, mode, true) ||
                !rawcd_synced_descriptor(device, sector17, mode, true))
                continue;
        } else if (!rawcd_volume_descriptor_at(
                       device, sector16 + layout->data_offset, true) ||
                   !rawcd_volume_descriptor_at(
                       device, sector17 + layout->data_offset, true)) {
            continue;
        }
        geometry->sector_size = layout->sector_size;
        geometry->data_offset = layout->data_offset;
        geometry->has_sync = layout->has_sync;
        geometry->first_sync = layout->has_sync ? RAWCD_VD_SECTOR : -1;
        return true;
    }
    return false;
}

static bool rawcd_probe(Abstractformat *format, rawcd_geometry *geometry) {
    int64_t total, size;
    if (!format || !format->device || !geometry || format->base_address < 0)
        return false;
    xx_rt_memset(geometry, 0, sizeof(*geometry));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)RAWCD_MODE2_SIZE * RAWCD_MIN_SECTORS) return false;
    geometry->size = size;
    if (!rawcd_probe_synced(format->device, format->base_address, size,
                            geometry) &&
        !rawcd_probe_syncless(format->device, format->base_address, size,
                              geometry))
        return false;
    return !rawcd_has_nrg_footer(format->device, total);
}

/* Where the leading data run ends.  If the last whole sector is synced the
 * whole image is data; otherwise a binary search finds a synced sector
 * followed by one that is not, and the few sectors after that edge are
 * checked so a blank hole inside the track does not end it. */
static int64_t rawcd_data_run(xx_io_device *device, int64_t base,
                              const rawcd_geometry *geometry,
                              int64_t sectors) {
    int64_t stride = (int64_t)geometry->sector_size;
    int64_t low, high;
    int round;
    if (!geometry->has_sync || sectors <= geometry->first_sync + 1) return sectors;
    if (rawcd_sector_synced(device, base + (sectors - 1) * stride))
        return sectors;
    low = geometry->first_sync; /* synced */
    high = sectors - 1;          /* not synced */
    for (round = 0; round < RAWCD_MAX_ROUNDS; ++round) {
        int64_t probe, limit, found = -1;
        while (high - low > 1) {
            int64_t middle = low + (high - low) / 2;
            if (rawcd_sector_synced(device, base + middle * stride))
                low = middle;
            else
                high = middle;
        }
        limit = high + RAWCD_HOLE_PROBE;
        if (limit > sectors - 1) limit = sectors - 1;
        for (probe = high + 1; probe < limit; ++probe) {
            if (rawcd_sector_synced(device, base + probe * stride)) {
                found = probe;
                break;
            }
        }
        if (found < 0) break;
        low = found;
        high = sectors - 1;
    }
    return low + 1;
}

static bool rawcd_parse(Abstractformat *format, rawcd_geometry *geometry) {
    int64_t sectors, rest;
    if (!rawcd_probe(format, geometry)) return false;
    sectors = geometry->size / (int64_t)geometry->sector_size;
    if (sectors < RAWCD_MIN_SECTORS) return false;
    geometry->data_sectors = rawcd_data_run(
        format->device, format->base_address, geometry, sectors);
    if (geometry->data_sectors < 1 || geometry->data_sectors > sectors)
        return false;
    /* Whole sectors from the run to the end of the device are the audio
     * tracks of a mixed-mode image; a ragged tail is not part of it. */
    rest = geometry->size -
           geometry->data_sectors * (int64_t)geometry->sector_size;
    geometry->audio_sectors =
        rest > 0 && rest % (int64_t)geometry->sector_size == 0
            ? rest / (int64_t)geometry->sector_size
            : 0;
    return true;
}

static const char *rawcd_data_comment(const rawcd_geometry *geometry) {
    if (!geometry->has_sync) return "MODE2/2336";
    switch (geometry->sector_size) {
    case RAWCD_PQ_SIZE:
        return geometry->data_offset == 16U ? "MODE1/2368" : "MODE2/2368";
    case RAWCD_PW_SIZE:
        return geometry->data_offset == 16U ? "MODE1/2448" : "MODE2/2448";
    default:
        return geometry->data_offset == 16U ? "MODE1/2352" : "MODE2/2352";
    }
}

static const char *rawcd_audio_comment(const rawcd_geometry *geometry) {
    switch (geometry->sector_size) {
    case RAWCD_PQ_SIZE: return "AUDIO/2368";
    case RAWCD_PW_SIZE: return "AUDIO/2448";
    default: return "AUDIO/2352";
    }
}

static void rawcd_store_geometry(xx_rawcd *archive,
                                 const rawcd_geometry *geometry) {
    archive->sector_size = geometry->sector_size;
    archive->data_offset = geometry->data_offset;
    archive->has_sync = geometry->has_sync;
    archive->data_sectors = geometry->data_sectors;
    archive->audio_sectors = geometry->audio_sectors;
}

static void rawcd_load_geometry(const xx_rawcd *archive,
                                rawcd_geometry *geometry) {
    xx_rt_memset(geometry, 0, sizeof(*geometry));
    geometry->sector_size = archive->sector_size;
    geometry->data_offset = archive->data_offset;
    geometry->has_sync = archive->has_sync;
    geometry->data_sectors = archive->data_sectors;
    geometry->audio_sectors = archive->audio_sectors;
}

static void rawcd_build_members(const Abstractformat *format,
                                const rawcd_geometry *geometry,
                                rawcd_stream *stream) {
    int64_t stride = (int64_t)geometry->sector_size;
    rawcd_member *member = &stream->items[0];
    xx_rt_memset(stream, 0, sizeof(*stream));
    member->name = "image.iso";
    member->comment = rawcd_data_comment(geometry);
    member->offset = format->base_address;
    member->sectors = geometry->data_sectors;
    member->packed_size = geometry->data_sectors * stride;
    member->unpacked_size =
        (uint64_t)geometry->data_sectors * XX_RAWCD_USER_DATA;
    stream->count = 1U;
    if (geometry->audio_sectors > 0) {
        member = &stream->items[1];
        member->name = "audio.cdda";
        member->comment = rawcd_audio_comment(geometry);
        member->offset = format->base_address + geometry->data_sectors * stride;
        member->sectors = geometry->audio_sectors;
        member->packed_size = geometry->audio_sectors * stride;
        member->unpacked_size =
            (uint64_t)geometry->audio_sectors * XX_RAWCD_AUDIO_FRAME;
        member->audio = true;
        stream->count = 2U;
    }
}

/* Streams a member through fixed buffers: user data (or zeros for mode 0)
 * per sector for the data run, the first 2352 bytes per sector for audio. */
static bool rawcd_write_member(Abstractformat *format,
                               const rawcd_geometry *geometry,
                               const rawcd_member *member,
                               xx_io_device *destination, xx_pd_struct *pd) {
    int64_t stride = (int64_t)geometry->sector_size;
    size_t frame = member->audio ? XX_RAWCD_AUDIO_FRAME : XX_RAWCD_USER_DATA;
    uint8_t *input, *output;
    int64_t done = 0;
    bool result = true;

    if (!format || stride <= 0 || stride > (int64_t)RAWCD_MAX_SECTOR ||
        member->sectors < 0 || member->offset < 0 ||
        member->sectors > (xx_io_total_size(format->device) - member->offset) /
                              stride)
        return false;
    if (member->audio && stride < (int64_t)XX_RAWCD_AUDIO_FRAME) return false;
    input = (uint8_t *)xx_mem_alloc(RAWCD_CHUNK_SECTORS * RAWCD_MAX_SECTOR);
    output = (uint8_t *)xx_mem_alloc(RAWCD_CHUNK_SECTORS * XX_RAWCD_AUDIO_FRAME);
    if (!input || !output) {
        result = false;
        goto done;
    }
    while (done < member->sectors) {
        int64_t left = member->sectors - done;
        size_t count = left > (int64_t)RAWCD_CHUNK_SECTORS
                           ? RAWCD_CHUNK_SECTORS
                           : (size_t)left;
        size_t index;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !rawcd_read_at(format->device, member->offset + done * stride,
                           input, count * (size_t)stride)) {
            result = false;
            break;
        }
        for (index = 0U; index < count; ++index) {
            const uint8_t *sector = input + index * (size_t)stride;
            uint8_t *target = output + index * frame;
            uint32_t offset = geometry->data_offset;
            if (member->audio) {
                xx_rt_memcpy(target, sector, XX_RAWCD_AUDIO_FRAME);
                continue;
            }
            if (geometry->has_sync && rawcd_is_sync(sector)) {
                /* Bits 5..7 of the mode byte are block indicators on
                 * recordable media; bits 0..1 are the mode. */
                uint8_t mode = (uint8_t)(sector[15] & 0x03U);
                if (mode == 0U) {
                    xx_rt_memset(target, 0, XX_RAWCD_USER_DATA);
                    continue;
                }
                if (mode == 1U) offset = 16U;
                else if (mode == 2U) offset = 24U;
            }
            xx_rt_memcpy(target, sector + offset, XX_RAWCD_USER_DATA);
        }
        if (!rawcd_write_all(destination, output, count * frame)) {
            result = false;
            break;
        }
        done += (int64_t)count;
    }
done:
    if (input) xx_mem_free(input);
    if (output) xx_mem_free(output);
    return result;
}

static void rawcd_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool rawcd_copy_options(xx_list_s *destination,
                               const xx_list_s *source) {
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

static const xx_var *rawcd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool rawcd_set_record(xx_archive_record *record,
                             const rawcd_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->offset;
    record->header_size = 0;
    record->data_offset = member->offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          member->comment) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_rawcd_init(xx_rawcd *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_RAWCD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cd-image");
    xx_format_set_extension(&archive->format, "bin");
    archive->format.check_is_valid = xx_rawcd_check_is_valid;
    archive->format.handle_base_info = xx_rawcd_handle_base_info;
    archive->format.get_format_size = xx_rawcd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rawcd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rawcd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rawcd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rawcd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rawcd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rawcd_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_rawcd *xx_rawcd_create(xx_io_device *device, int64_t base_address) {
    xx_rawcd *archive = (xx_rawcd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_rawcd_init(archive, device, base_address);
    return archive;
}

void xx_rawcd_destroy(xx_rawcd *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_rawcd_free(xx_rawcd *archive) {
    if (!archive) return;
    xx_rawcd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_rawcd_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    rawcd_geometry geometry;
    (void)pd;
    return rawcd_probe(format, &geometry);
}

bool xx_rawcd_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    rawcd_geometry geometry;
    xx_rawcd *archive;
    int64_t size;
    (void)pd;
    if (!format || !rawcd_parse(format, &geometry)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_rawcd *)format;
    rawcd_store_geometry(archive, &geometry);
    size = (geometry.data_sectors + geometry.audio_sectors) *
           (int64_t)geometry.sector_size;
    archive->number_of_records = geometry.audio_sectors > 0 ? 2U : 1U;
    archive->archive_end = format->base_address + size;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = size;
    format->file_type = XX_RAWCD_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    if (geometry.size > size) {
        format->overlay_offset = format->base_address + size;
        format->overlay_size = geometry.size - size;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_rawcd_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rawcd_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_rawcd_get_number_of_archive_records(Abstractformat *format,
                                                xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rawcd_handle_base_info(format, pd))
               ? ((xx_rawcd *)format)->number_of_records
               : 0U;
}

bool xx_rawcd_cook_to_device(xx_rawcd *archive, xx_io_device *destination,
                             xx_pd_struct *pd) {
    rawcd_geometry geometry;
    rawcd_stream stream;
    if (!archive || (!archive->format.base_info_handled &&
                     !xx_rawcd_handle_base_info(&archive->format, pd)))
        return false;
    rawcd_load_geometry(archive, &geometry);
    rawcd_build_members(&archive->format, &geometry, &stream);
    return rawcd_write_member(&archive->format, &geometry, &stream.items[0],
                              destination, pd);
}

xx_archive_record_state *xx_rawcd_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    rawcd_geometry geometry;
    rawcd_stream *stream;
    xx_archive_record_state *state;
    if (!format || (!format->base_info_handled &&
                    !xx_rawcd_handle_base_info(format, pd)))
        return NULL;
    rawcd_load_geometry((const xx_rawcd *)format, &geometry);
    stream = (rawcd_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    rawcd_build_members(format, &geometry, stream);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        rawcd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = rawcd_stream_free;
    state->total_records = stream->count;
    if (!rawcd_copy_options(&state->options, options) ||
        !rawcd_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_rawcd_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rawcd_archive_record_move_to_next(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    rawcd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (rawcd_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        rawcd_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rawcd_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    rawcd_stream *stream;
    const rawcd_member *member;
    rawcd_geometry geometry;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (rawcd_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    rawcd_load_geometry((const xx_rawcd *)format, &geometry);
    path_option = rawcd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return rawcd_write_member(format, &geometry, member, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* Member names are the two constants above, never taken from the
     * image, so they need no sanitising. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    if (!destination) goto done;
    created = true;
    result = rawcd_write_member(format, &geometry, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_rawcd_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
