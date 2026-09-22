/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Commodore 64 "Wraptor" archives (*.wra, *.wr3). The codec is
 * xx_c64wraptor_decode_memory() / xx_c64wraptor_scan_memory().
 *
 *   member:
 *     u32 LE  0xff4c42ff  -- the bytes ff 42 4c ff, "\xff BL \xff"
 *     char[]  name, NUL-terminated
 *     u8      flags
 *     ...     LZSS stream, no length field of any kind
 *     u16     checksum, not verified here
 *   archive end:
 *     u32 LE  0x1a1a1a1a
 *
 * NEITHER length is stored: the packed length is where the stream's own end
 * escape falls and the unpacked length is whatever it produced. Both come from
 * xx_c64wraptor_scan_memory(), which runs the decoder's core without keeping
 * the output, so the walk and a later extraction can never disagree about
 * where a member ends. The cost is that merely LISTING an archive decodes
 * every member once.
 *
 * Because the chain can only be followed by decoding it, the whole file is
 * read into memory for the walk and an archive larger than
 * XX_C64WRAPTOR_MAX_INPUT is refused -- the reference does the same.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/c64wraptor/xx_c64wraptor.h"

#include "xxfclib/algo/c64wraptor/xx_c64wraptor.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The alias macro is defined next to the enumerator in xxfc_defs.h, so testing
 * for it picks up the real file type as soon as C64WRAPTOR is registered
 * there. See the port report for the registration this needs. */
#ifdef C64WRAPTOR
#define XX_C64WRAPTOR_FILE_TYPE XX_FILE_TYPE_C64WRAPTOR
#else
#define XX_C64WRAPTOR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_C64WRAPTOR_MEMBER_MAGIC 0xff4c42ffU
#define XX_C64WRAPTOR_END_MARKER 0x1a1a1a1aU
#define XX_C64WRAPTOR_MAGIC_SIZE 4
#define XX_C64WRAPTOR_CHECKSUM_SIZE 2
#define XX_C64WRAPTOR_MAX_MEMBERS 100000
#define XX_C64WRAPTOR_MAX_NAME_SIZE 4096
#define XX_C64WRAPTOR_MAX_OUTPUT ((int64_t)0x4000000)
/* The chain can only be walked in memory, and a C64 archive that needs more
 * than this is not one. */
#define XX_C64WRAPTOR_MAX_INPUT ((int64_t)0x4000000)

typedef struct xx_c64wraptor_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint8_t flags;
} xx_c64wraptor_member;

typedef struct xx_c64wraptor_stream_s {
    xx_c64wraptor_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_c64wraptor_stream;

static void xx_c64wraptor_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_c64wraptor_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_c64wraptor_read_at(Abstractformat *self, int64_t offset,
                                  uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. C64 names are
 * PETSCII and carry no directory structure, so any separator at all is a
 * reason to refuse rather than a path to honour. */
static bool xx_c64wraptor_path_safe(const char *name) {
    const char *cursor;

    if (!name || !name[0]) return false;
    for (cursor = name; *cursor; ++cursor) {
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':') return false;
    }
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
        return false;
    }
    return true;
}

