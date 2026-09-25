/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Nero Burning ROM disc image (.nrg).  xx_nrg.h carries the layout.
 *
 * The footer at the end of the file points at a chunk list; the DAOX/DAOI
 * and ETN2/ETNF chunks in it give every track's extent in the file, and
 * those extents are the members, cut exactly as stored.  Written from the
 * format's structure; XArchive's XNeroDiscImage (MIT, same author) served as
 * the design reference for the checks, and libmirage's image-nrg parser
 * (GPL) was read only to confirm field positions - no code was taken from
 * it.
 *
 * Detection has nothing to go on in the first bytes (they are track data),
 * so this is a late, magic-less probe: it reads the last 12 bytes and gives
 * up unless they hold a "NER5" / "NERO" footer, before it allocates
 * anything.  Past that, the whole chunk chain up to END! and at least one
 * consistent track extent are required.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/nrg/xx_nrg.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as NRG is registered there. */
#ifdef NRG
#define XX_NRG_FILE_TYPE XX_FILE_TYPE_NRG
#else
#define XX_NRG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define NRG_FOOTER_V1 8
#define NRG_FOOTER_V2 12
#define NRG_CHUNK_HEADER 8
/* Real lists hold a handful of chunks per session. */
#define NRG_MAX_CHUNKS 4096U
#define NRG_DAO_HEADER 22U
#define NRG_DAOI_ENTRY 30U
#define NRG_DAOX_ENTRY 42U
#define NRG_ETNF_ENTRY 20U
#define NRG_ETN2_ENTRY 32U
#define NRG_CUE_ENTRY 8U
/* The biggest table payload: a DAOX header plus 99 entries. */
#define NRG_MAX_TABLE (NRG_DAO_HEADER + XX_NRG_MAX_TRACKS * NRG_DAOX_ENTRY)
#define NRG_MIN_SECTOR 2048
#define NRG_COPY_CHUNK 65536U
#define NRG_NAME_SIZE 24U /* "track99.pregap.cdda" is the longest */

typedef struct nrg_track_s {
    int64_t entry_offset; /* table entry, from the image start */
    int64_t entry_size;
    int64_t pregap;       /* index 0; equals start when nothing is stored */
    int64_t start;        /* index 1 */
    int64_t end;
    uint32_t sector_size;
    uint8_t mode;
    bool tao;
    uint32_t table;       /* ordinal of the chunk that described it */
} nrg_track;

typedef struct nrg_member_s {
    char name[NRG_NAME_SIZE];
    const char *comment;  /* a literal chosen here, never from the file */
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;  /* absolute device offset */
    int64_t size;
} nrg_member;

typedef struct nrg_image_s {
    nrg_track tracks[XX_NRG_MAX_TRACKS];
    uint32_t track_count;
    nrg_member members[XX_NRG_MAX_MEMBERS];
    size_t member_count;
    uint32_t kept_tracks;
    uint32_t sessions;
    uint32_t version;
    int64_t chunk_list;   /* from the image start */
    int64_t image_size;
} nrg_image;

typedef struct nrg_stream_s {
    nrg_image *image;
    size_t index;
} nrg_stream;

static uint32_t nrg_be16(const uint8_t *b) {
    return ((uint32_t)b[0] << 8U) | (uint32_t)b[1];
}

static uint32_t nrg_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t nrg_be64(const uint8_t *b) {
    return ((uint64_t)nrg_be32(b) << 32U) | (uint64_t)nrg_be32(b + 4U);
}

static bool nrg_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool nrg_tag_is(const uint8_t *tag, const char *text) {
    return xx_rt_memcmp(tag, text, 4U) == 0;
}

/* Nero's chunk tags are upper-case letters, digits and '!'. */
static bool nrg_tag_plausible(const uint8_t *tag) {
    size_t index;
    for (index = 0U; index < 4U; ++index) {
        uint8_t c = tag[index];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '!'))
            return false;
    }
    return true;
}

/* Sector size of a mode code, for the TAO tables that carry no size. */
static uint32_t nrg_mode_sector_size(uint8_t mode) {
    switch (mode) {
    case 0x00:
    case 0x02: return 2048U;
    case 0x03: return 2336U;
    case 0x05:
    case 0x06:
    case 0x07: return 2352U;
    case 0x0F:
    case 0x10:
    case 0x11: return 2448U;
    default: return 0U;
    }
}

