/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Pocket Soft RTPatch (.rta) archives.
 *
 *   header, 4 bytes at offset 0:
 *     0x00  "KJd\0"
 *
 *   record, variable length, chained from offset 4:
 *     0x00  u8 name length; ZERO ends the chain and is the last byte of the
 *           archive
 *     ..    name, that many bytes, no terminator
 *     ..    u8 length of a second string
 *     ..    that string, that many bytes
 *     ..    fixed part, 13 bytes:
 *             +0x00  u8  DOS attribute byte
 *             +0x01  u16 LE DOS date
 *             +0x03  u16 LE DOS time
 *             +0x05  i32 LE uncompressed size
 *             +0x09  i32 LE compressed size
 *     ..    the member's bytes, compressed size of them, immediately behind
 *           the fixed part
 *
 * The next record starts where those bytes end, so the archive is one chain
 * with no directory and no count. A record with both sizes zero is an empty
 * file and carries no stream at all; every other record carries one complete
 * RTPatch adaptive-Huffman/LZSS stream, which opens with the codec's own
 * magic 0xb59c, a raw-literal flag of 0 or 1, and a mandatory 0xff.
 *
 * The second string never reaches the file name. It is empty throughout the
 * reference set and its meaning is not established, so this reader validates
 * it - it is part of the chain and getting its length wrong desynchronises
 * everything behind it - and then drops it.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rta/xx_rta.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/rtpatch/xx_rtpatch.h"

#include <stdio.h>

#define XX_RTA_COPY_CHUNK (64 * 1024)

typedef struct xx_rta_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_rta_member;

