/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PackIt archives.
 *
 *   signature, 16 bytes:  "PACKIT by MJP" CR LF SUB
 *
 * then a chain of records, each introduced by a u16 LE tag:
 *
 *   0x00ff  a member:
 *     +0x02  i32 LE size
 *     +0x06  i32 LE size again
 *     +0x0a  u16 LE DOS date
 *     +0x0c  u16 LE DOS time
 *     +0x0e  u32 LE CRC-32
 *     +0x12  u8 name length, then the name and an explicit NUL
 *   0xffff  end of archive
 *
 * The size is stored twice and both copies must agree: that redundancy is the
 * only per-member integrity field available before the data is read, and it is
 * what stops a plausible tag from starting a bogus member.
 *
 * Any tag that is neither of the two is a structural error rather than an end
 * of chain -- ending quietly there would silently truncate an archive whose
 * middle is damaged.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/packit/xx_packit.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_PACKIT_COPY_CHUNK (64 * 1024)

typedef struct xx_packit_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_packit_member;

typedef struct xx_packit_stream_s {
    xx_packit_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_packit_stream;

static void xx_packit_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_packit_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_packit_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_packit_path_safe(const char *name) {
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

static void xx_packit_stream_free(void *pointer) {
    xx_packit_stream *stream = (xx_packit_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_packit_add(xx_packit_stream *stream,
                          const xx_packit_member *member) {
    xx_packit_member *grown = (xx_packit_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_packit_decode(Abstractformat *self,
                             const xx_packit_member *member, uint8_t **out,
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
         !xx_packit_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_PACKIT_SIGNATURE_SIZE 16
#define XX_PACKIT_FIXED_HEADER_SIZE 19
#define XX_PACKIT_TAG_MEMBER 0x00FFU
#define XX_PACKIT_TAG_END 0xFFFFU
#define XX_PACKIT_MAX_MEMBERS 65536

static const uint8_t XX_PACKIT_SIGNATURE[XX_PACKIT_SIGNATURE_SIZE] = {
    'P', 'A', 'C', 'K', 'I', 'T', ' ', 'b',
    'y', ' ', 'M', 'J', 'P', 0x0DU, 0x0AU, 0x1AU};

static uint16_t xx_packit_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_packit_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_packit_stream *xx_packit_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_packit_stream *stream;
    uint8_t signature[XX_PACKIT_SIGNATURE_SIZE];
    uint8_t header[XX_PACKIT_FIXED_HEADER_SIZE];
    uint8_t name[256];
    int64_t total;
    int64_t span;
    int64_t offset;
    int index;
    bool terminated = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_PACKIT_SIGNATURE_SIZE + XX_PACKIT_FIXED_HEADER_SIZE + 2) {
        return NULL;
    }
    if (!xx_packit_read_at(self, self->base_address, signature,
                           sizeof(signature)) ||
        xx_rt_memcmp(signature, XX_PACKIT_SIGNATURE,
                     XX_PACKIT_SIGNATURE_SIZE) != 0) {
        return NULL;
    }

    stream = (xx_packit_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_PACKIT_SIGNATURE_SIZE;
    for (index = 0; index < XX_PACKIT_MAX_MEMBERS; ++index) {
        uint8_t tag_bytes[2];
        uint16_t tag;
        xx_packit_member member;
        int64_t size;
        int64_t size_copy;
        int64_t name_offset;
        size_t name_length;
        size_t i;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (offset + 2 > span) break;
        if (!xx_packit_read_at(self, self->base_address + offset, tag_bytes,
                               2U)) {
            goto fail;
        }
        tag = xx_packit_le16(tag_bytes);
        if (tag == XX_PACKIT_TAG_END) {
            offset += 2;
            terminated = true;
            break;
        }
        /* Not the end, and not a member: the chain is damaged. Stopping
         * quietly here would silently drop everything after it. */
        if (tag != XX_PACKIT_TAG_MEMBER) goto fail;

        if (offset + XX_PACKIT_FIXED_HEADER_SIZE > span) break;
        if (!xx_packit_read_at(self, self->base_address + offset, header,
                               sizeof(header))) {
            goto fail;
        }
        size = (int64_t)(int32_t)xx_packit_le32(header + 2);
        size_copy = (int64_t)(int32_t)xx_packit_le32(header + 6);
        /* Both copies must agree. */
        if (size < 0 || size_copy < 0 || size != size_copy) goto fail;
        name_length = header[18];
        if (name_length == 0U) goto fail;

        name_offset = offset + XX_PACKIT_FIXED_HEADER_SIZE;
        if (name_offset + (int64_t)name_length + 1 > span) break;
        if (!xx_packit_read_at(self, self->base_address + name_offset, name,
                               name_length + 1U)) {
            goto fail;
        }
        /* The name is followed by an explicit NUL; anything else means the
         * record is not laid out as the format says. */
        if (name[name_length] != 0U) goto fail;
        for (i = 0U; i < name_length; ++i) {
            if (name[i] < 0x20U || name[i] > 0x7EU) goto fail;
        }
        name[name_length] = '\0';

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup((const char *)name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_PACKIT_FIXED_HEADER_SIZE +
                             (int64_t)name_length + 1;
        member.data_offset =
            self->base_address + name_offset + (int64_t)name_length + 1;
        member.timestamp = (uint64_t)xx_packit_le16(header + 10) |
                           ((uint64_t)xx_packit_le16(header + 12) << 16);

        if (name_offset + (int64_t)name_length + 1 + size > span) {
            /* Cut off by the end of the file. The reference publishes what
             * survives and reports the archive damaged, so the member is
             * clamped to the bytes actually present and the chain stops. Its
             * stored CRC will not match, which is how a consumer finds out. */
            int64_t available =
                span - (name_offset + (int64_t)name_length + 1);
            if (available <= 0) {
                xx_str_free(member.name);
                break;
            }
            member.compressed_size = available;
            member.uncompressed_size = available;
            if (!xx_packit_add(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
            offset = span;
            break;
        }
        member.compressed_size = size;
        member.uncompressed_size = size;
        if (!xx_packit_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        offset = name_offset + (int64_t)name_length + 1 + size;
    }
    (void)terminated;
    if (stream->count == 0U) goto fail;
    stream->archive_size = offset < span ? offset : span;
    return stream;

fail:
    xx_packit_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_packit_init(xx_packit *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PACKIT;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-packit");
    xx_format_set_extension(&archive->format, "pit");
    archive->format.check_is_valid = xx_packit_check_is_valid;
    archive->format.handle_base_info = xx_packit_handle_base_info;
    archive->format.get_format_size = xx_packit_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_packit_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_packit_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_packit_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_packit_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_packit_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_packit_free_archive_records_reading;
    archive->format.destroy = xx_packit_vtable_destroy;
}

xx_packit *xx_packit_create(xx_io_device *device, int64_t base_address) {
    xx_packit *archive = (xx_packit *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_packit_init(archive, device, base_address);
    return archive;
}

void xx_packit_destroy(xx_packit *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_packit_free(xx_packit *archive) {
    if (!archive) return;
    xx_packit_destroy(archive);
    xx_mem_free(archive);
}

static void xx_packit_vtable_destroy(Abstractformat *self) {
    xx_packit_destroy((xx_packit *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_packit_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_packit_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_packit_parse(self, pd);
    if (!stream) return false;
    xx_packit_stream_free(stream);
    return true;
}

bool xx_packit_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_packit *archive = (xx_packit *)self;
    xx_packit_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_packit_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_packit_stream_free(stream);
    return true;
}

int64_t xx_packit_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_packit_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_packit *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_packit_set_record(xx_archive_record *record,
                                 const xx_packit_member *member) {
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

static bool xx_packit_copy_options(xx_list_s *target,
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

static const xx_var *xx_packit_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_packit_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_packit_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_packit_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_packit_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_packit_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_packit_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_packit_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_packit_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_packit_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_packit_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_packit_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_packit_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_packit_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_packit_stream *stream;
    const xx_packit_member *member;
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
    stream = (xx_packit_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_packit_path_safe(member->name)) return false;

    path_option = xx_packit_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_packit_decode(self, member, &plain, &plain_size, pd);
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
        !xx_packit_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_packit_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
