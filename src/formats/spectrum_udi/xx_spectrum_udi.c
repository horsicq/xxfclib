/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZX Spectrum UDI ("Ultra Disk Image", Alex Makeev, v1.0 / v1.1).  Written
 * from the published description (Sinclair Wiki "UDI format"); SAMdisk was
 * used as a black-box oracle only and no code was taken from it.
 *
 * Header (16 bytes, little endian):
 *   0  "UDI!"            ("udi!" = v1.0 whole-file compression, never
 *                         specified; refused)
 *   4  u32 file size - 4 (the CRC32 of everything before it follows)
 *   8  u8  version (0)
 *   9  u8  cylinders - 1
 *  10  u8  heads - 1     (0 or 1)
 *  11  u8  unused
 *  12  u32 extended header length, extended header follows
 * Then cylinders x heads tracks, cylinder major:
 *   u8 type, u16 TLEN, TLEN raw track bytes, then CLEN = ceil(TLEN / 8)
 *   byte bitmaps: 0x00 MFM / 0x01 FM: clock marks; 0x02 mixed and 0x80 /
 *   0x81 weak MFM / FM: two maps; 0x82 weak mixed: three maps.  Type 0xF0 is
 *   a zlib-compressed track: u16 DLEN - 1, u8 original type, u16 TLEN,
 *   DLEN bytes of zlib data inflating to TLEN + maps (a track that does not
 *   inflate to exactly that size yields no sectors).  Types 0x83 (multiple
 *   weak reads, layout not fully specified) and 0xE0 (Microdrive) make the
 *   file unsupported.
 * The trailing CRC32 uses the spec's signed-shift variant; a mismatch is
 * reported through crc_valid only (SAMdisk also just warns).
 *
 * Sectors are found on the raw track bytes: an ID field is A1 A1 A1 FE (MFM,
 * the A1s carry a clock mark) or a clock-marked FE (FM), then C H R N and a
 * CRC-CCITT that must match; the data field is the next A1 A1 A1 F8..FB /
 * marked F8..FB within a short gap.  The first ID with data wins for each
 * sector number R.  If every track holds the same set of sector numbers
 * with one size, the image is exported as "disk.img" (cylinder, head,
 * ascending R order; a TR-DOS disk gives the .trd image).  Otherwise each
 * track with sectors becomes "trackCC_H.bin" (sector data in ascending R).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/spectrum_udi/xx_spectrum_udi.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef SPECTRUM_UDI
#define XX_SPECTRUM_UDI_FILE_TYPE XX_FILE_TYPE_SPECTRUM_UDI
#else
#define XX_SPECTRUM_UDI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define UDI_HEADER_SIZE 16U
#define UDI_MAX_SECTORS 256U
#define UDI_MFM_GAP 64U /* bytes between ID CRC and data mark */
#define UDI_FM_GAP 40U
#define UDI_CRC_CHUNK 65536U
#define UDI_OUT_SIZE 65536U

typedef struct udi_track_s {
    int64_t offset;     /* absolute offset of the first byte after the type/len */
    uint32_t stored;    /* bytes stored after the 3 (or 6) byte track header */
    uint32_t tlen;      /* raw track length */
    uint8_t type;       /* effective track type (inner type for 0xF0) */
    bool compressed;
    uint8_t cylinder;
    uint8_t head;
    uint32_t sector_count;
    uint32_t sector_size; /* common size, 0 when sizes differ */
    uint64_t bytes;       /* sum of sector data sizes */
    uint8_t ids[32];      /* bitmap of sector numbers present */
} udi_track;

typedef struct udi_sector_s {
    uint32_t pos;  /* offset of the data in the track buffer */
    uint32_t size;
    uint8_t id;
} udi_sector;

typedef struct udi_member_s {
    char name[24];
    int32_t track; /* -1 = the whole disk */
    uint64_t size;
} udi_member;

typedef struct udi_stream_s {
    udi_track *tracks;
    uint32_t track_count;
    udi_member *members;
    uint32_t member_count;
    uint32_t index;
    uint64_t sector_total;
    int64_t archive_size;
    uint32_t cylinders;
    uint32_t heads;
    bool regular;
} udi_stream;

