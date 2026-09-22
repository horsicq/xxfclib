/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ChiefLZ "Single" archives.
 *
 *   header, 0x101 bytes at offset 0:
 *     0x00  u8       8, the length of the magic that follows
 *     0x01  8 bytes  "aChiefM#"
 *     0x09  2 bytes  unconstrained
 *     0x0b  u32 LE   uncompressed size of the member
 *     0x0f  4 bytes  unconstrained
 *     0x13  u16 LE   DOS date
 *     0x15  u16 LE   DOS time
 *     0x17  4 bytes  unconstrained
 *     0x1b  u32 LE   CRC32 of the decoded bytes
 *     0x28  u8       name length, then that many name bytes
 *     0xad  u8       compression method; only 4 is defined
 *     ...   padding to 0x101
 *
 *   payload: 0x101 to end-of-file, one ChiefLZ method 4 stream
 *     (MSB-first adaptive Huffman over a 629 symbol alphabet plus LZ77).
 *
 * The container holds exactly one member and stores no compressed length:
 * the payload simply runs to end-of-file. The plaintext length in the header
 * is therefore the only figure the decoder can be held to, which is why
 * decode insists the codec produce exactly that many bytes.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/chieflz/xx_chieflz.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/chieflz/xx_chieflz.h"

#include <stdio.h>

#define XX_CHIEFLZ_COPY_CHUNK (64 * 1024)

typedef struct xx_chieflz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_chieflz_member;