static bool nrg_sector_size_known(uint32_t size) {
    return size == 2048U || size == 2336U || size == 2352U || size == 2448U;
}

static bool nrg_is_audio(const nrg_track *track) {
    return (track->mode == 0x07U || track->mode == 0x10U) &&
           track->sector_size >= 2352U;
}

static const char *nrg_comment(const nrg_track *track) {
    switch (track->mode) {
    case 0x00: return "MODE1/2048";
    case 0x02: return "MODE2/2048";
    case 0x03: return "MODE2/2336";
    case 0x05: return "MODE1/2352";
    case 0x06: return "MODE2/2352";
    case 0x07: return "AUDIO";
    case 0x0F: return "MODE1/2448";
    case 0x10: return "CDG";
    case 0x11: return "MODE2/2448";
    default: break;
    }
    switch (track->sector_size) {
    case 2048U: return "DATA/2048";
    case 2336U: return "DATA/2336";
    case 2352U: return "DATA/2352";
    default: return "DATA/2448";
    }
}

static const char *nrg_extension(const nrg_track *track, bool pregap) {
    if (nrg_is_audio(track)) return track->sector_size == 2448U ? "cdg" : "cdda";
    if (track->sector_size == 2048U) return pregap ? "bin" : "iso";
    return "bin";
}

static void nrg_make_name(char *name, uint32_t number, const char *extension,
                          bool pregap) {
    static const char prefix[] = "track";
    static const char middle[] = ".pregap.";
    size_t used = 0U, index;
    for (index = 0U; prefix[index]; ++index) name[used++] = prefix[index];
    name[used++] = (char)('0' + (number / 10U) % 10U);
    name[used++] = (char)('0' + number % 10U);
    if (pregap) {
        for (index = 0U; middle[index]; ++index) name[used++] = middle[index];
    } else {
        name[used++] = '.';
    }
    for (index = 0U; extension[index] && used < NRG_NAME_SIZE - 1U; ++index)
        name[used++] = extension[index];
    name[used] = 0;
}

static bool nrg_add_track(nrg_image *image, const nrg_track *track) {
    if (image->track_count >= XX_NRG_MAX_TRACKS) return false;
    image->tracks[image->track_count++] = *track;
    return true;
}

/* DAOX / DAOI: a 22-byte header, then one fixed-size entry per track. */
static bool nrg_parse_dao(nrg_image *image, const uint8_t *payload,
                          uint32_t size, int64_t payload_offset, bool wide,
                          uint32_t table) {
    uint32_t entry_size = wide ? NRG_DAOX_ENTRY : NRG_DAOI_ENTRY;
    uint32_t count, index;
    if (size < NRG_DAO_HEADER + entry_size ||
        (size - NRG_DAO_HEADER) % entry_size != 0U)
        return false;
    count = (size - NRG_DAO_HEADER) / entry_size;
    if (count > XX_NRG_MAX_TRACKS) return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *entry = payload + NRG_DAO_HEADER + index * entry_size;
        uint64_t values[3];
        nrg_track track;
        uint32_t field;
        xx_mem_zero(&track, sizeof(track));
        track.sector_size = nrg_be16(entry + 12U);
        track.mode = entry[14];
        for (field = 0U; field < 3U; ++field)
            values[field] = wide ? nrg_be64(entry + 18U + field * 8U)
                                 : (uint64_t)nrg_be32(entry + 18U + field * 4U);
        /* Every extent sits in the data area in front of the chunk list. */
        if (!nrg_sector_size_known(track.sector_size) ||
            values[0] > values[1] || values[1] >= values[2] ||
            values[2] > (uint64_t)image->chunk_list)
            return false;
        track.pregap = (int64_t)values[0];
        track.start = (int64_t)values[1];
        track.end = (int64_t)values[2];
        if ((track.start - track.pregap) % (int64_t)track.sector_size != 0 ||
            (track.end - track.start) % (int64_t)track.sector_size != 0)
            return false;
        track.entry_offset =
            payload_offset + (int64_t)NRG_DAO_HEADER + (int64_t)index * entry_size;
        track.entry_size = (int64_t)entry_size;
        track.table = table;
        if (!nrg_add_track(image, &track)) return false;
    }
    return true;
}