typedef struct udi_work_s {
    uint8_t *buffer;  /* track data + maps */
    uint8_t *packed;  /* zlib input */
    uint8_t *out;     /* sector data of one track */
    udi_sector sectors[UDI_MAX_SECTORS];
    uint32_t count;
} udi_work;

static uint32_t udi_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t udi_le16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static bool udi_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Number of bitmaps after the raw bytes; 0 for an unsupported type. */
static uint32_t udi_map_count(uint8_t type) {
    switch (type) {
    case 0x00: case 0x01: return 1U;
    case 0x02: case 0x80: case 0x81: return 2U;
    case 0x82: return 3U;
    default: return 0U;
    }
}

static uint32_t udi_track_body(uint32_t tlen, uint32_t maps) {
    return tlen + maps * ((tlen + 7U) / 8U);
}

static void udi_stream_free(void *opaque) {
    udi_stream *stream = (udi_stream *)opaque;
    if (!stream) return;
    if (stream->tracks) xx_mem_free(stream->tracks);
    if (stream->members) xx_mem_free(stream->members);
    xx_mem_free(stream);
}

static void udi_work_free(udi_work *work) {
    if (!work) return;
    if (work->buffer) xx_mem_free(work->buffer);
    if (work->packed) xx_mem_free(work->packed);
    if (work->out) xx_mem_free(work->out);
    xx_mem_free(work);
}

static udi_work *udi_work_new(void) {
    udi_work *work = (udi_work *)xx_mem_calloc(1U, sizeof(*work));
    if (!work) return NULL;
    /* 65535 raw bytes + three maps of 8192, and 65536 packed bytes. */
    work->buffer = (uint8_t *)xx_mem_alloc(udi_track_body(65535U, 3U));
    work->packed = (uint8_t *)xx_mem_alloc(65536U);
    work->out = (uint8_t *)xx_mem_alloc(UDI_OUT_SIZE);
    if (!work->buffer || !work->packed || !work->out) {
        udi_work_free(work);
        return NULL;
    }
    return work;
}

static uint16_t udi_ccitt(const uint8_t *data, size_t size, uint16_t crc) {
    return xx_crc16_ccitt_calc(crc, data, size);
}

/* Loads a track body (raw bytes + maps) into work->buffer. */
static bool udi_load_track(Abstractformat *format, const udi_track *track,
                           udi_work *work) {
    uint32_t body = udi_track_body(track->tlen, udi_map_count(track->type));
    if (track->tlen == 0U) return true;
    if (!track->compressed)
        return track->stored == body &&
               udi_read_at(format->device, track->offset, work->buffer, body);
    {
        size_t written = 0U;
        if (track->stored > 65536U ||
            !udi_read_at(format->device, track->offset, work->packed,
                         track->stored) ||
            !xx_zlib_stream_decode_memory(work->packed, track->stored,
                                          work->buffer, body, &written) ||
            written != body)
            return false;
    }
    return true;
}

static bool udi_marked(const uint8_t *clock, uint32_t index) {
    return (clock[index >> 3] & (uint8_t)(1U << (index & 7U))) != 0U;
}

static bool udi_is_dam(uint8_t value) {
    return value >= 0xf8U && value <= 0xfbU;
}

/* MFM sync: three clock-marked A1 bytes at `index`. */
static bool udi_mfm_sync(const uint8_t *data, const uint8_t *clock,
                         uint32_t tlen, uint32_t index) {
    return index + 4U <= tlen && data[index] == 0xa1U &&
           data[index + 1U] == 0xa1U && data[index + 2U] == 0xa1U &&
           udi_marked(clock, index) && udi_marked(clock, index + 1U) &&
           udi_marked(clock, index + 2U);
}

/* Finds the data field after an ID field ending at `from`; returns the
 * offset of the first data byte or 0 when none is found. */
