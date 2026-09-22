/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BSA archives (".BSN"), the DOS-era Russian archiver.  Ported from
 * XArchive's games/xbsn.cpp and cross-checked against U3's BSN handler
 * (class sla, VMT 0051da58).  All multi-byte fields are BIG endian.
 *
 *   archive header, 6 bytes at offset 0:
 *     0x00   4  u32 BE   magic ff 42 53 47  (0xff "BSG")
 *     0x04   2  u16 BE   version; only 0x0000 and 0x0001 exist
 *
 *   then a chain of members, each:
 *     +0x00  4  u32 BE   magic ff 42 53 41  (0xff "BSA")
 *     +0x04  2  u16 BE   header body size
 *     body, exactly that many bytes:
 *       +0x00  4  u32 BE   attributes; bit 0x02000000 = directory,
 *                          bit 0x08000000 = stored
 *       +0x04  4  u32 BE   DOS timestamp
 *       +0x08  n  char[n] name, '/' separated, NOT NUL terminated inside n
 *       +0x08+n 1 u8      NUL
 *       +..    4  u32 BE   uncompressed size
 *       +..    4  u32 BE   compressed size
 *       +..    4  u32 BE   CRC32 of the plaintext
 *     then 4 bytes u32 BE: CRC32 of the header body
 *     then the payload, `compressed size` bytes
 *
 *   the archive ends on a two-byte 00 00 trailer.  A member can never be
 *   mistaken for it because every member opens with the 0xff magic byte.
 *
 * The decisive recognition gate is the per-header CRC32: the body length and
 * the ASCIIZ name must agree exactly (body = 21 + strlen(name)) and the
 * stored CRC must match, which makes a false positive a ~2^-32 event.  Every
 * declared extent is bounded against the real file size before the cursor
 * advances.
 *
 * COMPRESSION IS NOT IMPLEMENTED.  BSA's compressed members use a solid LZ
 * scheme whose dictionary is carried across member boundaries, so decoding
 * member k requires replaying members 0..k-1.  Listing is complete and exact;
 * stored members (attribute bit 0x08000000) and directories extract, and a
 * compressed member fails closed rather than emitting a plausible-looking
 * wrong result.
 *
 * All 3 corpus samples in F:\ARC\ARC\BSN parse.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bsn/xx_bsn.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/crc/xx_crc.h"

#include <stdio.h>

#ifdef BSN
#define XX_BSN_FILE_TYPE XX_FILE_TYPE_BSN
#else
#define XX_BSN_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_BSN_METHOD_STORE 0U
#define XX_BSN_METHOD_SOLID_LZ 1U
#define XX_BSN_ARCHIVE_MAGIC 0xff425347U
#define XX_BSN_MEMBER_MAGIC 0xff425341U
#define XX_BSN_ARCHIVE_HEADER_SIZE 6
#define XX_BSN_MEMBER_PREFIX_SIZE 6
#define XX_BSN_TRAILER_SIZE 2
/* Body = 4 attrs + 4 timestamp + name + NUL + 4 + 4 + 4. */
#define XX_BSN_HEADER_BODY_OVERHEAD 21U
#define XX_BSN_NAME_OFFSET 8U
#define XX_BSN_MAX_NAME_SIZE 260U
#define XX_BSN_MAX_VERSION 0x000fU
#define XX_BSN_MAX_MEMBERS 100000U
#define XX_BSN_ATTR_DIRECTORY 0x02000000U
#define XX_BSN_ATTR_STORED 0x08000000U

typedef struct xx_bsn_member_s {
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
} xx_bsn_member;