/* ETN2 / ETNF: one entry per track, the sector size implied by the mode. */
static bool nrg_parse_etn(nrg_image *image, const uint8_t *payload,
                          uint32_t size, int64_t payload_offset, bool wide,
                          uint32_t table) {
    uint32_t entry_size = wide ? NRG_ETN2_ENTRY : NRG_ETNF_ENTRY;
    uint32_t count, index;
    if (size < entry_size || size % entry_size != 0U) return false;
    count = size / entry_size;
    if (count > XX_NRG_MAX_TRACKS) return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *entry = payload + index * entry_size;
        uint64_t offset, length;
        nrg_track track;
        xx_mem_zero(&track, sizeof(track));
        if (wide) {
            offset = nrg_be64(entry);
            length = nrg_be64(entry + 8U);
            track.mode = entry[19];
        } else {
            offset = nrg_be32(entry);
            length = nrg_be32(entry + 4U);
            track.mode = entry[11];
        }
        track.sector_size = nrg_mode_sector_size(track.mode);
        if (track.sector_size == 0U || length == 0U ||
            length % track.sector_size != 0U ||
            offset > (uint64_t)image->chunk_list ||
            length > (uint64_t)image->chunk_list - offset)
            return false;
        track.pregap = (int64_t)offset;
        track.start = (int64_t)offset;
        track.end = (int64_t)(offset + length);
        track.tao = true;
        track.entry_offset = payload_offset + (int64_t)index * entry_size;
        track.entry_size = (int64_t)entry_size;
        track.table = table;
        if (!nrg_add_track(image, &track)) return false;
    }
    return true;
}

/* A TAO table that repeats extents a DAO table already gives would list the
 * same data twice; such TAO entries are dropped. */
static bool nrg_overlaps_dao(const nrg_image *image, const nrg_track *track) {
    uint32_t index;
    for (index = 0U; index < image->track_count; ++index) {
        const nrg_track *other = &image->tracks[index];
        if (!other->tao && track->start < other->end &&
            other->pregap < track->end)
            return true;
    }
    return false;
}

static bool nrg_add_member(nrg_image *image, const nrg_track *track,
                           uint32_t number, bool pregap, int64_t base) {
    nrg_member *member;
    if (image->member_count >= XX_NRG_MAX_MEMBERS) return false;
    member = &image->members[image->member_count++];
    nrg_make_name(member->name, number, nrg_extension(track, pregap), pregap);
    member->comment = nrg_comment(track);
    member->header_offset = base + track->entry_offset;
    member->header_size = track->entry_size;
    member->data_offset = base + (pregap ? track->pregap : track->start);
    member->size = pregap ? track->start - track->pregap
                          : track->end - track->start;
    return true;
}

static bool nrg_build_members(nrg_image *image, int64_t base) {
    uint32_t index, number = 0U, last_table = UINT32_MAX;
    for (index = 0U; index < image->track_count; ++index) {
        const nrg_track *track = &image->tracks[index];
        if (track->tao && nrg_overlaps_dao(image, track)) continue;
        ++number;
        if (track->table != last_table) {
            ++image->sessions;
            last_table = track->table;
        }
        if (track->start > track->pregap &&
            !nrg_add_member(image, track, number, true, base))
            return false;
        if (!nrg_add_member(image, track, number, false, base)) return false;
    }
    image->kept_tracks = number;
    return image->member_count != 0U;
}