static uint32_t udi_find_data(const uint8_t *data, const uint8_t *clock,
                              uint32_t tlen, uint32_t from, bool mfm, bool fm) {
    uint32_t limit = from + (mfm ? UDI_MFM_GAP : UDI_FM_GAP);
    uint32_t index;
    if (limit > tlen) limit = tlen;
    for (index = from; index < limit; ++index) {
        if (mfm && udi_mfm_sync(data, clock, tlen, index)) {
            if (data[index + 3U] == 0xfeU) return 0U; /* next ID first */
            if (udi_is_dam(data[index + 3U])) return index + 4U;
        }
        if (fm && udi_marked(clock, index)) {
            if (data[index] == 0xfeU) return 0U;
            if (udi_is_dam(data[index])) return index + 1U;
        }
    }
    return 0U;
}

/* Finds the sectors of the loaded track; bounded by one pass over TLEN with
 * a fixed look-ahead per ID. */
static void udi_scan_track(const udi_track *track, udi_work *work) {
    const uint8_t *data = work->buffer;
    const uint8_t *clock = work->buffer + track->tlen;
    uint32_t tlen = track->tlen, index;
    bool mfm = track->type == 0x00U || track->type == 0x02U ||
               track->type == 0x80U || track->type == 0x82U;
    bool fm = track->type == 0x01U || track->type == 0x02U ||
              track->type == 0x81U || track->type == 0x82U;
    uint8_t seen[32];
    xx_rt_memset(seen, 0, sizeof(seen));
    work->count = 0U;
    for (index = 0U; index < tlen; ++index) {
        uint32_t chrn = 0U, end, start, size;
        const uint8_t *field;
        uint8_t id;
        if (mfm && index + 10U <= tlen && udi_mfm_sync(data, clock, tlen, index) &&
            data[index + 3U] == 0xfeU) {
            field = data + index;
            if (udi_ccitt(field, 8U, 0xffffU) !=
                (uint16_t)(((uint32_t)field[8] << 8) | field[9]))
                continue;
            chrn = index + 4U;
            end = index + 10U;
        } else if (fm && index + 7U <= tlen && data[index] == 0xfeU &&
                   udi_marked(clock, index)) {
            field = data + index;
            if (udi_ccitt(field, 5U, 0xffffU) !=
                (uint16_t)(((uint32_t)field[5] << 8) | field[6]))
                continue;
            chrn = index + 1U;
            end = index + 7U;
        } else {
            continue;
        }
        index = end - 1U; /* resume after the ID field */
        id = data[chrn + 2U];
        if (data[chrn + 3U] > 7U) continue;
        size = 128U << data[chrn + 3U];
        start = udi_find_data(data, clock, tlen, end, mfm, fm);
        if (start == 0U || size > tlen - start) continue;
        if (seen[id >> 3] & (uint8_t)(1U << (id & 7U))) continue;
        seen[id >> 3] |= (uint8_t)(1U << (id & 7U));
        work->sectors[work->count].pos = start;
        work->sectors[work->count].size = size;
        work->sectors[work->count].id = id;
        ++work->count; /* at most 256: one per distinct id */
    }
    /* Insertion sort by id (<= 256 entries, ids unique). */
    for (index = 1U; index < work->count; ++index) {
        udi_sector key = work->sectors[index];
        uint32_t at = index;
        while (at > 0U && work->sectors[at - 1U].id > key.id) {
            work->sectors[at] = work->sectors[at - 1U];
            --at;
        }
        work->sectors[at] = key;
    }
}

/* Walks the header and track table.  With `result` NULL only validates the
 * structure (no track data is read). */
