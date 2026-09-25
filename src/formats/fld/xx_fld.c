/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CodeBase install file group (.FLD) archives.
 *
 * There is no signature and no directory. The file is a chain: a 27-byte
 * record, then that member's payload, then the next record, and so on. A
 * record is
 *
 *   0x00  u8   name length, always 0x0C
 *   0x01  12   name, space padded, raw DOS bytes
 *   0x0D  i32 LE compressed size
 *   0x11  i32 LE uncompressed size
 *   0x15  u16 LE DOS time
 *   0x17  u16 LE DOS date
 *   0x19  u8   method: 0 = stored, 1 = PKWARE DCL implode
 *   0x1A  u8   record marker, always '$' (0x24)
 *   0x1B       compressed size bytes of payload
 *
 * and the chain is followed by a 5-byte trailer (some producers omit it, so
 * a chain ending exactly at EOF is also accepted).
 *
 * With no magic number, the format's defence against a false positive is not
 * any single field but the tiling identity: walking the chain from offset 0,
 * every record must carry 0x0C at 0x00 and '$' at 0x1A, every name byte must
 * be >= 0x20, every method must be one the format defines, and the walk must
 * land either exactly on EOF or exactly 5 bytes short of it. A run of
 * unrelated bytes that satisfies the first record almost never re-synchronises
 * on the second, and essentially never terminates on the byte. Loosening the
 * landing rule to "at or before EOF" would make this reader claim arbitrary
 * files.
 *
 * A DCL-imploded payload keeps its own two-byte prelude (literal mode then
 * dictionary-size bits), which the decoder reads off the front of the packed
 * buffer; the parse only sanity-checks those two bytes.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/fld/xx_fld.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_FLD_COPY_CHUNK (64 * 1024)

typedef struct xx_fld_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_fld_member;

