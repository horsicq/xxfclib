/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LSPack 1.0 archives (".LSP"), the Clarion-era packer.  XArchive has no
 * module for this one.  The layout was recovered from U3's "LSPack 10"
 * handler -- class zza, VMT 005fb008, recognition predicate
 * decompiled/functions/005f/005fba10.c, which tail-calls
 * decompiled/functions/005f/005fb080.c -- and then confirmed against the
 * corpus, where every member decodes to its declared size with a matching
 * CRC32.
 *
 *   there is no archive-level header; the file is a chain of members, each a
 *   40-byte header followed by three variable parts.  The header is ZIP's
 *   local file header with a different signature and ten extra bytes:
 *     0x00   4  bytes    magic 'F' 'L' 0x03 0x04
 *     0x04   2  u16 LE   version, 10
 *     0x06   2  u16 LE   flags
 *     0x08   2  u16 LE   method: 2 is raw Deflate
 *     0x0a   4  u32 LE   DOS packed date/time
 *     0x0e   4  u32 LE   CRC32 of the plaintext
 *     0x12   4  i32 LE   compressed size
 *     0x16   4  i32 LE   uncompressed size
 *     0x1a   2  u16 LE   zero
 *     0x1c   2  u16 LE   zero
 *     0x1e   2  u16 LE   zero
 *     0x20   4  u32 LE   zero
 *     0x24   2  u16 LE   file-name length, never zero
 *     0x26   2  u16 LE   directory-path length
 *   then the file name, then the backslash-separated directory path, then the
 *   compressed payload.
 *
 * U3's predicate is exactly: the magic; both size fields non-negative; the
 * four reserved fields at 0x1a, 0x1c, 0x1e and 0x20 all zero; and a non-zero
 * name length.  This reader keeps all of that, applies it to EVERY member
 * rather than just the first, and additionally requires the chain to land
 * exactly on end of file -- that exactness is what makes a four-byte magic
 * safe here.
 *
 * Payloads are raw Deflate (method 2) and are verified against both the
 * header's uncompressed size and its CRC32.  Method 0 is accepted as stored,
 * where the two size fields must agree; every other method is rejected rather
 * than guessed at.
 *
 * All 5 corpus samples in F:\ARC\ARC\LSPACK 10 parse, for 19 members, and
 * every one of them decodes with a matching CRC32.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lspack10/xx_lspack10.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"

#include <stdio.h>

#ifdef LSPACK10
#define XX_LSPACK10_FILE_TYPE XX_FILE_TYPE_LSPACK10
#else
#define XX_LSPACK10_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_LSPACK10_METHOD_STORE 0U
#define XX_LSPACK10_METHOD_DEFLATE 2U
#define XX_LSPACK10_HEADER_SIZE 40
#define XX_LSPACK10_MAX_MEMBERS 1000000U
/* A compressed member's plain size is not bounded by the file, so it gets its
 * own ceiling rather than none at all. */
#define XX_LSPACK10_MAX_PLAIN (1024ULL * 1024ULL * 1024ULL)

typedef struct xx_lspack10_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t method;
    bool has_crc;
    bool is_folder;
} xx_lspack10_member;

