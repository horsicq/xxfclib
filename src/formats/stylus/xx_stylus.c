/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Stylus dictionary containers.
 *
 *   0x00  'D' 'P' 0x1a 0x07     magic; the 0x1a/0x07 pair is part of it
 *   0x04  u16 LE container version, always 1
 *   0x06  u8  stream kind, always 3
 *   0x07  three bytes "SDC"     payload tag, compared CASE-INSENSITIVELY,
 *                               which is what the reference implementation
 *                               does; lowercase "sdc" occurs in the wild
 *   0x0a  two bytes             not interpreted by the reference
 *   0x0c  u32 LE CRC-32 of the plaintext (the standard 0xedb88320 variant)
 *   0x10  the SDC stream, running to end-of-file
 *
 * There is ONE member, no member table, no name and no timestamp. The packed
 * size is simply the file size minus sixteen.
 *
 * THE CONTAINER NEVER STORES THE PLAINTEXT LENGTH. The only way to learn it
 * is to run the codec, so parse calls xx_stylus_scan_memory() over the whole
 * payload; the same core then decodes it, so the measurement and the decode
 * can never disagree.
 *
 * The codec is XOR-0xB5 obfuscated LZSS over a zero-filled 4 KiB ring with a
 * +18 match bias, and it has no end marker: the stream ends when the input
 * runs out.
 *
 * DIVERGENCE FROM THE REFERENCE, stated plainly: the reference publishes a
 * member with an UNKNOWN (-1) plaintext size when the payload exceeds its
 * 64 MiB measuring cap. This reader's member contract has no "unknown size"
 * value - every member carries a concrete uncompressed_size - so a payload
 * above the same 64 MiB cap is rejected outright rather than published with
 * an invented length.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stylus/xx_stylus.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/stylus/xx_stylus.h"

#include <stdio.h>

#define XX_STYLUS_COPY_CHUNK (64 * 1024)

typedef struct xx_stylus_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_stylus_member;