typedef struct xx_rta_stream_s {
    xx_rta_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_rta_stream;

static void xx_rta_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_rta_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_rta_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_rta_path_safe(const char *name) {
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

static void xx_rta_stream_free(void *pointer) {
    xx_rta_stream *stream = (xx_rta_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_rta_add(xx_rta_stream *stream,
                          const xx_rta_member *member) {
    xx_rta_member *grown = (xx_rta_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_RTA_MAGIC_SIZE 4
#define XX_RTA_FIXED_HEADER_SIZE 13
#define XX_RTA_MAX_RECORD_HEADER (1 + 255 + 1 + 255 + XX_RTA_FIXED_HEADER_SIZE)
#define XX_RTA_MAX_MEMBERS 100000
#define XX_RTA_METHOD_EMPTY 0U
#define XX_RTA_METHOD_RTPATCH 1U
#define XX_RTA_STREAM_PREFIX_SIZE 4
#define XX_RTA_STREAM_MAGIC_HIGH 0xB5U
#define XX_RTA_STREAM_MAGIC_LOW 0x9CU
#define XX_RTA_STREAM_RESERVED 0xFFU
#define XX_RTA_ATTRIBUTE_VOLUME 0x08U
#define XX_RTA_ATTRIBUTE_DIRECTORY 0x10U
#define XX_RTA_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_rta_le16(const uint8_t *data);
static uint32_t xx_rta_le32(const uint8_t *data);
static bool xx_rta_decode_name(const uint8_t *data, size_t size, char **out_name);
static bool xx_rta_check_extra(const uint8_t *data, size_t size);
static xx_rta_stream *xx_rta_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_rta_decode(Abstractformat *self, const xx_rta_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


static uint16_t xx_rta_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_rta_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The name is taken verbatim; only the OS/2 - DOS separator is normalised.
 * Nothing is stripped and nothing is folded onto '_', so two distinct members
 * can never collapse onto one output path. A name the host cannot represent
 * rejects the container rather than being rewritten into something that might
 * collide with a sibling. */
static bool xx_rta_decode_name(const uint8_t *data, size_t size,
                               char **out_name) {
    /* The length prefix is a u8, so 255 bytes plus a terminator is the
     * widest a name can be and the buffer never has to grow. */
    char buffer[256];
    char *name;
    size_t index;

    *out_name = NULL;
    if (size == 0U || size >= sizeof(buffer)) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t character = data[index];
        /* Printable ASCII only: the format stores DOS and OS/2 paths and
         * grants no exemption for high bytes. */
        if (character < 0x20U || character > 0x7EU) return false;
        buffer[index] = (character == '\\') ? '/' : (char)character;
    }
    buffer[size] = '\0';

    /* An empty path component, a '.' component or a '..' component would
     * either collapse two members onto one path or escape the extraction
     * directory. The separator is normalised first, so "A\\..\\B" is caught
     * here too. */
    if (buffer[0] == '/') return false;
    index = 0U;
    while (index < size) {
        size_t start = index;
        size_t length;
        while (index < size && buffer[index] != '/') ++index;
        length = index - start;
        if (length == 0U || (length == 1U && buffer[start] == '.') ||
            (length == 2U && buffer[start] == '.' &&
             buffer[start + 1U] == '.')) {
            return false;
        }
        if (index < size) ++index;
    }

    name = xx_str_dup(buffer);
    if (!name) return false;
    *out_name = name;
    return true;
}

/* The second string is validated but discarded: it is part of the chain, so
 * a wrong length here desynchronises every record behind it, but its meaning
 * is not established and it never reaches a file name. */
static bool xx_rta_check_extra(const uint8_t *data, size_t size) {
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (data[index] < 0x20U || data[index] > 0x7EU) return false;
    }
    return true;
}

static xx_rta_stream *xx_rta_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_rta_stream *stream;
    uint8_t magic[XX_RTA_MAGIC_SIZE + 1];
    uint8_t window[XX_RTA_MAX_RECORD_HEADER];
    uint8_t prefix[XX_RTA_STREAM_PREFIX_SIZE];
    int64_t total;
    int64_t span;
    int64_t current;
    int64_t archive_size = 0;
    bool terminated = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_RTA_MAGIC_SIZE + 1) return NULL;
    if (!xx_rta_read_at(self, self->base_address, magic, sizeof(magic))) {
        return NULL;
    }
    /* Four bytes of signature, the last of them a NUL. On its own that is
     * thin, which is why every record below is required to be structurally
     * complete and the chain is required to land on a terminator. */
    if (magic[0] != 'K' || magic[1] != 'J' || magic[2] != 'd' ||
        magic[3] != 0U) {
        return NULL;
    }
    /* The fifth byte is the first record's name length, and zero there is
     * the end marker: such a file declares no members at all. */
    if (magic[4] == 0U) return NULL;

    stream = (xx_rta_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    current = XX_RTA_MAGIC_SIZE;
    while (!terminated) {
        xx_rta_member member;
        const uint8_t *fixed;
        char *name;
        int64_t available;
        int64_t position = 0;
        int64_t uncompressed_size;
        int64_t compressed_size;
        int64_t data_offset;
        size_t portion;
        uint8_t name_length;
        uint8_t extra_length;
        uint8_t attributes;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count > (size_t)XX_RTA_MAX_MEMBERS) goto fail;
        /* Running out of file without meeting the terminator means the chain
         * does not describe this file. */
        if (current >= span) goto fail;

        available = span - current;
        portion = (size_t)(available < (int64_t)XX_RTA_MAX_RECORD_HEADER
                               ? available
                               : (int64_t)XX_RTA_MAX_RECORD_HEADER);
        if (!xx_rta_read_at(self, self->base_address + current, window,
                            portion)) {
            goto fail;
        }

        name_length = window[position++];
        if (name_length == 0U) {
            /* The terminator is one byte and it is the archive's last byte. */
            archive_size = current + 1;
            terminated = true;
            break;
        }
        if (position + (int64_t)name_length > (int64_t)portion) goto fail;
        if (!xx_rta_decode_name(window + position, (size_t)name_length,
                                &name)) {
            goto fail;
        }
        position += (int64_t)name_length;

        if (position >= (int64_t)portion) {
            xx_str_free(name);
            goto fail;
        }
        extra_length = window[position++];
        if (position + (int64_t)extra_length > (int64_t)portion ||
            !xx_rta_check_extra(window + position, (size_t)extra_length)) {
            xx_str_free(name);
            goto fail;
        }
        position += (int64_t)extra_length;

        if (position + XX_RTA_FIXED_HEADER_SIZE > (int64_t)portion) {
            xx_str_free(name);
            goto fail;
        }
        fixed = window + position;
        attributes = fixed[0];
        /* Signed on purpose: the reference reads both sizes as int32, so a
         * value with the top bit set is a corrupt field, not a
         * four-gigabyte member. */
        uncompressed_size = (int64_t)(int32_t)xx_rta_le32(fixed + 5);
        compressed_size = (int64_t)(int32_t)xx_rta_le32(fixed + 9);
        position += XX_RTA_FIXED_HEADER_SIZE;

        if (uncompressed_size < 0 || compressed_size < 0 ||
            /* A volume label or a directory entry has no stream this reader
             * knows how to place, so the container is refused rather than
             * guessed at. */
            (attributes & (XX_RTA_ATTRIBUTE_VOLUME |
                           XX_RTA_ATTRIBUTE_DIRECTORY)) != 0U ||
            /* The only record without a stream is the empty file. */
            (compressed_size == 0 && uncompressed_size != 0)) {
            xx_str_free(name);
            goto fail;
        }

        data_offset = current + position;
        if (!xx_rta_range_within(span, data_offset, compressed_size)) {
            xx_str_free(name);
            goto fail;
        }

        if (compressed_size > 0) {
            /* This is the check that keeps a four-byte header hit from
             * walking a chain of invented records: the four bytes every
             * RTPatch stream must open with are the decoder's own
             * invariants, so a record whose payload does not start with them
             * is not a member however well its lengths happen to add up. */
            if (compressed_size < XX_RTA_STREAM_PREFIX_SIZE ||
                !xx_rta_read_at(self, self->base_address + data_offset,
                                prefix, sizeof(prefix)) ||
                prefix[0] != XX_RTA_STREAM_MAGIC_HIGH ||
                prefix[1] != XX_RTA_STREAM_MAGIC_LOW ||
                prefix[2] > 1U ||
                prefix[3] != XX_RTA_STREAM_RESERVED) {
                xx_str_free(name);
                goto fail;
            }
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + current;
        member.header_size = position;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = (compressed_size == 0) ? XX_RTA_METHOD_EMPTY
                                               : XX_RTA_METHOD_RTPATCH;
        /* DOS date/time, packed date-high / time-low. The record stores the
         * date word first and the time word second. */
        member.timestamp = ((uint64_t)xx_rta_le16(fixed + 1) << 16) |
                           (uint64_t)xx_rta_le16(fixed + 3);
        /* Directory records are refused above, so every member is a file. */
        member.is_folder = false;

        if (!xx_rta_path_safe(name) || !xx_rta_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        current = data_offset + compressed_size;
    }

    /* A chain that ran off the end rather than meeting its zero byte does not
     * describe this file. */
    if (!terminated || stream->count == 0U) goto fail;
    if (!xx_rta_range_within(span, 0, archive_size)) goto fail;
    stream->archive_size = archive_size;
    return stream;

fail:
    xx_rta_stream_free(stream);
    return NULL;
}


/* Both string lengths are u8, so this is the widest a record header can be. */
/* No count is stored, so this is a runaway guard, not a format limit. */

/* The container has no method field: a record states its method by carrying
 * a stream or not carrying one. These two numbers are this reader's, and the
 * decode switch is the only place they are read. */

/* A member stream always opens with the codec magic, the raw-literal flag and
 * the mandatory reserved byte. These are the decoder's own invariants, which
 * is what makes them safe to require of a candidate container. */

/* DOS attribute bits the record's attribute byte carries. */

/* The stored uncompressed size is attacker-controlled; refuse rather than
 * attempt the allocation. */

static bool xx_rta_decode(Abstractformat *self, const xx_rta_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_RTA_MAX_DECODED ||
        member->uncompressed_size > XX_RTA_MAX_DECODED) {
        return false;
    }

    if (member->method == XX_RTA_METHOD_EMPTY) {
        /* A record with no bytes on either side is a real, empty file, not a
         * failure. */
        if (member->compressed_size != 0 || member->uncompressed_size != 0) {
            return false;
        }
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    /* Anything else this reader does not implement must fail here rather
     * than fall through to a byte copy: an RTPatch stream copied verbatim is
     * garbage that is indistinguishable from data. */
    if (member->method != XX_RTA_METHOD_RTPATCH) return false;
    if (member->compressed_size < XX_RTA_STREAM_PREFIX_SIZE) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_rta_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* The codec has no measuring entry point because every container stores
     * the decoded length: the stream must produce exactly that many bytes,
     * and a short decode reported as success is the one failure the caller
     * cannot detect. */
    if (!xx_rtpatch_decode_memory(input, (size_t)member->compressed_size,
                                  output,
                                  (size_t)member->uncompressed_size,
                                  &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_rta_init(xx_rta *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_RTA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-rtpatch-archive");
    xx_format_set_extension(&archive->format, "rta");
    archive->format.check_is_valid = xx_rta_check_is_valid;
    archive->format.handle_base_info = xx_rta_handle_base_info;
    archive->format.get_format_size = xx_rta_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rta_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rta_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rta_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rta_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rta_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rta_free_archive_records_reading;
    archive->format.destroy = xx_rta_vtable_destroy;
}

xx_rta *xx_rta_create(xx_io_device *device, int64_t base_address) {
    xx_rta *archive = (xx_rta *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_rta_init(archive, device, base_address);
    return archive;
}

void xx_rta_destroy(xx_rta *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_rta_free(xx_rta *archive) {
    if (!archive) return;
    xx_rta_destroy(archive);
    xx_mem_free(archive);
}

static void xx_rta_vtable_destroy(Abstractformat *self) {
    xx_rta_destroy((xx_rta *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_rta_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rta_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_rta_parse(self, pd);
    if (!stream) return false;
    xx_rta_stream_free(stream);
    return true;
}

bool xx_rta_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rta *archive = (xx_rta *)self;
    xx_rta_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_rta_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_rta_stream_free(stream);
    return true;
}

int64_t xx_rta_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_rta_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_rta *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_rta_set_record(xx_archive_record *record,
                                 const xx_rta_member *member) {
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

static bool xx_rta_copy_options(xx_list_s *target,
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

static const xx_var *xx_rta_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_rta_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_rta_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_rta_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_rta_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_rta_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_rta_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_rta_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_rta_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rta_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_rta_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_rta_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_rta_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rta_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_rta_stream *stream;
    const xx_rta_member *member;
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
    stream = (xx_rta_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_rta_path_safe(member->name)) return false;

    path_option = xx_rta_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_rta_decode(self, member, &plain, &plain_size, pd);
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
        !xx_rta_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_rta_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
