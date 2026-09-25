/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Genius Library (.gpl) archives.
 *
 *   header, 0x20 bytes, little-endian:
 *     0x00  14   ASCII "GENIUS LIBRARY"
 *     0x0E  1    NUL terminating the signature
 *     0x0F  1    unused
 *     0x10  u16  0x0000 in the whole reference corpus
 *     0x12  u16  uninitialised writer memory - fragments of unrelated
 *                strings, 22 distinct values over 44 files.  Not a version:
 *                neither validated nor published.
 *     0x14  u32  member count
 *
 *   then, for each member in turn, an inline record followed immediately by
 *   that member's data:
 *     +0x00  u64  offset at which this member's data starts, absolute
 *     +0x08  u64  compressed size
 *     +0x10  u64  uncompressed size
 *     +0x28  u64  write time
 *     +0x33  u8   method: 0 = stored, 1 = PKWARE DCL "implode" blocks
 *     +0x34  u16  length of the name that follows the record
 *     +0x36  ...  the name, counted, with its NUL inside the count
 *     ...    ...  compressed size bytes of member data
 *
 * A method-1 member is not a single DCL stream.  Its plaintext is cut into
 * 4096-byte pieces and each piece is a COMPLETE DCL stream with its own
 * dictionary, framed as
 *
 *     repeat until 8 bytes are left:
 *       u32  packed length of the block
 *       ...  that many bytes, one complete DCL stream
 *     u32  plaintext length of the whole member
 *     u32  CRC-32 of the plaintext
 *
 * The block's own end-of-stream code says where its plaintext ends, so a
 * block's unpacked length is never stored.
 *
 * The archive ends where the last member's data ends; anything after that is
 * an overlay and is not claimed.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/genius/xx_genius.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/genius/xx_genius.h"

#include <stdio.h>

#define XX_GENIUS_COPY_CHUNK (64 * 1024)

typedef struct xx_genius_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_genius_member;

