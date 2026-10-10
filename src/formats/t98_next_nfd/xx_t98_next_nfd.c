/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the T98-Next NFD floppy image (NEC PC-98), read by
 * MAME floptool "nfd" and the Neko Project II family.  Written from the
 * published layout; the extracted bytes were checked against black-box
 * runs of floptool (nfd to pc98).
 *
 * Common file header, 0x120 bytes, little endian:
 *   0x000  "T98FDDIMAGE.R0\0" or "T98FDDIMAGE.R1\0"
 *   0x010  comment, 0x100 bytes
 *   0x110  dwHeadSize: offset of the first data byte
 *   0x114  write-protect flag, 0x115 number of heads, 10 reserved bytes
 * A 16-byte sector ID is C, H, R, N, MFM flag, DDAM flag, status, ST0..ST2,
 * then (r0) PDA and 5 reserved bytes, or (r1) retry count, PDA and 4
 * reserved bytes.  A sector holds 128 << N bytes.
 *
 * r0: 163 tracks * 26 sector IDs follow the file header at 0x120 (plus 16
 *     reserved bytes); an ID with C == 0xFF is an absent sector.  The data
 *     of every present sector follows at dwHeadSize in table order.
 * r1: 164 track-header offsets follow at 0x120, then dwAddInfo and 12
 *     reserved bytes (0x3C0 in all).  A track header is a count of sectors
 *     (u16), a count of diagnostic entries (u16) and 12 reserved bytes,
 *     followed by that many sector IDs and 16-byte diagnostic IDs (command,
 *     C, H, R, N, status, ST0..ST2, retry count, data length u32, PDA,
 *     reserved).  The data runs from dwHeadSize track by track: each
 *     sector followed by its retry copies, then each diagnostic block
 *     (length * (retry + 1) bytes).  MAME ignores the sector retry copies;
 *     Neko Project II skips them, and so does this reader.
 *
 * The member is the plain sector dump: tracks in table order and, within a
 * track, the first copy of every sector ordered by its R number, which is
 * what a raw-image writer produces for a regular disk.  Diagnostic data and
 * retry copies are not part of it.  Unformatted tracks add nothing.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/t98_next_nfd/xx_t98_next_nfd.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef T98_NEXT_NFD
#define XX_T98_NEXT_NFD_FILE_TYPE XX_FILE_TYPE_T98_NEXT_NFD
#else
#define XX_T98_NEXT_NFD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define NFD_FILE_HEAD 0x120U
#define NFD_R0_TRACKS 163U
#define NFD_R0_SECTORS 26U
#define NFD_R0_TABLE (NFD_R0_TRACKS * NFD_R0_SECTORS * 16U) /* 0x10910 */
#define NFD_R0_MIN_HEADER (NFD_FILE_HEAD + NFD_R0_TABLE)    /* 0x10A30 */
#define NFD_R1_TRACKS 164U
#define NFD_R1_HEAD 0x3C0U
#define NFD_R1_MAX_SECTORS 255U
#define NFD_R1_MAX_DIAG 255U
#define NFD_MAX_N 7U /* 16384 bytes */
#define NFD_MAX_HEADER (16U * 1024U * 1024U)
#define NFD_MAX_IMAGE (64U * 1024U * 1024U)
#define NFD_MAX_TOTAL_SECTORS (NFD_R1_TRACKS * NFD_R1_MAX_SECTORS)
#define NFD_MEMBER_NAME "image.img"
#define NFD_COPY_BUFFER (256U * 1024U)

typedef struct nfd_sector_s {
    int64_t offset; /* absolute */
    uint32_t size;
} nfd_sector;

typedef struct nfd_info_s {
    uint32_t revision;
    uint32_t header_size;
    uint32_t heads;
    uint32_t tracks;
    uint32_t sectors;
    uint64_t image_size; /* the dump */
    uint64_t data_size;  /* everything after dwHeadSize */
    int64_t archive_size;
    int64_t header_offset; /* absolute */
} nfd_info;

typedef struct nfd_stream_s {
    nfd_info info;
    nfd_sector *sectors;
    size_t count;
    size_t index;
} nfd_stream;

