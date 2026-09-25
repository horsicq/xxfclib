/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IRIX standalone tools volumes ("sa").
 *
 * One 512-byte header block, then raw payload. Everything is big-endian and
 * every offset in the volume is counted in 512-byte blocks.
 *
 *   0x00  u32 BE magic, 0xACED1234
 *   0x04  u32 BE checksum makeweight
 *   0x20  directory: 20 slots of 24 bytes each, filling the block exactly
 *           +0x00  name, 16 bytes, NUL padded
 *           +0x10  u32 BE first block of the member
 *           +0x14  u32 BE member size in bytes
 *
 * The whole 512-byte header block, read as 128 big-endian u32 words, must sum
 * to zero modulo 2^32; the word at 0x04 is the makeweight that makes it so.
 * That is what makes a mere four-byte magic safe to detect on.
 *
 * Slot 0 is always in use and always points at block 1, the block directly
 * behind the header. Later slots may be zeroed; a zeroed slot is skipped
 * rather than ending the walk, because a used slot can follow an unused one.
 *
 * Members are raw ECOFF MIPS images -- the volume never compresses -- and the
 * volume is padded out to a whole block past the last member.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/irixsa/xx_irixsa.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_IRIXSA_COPY_CHUNK (64 * 1024)

typedef struct xx_irixsa_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_irixsa_member;