typedef struct xx_bsn_stream_s {
    xx_bsn_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_bsn_stream;

static void xx_bsn_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_bsn_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_bsn_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_bsn_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_bsn_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_bsn_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_bsn_path_safe(const char *path) {
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
static char *xx_bsn_make_name(const uint8_t *raw, size_t size,
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

static void xx_bsn_stream_free(void *pointer) {
    xx_bsn_stream *stream = (xx_bsn_stream *)pointer;
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
static bool xx_bsn_add(xx_bsn_stream *stream,
                          const xx_bsn_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_bsn_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_bsn_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}


/* --------------------------------------------------------------- parse -- */

static xx_bsn_stream *xx_bsn_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_bsn_stream *stream = NULL;
    uint8_t head[XX_BSN_ARCHIVE_HEADER_SIZE];
    uint8_t *header = NULL;
    int64_t total;
    int64_t span;
    int64_t cursor;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_BSN_ARCHIVE_HEADER_SIZE + XX_BSN_TRAILER_SIZE) return NULL;
    if (!xx_bsn_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }
    if (xx_bsn_be32(head) != XX_BSN_ARCHIVE_MAGIC) return NULL;
    if (xx_bsn_be16(head + 4) > XX_BSN_MAX_VERSION) return NULL;

    stream = (xx_bsn_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    cursor = XX_BSN_ARCHIVE_HEADER_SIZE;
    for (;;) {
        uint8_t prefix[XX_BSN_MEMBER_PREFIX_SIZE];
        const uint8_t *body;
        xx_bsn_member member;
        uint32_t body_size;
        uint32_t name_size;
        int64_t header_size;
        uint64_t packed;
        uint64_t plain;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (span - cursor < XX_BSN_TRAILER_SIZE) goto fail;
        if (!xx_bsn_read_at(self, self->base_address + cursor, prefix,
                            XX_BSN_TRAILER_SIZE)) {
            goto fail;
        }
        /* The two-byte zero trailer closes the archive. */
        if (prefix[0] == 0x00U && prefix[1] == 0x00U) {
            if (stream->count == 0U) goto fail;
            stream->archive_size = cursor + XX_BSN_TRAILER_SIZE;
            return stream;
        }
        if (span - cursor < XX_BSN_MEMBER_PREFIX_SIZE ||
            stream->count >= XX_BSN_MAX_MEMBERS) {
            goto fail;
        }
        if (!xx_bsn_read_at(self, self->base_address + cursor, prefix,
                            sizeof(prefix))) {
            goto fail;
        }
        if (xx_bsn_be32(prefix) != XX_BSN_MEMBER_MAGIC) goto fail;

        /* The body size is gated to the one shape the format can produce
         * before it is used to size a read. */
        body_size = xx_bsn_be16(prefix + 4);
        if (body_size < XX_BSN_HEADER_BODY_OVERHEAD ||
            body_size > XX_BSN_HEADER_BODY_OVERHEAD + XX_BSN_MAX_NAME_SIZE) {
            goto fail;
        }
        name_size = body_size - XX_BSN_HEADER_BODY_OVERHEAD;
        header_size = XX_BSN_MEMBER_PREFIX_SIZE + (int64_t)body_size + 4;
        if (header_size > span - cursor) goto fail;

        header = (uint8_t *)xx_mem_alloc((size_t)header_size);
        if (!header) goto fail;
        if (!xx_bsn_read_at(self, self->base_address + cursor, header,
                            (size_t)header_size)) {
            goto fail;
        }
        body = header + XX_BSN_MEMBER_PREFIX_SIZE;

        /* The declared body length and the ASCIIZ name must agree exactly:
         * body == 21 + strlen(name).  A name that stops early, or not at all,
         * is a mis-parse rather than something to tolerate. */
        if (body[XX_BSN_NAME_OFFSET + name_size] != 0x00U) goto fail;
        {
            uint32_t index;
            if (name_size == 0U) goto fail;
            for (index = 0U; index < name_size; ++index) {
                /* Only control bytes are rejected; the high half is
                 * legitimate cp866 text. */
                if (body[XX_BSN_NAME_OFFSET + index] < 0x20U) goto fail;
            }
        }

        /* The CRC32 over the header body is the decisive gate: it holds for
         * every header in the corpus, so reaching this point by accident is a
         * ~2^-32 event. */
        if (xx_crc32_calc(0U, body, body_size) !=
            xx_bsn_be32(header + XX_BSN_MEMBER_PREFIX_SIZE + body_size)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        plain = (uint64_t)xx_bsn_be32(body + XX_BSN_NAME_OFFSET + name_size +
                                      1U);
        packed = (uint64_t)xx_bsn_be32(body + XX_BSN_NAME_OFFSET + name_size +
                                       5U);
        member.crc32 =
            xx_bsn_be32(body + XX_BSN_NAME_OFFSET + name_size + 9U);
        member.has_crc = true;
        member.is_folder =
            (xx_bsn_be32(body) & XX_BSN_ATTR_DIRECTORY) != 0U;
        member.method = (xx_bsn_be32(body) & XX_BSN_ATTR_STORED) != 0U
                            ? XX_BSN_METHOD_STORE
                            : XX_BSN_METHOD_SOLID_LZ;

        /* Bound the payload against the real file before the cursor moves. */
        if (packed > (uint64_t)(span - cursor - header_size)) goto fail;
        /* A directory carries no payload, and a stored member's two size
         * fields must agree; that pair is what keeps the attribute bits
         * honest when a header is otherwise plausible. */
        if (member.is_folder && (packed != 0U || plain != 0U)) goto fail;
        if (!member.is_folder && member.method == XX_BSN_METHOD_STORE &&
            packed != plain) {
            goto fail;
        }

        member.name = xx_bsn_make_name(body + XX_BSN_NAME_OFFSET,
                                       (size_t)name_size, true);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + cursor;
        member.header_size = header_size;
        member.data_offset = self->base_address + cursor + header_size;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = plain;
        if (!xx_bsn_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        xx_mem_free(header);
        header = NULL;
        cursor += header_size + (int64_t)packed;
    }

fail:
    if (header) xx_mem_free(header);
    xx_bsn_stream_free(stream);
    return NULL;
}

/* Stored members and directories extract; a compressed member fails closed.
 * BSA's compressed stream is solid -- its LZ dictionary carries across member
 * boundaries -- so decoding member k would require replaying every member
 * before it.  That decoder is not implemented here, and refusing is worth
 * more than emitting a plausible-looking wrong result. */
static bool xx_bsn_decode(Abstractformat *self, const xx_bsn_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->is_folder) return true;
    if (member->method != XX_BSN_METHOD_STORE) return false;
    if ((uint64_t)member->packed_size != member->unpacked_size) return false;
    if (member->packed_size == 0) return true;
    if ((uint64_t)member->packed_size > (uint64_t)SIZE_MAX) return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!output) return false;
    if (!xx_bsn_read_at(self, member->data_offset, output,
                        (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    /* The header records the plaintext CRC32; a stored member can be checked
     * against it outright. */
    if (member->has_crc &&
        xx_crc32_calc(0U, output, (size_t)member->packed_size) !=
            member->crc32) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->packed_size;
    return true;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_bsn_init(xx_bsn *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_BSN_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-bsa-bsn");
    xx_format_set_extension(&archive->format, "bsn");
    archive->format.check_is_valid = xx_bsn_check_is_valid;
    archive->format.handle_base_info = xx_bsn_handle_base_info;
    archive->format.get_format_size = xx_bsn_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bsn_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bsn_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bsn_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bsn_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bsn_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bsn_free_archive_records_reading;
    archive->format.destroy = xx_bsn_vtable_destroy;
}

xx_bsn *xx_bsn_create(xx_io_device *device, int64_t base_address) {
    xx_bsn *archive = (xx_bsn *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_bsn_init(archive, device, base_address);
    return archive;
}

void xx_bsn_destroy(xx_bsn *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_bsn_free(xx_bsn *archive) {
    if (!archive) return;
    xx_bsn_destroy(archive);
    xx_mem_free(archive);
}

static void xx_bsn_vtable_destroy(Abstractformat *self) {
    xx_bsn_destroy((xx_bsn *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_bsn_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_bsn_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_bsn_parse(self, pd);
    if (!stream) return false;
    xx_bsn_stream_free(stream);
    return true;
}

bool xx_bsn_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_bsn *archive = (xx_bsn *)self;
    xx_bsn_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_bsn_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_bsn_stream_free(stream);
    return true;
}

int64_t xx_bsn_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_bsn_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_bsn *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_bsn_set_record(xx_archive_record *record,
                                 const xx_bsn_member *member) {
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

static bool xx_bsn_copy_options(xx_list_s *target,
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

static const xx_var *xx_bsn_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_bsn_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_bsn_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_bsn_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_bsn_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_bsn_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_bsn_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_bsn_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_bsn_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_bsn_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_bsn_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_bsn_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_bsn_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_bsn_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_bsn_stream *stream;
    const xx_bsn_member *member;
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
    stream = (xx_bsn_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_bsn_path_safe(member->name)) return false;

    path_option =
        xx_bsn_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_bsn_decode(self, member, &plain, &plain_size, pd);
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
        !xx_bsn_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_bsn_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
