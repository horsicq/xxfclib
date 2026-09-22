/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "EDC Packed" archives, the packed files found on Windows 3.x installation
 * disks (*.PI_, *.DL_, *.MS_, *.386, *.PAC). Most hold a single member - the
 * installer packs one file per disk file - but the format is a chain, and the
 * *.PAC collections carry a hundred members or more.
 *
 *   member header, 41 bytes (0x29):
 *     0x00  12  char     banner " EDC Packed " (leading space, trailing space)
 *     0x0c  12  char     original name, NUL padded; twelve characters fill the
 *                        field with no terminator at all
 *     0x18   1  u8       0x1a, the DOS end-of-file byte
 *     0x19   1  u8       format version, 0x03 in every sample
 *     0x1a   1  u8       method, 0x00 in every sample
 *     0x1b   4  u32 LE   plaintext length
 *     0x1f   4  u32 LE   member length, THIS HEADER INCLUDED
 *     0x23   2  u16 LE   reserved, 0x0000 in all 1112 corpus members
 *     0x25   2  u16 LE   DOS time (bits 0-4 sec/2, 5-10 min, 11-15 hour)
 *     0x27   2  u16 LE   DOS date (bits 0-4 day, 5-8 month, 9-15 year-1980)
 *   then (member length - 41) bytes of packed stream.
 *
 * The next member begins immediately behind, with no alignment and no
 * separator, and the last one ends exactly at end-of-file. There is no
 * directory, no member count and no checksum anywhere - the banner and that
 * exact fit are the whole of the framing.
 *
 * NOTHING HERE IS DOCUMENTED. No published description of this format was
 * found; the layout above was recovered from the corpus, where all 431
 * archives chain to an exact end-of-file fit across 1112 members, every one
 * of them carrying the same fixed bytes at 0x18..0x1a and 0x23..0x24, and the
 * names read as the DOS names the installer restores. The two bytes at 0x23
 * were the last to fall into place: they used to look like the first bytes of
 * the packed stream, and it was recognising the DOS timestamp behind them
 * that fixed the header at 41 bytes instead of 36.
 *
 *   THE CODEC IS NOVELL'S. The packed stream is byte for byte what Novell's
 * own "Packed File " containers carry: the LSB-first three-tree coder already
 * implemented here as xx_netwarepack_decode_memory() - one Huffman tree for
 * literals, one for match lengths with a 0xFE escape to a 13-bit length, one
 * for the high part of the match distance, over a 0x4000 byte window. The two
 * formats are cousins down to the framing: the same twelve byte banner plus
 * twelve byte name plus 0x1a, and the plaintext length at the same 0x1b. Only
 * the banner text, the version/method pair and the five bytes EDC adds behind
 * the length differ. The timestamp fields are EDC's own; Novell's variant has
 * none.
 *
 * That identification is anchored, not guessed: every one of the 1112 corpus
 * members decodes to EXACTLY its declared plaintext length, the executables
 * among them come out starting with "MZ", and the .PIF members come out as
 * well formed Windows program information files. A wrong codec does not land
 * on a stored length 1112 times.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/edc/xx_edc.h"

#include "xxfclib/algo/netwarepack/xx_netwarepack.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The alias macro is defined next to the enumerator in xxfc_defs.h, so testing
 * for it picks up the real file type as soon as EDC is registered there.
 * Until then the reader identifies itself as unknown rather than borrowing
 * another format's id. See the port report for the registration this needs. */
#ifdef EDC
#define XX_EDC_FILE_TYPE XX_FILE_TYPE_EDC
#else
#define XX_EDC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_EDC_HEADER_SIZE 41
#define XX_EDC_BANNER " EDC Packed "
#define XX_EDC_BANNER_SIZE 12
#define XX_EDC_NAME_OFFSET 12
#define XX_EDC_NAME_FIELD 12
#define XX_EDC_MARK_OFFSET 0x18
#define XX_EDC_RAW_OFFSET 0x1b
#define XX_EDC_TOTAL_OFFSET 0x1f
#define XX_EDC_VERSION_OFFSET 0x19
#define XX_EDC_METHOD_OFFSET 0x1a
#define XX_EDC_RESERVED_OFFSET 0x23
#define XX_EDC_TIME_OFFSET 0x25
#define XX_EDC_DATE_OFFSET 0x27
#define XX_EDC_DOS_EOF 0x1aU
#define XX_EDC_VERSION 0x03U
/* The single method value the corpus knows. Anything else is a member this
 * reader cannot describe, so the chain stops rather than guessing. */
#define XX_EDC_METHOD 0U
/* The two bytes at 0x23, zero across every corpus member. */
#define XX_EDC_RESERVED 0x0000U
#define XX_EDC_MAX_MEMBERS 65536
#define XX_EDC_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* The corpus peaks near eleven plaintext bytes per packed byte. The ceiling
 * below leaves room for a better-compressing member and still refuses a header
 * claiming a gigabyte behind a few hundred bytes. */
