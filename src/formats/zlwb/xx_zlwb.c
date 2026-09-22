/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZLWB archives.
 *
 *   header, 0x1e bytes:
 *     0x00  "ZLWB"
 *     0x04  0x1a            (DOS end-of-file, part of the signature)
 *     0x05  u32 LE version, must be 1
 *     0x12  i32 LE member count
 *     0x16  i32 LE directory offset
 *
 * The directory is a chain of blobs starting at the directory offset. Each
 * blob has a 0x10-byte header:
 *
 *     0x00  u32 LE unused
 *     0x04  i32 LE blob size      (compressed directory entry)
 *     0x08  i32 LE record size    (0x110 or 0x228, nothing else)
 *     0x0c  u32 LE unused
 *
 * followed by blob size bytes that are a complete zlib stream inflating to
 * exactly record size bytes. The record size is not a hint: it names which of
 * two layouts the inflated record uses.
 *
 *   short record, 0x110 bytes:
 *     0x000  Delphi ShortString name (length byte, then that many chars)
 *     0x100  i32 LE data offset
 *     0x104  i32 LE compressed size
 *     0x108  i32 LE uncompressed size
 *     0x10c  u32 LE CRC, not checked here
 *
 *   long record, 0x228 bytes:
 *     0x006  ShortString member name
 *     0x106  ShortString installer destination path, e.g. "<App>\NAME.DLL"
 *     0x218  i32 LE data offset
 *     0x21c  i32 LE compressed size
 *     0x220  i32 LE uncompressed size
 *     0x224  u32 LE CRC, not checked here
 *
 * The two name fields of a long record are NOT alternatives: the path field
 * is where the installer would put the file and the name field is the member
 * itself, and they differ whenever one payload is installed under an
 * application-specific name.  A member's published name therefore takes its
 * directory from the path field and its leaf from the name field.  The
 * placeholder root of a destination path ("<App>", "<Sys>") is folded to
 * filesystem-legal bytes on the way out, the same way the reference unpacker
 * writes it.
 *
 * Member data is a zlib stream too; the container carries no per-member
 * method field, so every member is tagged with one synthetic method and the
 * decoder refuses anything else.
 *
 * Identification does not rest on the magic. "ZLWB" + 0x1a + version 1 is
 * eleven fixed bytes, and on top of that the first directory blob must
 * inflate cleanly to one of exactly two record sizes and authenticate its
 * Adler-32. A stray "ZLWB" in unrelated data does not survive that.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zlwb/xx_zlwb.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"

#include <stdio.h>

#define XX_ZLWB_COPY_CHUNK (64 * 1024)

typedef struct xx_zlwb_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_zlwb_member;

typedef struct xx_zlwb_stream_s {
    xx_zlwb_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_zlwb_stream;

static void xx_zlwb_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_zlwb_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_zlwb_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Map one byte onto a byte that is legal inside a path component.  The long
 * record's path field is an installer destination such as "<App>\NAME.DLL",
 * so the angle brackets of a placeholder root are routine and must be folded
 * rather than treated as a rejection. */
static char xx_zlwb_safe_char(uint8_t c) {
    if (c < 0x20U || c > 0x7EU) return '_';
    if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' ||
        c == '?' || c == '*')
        return '_';
    return (char)c;
}

/* The last path component of @p path, or NULL when there is none. */
static const char *xx_zlwb_last_part(const char *path) {
    const char *last = path;
    const char *cursor;
    if (!path) return NULL;
    for (cursor = path; *cursor; ++cursor)
        if (*cursor == '\\' || *cursor == '/') last = cursor + 1;
    return *last ? last : NULL;
}

/* Build the member's path from the long record's two name fields.
 *
 * The record carries a destination path ("<App>\WheelRes.DLL") and the
 * member's own name ("WheelRes.CHS"); they differ because one installer
 * payload is written out under an application-specific name.  The directory
 * therefore comes from the path field and the leaf from the name field, which
 * is what the reference unpacker lays down.  @p path may be NULL, in which
 * case the leaf stands alone.  Traversal components are dropped, separators
 * are normalized to '/', and every component is folded to bytes a filesystem
 * will accept.  Returns NULL only on allocation failure or an empty result. */
