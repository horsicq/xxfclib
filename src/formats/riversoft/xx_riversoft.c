/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RiverSoft Data Library (.rdl) archives.
 *
 * Three parts, in file order: header, payloads, directory. The directory is
 * at the END of the file and its size is implied by the header count, so the
 * file has no room for trailing slack -- that is what makes the layout
 * self-checking.
 *
 * Header, 0x20 bytes at offset 0:
 *   +0x00  "RiverSoft Data Library\x1a"   23 bytes, no terminator
 *   +0x17  u8     major version, 1 in every known archive
 *   +0x18  u8     unused
 *   +0x19  u8     minor version, 1 in every known archive
 *   +0x1a  u16le  number of directory records, at least 1
 *   +0x1c  u32le  total uncompressed size of all members
 *
 * Directory record, 0x15 bytes; the directory begins at
 * file_size - count * 0x15 and runs to end of file:
 *   +0x00  u8     name length, 1..12
 *   +0x01  12 bytes name, padding after the length is ignored
 *                  (a Turbo Pascal ShortString[12] field, 13 bytes total)
 *   +0x0d  u32le  payload offset, absolute in the file
 *   +0x11  u16le  compressed size
 *   +0x13  u16le  uncompressed size
 *
 * There is no method field. A member with both sizes zero is empty and is
 * reported as stored; every other member is a PKWARE DCL ("implode") stream.
 * Both sizes are u16, so no single member exceeds 64 KiB - 1.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/riversoft/xx_riversoft.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_RIVERSOFT_COPY_CHUNK (64 * 1024)

typedef struct xx_riversoft_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_riversoft_member;

