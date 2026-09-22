/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for the PC-Install .BND disk-set volume.  The signature is a
 * TRAILER, not a head magic: the last sixteen bytes are "[20/20]\0" followed
 * by the offset of the first record and the offset of the last one.  The
 * volume header is sixteen zero bytes.  Records chain forwards; each 0x114
 * byte record header carries the absolute offset of the next record at +0,
 * the stored size of its member at +0x10 and a 256-byte source path at +0x14,
 * and is followed by the member itself.  A member opens with a 0x3a-byte
 * prologue whose type word at +0x12 is 0x0074 for a full member and 0x0075
 * for a fragment continued from the previous volume; a full member's 0xe2-byte
 * header adds a 128-byte name at +0x3a, the attributes at +0xba and the
 * compressed length at +0xc2.  Payloads are raw PKWARE DCL streams.  A record
 * may hold less than the whole stream -- the last member of a volume is
 * continued on the next disk -- and such an incomplete member is listed but
 * refuses to unpack.  Ported from XArchive's installers/xpcinstall.cpp.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bnd/xx_bnd.h"

#include "xxfclib/algo/dcl/xx_dcl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef BND
#define XX_BND_FILE_TYPE XX_FILE_TYPE_BND
#else
#define XX_BND_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BND_MAX_MEMBERS 1048576U

typedef struct bnd_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;
    uint32_t crc;
    uint32_t dos_time;
    bool folder;
} bnd_member;

typedef struct bnd_stream_s {
    bnd_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} bnd_stream;

static uint16_t bnd_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t bnd_le32(const uint8_t *bytes) {
    return (uint32_t)bnd_le16(bytes) | ((uint32_t)bnd_le16(bytes + 2U) << 16U);
}

static bool bnd_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Every name field in these containers is a fixed-width buffer whose tail is
 * uninitialised builder heap, so only the bytes before the first NUL are ever
 * surfaced, and separators and traversal components are made harmless. */
static char *bnd_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U, limit = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    /* The field is fixed width and its tail is stale builder heap, so the
     * name ends at the first NUL and everything after it is discarded. */
    while (limit < size && bytes[limit] != 0U) ++limit;
    size = limit;
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

/* A member name that survives to the filesystem must be a plain relative
 * path; anything else makes the member invalid rather than renamed. */
static bool bnd_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U)) return false;
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

/* The raw 8.3 fields of these DOS-era containers are the only evidence that a
 * candidate offset really is a header, so a byte that cannot appear in a name
 * rejects the file instead of being scrubbed. */
static bool bnd_plausible_raw_name(const uint8_t *bytes, size_t size) {
    size_t index;
    if (!bytes || size == 0U || bytes[0] == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U) return true;
        if (c < 0x20U || c == 0x7fU) return false;
    }
    return true;
}