static char *xx_zlwb_build_name(const char *path, const char *leaf) {
    char *result;
    size_t path_length = path ? xx_str_len(path) : 0U;
    size_t leaf_length = leaf ? xx_str_len(leaf) : 0U;
    size_t directory_length = 0U;
    size_t input, output = 0U;

    if (!leaf || leaf_length == 0U) return NULL;
    /* Everything up to the last separator of the path field is the directory;
     * the leaf the path field names is discarded in favour of @p leaf. */
    for (input = 0U; input < path_length; ++input) {
        if (path[input] == '\\' || path[input] == '/') directory_length = input;
    }
    result = (char *)xx_mem_alloc(directory_length + leaf_length + 2U);
    if (!result) return NULL;
    input = 0U;
    while (input < directory_length) {
        size_t start, end, component_start;
        while (input < directory_length &&
               (path[input] == '\\' || path[input] == '/'))
            ++input;
        start = input;
        while (input < directory_length && path[input] != '\\' &&
               path[input] != '/')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && path[start] == '.'))
            continue;
        /* A destination path that climbs out of the extraction directory is
         * refused rather than quietly flattened. */
        if (end - start == 2U && path[start] == '.' &&
            path[start + 1U] == '.') {
            xx_mem_free(result);
            return NULL;
        }
        if (output != 0U) result[output++] = '/';
        component_start = output;
        while (start < end)
            result[output++] = xx_zlwb_safe_char((uint8_t)path[start++]);
        while (output > component_start &&
               (result[output - 1U] == ' ' || result[output - 1U] == '.'))
            --output;
        if (output == component_start) result[output++] = '_';
    }
    if (output != 0U) result[output++] = '/';
    {
        size_t component_start = output;
        for (input = 0U; input < leaf_length; ++input) {
            char c = leaf[input];
            result[output++] = (c == '\\' || c == '/')
                                   ? '_' : xx_zlwb_safe_char((uint8_t)c);
        }
        while (output > component_start &&
               (result[output - 1U] == ' ' || result[output - 1U] == '.'))
            --output;
        if (output == component_start) result[output++] = '_';
    }
    result[output] = '\0';
    return result;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_zlwb_path_safe(const char *name) {
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

static void xx_zlwb_stream_free(void *pointer) {
    xx_zlwb_stream *stream = (xx_zlwb_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_zlwb_add(xx_zlwb_stream *stream,
                          const xx_zlwb_member *member) {
    xx_zlwb_member *grown = (xx_zlwb_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ZLWB_HEADER_SIZE 0x1e
#define XX_ZLWB_BLOB_HEADER_SIZE 0x10
#define XX_ZLWB_RECORD_SHORT 0x110
#define XX_ZLWB_RECORD_LONG 0x228
#define XX_ZLWB_MAX_MEMBERS 100000
#define XX_ZLWB_MAX_BLOB_SIZE 0x100000
#define XX_ZLWB_METHOD_ZLIB 1U
#define XX_ZLWB_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_zlwb_le32(const uint8_t *data);
static char *xx_zlwb_short_string(const uint8_t *record, size_t record_size, size_t offset);
static xx_zlwb_stream *xx_zlwb_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_zlwb_decode(Abstractformat *self, const xx_zlwb_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);



static uint32_t xx_zlwb_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Delphi ShortString: a length byte and that many characters, no terminator.
 * The name is the only text in the record, so it is also the last sanity
 * check on a blob that inflated to the right size by coincidence. */
static char *xx_zlwb_short_string(const uint8_t *record, size_t record_size,
                                  size_t offset) {
    char text[256];
    size_t length;
    size_t index;

    if (offset >= record_size) return NULL;
    length = (size_t)record[offset];
    if (length == 0U || offset + 1U + length > record_size) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t byte = record[offset + 1U + index];
        /* Delphi stores raw bytes here, but a ZLWB name is a DOS-era path:
         * anything outside printable ASCII means this is not a record. */
        if (byte < 0x20U || byte > 0x7EU) return NULL;
        text[index] = (char)byte;
    }
    text[length] = '\0';
    return xx_str_dup(text);
}

static xx_zlwb_stream *xx_zlwb_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_zlwb_stream *stream;
    uint8_t header[XX_ZLWB_HEADER_SIZE];
    uint8_t blob_header[XX_ZLWB_BLOB_HEADER_SIZE];
    uint8_t *blob = NULL;
    uint8_t *record = NULL;
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t directory_offset;
    int64_t position;
    int64_t archive_size;
    int64_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_ZLWB_HEADER_SIZE) return NULL;
    if (!xx_zlwb_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (header[0] != 'Z' || header[1] != 'L' || header[2] != 'W' ||
        header[3] != 'B') {
        return NULL;
    }
    /* The 0x1a and the version are part of the signature, not decoration:
     * four magic letters alone match far too much. */
    if (header[4] != 0x1aU) return NULL;
    if (xx_zlwb_le32(header + 5) != 1U) return NULL;

    /* Both fields are written as signed 32-bit by the producer. */
    count = (int64_t)(int32_t)xx_zlwb_le32(header + 0x12);
    directory_offset = (int64_t)(int32_t)xx_zlwb_le32(header + 0x16);
    if (count < 0 || count > XX_ZLWB_MAX_MEMBERS) return NULL;

    stream = (xx_zlwb_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    archive_size = XX_ZLWB_HEADER_SIZE;
    position = directory_offset;
    /* An archive with no members is legal; it is just the header. */
    if (count > 0 && !xx_zlwb_range_within(span, directory_offset, 0)) {
        goto fail;
    }

    for (index = 0; index < count; ++index) {
        xx_zlwb_member member;
        char *name;
        int64_t blob_size;
        int64_t record_size;
        int64_t data_offset;
        int64_t compressed_size;
        int64_t uncompressed_size;
        size_t written = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_zlwb_range_within(span, position, XX_ZLWB_BLOB_HEADER_SIZE) ||
            !xx_zlwb_read_at(self, self->base_address + position, blob_header,
                             sizeof(blob_header))) {
            goto fail;
        }
        blob_size = (int64_t)(int32_t)xx_zlwb_le32(blob_header + 4);
        record_size = (int64_t)(int32_t)xx_zlwb_le32(blob_header + 8);
        if (blob_size <= 0 || blob_size > XX_ZLWB_MAX_BLOB_SIZE) goto fail;
        /* The record size is not a hint, it names the layout: any other value
         * means the bytes being walked are not a ZLWB directory. */
        if (record_size != XX_ZLWB_RECORD_SHORT &&
            record_size != XX_ZLWB_RECORD_LONG) {
            goto fail;
        }
        if (!xx_zlwb_range_within(span, position + XX_ZLWB_BLOB_HEADER_SIZE,
                                  blob_size)) {
            goto fail;
        }

        blob = (uint8_t *)xx_mem_alloc((size_t)blob_size);
        record = (uint8_t *)xx_mem_alloc((size_t)record_size);
        if (!blob || !record) goto fail;
        if (!xx_zlwb_read_at(self,
                             self->base_address + position +
                                 XX_ZLWB_BLOB_HEADER_SIZE,
                             blob, (size_t)blob_size)) {
            goto fail;
        }

        /* This is the format's real defence against a false positive. Every
         * directory blob must be a complete zlib stream that inflates to
         * exactly the declared record size and whose Adler-32 matches. Loosen
         * any of the three -- accept a short inflate, skip the trailer, treat
         * the record size as advisory -- and arbitrary data starts parsing as
         * a member list. */
        if (!xx_zlib_stream_header_is_valid(blob, (size_t)blob_size) ||
            !xx_zlib_stream_decode_memory(blob, (size_t)blob_size, record,
                                          (size_t)record_size, &written) ||
            written != (size_t)record_size ||
            !xx_zlib_stream_trailer_matches(blob, (size_t)blob_size, record,
                                            written)) {
            goto fail;
        }

        if (record_size == XX_ZLWB_RECORD_SHORT) {
            name = xx_zlwb_short_string(record, (size_t)record_size, 0x000U);
            data_offset = (int64_t)(int32_t)xx_zlwb_le32(record + 0x100);
            compressed_size = (int64_t)(int32_t)xx_zlwb_le32(record + 0x104);
            uncompressed_size =
                (int64_t)(int32_t)xx_zlwb_le32(record + 0x108);
        } else {
            /* The record names the member twice: the path field is the
             * installer destination and the name field is the member itself.
             * The directory comes from the first, the leaf from the second. */
            char *full = xx_zlwb_short_string(record, (size_t)record_size,
                                              0x106U);
            char *bare = xx_zlwb_short_string(record, (size_t)record_size,
                                              0x006U);
            /* A writer that leaves the name field empty still fills the path
             * field, so fall back to that path's own last component. */
            const char *leaf = bare ? bare : xx_zlwb_last_part(full);
            name = xx_zlwb_build_name(full, leaf);
            /* A destination path that tries to climb out is dropped, not
             * fatal: the member itself is still perfectly listable under its
             * own name, and one bad path must not cost the whole archive. */
            if (!name) name = xx_zlwb_build_name(NULL, leaf);
            xx_str_free(full);
            xx_str_free(bare);
            data_offset = (int64_t)(int32_t)xx_zlwb_le32(record + 0x218);
            compressed_size = (int64_t)(int32_t)xx_zlwb_le32(record + 0x21c);
            uncompressed_size =
                (int64_t)(int32_t)xx_zlwb_le32(record + 0x220);
        }
        if (!name) goto fail;
        if (data_offset < 0 || compressed_size < 0 ||
            uncompressed_size < 0) {
            xx_str_free(name);
            goto fail;
        }
        /* A member whose extent runs past EOF is a rejection, not a clamp. */
        if (!xx_zlwb_range_within(span, data_offset, compressed_size)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + position;
        member.header_size = XX_ZLWB_BLOB_HEADER_SIZE + blob_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = XX_ZLWB_METHOD_ZLIB;
        member.is_folder = false;
        if (!xx_zlwb_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        position += XX_ZLWB_BLOB_HEADER_SIZE + blob_size;
        /* Member data may sit anywhere, before or after the directory, so the
         * archive end is the high-water mark of both chains. */
        if (position > archive_size) archive_size = position;
        if (data_offset + compressed_size > archive_size) {
            archive_size = data_offset + compressed_size;
        }

        xx_mem_free(blob);
        xx_mem_free(record);
        blob = NULL;
        record = NULL;
    }

    if (archive_size > span) archive_size = span;
    stream->archive_size = archive_size;
    return stream;

fail:
    xx_mem_free(blob);
    xx_mem_free(record);
    xx_zlwb_stream_free(stream);
    return NULL;
}


/* The container has no method field: everything it holds -- directory blobs
 * and member data alike -- is a zlib stream. The synthetic value keeps the
 * switch below honest, so a future layout that does carry a method cannot
 * fall through to "stored" and hand back compressed bytes as plain data. */
/* The uncompressed size comes straight out of the archive, so it is
 * attacker-controlled: refuse rather than attempt a huge allocation. */

static bool xx_zlwb_decode(Abstractformat *self,
                           const xx_zlwb_member *member, uint8_t **out,
                           size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->method != XX_ZLWB_METHOD_ZLIB) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_ZLWB_MAX_DECODED ||
        member->uncompressed_size > XX_ZLWB_MAX_DECODED) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    output = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!input || !output) goto decode_fail;
    if (!xx_zlwb_read_at(self, member->data_offset, input,
                         (size_t)member->compressed_size)) {
        goto decode_fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto decode_fail;

    if (!xx_zlib_stream_header_is_valid(input,
                                        (size_t)member->compressed_size)) {
        goto decode_fail;
    }
    if (!xx_zlib_stream_decode_memory(input, (size_t)member->compressed_size,
                                      output,
                                      (size_t)member->uncompressed_size,
                                      &written)) {
        goto decode_fail;
    }
    /* A truncated member must fail, not succeed short: the caller has no way
     * to tell the difference once the bytes are handed over. */
    if (written != (size_t)member->uncompressed_size) goto decode_fail;
    /* The directory records the exact stream length, so the RFC 1950 trailer
     * is inside the member and the Adler-32 can be insisted on. */
    if (!xx_zlib_stream_trailer_matches(input,
                                        (size_t)member->compressed_size,
                                        output, written)) {
        goto decode_fail;
    }

    xx_mem_free(input);
    *out = output;
    *out_size = written;
    return true;

decode_fail:
    xx_mem_free(input);
    xx_mem_free(output);
    return false;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_zlwb_init(xx_zlwb *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZLWB;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zlwb");
    xx_format_set_extension(&archive->format, "zlw");
    archive->format.check_is_valid = xx_zlwb_check_is_valid;
    archive->format.handle_base_info = xx_zlwb_handle_base_info;
    archive->format.get_format_size = xx_zlwb_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zlwb_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zlwb_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zlwb_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zlwb_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zlwb_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zlwb_free_archive_records_reading;
    archive->format.destroy = xx_zlwb_vtable_destroy;
}

xx_zlwb *xx_zlwb_create(xx_io_device *device, int64_t base_address) {
    xx_zlwb *archive = (xx_zlwb *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zlwb_init(archive, device, base_address);
    return archive;
}

void xx_zlwb_destroy(xx_zlwb *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_zlwb_free(xx_zlwb *archive) {
    if (!archive) return;
    xx_zlwb_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zlwb_vtable_destroy(Abstractformat *self) {
    xx_zlwb_destroy((xx_zlwb *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_zlwb_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_zlwb_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_zlwb_parse(self, pd);
    if (!stream) return false;
    xx_zlwb_stream_free(stream);
    return true;
}

bool xx_zlwb_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zlwb *archive = (xx_zlwb *)self;
    xx_zlwb_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_zlwb_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_zlwb_stream_free(stream);
    return true;
}

int64_t xx_zlwb_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zlwb_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zlwb *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zlwb_set_record(xx_archive_record *record,
                                 const xx_zlwb_member *member) {
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

static bool xx_zlwb_copy_options(xx_list_s *target,
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

static const xx_var *xx_zlwb_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_zlwb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_zlwb_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_zlwb_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_zlwb_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_zlwb_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_zlwb_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_zlwb_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zlwb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zlwb_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_zlwb_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zlwb_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_zlwb_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_zlwb_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_zlwb_stream *stream;
    const xx_zlwb_member *member;
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
    stream = (xx_zlwb_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_zlwb_path_safe(member->name)) return false;

    path_option = xx_zlwb_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_zlwb_decode(self, member, &plain, &plain_size, pd);
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
        !xx_zlwb_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_zlwb_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