static void xx_c64wraptor_stream_free(void *pointer) {
    xx_c64wraptor_stream *stream = (xx_c64wraptor_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of member->name. */
static bool xx_c64wraptor_add(xx_c64wraptor_stream *stream,
                              const xx_c64wraptor_member *member) {
    xx_c64wraptor_member *grown = (xx_c64wraptor_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static char *xx_c64wraptor_name_copy(const uint8_t *data, size_t size) {
    char *name = (char *)xx_mem_alloc(size + 1U);

    if (!name) return NULL;
    if (size != 0U) xx_rt_memcpy(name, data, size);
    name[size] = '\0';
    return name;
}

/* --------------------------------------------------------------- parse -- */

static xx_c64wraptor_stream *xx_c64wraptor_parse(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    xx_c64wraptor_stream *stream = NULL;
    xx_c64wraptor_member member;
    uint8_t *data = NULL;
    int64_t total;
    int64_t span;
    int64_t position = 0;
    bool end_marker = false;
    char *name = NULL;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)XX_C64WRAPTOR_MAGIC_SIZE ||
        span > XX_C64WRAPTOR_MAX_INPUT) {
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    data = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!data) return NULL;
    if (!xx_c64wraptor_read_at(self, self->base_address, data, (size_t)span)) {
        goto fail;
    }
    /* Cheap gate before the expensive walk: the first member's magic. */
    if (xx_c64wraptor_le32(data) != XX_C64WRAPTOR_MEMBER_MAGIC) goto fail;

    stream = (xx_c64wraptor_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    while (position + XX_C64WRAPTOR_MAGIC_SIZE <= span) {
        int64_t header_offset;
        int64_t name_end;
        int64_t flags_offset;
        int64_t data_offset;
        size_t consumed = 0U;
        size_t produced = 0U;
        uint32_t tag;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_C64WRAPTOR_MAX_MEMBERS) goto fail;

        tag = xx_c64wraptor_le32(data + position);
        if (tag == XX_C64WRAPTOR_END_MARKER) {
            end_marker = true;
            break;
        }
        /* Unlike a chain that may simply stop, a tag that is neither the
         * member magic nor the end marker means the previous member's length
         * was wrong, and every offset after it is meaningless. Reject. */
        if (tag != XX_C64WRAPTOR_MEMBER_MAGIC) goto fail;

        header_offset = position;
        position += XX_C64WRAPTOR_MAGIC_SIZE;

        name_end = position;
        while (name_end < span && data[name_end] != 0U) {
            if (name_end - position > (int64_t)XX_C64WRAPTOR_MAX_NAME_SIZE) {
                goto fail;
            }
            ++name_end;
        }
        if (name_end >= span) goto fail;

        /* the NUL, and the flags byte that follows it */
        flags_offset = name_end + 1;
        if (flags_offset >= span) goto fail;
        data_offset = flags_offset + 1;
        if (data_offset > span) goto fail;

        /* The only way to find where the member ends. */
        if (!xx_c64wraptor_scan_memory(data + data_offset,
                                       (size_t)(span - data_offset),
                                       (size_t)XX_C64WRAPTOR_MAX_OUTPUT,
                                       &consumed, &produced)) {
            goto fail;
        }
        if ((int64_t)consumed > span - data_offset) goto fail;

        name = xx_c64wraptor_name_copy(data + position,
                                       (size_t)(name_end - position));
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + header_offset;
        member.header_size = data_offset - header_offset;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = (int64_t)consumed;
        member.uncompressed_size = (int64_t)produced;
        member.flags = data[flags_offset];
        if (!xx_c64wraptor_add(stream, &member)) goto fail;
        name = NULL;

        position = data_offset + (int64_t)consumed + XX_C64WRAPTOR_CHECKSUM_SIZE;
        if (position > span) {
            /* The checksum of the last member may itself be truncated; the
             * walk simply ends there, exactly as the reference's does. */
            position = span;
            break;
        }
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size =
        end_marker ? (position + XX_C64WRAPTOR_MAGIC_SIZE) : position;
    if (stream->archive_size > span) stream->archive_size = span;

    xx_mem_free(data);
    return stream;

fail:
    if (name) xx_str_free(name);
    if (data) xx_mem_free(data);
    xx_c64wraptor_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_c64wraptor_decode(Abstractformat *self,
                                 const xx_c64wraptor_member *member,
                                 uint8_t **out, size_t *out_size,
                                 xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size <= 0) {
        return false;
    }
    if (member->compressed_size > XX_C64WRAPTOR_MAX_INPUT ||
        member->uncompressed_size > XX_C64WRAPTOR_MAX_OUTPUT) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_c64wraptor_read_at(self, member->data_offset, input,
                               (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* The size came from the scan of this very stream, so a disagreement here
     * means the file changed under us. */
    if (!xx_c64wraptor_decode_memory(input, (size_t)member->compressed_size,
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

void xx_c64wraptor_init(xx_c64wraptor *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_C64WRAPTOR_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-c64-wraptor");
    xx_format_set_extension(&archive->format, "wra");
    archive->format.check_is_valid = xx_c64wraptor_check_is_valid;
    archive->format.handle_base_info = xx_c64wraptor_handle_base_info;
    archive->format.get_format_size = xx_c64wraptor_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_c64wraptor_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_c64wraptor_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_c64wraptor_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_c64wraptor_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_c64wraptor_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_c64wraptor_free_archive_records_reading;
    archive->format.destroy = xx_c64wraptor_vtable_destroy;
}

xx_c64wraptor *xx_c64wraptor_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_c64wraptor *archive = (xx_c64wraptor *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_c64wraptor_init(archive, device, base_address);
    return archive;
}

void xx_c64wraptor_destroy(xx_c64wraptor *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_c64wraptor_free(xx_c64wraptor *archive) {
    if (!archive) return;
    xx_c64wraptor_destroy(archive);
    xx_mem_free(archive);
}

static void xx_c64wraptor_vtable_destroy(Abstractformat *self) {
    xx_c64wraptor_destroy((xx_c64wraptor *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_c64wraptor_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_c64wraptor_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_c64wraptor_parse(self, pd);
    if (!stream) return false;
    xx_c64wraptor_stream_free(stream);
    return true;
}

bool xx_c64wraptor_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_c64wraptor *archive = (xx_c64wraptor *)self;
    xx_c64wraptor_stream *stream;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_c64wraptor_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;

    total = xx_io_total_size(self->device);
    if (total > self->base_address + stream->archive_size) {
        self->overlay_offset = self->base_address + stream->archive_size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    xx_c64wraptor_stream_free(stream);
    return true;
}

int64_t xx_c64wraptor_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_c64wraptor_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_c64wraptor *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_c64wraptor_set_record(xx_archive_record *record,
                                     const xx_c64wraptor_member *member) {
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
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          (uint64_t)member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_c64wraptor_copy_options(xx_list_s *target,
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

static const xx_var *xx_c64wraptor_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_c64wraptor_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_c64wraptor_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_c64wraptor_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_c64wraptor_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_c64wraptor_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_c64wraptor_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_c64wraptor_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_c64wraptor_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_c64wraptor_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_c64wraptor_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_c64wraptor_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_c64wraptor_set_record(&state->current_record,
                                                 &stream->items[stream->index]);
    return state->has_record;
}

bool xx_c64wraptor_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_c64wraptor_stream *stream;
    const xx_c64wraptor_member *member;
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
    stream = (xx_c64wraptor_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_c64wraptor_path_safe(member->name)) return false;

    path_option =
        xx_c64wraptor_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_c64wraptor_decode(self, member, &plain, &plain_size, pd);
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
        !xx_c64wraptor_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_c64wraptor_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
