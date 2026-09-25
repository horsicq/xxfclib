/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TRCPAK archives.
 *
 *   header, 0x1c bytes:
 *     0x00  "TRCPAK\0", 7 bytes
 *     0x07  padding
 *     0x08  u32 LE version, an IEEE-754 float rather than a number
 *     0x0c  i32 LE member count
 *     0x10  reserved to 0x1c
 *
 *   directory at 0x1c, one 0x10c byte entry per member:
 *     0x000  name, 0x104 bytes, NUL terminated
 *     0x104  i32 LE data size
 *     0x108  i32 LE data offset
 *
 * Size comes before offset, which is the reverse of the usual ordering and
 * the easiest field pair in the format to transpose.
 *
 * Every member is stored verbatim; there is no compression method field.
 * Member data lives outside the directory and may be ordered freely, so the
 * archive ends at the furthest member end rather than at the last entry.
 *
 * The count bound is what keeps a stray "TRCPAK" from parsing: the directory
 * it claims must fit in the file before a single entry is read.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/trcpak/xx_trcpak.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_TRCPAK_COPY_CHUNK (64 * 1024)

typedef struct xx_trcpak_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_trcpak_member;

typedef struct xx_trcpak_stream_s {
    xx_trcpak_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_trcpak_stream;

static void xx_trcpak_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_trcpak_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_trcpak_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_trcpak_path_safe(const char *name) {
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

static void xx_trcpak_stream_free(void *pointer) {
    xx_trcpak_stream *stream = (xx_trcpak_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_trcpak_add(xx_trcpak_stream *stream,
                          const xx_trcpak_member *member) {
    xx_trcpak_member *grown = (xx_trcpak_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_trcpak_decode(Abstractformat *self,
                             const xx_trcpak_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *buffer;

    *out = NULL;
    *out_size = 0U;
    if (member->compressed_size < 0 ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!buffer) return false;
    if (member->compressed_size != 0 &&
        ((pd && xx_pd_is_stopped(pd)) ||
         !xx_trcpak_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_TRCPAK_HEADER_SIZE 0x1c
#define XX_TRCPAK_ENTRY_SIZE 0x10c
#define XX_TRCPAK_NAME_SIZE 0x104
#define XX_TRCPAK_MAX_MEMBERS 1000000

static uint32_t xx_trcpak_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_trcpak_stream *xx_trcpak_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    static const uint8_t magic[7] = {'T', 'R', 'C', 'P', 'A', 'K', 0x00};
    xx_trcpak_stream *stream;
    uint8_t header[XX_TRCPAK_HEADER_SIZE];
    uint8_t entry[XX_TRCPAK_ENTRY_SIZE];
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t index;
    int64_t archive_end;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_TRCPAK_HEADER_SIZE) return NULL;
    if (!xx_trcpak_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* The NUL is part of the magic: "TRCPAKx" is not this format. */
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;

    /* Signed on purpose: a count with the top bit set is a corrupt field, not
     * a two-billion-entry directory. */
    count = (int64_t)(int32_t)xx_trcpak_le32(header + 0x0c);
    if (count <= 0 || count > XX_TRCPAK_MAX_MEMBERS) return NULL;
    /* The whole defence against a false positive: seven magic bytes are cheap
     * to hit by accident, but a directory of count * 0x10c bytes that also
     * fits in the file is not. Written as a division so the product cannot
     * overflow. */
    if (count > (span - XX_TRCPAK_HEADER_SIZE) / XX_TRCPAK_ENTRY_SIZE) {
        return NULL;
    }

    stream = (xx_trcpak_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    archive_end = XX_TRCPAK_HEADER_SIZE + (count * XX_TRCPAK_ENTRY_SIZE);

    for (index = 0; index < count; ++index) {
        xx_trcpak_member member;
        char *name;
        size_t name_length = 0U;
        int64_t entry_offset;
        int64_t data_offset;
        int64_t data_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry_offset = XX_TRCPAK_HEADER_SIZE + (index * XX_TRCPAK_ENTRY_SIZE);
        if (!xx_trcpak_range_within(span, entry_offset, XX_TRCPAK_ENTRY_SIZE) ||
            !xx_trcpak_read_at(self, self->base_address + entry_offset, entry,
                               sizeof(entry))) {
            goto fail;
        }

        /* Size first, offset second - the reverse of the usual ordering. */
        data_size = (int64_t)(int32_t)xx_trcpak_le32(entry + 0x104);
        data_offset = (int64_t)(int32_t)xx_trcpak_le32(entry + 0x108);
        if (data_size < 0 || data_offset < 0) goto fail;
        /* A member extending past EOF is a rejection, not a truncated read. */
        if (!xx_trcpak_range_within(span, data_offset, data_size)) goto fail;

        /* The name field must contain its own terminator; a field filled edge
         * to edge with text is the signature of misparsed data, not a name. */
        while (name_length < (size_t)XX_TRCPAK_NAME_SIZE &&
               entry[name_length] != 0U) {
            if (entry[name_length] < 0x20U || entry[name_length] > 0x7EU) {
                goto fail;
            }
            ++name_length;
        }
        if (name_length == 0U ||
            name_length >= (size_t)XX_TRCPAK_NAME_SIZE) {
            goto fail;
        }
        name = (char *)xx_mem_alloc(name_length + 1U);
        if (!name) goto fail;
        for (entry_offset = 0; entry_offset < (int64_t)name_length;
             ++entry_offset) {
            name[entry_offset] = (char)entry[entry_offset];
        }
        name[name_length] = '\0';
        if (!xx_trcpak_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset =
            self->base_address + XX_TRCPAK_HEADER_SIZE +
            (index * XX_TRCPAK_ENTRY_SIZE);
        member.header_size = XX_TRCPAK_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = data_size;
        if (!xx_trcpak_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        /* Members are not required to follow the directory in order, so the
         * end of the archive is the furthest extent seen, not the last one. */
        if (data_offset + data_size > archive_end) {
            archive_end = data_offset + data_size;
        }
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = (archive_end < span) ? archive_end : span;
    return stream;

fail:
    xx_trcpak_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_trcpak_init(xx_trcpak *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_TRCPAK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-trcpak");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_trcpak_check_is_valid;
    archive->format.handle_base_info = xx_trcpak_handle_base_info;
    archive->format.get_format_size = xx_trcpak_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_trcpak_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_trcpak_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_trcpak_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_trcpak_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_trcpak_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_trcpak_free_archive_records_reading;
    archive->format.destroy = xx_trcpak_vtable_destroy;
}

xx_trcpak *xx_trcpak_create(xx_io_device *device, int64_t base_address) {
    xx_trcpak *archive = (xx_trcpak *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_trcpak_init(archive, device, base_address);
    return archive;
}

void xx_trcpak_destroy(xx_trcpak *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_trcpak_free(xx_trcpak *archive) {
    if (!archive) return;
    xx_trcpak_destroy(archive);
    xx_mem_free(archive);
}

static void xx_trcpak_vtable_destroy(Abstractformat *self) {
    xx_trcpak_destroy((xx_trcpak *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_trcpak_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_trcpak_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_trcpak_parse(self, pd);
    if (!stream) return false;
    xx_trcpak_stream_free(stream);
    return true;
}

bool xx_trcpak_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_trcpak *archive = (xx_trcpak *)self;
    xx_trcpak_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_trcpak_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_trcpak_stream_free(stream);
    return true;
}

int64_t xx_trcpak_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_trcpak_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_trcpak *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_trcpak_set_record(xx_archive_record *record,
                                 const xx_trcpak_member *member) {
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

static bool xx_trcpak_copy_options(xx_list_s *target,
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

static const xx_var *xx_trcpak_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_trcpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_trcpak_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_trcpak_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_trcpak_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_trcpak_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_trcpak_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_trcpak_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_trcpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_trcpak_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_trcpak_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_trcpak_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_trcpak_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_trcpak_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_trcpak_stream *stream;
    const xx_trcpak_member *member;
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
    stream = (xx_trcpak_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_trcpak_path_safe(member->name)) return false;

    path_option = xx_trcpak_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_trcpak_decode(self, member, &plain, &plain_size, pd);
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
        !xx_trcpak_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_trcpak_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
