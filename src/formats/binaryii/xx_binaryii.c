/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for Apple II Binary II, the 1986 transport wrapper used for
 * .bny, .bxy and .sdk files.  Layout ported from XArchive's transport module
 * (transport/xbinaryii.cpp) and confirmed against the corpus.
 *
 * The archive is a chain of 128-byte member headers, each immediately
 * followed by that member's bytes padded up to the next 128-byte boundary.
 * Every multi-byte field is little-endian, and several are split: the low
 * half sits in the original layout and the high half in the block Binary II
 * version 1 appended at 0x6d.
 *
 *   0x00  "\x0aGL"          signature
 *   0x03  uint8    access
 *   0x04  uint8    ProDOS file type      (high byte at 0x70)
 *   0x05  uint16   ProDOS aux type       (high half at 0x6d)
 *   0x07  uint8    storage type          (high byte at 0x71; 0x0d = folder)
 *   0x08  uint16   block count           (high half at 0x72)
 *   0x0a  uint16   modification date / 0x0c time  (ProDOS packed)
 *   0x0e  uint16   creation date     / 0x10 time
 *   0x12  uint8    0x02, the format's second identity byte
 *   0x14  uint24   end of file          (high byte at 0x74)
 *   0x17  uint8    name length (1..64)
 *   0x18  name
 *   0x79  uint8    OS type
 *   0x7a  uint16   native file type
 *   0x7d  uint8    data flags (0x80 = payload is itself a compressed file)
 *   0x7e  uint8    Binary II version
 *   0x7f  uint8    files to follow
 *
 * The "files to follow" countdown is the only terminator the format has, so
 * it is required to step down by exactly one; that keeps a stray "\nGL" in a
 * payload from being adopted as the next member.  Binary II never
 * compresses: every member is stored, and the data flag only records the
 * writer's claim that the payload is a compressed file in its own right.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/binaryii/xx_binaryii.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* The enum entry is added by the coordinator; keep compiling until it is. */
#ifdef BINARYII
#define XX_BINARYII_FILE_TYPE XX_FILE_TYPE_BINARYII
#else
#define XX_BINARYII_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define B2_HEADER_SIZE 128
#define B2_BLOCK_SIZE 128
#define B2_MAX_MEMBERS 65536U
#define B2_MAX_NAME 64U

#define B2_OFF_ACCESS 0x03U
#define B2_OFF_FILETYPE 0x04U
#define B2_OFF_AUXTYPE 0x05U
#define B2_OFF_STORAGETYPE 0x07U
#define B2_OFF_BLOCKCOUNT 0x08U
#define B2_OFF_MODDATE 0x0aU
#define B2_OFF_MODTIME 0x0cU
#define B2_OFF_CREATEDATE 0x0eU
#define B2_OFF_CREATETIME 0x10U
#define B2_OFF_ID 0x12U
#define B2_OFF_EOF 0x14U
#define B2_OFF_NAMESIZE 0x17U
#define B2_OFF_NAME 0x18U
#define B2_OFF_AUXTYPE_HIGH 0x6dU
#define B2_OFF_ACCESS_HIGH 0x6fU
#define B2_OFF_FILETYPE_HIGH 0x70U
#define B2_OFF_STORAGETYPE_HIGH 0x71U
#define B2_OFF_BLOCKCOUNT_HIGH 0x72U
#define B2_OFF_EOF_HIGH 0x74U
#define B2_OFF_OSTYPE 0x79U
#define B2_OFF_NATIVETYPE 0x7aU
#define B2_OFF_DATAFLAGS 0x7dU
#define B2_OFF_VERSION 0x7eU
#define B2_OFF_FILESTOFOLLOW 0x7fU

#define B2_ID_BYTE 0x02U
/* ProDOS storage type $0d is a directory.  Such a member can declare a
 * non-zero EOF (TIC.BNY's "TERMCAPS" declares 1024) yet store nothing at
 * all; trusting that EOF desynchronizes the whole chain. */
#define B2_STORAGETYPE_DIRECTORY 0x0dU
#define B2_DATAFLAG_COMPRESSED 0x80U

typedef struct b2_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t stored_size;
    uint64_t declared_size;
    uint32_t aux_type;
    uint32_t block_count;
    uint16_t file_type;
    uint16_t storage_type;
    uint16_t access;
    uint16_t native_type;
    uint32_t modified;
    uint8_t os_type;
    uint8_t data_flags;
    uint8_t version;
    uint8_t files_to_follow;
    bool folder;
} b2_member;

typedef struct b2_stream_s {
    b2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint8_t version;
} b2_stream;

static uint16_t b2_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static bool b2_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static int64_t b2_align_up(int64_t value) {
    int64_t remainder;
    if (value < 0 || value > INT64_MAX - B2_BLOCK_SIZE) return -1;
    remainder = value % (int64_t)B2_BLOCK_SIZE;
    return remainder == 0 ? value : value + ((int64_t)B2_BLOCK_SIZE - remainder);
}