/* A sector of the track being scanned: where it is and its R number. */
typedef struct nfd_pending_s {
    int64_t offset;
    uint32_t size;
    uint8_t r;
} nfd_pending;

static bool nfd_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Appends one track's sectors, ordered by R (stable), to the output list.
 * pending holds at most NFD_R1_MAX_SECTORS (r1) or 26 (r0) entries. */
static void nfd_flush_track(nfd_pending *pending, uint32_t count, nfd_sector *out, uint32_t *out_count)
{
    uint32_t i, j;
    for (i = 1U; i < count; ++i) {
        nfd_pending key = pending[i];
        j = i;
        while (j > 0U && pending[j - 1U].r > key.r) {
            pending[j] = pending[j - 1U];
            --j;
        }
        pending[j] = key;
    }
    if (!out) {
        *out_count += count;
        return;
    }
    for (i = 0U; i < count; ++i) {
        out[*out_count].offset = pending[i].offset;
        out[*out_count].size = pending[i].size;
        ++*out_count;
    }
}

/* Adds a sector of 'size' bytes plus 'copies' extra copies to the running
 * totals.  All sums stay far below 2^63; the caps keep them there. */
static bool nfd_add_sector(nfd_info *info, uint64_t *cursor, uint64_t limit, uint32_t size, uint32_t copies)
{
    uint64_t span = (uint64_t)size * ((uint64_t)copies + 1U);
    if (span > limit || *cursor > limit - span) return false;
    info->image_size += size;
    if (info->image_size > NFD_MAX_IMAGE) return false;
    *cursor += span;
    return true;
}

/* Parses the header and every sector descriptor; bounded reads only.  If
 * 'sectors' is non-NULL it receives an allocated list of dump sectors. */