typedef struct xx_irixsa_stream_s {
    xx_irixsa_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_irixsa_stream;

static void xx_irixsa_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_irixsa_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_irixsa_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_irixsa_path_safe(const char *name) {
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

static void xx_irixsa_stream_free(void *pointer) {
    xx_irixsa_stream *stream = (xx_irixsa_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_irixsa_add(xx_irixsa_stream *stream,
                          const xx_irixsa_member *member) {
    xx_irixsa_member *grown = (xx_irixsa_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_irixsa_decode(Abstractformat *self,
                             const xx_irixsa_member *member, uint8_t **out,
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
         !xx_irixsa_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_IRIXSA_BLOCK_SIZE 512
#define XX_IRIXSA_HEADER_SIZE 512
#define XX_IRIXSA_DIRECTORY_OFFSET 0x20
#define XX_IRIXSA_ENTRY_SIZE 24
#define XX_IRIXSA_NAME_SIZE 16
#define XX_IRIXSA_MAGIC 0xACED1234U
/* The directory is a fixed table inside the header block, not a counted list:
 * 0x20 + 20 * 24 == 512 exactly. */
#define XX_IRIXSA_MAX_MEMBERS 20

static const char XX_IRIXSA_HEX_DIGITS[] = "0123456789ABCDEF";

static uint32_t xx_irixsa_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static bool xx_irixsa_is_empty_slot(const uint8_t *entry) {
    int32_t i;
    for (i = 0; i < XX_IRIXSA_ENTRY_SIZE; ++i) {
        if (entry[i] != 0U) return false;
    }
    return true;
}

/* The 16-byte name field is NUL padded. A NUL followed by a non-NUL never
 * happens in a real directory slot, and refusing that shape -- rather than
 * just stopping at the first NUL -- is one of the structural rules that stops
 * random data from parsing as a directory. */
static bool xx_irixsa_is_valid_raw_name(const uint8_t *raw) {
    bool padding = false;
    int32_t i;

    if (raw[0] == 0U) return false;
    for (i = 0; i < XX_IRIXSA_NAME_SIZE; ++i) {
        const uint8_t character = raw[i];
        if (character == 0U) {
            padding = true;
        } else if (padding) {
            return false;
        } else if ((character < 0x20U) || (character > 0x7EU)) {
            return false;
        }
    }
    return true;
}

/* Names are plain IRIX identifiers ("sash.IP22", "fx.ARCS") in practice, but
 * the field is raw bytes: path separators and the Windows-reserved
 * punctuation are escaped as %XX rather than folded to '_', because escaping
 * is reversible and cannot collapse two distinct members onto one name. */
static char *xx_irixsa_name_dup(const uint8_t *raw, int32_t index) {
    char buffer[(XX_IRIXSA_NAME_SIZE * 3) + 16];
    size_t length = 0U;
    int32_t used = 0;
    int32_t i;

    while ((used < XX_IRIXSA_NAME_SIZE) && (raw[used] != 0U)) ++used;
    /* Trailing blanks are padding rather than part of the name. */
    while ((used > 0) && (raw[used - 1] == 0x20U)) --used;

    for (i = 0; i < used; ++i) {
        const uint8_t character = raw[i];
        const bool safe = (character > 0x20U) && (character < 0x7FU) &&
                          (character != '%') && (character != '/') &&
                          (character != '\\') && (character != ':') &&
                          (character != '*') && (character != '?') &&
                          (character != '"') && (character != '<') &&
                          (character != '>') && (character != '|');
        if (safe) {
            buffer[length++] = (char)character;
        } else {
            buffer[length++] = '%';
            buffer[length++] = XX_IRIXSA_HEX_DIGITS[(character >> 4) & 0x0FU];
            buffer[length++] = XX_IRIXSA_HEX_DIGITS[character & 0x0FU];
        }
    }
    buffer[length] = '\0';
    /* A name of nothing but blanks survives the validity rules but escapes to
     * the empty string, which would be an unusable output name. */
    if (length == 0U) {
        xx_rt_snprintf(buffer, sizeof(buffer), "record%d", (int)index);
    }
    return xx_str_dup(buffer);
}

static xx_irixsa_stream *xx_irixsa_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_irixsa_stream *stream;
    uint8_t header[XX_IRIXSA_HEADER_SIZE];
    uint32_t checksum = 0U;
    int64_t total;
    int64_t span;
    int64_t end_of_data;
    int64_t archive_size;
    int32_t i;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The header block plus at least one block of payload. */
    if (span < (XX_IRIXSA_HEADER_SIZE + XX_IRIXSA_BLOCK_SIZE)) return NULL;
    if (!xx_irixsa_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_irixsa_be32(header) != XX_IRIXSA_MAGIC) return NULL;

    /* The whole 512-byte header block, summed as big-endian u32 words, must
     * come to zero; the word at 0x04 is the makeweight that makes it so. This
     * is the format's real defence against a false positive -- four magic
     * bytes on their own are far too cheap to hit by chance. Do not loosen it.
     * The sum is deliberately unsigned so it wraps modulo 2^32. */
    for (i = 0; i < XX_IRIXSA_HEADER_SIZE; i += 4) {
        checksum += xx_irixsa_be32(header + i);
    }
    if (checksum != 0U) return NULL;

    /* Slot 0 is always in use and its member always begins at block 1, the
     * block directly behind the header. A zeroed slot 0 fails here too. */
    if (xx_irixsa_be32(header + XX_IRIXSA_DIRECTORY_OFFSET +
                       XX_IRIXSA_NAME_SIZE) != 1U) {
        return NULL;
    }

    stream = (xx_irixsa_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    end_of_data = XX_IRIXSA_HEADER_SIZE;
    for (i = 0; i < XX_IRIXSA_MAX_MEMBERS; ++i) {
        xx_irixsa_member member;
        const uint8_t *entry;
        char *name;
        uint32_t block;
        uint32_t size;
        int64_t data_offset;
        int64_t data_size;
        int64_t end;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry = header + XX_IRIXSA_DIRECTORY_OFFSET +
                (XX_IRIXSA_ENTRY_SIZE * i);
        /* Unused slots are zeroed and simply skipped; a used slot may follow
         * one, so the walk never stops early. */
        if (xx_irixsa_is_empty_slot(entry)) continue;
        if (!xx_irixsa_is_valid_raw_name(entry)) goto fail;

        block = xx_irixsa_be32(entry + XX_IRIXSA_NAME_SIZE);
        size = xx_irixsa_be32(entry + XX_IRIXSA_NAME_SIZE + 4);
        /* Block 0 is the header itself, so it can never hold a member, and a
         * zero-length member is not a shape this volume ever writes: both mean
         * the slot is corrupt rather than merely unused. */
        if (block == 0U) goto fail;
        if (size == 0U) goto fail;

        data_offset = (int64_t)block * XX_IRIXSA_BLOCK_SIZE;
        data_size = (int64_t)size;
        if (!xx_irixsa_range_within(span, data_offset, data_size)) goto fail;

        name = xx_irixsa_name_dup(entry, i);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address +
                               XX_IRIXSA_DIRECTORY_OFFSET +
                               (XX_IRIXSA_ENTRY_SIZE * i);
        member.header_size = XX_IRIXSA_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = data_size;
        if (!xx_irixsa_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        end = data_offset + data_size;
        if (end > end_of_data) end_of_data = end;
    }

    if (stream->count == 0U) goto fail;

    /* The volume is padded out to a whole block past the last member. */
    archive_size = ((end_of_data + XX_IRIXSA_BLOCK_SIZE - 1) /
                    XX_IRIXSA_BLOCK_SIZE) * XX_IRIXSA_BLOCK_SIZE;
    if (archive_size > span) archive_size = span;
    stream->archive_size = archive_size;
    return stream;

fail:
    xx_irixsa_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_irixsa_init(xx_irixsa *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_IRIXSA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-irix-sa");
    xx_format_set_extension(&archive->format, "sa");
    archive->format.check_is_valid = xx_irixsa_check_is_valid;
    archive->format.handle_base_info = xx_irixsa_handle_base_info;
    archive->format.get_format_size = xx_irixsa_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_irixsa_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_irixsa_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_irixsa_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_irixsa_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_irixsa_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_irixsa_free_archive_records_reading;
    archive->format.destroy = xx_irixsa_vtable_destroy;
}

xx_irixsa *xx_irixsa_create(xx_io_device *device, int64_t base_address) {
    xx_irixsa *archive = (xx_irixsa *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_irixsa_init(archive, device, base_address);
    return archive;
}

void xx_irixsa_destroy(xx_irixsa *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_irixsa_free(xx_irixsa *archive) {
    if (!archive) return;
    xx_irixsa_destroy(archive);
    xx_mem_free(archive);
}

static void xx_irixsa_vtable_destroy(Abstractformat *self) {
    xx_irixsa_destroy((xx_irixsa *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_irixsa_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_irixsa_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_irixsa_parse(self, pd);
    if (!stream) return false;
    xx_irixsa_stream_free(stream);
    return true;
}

bool xx_irixsa_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_irixsa *archive = (xx_irixsa *)self;
    xx_irixsa_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_irixsa_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_irixsa_stream_free(stream);
    return true;
}

int64_t xx_irixsa_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_irixsa_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_irixsa *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_irixsa_set_record(xx_archive_record *record,
                                 const xx_irixsa_member *member) {
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

static bool xx_irixsa_copy_options(xx_list_s *target,
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

static const xx_var *xx_irixsa_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_irixsa_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_irixsa_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_irixsa_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_irixsa_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_irixsa_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_irixsa_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_irixsa_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_irixsa_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_irixsa_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_irixsa_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_irixsa_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_irixsa_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_irixsa_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_irixsa_stream *stream;
    const xx_irixsa_member *member;
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
    stream = (xx_irixsa_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_irixsa_path_safe(member->name)) return false;

    path_option = xx_irixsa_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_irixsa_decode(self, member, &plain, &plain_size, pd);
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
        !xx_irixsa_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_irixsa_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