typedef struct xx_stylus_stream_s {
    xx_stylus_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_stylus_stream;

static void xx_stylus_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_stylus_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_stylus_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_stylus_path_safe(const char *name) {
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

static void xx_stylus_stream_free(void *pointer) {
    xx_stylus_stream *stream = (xx_stylus_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_stylus_add(xx_stylus_stream *stream,
                          const xx_stylus_member *member) {
    xx_stylus_member *grown = (xx_stylus_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_STYLUS_HEADER_SIZE 16
#define XX_STYLUS_CRC32_OFFSET 0x0c
#define XX_STYLUS_MAX_MEMBERS 1
#define XX_STYLUS_MAX_MEASURE_SIZE ((int64_t)0x4000000)
#define XX_STYLUS_MEMBER_NAME "stylus_data.sdc"
#define XX_STYLUS_KIND_SDC 3U
#define XX_STYLUS_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_stylus_le16(const uint8_t *data);
static uint8_t xx_stylus_to_lower(uint8_t character);
static xx_stylus_stream *xx_stylus_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_stylus_decode(Abstractformat *self, const xx_stylus_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Exactly one member; the cap exists only for shape. */

/* The reference's measuring cap. Beyond it the reference publishes an unknown
 * plaintext size; this reader has no way to say "unknown", so the cap becomes
 * a rejection instead. See the DIVERGENCE note in the file comment. */

/* The base name is stored nowhere in the container - the reference takes it
 * from the device's file name - so this placeholder plus the format's own
 * extension is the only truthful thing to publish. */

static uint16_t xx_stylus_le16(const uint8_t *data) {
    return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

static uint8_t xx_stylus_to_lower(uint8_t character) {
    return (character >= 'A' && character <= 'Z')
               ? (uint8_t)(character + 0x20U)
               : character;
}

static xx_stylus_stream *xx_stylus_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_stylus_stream *stream;
    xx_stylus_member member;
    uint8_t header[XX_STYLUS_HEADER_SIZE];
    uint8_t *packed;
    char *name;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    size_t produced = 0U;
    size_t consumed = 0U;
    bool measured;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A header with no payload behind it describes nothing. */
    if (span <= XX_STYLUS_HEADER_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_stylus_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* Four bytes of magic. The 0x1a/0x07 pair is as much a part of it as the
     * two letters - "DP" alone is two bytes of ASCII and would match
     * constantly. */
    if (header[0] != 'D' || header[1] != 'P' || header[2] != 0x1aU ||
        header[3] != 0x07U) {
        return NULL;
    }
    /* The version and the stream-kind byte are both fixed constants in this
     * format. Together with the payload tag they are what turns four bytes of
     * magic into a ten-byte invariant, and they are the checks a later reader
     * will be tempted to loosen as "probably just informational". There is no
     * member table, no length field and no terminator behind them - if these
     * go, the format has no false-positive defence left except the CRC, which
     * is not checked until extraction. */
    if (xx_stylus_le16(header + 4) != 1U) return NULL;
    if (header[6] != (uint8_t)XX_STYLUS_KIND_SDC) return NULL;
    /* Case-insensitive on purpose: the reference compares the tag that way
     * and lowercase "sdc" occurs in the wild. */
    if (xx_stylus_to_lower(header[7]) != 's' ||
        xx_stylus_to_lower(header[8]) != 'd' ||
        xx_stylus_to_lower(header[9]) != 'c') {
        return NULL;
    }

    compressed_size = span - XX_STYLUS_HEADER_SIZE;
    if (!xx_stylus_range_within(span, XX_STYLUS_HEADER_SIZE,
                                compressed_size)) {
        return NULL;
    }
    /* See the DIVERGENCE note: measuring is mandatory here, so the
     * reference's measuring cap becomes this reader's size limit. */
    if (compressed_size > XX_STYLUS_MAX_MEASURE_SIZE) return NULL;

    packed = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
    if (!packed) return NULL;
    if (!xx_stylus_read_at(self, self->base_address + XX_STYLUS_HEADER_SIZE,
                           packed, (size_t)compressed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return NULL;
    }
    /* The plaintext length is stored nowhere, so it has to be measured. The
     * scan runs the very same core as the decode with the output discarded,
     * so a member that measures here is a member that decodes. A payload that
     * does NOT measure is not a Stylus stream, whatever the header says -
     * this is the format's second line of defence. */
    measured = xx_stylus_scan_memory(packed, (size_t)compressed_size,
                                     (size_t)XX_STYLUS_MAX_DECODED, &consumed,
                                     &produced);
    xx_mem_free(packed);
    if (!measured) return NULL;
    /* The format has no end marker: the stream ends when the input runs out,
     * so a scan that stopped early means trailing bytes that belong to no
     * stream. */
    if (consumed != (size_t)compressed_size) return NULL;
    if (produced == 0U || (int64_t)produced > XX_STYLUS_MAX_DECODED) {
        return NULL;
    }

    stream = (xx_stylus_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(XX_STYLUS_MEMBER_NAME);
    if (!name) goto fail;
    if (!xx_stylus_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_STYLUS_HEADER_SIZE;
    member.data_offset = self->base_address + XX_STYLUS_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = (int64_t)produced;
    member.method = XX_STYLUS_KIND_SDC;
    /* The container carries a plaintext CRC-32 at +0x0c but no timestamp of
     * any kind; reporting anything but zero would be an invention. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_stylus_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_stylus_stream_free(stream);
    return NULL;
}


/* The container's own stream-kind byte at +0x06. It is the closest thing the
 * format has to a method field, so it is published unchanged and decode
 * refuses anything else: silently treating an unknown kind as stored would
 * hand the caller XORed LZSS codes and call them a dictionary. */

/* Both the scan ceiling in parse and the allocation cap here, so the two can
 * never disagree about what is too large. */

static bool xx_stylus_decode(Abstractformat *self,
                             const xx_stylus_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_STYLUS_KIND_SDC) return false;
    if (member->compressed_size < 1 ||
        member->compressed_size > XX_STYLUS_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_STYLUS_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_stylus_read_at(self, member->data_offset, input,
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
    /* The stream has no end marker, so the measured length IS the stop
     * condition: xx_stylus_decode_memory() succeeds only when it filled the
     * buffer exactly, which is what keeps a truncated payload from being
     * reported as a short but successful member. */
    if (!xx_stylus_decode_memory(input, (size_t)member->compressed_size,
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

void xx_stylus_init(xx_stylus *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_STYLUS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stylus-dictionary");
    xx_format_set_extension(&archive->format, "sdc");
    archive->format.check_is_valid = xx_stylus_check_is_valid;
    archive->format.handle_base_info = xx_stylus_handle_base_info;
    archive->format.get_format_size = xx_stylus_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_stylus_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_stylus_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_stylus_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_stylus_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_stylus_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_stylus_free_archive_records_reading;
    archive->format.destroy = xx_stylus_vtable_destroy;
}

xx_stylus *xx_stylus_create(xx_io_device *device, int64_t base_address) {
    xx_stylus *archive = (xx_stylus *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_stylus_init(archive, device, base_address);
    return archive;
}

void xx_stylus_destroy(xx_stylus *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_stylus_free(xx_stylus *archive) {
    if (!archive) return;
    xx_stylus_destroy(archive);
    xx_mem_free(archive);
}

static void xx_stylus_vtable_destroy(Abstractformat *self) {
    xx_stylus_destroy((xx_stylus *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_stylus_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_stylus_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_stylus_parse(self, pd);
    if (!stream) return false;
    xx_stylus_stream_free(stream);
    return true;
}

bool xx_stylus_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_stylus *archive = (xx_stylus *)self;
    xx_stylus_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_stylus_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_stylus_stream_free(stream);
    return true;
}

int64_t xx_stylus_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_stylus_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_stylus *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_stylus_set_record(xx_archive_record *record,
                                 const xx_stylus_member *member) {
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

static bool xx_stylus_copy_options(xx_list_s *target,
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

static const xx_var *xx_stylus_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_stylus_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_stylus_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_stylus_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_stylus_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_stylus_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_stylus_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_stylus_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_stylus_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_stylus_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_stylus_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_stylus_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_stylus_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_stylus_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_stylus_stream *stream;
    const xx_stylus_member *member;
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
    stream = (xx_stylus_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_stylus_path_safe(member->name)) return false;

    path_option = xx_stylus_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_stylus_decode(self, member, &plain, &plain_size, pd);
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
        !xx_stylus_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_stylus_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