static bool udi_parse(Abstractformat *format, udi_stream **result) {
    uint8_t header[UDI_HEADER_SIZE];
    uint8_t th[6];
    udi_stream *stream = NULL;
    udi_work *work = NULL;
    int64_t total, avail, pos, end;
    uint32_t file_size, ext_size, cylinders, heads, count, index;
    if (result) *result = NULL;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    avail = total - format->base_address;
    if (avail < (int64_t)UDI_HEADER_SIZE + 4 ||
        !udi_read_at(format->device, format->base_address, header,
                     UDI_HEADER_SIZE) ||
        xx_rt_memcmp(header, "UDI!", 4U) != 0 || header[8] != 0U ||
        header[10] > 1U)
        return false;
    file_size = udi_le32(header + 4);
    ext_size = udi_le32(header + 12);
    cylinders = (uint32_t)header[9] + 1U;
    heads = (uint32_t)header[10] + 1U;
    count = cylinders * heads;
    if (file_size < UDI_HEADER_SIZE || (int64_t)file_size + 4 > avail ||
        ext_size > file_size - UDI_HEADER_SIZE)
        return false;
    end = format->base_address + (int64_t)file_size;
    pos = format->base_address + UDI_HEADER_SIZE + (int64_t)ext_size;
    if (result) {
        stream = (udi_stream *)xx_mem_calloc(1U, sizeof(*stream));
        if (!stream) return false;
        stream->tracks = (udi_track *)xx_mem_calloc(count, sizeof(udi_track));
        if (!stream->tracks) goto fail;
        stream->track_count = count;
        stream->cylinders = cylinders;
        stream->heads = heads;
        stream->archive_size = (int64_t)file_size + 4;
    }
    for (index = 0U; index < count; ++index) {
        udi_track track;
        uint32_t maps;
        xx_rt_memset(&track, 0, sizeof(track));
        if (end - pos < 3 || !udi_read_at(format->device, pos, th, 3U))
            goto fail;
        track.cylinder = (uint8_t)(index / heads);
        track.head = (uint8_t)(index % heads);
        if (th[0] == 0xf0U) {
            if (end - pos < 6 || !udi_read_at(format->device, pos, th, 6U))
                goto fail;
            track.compressed = true;
            track.type = th[3];
            track.tlen = udi_le16(th + 4);
            track.stored = udi_le16(th + 1) + 1U;
            track.offset = pos + 6;
            maps = udi_map_count(track.type);
            if (!maps || track.tlen == 0U) goto fail;
        } else {
            track.type = th[0];
            track.tlen = udi_le16(th + 1);
            maps = udi_map_count(track.type);
            if (!maps) goto fail;
            track.stored = udi_track_body(track.tlen, maps);
            track.offset = pos + 3;
        }
        if (end - track.offset < (int64_t)track.stored) goto fail;
        pos = track.offset + (int64_t)track.stored;
        if (stream) stream->tracks[index] = track;
    }
    if (!result) return true;

    /* Decode every track once to size the members. */
    work = udi_work_new();
    if (!work) goto fail;
    {
        bool regular = true;
        uint64_t disk_bytes = 0U;
        uint32_t with_sectors = 0U;
        for (index = 0U; index < count; ++index) {
            udi_track *track = &stream->tracks[index];
            uint32_t s;
            /* A compressed track that does not inflate to its declared
             * size contributes no sectors; the rest stays readable. */
            if (!udi_load_track(format, track, work)) {
                if (!track->compressed) goto fail;
                work->count = 0U;
            } else if (track->tlen != 0U) {
                udi_scan_track(track, work);
            } else {
                work->count = 0U;
            }
            track->sector_count = work->count;
            track->sector_size = work->count ? work->sectors[0].size : 0U;
            for (s = 0U; s < work->count; ++s) {
                const udi_sector *sector = &work->sectors[s];
                track->bytes += sector->size;
                if (sector->size != track->sector_size) track->sector_size = 0U;
                track->ids[sector->id >> 3] |= (uint8_t)(1U << (sector->id & 7U));
            }
            stream->sector_total += work->count;
            if (work->count) ++with_sectors;
            if (!work->count || !track->sector_size ||
                track->sector_size != stream->tracks[0].sector_size ||
                track->sector_count != stream->tracks[0].sector_count ||
                xx_rt_memcmp(track->ids, stream->tracks[0].ids, 32U) != 0)
                regular = false;
            disk_bytes += track->bytes;
        }
        stream->regular = regular;
        stream->members = (udi_member *)xx_mem_calloc(
            regular ? 1U : (with_sectors ? with_sectors : 1U), sizeof(udi_member));
        if (!stream->members) goto fail;
        if (regular) {
            xx_rt_snprintf(stream->members[0].name, sizeof(stream->members[0].name),
                           "disk.img");
            stream->members[0].track = -1;
            stream->members[0].size = disk_bytes;
            stream->member_count = 1U;
        } else {
            for (index = 0U; index < count; ++index) {
                const udi_track *track = &stream->tracks[index];
                udi_member *member;
                if (!track->sector_count) continue;
                member = &stream->members[stream->member_count++];
                xx_rt_snprintf(member->name, sizeof(member->name),
                               "track%02u_%u.bin", (unsigned)track->cylinder,
                               (unsigned)track->head);
                member->track = (int32_t)index;
                member->size = track->bytes;
            }
        }
    }
    udi_work_free(work);
    *result = stream;
    return true;
fail:
    udi_work_free(work);
    udi_stream_free(stream);
    return false;
}

