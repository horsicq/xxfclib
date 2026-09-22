/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Stac Electronics SAF archives.
 *
 *   banner, at offset 0: the ASCII text "SAF, (c) ..." in one of exactly two
 *   fixed lengths, terminated by the byte pair 1A 00:
 *     1A 00 at 0x1b -> the banner is 0x1d bytes and the first member
 *                      header starts there
 *     1A 00 at 0x2d -> the banner is 0x2f bytes
 *   Nothing else is accepted; the pair's position IS the version
 *   discrimination, and both variants are read out of the same 0x2f-byte
 *   window, so the file must be at least that long.
 *
 *   member header, 0x23 bytes, immediately followed by the packed bytes:
 *     0x00  char[14] NUL-terminated DOS 8.3 name
 *     0x0e  i32 LE   uncompressed size
 *     0x12  i32 LE   packed size - the next header sits right after these
 *                    many bytes
 *     0x16  u16 LE   DOS date
 *     0x18  u16 LE   DOS time
 *     0x1b  u8       method: 3 means the packed extent is one stream,
 *                    anything else means a chain of length-prefixed chunks
 *
 *   THE DATE COMES BEFORE THE TIME here, which is the other way round from
 *   most DOS containers; the reference notes that reading them the other way
 *   round yields month 0 / day 0 on real members.
 *
 *   There is no central directory, no member count and no end marker: the
 *   chain simply ends when fewer than 0x23 bytes remain.
 *
 * The member codec (LZ77 over a 2 KiB ring with a 9-bit token alphabet) lives
 * in xx_saf_decode_memory_method() and is NOT duplicated here; this file is
 * the container only.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/saf/xx_saf.h"

#include "xxfclib/algo/saf/xx_saf.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* REGISTRATION PENDING.  xxfc_defs.h carries no XX_FILE_TYPE_SAF yet and this
 * port must not edit that shared header.  Delete this block when the enum is
 * added - until then the reader reports itself as plain binary. */

#define XX_SAF_ENTRY_SIZE 0x23
#define XX_SAF_NAME_SIZE 14
#define XX_SAF_SHORT_BANNER 0x1d
#define XX_SAF_LONG_BANNER 0x2f
#define XX_SAF_MAX_MEMBERS 65536
#define XX_SAF_MAX_DECODED ((int64_t)256 * 1024 * 1024)

typedef struct xx_saf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_saf_member;

typedef struct xx_saf_stream_s {
    xx_saf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    int64_t first_member_offset;
} xx_saf_stream;

