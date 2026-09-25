/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TRC (TRCZip) archives.
 *
 *   header, 0x132 bytes, little-endian throughout:
 *     0x000  magic, 12 bytes: B0 B1 B2 'T' 'R' 'C' 'Z' 'i' 'p' B2 B1 B0
 *            the guard bytes are a palindrome around the ASCII tag
 *     0x00c  u16 version, must be 0x0210
 *     0x00e  u32 CRC of the decoded member
 *     0x012  u8  '*', a literal separator byte
 *     0x013  unused up to 0x032
 *     0x032  name, 13 bytes, DOS 8.3, NUL terminated, must not be empty
 *     0x043  i32 uncompressed size
 *     0x047  i32 compressed size
 *     0x04b  "!?TOMC?!", 8 bytes - a second magic in the middle of the
 *            header ("TOMC" is "CMOT" reversed, matching the house style
 *            of the outer palindrome)
 *     0x053  unused
 *     0x057  u32 second copy of the CRC at 0x00e, must agree
 *     0x05b  unused
 *     0x09f  i32 second copy of the uncompressed size at 0x043, must agree
 *     0x0a3  unused to 0x132
 *
 *   0x132  compressed payload, `compressed size` bytes
 *
 * The container holds exactly ONE member - there is no directory and no
 * count field - and that member is always PKWARE DCL ("implode") compressed;
 * no method field exists to say otherwise.
 *
 * False positives are cheap to rule out here: two independent magics 0x4b
 * bytes apart, a fixed version word, a literal '*' at 0x012, and two fields
 * that are each stored twice and must agree. Random data passing all of
 * those is not a realistic accident, which is why none of them should be
 * relaxed to "recover" a damaged file.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/trc/xx_trc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_TRC_COPY_CHUNK (64 * 1024)

typedef struct xx_trc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_trc_member;

