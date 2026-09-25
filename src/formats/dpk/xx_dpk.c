/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DPK4 archives.
 *
 *   header, 0x10 bytes:
 *     0x00  "DPK4", 4 bytes
 *     0x04  i32 LE total size - the length of the file itself
 *     0x08  i32 LE directory size, in bytes
 *     0x0c  i32 LE member count
 *
 *   directory at 0x10, `directory size` bytes, entries packed back to back.
 *   Entries are VARIABLE length: the record size covers the fixed part and
 *   the name, so the next entry starts `record size` bytes on rather than at
 *   a fixed stride.
 *
 *   entry, at least 0x10 bytes:
 *     0x00  i32 LE record size, >= 0x10, includes these 0x10 bytes
 *     0x04  i32 LE uncompressed size
 *     0x08  i32 LE compressed size
 *     0x0c  i32 LE data offset, from the start of the file
 *     0x10  name, (record size - 0x10) bytes, padded with NULs
 *
 * There is no method field. A member whose compressed size equals its
 * uncompressed size is stored; any other member is a zlib (RFC 1950) stream.
 * That is a derivation, not a stored value, so this reader synthesises a
 * method number: 0 for stored, 8 for zlib-wrapped Deflate, matching the
 * numbering every other container in the library uses.
 *
 * Member data lives after the directory and may be ordered freely, so the
 * archive ends at the furthest member end rather than at the last entry.
 *
 * The header's own total-size field is the format's only real self-check:
 * "DPK4" is four bytes and cheap to hit by accident, but four bytes that
 * also happen to spell the file's exact length are not.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dpk/xx_dpk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"

#include <stdio.h>

#define XX_DPK_COPY_CHUNK (64 * 1024)

typedef struct xx_dpk_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_dpk_member;

