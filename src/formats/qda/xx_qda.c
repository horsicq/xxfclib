/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QDA archives.
 *
 *   archive header, 0x100 bytes at offset 0 (only the first 0x10 are used):
 *     0x00  u32 LE  packed flag: 0 = every member stored, 1 = every member
 *                   byte-pair encoded. No other value exists.
 *     0x04  "QDA0"  the signature, four bytes in
 *     0x08  u32 LE  number of directory entries
 *     0x0c  u32 LE  reserved; MUST be zero
 *     0x10  ...     unused to 0x100
 *
 *   directory entry, 0x10c bytes, at 0x100 + index * 0x10c:
 *     0x00  u32 LE  data offset, from the start of the archive
 *     0x04  u32 LE  compressed size
 *     0x08  u32 LE  uncompressed size
 *     0x0c  char[0x100]  file name, NUL terminated inside the field
 *
 * The entry stores BOTH sizes but which one is the stream's length is
 * decided by the archive's flag, not by the entry: a stored archive's
 * members occupy uncompressed_size bytes and its compressed_size field is
 * not meaningful. Reading the entry's compressed size unconditionally is the
 * mistake this container invites.
 *
 * Payloads live past the directory and are not required to be in order or
 * contiguous, so the archive's extent is the directory's end pushed out by
 * the furthest member end.
 *
 * Methods: 0 = stored, 1 = QDA byte-pair encoding. The flag is archive wide,
 * so every member carries the same one; it is published per member unchanged
 * so a listing shows what the archive actually says.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qda/xx_qda.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/qda/xx_qda.h"

#include <stdio.h>

#define XX_QDA_COPY_CHUNK (64 * 1024)

typedef struct xx_qda_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_qda_member;