static bool b2_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* ProDOS packs the date as yyyyyyym mmmddddd and the time as 000hhhhh
 * 00mmmmmm, with a two-digit year: below 40 is 20xx, otherwise 19xx.  The
 * result is reported as a packed DOS-style value so a caller has something
 * ordered to work with. */
static uint32_t b2_prodos_time(uint16_t date, uint16_t time) {
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    if (date == 0U && time == 0U) return 0U;
    year = (uint32_t)((date >> 9U) & 0x7fU);
    month = (uint32_t)((date >> 5U) & 0x0fU);
    day = (uint32_t)(date & 0x1fU);
    hour = (uint32_t)((time >> 8U) & 0x1fU);
    minute = (uint32_t)(time & 0x3fU);
    if (month < 1U || month > 12U || day < 1U || day > 31U || hour > 23U ||
        minute > 59U)
        return 0U;
    year += (year < 40U) ? 2000U : 1900U;
    return (year << 20U) | (month << 16U) | (day << 11U) | (hour << 6U) |
           minute;
}

/* ProDOS names are 7-bit ASCII, but a writer may leave the Apple II high bit
 * set on every character (GUADCNL.DOX stores "USERS.GUIDE.BXY" as D5 D3 C5
 * ...).  Masking is what turns that member into a readable name instead of
 * mojibake.  '/' is the ProDOS path separator and must survive into a
 * subdirectory; an absolute or dot-relative path makes the file invalid. */
static char *b2_decode_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t index;
    size_t segment_start = 0U;
    if (size == 0U || size > B2_MAX_NAME) return NULL;
    name = (char *)xx_mem_alloc(size + 1U);
    if (!name) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = (uint8_t)(bytes[index] & 0x7fU);
        if (c < 0x20U || c > 0x7eU || c == '\\') {
            xx_mem_free(name);
            return NULL;
        }
        name[index] = (char)c;
    }
    name[size] = 0;
    for (index = 0U; index <= size; ++index) {
        if (index == size || name[index] == '/') {
            size_t length = index - segment_start;
            if (length == 0U ||
                (length == 1U && name[segment_start] == '.') ||
                (length == 2U && name[segment_start] == '.' &&
                 name[segment_start + 1U] == '.')) {
                xx_mem_free(name);
                return NULL;
            }
            segment_start = index + 1U;
        }
    }
    return name;
}