typedef struct xx_fld_stream_s {
    xx_fld_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_fld_stream;

static void xx_fld_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_fld_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_fld_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_fld_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_fld_stream_free(void *pointer) {
    xx_fld_stream *stream = (xx_fld_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_fld_add(xx_fld_stream *stream,
                          const xx_fld_member *member) {
    xx_fld_member *grown = (xx_fld_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_FLD_NAME_BUFFER (XX_FLD_NAME_SIZE * 3 + 24)
#define XX_FLD_RECORD_SIZE 27
#define XX_FLD_NAME_OFFSET 1
#define XX_FLD_NAME_SIZE 12
#define XX_FLD_TRAILER_SIZE 5
#define XX_FLD_NAME_LENGTH_BYTE 0x0CU
#define XX_FLD_MARKER 0x24U
#define XX_FLD_METHOD_STORED 0U
#define XX_FLD_METHOD_DCL_IMPLODE 1U
#define XX_FLD_MAX_MEMBERS 100000
#define XX_FLD_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_fld_le16(const uint8_t *data);
static uint32_t xx_fld_le32(const uint8_t *data);
static bool xx_fld_name_valid(const uint8_t *raw);
static char *xx_fld_name_build(const uint8_t *raw, size_t fallback_index);
static xx_fld_stream *xx_fld_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_fld_decode(Abstractformat *self, const xx_fld_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* 12 raw bytes, each escaping to at most "%XX", plus room for the synthetic
 * "recordNNNNN" fallback and a NUL. */

static uint16_t xx_fld_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_fld_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Every byte of the 12-byte field must be 0x20 or above, and the field must
 * not be entirely spaces. Bytes above 0x7E are deliberately permitted: the
 * field is raw DOS bytes and the builder below escapes anything unsafe as
 * %XX, so nothing unprintable reaches the filesystem. Together with the 0x0C
 * length byte and the '$' marker this is the per-record half of the format's
 * false-positive defence -- twelve bytes of arbitrary data clearing it is
 * already unlikely, and it must clear on every record in the chain. */
static bool xx_fld_name_valid(const uint8_t *raw) {
    size_t index;
    bool any = false;

    for (index = 0U; index < (size_t)XX_FLD_NAME_SIZE; ++index) {
        if (raw[index] < 0x20U) return false;
        if (raw[index] != 0x20U) any = true;
    }
    return any;
}

/* Trailing spaces and NULs are stripped, then path separators and the Windows
 * reserved punctuation are escaped as %XX rather than folded to '_': escaping
 * is reversible and cannot collapse two distinct members onto one output
 * file. */
static char *xx_fld_name_build(const uint8_t *raw, size_t fallback_index) {
    static const char hex_digits[] = "0123456789ABCDEF";
    char escaped[XX_FLD_NAME_BUFFER];
    size_t escaped_length = 0U;
    size_t length = (size_t)XX_FLD_NAME_SIZE;
    size_t index;
    char *result;

    while (length > 0U &&
           (raw[length - 1U] == 0x20U || raw[length - 1U] == 0x00U)) {
        --length;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t byte = raw[index];
        bool safe = byte > 0x20U && byte < 0x7FU && byte != '%' &&
                    byte != '/' && byte != '\\' && byte != ':' &&
                    byte != '*' && byte != '?' && byte != '"' &&
                    byte != '<' && byte != '>' && byte != '|';

        if (safe) {
            escaped[escaped_length++] = (char)byte;
        } else {
            escaped[escaped_length++] = '%';
            escaped[escaped_length++] = hex_digits[(byte >> 4) & 0x0FU];
            escaped[escaped_length++] = hex_digits[byte & 0x0FU];
        }
    }
    if (escaped_length == 0U) {
        /* A field of all spaces is rejected by the validator, but one that is
         * space padded after a run of 0x20-only-safe bytes can still strip to
         * nothing; give it a stable synthetic name rather than an empty one. */
        char digits[24];
        size_t digit_count = 0U;
        size_t value = fallback_index;

        escaped[escaped_length++] = 'r';
        escaped[escaped_length++] = 'e';
        escaped[escaped_length++] = 'c';
        escaped[escaped_length++] = 'o';
        escaped[escaped_length++] = 'r';
        escaped[escaped_length++] = 'd';
        do {
            digits[digit_count++] = (char)('0' + (int)(value % 10U));
            value /= 10U;
        } while (value != 0U && digit_count < sizeof(digits));
        while (digit_count > 0U) {
            escaped[escaped_length++] = digits[--digit_count];
        }
    }

    result = (char *)xx_mem_alloc(escaped_length + 1U);
    if (!result) return NULL;
    for (index = 0U; index < escaped_length; ++index) {
        result[index] = escaped[index];
    }
    result[escaped_length] = '\0';
    return result;
}

static xx_fld_stream *xx_fld_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_fld_stream *stream = NULL;
    uint8_t record[XX_FLD_RECORD_SIZE];
    uint8_t prelude[2];
    int64_t offset = 0;
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* One complete record plus the trailer is the smallest possible file. */
    if (span < XX_FLD_RECORD_SIZE + XX_FLD_TRAILER_SIZE) return NULL;

    stream = (xx_fld_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (offset + XX_FLD_RECORD_SIZE <= span) {
        xx_fld_member member;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t data_offset;
        uint32_t method;
        uint16_t dos_time;
        uint16_t dos_date;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_FLD_MAX_MEMBERS) goto fail;

        /* A short read here is an I/O failure, not a chain end: the range was
         * already checked to be inside the span. */
        if (!xx_fld_read_at(self, self->base_address + offset, record,
                            (size_t)XX_FLD_RECORD_SIZE)) {
            goto fail;
        }

        /* The two constant bytes of the record. Without a magic number these
         * are all that anchors each link of the chain; they are checked on
         * every record, not just the first. */
        if (record[0] != XX_FLD_NAME_LENGTH_BYTE) break;
        if (record[0x1A] != XX_FLD_MARKER) break;
        if (!xx_fld_name_valid(record + XX_FLD_NAME_OFFSET)) break;

        compressed_size =
            (int64_t)(int32_t)xx_fld_le32(record + 0x0D);
        uncompressed_size =
            (int64_t)(int32_t)xx_fld_le32(record + 0x11);
        dos_time = xx_fld_le16(record + 0x15);
        dos_date = xx_fld_le16(record + 0x17);
        method = (uint32_t)record[0x19];

        if (compressed_size < 0 || uncompressed_size < 0) break;
        if (compressed_size > XX_FLD_MAX_DECODED ||
            uncompressed_size > XX_FLD_MAX_DECODED) {
            break;
        }
        /* An undefined method would otherwise be carried into decode, which
         * refuses it there; rejecting at parse keeps a listing honest. */
        if (method != XX_FLD_METHOD_STORED &&
            method != XX_FLD_METHOD_DCL_IMPLODE) {
            break;
        }
        /* A stored member is its own decompressed form, so disagreeing sizes
         * mean this is not a record. */
        if (method == XX_FLD_METHOD_STORED &&
            compressed_size != uncompressed_size) {
            break;
        }

        data_offset = offset + XX_FLD_RECORD_SIZE;
        if (!xx_fld_range_within(span, data_offset, compressed_size)) break;

        if (method == XX_FLD_METHOD_DCL_IMPLODE) {
            /* A DCL stream opens with a literal-mode byte in {0,1} and a
             * dictionary-size byte in {4,5,6}, and cannot be shorter than
             * those two bytes plus an end marker. Cheap, and it rejects most
             * chains that would otherwise re-synchronise by luck. */
            if (compressed_size < 3) break;
            if (!xx_fld_read_at(self, self->base_address + data_offset,
                                prelude, sizeof(prelude))) {
                goto fail;
            }
            if (prelude[0] > 1U) break;
            if (prelude[1] < 4U || prelude[1] > 6U) break;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_fld_name_build(record + XX_FLD_NAME_OFFSET,
                                        stream->count);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_FLD_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = method;
        /* DOS date in the high half, DOS time in the low half. */
        member.timestamp = ((uint64_t)dos_date << 16) | (uint64_t)dos_time;
        if (!xx_fld_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset = data_offset + compressed_size;
    }

    if (stream->count == 0U) goto fail;

    /* The decisive test. The chain has to tile the container exactly: either
     * it ends on EOF, or it ends exactly one 5-byte trailer short of it.
     * Anything else -- a chain that stops early because a record failed one of
     * the checks above, or one that leaves slack -- is not a .FLD. With no
     * magic number this is the only thing separating a real archive from a
     * coincidence, so it must never be relaxed to "at or before EOF". */
    if (offset != span && offset + XX_FLD_TRAILER_SIZE != span) goto fail;

    stream->archive_size = span;
    return stream;

fail:
    xx_fld_stream_free(stream);
    return NULL;
}


/* The chain is walked in full during detection, so the member count needs a
 * producer-plausible ceiling; the reference corpus tops out at 261. */
/* No member in the family exceeds ~1.5 MB unpacked. The cap only exists so a
 * corrupt size field cannot become an attacker-chosen allocation. */

/*
 * Extraction. The container states the decoded length, so both methods
 * allocate exactly that and refuse a decoder that writes any other amount:
 * reporting a short member as success is the one failure the caller cannot
 * detect.
 */
static bool xx_fld_decode(Abstractformat *self, const xx_fld_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool result = false;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_FLD_MAX_DECODED ||
        member->uncompressed_size > XX_FLD_MAX_DECODED) {
        return false;
    }
    /* A method the format defines but this reader does not implement must
     * fail here rather than fall through to the stored path, which would
     * hand back compressed bytes dressed as data. */
    if (member->method != XX_FLD_METHOD_STORED &&
        member->method != XX_FLD_METHOD_DCL_IMPLODE) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!packed) return false;
    if (member->compressed_size != 0 &&
        !xx_fld_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size)) {
        goto cleanup;
    }
    if (pd && xx_pd_is_stopped(pd)) goto cleanup;

    if (member->method == XX_FLD_METHOD_STORED) {
        /* parse already refuses a stored record whose two sizes disagree;
         * re-checking keeps decode correct on its own terms. */
        if (member->compressed_size != member->uncompressed_size) goto cleanup;
        *out = packed;
        *out_size = (size_t)member->compressed_size;
        packed = NULL;
        result = true;
        goto cleanup;
    }

    plain = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!plain) goto cleanup;
    /* The two-byte DCL prelude is part of the packed stream, not stripped. */
    if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        goto cleanup;
    }
    *out = plain;
    *out_size = written;
    plain = NULL;
    result = true;

cleanup:
    xx_mem_free(plain);
    xx_mem_free(packed);
    return result;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_fld_init(xx_fld *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_FLD;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-fld");
    xx_format_set_extension(&archive->format, "fld");
    archive->format.check_is_valid = xx_fld_check_is_valid;
    archive->format.handle_base_info = xx_fld_handle_base_info;
    archive->format.get_format_size = xx_fld_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_fld_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_fld_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_fld_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_fld_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_fld_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_fld_free_archive_records_reading;
    archive->format.destroy = xx_fld_vtable_destroy;
}

xx_fld *xx_fld_create(xx_io_device *device, int64_t base_address) {
    xx_fld *archive = (xx_fld *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_fld_init(archive, device, base_address);
    return archive;
}

void xx_fld_destroy(xx_fld *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_fld_free(xx_fld *archive) {
    if (!archive) return;
    xx_fld_destroy(archive);
    xx_mem_free(archive);
}

static void xx_fld_vtable_destroy(Abstractformat *self) {
    xx_fld_destroy((xx_fld *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_fld_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_fld_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_fld_parse(self, pd);
    if (!stream) return false;
    xx_fld_stream_free(stream);
    return true;
}

bool xx_fld_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_fld *archive = (xx_fld *)self;
    xx_fld_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_fld_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_fld_stream_free(stream);
    return true;
}

int64_t xx_fld_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_fld_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_fld *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_fld_set_record(xx_archive_record *record,
                                 const xx_fld_member *member) {
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

static bool xx_fld_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
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

static const xx_var *xx_fld_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_fld_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_fld_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_fld_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_fld_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_fld_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_fld_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_fld_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_fld_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_fld_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_fld_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fld_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_fld_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_fld_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_fld_stream *stream;
    const xx_fld_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fld_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_fld_path_safe(member->name)) return false;

    path_option = xx_fld_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_fld_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_fld_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_fld_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
