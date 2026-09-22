/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ARQ, written by Crusher!.
 *
 *   optional container prelude, 12 bytes:
 *     0..3   67 57 04 02        magic
 *     4..7   u32 LE total uncompressed size of the chain
 *     8..11  u32 LE, zero
 *
 *   member record:
 *     0..3   67 57 04 01        magic
 *     4..5   u16 signature, low byte 0x30 or 0x31
 *     6..7   u16 name length
 *     8..    name, printable ASCII
 *     then a 33-byte trailer:
 *       +0   u32 mtime
 *       +4   u16 attributes
 *       +6   u32 reserved, zero
 *       +10  u32 compressed size
 *       +14  u32 uncompressed size
 *       +18  u32 CRC-32 of the PACKED bytes
 *       +22  u16 method: low byte 1 = stored, 2 = crushed (-lh5-)
 *       +24  u8 per-member flag, values 0/1/2 observed
 *       +25  eight zero bytes
 *
 * The chain ends with a record whose name length is zero: a complete
 * header-shaped index entry whose other fields duplicate the last member.
 *
 * The prelude only appears when Crusher! wraps the chain, as in its
 * self-extracting stub. A bare .ARQ/.SKU/.IRD file starts at the first member
 * and carries no total-size field, so the sum-of-members cross-check applies
 * only to the wrapped shape.
 *
 * The packed CRC-32 is verified while parsing. That is what makes the format
 * safely identifiable: four magic bytes plus a signature byte would not be,
 * but a CRC over the payload agreeing with a stored value is not something
 * unrelated data produces. It is deliberately not published as the record's
 * checksum, because it covers the packed bytes -- a caller that applied it to
 * the extracted plaintext would reject every crushed member.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arq/xx_arq.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_ARQ_CONTAINER_MAGIC 0x02045767U
#define XX_ARQ_MEMBER_MAGIC 0x01045767U
#define XX_ARQ_CONTAINER_HEADER_SIZE 12
#define XX_ARQ_MEMBER_PREFIX_SIZE 8
#define XX_ARQ_MEMBER_TRAILER_SIZE 33
#define XX_ARQ_TERMINATOR_SIZE \
    (XX_ARQ_MEMBER_PREFIX_SIZE + XX_ARQ_MEMBER_TRAILER_SIZE)
#define XX_ARQ_MAX_MEMBERS 100000
#define XX_ARQ_MAX_NAME_SIZE 4096U
#define XX_ARQ_SIGNATURE_LOW_A 0x30U
#define XX_ARQ_SIGNATURE_LOW_B 0x31U
#define XX_ARQ_METHOD_STORED 0x01U
#define XX_ARQ_METHOD_CRUSHED 0x02U
#define XX_ARQ_CRC_CHUNK (64 * 1024)

typedef struct xx_arq_member_s {
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t mtime;
    uint32_t packed_crc32;
    uint16_t attributes;
    uint16_t method;
    char *name;
} xx_arq_member;

typedef struct xx_arq_stream_s {
    xx_arq_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    bool has_container_header;
    uint32_t declared_uncompressed_size;
} xx_arq_stream;

static void xx_arq_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_arq_read_at(Abstractformat *self, int64_t offset,
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

static uint32_t xx_arq_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_arq_u16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static bool xx_arq_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total && size <= total - offset;
}

static bool xx_arq_signature_ok(uint16_t signature) {
    uint8_t low = (uint8_t)(signature & 0xFFU);

    return low == XX_ARQ_SIGNATURE_LOW_A || low == XX_ARQ_SIGNATURE_LOW_B;
}

/* Names are stored as printable ASCII; anything else means this is not a
 * member header, which is how a random magic hit is caught. */