static bool b2_safe_output_name(const char *name) {
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

static void b2_stream_free(void *opaque) {
    b2_stream *stream = (b2_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool b2_add_member(b2_stream *stream, const b2_member *member) {
    b2_member *grown;
    if (!stream || !member || stream->count >= B2_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (b2_member *)xx_mem_realloc(stream->items,
                                        (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool b2_parse_header(const uint8_t *header, int64_t header_offset,
                            b2_member *member) {
    size_t name_size;
    if (!header || !member) return false;
    if (header[0] != 0x0aU || header[1] != 0x47U || header[2] != 0x4cU)
        return false;
    if (header[B2_OFF_ID] != B2_ID_BYTE) return false;
    name_size = header[B2_OFF_NAMESIZE];
    if (name_size < 1U || name_size > B2_MAX_NAME) return false;

    xx_mem_zero(member, sizeof(*member));
    member->name = b2_decode_name(header + B2_OFF_NAME, name_size);
    if (!member->name) return false;

    member->header_offset = header_offset;
    member->data_offset = header_offset + B2_HEADER_SIZE;
    member->declared_size = (uint64_t)b2_le16(header + B2_OFF_EOF) |
                            ((uint64_t)header[B2_OFF_EOF + 2U] << 16U) |
                            ((uint64_t)header[B2_OFF_EOF_HIGH] << 24U);
    member->aux_type = (uint32_t)b2_le16(header + B2_OFF_AUXTYPE) |
                       ((uint32_t)b2_le16(header + B2_OFF_AUXTYPE_HIGH) << 16U);
    member->block_count =
        (uint32_t)b2_le16(header + B2_OFF_BLOCKCOUNT) |
        ((uint32_t)b2_le16(header + B2_OFF_BLOCKCOUNT_HIGH) << 16U);
    member->file_type = (uint16_t)((uint16_t)header[B2_OFF_FILETYPE] |
                                   ((uint16_t)header[B2_OFF_FILETYPE_HIGH]
                                    << 8U));
    member->storage_type =
        (uint16_t)((uint16_t)header[B2_OFF_STORAGETYPE] |
                   ((uint16_t)header[B2_OFF_STORAGETYPE_HIGH] << 8U));
    member->access = (uint16_t)((uint16_t)header[B2_OFF_ACCESS] |
                                ((uint16_t)header[B2_OFF_ACCESS_HIGH] << 8U));
    member->native_type = b2_le16(header + B2_OFF_NATIVETYPE);
    member->os_type = header[B2_OFF_OSTYPE];
    member->data_flags = header[B2_OFF_DATAFLAGS];
    member->version = header[B2_OFF_VERSION];
    member->files_to_follow = header[B2_OFF_FILESTOFOLLOW];
    member->modified = b2_prodos_time(b2_le16(header + B2_OFF_MODDATE),
                                      b2_le16(header + B2_OFF_MODTIME));
    member->folder =
        ((member->storage_type & 0xffU) == B2_STORAGETYPE_DIRECTORY);
    /* A declared EOF is only believed once it has been bounded by the
     * caller; a directory stores nothing regardless of what it declares. */
    member->stored_size =
        member->folder || member->declared_size > (uint64_t)INT64_MAX
            ? 0
            : (int64_t)member->declared_size;
    return true;
}

static bool b2_parse(Abstractformat *format, b2_stream **result) {
    b2_stream *stream;
    int64_t total;
    int64_t size;
    int64_t offset = 0;
    bool follow_expected = false;
    uint8_t expected_to_follow = 0U;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < 0 || format->base_address > total) return false;
    size = total - format->base_address;
    if (size < B2_HEADER_SIZE) return false;

    stream = (b2_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;

    while (stream->count < B2_MAX_MEMBERS) {
        uint8_t header[B2_HEADER_SIZE];
        b2_member member;
        int64_t member_end;

        if (!b2_range_within(size, offset, B2_HEADER_SIZE) ||
            !b2_read_at(format->device, format->base_address + offset, header,
                        sizeof(header)))
            goto fail;
        if (!b2_parse_header(header, format->base_address + offset, &member))
            goto fail;
        /* The countdown must step by exactly one or the chain is not real. */
        if (follow_expected && member.files_to_follow != expected_to_follow) {
            xx_str_free(member.name);
            goto fail;
        }
        /* Bound the declared payload against the real file before it is
         * recorded or walked past. */
        if (!b2_range_within(size, offset + B2_HEADER_SIZE,
                             member.stored_size)) {
            xx_str_free(member.name);
            goto fail;
        }
        if (!b2_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        member_end = offset + B2_HEADER_SIZE + member.stored_size;
        if (member.files_to_follow == 0U) {
            /* The final payload is not always padded on disk, so the aligned
             * end may legitimately lie past the file; anything after it is a
             * ProDOS allocation overlay, not part of the archive. */
            int64_t aligned_end = b2_align_up(member_end);
            if (aligned_end < 0) goto fail;
            stream->archive_size = aligned_end < size ? aligned_end : size;
            stream->version = stream->items[0].version;
            *result = stream;
            return true;
        }
        offset = b2_align_up(member_end);
        if (offset < 0) goto fail;
        follow_expected = true;
        expected_to_follow = (uint8_t)(member.files_to_follow - 1U);
    }
fail:
    b2_stream_free(stream);
    return false;
}

static bool b2_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *b2_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool b2_set_record(xx_archive_record *record, const b2_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = B2_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->stored_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->stored_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->stored_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->modified) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->access) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->file_type) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_EXTERNAL_ATTRS,
                                          member->aux_type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

void xx_binaryii_init(xx_binaryii *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BINARYII_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-binary2");
    xx_format_set_extension(&archive->format, "bny");
    archive->format.check_is_valid = xx_binaryii_check_is_valid;
    archive->format.handle_base_info = xx_binaryii_handle_base_info;
    archive->format.get_format_size = xx_binaryii_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_binaryii_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_binaryii_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_binaryii_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_binaryii_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_binaryii_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_binaryii_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_binaryii *xx_binaryii_create(xx_io_device *device, int64_t base_address) {
    xx_binaryii *archive = (xx_binaryii *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_binaryii_init(archive, device, base_address);
    return archive;
}

void xx_binaryii_destroy(xx_binaryii *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_binaryii_free(xx_binaryii *archive) {
    if (!archive) return;
    xx_binaryii_destroy(archive);
    xx_mem_free(archive);
}

bool xx_binaryii_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    b2_stream *stream;
    (void)pd;
    if (!b2_parse(format, &stream)) return false;
    b2_stream_free(stream);
    return true;
}

bool xx_binaryii_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    b2_stream *stream;
    xx_binaryii *archive;
    (void)pd;
    if (!format || !b2_parse(format, &stream)) {
        if (format) format->is_valid = false;
        return false;
    }
    archive = (xx_binaryii *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->version = stream->version;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_BINARYII_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    b2_stream_free(stream);
    return true;
}

int64_t xx_binaryii_get_format_size(Abstractformat *format,
                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_binaryii_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_binaryii_get_number_of_archive_records(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_binaryii_handle_base_info(format, pd))
               ? ((xx_binaryii *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_binaryii_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    b2_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!b2_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        b2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = b2_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!b2_copy_options(&state->options, options) ||
        !b2_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_binaryii_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_binaryii_archive_record_move_to_next(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    b2_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (b2_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = b2_set_record(&state->current_record,
                                      &stream->items[stream->index]);
    return state->has_record;
}

bool xx_binaryii_unpack_current_archive_record(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    b2_stream *stream;
    const b2_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (b2_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!b2_safe_output_name(member->name)) return false;
    path_option = b2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        int64_t total = xx_io_total_size(format->device);
        return b2_range_within(total, member->data_offset,
                               member->stored_size);
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
    result = xx_store_unpack_device_to_file(format->device,
                                            member->data_offset,
                                            member->stored_size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_binaryii_free_archive_records_reading(Abstractformat *format,
                                              xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