typedef struct xx_dpk_stream_s {
    xx_dpk_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_dpk_stream;

static void xx_dpk_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_dpk_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_dpk_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_dpk_path_safe(const char *name) {
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

static void xx_dpk_stream_free(void *pointer) {
    xx_dpk_stream *stream = (xx_dpk_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_dpk_add(xx_dpk_stream *stream,
                          const xx_dpk_member *member) {
    xx_dpk_member *grown = (xx_dpk_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_DPK_HEADER_SIZE 0x10
#define XX_DPK_ENTRY_MIN_SIZE 0x10
#define XX_DPK_ENTRY_STRIDE 0x14
#define XX_DPK_MAX_MEMBERS 100000
#define XX_DPK_MAX_DIRECTORY_SIZE 0x1000000
#define XX_DPK_MAX_NAME_FIELD 0x400
#define XX_DPK_METHOD_STORE 0U
#define XX_DPK_METHOD_ZLIB 8U
#define XX_DPK_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_dpk_le32(const uint8_t *data);
static xx_dpk_stream *xx_dpk_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_dpk_decode(Abstractformat *self, const xx_dpk_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Not a stride - entries are variable length. It is the average entry size
 * the count bound is measured against. */
/* The name is whatever the record size leaves over. Bounding the field keeps
 * a corrupt record size from asking for a multi-megabyte name; real DPK
 * records carry a path, not padding. */

static uint32_t xx_dpk_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_dpk_stream *xx_dpk_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic[4] = {'D', 'P', 'K', '4'};
    xx_dpk_stream *stream;
    uint8_t header[XX_DPK_HEADER_SIZE];
    uint8_t entry[XX_DPK_ENTRY_MIN_SIZE];
    uint8_t name_field[XX_DPK_MAX_NAME_FIELD];
    int64_t total;
    int64_t span;
    int64_t declared_size;
    int64_t directory_size;
    int64_t count;
    int64_t index;
    int64_t position;
    int64_t left;
    int64_t archive_end;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_DPK_HEADER_SIZE) return NULL;
    if (!xx_dpk_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;

    /* Signed on purpose throughout: a field with the top bit set is corrupt,
     * not a two-gigabyte quantity. */
    declared_size = (int64_t)(int32_t)xx_dpk_le32(header + 4);
    directory_size = (int64_t)(int32_t)xx_dpk_le32(header + 8);
    count = (int64_t)(int32_t)xx_dpk_le32(header + 0x0c);

    /* THE defence against a false positive: the header carries the length of
     * the file it sits in, so a stray "DPK4" only survives if the next four
     * bytes also spell the exact span. Loosening this to "<= span" or
     * dropping it turns this reader into a four-byte magic match. */
    if (declared_size != span) return NULL;
    if (directory_size <= 0 || directory_size > XX_DPK_MAX_DIRECTORY_SIZE) {
        return NULL;
    }
    if (count <= 0 || count > XX_DPK_MAX_MEMBERS) return NULL;
    /* Entries are variable length, so the directory cannot be indexed; this
     * bounds the count by what the directory could possibly hold. Written as
     * a division so nothing overflows. */
    if (count > directory_size / XX_DPK_ENTRY_STRIDE) return NULL;
    if (!xx_dpk_range_within(span, XX_DPK_HEADER_SIZE, directory_size)) {
        return NULL;
    }

    stream = (xx_dpk_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    archive_end = XX_DPK_HEADER_SIZE + directory_size;
    position = 0;
    left = directory_size;

    for (index = 0; index < count; ++index) {
        xx_dpk_member member;
        char *name;
        int64_t entry_offset;
        int64_t record_size;
        int64_t uncompressed_size;
        int64_t compressed_size;
        int64_t data_offset;
        int64_t name_size;
        int64_t cursor;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (left < XX_DPK_ENTRY_MIN_SIZE) goto fail;

        entry_offset = XX_DPK_HEADER_SIZE + position;
        if (!xx_dpk_read_at(self, self->base_address + entry_offset, entry,
                            sizeof(entry))) {
            goto fail;
        }

        record_size = (int64_t)(int32_t)xx_dpk_le32(entry);
        /* The record size is what advances the cursor; a record smaller than
         * the fixed part, or larger than the directory remainder, would walk
         * the loop off the directory (or never advance it at all). */
        if (record_size < XX_DPK_ENTRY_MIN_SIZE || record_size > left) {
            goto fail;
        }

        uncompressed_size = (int64_t)(int32_t)xx_dpk_le32(entry + 4);
        compressed_size = (int64_t)(int32_t)xx_dpk_le32(entry + 8);
        data_offset = (int64_t)(int32_t)xx_dpk_le32(entry + 0x0c);
        if (uncompressed_size < 0 || compressed_size < 0 || data_offset < 0) {
            goto fail;
        }
        /* A member extending past EOF is a rejection, not a truncated read. */
        if (!xx_dpk_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }

        name_size = record_size - XX_DPK_ENTRY_MIN_SIZE;
        if (name_size > XX_DPK_MAX_NAME_FIELD) goto fail;
        if (name_size > 0) {
            if (!xx_dpk_read_at(self,
                                self->base_address + entry_offset +
                                    XX_DPK_ENTRY_MIN_SIZE,
                                name_field, (size_t)name_size)) {
                goto fail;
            }
            /* The field is NUL padded to the record size rather than NUL
             * terminated, so the name is what is left after the padding. */
            while (name_size > 0 && name_field[name_size - 1] == 0U) {
                --name_size;
            }
        }
        if (name_size <= 0) goto fail;
        for (cursor = 0; cursor < name_size; ++cursor) {
            /* DPK names are plain Latin-1 paths; a field that is not printable
             * ASCII is misparsed data rather than a name. */
            if (name_field[cursor] < 0x20U || name_field[cursor] > 0x7EU) {
                goto fail;
            }
        }
        name = (char *)xx_mem_alloc((size_t)name_size + 1U);
        if (!name) goto fail;
        for (cursor = 0; cursor < name_size; ++cursor) {
            name[cursor] = (char)name_field[cursor];
        }
        name[name_size] = '\0';
        if (!xx_dpk_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + entry_offset;
        member.header_size = record_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* No method field exists: equal sizes mean stored, anything else is
         * a zlib stream. Recorded as 0 / 8 so a listing shows the method the
         * decode switch will actually use. */
        member.method = (compressed_size == uncompressed_size)
                            ? XX_DPK_METHOD_STORE
                            : XX_DPK_METHOD_ZLIB;
        if (!xx_dpk_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        /* Members need not follow the directory in order, so the archive ends
         * at the furthest extent seen, not at the last entry. */
        if (data_offset + compressed_size > archive_end) {
            archive_end = data_offset + compressed_size;
        }

        left -= record_size;
        position += record_size;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = (archive_end < span) ? archive_end : span;
    return stream;

fail:
    xx_dpk_stream_free(stream);
    return NULL;
}


/* DPK records no method number, so parse derives one: a member whose two
 * sizes agree is stored, anything else is a zlib stream. */
/* A member's uncompressed size is attacker-controlled; refuse rather than
 * attempt an allocation the container merely claims to need. */

static bool xx_dpk_decode(Abstractformat *self, const xx_dpk_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_DPK_MAX_DECODED ||
        member->compressed_size > (int64_t)XX_DPK_MAX_DECODED) {
        return false;
    }
    if ((uint64_t)member->uncompressed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    if (member->method == XX_DPK_METHOD_STORE) {
        /* Stored means the two sizes agree; a member claiming STORE with
         * unequal sizes is malformed, not a short copy to be papered over. */
        if (member->compressed_size != member->uncompressed_size) return false;
        plain = (uint8_t *)xx_mem_alloc(
            member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                           : 1U);
        if (!plain) return false;
        if (member->uncompressed_size != 0 &&
            !xx_dpk_read_at(self, member->data_offset, plain,
                            (size_t)member->uncompressed_size)) {
            xx_mem_free(plain);
            return false;
        }
        if (pd && xx_pd_is_stopped(pd)) {
            xx_mem_free(plain);
            return false;
        }
        *out = plain;
        *out_size = (size_t)member->uncompressed_size;
        return true;
    }

    /* Any method this reader does not implement fails here: treating an
     * unknown method as stored produces garbage that looks like data. */
    if (member->method != XX_DPK_METHOD_ZLIB) return false;

    /* A zlib stream is a 2-byte header plus at least one Deflate byte. */
    if (member->compressed_size < 3) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_dpk_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_zlib_stream_header_is_valid(packed,
                                        (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_zlib_stream_decode_memory(packed, (size_t)member->compressed_size,
                                      plain,
                                      (size_t)member->uncompressed_size,
                                      &written)) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    /* Exactly the promised length or nothing: a partially decoded member
     * reported as success is the one failure the caller cannot detect. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_dpk_init(xx_dpk *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_DPK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dpk");
    xx_format_set_extension(&archive->format, "dpk");
    archive->format.check_is_valid = xx_dpk_check_is_valid;
    archive->format.handle_base_info = xx_dpk_handle_base_info;
    archive->format.get_format_size = xx_dpk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_dpk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_dpk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_dpk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_dpk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_dpk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_dpk_free_archive_records_reading;
    archive->format.destroy = xx_dpk_vtable_destroy;
}

xx_dpk *xx_dpk_create(xx_io_device *device, int64_t base_address) {
    xx_dpk *archive = (xx_dpk *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_dpk_init(archive, device, base_address);
    return archive;
}

void xx_dpk_destroy(xx_dpk *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_dpk_free(xx_dpk *archive) {
    if (!archive) return;
    xx_dpk_destroy(archive);
    xx_mem_free(archive);
}

static void xx_dpk_vtable_destroy(Abstractformat *self) {
    xx_dpk_destroy((xx_dpk *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_dpk_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dpk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_dpk_parse(self, pd);
    if (!stream) return false;
    xx_dpk_stream_free(stream);
    return true;
}

bool xx_dpk_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dpk *archive = (xx_dpk *)self;
    xx_dpk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_dpk_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_dpk_stream_free(stream);
    return true;
}

int64_t xx_dpk_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_dpk_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_dpk *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_dpk_set_record(xx_archive_record *record,
                                 const xx_dpk_member *member) {
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

static bool xx_dpk_copy_options(xx_list_s *target,
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

static const xx_var *xx_dpk_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_dpk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_dpk_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_dpk_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_dpk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_dpk_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_dpk_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_dpk_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_dpk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dpk_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_dpk_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dpk_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_dpk_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_dpk_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_dpk_stream *stream;
    const xx_dpk_member *member;
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
    stream = (xx_dpk_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_dpk_path_safe(member->name)) return false;

    path_option = xx_dpk_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_dpk_decode(self, member, &plain, &plain_size, pd);
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
        !xx_dpk_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_dpk_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