static bool nrg_parse(Abstractformat *format, nrg_image **result) {
    uint8_t footer[NRG_FOOTER_V2];
    uint8_t header[NRG_CHUNK_HEADER];
    uint8_t *table = NULL;
    nrg_image *image = NULL;
    int64_t total, size, footer_offset, position;
    uint32_t chunks, tables = 0U, version;
    uint64_t list;
    bool ended = false;
    if (result) *result = NULL;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* One sector of data, one table entry, END! and the footer at least. */
    if (size < NRG_MIN_SECTOR + NRG_CHUNK_HEADER + NRG_ETNF_ENTRY +
                   NRG_CHUNK_HEADER + NRG_FOOTER_V1 ||
        !nrg_read_at(format->device,
                     format->base_address + size - NRG_FOOTER_V2, footer,
                     sizeof(footer)))
        return false;
    if (nrg_tag_is(footer, "NER5")) {
        version = 2U;
        list = nrg_be64(footer + 4U);
        footer_offset = size - NRG_FOOTER_V2;
    } else if (nrg_tag_is(footer + 4U, "NERO")) {
        version = 1U;
        list = nrg_be32(footer + 8U);
        footer_offset = size - NRG_FOOTER_V1;
    } else {
        return false;
    }
    if (list < NRG_MIN_SECTOR ||
        list > (uint64_t)(footer_offset - NRG_CHUNK_HEADER))
        return false;
    image = (nrg_image *)xx_mem_calloc(1U, sizeof(*image));
    table = (uint8_t *)xx_mem_alloc(NRG_MAX_TABLE);
    if (!image || !table) goto fail;
    image->version = version;
    image->chunk_list = (int64_t)list;
    image->image_size = size;
    position = (int64_t)list;
    for (chunks = 0U; chunks < NRG_MAX_CHUNKS; ++chunks) {
        int64_t payload_offset;
        uint32_t chunk_size;
        bool dao, etn, wide;
        if (position > footer_offset - NRG_CHUNK_HEADER ||
            !nrg_read_at(format->device, format->base_address + position,
                         header, sizeof(header)) ||
            !nrg_tag_plausible(header))
            goto fail;
        chunk_size = nrg_be32(header + 4U);
        payload_offset = position + NRG_CHUNK_HEADER;
        if ((int64_t)chunk_size > footer_offset - payload_offset) goto fail;
        if (nrg_tag_is(header, "END!")) {
            ended = true;
            break;
        }
        dao = nrg_tag_is(header, "DAOX") || nrg_tag_is(header, "DAOI");
        etn = nrg_tag_is(header, "ETN2") || nrg_tag_is(header, "ETNF");
        wide = nrg_tag_is(header, "DAOX") || nrg_tag_is(header, "ETN2");
        if (dao || etn) {
            if (chunk_size > NRG_MAX_TABLE ||
                !nrg_read_at(format->device,
                             format->base_address + payload_offset, table,
                             chunk_size))
                goto fail;
            if (dao ? !nrg_parse_dao(image, table, chunk_size, payload_offset,
                                     wide, tables)
                    : !nrg_parse_etn(image, table, chunk_size, payload_offset,
                                     wide, tables))
                goto fail;
            ++tables;
        } else if ((nrg_tag_is(header, "CUEX") || nrg_tag_is(header, "CUES")) &&
                   chunk_size % NRG_CUE_ENTRY != 0U) {
            goto fail;
        }
        position = payload_offset + (int64_t)chunk_size;
    }
    if (!ended || image->track_count == 0U ||
        !nrg_build_members(image, format->base_address))
        goto fail;
    xx_mem_free(table);
    *result = image;
    return true;
fail:
    if (table) xx_mem_free(table);
    if (image) xx_mem_free(image);
    return false;
}

static void nrg_stream_free(void *opaque) {
    nrg_stream *stream = (nrg_stream *)opaque;
    if (!stream) return;
    if (stream->image) xx_mem_free(stream->image);
    xx_mem_free(stream);
}

static bool nrg_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *nrg_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* XX_META_ID_OPT_MAX_MEMBER_SIZE, when given, refuses larger members. */
static bool nrg_member_allowed(Abstractformat *format,
                               const xx_list_s *options, int64_t size) {
    const xx_var *limit = xx_format_resolve_extra_parameter(
        format, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!limit) return true;
    switch (limit->type) {
    case XX_VAR_TYPE_UINT8:
    case XX_VAR_TYPE_UINT16:
    case XX_VAR_TYPE_UINT32:
    case XX_VAR_TYPE_UINT64: return (uint64_t)size <= xx_var_get_u64(limit);
    case XX_VAR_TYPE_INT8:
    case XX_VAR_TYPE_INT16:
    case XX_VAR_TYPE_INT32:
    case XX_VAR_TYPE_INT64: {
        int64_t value = xx_var_get_i64(limit);
        return value >= 0 && size <= value;
    }
    default: return true;
    }
}

static bool nrg_set_record(xx_archive_record *record,
                           const nrg_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    /* Tracks are stored verbatim, so both sizes agree and nothing is
     * compressed. */
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          member->comment) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Copies the member's bytes to @p destination through a fixed buffer; with
 * no destination the bytes are only read, which proves they are there. */