typedef struct xx_chieflz_stream_s {
    xx_chieflz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_chieflz_stream;

static void xx_chieflz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_chieflz_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_chieflz_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_chieflz_path_safe(const char *name) {
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

static void xx_chieflz_stream_free(void *pointer) {
    xx_chieflz_stream *stream = (xx_chieflz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_chieflz_add(xx_chieflz_stream *stream,
                          const xx_chieflz_member *member) {
    xx_chieflz_member *grown = (xx_chieflz_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_CHIEFLZ_FALLBACK_NAME "data"
#define XX_CHIEFLZ_HEADER_SIZE 0x101
#define XX_CHIEFLZ_NAME_OFFSET 0x28
#define XX_CHIEFLZ_METHOD_OFFSET 0xad
#define XX_CHIEFLZ_MAX_MEMBERS 1
#define XX_CHIEFLZ_METHOD_LZ 4U
#define XX_CHIEFLZ_MAX_DECODED ((int64_t)0x10000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_chieflz_le16(const uint8_t *data);
static uint32_t xx_chieflz_le32(const uint8_t *data);
static xx_chieflz_stream *xx_chieflz_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_chieflz_decode(Abstractformat *self, const xx_chieflz_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Used when the header carries no name; the container has no other place to
 * keep one, so a fixed placeholder is the honest answer. */

static uint16_t xx_chieflz_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_chieflz_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_chieflz_stream *xx_chieflz_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    static const uint8_t magic[9] = {8U, (uint8_t)'a', (uint8_t)'C',
                                     (uint8_t)'h', (uint8_t)'i', (uint8_t)'e',
                                     (uint8_t)'f', (uint8_t)'M', (uint8_t)'#'};
    xx_chieflz_stream *stream;
    xx_chieflz_member member;
    uint8_t header[XX_CHIEFLZ_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t raw_size;
    uint32_t name_length;
    uint32_t index;
    uint16_t dos_date;
    uint16_t dos_time;
    char *name = NULL;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The payload runs to EOF, so a file that is only the header carries no
     * member at all. */
    if (span <= (int64_t)XX_CHIEFLZ_HEADER_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_chieflz_read_at(self, self->base_address, header,
                            sizeof(header))) {
        return NULL;
    }

    /* The nine-byte length-prefixed magic is the format's primary defence:
     * the leading 8 is the length of "aChiefM#" and both halves must agree.
     * Testing only the eight text bytes would match any container that
     * happens to embed that string at offset 1. */
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;

    /* Second defence, and the one most likely to be loosened later: method 4
     * is the sole method the format defines. A header whose method byte says
     * anything else is not a ChiefLZ Single archive, and accepting it would
     * publish a member the decoder cannot produce. */
    if (header[XX_CHIEFLZ_METHOD_OFFSET] != (uint8_t)XX_CHIEFLZ_METHOD_LZ) {
        return NULL;
    }

    /* The size is written as a 32-bit word but the reference treats it as
     * signed and rejects negatives, so the top bit set is a rejection here
     * too rather than a four-gigabyte member. */
    raw_size = xx_chieflz_le32(header + 0x0b);
    if (raw_size > 0x7fffffffU) return NULL;
    uncompressed_size = (int64_t)raw_size;
    if (uncompressed_size <= 0) return NULL;
    if (uncompressed_size > XX_CHIEFLZ_MAX_DECODED) return NULL;

    dos_date = xx_chieflz_le16(header + 0x13);
    dos_time = xx_chieflz_le16(header + 0x15);

    /* The name is length-prefixed, not NUL terminated, and the prefix must
     * leave the text inside the fixed header. */
    name_length = (uint32_t)header[XX_CHIEFLZ_NAME_OFFSET];
    if ((int64_t)XX_CHIEFLZ_NAME_OFFSET + 1 + (int64_t)name_length >
        (int64_t)XX_CHIEFLZ_HEADER_SIZE) {
        name_length = 0U;
    }
    for (index = 0U; index < name_length; ++index) {
        /* Control bytes are a rejection, not a trim: a header that reaches
         * this point with a control byte in the name field is structurally
         * wrong, not merely oddly named. Bytes above 0x7e are accepted
         * because the format stores DOS OEM names, where accented characters
         * are ordinary and common. */
        if (header[XX_CHIEFLZ_NAME_OFFSET + 1U + index] < 0x20U) return NULL;
    }

    compressed_size = span - (int64_t)XX_CHIEFLZ_HEADER_SIZE;

    stream = (xx_chieflz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    if (!xx_chieflz_range_within(span, (int64_t)XX_CHIEFLZ_HEADER_SIZE,
                                 compressed_size)) {
        goto fail;
    }
    if (stream->count >= (size_t)XX_CHIEFLZ_MAX_MEMBERS) goto fail;

    if (name_length > 0U) {
        name = (char *)xx_mem_alloc((size_t)name_length + 1U);
        if (!name) goto fail;
        xx_rt_memcpy(name, header + XX_CHIEFLZ_NAME_OFFSET + 1,
                     (size_t)name_length);
        name[name_length] = '\0';
    } else {
        name = xx_str_dup(XX_CHIEFLZ_FALLBACK_NAME);
        if (!name) goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = (int64_t)XX_CHIEFLZ_HEADER_SIZE;
    member.data_offset = self->base_address + (int64_t)XX_CHIEFLZ_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    member.method = XX_CHIEFLZ_METHOD_LZ;
    member.timestamp = ((uint64_t)dos_date << 16) | (uint64_t)dos_time;
    member.is_folder = false;

    if (!xx_chieflz_add(stream, &member)) goto fail;
    name = NULL;

    stream->archive_size = span;
    return stream;

fail:
    if (name) xx_str_free(name);
    xx_chieflz_stream_free(stream);
    return NULL;
}



/* One member, always. The cap the briefing asks for is a constant here. */

/* The container's own method number, published unchanged in member->method.
 * 4 is the only value the format defines and the only one the header check
 * accepts, but decode re-tests it so that a future relaxation of the header
 * check cannot silently route an unknown method through the ChiefLZ codec. */

/* The uncompressed size is attacker-controlled; refuse rather than attempt an
 * allocation that large. */

/* A single ChiefLZ method 4 stream. The codec is output-driven and returns
 * true only when it produced exactly output_size bytes, so a stream that runs
 * dry early is reported as failure rather than as a short buffer. */
static bool xx_chieflz_decode(Abstractformat *self,
                              const xx_chieflz_member *member, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* Anything but method 4 must fail: treating an unimplemented method as
     * stored would hand the caller compressed bytes dressed as plaintext. */
    if (member->method != XX_CHIEFLZ_METHOD_LZ) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_CHIEFLZ_MAX_DECODED ||
        member->uncompressed_size > XX_CHIEFLZ_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_chieflz_read_at(self, member->data_offset, input,
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
    if (!xx_chieflz_decode_memory(input, (size_t)member->compressed_size,
                                  output, (size_t)member->uncompressed_size,
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

void xx_chieflz_init(xx_chieflz *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_CHIEFLZ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-chieflz");
    xx_format_set_extension(&archive->format, "pk_");
    archive->format.check_is_valid = xx_chieflz_check_is_valid;
    archive->format.handle_base_info = xx_chieflz_handle_base_info;
    archive->format.get_format_size = xx_chieflz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_chieflz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_chieflz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_chieflz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_chieflz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_chieflz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_chieflz_free_archive_records_reading;
    archive->format.destroy = xx_chieflz_vtable_destroy;
}

xx_chieflz *xx_chieflz_create(xx_io_device *device, int64_t base_address) {
    xx_chieflz *archive = (xx_chieflz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_chieflz_init(archive, device, base_address);
    return archive;
}

void xx_chieflz_destroy(xx_chieflz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_chieflz_free(xx_chieflz *archive) {
    if (!archive) return;
    xx_chieflz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_chieflz_vtable_destroy(Abstractformat *self) {
    xx_chieflz_destroy((xx_chieflz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_chieflz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_chieflz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_chieflz_parse(self, pd);
    if (!stream) return false;
    xx_chieflz_stream_free(stream);
    return true;
}

bool xx_chieflz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_chieflz *archive = (xx_chieflz *)self;
    xx_chieflz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_chieflz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_chieflz_stream_free(stream);
    return true;
}

int64_t xx_chieflz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_chieflz_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_chieflz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_chieflz_set_record(xx_archive_record *record,
                                 const xx_chieflz_member *member) {
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

static bool xx_chieflz_copy_options(xx_list_s *target,
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

static const xx_var *xx_chieflz_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_chieflz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_chieflz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_chieflz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_chieflz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_chieflz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_chieflz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_chieflz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_chieflz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_chieflz_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_chieflz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_chieflz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_chieflz_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_chieflz_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_chieflz_stream *stream;
    const xx_chieflz_member *member;
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
    stream = (xx_chieflz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_chieflz_path_safe(member->name)) return false;

    path_option = xx_chieflz_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_chieflz_decode(self, member, &plain, &plain_size, pd);
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
        !xx_chieflz_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_chieflz_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