static bool udi_check_crc(Abstractformat *format, int64_t file_size) {
    uint8_t *chunk = (uint8_t *)xx_mem_alloc(UDI_CRC_CHUNK);
    uint8_t stored[4];
    uint32_t crc = 0xffffffffU;
    int64_t done = 0;
    bool ok = false;
    if (!chunk) return false;
    while (done < file_size) {
        size_t part = (file_size - done > (int64_t)UDI_CRC_CHUNK)
                          ? UDI_CRC_CHUNK : (size_t)(file_size - done);
        if (!udi_read_at(format->device, format->base_address + done, chunk,
                         part))
            goto out;
        crc = xx_crc32_udi_calc(crc, chunk, part);
        done += (int64_t)part;
    }
    ok = udi_read_at(format->device, format->base_address + file_size, stored,
                     4U) && udi_le32(stored) == crc;
out:
    xx_mem_free(chunk);
    return ok;
}

static bool udi_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *udi_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool udi_set_record(xx_archive_record *record, const udi_stream *stream,
                           const udi_member *member, int64_t base) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base;
    record->header_size = UDI_HEADER_SIZE;
    record->data_offset = member->track < 0
                              ? stream->tracks[0].offset
                              : stream->tracks[member->track].offset;
    record->compressed_size = (int64_t)member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Decodes one track and writes its sectors (ascending id) to `out` in one
 * write.  Data fields never overlap (a data search stops at the next ID
 * mark), so they fit in the packed buffer; checked anyway. */
static bool udi_write_track(Abstractformat *format, const udi_track *track,
                            udi_work *work, xx_io_device *out,
                            uint64_t *written) {
    uint32_t s;
    size_t bytes = 0U, done = 0U;
    if (!udi_load_track(format, track, work)) return false;
    if (track->tlen != 0U) udi_scan_track(track, work);
    else work->count = 0U;
    if (work->count != track->sector_count) return false;
    for (s = 0U; s < work->count; ++s) {
        const udi_sector *sector = &work->sectors[s];
        if (sector->size > UDI_OUT_SIZE - bytes) return false;
        xx_rt_memcpy(work->out + bytes, work->buffer + sector->pos,
                     sector->size);
        bytes += sector->size;
    }
    if ((uint64_t)bytes != track->bytes) return false;
    while (done < bytes) {
        ssize_t amount = xx_io_write(out, work->out + done, bytes - done);
        if (amount <= 0 || (size_t)amount > bytes - done) return false;
        done += (size_t)amount;
    }
    *written += bytes;
    return true;
}