#define XX_EDC_MAX_RATIO 64
#define XX_EDC_RATIO_SLACK 4096

typedef struct xx_edc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_edc_member;

typedef struct xx_edc_stream_s {
    xx_edc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_edc_stream;

static void xx_edc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_edc_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_edc_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* DOS date/time -> Unix seconds. Written out rather than taken from a helper
 * because there is no CRT here; an out-of-range field yields 0 (unknown)
 * instead of a bogus instant. */
static uint64_t xx_edc_dos_to_unix(uint16_t dos_date, uint16_t dos_time) {
    static const int32_t days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int32_t year = 1980 + (int32_t)((dos_date >> 9) & 0x7f);
    int32_t month = (int32_t)((dos_date >> 5) & 0x0f);
    int32_t day = (int32_t)(dos_date & 0x1f);
    int32_t hour = (int32_t)((dos_time >> 11) & 0x1f);
    int32_t minute = (int32_t)((dos_time >> 5) & 0x3f);
    int32_t second = (int32_t)((dos_time & 0x1f) * 2);
    int32_t cursor;
    int64_t days;

    if (month < 1 || month > 12) return 0U;
    if (day < 1 || day > 31) return 0U;
    if (hour > 23 || minute > 59 || second > 59) return 0U;

    days = 0;
    for (cursor = 1970; cursor < year; ++cursor) {
        bool leap = ((cursor % 4) == 0 && (cursor % 100) != 0) ||
                    ((cursor % 400) == 0);
        days += leap ? 366 : 365;
    }
    days += days_before_month[month - 1];
    if (month > 2 && (((year % 4) == 0 && (year % 100) != 0) ||
                      ((year % 400) == 0))) {
        days += 1;
    }
    days += day - 1;
    return (uint64_t)(days * 86400 + hour * 3600 + minute * 60 + second);
}

static const xx_var *xx_edc_get_option(const xx_list_s *options,
                                       uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

static bool xx_edc_read_at(Abstractformat *self, int64_t offset,
                           uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_edc_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. EDC names come
 * from DOS and may carry either separator, so both are treated as one. */
static bool xx_edc_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    if (name[0] != '\0' && name[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;

        while (*end && *end != '/' && *end != '\\') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* The name occupies a fixed twelve byte field: a name of exactly twelve
 * characters fills it with no NUL at all, and a shorter one is NUL padded.
 * Any byte outside printable ASCII means this is not a member header. */
static char *xx_edc_name_from_field(const uint8_t *field) {
    char *name;
    size_t length = 0U;

    while (length < (size_t)XX_EDC_NAME_FIELD && field[length] != 0U) {
        if (field[length] < 0x20U || field[length] > 0x7eU) return NULL;
        ++length;
    }
    /* Everything behind the first NUL must be NUL too; a second string in the
     * padding would mean the field is not what this reader thinks it is. */
    {
        size_t index;
        for (index = length; index < (size_t)XX_EDC_NAME_FIELD; ++index) {
            if (field[index] != 0U) return NULL;
        }
    }
    if (length == 0U) return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    xx_rt_memcpy(name, field, length);
    name[length] = '\0';
    return name;
}

static void xx_edc_stream_free(void *pointer) {
    xx_edc_stream *stream = (xx_edc_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of member->name. */
static bool xx_edc_add(xx_edc_stream *stream, const xx_edc_member *member) {
    xx_edc_member *grown = (xx_edc_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_edc_stream *xx_edc_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_edc_stream *stream;
    uint8_t header[XX_EDC_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)XX_EDC_HEADER_SIZE) return NULL;

    stream = (xx_edc_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (offset < span) {
        xx_edc_member member;
        char *name;
        int64_t member_size;
        int64_t uncompressed;
        int64_t compressed;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_EDC_MAX_MEMBERS) goto fail;
        if (!xx_edc_range_within(span, offset, (int64_t)XX_EDC_HEADER_SIZE)) {
            goto fail;
        }
        if (!xx_edc_read_at(self, self->base_address + offset, header,
                            sizeof(header))) {
            goto fail;
        }
        if (xx_rt_memcmp(header, XX_EDC_BANNER, XX_EDC_BANNER_SIZE) != 0) {
            goto fail;
        }
        if (header[XX_EDC_MARK_OFFSET] != (uint8_t)XX_EDC_DOS_EOF) goto fail;
        if (header[XX_EDC_VERSION_OFFSET] != (uint8_t)XX_EDC_VERSION) {
            goto fail;
        }
        /* Method 0 is the only coder this reader can drive. Any other value
         * is a member it cannot describe, so the chain stops rather than
         * decoding it with the wrong algorithm. */
        if (header[XX_EDC_METHOD_OFFSET] != (uint8_t)XX_EDC_METHOD) goto fail;
        if (xx_edc_le16(header + XX_EDC_RESERVED_OFFSET) !=
            (uint16_t)XX_EDC_RESERVED) {
            goto fail;
        }

        /* Both lengths are bounded before either is used for anything. */
        {
            uint32_t declared = xx_edc_le32(header + XX_EDC_TOTAL_OFFSET);
            if (declared > (uint32_t)XX_EDC_MAX_DECODED) goto fail;
            member_size = (int64_t)declared;
        }
        if (member_size <= (int64_t)XX_EDC_HEADER_SIZE) goto fail;
        if (!xx_edc_range_within(span, offset, member_size)) goto fail;
        compressed = member_size - (int64_t)XX_EDC_HEADER_SIZE;
        {
            uint32_t declared = xx_edc_le32(header + XX_EDC_RAW_OFFSET);
            if (declared > (uint32_t)XX_EDC_MAX_DECODED) goto fail;
            uncompressed = (int64_t)declared;
        }
        if (uncompressed < 1) goto fail;
        if (uncompressed >
            (compressed * XX_EDC_MAX_RATIO) + XX_EDC_RATIO_SLACK) {
            goto fail;
        }
        name = xx_edc_name_from_field(header + XX_EDC_NAME_OFFSET);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = (int64_t)XX_EDC_HEADER_SIZE;
        member.data_offset =
            self->base_address + offset + (int64_t)XX_EDC_HEADER_SIZE;
        member.compressed_size = compressed;
        member.uncompressed_size = uncompressed;
        member.method = XX_EDC_METHOD;
        member.timestamp =
            xx_edc_dos_to_unix(xx_edc_le16(header + XX_EDC_DATE_OFFSET),
                               xx_edc_le16(header + XX_EDC_TIME_OFFSET));
        /* The chain has no directory entries and never will: every member is
         * a file. */
        member.is_folder = false;

        if (!xx_edc_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        offset += member_size;
    }

    /* The chain IS the format: members run back to back with no alignment and
     * no separator, so anything but an exact landing on end-of-file means the
     * file is truncated or is not an EDC archive. */
    if (offset != span) goto fail;
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_edc_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/* The stream is Novell's three-tree coder, so the shared decoder does all the
 * work; this only reads the extent the parser already computed and holds the
 * decoder to the declared plaintext length. That length is the format's ONLY
 * anchor - there is no checksum - so a decode that stops short is a failure,
 * not a short read. */
static bool xx_edc_decode(Abstractformat *self, const xx_edc_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (!self || !member || !out || !out_size) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* Re-checked here even though parse bounded them: the decode allocates
     * from these two numbers and must not depend on a caller having kept the
     * parser's invariants. */
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_EDC_MAX_DECODED) {
        return false;
    }
    if (member->compressed_size < 1 ||
        member->compressed_size > XX_EDC_MAX_DECODED) {
        return false;
    }
    if (member->method != (uint32_t)XX_EDC_METHOD) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_edc_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_netwarepack_decode_memory(input, (size_t)member->compressed_size,
                                      output,
                                      (size_t)member->uncompressed_size,
                                      &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_edc_init(xx_edc *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_EDC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-edc-packed");
    xx_format_set_extension(&archive->format, "edc");
    archive->format.check_is_valid = xx_edc_check_is_valid;
    archive->format.handle_base_info = xx_edc_handle_base_info;
    archive->format.get_format_size = xx_edc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_edc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_edc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_edc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_edc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_edc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_edc_free_archive_records_reading;
    archive->format.destroy = xx_edc_vtable_destroy;
}

xx_edc *xx_edc_create(xx_io_device *device, int64_t base_address) {
    xx_edc *archive = (xx_edc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_edc_init(archive, device, base_address);
    return archive;
}

void xx_edc_destroy(xx_edc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_edc_free(xx_edc *archive) {
    if (!archive) return;
    xx_edc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_edc_vtable_destroy(Abstractformat *self) {
    xx_edc_destroy((xx_edc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_edc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_edc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_edc_parse(self, pd);
    if (!stream) return false;
    xx_edc_stream_free(stream);
    return true;
}

bool xx_edc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_edc *archive = (xx_edc *)self;
    xx_edc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_edc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_edc_stream_free(stream);
    return true;
}

int64_t xx_edc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_edc_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_edc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_edc_set_record(xx_archive_record *record,
                              const xx_edc_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_edc_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

xx_archive_record_state *xx_edc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_edc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_edc_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_edc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_edc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_edc_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_edc_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_edc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_edc_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_edc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_edc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_edc_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

/* Extraction is unimplemented on purpose: see xx_edc_decode(). The record
 * still has to be a real one - a caller asking to unpack a record that does
 * not exist is a different error from a codec this port does not have. */
bool xx_edc_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_edc_stream *stream;
    const xx_edc_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_edc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_edc_path_safe(member->name)) return false;

    path_option = xx_edc_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_edc_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_edc_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_edc_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