typedef struct xx_genius_stream_s {
    xx_genius_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_genius_stream;

static void xx_genius_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_genius_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_genius_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_genius_path_safe(const char *name) {
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

static void xx_genius_stream_free(void *pointer) {
    xx_genius_stream *stream = (xx_genius_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_genius_add(xx_genius_stream *stream,
                          const xx_genius_member *member) {
    xx_genius_member *grown = (xx_genius_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_GENIUS_HEADER_SIZE 0x20
#define XX_GENIUS_ENTRY_SIZE 0x36
#define XX_GENIUS_SIGNATURE_SIZE 14
#define XX_GENIUS_COUNT_OFFSET 0x14
#define XX_GENIUS_ENTRY_COMPRESSED 0x08
#define XX_GENIUS_ENTRY_UNCOMPRESSED 0x10
#define XX_GENIUS_ENTRY_TIME 0x28
#define XX_GENIUS_ENTRY_METHOD 0x33
#define XX_GENIUS_ENTRY_NAMESIZE 0x34
#define XX_GENIUS_MAX_MEMBERS 100000
#define XX_GENIUS_MAX_NAME_SIZE 0x400
#define XX_GENIUS_MAX_MEMBER_SIZE (512 * 1024 * 1024)
#define XX_GENIUS_MAX_DECODED (256 * 1024 * 1024)
#define XX_GENIUS_METHOD_STORE 0U
#define XX_GENIUS_METHOD_DCL_BLOCKS 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_genius_le16(const uint8_t *data);
static uint32_t xx_genius_le32(const uint8_t *data);
static uint64_t xx_genius_le64(const uint8_t *data);
static bool xx_genius_validate_name(uint8_t *field, size_t size);
static xx_genius_stream *xx_genius_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_genius_decode(Abstractformat *self, const xx_genius_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The reference implementation rejects a name length outside 1..0x400. */
/* Member sizes are 64-bit fields, so they have to be bounded before they are
 * used as buffer sizes. */

static uint32_t xx_genius_le16(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8);
}

static uint32_t xx_genius_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t xx_genius_le64(const uint8_t *data) {
    return (uint64_t)xx_genius_le32(data) |
           ((uint64_t)xx_genius_le32(data + 4) << 32);
}

/* The name field is counted, not NUL terminated, but the writer stores the NUL
 * inside the count; the name is the run in front of it.  Nothing in the
 * reference corpus carries a path component, so a separator is a rejection
 * rather than something to strip: a name this reader cannot publish verbatim
 * is not a name it understands.
 *
 * Validates @p field in place and terminates it after the name; @p field must
 * therefore hold @p size + 1 bytes. */
static bool xx_genius_validate_name(uint8_t *field, size_t size) {
    size_t length = 0U;
    size_t index;

    while (length < size && field[length] != 0U) ++length;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t value = field[index];

        if (value < 0x20U || value > 0x7eU) return false;
        switch (value) {
            case '\\':
            case '/':
            case ':':
            case '*':
            case '?':
            case '"':
            case '<':
            case '>':
            case '|':
                return false;
            default:
                break;
        }
    }
    field[length] = 0U;
    return true;
}

static xx_genius_stream *xx_genius_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    static const uint8_t signature[XX_GENIUS_SIGNATURE_SIZE] = {
        'G', 'E', 'N', 'I', 'U', 'S', ' ', 'L', 'I', 'B', 'R', 'A', 'R', 'Y'};
    xx_genius_stream *stream = NULL;
    uint8_t header[XX_GENIUS_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    uint32_t count;
    uint32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_GENIUS_HEADER_SIZE + XX_GENIUS_ENTRY_SIZE) return NULL;
    if (!xx_genius_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* The signature and the NUL behind it are the primary gate: fifteen fixed
     * bytes. */
    if (xx_rt_memcmp(header, signature, XX_GENIUS_SIGNATURE_SIZE) != 0) {
        return NULL;
    }
    if (header[XX_GENIUS_SIGNATURE_SIZE] != 0U) return NULL;

    count = xx_genius_le32(header + XX_GENIUS_COUNT_OFFSET);
    if (count < 1U || count > (uint32_t)XX_GENIUS_MAX_MEMBERS) return NULL;

    stream = (xx_genius_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_GENIUS_HEADER_SIZE;
    for (index = 0U; index < count; ++index) {
        uint8_t entry[XX_GENIUS_ENTRY_SIZE];
        uint8_t *name_field = NULL;
        char *name = NULL;
        xx_genius_member member;
        int64_t declared_offset;
        int64_t compressed;
        int64_t uncompressed;
        int64_t header_size;
        int64_t data_offset;
        uint32_t name_size;
        bool added;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_genius_range_within(span, offset, XX_GENIUS_ENTRY_SIZE)) {
            goto fail;
        }
        if (!xx_genius_read_at(self, self->base_address + offset, entry,
                               sizeof(entry))) {
            goto fail;
        }
        declared_offset = (int64_t)xx_genius_le64(entry);
        compressed =
            (int64_t)xx_genius_le64(entry + XX_GENIUS_ENTRY_COMPRESSED);
        uncompressed =
            (int64_t)xx_genius_le64(entry + XX_GENIUS_ENTRY_UNCOMPRESSED);
        name_size = xx_genius_le16(entry + XX_GENIUS_ENTRY_NAMESIZE);

        /* These are u64 fields read into a signed type; a value with the top
         * bit set arrives negative and must be refused before it is used as a
         * length. */
        if (compressed < 0 || uncompressed < 0) goto fail;
        if (compressed > XX_GENIUS_MAX_MEMBER_SIZE ||
            uncompressed > XX_GENIUS_MAX_MEMBER_SIZE) {
            goto fail;
        }
        if (name_size < 1U || name_size > (uint32_t)XX_GENIUS_MAX_NAME_SIZE) {
            goto fail;
        }
        if (!xx_genius_range_within(span, offset + XX_GENIUS_ENTRY_SIZE,
                                    (int64_t)name_size)) {
            goto fail;
        }

        name_field = (uint8_t *)xx_mem_alloc((size_t)name_size + 1U);
        if (!name_field) goto fail;
        if (!xx_genius_read_at(self,
                               self->base_address + offset +
                                   XX_GENIUS_ENTRY_SIZE,
                               name_field, (size_t)name_size)) {
            xx_mem_free(name_field);
            goto fail;
        }
        if (!xx_genius_validate_name(name_field, (size_t)name_size)) {
            xx_mem_free(name_field);
            goto fail;
        }
        /* Duplicated through xx_str_dup so the stream owns it with the same
         * allocator its free path uses. */
        name = xx_str_dup((const char *)name_field);
        xx_mem_free(name_field);
        if (!name) goto fail;
        if (!xx_genius_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        header_size = XX_GENIUS_ENTRY_SIZE + (int64_t)name_size;
        data_offset = offset + header_size;
        /* The entry states where its own data starts, and it has to be exactly
         * where the walk already is.  This equality on every entry is the only
         * thing that makes the chain self-checking: without it a file merely
         * beginning with the signature would be walked using attacker-chosen
         * sizes until one of them happened to fall off the end. */
        if (declared_offset != data_offset) {
            xx_str_free(name);
            goto fail;
        }
        if (!xx_genius_range_within(span, data_offset, compressed)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = header_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed;
        member.uncompressed_size = uncompressed;
        /* Kept raw: the container's own method byte, so a listing shows what
         * the archive says rather than a library enum. */
        member.method = (uint32_t)entry[XX_GENIUS_ENTRY_METHOD];
        /* A 64-bit write time whose epoch the reference does not interpret
         * either; published unchanged. */
        member.timestamp = xx_genius_le64(entry + XX_GENIUS_ENTRY_TIME);
        member.is_folder = false;
        added = xx_genius_add(stream, &member);
        if (!added) {
            xx_str_free(name);
            goto fail;
        }
        if (stream->count > (size_t)XX_GENIUS_MAX_MEMBERS) goto fail;

        offset = data_offset + compressed;
    }

    if (stream->count == 0U) goto fail;
    /* The chain, not the file, defines the archive: bytes behind the last
     * member's data are an overlay. */
    stream->archive_size = offset;
    return stream;

fail:
    xx_genius_stream_free(stream);
    return NULL;
}


static bool xx_genius_decode(Abstractformat *self,
                             const xx_genius_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t packed_size;
    size_t plain_size;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    /* The parse publishes the container's own method byte unchanged, so a
     * listing shows what the archive actually says.  Mapping it happens here
     * and only here - and a method this reader does not implement is a
     * refusal, not a fall-through to a stored copy, which would hand the
     * caller compressed bytes dressed up as content. */
    if (member->method != XX_GENIUS_METHOD_STORE &&
        member->method != XX_GENIUS_METHOD_DCL_BLOCKS) {
        return false;
    }
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if ((uint64_t)member->compressed_size > (uint64_t)XX_GENIUS_MAX_DECODED ||
        (uint64_t)member->uncompressed_size >
            (uint64_t)XX_GENIUS_MAX_DECODED) {
        return false;
    }
    /* A stored member is the plaintext verbatim, so the two sizes have to
     * agree.  When they do not, the record is not something this reader can
     * reproduce and must not be published as stored bytes. */
    if (member->method == XX_GENIUS_METHOD_STORE &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    plain_size = (size_t)member->uncompressed_size;
    packed_size = (size_t)member->compressed_size;
    if (plain_size == 0U) {
        /* Only a stored member can legitimately be empty: the block framing
         * always carries at least its 8-byte trailer. */
        if (member->method != XX_GENIUS_METHOD_STORE || packed_size != 0U) {
            return false;
        }
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }
    if (packed_size == 0U) return false;

    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed) return false;
    if (!xx_genius_read_at(self, member->data_offset, packed, packed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (member->method == XX_GENIUS_METHOD_STORE) {
        xx_rt_memcpy(plain, packed, plain_size);
        written = plain_size;
    } else if (!xx_genius_decode_memory(packed, packed_size, plain, plain_size,
                                        &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);
    /* The trailer repeats the plaintext length and carries its CRC-32, both of
     * which the block decoder checks; this catches a directory whose declared
     * size disagrees with the framing. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_genius_init(xx_genius *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_GENIUS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-genius-library");
    xx_format_set_extension(&archive->format, "gpl");
    archive->format.check_is_valid = xx_genius_check_is_valid;
    archive->format.handle_base_info = xx_genius_handle_base_info;
    archive->format.get_format_size = xx_genius_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_genius_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_genius_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_genius_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_genius_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_genius_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_genius_free_archive_records_reading;
    archive->format.destroy = xx_genius_vtable_destroy;
}

xx_genius *xx_genius_create(xx_io_device *device, int64_t base_address) {
    xx_genius *archive = (xx_genius *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_genius_init(archive, device, base_address);
    return archive;
}

void xx_genius_destroy(xx_genius *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_genius_free(xx_genius *archive) {
    if (!archive) return;
    xx_genius_destroy(archive);
    xx_mem_free(archive);
}

static void xx_genius_vtable_destroy(Abstractformat *self) {
    xx_genius_destroy((xx_genius *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_genius_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_genius_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_genius_parse(self, pd);
    if (!stream) return false;
    xx_genius_stream_free(stream);
    return true;
}

bool xx_genius_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_genius *archive = (xx_genius *)self;
    xx_genius_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_genius_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_genius_stream_free(stream);
    return true;
}

int64_t xx_genius_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_genius_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_genius *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_genius_set_record(xx_archive_record *record,
                                 const xx_genius_member *member) {
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

static bool xx_genius_copy_options(xx_list_s *target,
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

static const xx_var *xx_genius_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_genius_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_genius_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_genius_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_genius_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_genius_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_genius_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_genius_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_genius_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_genius_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_genius_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_genius_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_genius_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_genius_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_genius_stream *stream;
    const xx_genius_member *member;
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
    stream = (xx_genius_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_genius_path_safe(member->name)) return false;

    path_option = xx_genius_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_genius_decode(self, member, &plain, &plain_size, pd);
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
        !xx_genius_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_genius_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