static bool nfd_parse(Abstractformat *format, nfd_info *out, nfd_sector **sectors)
{
    uint8_t head[NFD_R1_HEAD];
    nfd_info info;
    nfd_pending *pending = NULL;
    nfd_sector *list = NULL;
    uint8_t *table = NULL;
    int64_t total, size;
    uint64_t cursor = 0U, limit;
    uint32_t count = 0U;
    bool ok = false;
    if (sectors) *sectors = NULL;
    if (!format || !format->device || !out || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)NFD_FILE_HEAD || !nfd_read_at(format->device, format->base_address, head, NFD_FILE_HEAD) || xx_rt_memcmp(head, "T98FDDIMAGE.R", 13U) != 0 ||
        (head[13] != '0' && head[13] != '1') || head[14] != 0U)
        return false;
    xx_mem_zero(&info, sizeof(info));
    info.revision = (uint32_t)(head[13] - '0');
    info.header_size = xx_data_get_u32(head + 0x110U, 4, 0, false);
    info.heads = head[0x115];
    info.header_offset = format->base_address;
    if (info.header_size > NFD_MAX_HEADER || (int64_t)info.header_size > size || info.header_size < (info.revision ? NFD_R1_HEAD : NFD_R0_MIN_HEADER)) return false;
    limit = (uint64_t)(size - (int64_t)info.header_size);
    pending = (nfd_pending *)xx_mem_alloc(sizeof(nfd_pending) * NFD_R1_MAX_SECTORS);
    if (!pending) return false;
    if (sectors) {
        list = (nfd_sector *)xx_mem_alloc(sizeof(nfd_sector) * NFD_MAX_TOTAL_SECTORS);
        if (!list) goto done;
    }
    if (info.revision == 0U) {
        uint32_t track, index;
        table = (uint8_t *)xx_mem_alloc(NFD_R0_TABLE);
        if (!table || !nfd_read_at(format->device, format->base_address + NFD_FILE_HEAD, table, NFD_R0_TABLE)) goto done;
        for (track = 0U; track < NFD_R0_TRACKS; ++track) {
            uint32_t present = 0U;
            for (index = 0U; index < NFD_R0_SECTORS; ++index) {
                const uint8_t *id = table + ((size_t)track * NFD_R0_SECTORS + index) * 16U;
                uint32_t bytes;
                if (id[0] == 0xFFU) continue;
                if (id[3] > NFD_MAX_N) goto done;
                bytes = 128U << id[3];
                pending[present].offset = format->base_address + (int64_t)info.header_size + (int64_t)cursor;
                pending[present].size = bytes;
                pending[present].r = id[2];
                if (!nfd_add_sector(&info, &cursor, limit, bytes, 0U)) goto done;
                ++present;
            }
            if (present) ++info.tracks;
            nfd_flush_track(pending, present, list, &count);
        }
    } else {
        uint32_t track;
        uint8_t *entries;
        table = (uint8_t *)xx_mem_alloc(16U + 16U * (NFD_R1_MAX_SECTORS + NFD_R1_MAX_DIAG));
        if (!table || !nfd_read_at(format->device, format->base_address + NFD_FILE_HEAD, head + NFD_FILE_HEAD, NFD_R1_HEAD - NFD_FILE_HEAD)) goto done;
        entries = table + 16U;
        for (track = 0U; track < NFD_R1_TRACKS; ++track) {
            uint32_t where = xx_data_get_u32(head + NFD_FILE_HEAD + 4U * track, 4, 0, false);
            uint32_t nsec, ndiag, index, present = 0U;
            if (where == 0U) continue;
            /* Track headers sit in the header area, after the file header. */
            if (where < NFD_R1_HEAD || where > info.header_size || info.header_size - where < 16U ||
                !nfd_read_at(format->device, format->base_address + (int64_t)where, table, 16U))
                goto done;
            nsec = xx_data_get_u16(table, 2, 0, false);
            ndiag = xx_data_get_u16(table + 2U, 2, 0, false);
            if (nsec > NFD_R1_MAX_SECTORS || ndiag > NFD_R1_MAX_DIAG || (uint64_t)16U * (1U + nsec + ndiag) > (uint64_t)(info.header_size - where) ||
                !nfd_read_at(format->device, format->base_address + (int64_t)where + 16, entries, 16U * (nsec + ndiag)))
                goto done;
            for (index = 0U; index < nsec; ++index) {
                const uint8_t *id = entries + 16U * index;
                uint32_t bytes;
                if (id[3] > NFD_MAX_N) goto done;
                bytes = 128U << id[3];
                pending[present].offset = format->base_address + (int64_t)info.header_size + (int64_t)cursor;
                pending[present].size = bytes;
                pending[present].r = id[2];
                if (!nfd_add_sector(&info, &cursor, limit, bytes, id[10])) goto done;
                ++present;
            }
            for (index = 0U; index < ndiag; ++index) {
                const uint8_t *id = entries + 16U * (nsec + index);
                uint64_t span = (uint64_t)xx_data_get_u32(id + 10U, 4, 0, false) * ((uint64_t)id[9] + 1U);
                if (span > limit || cursor > limit - span) goto done;
                cursor += span;
            }
            if (present) ++info.tracks;
            nfd_flush_track(pending, present, list, &count);
        }
    }
    info.sectors = count;
    info.data_size = cursor;
    info.archive_size = (int64_t)info.header_size + (int64_t)cursor;
    *out = info;
    if (sectors) {
        *sectors = list;
        list = NULL;
    }
    ok = true;
done:
    if (list) xx_mem_free(list);
    if (table) xx_mem_free(table);
    xx_mem_free(pending);
    return ok;
}

static void nfd_stream_free(void *opaque)
{
    nfd_stream *stream = (nfd_stream *)opaque;
    if (!stream) return;
    if (stream->sectors) xx_mem_free(stream->sectors);
    xx_mem_free(stream);
}