typedef struct xx_lspack10_stream_s {
    xx_lspack10_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_lspack10_stream;

static void xx_lspack10_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_lspack10_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_lspack10_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_lspack10_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_lspack10_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_lspack10_read_at(Abstractformat *self, int64_t offset,
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

/* Refuse anything that would escape the extraction directory. */
static bool xx_lspack10_path_safe(const char *path) {
    const char *cursor = path;

    if (!path || !path[0] || path[0] == '/') return false;
    if (path[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* Build a filesystem-safe name from raw 8-bit bytes.  Backslashes become
 * path separators, everything a filesystem would object to becomes '_'. */
static char *xx_lspack10_make_name(const uint8_t *raw, size_t size,
                                 bool keep_path) {
    char *text;
    size_t length = 0U;
    size_t index;

    if (!raw && size != 0U) return NULL;
    text = (char *)xx_mem_alloc(size + 2U);
    if (!text) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c == 0x00U) break;
        if ((c == '/' || c == '\\') && keep_path) {
            if (length != 0U && text[length - 1U] == '/') continue;
            text[length++] = '/';
            continue;
        }
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            text[length++] = '_';
        } else {
            text[length++] = (char)c;
        }
    }
    while (length != 0U &&
           (text[length - 1U] == ' ' || text[length - 1U] == '.' ||
            text[length - 1U] == '/')) {
        --length;
    }
    while (length != 0U && text[0] == '/') {
        xx_rt_memmove(text, text + 1, length - 1U);
        --length;
    }
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return text;
}

static void xx_lspack10_stream_free(void *pointer) {
    xx_lspack10_stream *stream = (xx_lspack10_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Grow the member vector one entry at a time.  The caller has already bounded
 * the member count against the real file size, so this cannot be driven to an
 * unbounded allocation by a small header. */
static bool xx_lspack10_add(xx_lspack10_stream *stream,
                          const xx_lspack10_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_lspack10_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_lspack10_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* The header splits a member's path into a base name and a backslash
 * separated directory; the archive path is the two joined back together. */
static char *xx_lspack10_member_path(const uint8_t *name, size_t name_size,
                                     const uint8_t *path, size_t path_size) {
    uint8_t *joined;
    char *result;

    if (name_size > SIZE_MAX - path_size) return NULL;
    joined = (uint8_t *)xx_mem_alloc(name_size + path_size + 1U);
    if (!joined) return NULL;
    if (path_size != 0U) xx_mem_copy(joined, path, path_size);
    if (name_size != 0U) xx_mem_copy(joined + path_size, name, name_size);
    result = xx_lspack10_make_name(joined, path_size + name_size, true);
    xx_mem_free(joined);
    return result;
}


/* --------------------------------------------------------------- parse -- */

static xx_lspack10_stream *xx_lspack10_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_lspack10_stream *stream = NULL;
    uint8_t *names = NULL;
    int64_t total;
    int64_t span;
    int64_t cursor;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_LSPACK10_HEADER_SIZE + 1) return NULL;

    stream = (xx_lspack10_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    cursor = 0;
    while (cursor < span) {
        uint8_t header[XX_LSPACK10_HEADER_SIZE];
        xx_lspack10_member member;
        uint32_t method;
        uint32_t name_size;
        uint32_t path_size;
        int64_t packed;
        int64_t plain;
        int64_t variable;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= XX_LSPACK10_MAX_MEMBERS) goto fail;
        if (span - cursor < XX_LSPACK10_HEADER_SIZE) goto fail;
        if (!xx_lspack10_read_at(self, self->base_address + cursor, header,
                                 sizeof(header))) {
            goto fail;
        }
        /* The magic repeats on every member, not just the first; that is what
         * makes a truncated or spliced chain fail closed. */
        if (header[0] != 'F' || header[1] != 'L' || header[2] != 0x03U ||
            header[3] != 0x04U) {
            goto fail;
        }
        /* U3's reserved-field gate, applied to every member. */
        if (xx_lspack10_le16(header + 0x1a) != 0U ||
            xx_lspack10_le16(header + 0x1c) != 0U ||
            xx_lspack10_le16(header + 0x1e) != 0U ||
            xx_lspack10_le32(header + 0x20) != 0U) {
            goto fail;
        }

        method = xx_lspack10_le16(header + 8);
        packed = (int64_t)(int32_t)xx_lspack10_le32(header + 0x12);
        plain = (int64_t)(int32_t)xx_lspack10_le32(header + 0x16);
        name_size = xx_lspack10_le16(header + 0x24);
        path_size = xx_lspack10_le16(header + 0x26);
        if (packed < 0 || plain < 0 || name_size == 0U) goto fail;
        if (method != XX_LSPACK10_METHOD_STORE &&
            method != XX_LSPACK10_METHOD_DEFLATE) {
            goto fail;
        }
        if (method == XX_LSPACK10_METHOD_STORE && packed != plain) goto fail;
        if ((uint64_t)plain > XX_LSPACK10_MAX_PLAIN) goto fail;

        /* The names and the payload are bounded against what is left of the
         * file before either is read or the cursor advances. */
        variable = (int64_t)name_size + (int64_t)path_size;
        if (variable > span - cursor - XX_LSPACK10_HEADER_SIZE) goto fail;
        if (packed > span - cursor - XX_LSPACK10_HEADER_SIZE - variable) {
            goto fail;
        }

        names = (uint8_t *)xx_mem_alloc((size_t)variable);
        if (!names) goto fail;
        if (!xx_lspack10_read_at(
                self, self->base_address + cursor + XX_LSPACK10_HEADER_SIZE,
                names, (size_t)variable)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_lspack10_member_path(names, (size_t)name_size,
                                              names + name_size,
                                              (size_t)path_size);
        xx_mem_free(names);
        names = NULL;
        if (!member.name) goto fail;
        member.header_offset = self->base_address + cursor;
        member.header_size = XX_LSPACK10_HEADER_SIZE + variable;
        member.data_offset =
            self->base_address + cursor + member.header_size;
        member.packed_size = packed;
        member.unpacked_size = (uint64_t)plain;
        member.method = method;
        member.crc32 = xx_lspack10_le32(header + 0x0e);
        member.has_crc = true;
        if (!xx_lspack10_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        cursor += member.header_size + packed;
    }

    /* There is no terminator: the chain ends by landing exactly on EOF. */
    if (cursor != span || stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    if (names) xx_mem_free(names);
    xx_lspack10_stream_free(stream);
    return NULL;
}

/* Method 2 is raw Deflate; method 0 is stored.  Either way the header's
 * uncompressed size and CRC32 are the anchors, and a decode that misses
 * either is an error rather than a result to publish. */
static bool xx_lspack10_decode(Abstractformat *self,
                               const xx_lspack10_member *member,
                               uint8_t **out, size_t *out_size,
                               xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->unpacked_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->packed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (member->packed_size != 0) {
        packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
        if (!packed) return false;
        if (!xx_lspack10_read_at(self, member->data_offset, packed,
                                 (size_t)member->packed_size)) {
            xx_mem_free(packed);
            return false;
        }
    }
    if (member->method == XX_LSPACK10_METHOD_STORE) {
        if ((uint64_t)member->packed_size != member->unpacked_size) {
            xx_mem_free(packed);
            return false;
        }
        plain = packed;
        packed = NULL;
        written = (size_t)member->unpacked_size;
    } else {
        plain = (uint8_t *)xx_mem_alloc(
            member->unpacked_size != 0U ? (size_t)member->unpacked_size : 1U);
        if (!plain ||
            !xx_deflate_decompress_memory(packed,
                                          (size_t)member->packed_size, plain,
                                          (size_t)member->unpacked_size,
                                          &written, false) ||
            written != member->unpacked_size) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
        xx_mem_free(packed);
    }
    if (member->has_crc &&
        xx_crc32_calc(0U, plain, written) != member->crc32) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_lspack10_init(xx_lspack10 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LSPACK10_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lspack");
    xx_format_set_extension(&archive->format, "lsp");
    archive->format.check_is_valid = xx_lspack10_check_is_valid;
    archive->format.handle_base_info = xx_lspack10_handle_base_info;
    archive->format.get_format_size = xx_lspack10_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lspack10_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lspack10_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lspack10_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lspack10_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lspack10_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lspack10_free_archive_records_reading;
    archive->format.destroy = xx_lspack10_vtable_destroy;
}

xx_lspack10 *xx_lspack10_create(xx_io_device *device, int64_t base_address) {
    xx_lspack10 *archive = (xx_lspack10 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lspack10_init(archive, device, base_address);
    return archive;
}

void xx_lspack10_destroy(xx_lspack10 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_lspack10_free(xx_lspack10 *archive) {
    if (!archive) return;
    xx_lspack10_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lspack10_vtable_destroy(Abstractformat *self) {
    xx_lspack10_destroy((xx_lspack10 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_lspack10_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lspack10_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_lspack10_parse(self, pd);
    if (!stream) return false;
    xx_lspack10_stream_free(stream);
    return true;
}

bool xx_lspack10_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lspack10 *archive = (xx_lspack10 *)self;
    xx_lspack10_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_lspack10_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_lspack10_stream_free(stream);
    return true;
}

int64_t xx_lspack10_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lspack10_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_lspack10 *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_lspack10_set_record(xx_archive_record *record,
                                 const xx_lspack10_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (member->has_crc &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                        member->crc32)) {
        return false;
    }
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_lspack10_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
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

static const xx_var *xx_lspack10_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_lspack10_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_lspack10_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_lspack10_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_lspack10_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_lspack10_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_lspack10_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_lspack10_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_lspack10_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lspack10_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_lspack10_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lspack10_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_lspack10_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lspack10_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_lspack10_stream *stream;
    const xx_lspack10_member *member;
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
    stream = (xx_lspack10_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_lspack10_path_safe(member->name)) return false;

    path_option =
        xx_lspack10_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_lspack10_decode(self, member, &plain, &plain_size, pd);
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

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_lspack10_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_lspack10_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