static bool xx_arq_name_ok(const uint8_t *name, size_t size) {
    size_t index;

    if (size == 0U) return false;
    for (index = 0U; index < size; ++index) {
        if (name[index] < 0x20U || name[index] > 0x7EU) return false;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_arq_path_safe(const char *name) {
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

/* Build the relative path a member is written to.
 *
 * Crusher! stores the path the file had when it was packed, drive letter and
 * all ("D:/BLAZE/CARDS/AD/COMET.BMP"), so the stored name is not a usable
 * filesystem path on its own.  The record keeps it verbatim; the write folds
 * the ':' the way the reference unpacker does ("D_/BLAZE/..."), drops
 * redundant "." components and leading separators, and trims the trailing
 * dots and spaces Windows would silently strip.  Returns NULL when the name
 * is unsafe or nothing usable is left. */
static char *xx_arq_make_extract_name(const char *name) {
    char *result;
    size_t output = 0U;
    const char *component;
    const char *cursor;

    if (!xx_arq_path_safe(name)) return NULL;
    result = xx_str_create_len(xx_str_len(name) + 1U);
    if (!result) return NULL;
    component = name;
    for (cursor = name;; ++cursor) {
        char character = *cursor;
        if (character == '/' || character == '\\' || character == '\0') {
            size_t length = (size_t)(cursor - component);
            /* xx_arq_path_safe only splits on '/', so a traversal spelled
             * with the backslash these DOS names use reaches here intact.
             * It is refused here, where both separators are recognised. */
            if (length == 2U && component[0] == '.' && component[1] == '.') {
                xx_str_free(result);
                return NULL;
            }
            if (length != 0U && !(length == 1U && component[0] == '.')) {
                size_t start;
                size_t index;
                if (output != 0U) result[output++] = '/';
                start = output;
                for (index = 0U; index < length; ++index) {
                    char byte = component[index];
                    result[output++] = (byte == ':' || byte == '<' ||
                                        byte == '>' || byte == '"' ||
                                        byte == '|' || byte == '?' ||
                                        byte == '*')
                                           ? '_' : byte;
                }
                while (output > start && (result[output - 1U] == ' ' ||
                                          result[output - 1U] == '.'))
                    --output;
                if (output == start) result[output++] = '_';
            }
            if (character == '\0') break;
            component = cursor + 1;
        }
    }
    result[output] = '\0';
    if (output == 0U) {
        xx_str_free(result);
        return NULL;
    }
    return result;
}

static uint32_t xx_arq_crc_range(Abstractformat *self, int64_t offset,
                                 uint32_t size, bool *ok, xx_pd_struct *pd) {
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(XX_ARQ_CRC_CHUNK);
    uint32_t crc = 0U;
    uint32_t remaining = size;

    *ok = false;
    if (!buffer) return 0U;
    while (remaining != 0U) {
        size_t take = remaining < XX_ARQ_CRC_CHUNK ? remaining
                                                   : XX_ARQ_CRC_CHUNK;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_arq_read_at(self, offset, buffer, take)) {
            xx_mem_free(buffer);
            return 0U;
        }
        crc = xx_crc32_calc(crc, buffer, take);
        offset += (int64_t)take;
        remaining -= (uint32_t)take;
    }
    xx_mem_free(buffer);
    *ok = true;
    return crc;
}

static void xx_arq_stream_free(void *pointer) {
    xx_arq_stream *stream = (xx_arq_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------- parsing -- */

static xx_arq_stream *xx_arq_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_arq_stream *stream = NULL;
    xx_arq_member *members = NULL;
    size_t count = 0U;
    size_t index;
    uint8_t container[XX_ARQ_CONTAINER_HEADER_SIZE];
    uint8_t prefix[XX_ARQ_MEMBER_PREFIX_SIZE];
    uint32_t leading_magic;
    bool has_container = false;
    uint32_t declared = 0U;
    uint64_t total_uncompressed = 0U;
    int64_t input_size;
    int64_t span;
    int64_t offset;

    if (!self || !self->device || self->base_address < 0) return NULL;
    input_size = xx_io_total_size(self->device);
    if (input_size < self->base_address) return NULL;
    span = input_size - self->base_address;
    if (span < XX_ARQ_CONTAINER_HEADER_SIZE + XX_ARQ_TERMINATOR_SIZE) {
        return NULL;
    }
    if (!xx_arq_read_at(self, self->base_address, container,
                        sizeof(container))) {
        return NULL;
    }

    leading_magic = xx_arq_u32(container);
    if (leading_magic == XX_ARQ_CONTAINER_MAGIC) {
        /* The wrapped shape: a declared total, then a field that must be
         * zero. */
        if (xx_arq_u32(container + 8) != 0U) return NULL;
        has_container = true;
        declared = xx_arq_u32(container + 4);
    } else if (leading_magic != XX_ARQ_MEMBER_MAGIC) {
        return NULL;
    }

    offset = has_container ? XX_ARQ_CONTAINER_HEADER_SIZE : 0;

    while (count < XX_ARQ_MAX_MEMBERS) {
        uint16_t signature;
        uint16_t name_size;
        int64_t header_size;
        uint8_t *header;
        const uint8_t *trailer;
        uint8_t method_code;
        uint32_t reserved;
        uint32_t crc;
        bool crc_ok;
        xx_arq_member *grown;
        int zero;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_arq_range_within(span, offset, XX_ARQ_MEMBER_PREFIX_SIZE)) {
            goto fail;
        }
        if (!xx_arq_read_at(self, self->base_address + offset, prefix,
                            sizeof(prefix))) {
            goto fail;
        }
        if (xx_arq_u32(prefix) != XX_ARQ_MEMBER_MAGIC) goto fail;
        signature = xx_arq_u16(prefix + 4);
        if (!xx_arq_signature_ok(signature)) goto fail;
        name_size = xx_arq_u16(prefix + 6);

        if (name_size == 0U) {
            /* The terminating index record. It must be present in full, and
             * when the chain is wrapped the declared total must agree with
             * what the members actually add up to. */
            if (count == 0U ||
                !xx_arq_range_within(span, offset, XX_ARQ_TERMINATOR_SIZE) ||
                (has_container && total_uncompressed != (uint64_t)declared)) {
                goto fail;
            }
            stream = (xx_arq_stream *)xx_mem_alloc(sizeof(*stream));
            if (!stream) goto fail;
            xx_mem_zero(stream, sizeof(*stream));
            stream->items = members;
            stream->count = count;
            stream->index = 0U;
            stream->archive_size = offset + XX_ARQ_TERMINATOR_SIZE;
            stream->has_container_header = has_container;
            stream->declared_uncompressed_size = declared;
            return stream;
        }
        if (name_size > XX_ARQ_MAX_NAME_SIZE) goto fail;

        header_size = XX_ARQ_MEMBER_PREFIX_SIZE + (int64_t)name_size +
                      XX_ARQ_MEMBER_TRAILER_SIZE;
        if (!xx_arq_range_within(span, offset, header_size)) goto fail;
        header = (uint8_t *)xx_mem_alloc((size_t)header_size);
        if (!header) goto fail;
        if (!xx_arq_read_at(self, self->base_address + offset, header,
                            (size_t)header_size)) {
            xx_mem_free(header);
            goto fail;
        }
        if (!xx_arq_name_ok(header + XX_ARQ_MEMBER_PREFIX_SIZE, name_size)) {
            xx_mem_free(header);
            goto fail;
        }
        trailer = header + XX_ARQ_MEMBER_PREFIX_SIZE + name_size;

        reserved = xx_arq_u32(trailer + 6);
        method_code = (uint8_t)(xx_arq_u16(trailer + 22) & 0xFFU);
        /* Byte +24 is a per-member flag with several observed values; only
         * the eight bytes after it are structurally required to be zero. */
        for (zero = 0; zero < 8; ++zero) {
            if (trailer[25 + zero] != 0U) {
                xx_mem_free(header);
                goto fail;
            }
        }
        if (reserved != 0U ||
            (method_code != XX_ARQ_METHOD_STORED &&
             method_code != XX_ARQ_METHOD_CRUSHED)) {
            xx_mem_free(header);
            goto fail;
        }

        grown = (xx_arq_member *)xx_mem_realloc(
            members, sizeof(*members) * (count + 1U));
        if (!grown) {
            xx_mem_free(header);
            goto fail;
        }
        members = grown;
        xx_mem_zero(&members[count], sizeof(members[count]));
        members[count].header_offset = offset;
        members[count].header_size = header_size;
        members[count].data_offset = offset + header_size;
        members[count].mtime = xx_arq_u32(trailer);
        members[count].attributes = xx_arq_u16(trailer + 4);
        members[count].compressed_size = xx_arq_u32(trailer + 10);
        members[count].uncompressed_size = xx_arq_u32(trailer + 14);
        members[count].packed_crc32 = xx_arq_u32(trailer + 18);
        members[count].method = xx_arq_u16(trailer + 22);
        /* The name is not NUL-terminated inside the header buffer, and
         * xxfclib has no length-limited dup, so copy it explicitly. */
        members[count].name = (char *)xx_mem_alloc((size_t)name_size + 1U);
        if (members[count].name) {
            xx_rt_memcpy(members[count].name,
                         header + XX_ARQ_MEMBER_PREFIX_SIZE, name_size);
            members[count].name[name_size] = '\0';
        }
        xx_mem_free(header);
        if (!members[count].name) goto fail;
        /* Crusher! writes DOS separators; the archive record API uses '/'. */
        {
            char *cursor = members[count].name;
            while (*cursor) {
                if (*cursor == '\\') *cursor = '/';
                ++cursor;
            }
        }

        if (!xx_arq_range_within(span, members[count].data_offset,
                                 members[count].compressed_size)) {
            ++count;
            goto fail;
        }
        /* A stored member is the anchor that keeps the two-value method gate
         * honest: its two size fields must agree exactly. */
        if (method_code == XX_ARQ_METHOD_STORED &&
            members[count].compressed_size !=
                members[count].uncompressed_size) {
            ++count;
            goto fail;
        }

        crc = xx_arq_crc_range(self,
                               self->base_address + members[count].data_offset,
                               members[count].compressed_size, &crc_ok, pd);
        if (!crc_ok || crc != members[count].packed_crc32) {
            ++count;
            goto fail;
        }

        total_uncompressed += members[count].uncompressed_size;
        if (has_container && total_uncompressed > (uint64_t)declared) {
            ++count;
            goto fail;
        }
        offset = members[count].data_offset + members[count].compressed_size;
        ++count;
    }

fail:
    if (members) {
        for (index = 0U; index < count; ++index) {
            xx_str_free(members[index].name);
        }
        xx_mem_free(members);
    }
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_arq_init(xx_arq *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ARQ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-arq");
    xx_format_set_extension(&archive->format, "arq");
    archive->format.check_is_valid = xx_arq_check_is_valid;
    archive->format.handle_base_info = xx_arq_handle_base_info;
    archive->format.get_format_size = xx_arq_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_arq_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_arq_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_arq_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_arq_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_arq_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_arq_free_archive_records_reading;
    archive->format.destroy = xx_arq_vtable_destroy;
}

xx_arq *xx_arq_create(xx_io_device *device, int64_t base_address) {
    xx_arq *archive = (xx_arq *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_arq_init(archive, device, base_address);
    return archive;
}

void xx_arq_destroy(xx_arq *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_arq_free(xx_arq *archive) {
    if (!archive) return;
    xx_arq_destroy(archive);
    xx_mem_free(archive);
}

static void xx_arq_vtable_destroy(Abstractformat *self) {
    xx_arq_destroy((xx_arq *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_arq_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_arq_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_arq_parse(self, pd);
    if (!stream) return false;
    xx_arq_stream_free(stream);
    return true;
}

bool xx_arq_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_arq *archive = (xx_arq *)self;
    xx_arq_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_arq_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->has_container_header = stream->has_container_header;
    archive->declared_uncompressed_size = stream->declared_uncompressed_size;
    xx_arq_stream_free(stream);
    return true;
}

int64_t xx_arq_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_arq_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_arq *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_arq_set_record(xx_archive_record *record,
                              const xx_arq_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->mtime) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
    /* Deliberately no checksum: the stored CRC covers the PACKED bytes, and a
     * caller applying it to the extracted plaintext would reject every
     * crushed member. It is verified during parsing instead. */
}

static bool xx_arq_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_arq_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_arq_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_arq_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_arq_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_arq_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_arq_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_arq_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_arq_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_arq_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_arq_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_arq_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_arq_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_arq_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

/*
 * Produce one member's plaintext. Stored members are copied; crushed ones go
 * through -lh5-, which is what Crusher!'s "crushed" method is.
 */
static bool xx_arq_decode(Abstractformat *self, const xx_arq_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    uint8_t method_code = (uint8_t)(member->method & 0xFFU);
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed = (uint8_t *)xx_mem_alloc(member->compressed_size != 0U
                                         ? member->compressed_size
                                         : 1U);
    if (!packed) return false;
    if (member->compressed_size != 0U &&
        !xx_arq_read_at(self, self->base_address + member->data_offset, packed,
                        member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }

    if (method_code == XX_ARQ_METHOD_STORED) {
        *out = packed;
        *out_size = member->compressed_size;
        return true;
    }

    plain = (uint8_t *)xx_mem_alloc(member->uncompressed_size != 0U
                                        ? member->uncompressed_size
                                        : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (member->uncompressed_size != 0U &&
        !xx_lzh5_decode_memory(packed, member->compressed_size, plain,
                               member->uncompressed_size, 5, &written)) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = member->uncompressed_size;
    return true;
}

bool xx_arq_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_arq_stream *stream;
    const xx_arq_member *member;
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
    stream = (xx_arq_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_arq_path_safe(member->name)) return false;

    path_option = xx_arq_get_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which is what verifies the
         * member without writing anything. */
        result = xx_arq_decode(self, member, &plain, &plain_size, pd);
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
    {
        char *relative = xx_arq_make_extract_name(member->name);
        if (!relative) {
            xx_str_free(converted_path);
            return false;
        }
        if (base_path[0] != '\0' &&
            base_path[xx_str_len(base_path) - 1U] != '/' &&
            base_path[xx_str_len(base_path) - 1U] != '\\') {
            target_path = xx_str_concat3(base_path, "/", relative);
        } else {
            target_path = xx_str_concat(base_path, relative);
        }
        xx_str_free(relative);
    }
    xx_str_free(converted_path);
    if (!target_path || !xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    if (!xx_arq_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_arq_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

bool xx_arq_has_container_header(const xx_arq *archive) {
    return archive ? archive->has_container_header : false;
}