static bool nrg_copy_member(Abstractformat *format, const nrg_member *member,
                            xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0;
    bool result = true;
    if (!format || !member || member->size < 0 || member->data_offset < 0 ||
        member->size > xx_io_total_size(format->device) - member->data_offset)
        return false;
    buffer = (uint8_t *)xx_mem_alloc(NRG_COPY_CHUNK);
    if (!buffer) return false;
    while (done < member->size) {
        size_t amount = member->size - done > (int64_t)NRG_COPY_CHUNK
                            ? NRG_COPY_CHUNK
                            : (size_t)(member->size - done);
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !nrg_read_at(format->device, member->data_offset + done, buffer,
                         amount)) {
            result = false;
            break;
        }
        while (destination && written < amount) {
            ssize_t step = xx_io_write(destination, buffer + written,
                                       amount - written);
            if (step <= 0 || (size_t)step > amount - written) break;
            written += (size_t)step;
        }
        if (destination && written != amount) {
            result = false;
            break;
        }
        done += (int64_t)amount;
    }
    xx_mem_free(buffer);
    return result;
}

void xx_nrg_init(xx_nrg *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_NRG_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-nrg");
    xx_format_set_extension(&archive->format, "nrg");
    archive->format.check_is_valid = xx_nrg_check_is_valid;
    archive->format.handle_base_info = xx_nrg_handle_base_info;
    archive->format.get_format_size = xx_nrg_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_nrg_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_nrg_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_nrg_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_nrg_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_nrg_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_nrg_free_archive_records_reading;
    archive->chunk_list_offset = -1;
}

xx_nrg *xx_nrg_create(xx_io_device *device, int64_t base_address) {
    xx_nrg *archive = (xx_nrg *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_nrg_init(archive, device, base_address);
    return archive;
}

void xx_nrg_destroy(xx_nrg *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_nrg_free(xx_nrg *archive) {
    if (!archive) return;
    xx_nrg_destroy(archive);
    xx_mem_free(archive);
}

bool xx_nrg_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    nrg_image *image;
    (void)pd;
    if (!nrg_parse(format, &image)) return false;
    xx_mem_free(image);
    return true;
}

bool xx_nrg_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    nrg_image *image;
    xx_nrg *archive;
    (void)pd;
    if (!format || !nrg_parse(format, &image)) return false;
    archive = (xx_nrg *)format;
    archive->number_of_records = image->member_count;
    archive->number_of_sessions = image->sessions;
    archive->footer_version = image->version;
    archive->chunk_list_offset = image->chunk_list;
    archive->number_of_tracks = image->kept_tracks;
    format->number_of_archive_records = image->member_count;
    format->format_size = image->image_size;
    format->is_valid = true;
    format->base_info_handled = true;
    xx_mem_free(image);
    return true;
}

int64_t xx_nrg_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_nrg_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_nrg_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_nrg_handle_base_info(format, pd))
               ? ((xx_nrg *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_nrg_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    nrg_stream *stream;
    xx_archive_record_state *state;
    nrg_image *image;
    (void)pd;
    if (!nrg_parse(format, &image)) return NULL;
    stream = (nrg_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        xx_mem_free(image);
        return NULL;
    }
    stream->image = image;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        nrg_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = nrg_stream_free;
    state->total_records = (int64_t)image->member_count;
    if (!nrg_copy_options(&state->options, options) ||
        !nrg_set_record(&state->current_record, &image->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_nrg_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_nrg_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    nrg_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (nrg_stream *)state->internal_state) || !stream->image ||
        stream->index + 1U >= stream->image->member_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = nrg_set_record(&state->current_record,
                                       &stream->image->members[stream->index]);
    return state->has_record;
}

bool xx_nrg_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    nrg_stream *stream;
    const nrg_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (nrg_stream *)state->internal_state) || !stream->image ||
        stream->index >= stream->image->member_count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->image->members[stream->index];
    if (!nrg_member_allowed(format, &state->options, member->size))
        return false;
    path_option = nrg_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return nrg_copy_member(format, member, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* Member names are built here ("trackNN..."), never taken from the
     * image, so they are safe and unique by construction. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = nrg_copy_member(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_nrg_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