static bool nfd_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *nfd_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool nfd_set_record(xx_archive_record *record, const nfd_info *info)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = info->header_offset;
    record->header_size = info->header_size;
    record->data_offset = info->header_offset + (int64_t)info->header_size;
    record->compressed_size = (int64_t)info->data_size;
    return xx_archive_record_set_original_name(record, NFD_MEMBER_NAME) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, info->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, info->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_t98_next_nfd_init(xx_t98_next_nfd *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_T98_NEXT_NFD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-t98-nfd");
    xx_format_set_extension(&archive->format, "nfd");
    archive->format.check_is_valid = xx_t98_next_nfd_check_is_valid;
    archive->format.handle_base_info = xx_t98_next_nfd_handle_base_info;
    archive->format.get_format_size = xx_t98_next_nfd_get_format_size;
    archive->format.get_number_of_archive_records = xx_t98_next_nfd_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_t98_next_nfd_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_t98_next_nfd_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_t98_next_nfd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_t98_next_nfd_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_t98_next_nfd_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_t98_next_nfd *xx_t98_next_nfd_create(xx_io_device *device, int64_t base_address)
{
    xx_t98_next_nfd *archive = (xx_t98_next_nfd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_t98_next_nfd_init(archive, device, base_address);
    return archive;
}

void xx_t98_next_nfd_destroy(xx_t98_next_nfd *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_t98_next_nfd_free(xx_t98_next_nfd *archive)
{
    if (!archive) return;
    xx_t98_next_nfd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_t98_next_nfd_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    nfd_info info;
    (void)pd;
    return nfd_parse(format, &info, NULL);
}

bool xx_t98_next_nfd_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    nfd_info info;
    xx_t98_next_nfd *archive;
    (void)pd;
    if (!format || !nfd_parse(format, &info, NULL)) return false;
    archive = (xx_t98_next_nfd *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + info.archive_size;
    archive->revision = info.revision;
    archive->header_size = info.header_size;
    archive->heads = info.heads;
    archive->tracks = info.tracks;
    archive->sectors = info.sectors;
    archive->image_size = info.image_size;
    format->number_of_archive_records = 1U;
    format->format_size = info.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_t98_next_nfd_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_t98_next_nfd_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_t98_next_nfd_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_t98_next_nfd_handle_base_info(format, pd)) ? ((xx_t98_next_nfd *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_t98_next_nfd_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    nfd_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    stream = (nfd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!nfd_parse(format, &stream->info, &stream->sectors)) {
        nfd_stream_free(stream);
        return NULL;
    }
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        nfd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = nfd_stream_free;
    state->total_records = 1;
    if (!nfd_copy_options(&state->options, options) || !nfd_set_record(&state->current_record, &stream->info)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_t98_next_nfd_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_t98_next_nfd_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    nfd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (nfd_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = nfd_set_record(&state->current_record, &stream->info);
    return state->has_record;
}

/* Copies the sectors through one bounded buffer: runs of sectors that are
 * adjacent in the file are read together and the output is written in
 * large blocks. */
static bool nfd_write_dump(xx_io_device *source, const nfd_stream *stream, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t *buffer;
    size_t index = 0U, fill = 0U;
    bool ok = true;
    buffer = (uint8_t *)xx_mem_alloc(NFD_COPY_BUFFER);
    if (!buffer) return false;
    while (ok && index < stream->info.sectors) {
        int64_t offset = stream->sectors[index].offset;
        uint64_t length = stream->sectors[index].size;
        ++index;
        while (index < stream->info.sectors && stream->sectors[index].offset == offset + (int64_t)length && length <= NFD_MAX_IMAGE) {
            length += stream->sectors[index].size;
            ++index;
        }
        while (ok && length > 0U) {
            size_t amount = NFD_COPY_BUFFER - fill;
            if ((uint64_t)amount > length) amount = (size_t)length;
            if ((pd && xx_pd_is_stopped(pd)) || !nfd_read_at(source, offset, buffer + fill, amount)) {
                ok = false;
                break;
            }
            fill += amount;
            offset += (int64_t)amount;
            length -= amount;
            if (fill == NFD_COPY_BUFFER) {
                if (xx_io_write(destination, buffer, fill) != (ssize_t)fill) ok = false;
                fill = 0U;
            }
        }
    }
    if (ok && fill != 0U && xx_io_write(destination, buffer, fill) != (ssize_t)fill) ok = false;
    xx_mem_free(buffer);
    return ok;
}

bool xx_t98_next_nfd_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    nfd_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    int64_t total;
    bool result = false;
    bool created = false;
    if (!format || !format->device || !state || state->format != format || !state->has_record || !(stream = (nfd_stream *)state->internal_state) ||
        stream->index >= stream->count || (!stream->sectors && stream->info.sectors != 0U) || (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < stream->info.header_offset || stream->info.archive_size > total - stream->info.header_offset) return false;
    path_option = nfd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", NFD_MEMBER_NAME)
                                                                                                  : xx_str_concat(base, NFD_MEMBER_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = nfd_write_dump(format->device, stream, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_t98_next_nfd_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
