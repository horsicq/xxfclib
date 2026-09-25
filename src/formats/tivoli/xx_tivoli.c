/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Tivoli Filepack Block (.PKT) containers.
 *
 *   0x00  "    79 TFPB-"    the ASCII header line begins here; the reference
 *                           detector compares only the anchors below, not
 *                           the whole line
 *   0x3c  "d5 c"            part of the "md5 compress=native" field
 *   0x4c  "ve"              the tail of "native"
 *   0x4e  0x0a              the line terminator; the header is 0x4f bytes
 *   0x4f  the block chain:
 *           u16 BE h == 0            end of chain
 *           u16 BE h, h & 0x8000 == 0  compressed block, h = COMPRESSED byte
 *                                      budget (not an output length)
 *           u16 BE h, h & 0x8000 != 0  stored block, length = h & 0x7fff
 *         The last eight bytes of every block are not payload: they are the
 *         first eight bytes of the MD5 of everything emitted SO FAR, from one
 *         running context snapshotted per block.
 *
 * THE MEMBERS DO NOT EXIST AS FILE EXTENTS. A member's bytes are spread
 * across blocks that are not aligned to member boundaries, each block carries
 * a digest tail in the middle of the data, and the LZ77 ring carries state
 * from block to block, so no member can be produced without decoding every
 * block before it. xx_tivoli_decode_memory() therefore takes the WHOLE
 * container and produces the WHOLE unwrapped stream, with every block's
 * running-MD5 tail verified - the only integrity signal the format has.
 *
 * HOW THAT IS EXPRESSED IN THE MEMBER CONTRACT. The contract has one file
 * extent per member and no free-form property field, so the reference's
 * 8-byte {u32 offset, u32 size} member property is carried in the fields that
 * can hold it honestly:
 *
 *   data_offset, compressed_size   THE WHOLE CONTAINER - literally the bytes
 *                                  decode must read, because that is what
 *                                  producing this member costs
 *   header_offset, header_size     the member's cpio header INSIDE THE
 *                                  UNWRAPPED STREAM, not in the file
 *   uncompressed_size              the member size, i.e. the property's size
 *
 * and the slice is header_offset + header_size, which is exactly the
 * property's offset, because cpio 'newc' as this format writes it puts the
 * data immediately after the name with NO padding. Those two header fields
 * are the one place this reader reports stream coordinates where a caller
 * would expect file coordinates; every other reader in this family reports
 * file offsets there. There is no field that could carry them otherwise, and
 * inventing a file offset for bytes that exist in no file extent would be
 * worse than the mismatch.
 *
 * THE INNER STREAM IS USUALLY A CPIO 'newc' ARCHIVE, with one deviation that
 * a stock cpio reader gets wrong: there is NO padding after the name and NO
 * padding after the data. When the unwrapped stream is not a cpio, the whole
 * stream is published as a single member, which is what the reference
 * extractor produces.
 *
 * COST, stated plainly: parse unwraps the whole container to enumerate, and
 * each extraction unwraps it again. The reference does the same. A format
 * whose members share one decoder state cannot do better.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/tivoli/xx_tivoli.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/tivoli/xx_tivoli.h"

#include <stdio.h>

#define XX_TIVOLI_COPY_CHUNK (64 * 1024)

typedef struct xx_tivoli_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_tivoli_member;