static void xx_saf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_saf_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_saf_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static bool xx_saf_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_saf_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_saf_path_safe(const char *name) {
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

/* DOS 8.3 names, NUL terminated inside a 14-byte field.  Anything that is not
 * printable ASCII, and anything that could redirect the output path, is a
 * rejection: with a banner as the only magic, a lax name test here is what
 * would let a non-SAF file list members. */
static bool xx_saf_valid_raw_name(const uint8_t *raw) {
    bool terminated = false;
    size_t index;

    if (raw[0] == 0U) return false;
    for (index = 0U; index < (size_t)XX_SAF_NAME_SIZE; ++index) {
        uint8_t character = raw[index];

        if (character == 0U) {
            terminated = true;
            continue;
        }
        /* Filler bytes after the terminator are not part of the name. */
        if (terminated) continue;
        if (character < 0x20U || character > 0x7eU) return false;
        if (character == (uint8_t)'/' || character == (uint8_t)'\\' ||
            character == (uint8_t)':' || character == (uint8_t)'*' ||
            character == (uint8_t)'?' || character == (uint8_t)'"' ||
            character == (uint8_t)'<' || character == (uint8_t)'>' ||
            character == (uint8_t)'|') {
            return false;
        }
    }
    return terminated;
}

static char *xx_saf_raw_name_to_string(const uint8_t *raw) {
    char buffer[XX_SAF_NAME_SIZE + 1];
    size_t length = 0U;

    while (length < (size_t)XX_SAF_NAME_SIZE && raw[length] != 0U) {
        buffer[length] = (char)raw[length];
        ++length;
    }
    buffer[length] = '\0';
    return xx_str_dup(buffer);
}

static void xx_saf_stream_free(void *pointer) {
    xx_saf_stream *stream = (xx_saf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p member->name. */
static bool xx_saf_add(xx_saf_stream *stream, const xx_saf_member *member) {
    xx_saf_member *grown = (xx_saf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- parse -- */

/* @p header_only stops after the banner and the FIRST member header, which is
 * what detection uses: those two together are the whole signature, and a
 * truncated tail somewhere further down should not make the file stop being a
 * SAF archive.  The full parse is stricter - it walks every header - so a
 * container can legitimately pass detection and still fail to list. */
static xx_saf_stream *xx_saf_parse(Abstractformat *self, bool header_only,
                                   xx_pd_struct *pd) {
    static const uint8_t banner_magic[8] = {
        (uint8_t)'S', (uint8_t)'A', (uint8_t)'F', (uint8_t)',',
        (uint8_t)' ', (uint8_t)'(', (uint8_t)'c', (uint8_t)')'};
    xx_saf_stream *stream = NULL;
    xx_saf_member member;
    uint8_t header[XX_SAF_LONG_BANNER];
    uint8_t entry[XX_SAF_ENTRY_SIZE];
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t first_member_offset;
    int64_t position;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Both banner variants are read out of the same window, and a banner with
     * no member behind it is not an archive. */
    if (span < (int64_t)(XX_SAF_LONG_BANNER + XX_SAF_ENTRY_SIZE)) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    if (!xx_saf_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, banner_magic, sizeof(banner_magic)) != 0) {
        return NULL;
    }
    /* The 1A 00 pair terminates the banner, and its position - one of exactly
     * two - is what fixes where the member chain starts. */
    if (header[0x1b] == 0x1aU && header[0x1c] == 0x00U) {
        first_member_offset = (int64_t)XX_SAF_SHORT_BANNER;
    } else if (header[0x2d] == 0x1aU && header[0x2e] == 0x00U) {
        first_member_offset = (int64_t)XX_SAF_LONG_BANNER;
    } else {
        return NULL;
    }

    stream = (xx_saf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->archive_size = span;
    stream->first_member_offset = self->base_address + first_member_offset;

    position = first_member_offset;
    while (position + (int64_t)XX_SAF_ENTRY_SIZE <= span) {
        int64_t uncompressed_size;
        int64_t compressed_size;
        int64_t data_offset;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_SAF_MAX_MEMBERS) goto fail;
        if (!xx_saf_read_at(self, self->base_address + position, entry,
                            sizeof(entry))) {
            goto fail;
        }
        if (!xx_saf_valid_raw_name(entry)) goto fail;

        /* Both sizes are written as u32 but read signed; a negative one is a
         * rejection, not a four-gigabyte member. */
        uncompressed_size = (int64_t)(int32_t)xx_saf_le32(entry + 0x0eU);
        compressed_size = (int64_t)(int32_t)xx_saf_le32(entry + 0x12U);
        if (uncompressed_size < 0 || compressed_size < 0) goto fail;

        data_offset = position + (int64_t)XX_SAF_ENTRY_SIZE;
        if (!xx_saf_range_within(span, data_offset, compressed_size)) goto fail;

        name = xx_saf_raw_name_to_string(entry);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + position;
        member.header_size = (int64_t)XX_SAF_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = entry[0x1b];
        /* Date first, time second - see the note at the top of this file. */
        member.timestamp = ((uint64_t)xx_saf_le16(entry + 0x16U) << 16) |
                           (uint64_t)xx_saf_le16(entry + 0x18U);
        member.is_folder = false;

        if (!xx_saf_add(stream, &member)) goto fail;
        name = NULL;

        if (header_only) return stream;

        /* The STORED packed size is what positions the next header. */
        position = data_offset + compressed_size;
    }

    if (stream->count == 0U) goto fail;
    return stream;

fail:
    if (name) xx_str_free(name);
    xx_saf_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/* Decode one member.  The method byte selects between a single stream and a
 * chain of length-prefixed chunks, and the codec entry point takes it
 * directly, so no chunk walking happens here. */
static bool xx_saf_decode(Abstractformat *self, const xx_saf_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool decoded;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_SAF_MAX_DECODED) return false;
    if (member->uncompressed_size > XX_SAF_MAX_DECODED) return false;

    /* A zero-length member is legal - the header simply records no bytes - but
     * the codec refuses a zero output capacity, so it is answered here with a
     * one-byte allocation and a reported length of zero. */
    if (member->uncompressed_size == 0) {
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size == 0) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_saf_read_at(self, member->data_offset, input,
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

    decoded = xx_saf_decode_memory_method(
        input, (size_t)member->compressed_size, member->method, output,
        (size_t)member->uncompressed_size, &written);
    /* The codec already demands an exact fill; the second half of this test is
     * what makes that a property of this reader rather than of the codec. */
    if (!decoded || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);

    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_saf_init(xx_saf *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SAF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stac-saf");
    xx_format_set_extension(&archive->format, "saf");
    archive->format.check_is_valid = xx_saf_check_is_valid;
    archive->format.handle_base_info = xx_saf_handle_base_info;
    archive->format.get_format_size = xx_saf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_saf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_saf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_saf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_saf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_saf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_saf_free_archive_records_reading;
    archive->format.destroy = xx_saf_vtable_destroy;
    archive->first_member_offset = -1;
}

xx_saf *xx_saf_create(xx_io_device *device, int64_t base_address) {
    xx_saf *archive = (xx_saf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_saf_init(archive, device, base_address);
    return archive;
}

void xx_saf_destroy(xx_saf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_saf_free(xx_saf *archive) {
    if (!archive) return;
    xx_saf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_saf_vtable_destroy(Abstractformat *self) {
    xx_saf_destroy((xx_saf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_saf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_saf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_saf_parse(self, true, pd);
    if (!stream) return false;
    xx_saf_stream_free(stream);
    return true;
}

bool xx_saf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_saf *archive = (xx_saf *)self;
    xx_saf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_saf_parse(self, false, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->first_member_offset = stream->first_member_offset;
    xx_saf_stream_free(stream);
    return true;
}

int64_t xx_saf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_saf_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_saf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_saf_set_record(xx_archive_record *record,
                              const xx_saf_member *member) {
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

static bool xx_saf_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_saf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_saf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_saf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_saf_parse(self, false, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_saf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_saf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_saf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_saf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_saf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_saf_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_saf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_saf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_saf_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_saf_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_saf_stream *stream;
    const xx_saf_member *member;
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
    stream = (xx_saf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_saf_path_safe(member->name)) return false;

    path_option = xx_saf_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_saf_decode(self, member, &plain, &plain_size, pd);
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
        !xx_saf_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_saf_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
