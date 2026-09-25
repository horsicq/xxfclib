/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HLB libraries.
 *
 *   header, 8 bytes:
 *     0x00  u16 LE magic, 0x04D2 (decimal 1234)
 *     0x02  u16 LE number of entries
 *     0x04  u32 LE offset of the directory
 *
 *   payloads follow the header, stored verbatim and back to back.
 *
 *   directory at that offset, one 18-byte entry per member:
 *     0x00  i32 LE offset of the member's data
 *     0x04  name, 14 bytes, NUL padded
 *
 *   then a trailing u32 LE repeating the directory offset.
 *
 * Nothing is compressed. Entries carry no size: a member ends where the next
 * one begins, and the last ends at the directory, so strictly ascending
 * offsets are what gives every member a positive, non-overlapping extent.
 *
 * The magic is only two bytes, so identification rests on the layout instead:
 * header, payloads, directory and back-pointer must tile the file exactly,
 * and the back-pointer must repeat the directory offset.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hlb/xx_hlb.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_HLB_COPY_CHUNK (64 * 1024)

typedef struct xx_hlb_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_hlb_member;

typedef struct xx_hlb_stream_s {
    xx_hlb_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_hlb_stream;

static void xx_hlb_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_hlb_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_hlb_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_hlb_path_safe(const char *name) {
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

static void xx_hlb_stream_free(void *pointer) {
    xx_hlb_stream *stream = (xx_hlb_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_hlb_add(xx_hlb_stream *stream,
                          const xx_hlb_member *member) {
    xx_hlb_member *grown = (xx_hlb_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_hlb_decode(Abstractformat *self,
                             const xx_hlb_member *member, uint8_t **out,
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
         !xx_hlb_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_HLB_HEADER_SIZE 8
#define XX_HLB_ENTRY_SIZE 18
#define XX_HLB_NAME_OFFSET 4
#define XX_HLB_NAME_SIZE 14
#define XX_HLB_BACKPOINTER_SIZE 4
#define XX_HLB_MAGIC 0x04D2U
/* The count field is 16 bit, so nothing larger than 65535 entries can have
 * been written by a real producer. */
#define XX_HLB_MAX_MEMBERS 65535

static uint32_t xx_hlb_le16(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8);
}

static uint32_t xx_hlb_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The name field is NUL padded. Everything before the first NUL is the name;
 * a NUL followed by a non-NUL byte is not a shape any writer produces, and
 * refusing it is one of the structural rules that keeps random bytes from
 * passing as a directory. */
static bool xx_hlb_name_is_valid(const uint8_t *field) {
    size_t index;
    bool padding = false;

    if (field[0] == 0U) return false;
    for (index = 0U; index < XX_HLB_NAME_SIZE; ++index) {
        uint8_t character = field[index];
        if (character == 0U) {
            padding = true;
        } else if (padding) {
            return false;
        } else if (character < 0x20U || character > 0x7EU) {
            return false;
        }
    }
    return true;
}

static xx_hlb_stream *xx_hlb_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_hlb_stream *stream = NULL;
    uint8_t *directory = NULL;
    uint8_t header[XX_HLB_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t previous;
    int64_t count;
    int64_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Header, at least one payload byte, one entry and the back-pointer. */
    if (span < XX_HLB_HEADER_SIZE + 1 + XX_HLB_ENTRY_SIZE +
                   XX_HLB_BACKPOINTER_SIZE) {
        return NULL;
    }
    if (!xx_hlb_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_hlb_le16(header) != XX_HLB_MAGIC) return NULL;
    count = (int64_t)xx_hlb_le16(header + 2);
    if (count < 1 || count > XX_HLB_MAX_MEMBERS) return NULL;

    directory_offset = (int64_t)xx_hlb_le32(header + 4);
    /* The directory always trails at least one payload byte. */
    if (directory_offset <= XX_HLB_HEADER_SIZE) return NULL;
    directory_size = count * XX_HLB_ENTRY_SIZE;
    /* Header, payloads, directory and back-pointer must tile the span
     * exactly. This is what makes a two-byte magic safe to detect on: it
     * rules out both a coincidental 0x04D2 and an appended overlay. */
    if (directory_offset + directory_size + XX_HLB_BACKPOINTER_SIZE != span) {
        return NULL;
    }

    directory = (uint8_t *)xx_mem_alloc(
        (size_t)(directory_size + XX_HLB_BACKPOINTER_SIZE));
    if (!directory ||
        !xx_hlb_read_at(self, self->base_address + directory_offset, directory,
                        (size_t)(directory_size + XX_HLB_BACKPOINTER_SIZE))) {
        xx_mem_free(directory);
        return NULL;
    }
    /* The directory repeats its own offset behind the last entry; when the
     * two disagree this is not an HLB library. */
    if ((int64_t)xx_hlb_le32(directory + directory_size) != directory_offset) {
        xx_mem_free(directory);
        return NULL;
    }

    stream = (xx_hlb_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(directory);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    /* One below the first legal data offset, so the ascending test below also
     * enforces the lower bound for the very first entry. */
    previous = XX_HLB_HEADER_SIZE - 1;
    for (index = 0; index < count; ++index) {
        const uint8_t *entry = directory + (size_t)(index * XX_HLB_ENTRY_SIZE);
        xx_hlb_member member;
        char *name;
        int64_t offset;
        int64_t end;
        int64_t size;
        size_t length = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        offset = (int64_t)(int32_t)xx_hlb_le32(entry);
        /* Sizes are implicit, so strictly ascending offsets are the only
         * thing guaranteeing a positive, non-overlapping extent per member. */
        if (offset <= previous) goto fail;
        previous = offset;
        if (!xx_hlb_name_is_valid(entry + XX_HLB_NAME_OFFSET)) goto fail;

        end = (index + 1 < count)
                  ? (int64_t)(int32_t)xx_hlb_le32(entry + XX_HLB_ENTRY_SIZE)
                  : directory_offset;
        size = end - offset;
        if (size <= 0 || !xx_hlb_range_within(span, offset, size)) goto fail;

        while (length < XX_HLB_NAME_SIZE &&
               entry[XX_HLB_NAME_OFFSET + length] != 0U) {
            ++length;
        }
        /* The corpus pads DOS 8.3 names out with blanks. */
        while (length > 0U && entry[XX_HLB_NAME_OFFSET + length - 1U] == ' ') {
            --length;
        }
        name = (char *)xx_mem_alloc(XX_HLB_NAME_SIZE + 1U);
        if (!name) goto fail;
        if (length == 0U) {
            /* An all-blank field is legal but unusable as a file name. */
            xx_rt_snprintf(name, XX_HLB_NAME_SIZE + 1U, "record%d",
                           (int)index);
        } else {
            size_t copy;
            for (copy = 0U; copy < length; ++copy) {
                name[copy] = (char)entry[XX_HLB_NAME_OFFSET + copy];
            }
            name[length] = '\0';
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset =
            self->base_address + directory_offset + index * XX_HLB_ENTRY_SIZE;
        member.header_size = XX_HLB_ENTRY_SIZE;
        member.data_offset = self->base_address + offset;
        member.compressed_size = size;
        member.uncompressed_size = size;
        if (!xx_hlb_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
    }
    /* The last payload must still end before the directory begins. */
    if (previous >= directory_offset) goto fail;

    xx_mem_free(directory);
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(directory);
    xx_hlb_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_hlb_init(xx_hlb *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_HLB;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-hlb");
    xx_format_set_extension(&archive->format, "hlb");
    archive->format.check_is_valid = xx_hlb_check_is_valid;
    archive->format.handle_base_info = xx_hlb_handle_base_info;
    archive->format.get_format_size = xx_hlb_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hlb_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hlb_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hlb_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hlb_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hlb_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hlb_free_archive_records_reading;
    archive->format.destroy = xx_hlb_vtable_destroy;
}

xx_hlb *xx_hlb_create(xx_io_device *device, int64_t base_address) {
    xx_hlb *archive = (xx_hlb *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_hlb_init(archive, device, base_address);
    return archive;
}

void xx_hlb_destroy(xx_hlb *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_hlb_free(xx_hlb *archive) {
    if (!archive) return;
    xx_hlb_destroy(archive);
    xx_mem_free(archive);
}

static void xx_hlb_vtable_destroy(Abstractformat *self) {
    xx_hlb_destroy((xx_hlb *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_hlb_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_hlb_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_hlb_parse(self, pd);
    if (!stream) return false;
    xx_hlb_stream_free(stream);
    return true;
}

bool xx_hlb_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_hlb *archive = (xx_hlb *)self;
    xx_hlb_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_hlb_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_hlb_stream_free(stream);
    return true;
}

int64_t xx_hlb_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_hlb_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_hlb *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_hlb_set_record(xx_archive_record *record,
                                 const xx_hlb_member *member) {
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

static bool xx_hlb_copy_options(xx_list_s *target,
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

static const xx_var *xx_hlb_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_hlb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_hlb_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_hlb_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_hlb_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_hlb_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_hlb_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_hlb_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_hlb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_hlb_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_hlb_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_hlb_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_hlb_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_hlb_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_hlb_stream *stream;
    const xx_hlb_member *member;
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
    stream = (xx_hlb_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_hlb_path_safe(member->name)) return false;

    path_option = xx_hlb_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_hlb_decode(self, member, &plain, &plain_size, pd);
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
        !xx_hlb_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_hlb_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