typedef struct xx_qda_stream_s {
    xx_qda_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_qda_stream;

static void xx_qda_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_qda_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_qda_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_qda_path_safe(const char *name) {
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

static void xx_qda_stream_free(void *pointer) {
    xx_qda_stream *stream = (xx_qda_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_qda_add(xx_qda_stream *stream,
                          const xx_qda_member *member) {
    xx_qda_member *grown = (xx_qda_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_QDA_HEADER_SIZE 0x100
#define XX_QDA_ENTRY_SIZE 0x10c
#define XX_QDA_NAME_OFFSET 0x0c
#define XX_QDA_NAME_SIZE 0x100
#define XX_QDA_MAX_MEMBERS 100000
#define XX_QDA_MAX_DECODED ((int64_t)0x10000000)
#define XX_QDA_METHOD_STORED 0U
#define XX_QDA_METHOD_PACKED 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_qda_le32(const uint8_t *data);
static xx_qda_stream *xx_qda_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_qda_decode(Abstractformat *self, const xx_qda_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The archive-wide flag, published per member unchanged. */

static uint32_t xx_qda_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_qda_stream *xx_qda_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_qda_stream *stream = NULL;
    uint8_t header[0x10];
    uint8_t entry[XX_QDA_ENTRY_SIZE];
    char name_buffer[XX_QDA_NAME_SIZE + 1];
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t index;
    int64_t archive_size;
    uint32_t flag;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_QDA_HEADER_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_qda_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* The signature sits four bytes in, behind the flag word, which is why
     * it is easy to misread as being at offset 0. */
    if (header[4] != (uint8_t)'Q' || header[5] != (uint8_t)'D' ||
        header[6] != (uint8_t)'A' || header[7] != (uint8_t)'0') {
        return NULL;
    }

    /* Four printable bytes are a weak signature on their own, so the two
     * words flanking them carry the rest of the detection: the flag is a
     * boolean stored in 32 bits and the word at 0x0c is always zero. Those
     * are the checks a later reader will be tempted to drop, and dropping
     * them makes any file containing the text "QDA0" at offset 4 match. */
    flag = xx_qda_le32(header);
    if (flag > 1U) return NULL;
    if (xx_qda_le32(header + 0x0c) != 0U) return NULL;

    /* The count is read as a signed 32-bit value by the reference, so the
     * top bit set is nonsense rather than a two-billion-entry directory. */
    if (xx_qda_le32(header + 8) > 0x7fffffffU) return NULL;
    count = (int64_t)xx_qda_le32(header + 8);
    /* Zero entries is reported as "not a QDA", not as an empty archive:
     * nothing else in the header is strong enough to accept on alone. */
    if (count < 1 || count > XX_QDA_MAX_MEMBERS) return NULL;

    archive_size = (int64_t)XX_QDA_HEADER_SIZE + count * XX_QDA_ENTRY_SIZE;
    /* The whole directory must be present. A count that runs past EOF is the
     * cheapest way a false positive shows itself. */
    if (archive_size > span) return NULL;

    stream = (xx_qda_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0; index < count; ++index) {
        xx_qda_member member;
        int64_t entry_offset;
        int64_t data_offset;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t stream_size;
        size_t name_length = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry_offset = (int64_t)XX_QDA_HEADER_SIZE +
                       index * (int64_t)XX_QDA_ENTRY_SIZE;
        if (!xx_qda_read_at(self, self->base_address + entry_offset, entry,
                            sizeof(entry))) {
            goto fail;
        }

        /* All three are written as u32 but read back as signed by the
         * reference, so the top bit set is a malformed entry. */
        if (xx_qda_le32(entry) > 0x7fffffffU) goto fail;
        if (xx_qda_le32(entry + 4) > 0x7fffffffU) goto fail;
        if (xx_qda_le32(entry + 8) > 0x7fffffffU) goto fail;
        data_offset = (int64_t)xx_qda_le32(entry);
        compressed_size = (int64_t)xx_qda_le32(entry + 4);
        uncompressed_size = (int64_t)xx_qda_le32(entry + 8);
        if (uncompressed_size > XX_QDA_MAX_DECODED) goto fail;

        /* The stream's length comes from the ARCHIVE's flag: in a stored
         * archive the compressed-size field is not filled in at all. */
        stream_size = (flag == XX_QDA_METHOD_PACKED) ? compressed_size
                                                     : uncompressed_size;
        /* A member whose extent leaves the file is a rejection, not a
         * truncation: the directory is authoritative and a directory that
         * points outside the archive was never a directory. */
        if (!xx_qda_range_within(span, data_offset, stream_size)) goto fail;

        while (name_length < (size_t)XX_QDA_NAME_SIZE &&
               entry[XX_QDA_NAME_OFFSET + name_length] != 0U) {
            uint8_t byte = entry[XX_QDA_NAME_OFFSET + name_length];

            /* Control bytes never appear in a name field that a DOS
             * archiver wrote; finding one means the 0x10c-byte stride has
             * drifted and the "entries" are being carved out of payload.
             * Bytes above 0x7e are accepted: the names are DOS OEM text and
             * accented characters are ordinary in them. */
            if (byte < 0x20U || byte == 0x7fU) goto fail;
            name_buffer[name_length] = (char)byte;
            ++name_length;
        }
        name_buffer[name_length] = '\0';

        xx_mem_zero(&member, sizeof(member));
        if (name_length == 0U) {
            /* An all-NUL name field is structurally fine and does happen;
             * the ordinal keeps such members addressable and distinct. */
            if (xx_rt_snprintf(name_buffer, sizeof(name_buffer),
                               "record%lld", (long long)(index + 1)) <= 0) {
                goto fail;
            }
        }
        member.name = xx_str_dup(name_buffer);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + entry_offset;
        member.header_size = XX_QDA_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = stream_size;
        member.uncompressed_size = uncompressed_size;
        member.method = flag;
        member.timestamp = 0U;
        member.is_folder = false;
        if (!xx_qda_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        /* Payloads are not required to follow the directory in order, so
         * the extent is whichever member ends last. */
        if (data_offset + stream_size > archive_size) {
            archive_size = data_offset + stream_size;
        }
    }

    if (pd && xx_pd_is_stopped(pd)) goto fail;
    stream->archive_size = archive_size;
    return stream;

fail:
    xx_qda_stream_free(stream);
    return NULL;
}


/* Stored and packed members are both read the same way; only what happens to
 * the bytes afterwards differs. */
static bool xx_qda_decode(Abstractformat *self, const xx_qda_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The flag is archive wide and parse rejects anything above 1, but the
     * switch is repeated here: were the flag check ever loosened, routing an
     * unknown method through either branch would produce plausible-looking
     * garbage rather than a refusal. */
    if (member->method != XX_QDA_METHOD_STORED &&
        member->method != XX_QDA_METHOD_PACKED) {
        return false;
    }
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > XX_QDA_MAX_DECODED) return false;
    if (member->compressed_size > XX_QDA_MAX_DECODED) return false;
    /* A stored member's stream IS its plaintext; a disagreement means the
     * member was not built by this parse. */
    if (member->method == XX_QDA_METHOD_STORED &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }

    /* A zero-length member is legal: the entry simply describes an empty
     * file. Allocate one byte so the caller always gets a freeable block. */
    output = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!output) return false;
    if (member->uncompressed_size == 0) {
        *out = output;
        *out_size = 0U;
        return true;
    }

    input = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!input) {
        xx_mem_free(output);
        return false;
    }
    if (member->compressed_size != 0 &&
        !xx_qda_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }

    if (member->method == XX_QDA_METHOD_STORED) {
        xx_rt_memcpy(output, input, (size_t)member->uncompressed_size);
    } else if (!xx_qda_decode_memory(input, (size_t)member->compressed_size,
                                     output,
                                     (size_t)member->uncompressed_size,
                                     &written) ||
               written != (size_t)member->uncompressed_size) {
        /* The BPE decoder stops at the capacity it was given, so a stream
         * that would expand further returns a short count rather than
         * overrunning; both that and a stream running dry early land here. */
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_qda_init(xx_qda *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_QDA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-qda");
    xx_format_set_extension(&archive->format, "qda");
    archive->format.check_is_valid = xx_qda_check_is_valid;
    archive->format.handle_base_info = xx_qda_handle_base_info;
    archive->format.get_format_size = xx_qda_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_qda_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_qda_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_qda_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_qda_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_qda_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_qda_free_archive_records_reading;
    archive->format.destroy = xx_qda_vtable_destroy;
}

xx_qda *xx_qda_create(xx_io_device *device, int64_t base_address) {
    xx_qda *archive = (xx_qda *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_qda_init(archive, device, base_address);
    return archive;
}

void xx_qda_destroy(xx_qda *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_qda_free(xx_qda *archive) {
    if (!archive) return;
    xx_qda_destroy(archive);
    xx_mem_free(archive);
}

static void xx_qda_vtable_destroy(Abstractformat *self) {
    xx_qda_destroy((xx_qda *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_qda_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_qda_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_qda_parse(self, pd);
    if (!stream) return false;
    xx_qda_stream_free(stream);
    return true;
}

bool xx_qda_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_qda *archive = (xx_qda *)self;
    xx_qda_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_qda_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_qda_stream_free(stream);
    return true;
}

int64_t xx_qda_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_qda_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_qda *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_qda_set_record(xx_archive_record *record,
                                 const xx_qda_member *member) {
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

static bool xx_qda_copy_options(xx_list_s *target,
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

static const xx_var *xx_qda_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_qda_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_qda_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_qda_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_qda_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_qda_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_qda_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_qda_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_qda_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qda_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_qda_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_qda_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_qda_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_qda_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_qda_stream *stream;
    const xx_qda_member *member;
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
    stream = (xx_qda_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_qda_path_safe(member->name)) return false;

    path_option = xx_qda_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_qda_decode(self, member, &plain, &plain_size, pd);
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
        !xx_qda_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_qda_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