typedef struct xx_tivoli_stream_s {
    xx_tivoli_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_tivoli_stream;

static void xx_tivoli_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_tivoli_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_tivoli_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_tivoli_path_safe(const char *name) {
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

static void xx_tivoli_stream_free(void *pointer) {
    xx_tivoli_stream *stream = (xx_tivoli_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_tivoli_add(xx_tivoli_stream *stream,
                          const xx_tivoli_member *member) {
    xx_tivoli_member *grown = (xx_tivoli_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_TIVOLI_HEADER_SIZE 0x4f
#define XX_TIVOLI_MAX_INPUT_SIZE ((int64_t)0x10000000)
#define XX_TIVOLI_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_TIVOLI_MAX_MEMBERS 100000
#define XX_TIVOLI_MAX_NAME_SIZE 4096
#define XX_TIVOLI_CPIO_HEADER_SIZE 110
#define XX_TIVOLI_S_IFMT 0170000U
#define XX_TIVOLI_S_IFDIR 0040000U
#define XX_TIVOLI_METHOD_FILEPACK 1U
#define XX_TIVOLI_FALLBACK_NAME "tivoli_data"

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static bool xx_tivoli_hex_field(const uint8_t *data, size_t size, uint32_t *value);
static bool xx_tivoli_is_cpio_magic(const uint8_t *data, int64_t size, int64_t offset);
static bool xx_tivoli_name_ok(const uint8_t *data, size_t size);
static bool xx_tivoli_check_header(const uint8_t *header);
static bool xx_tivoli_unwrap(Abstractformat *self, int64_t offset, int64_t size, uint8_t **inner, size_t *inner_size, xx_pd_struct *pd);
static bool xx_tivoli_parse_cpio(xx_tivoli_stream *stream, const uint8_t *data, int64_t size, int64_t container_offset, int64_t container_size, xx_pd_struct *pd);
static xx_tivoli_stream *xx_tivoli_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_tivoli_decode(Abstractformat *self, const xx_tivoli_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The container's header line is 0x4f bytes including its 0x0a terminator. */
/* The reference's own input ceiling: the whole container is buffered, twice
 * over the life of a listing plus an extraction, so this is a real limit and
 * not a formality. */

/* The container has no method field: the block chain is the only encoding it
 * has. parse stamps this one synthetic value and decode refuses anything
 * else. */

/* Used when the unwrapped stream is not a cpio and the whole stream becomes
 * one member; the container stores no name for that case. */

/* One ASCII-hex field of a cpio header. Rejecting anything that is not hex is
 * what makes a desynchronised walk stop instead of inventing a length. */
static bool xx_tivoli_hex_field(const uint8_t *data, size_t size,
                                uint32_t *value) {
    uint32_t result = 0U;
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t byte = data[index];
        int digit = -1;
        if (byte >= '0' && byte <= '9') digit = (int)(byte - '0');
        else if (byte >= 'a' && byte <= 'f') digit = 10 + (int)(byte - 'a');
        else if (byte >= 'A' && byte <= 'F') digit = 10 + (int)(byte - 'A');
        if (digit < 0) return false;
        result = (result << 4) | (uint32_t)digit;
    }
    *value = result;
    return true;
}

static bool xx_tivoli_is_cpio_magic(const uint8_t *data, int64_t size,
                                    int64_t offset) {
    if (offset < 0 || (offset + 6) > size) return false;
    if (data[offset] != '0' || data[offset + 1] != '7' ||
        data[offset + 2] != '0' || data[offset + 3] != '7' ||
        data[offset + 4] != '0') {
        return false;
    }
    /* "070701" and "070702" - the new ASCII format with and without CRC. */
    return data[offset + 5] == '1' || data[offset + 5] == '2';
}

/* A cpio path. The stream is a real archive of real file names, so the
 * printable-ASCII rule applies with '/' allowed as the separator. */
static bool xx_tivoli_name_ok(const uint8_t *data, size_t size) {
    size_t index;

    if (size == 0U) return false;
    for (index = 0U; index < size; ++index) {
        if (data[index] < 0x20U || data[index] > 0x7eU) return false;
    }
    return true;
}

/* The reference compares only these anchors of the 79-byte header line, not
 * the whole line, because the middle of the line carries variable fields.
 * They are nonetheless the format's ENTIRE false-positive defence at parse
 * time: twelve bytes at the start, four at 0x3c, two at 0x4c and the 0x0a
 * terminator. Anything a reader loosens here it pays for by buffering and
 * unwrapping whole files that were never Filepack containers. */
static bool xx_tivoli_check_header(const uint8_t *header) {
    static const char anchor_head[12] = {' ', ' ', ' ', ' ', '7', '9',
                                         ' ', 'T', 'F', 'P', 'B', '-'};
    size_t index;

    for (index = 0U; index < sizeof(anchor_head); ++index) {
        if (header[index] != (uint8_t)anchor_head[index]) return false;
    }
    if (header[0x3c] != 'd' || header[0x3d] != '5' || header[0x3e] != ' ' ||
        header[0x3f] != 'c') {
        return false;
    }
    if (header[0x4c] != 'v' || header[0x4d] != 'e') return false;
    return header[0x4e] == 0x0aU;
}

/* Read the whole container, measure the unwrapped stream and unwrap it. Both
 * parse and decode need exactly this, and doing it in one place is what keeps
 * the sizes parse published and the buffer decode slices from ever
 * disagreeing. */
static bool xx_tivoli_unwrap(Abstractformat *self, int64_t offset,
                             int64_t size, uint8_t **inner, size_t *inner_size,
                             xx_pd_struct *pd) {
    uint8_t *file;
    uint8_t *output;
    size_t produced = 0U;
    size_t written = 0U;

    *inner = NULL;
    *inner_size = 0U;
    if (size < XX_TIVOLI_HEADER_SIZE || size > XX_TIVOLI_MAX_INPUT_SIZE) {
        return false;
    }

    file = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!file) return false;
    if (!xx_tivoli_read_at(self, offset, file, (size_t)size)) {
        xx_mem_free(file);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(file);
        return false;
    }
    if (!xx_tivoli_check_header(file)) {
        xx_mem_free(file);
        return false;
    }

    /* The container stores each member's size but never the length of the
     * whole unwrapped stream, so it has to be measured. The scan runs the
     * same core as the decode with the output discarded, and it verifies the
     * running MD5 exactly as the decode does, so a container that measures is
     * a container that unwraps. */
    if (!xx_tivoli_scan_memory(file, (size_t)size,
                               (size_t)XX_TIVOLI_MAX_DECODED, NULL,
                               &produced) ||
        produced == 0U) {
        xx_mem_free(file);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(file);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(produced);
    if (!output) {
        xx_mem_free(file);
        return false;
    }
    /* produced is both the capacity and the exact expected length: a chain
     * that stops early, or whose per-block digest does not match, fails here
     * rather than handing back a short stream that members would be sliced
     * out of. */
    if (!xx_tivoli_decode_memory(file, (size_t)size, output, produced,
                                 &written) ||
        written != produced) {
        xx_mem_free(output);
        xx_mem_free(file);
        return false;
    }
    xx_mem_free(file);
    *inner = output;
    *inner_size = produced;
    return true;
}

/* Walk the cpio 'newc' records of the unwrapped stream. Every malformed
 * condition ends the walk rather than throwing the stream away: what has
 * already been recovered is real, and an empty list is what tells the caller
 * to fall back to the whole stream as one member. */
static bool xx_tivoli_parse_cpio(xx_tivoli_stream *stream, const uint8_t *data,
                                 int64_t size, int64_t container_offset,
                                 int64_t container_size, xx_pd_struct *pd) {
    xx_tivoli_member member;
    char *name;
    int64_t offset = 0;
    uint32_t mode = 0U;
    uint32_t file_size = 0U;
    uint32_t name_size = 0U;
    int64_t header_offset;
    int64_t data_size;

    if (!xx_tivoli_is_cpio_magic(data, size, 0)) return false;

    while ((offset + XX_TIVOLI_CPIO_HEADER_SIZE) <= size) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (stream->count >= (size_t)XX_TIVOLI_MAX_MEMBERS) break;
        if (!xx_tivoli_is_cpio_magic(data, size, offset)) break;

        header_offset = offset;
        if (!xx_tivoli_hex_field(data + offset + 14, 8U, &mode)) break;
        if (!xx_tivoli_hex_field(data + offset + 54, 8U, &file_size)) break;
        if (!xx_tivoli_hex_field(data + offset + 94, 8U, &name_size)) break;

        offset += XX_TIVOLI_CPIO_HEADER_SIZE;
        if (name_size == 0U || (int64_t)name_size > XX_TIVOLI_MAX_NAME_SIZE ||
            (int64_t)name_size > (size - offset)) {
            break;
        }
        /* The stored name length includes the NUL terminator. */
        data_size = (int64_t)name_size;
        if (data[offset + data_size - 1] == 0U) --data_size;
        /* The trailer record closes the archive and is not a member. */
        if (data_size == 10 &&
            xx_rt_memcmp(data + offset, "TRAILER!!!", 10U) == 0) {
            return stream->count != 0U;
        }
        if (!xx_tivoli_name_ok(data + offset, (size_t)data_size)) break;

        offset += (int64_t)name_size;
        /* NO padding after the name and NO padding after the data - this is
         * the one place a stock 'newc' reader goes wrong on this format. */
        if ((int64_t)file_size > (size - offset)) break;

        /* The name is not NUL terminated when the stored length did not
         * include a terminator, so it is copied rather than pointed at. */
        name = xx_str_create_len((size_t)data_size);
        if (!name) return false;
        xx_rt_memcpy(name, data + offset - (int64_t)name_size,
                     (size_t)data_size);
        if (!xx_tivoli_path_safe(name)) {
            xx_str_free(name);
            break;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        /* Stream coordinates, not file coordinates - see the contract note in
         * the file comment. decode slices at header_offset + header_size. */
        member.header_offset = header_offset;
        member.header_size = XX_TIVOLI_CPIO_HEADER_SIZE + (int64_t)name_size;
        member.data_offset = container_offset;
        member.compressed_size = container_size;
        member.uncompressed_size = (int64_t)file_size;
        member.method = XX_TIVOLI_METHOD_FILEPACK;
        member.timestamp = 0U;
        member.is_folder = ((mode & XX_TIVOLI_S_IFMT) == XX_TIVOLI_S_IFDIR);
        /* A directory record carries a size field like any other; publishing
         * it would claim bytes that are not there. */
        if (member.is_folder) member.uncompressed_size = 0;

        if (!xx_tivoli_add(stream, &member)) {
            xx_str_free(name);
            return false;
        }

        offset += (int64_t)file_size;
    }

    /* A cpio without its trailer is truncated; keep what was recovered rather
     * than throwing the whole archive away. */
    return stream->count != 0U;
}

static xx_tivoli_stream *xx_tivoli_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_tivoli_stream *stream;
    xx_tivoli_member member;
    uint8_t header[XX_TIVOLI_HEADER_SIZE];
    uint8_t *inner = NULL;
    size_t inner_size = 0U;
    char *name;
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_TIVOLI_HEADER_SIZE || span > XX_TIVOLI_MAX_INPUT_SIZE) {
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    /* Check the header line before buffering the whole container: unwrapping
     * costs a full read plus two allocations, so the cheap anchors go first. */
    if (!xx_tivoli_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (!xx_tivoli_check_header(header)) return NULL;

    /* The members live in the unwrapped stream only, so enumerating them
     * means unwrapping the whole block chain right here. */
    if (!xx_tivoli_unwrap(self, self->base_address, span, &inner, &inner_size,
                          pd)) {
        return NULL;
    }

    stream = (xx_tivoli_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(inner);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    if (!xx_tivoli_parse_cpio(stream, inner, (int64_t)inner_size,
                              self->base_address, span, pd)) {
        /* Not a cpio: the whole unwrapped stream is the one member, which is
         * what the reference extractor produces. Any members the failed walk
         * had already added are dropped with it. */
        xx_tivoli_stream_free(stream);
        stream = (xx_tivoli_stream *)xx_mem_alloc(sizeof(*stream));
        if (!stream) {
            xx_mem_free(inner);
            return NULL;
        }
        xx_mem_zero(stream, sizeof(*stream));

        if ((int64_t)inner_size > XX_TIVOLI_MAX_DECODED) goto fail;
        name = xx_str_dup(XX_TIVOLI_FALLBACK_NAME);
        if (!name) goto fail;
        if (!xx_tivoli_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }
        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        /* The whole stream: nothing precedes it, so the slice offset is 0. */
        member.header_offset = 0;
        member.header_size = 0;
        member.data_offset = self->base_address;
        member.compressed_size = span;
        member.uncompressed_size = (int64_t)inner_size;
        member.method = XX_TIVOLI_METHOD_FILEPACK;
        member.timestamp = 0U;
        member.is_folder = false;
        if (!xx_tivoli_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
    }
    xx_mem_free(inner);
    inner = NULL;

    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    if (inner) xx_mem_free(inner);
    xx_tivoli_stream_free(stream);
    return NULL;
}


static bool xx_tivoli_unwrap(Abstractformat *self, int64_t offset,
                             int64_t size, uint8_t **inner, size_t *inner_size,
                             xx_pd_struct *pd);

static bool xx_tivoli_decode(Abstractformat *self,
                             const xx_tivoli_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *inner = NULL;
    size_t inner_size = 0U;
    uint8_t *output;
    int64_t slice_offset;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The chain is the only thing this container knows how to be; a member
     * carrying any other value was not published by this parse. */
    if (member->method != XX_TIVOLI_METHOD_FILEPACK) return false;
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_TIVOLI_MAX_DECODED) {
        return false;
    }
    if (member->header_offset < 0 || member->header_size < 0) return false;
    slice_offset = member->header_offset + member->header_size;
    if (slice_offset < 0) return false;

    /* data_offset/compressed_size are the whole container: a member's bytes
     * exist nowhere else, so the entire chain is decoded and the member is
     * sliced out of the result. */
    if (!xx_tivoli_unwrap(self, member->data_offset, member->compressed_size,
                          &inner, &inner_size, pd)) {
        return false;
    }
    /* The slice is re-checked against the stream that was actually produced,
     * not against the one parse measured: an extraction that ran against a
     * changed file must fail rather than read past the buffer. */
    if (!xx_tivoli_range_within((int64_t)inner_size, slice_offset,
                                member->uncompressed_size)) {
        xx_mem_free(inner);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(inner);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(member->uncompressed_size != 0
                                         ? (size_t)member->uncompressed_size
                                         : 1U);
    if (!output) {
        xx_mem_free(inner);
        return false;
    }
    if (member->uncompressed_size != 0) {
        xx_rt_memcpy(output, inner + (size_t)slice_offset,
                     (size_t)member->uncompressed_size);
    }
    xx_mem_free(inner);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_tivoli_init(xx_tivoli *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_TIVOLI;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-tivoli-filepack");
    xx_format_set_extension(&archive->format, "pkt");
    archive->format.check_is_valid = xx_tivoli_check_is_valid;
    archive->format.handle_base_info = xx_tivoli_handle_base_info;
    archive->format.get_format_size = xx_tivoli_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_tivoli_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_tivoli_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_tivoli_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_tivoli_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_tivoli_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_tivoli_free_archive_records_reading;
    archive->format.destroy = xx_tivoli_vtable_destroy;
}

xx_tivoli *xx_tivoli_create(xx_io_device *device, int64_t base_address) {
    xx_tivoli *archive = (xx_tivoli *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_tivoli_init(archive, device, base_address);
    return archive;
}

void xx_tivoli_destroy(xx_tivoli *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_tivoli_free(xx_tivoli *archive) {
    if (!archive) return;
    xx_tivoli_destroy(archive);
    xx_mem_free(archive);
}

static void xx_tivoli_vtable_destroy(Abstractformat *self) {
    xx_tivoli_destroy((xx_tivoli *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_tivoli_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tivoli_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_tivoli_parse(self, pd);
    if (!stream) return false;
    xx_tivoli_stream_free(stream);
    return true;
}

bool xx_tivoli_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tivoli *archive = (xx_tivoli *)self;
    xx_tivoli_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_tivoli_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_tivoli_stream_free(stream);
    return true;
}

int64_t xx_tivoli_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_tivoli_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_tivoli *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_tivoli_set_record(xx_archive_record *record,
                                 const xx_tivoli_member *member) {
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

static bool xx_tivoli_copy_options(xx_list_s *target,
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

static const xx_var *xx_tivoli_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_tivoli_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tivoli_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_tivoli_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_tivoli_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_tivoli_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_tivoli_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_tivoli_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_tivoli_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_tivoli_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_tivoli_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tivoli_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_tivoli_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_tivoli_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_tivoli_stream *stream;
    const xx_tivoli_member *member;
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
    stream = (xx_tivoli_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_tivoli_path_safe(member->name)) return false;

    path_option = xx_tivoli_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_tivoli_decode(self, member, &plain, &plain_size, pd);
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
        !xx_tivoli_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_tivoli_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