static void bnd_stream_free(void *opaque) {
    bnd_stream *stream = (bnd_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool bnd_add_member(bnd_stream *stream, const bnd_member *member) {
    bnd_member *grown;
    if (!stream || !member || stream->count >= BND_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (bnd_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define BND_VOLUME_HEADER_SIZE 16
#define BND_TRAILER_SIZE 16
#define BND_RECORD_HEADER_SIZE 0x114
#define BND_MEMBER_PROLOGUE_SIZE 0x3a
#define BND_MEMBER_HEADER_SIZE 0xe2
#define BND_RECORD_FULL 0x0074U
#define BND_RECORD_CONTINUATION 0x0075U
#define BND_LINK_NAME_SIZE 14U
#define BND_LONG_NAME_SIZE 128U

/* Every name field here is a fixed-width buffer whose tail is uninitialised
 * builder heap, so only printable bytes before the first NUL are a name. */
static bool bnd_fixed_name_ok(const uint8_t *bytes, size_t size) {
    size_t index;
    if (!bytes || size == 0U || bytes[0] == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U) return true;
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\') return false;
    }
    return false;
}

static bool bnd_parse(Abstractformat *format, bnd_stream **result) {
    uint8_t trailer[BND_TRAILER_SIZE];
    uint8_t volume[BND_VOLUME_HEADER_SIZE];
    bnd_stream *stream = NULL;
    int64_t total, size, trailer_offset, cursor;
    uint32_t first, last;
    size_t index;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < BND_VOLUME_HEADER_SIZE + BND_RECORD_HEADER_SIZE +
                   BND_MEMBER_PROLOGUE_SIZE + BND_TRAILER_SIZE)
        return false;
    trailer_offset = size - BND_TRAILER_SIZE;
    if (!bnd_read_at(format->device, format->base_address + trailer_offset,
                     trailer, sizeof(trailer)) ||
        xx_rt_memcmp(trailer, "[20/20]\x00", 8U) != 0)
        return false;
    first = bnd_le32(trailer + 8U);
    last = bnd_le32(trailer + 12U);
    if ((int64_t)first != BND_VOLUME_HEADER_SIZE ||
        (int64_t)last < BND_VOLUME_HEADER_SIZE ||
        (int64_t)last > trailer_offset - BND_RECORD_HEADER_SIZE)
        return false;
    if (!bnd_read_at(format->device, format->base_address, volume,
                     sizeof(volume)))
        return false;
    /* A plain volume zeroes this block; the shape that lives in an executable
     * overlay repeats the tag instead, so both are accepted. */
    for (index = 0U; index < sizeof(volume); ++index)
        if (volume[index] != 0U) break;
    if (index != sizeof(volume) &&
        xx_rt_memcmp(volume, "[20/20]\x00", 8U) != 0)
        return false;
    stream = (bnd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    cursor = (int64_t)first;
    for (;;) {
        uint8_t record[BND_RECORD_HEADER_SIZE];
        uint8_t member_header[BND_MEMBER_HEADER_SIZE];
        bnd_member member;
        int64_t next, stored, member_offset;
        uint16_t record_type;
        bool full;
        if (stream->count >= BND_MAX_MEMBERS) goto fail;
        if (cursor < 0 || cursor > trailer_offset - BND_RECORD_HEADER_SIZE ||
            !bnd_read_at(format->device, format->base_address + cursor, record,
                         sizeof(record)))
            goto fail;
        next = (int64_t)bnd_le32(record);
        stored = (int64_t)bnd_le32(record + 0x10U);
        member_offset = cursor + BND_RECORD_HEADER_SIZE;
        /* Bound the member against the trailer before anything reads it. */
        if (member_offset > trailer_offset ||
            stored > trailer_offset - member_offset ||
            stored < BND_MEMBER_PROLOGUE_SIZE)
            goto fail;
        if (!bnd_fixed_name_ok(record + 0x14U, 256U)) goto fail;
        if (!bnd_read_at(format->device, format->base_address + member_offset,
                         member_header, BND_MEMBER_PROLOGUE_SIZE))
            goto fail;
        record_type = bnd_le16(member_header + 0x12U);
        if (bnd_le16(member_header + 0x0eU) != 0x0074U ||
            bnd_le16(member_header + 0x10U) != 0x0001U ||
            bnd_le16(member_header + 0x14U) != 0x0005U ||
            (record_type != BND_RECORD_FULL &&
             record_type != BND_RECORD_CONTINUATION))
            goto fail;
        for (index = 0x16U; index < (size_t)BND_MEMBER_PROLOGUE_SIZE; ++index)
            if (member_header[index] != 0U) goto fail;
        full = record_type == BND_RECORD_FULL;
        xx_mem_zero(&member, sizeof(member));
        member.header_offset = format->base_address + cursor;
        member.data_offset = format->base_address + member_offset;
        if (full) {
            int64_t declared;
            if (stored < BND_MEMBER_HEADER_SIZE ||
                !bnd_read_at(format->device,
                             format->base_address + member_offset,
                             member_header, BND_MEMBER_HEADER_SIZE))
                goto fail;
            if (!bnd_fixed_name_ok(member_header + 0x3aU, BND_LONG_NAME_SIZE) ||
                bnd_le32(member_header + 0xbeU) != 0U)
                goto fail;
            declared = (int64_t)bnd_le32(member_header + 0xc2U);
            member.name = bnd_normalize_name(member_header + 0x3aU,
                                             BND_LONG_NAME_SIZE);
            member.header_size = BND_RECORD_HEADER_SIZE +
                                 BND_MEMBER_HEADER_SIZE;
            member.data_offset += BND_MEMBER_HEADER_SIZE;
            member.packed_size = stored - BND_MEMBER_HEADER_SIZE;
            member.unpacked_size = 0U;
            member.crc = bnd_le32(member_header + 0xbaU);
            member.dos_time = ((bnd_le32(member_header + 0xcaU) & 0xffffU)
                               << 16U) |
                              (bnd_le32(member_header + 0xc6U) & 0xffffU);
            /* The record may hold less than the whole stream (the last member
             * of a volume continues on the next disk).  It may never hold
             * more, and only a complete member may be decoded. */
            if (member.packed_size > declared) {
                if (member.name) xx_str_free(member.name);
                goto fail;
            }
            member.method = member.packed_size == declared ? 0U : 1U;
            if (member.packed_size >= 2) {
                uint8_t probe[2];
                if (!bnd_read_at(format->device, member.data_offset, probe,
                                 sizeof(probe)) ||
                    probe[0] > 1U || probe[1] < 4U || probe[1] > 6U) {
                    if (member.name) xx_str_free(member.name);
                    goto fail;
                }
            }
        } else {
            /* A fragment carries no name or size fields; the only name it has
             * is the link at member+0 naming what it continues. */
            if (!bnd_fixed_name_ok(member_header, BND_LINK_NAME_SIZE))
                goto fail;
            member.name = bnd_normalize_name(member_header,
                                             BND_LINK_NAME_SIZE);
            member.header_size = BND_RECORD_HEADER_SIZE +
                                 BND_MEMBER_PROLOGUE_SIZE;
            member.data_offset += BND_MEMBER_PROLOGUE_SIZE;
            member.packed_size = stored - BND_MEMBER_PROLOGUE_SIZE;
            member.method = 1U;
        }
        if (!member.name) goto fail;
        if (!bnd_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        if (next == 0) {
            if (cursor != (int64_t)last ||
                member_offset + stored != trailer_offset)
                goto fail;
            stream->archive_size = size;
            *result = stream;
            return true;
        }
        if (next <= cursor || next != member_offset + stored) goto fail;
        cursor = next;
    }
fail:
    bnd_stream_free(stream);
    return false;
}

static bool bnd_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *bnd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool bnd_set_record(xx_archive_record *record,
                           const bnd_member *member) {
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
                                          member->crc) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

/* Only a member whose whole DCL stream lives in this volume can be produced;
 * a fragment continued on another disk refuses rather than emitting a prefix.
 * The stream stores no decoded length, so it is measured first. */
static bool bnd_decode_member(Abstractformat *format, const bnd_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t produced = 0U, consumed = 0U, written = 0U;
    if (!format || !member || !plain || !plain_size || member->method != 0U ||
        member->packed_size < 2)
        return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!packed ||
        !bnd_read_at(format->device, member->data_offset, packed,
                     (size_t)member->packed_size) ||
        !xx_dcl_scan_memory(packed, (size_t)member->packed_size,
                            (size_t)256U * 1024U * 1024U, &consumed,
                            &produced) ||
        consumed != (size_t)member->packed_size)
        goto fail;
    output = (uint8_t *)xx_mem_alloc(produced != 0U ? produced : 1U);
    if (!output ||
        !xx_dcl_decode_memory(packed, (size_t)member->packed_size, output,
                              produced, &written) ||
        written != produced)
        goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_bnd_init(xx_bnd *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BND_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pcinstall-bnd");
    xx_format_set_extension(&archive->format, "bnd");
    archive->format.check_is_valid = xx_bnd_check_is_valid;
    archive->format.handle_base_info = xx_bnd_handle_base_info;
    archive->format.get_format_size = xx_bnd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bnd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bnd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bnd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bnd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bnd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bnd_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_bnd *xx_bnd_create(xx_io_device *device, int64_t base_address) {
    xx_bnd *archive = (xx_bnd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_bnd_init(archive, device, base_address);
    return archive;
}

void xx_bnd_destroy(xx_bnd *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_bnd_free(xx_bnd *archive) {
    if (!archive) return;
    xx_bnd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_bnd_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    bnd_stream *stream;
    (void)pd;
    if (!bnd_parse(format, &stream)) return false;
    bnd_stream_free(stream);
    return true;
}

bool xx_bnd_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    bnd_stream *stream;
    xx_bnd *archive;
    (void)pd;
    if (!format || !bnd_parse(format, &stream)) return false;
    archive = (xx_bnd *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    bnd_stream_free(stream);
    return true;
}

int64_t xx_bnd_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_bnd_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_bnd_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_bnd_handle_base_info(format, pd))
               ? ((xx_bnd *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_bnd_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    bnd_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!bnd_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        bnd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = bnd_stream_free;
    state->total_records = stream->count;
    if (!bnd_copy_options(&state->options, options) ||
        !bnd_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_bnd_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_bnd_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    bnd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (bnd_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = bnd_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_bnd_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    bnd_stream *stream;
    bnd_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (bnd_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!bnd_safe_output_name(member->name) ||
        !bnd_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = bnd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
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
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_bnd_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