typedef struct xx_riversoft_stream_s {
    xx_riversoft_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_riversoft_stream;

static void xx_riversoft_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_riversoft_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_riversoft_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_riversoft_path_safe(const char *name) {
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

static void xx_riversoft_stream_free(void *pointer) {
    xx_riversoft_stream *stream = (xx_riversoft_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_riversoft_add(xx_riversoft_stream *stream,
                          const xx_riversoft_member *member) {
    xx_riversoft_member *grown = (xx_riversoft_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_RIVERSOFT_HEADER_SIZE 0x20
#define XX_RIVERSOFT_MAGIC_SIZE 23
#define XX_RIVERSOFT_COUNT_OFFSET 0x1a
#define XX_RIVERSOFT_ENTRY_SIZE 0x15
#define XX_RIVERSOFT_NAME_FIELD 13
#define XX_RIVERSOFT_MAX_NAME 12
#define XX_RIVERSOFT_OFFSET_FIELD 0x0d
#define XX_RIVERSOFT_MAX_MEMBERS 0x10000
#define XX_RIVERSOFT_METHOD_STORE 0U
#define XX_RIVERSOFT_METHOD_DCL 1U
#define XX_RIVERSOFT_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_riversoft_le16(const uint8_t *data);
static uint32_t xx_riversoft_le32(const uint8_t *data);
static bool xx_riversoft_name_valid(const char *name, int32_t size);
static xx_riversoft_stream *xx_riversoft_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_riversoft_decode(Abstractformat *self, const xx_riversoft_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Turbo Pascal ShortString[12]: one length byte plus a fixed 12-byte body. */
/* The record count is a u16, so it cannot exceed this; the cap keeps a later
 * widening of the field from becoming an unbounded walk. */

static uint16_t xx_riversoft_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_riversoft_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Names are DOS 8.3 names written by a Pascal program: printable ASCII with
 * the characters DOS itself forbids in a filename excluded. Nothing in the
 * format permits a byte outside 0x20..0x7e, and no member name carries a
 * path separator, so a name is always a single flat component. */
static bool xx_riversoft_name_valid(const char *name, int32_t size) {
    int32_t index;

    for (index = 0; index < size; ++index) {
        uint8_t value = (uint8_t)name[index];
        if (value < 0x20U || value > 0x7eU) return false;
        if (value == (uint8_t)'"' || value == (uint8_t)'*' ||
            value == (uint8_t)'<' || value == (uint8_t)'>' ||
            value == (uint8_t)'?' || value == (uint8_t)'|' ||
            value == (uint8_t)':' || value == (uint8_t)'/' ||
            value == (uint8_t)'\\') {
            return false;
        }
    }
    return true;
}

static xx_riversoft_stream *xx_riversoft_parse(Abstractformat *self,
                                               xx_pd_struct *pd) {
    uint8_t header[XX_RIVERSOFT_HEADER_SIZE];
    xx_riversoft_stream *stream = NULL;
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int32_t count;
    int32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)(XX_RIVERSOFT_HEADER_SIZE + XX_RIVERSOFT_ENTRY_SIZE)) {
        return NULL;
    }
    if (!xx_riversoft_read_at(self, self->base_address, header,
                              sizeof(header))) {
        return NULL;
    }

    /* The 22-character title plus the 0x1a terminator. This is the whole of
     * the format's signature -- there is no checksum and no second constant
     * field -- so shortening the comparison, or dropping the 0x1a because it
     * looks like padding, is what would let arbitrary text files through. */
    if (xx_rt_memcmp(header, "RiverSoft Data Library\x1a",
                     XX_RIVERSOFT_MAGIC_SIZE) != 0) {
        return NULL;
    }
    /* Version bytes live at +0x17 and +0x19 and are 1.1 in every archive
     * seen; they are deliberately not enforced, as a 1.2 library would still
     * have this layout. */

    count = (int32_t)xx_riversoft_le16(header + XX_RIVERSOFT_COUNT_OFFSET);
    /* An empty library is rejected: with no records the 23-byte magic would
     * be the only thing checked, and nothing structural could be. */
    if (count <= 0 || count > XX_RIVERSOFT_MAX_MEMBERS) return NULL;

    directory_size = (int64_t)count * XX_RIVERSOFT_ENTRY_SIZE;
    /* The directory is anchored to EOF, not pointed at by the header, so the
     * count and the file length have to agree exactly. Together with the
     * per-record checks below this is the format's real structural defence:
     * a file whose size is not header + payloads + count*0x15 cannot parse. */
    directory_offset = span - directory_size;
    if (directory_offset < XX_RIVERSOFT_HEADER_SIZE) return NULL;

    stream = (xx_riversoft_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0; index < count; ++index) {
        uint8_t record[XX_RIVERSOFT_ENTRY_SIZE];
        xx_riversoft_member member;
        int64_t record_offset;
        int64_t data_offset;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int32_t name_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        record_offset = directory_offset + (int64_t)index *
                                               XX_RIVERSOFT_ENTRY_SIZE;
        if (!xx_riversoft_range_within(span, record_offset,
                                       XX_RIVERSOFT_ENTRY_SIZE)) {
            goto fail;
        }
        if (!xx_riversoft_read_at(self, self->base_address + record_offset,
                                  record, sizeof(record))) {
            goto fail;
        }

        /* The length byte is the sole authority over the 13-byte name field;
         * the bytes after it are stale padding and must not be looked at. */
        name_size = (int32_t)record[0];
        if (name_size < 1 || name_size > XX_RIVERSOFT_MAX_NAME) goto fail;

        data_offset = (int64_t)xx_riversoft_le32(record +
                                                 XX_RIVERSOFT_OFFSET_FIELD);
        compressed_size =
            (int64_t)xx_riversoft_le16(record + XX_RIVERSOFT_OFFSET_FIELD + 4);
        uncompressed_size =
            (int64_t)xx_riversoft_le16(record + XX_RIVERSOFT_OFFSET_FIELD + 6);

        /* Payloads live strictly between the header and the directory: they
         * may not overlap either. Checking against directory_offset rather
         * than the span is what keeps a member from being read out of the
         * directory it was described by. */
        if (data_offset < XX_RIVERSOFT_HEADER_SIZE ||
            data_offset > directory_offset ||
            compressed_size > directory_offset - data_offset) {
            goto fail;
        }
        if (!xx_riversoft_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }
        /* No compressed bytes cannot yield a non-empty member; the reverse
         * (a few bytes that decode to nothing) is a legal DCL stream. */
        if (compressed_size == 0 && uncompressed_size != 0) goto fail;

        name = (char *)xx_mem_alloc((size_t)name_size + 1U);
        if (!name) goto fail;
        xx_rt_memcpy(name, record + 1, (size_t)name_size);
        name[name_size] = '\0';
        if (!xx_riversoft_name_valid(name, name_size)) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + record_offset;
        member.header_size = XX_RIVERSOFT_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* No method field exists. An entry with both sizes zero is an empty
         * member and needs no decoder; everything else is a DCL stream. */
        member.method = (compressed_size == 0 && uncompressed_size == 0)
                            ? XX_RIVERSOFT_METHOD_STORE
                            : XX_RIVERSOFT_METHOD_DCL;
        member.timestamp = 0U; /* the format records no timestamps */
        member.is_folder = false;

        if (!xx_riversoft_add(stream, &member)) goto fail;
        name = NULL; /* owned by the stream now */
    }

    if (pd && xx_pd_is_stopped(pd)) goto fail;
    /* The directory ends at EOF by construction, so the archive is the whole
     * span; there is no overlay to leave behind. */
    stream->archive_size = span;
    return stream;

fail:
    xx_str_free(name);
    xx_riversoft_stream_free(stream);
    return NULL;
}


/* The container has no method field: these two values are this reader's own,
 * derived in parse from the stored sizes, and the switch below is the only
 * place that derivation is acted on. */
/* Both size fields are u16, so a well-formed member can never approach this;
 * the cap exists so a future widening of the fields cannot turn an
 * attacker-chosen length into an unbounded allocation. */

static bool xx_riversoft_decode(Abstractformat *self,
                                const xx_riversoft_member *member,
                                uint8_t **out, size_t *out_size,
                                xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || (pd && xx_pd_is_stopped(pd))) return false;
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_RIVERSOFT_MAX_DECODED ||
        member->compressed_size < 0) {
        return false;
    }

    if (member->method == XX_RIVERSOFT_METHOD_STORE) {
        /* Stored here means genuinely empty -- parse only assigns this method
         * when both sizes are zero -- so there is nothing to read. */
        if (member->uncompressed_size != 0 || member->compressed_size != 0) {
            return false;
        }
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }
    if (member->method != XX_RIVERSOFT_METHOD_DCL) {
        /* An unrecognised method must fail rather than fall through to a
         * verbatim copy: a DCL stream copied out raw looks like plausible
         * data to every caller. */
        return false;
    }
    if (member->compressed_size == 0) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_riversoft_read_at(self, member->data_offset, packed,
                              (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(member->uncompressed_size != 0
                                        ? (size_t)member->uncompressed_size
                                        : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);
    /* A short decode is a failure, not a partial success: the directory size
     * is the only record of how long the member is, so a caller handed fewer
     * bytes has no way to notice. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_riversoft_init(xx_riversoft *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_RIVERSOFT;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-riversoft-rdl");
    xx_format_set_extension(&archive->format, "rdl");
    archive->format.check_is_valid = xx_riversoft_check_is_valid;
    archive->format.handle_base_info = xx_riversoft_handle_base_info;
    archive->format.get_format_size = xx_riversoft_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_riversoft_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_riversoft_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_riversoft_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_riversoft_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_riversoft_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_riversoft_free_archive_records_reading;
    archive->format.destroy = xx_riversoft_vtable_destroy;
}

xx_riversoft *xx_riversoft_create(xx_io_device *device, int64_t base_address) {
    xx_riversoft *archive = (xx_riversoft *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_riversoft_init(archive, device, base_address);
    return archive;
}

void xx_riversoft_destroy(xx_riversoft *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_riversoft_free(xx_riversoft *archive) {
    if (!archive) return;
    xx_riversoft_destroy(archive);
    xx_mem_free(archive);
}

static void xx_riversoft_vtable_destroy(Abstractformat *self) {
    xx_riversoft_destroy((xx_riversoft *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_riversoft_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_riversoft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_riversoft_parse(self, pd);
    if (!stream) return false;
    xx_riversoft_stream_free(stream);
    return true;
}

bool xx_riversoft_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_riversoft *archive = (xx_riversoft *)self;
    xx_riversoft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_riversoft_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_riversoft_stream_free(stream);
    return true;
}

int64_t xx_riversoft_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_riversoft_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_riversoft *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_riversoft_set_record(xx_archive_record *record,
                                 const xx_riversoft_member *member) {
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

static bool xx_riversoft_copy_options(xx_list_s *target,
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

static const xx_var *xx_riversoft_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_riversoft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_riversoft_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_riversoft_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_riversoft_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_riversoft_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_riversoft_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_riversoft_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_riversoft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_riversoft_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_riversoft_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_riversoft_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_riversoft_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_riversoft_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_riversoft_stream *stream;
    const xx_riversoft_member *member;
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
    stream = (xx_riversoft_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_riversoft_path_safe(member->name)) return false;

    path_option = xx_riversoft_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_riversoft_decode(self, member, &plain, &plain_size, pd);
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
        !xx_riversoft_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_riversoft_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