typedef struct xx_trc_stream_s {
    xx_trc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_trc_stream;

static void xx_trc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_trc_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_trc_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_trc_path_safe(const char *name) {
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

static void xx_trc_stream_free(void *pointer) {
    xx_trc_stream *stream = (xx_trc_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_trc_add(xx_trc_stream *stream,
                          const xx_trc_member *member) {
    xx_trc_member *grown = (xx_trc_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_TRC_HEADER_SIZE 0x132
#define XX_TRC_NAME_OFFSET 0x32
#define XX_TRC_NAME_SIZE 13
#define XX_TRC_MAX_MEMBERS 1
#define XX_TRC_MAX_DECODED (256 * 1024 * 1024)
#define XX_TRC_VERSION 0x0210

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_trc_le16(const uint8_t *data);
static uint32_t xx_trc_le32(const uint8_t *data);
static xx_trc_stream *xx_trc_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_trc_decode(Abstractformat *self, const xx_trc_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* XX_TRC_VERSION is defined above, with the decode that switches on it. */
/* The container holds exactly one member; the cap exists so the shape of
 * this reader matches the others, not because a directory could grow. */

static uint16_t xx_trc_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_trc_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_trc_stream *xx_trc_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic[12] = {0xb0, 0xb1, 0xb2, 'T', 'R', 'C', 'Z',
                                      'i', 'p', 0xb2, 0xb1, 0xb0};
    static const uint8_t inner_magic[8] = {'!', '?', 'T', 'O', 'M', 'C',
                                           '?', '!'};
    xx_trc_stream *stream = NULL;
    xx_trc_member member;
    uint8_t header[XX_TRC_HEADER_SIZE];
    char *name;
    size_t name_length = 0U;
    size_t index;
    int64_t total;
    int64_t span;
    int64_t compressed;
    int64_t uncompressed;
    uint32_t crc;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_TRC_HEADER_SIZE) return NULL;
    if (!xx_trc_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* The magic is a palindrome of guard bytes around "TRCZip"; the trailing
     * B2 B1 B0 is as much a part of it as the leading one. */
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;
    if (xx_trc_le16(header + 0x0c) != XX_TRC_VERSION) return NULL;
    /* A literal '*' separator - cheap, but it is a fixed byte and costs a
     * further 1-in-256 to hit by chance. */
    if (header[0x12] != (uint8_t)'*') return NULL;
    /* The second magic, 0x4b bytes into the header. Together with the first
     * one this is the format's real defence against a false positive: a
     * random 0x132 byte block matching twelve bytes at 0x00 and eight more at
     * 0x4b does not happen. Do not relax either to "repair" a damaged file. */
    if (xx_rt_memcmp(header + 0x4b, inner_magic, sizeof(inner_magic)) != 0) {
        return NULL;
    }

    /* The CRC and the uncompressed size are each written twice, far apart.
     * Both copies must agree - a mismatch means this is not a TRC header. */
    crc = xx_trc_le32(header + 0x0e);
    if (xx_trc_le32(header + 0x57) != crc) return NULL;

    /* Signed on purpose: these are i32 fields, and a top-bit-set value is a
     * corrupt field rather than a two-gigabyte member. */
    uncompressed = (int64_t)(int32_t)xx_trc_le32(header + 0x43);
    compressed = (int64_t)(int32_t)xx_trc_le32(header + 0x47);
    if (uncompressed < 0 || compressed < 0) return NULL;
    if ((int64_t)(int32_t)xx_trc_le32(header + 0x9f) != uncompressed) {
        return NULL;
    }
    /* A payload running past EOF is a rejection, not a truncated read. */
    if (!xx_trc_range_within(span, XX_TRC_HEADER_SIZE, compressed)) {
        return NULL;
    }

    /* A DOS 8.3 name in a 13 byte field: it must be NUL terminated inside the
     * field and must not be empty. A field filled edge to edge with text is
     * the signature of misparsed data. */
    while (name_length < (size_t)XX_TRC_NAME_SIZE &&
           header[XX_TRC_NAME_OFFSET + name_length] != 0U) {
        uint8_t byte = header[XX_TRC_NAME_OFFSET + name_length];
        /* The field is a plain DOS filename; nothing outside printable ASCII
         * belongs in it. */
        if (byte < 0x20U || byte > 0x7EU) return NULL;
        ++name_length;
    }
    if (name_length == 0U || name_length >= (size_t)XX_TRC_NAME_SIZE) {
        return NULL;
    }

    stream = (xx_trc_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    if (pd && xx_pd_is_stopped(pd)) goto fail;

    name = (char *)xx_mem_alloc(name_length + 1U);
    if (!name) goto fail;
    for (index = 0U; index < name_length; ++index) {
        name[index] = (char)header[XX_TRC_NAME_OFFSET + index];
    }
    name[name_length] = '\0';
    if (!xx_trc_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_TRC_HEADER_SIZE;
    member.data_offset = self->base_address + XX_TRC_HEADER_SIZE;
    member.compressed_size = compressed;
    member.uncompressed_size = uncompressed;
    /* No per-member method field exists; the version word is the only value
     * the container itself supplies, and decode maps it to DCL implode. */
    member.method = (uint32_t)XX_TRC_VERSION;
    if (!xx_trc_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    if (stream->count != (size_t)XX_TRC_MAX_MEMBERS) goto fail;

    /* The archive ends where its one payload ends, not at EOF: anything after
     * that is an overlay and does not belong to this format. */
    stream->archive_size = XX_TRC_HEADER_SIZE + compressed;
    return stream;

fail:
    xx_trc_stream_free(stream);
    return NULL;
}


/* A container-supplied uncompressed size is attacker controlled; refuse an
 * absurd allocation instead of attempting it. */
/* Declared here rather than with the parse constants below because decode is
 * emitted first and switches on it. */

/* Every member is PKWARE DCL imploded. The container has no per-member
 * method field, so parse stores the header's version word - the only
 * container-supplied discriminator there is - and this switch is the one
 * place that maps it onto a decoder. */
static bool xx_trc_decode(Abstractformat *self, const xx_trc_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* An unknown version must not silently fall through to "stored": that
     * would hand the caller compressed bytes dressed up as data. */
    if (member->method != XX_TRC_VERSION) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > XX_TRC_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;

    if (member->uncompressed_size == 0) {
        /* An empty member decodes to nothing; there is no stream to run. */
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size == 0) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_trc_read_at(self, member->data_offset, input,
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
    if (!xx_dcl_decode_memory(input, (size_t)member->compressed_size, output,
                              (size_t)member->uncompressed_size, &written)) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(input);
    /* Short output is a failure, never a success with fewer bytes: a caller
     * cannot tell a truncated member from a genuinely small one. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_trc_init(xx_trc *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_TRC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-apricot-image");
    xx_format_set_extension(&archive->format, "trc");
    archive->format.check_is_valid = xx_trc_check_is_valid;
    archive->format.handle_base_info = xx_trc_handle_base_info;
    archive->format.get_format_size = xx_trc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_trc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_trc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_trc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_trc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_trc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_trc_free_archive_records_reading;
    archive->format.destroy = xx_trc_vtable_destroy;
}

xx_trc *xx_trc_create(xx_io_device *device, int64_t base_address) {
    xx_trc *archive = (xx_trc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_trc_init(archive, device, base_address);
    return archive;
}

void xx_trc_destroy(xx_trc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_trc_free(xx_trc *archive) {
    if (!archive) return;
    xx_trc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_trc_vtable_destroy(Abstractformat *self) {
    xx_trc_destroy((xx_trc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_trc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_trc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_trc_parse(self, pd);
    if (!stream) return false;
    xx_trc_stream_free(stream);
    return true;
}

bool xx_trc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_trc *archive = (xx_trc *)self;
    xx_trc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_trc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_trc_stream_free(stream);
    return true;
}

int64_t xx_trc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_trc_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_trc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_trc_set_record(xx_archive_record *record,
                                 const xx_trc_member *member) {
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

static bool xx_trc_copy_options(xx_list_s *target,
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

static const xx_var *xx_trc_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_trc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_trc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_trc_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_trc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_trc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_trc_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_trc_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_trc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_trc_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_trc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_trc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_trc_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_trc_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_trc_stream *stream;
    const xx_trc_member *member;
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
    stream = (xx_trc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_trc_path_safe(member->name)) return false;

    path_option = xx_trc_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_trc_decode(self, member, &plain, &plain_size, pd);
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
        !xx_trc_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_trc_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