void xx_spectrum_udi_init(xx_spectrum_udi *archive, xx_io_device *device,
                          int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SPECTRUM_UDI_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-spectrum-udi");
    xx_format_set_extension(&archive->format, "udi");
    archive->format.check_is_valid = xx_spectrum_udi_check_is_valid;
    archive->format.handle_base_info = xx_spectrum_udi_handle_base_info;
    archive->format.get_format_size = xx_spectrum_udi_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_spectrum_udi_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_spectrum_udi_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_spectrum_udi_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_spectrum_udi_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_spectrum_udi_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_spectrum_udi_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_spectrum_udi *xx_spectrum_udi_create(xx_io_device *device,
                                        int64_t base_address) {
    xx_spectrum_udi *archive =
        (xx_spectrum_udi *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_spectrum_udi_init(archive, device, base_address);
    return archive;
}

void xx_spectrum_udi_destroy(xx_spectrum_udi *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_spectrum_udi_free(xx_spectrum_udi *archive) {
    if (!archive) return;
    xx_spectrum_udi_destroy(archive);
    xx_mem_free(archive);
}

bool xx_spectrum_udi_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    (void)pd;
    return udi_parse(format, NULL);
}

bool xx_spectrum_udi_handle_base_info(Abstractformat *format,
                                      xx_pd_struct *pd) {
    udi_stream *stream;
    xx_spectrum_udi *archive;
    (void)pd;
    if (!format || !udi_parse(format, &stream)) return false;
    archive = (xx_spectrum_udi *)format;
    archive->number_of_records = stream->member_count;
    archive->number_of_sectors = stream->sector_total;
    archive->cylinders = stream->cylinders;
    archive->heads = stream->heads;
    archive->regular = stream->regular;
    archive->crc_valid = udi_check_crc(format, stream->archive_size - 4);
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->member_count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    udi_stream_free(stream);
    return true;
}

int64_t xx_spectrum_udi_get_format_size(Abstractformat *format,
                                        xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_spectrum_udi_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_spectrum_udi_get_number_of_archive_records(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_spectrum_udi_handle_base_info(format, pd))
               ? ((xx_spectrum_udi *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_spectrum_udi_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    udi_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!udi_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        udi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = udi_stream_free;
    state->total_records = (int64_t)stream->member_count;
    if (!udi_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    /* An unformatted disk (every track blank, no sector IDs) is a valid
     * image with no members: iterate nothing rather than fail. */
    if (stream->member_count == 0U) return state;
    if (!udi_set_record(&state->current_record, stream, &stream->members[0],
                        format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_spectrum_udi_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_spectrum_udi_archive_record_move_to_next(Abstractformat *format,
                                                 xx_archive_record_state *state,
                                                 xx_pd_struct *pd) {
    udi_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (udi_stream *)state->internal_state) ||
        ++stream->index >= stream->member_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = udi_set_record(&state->current_record, stream,
                                       &stream->members[stream->index],
                                       format->base_address);
    return state->has_record;
}

bool xx_spectrum_udi_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    udi_stream *stream;
    udi_member *member;
    udi_work *work = NULL;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    uint64_t written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (udi_stream *)state->internal_state) ||
        stream->index >= stream->member_count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->members[stream->index];
    work = udi_work_new();
    if (!work) goto done;
    path_option = udi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (path_option) {
        if (path_option->type == XX_VAR_TYPE_STRING ||
            path_option->type == XX_VAR_TYPE_STRING_VIEW)
            base = xx_var_get_str(path_option);
        else if (path_option->type == XX_VAR_TYPE_WSTRING ||
                 path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
            owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
            base = owned_base;
        }
        if (!base) goto done;
        /* Member names are built from numbers only ("disk.img",
         * "trackCC_H.bin"), so they are safe by construction. */
        path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
                base[xx_str_len(base) - 1U] != '\\')
                   ? xx_str_concat3(base, "/", member->name)
                   : xx_str_concat(base, member->name);
        if (!path || !xx_store_create_dirs_a(path, false)) goto done;
        destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
    } else {
        /* No destination: decode only, to verify the member. */
        uint32_t index;
        uint64_t bytes = 0U;
        for (index = 0U; index < stream->track_count; ++index) {
            const udi_track *track = &stream->tracks[index];
            if (member->track >= 0 && (uint32_t)member->track != index) continue;
            if (!udi_load_track(format, track, work)) goto done;
            if (track->tlen != 0U) udi_scan_track(track, work);
            else work->count = 0U;
            if (work->count != track->sector_count) goto done;
            bytes += track->bytes;
        }
        result = bytes == member->size;
        goto done;
    }
    result = true;
    if (member->track >= 0) {
        result = udi_write_track(format, &stream->tracks[member->track], work,
                                 destination, &written);
    } else {
        uint32_t index;
        for (index = 0U; result && index < stream->track_count; ++index) {
            if (pd && xx_pd_is_stopped(pd)) result = false;
            else result = udi_write_track(format, &stream->tracks[index], work,
                                          destination, &written);
        }
    }
    if (result && written != member->size) result = false;
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (destination) xx_io_close(destination);
    if (!result && path && created) xx_rt_remove(path);
    udi_work_free(work);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_spectrum_udi_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
