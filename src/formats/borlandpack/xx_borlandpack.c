/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Borland PACK archives.
 *
 * Every field in the container is ASCII text, written by Turbo Pascal
 * write(x:width): right justified decimal, blank padded on the left.
 *
 *   preamble, 36 bytes at offset 0:
 *     0x00  "This is a packed file.", 22 bytes
 *     0x16  0x1A - the DOS EOF is part of the magic. It is what makes TYPE
 *           stop after the notice, and it is what keeps the archive from
 *           being confused with SEA/PKWARE ARC, whose gate wants 0x1A at
 *           offset 0 rather than at offset 22.
 *     0x17  ":CM ", 4 bytes
 *     0x1B  2 decimal digits - the compression method, archive-wide. There
 *           is no per-member method field anywhere in the container.
 *     0x1D  ' '
 *     0x1E  3 decimal digits - the packer version, e.g. "100" = 1.00
 *     0x21  ':'
 *     0x22  CRLF
 *
 *   members follow back to back from 0x24. Each member is two header lines
 *   and then its payload:
 *
 *     line 1:  '!' + name + fixed 26 byte tail + CRLF
 *       The name is VARIABLE width and blank padded on the right; the tail
 *       is what is fixed, so the tail has to be located from the CRLF
 *       backwards. Parsing line 1 from the left with an assumed name width
 *       breaks on every name that is not as long as the first one.
 *       tail, 26 bytes, relative to its own start:
 *         +0   11 chars  uncompressed size, decimal, blank padded
 *         +11  ' '
 *         +12  4 chars   checksum, uppercase hex
 *         +16  ' '
 *         +17  4 chars   DOS date, uppercase hex
 *         +21  ' '
 *         +22  4 chars   DOS time, uppercase hex
 *
 *     line 2:  10 chars packed size, decimal, blank padded + CRLF
 *
 *     payload: exactly `packed size` bytes, a headerless Unix compress
 *       stream - byte 0 is the flags byte (bit 7 block mode, bits 0..4 max
 *       code width), the codes follow. The 1F 9D magic a .Z file would
 *       carry is not stored, so this reader synthesises it before handing
 *       the range to the library decoder.
 *
 * There is no trailer, no central directory and no member count. The only
 * end-of-archive signal is landing exactly on EOF, so a leftover byte or an
 * overrunning payload is truncation, and emitting the members parsed so far
 * would advertise a partial listing as a complete one.
 *
 * The checksum is carried but never verified: an exhaustive CRC-16 sweep
 * failed to identify the algorithm, so it must not gate extraction.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/borlandpack/xx_borlandpack.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/algo/compress/xx_compress.h"

#include <stdio.h>

#define XX_BORLANDPACK_COPY_CHUNK (64 * 1024)

typedef struct xx_borlandpack_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_borlandpack_member;

typedef struct xx_borlandpack_stream_s {
    xx_borlandpack_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_borlandpack_stream;

static void xx_borlandpack_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_borlandpack_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_borlandpack_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_borlandpack_path_safe(const char *name) {
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

static void xx_borlandpack_stream_free(void *pointer) {
    xx_borlandpack_stream *stream = (xx_borlandpack_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_borlandpack_add(xx_borlandpack_stream *stream,
                          const xx_borlandpack_member *member) {
    xx_borlandpack_member *grown = (xx_borlandpack_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_BORLANDPACK_PREAMBLE_SIZE 36
#define XX_BORLANDPACK_MAGIC_SIZE 23
#define XX_BORLANDPACK_TAIL_SIZE 26
#define XX_BORLANDPACK_ORIGSIZE_FIELD 11
#define XX_BORLANDPACK_PACKSIZE_FIELD 10
#define XX_BORLANDPACK_HEX_FIELD 4
#define XX_BORLANDPACK_SIZELINE_SIZE (XX_BORLANDPACK_PACKSIZE_FIELD + 2)
#define XX_BORLANDPACK_MAX_NAME 255
#define XX_BORLANDPACK_MIN_MEMBER \
    (1 + 1 + XX_BORLANDPACK_TAIL_SIZE + 2 + XX_BORLANDPACK_SIZELINE_SIZE)
#define XX_BORLANDPACK_HEADER_WINDOW                                      \
    (1 + XX_BORLANDPACK_MAX_NAME + XX_BORLANDPACK_TAIL_SIZE + 2 +         \
     XX_BORLANDPACK_SIZELINE_SIZE)
#define XX_BORLANDPACK_MAX_MEMBERS 100000
#define XX_BORLANDPACK_MAX_UNCOMPRESSED INT64_C(0x7fffffff)
#define XX_BORLANDPACK_METHOD_LZW 1U
#define XX_BORLANDPACK_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static bool xx_borlandpack_is_digit(uint8_t value);
static bool xx_borlandpack_parse_size(const uint8_t *field, int64_t width, int64_t maximum, int64_t *value);
static bool xx_borlandpack_parse_hex(const uint8_t *field, uint32_t *value);
static int64_t xx_borlandpack_find_crlf(const uint8_t *window, int64_t size, int64_t from);
static xx_borlandpack_stream *xx_borlandpack_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_borlandpack_decode(Abstractformat *self, const xx_borlandpack_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Line 2 is a fixed width size field plus CRLF, always. */
/* '!' + one name byte + tail + CRLF + line 2. The payload is deliberately
 * not counted: a zero length payload is not attested but is not structurally
 * forbidden either, and rejecting the archive for it would be a guess. */
/* One bounded read per member covers both header lines at maximum width. */

static bool xx_borlandpack_is_digit(uint8_t value) {
    return value >= (uint8_t)'0' && value <= (uint8_t)'9';
}

/* Turbo Pascal write(x:width): right justified decimal, blank padded on the
 * left. Only the leading run may be blank; a blank inside the number, or a
 * wholly blank field, is not a number. */
static bool xx_borlandpack_parse_size(const uint8_t *field, int64_t width,
                                      int64_t maximum, int64_t *value) {
    int64_t index = 0;
    int64_t result = 0;

    if (!field || !value || width <= 0) return false;
    while (index < width && field[index] == (uint8_t)' ') ++index;
    if (index == width) return false;
    for (; index < width; ++index) {
        if (!xx_borlandpack_is_digit(field[index])) return false;
        result = (result * 10) + (int64_t)(field[index] - (uint8_t)'0');
        if (result > maximum) return false;
    }
    *value = result;
    return true;
}

/* The three tail fields are uppercase hex only; lower case never occurs, and
 * accepting it would widen the detection gate for no gain. */
static bool xx_borlandpack_parse_hex(const uint8_t *field, uint32_t *value) {
    uint32_t result = 0U;
    int64_t index;

    if (!field || !value) return false;
    for (index = 0; index < XX_BORLANDPACK_HEX_FIELD; ++index) {
        const uint8_t character = field[index];
        uint32_t digit;
        if (xx_borlandpack_is_digit(character)) {
            digit = (uint32_t)(character - (uint8_t)'0');
        } else if (character >= (uint8_t)'A' && character <= (uint8_t)'F') {
            digit = (uint32_t)(character - (uint8_t)'A') + 10U;
        } else {
            return false;
        }
        result = (result << 4) | digit;
    }
    *value = result;
    return true;
}

static int64_t xx_borlandpack_find_crlf(const uint8_t *window, int64_t size,
                                        int64_t from) {
    int64_t index;
    for (index = from; index + 1 < size; ++index) {
        if (window[index] == (uint8_t)'\r' &&
            window[index + 1] == (uint8_t)'\n') {
            return index;
        }
    }
    return -1;
}

static xx_borlandpack_stream *xx_borlandpack_parse(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    static const uint8_t magic[XX_BORLANDPACK_MAGIC_SIZE] = {
        'T', 'h', 'i', 's', ' ', 'i', 's', ' ', 'a', ' ', 'p', 'a',
        'c', 'k', 'e', 'd', ' ', 'f', 'i', 'l', 'e', '.', 0x1A};
    xx_borlandpack_stream *stream = NULL;
    uint8_t preamble[XX_BORLANDPACK_PREAMBLE_SIZE];
    uint8_t window[XX_BORLANDPACK_HEADER_WINDOW];
    int64_t total;
    int64_t span;
    int64_t offset;
    uint32_t method;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_BORLANDPACK_PREAMBLE_SIZE + XX_BORLANDPACK_MIN_MEMBER) {
        return NULL;
    }
    if (!xx_borlandpack_read_at(self, self->base_address, preamble,
                                sizeof(preamble))) {
        return NULL;
    }
    /* The 0x1A at the end of the notice is part of the magic, not decoration:
     * dropping it would let any file that merely quotes the sentence in
     * running text parse as an archive. */
    if (xx_rt_memcmp(preamble, magic, sizeof(magic)) != 0) return NULL;
    /* ":CM nn vvv:" CRLF. Every one of these positions is fixed, and the
     * shape of the token is the second half of the detection gate - the
     * sentence alone is plain English and cheap to hit by accident. */
    if (preamble[23] != (uint8_t)':' || preamble[24] != (uint8_t)'C' ||
        preamble[25] != (uint8_t)'M' || preamble[26] != (uint8_t)' ' ||
        !xx_borlandpack_is_digit(preamble[27]) ||
        !xx_borlandpack_is_digit(preamble[28]) ||
        preamble[29] != (uint8_t)' ' ||
        !xx_borlandpack_is_digit(preamble[30]) ||
        !xx_borlandpack_is_digit(preamble[31]) ||
        !xx_borlandpack_is_digit(preamble[32]) ||
        preamble[33] != (uint8_t)':' || preamble[34] != (uint8_t)'\r' ||
        preamble[35] != (uint8_t)'\n') {
        return NULL;
    }
    /* Archive-wide: the same method number goes on every member, because
     * that is the only place the container states one. */
    method = (uint32_t)((preamble[27] - (uint8_t)'0') * 10 +
                        (preamble[28] - (uint8_t)'0'));

    stream = (xx_borlandpack_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_BORLANDPACK_PREAMBLE_SIZE;
    while (offset < span) {
        xx_borlandpack_member member;
        char *name = NULL;
        const uint8_t *tail;
        const uint8_t *size_line;
        int64_t window_size;
        int64_t line_end;
        int64_t name_size;
        int64_t header_size;
        int64_t data_offset;
        int64_t uncompressed_size = 0;
        int64_t compressed_size = 0;
        int64_t cursor;
        uint32_t checksum = 0U;
        uint32_t dos_date = 0U;
        uint32_t dos_time = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_BORLANDPACK_MAX_MEMBERS) goto fail;

        window_size = span - offset;
        if (window_size > XX_BORLANDPACK_HEADER_WINDOW) {
            window_size = XX_BORLANDPACK_HEADER_WINDOW;
        }
        if (window_size < XX_BORLANDPACK_MIN_MEMBER) goto fail;
        if (!xx_borlandpack_read_at(self, self->base_address + offset, window,
                                    (size_t)window_size)) {
            goto fail;
        }
        if (window[0] != (uint8_t)'!') goto fail;

        line_end = xx_borlandpack_find_crlf(window, window_size, 1);
        /* '!' + at least one name byte + the fixed 26 byte tail, and line 2
         * must still fit inside the window that was read. */
        if (line_end < 1 + 1 + XX_BORLANDPACK_TAIL_SIZE ||
            line_end + 2 + XX_BORLANDPACK_SIZELINE_SIZE > window_size) {
            goto fail;
        }

        /* Located from the CRLF backwards: the name is the variable part. */
        tail = window + (line_end - XX_BORLANDPACK_TAIL_SIZE);
        if (tail[11] != (uint8_t)' ' || tail[16] != (uint8_t)' ' ||
            tail[21] != (uint8_t)' ') {
            goto fail;
        }
        if (!xx_borlandpack_parse_size(tail, XX_BORLANDPACK_ORIGSIZE_FIELD,
                                       XX_BORLANDPACK_MAX_UNCOMPRESSED,
                                       &uncompressed_size) ||
            !xx_borlandpack_parse_hex(tail + 12, &checksum) ||
            !xx_borlandpack_parse_hex(tail + 17, &dos_date) ||
            !xx_borlandpack_parse_hex(tail + 22, &dos_time)) {
            goto fail;
        }

        name_size = line_end - XX_BORLANDPACK_TAIL_SIZE - 1;
        /* The writer blank pads a short name out to the field it uses; only
         * the trailing padding is decoration, everything else is the name. */
        while (name_size > 0 && window[1 + name_size - 1] == (uint8_t)' ') {
            --name_size;
        }
        if (name_size <= 0 || name_size > XX_BORLANDPACK_MAX_NAME) goto fail;
        for (cursor = 0; cursor < name_size; ++cursor) {
            const uint8_t character = window[1 + cursor];
            if (character < 0x20U || character > 0x7EU) goto fail;
            /* The container stores a bare DOS 8.3 name and has no directory
             * concept at all, so a separator here is a path smuggled through
             * a format that cannot express one. */
            if (character == (uint8_t)'/' || character == (uint8_t)'\\' ||
                character == (uint8_t)':') {
                goto fail;
            }
        }
        name = (char *)xx_mem_alloc((size_t)name_size + 1U);
        if (!name) goto fail;
        for (cursor = 0; cursor < name_size; ++cursor) {
            name[cursor] = (char)window[1 + cursor];
        }
        name[name_size] = '\0';
        if (!xx_borlandpack_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        size_line = window + line_end + 2;
        if (size_line[XX_BORLANDPACK_PACKSIZE_FIELD] != (uint8_t)'\r' ||
            size_line[XX_BORLANDPACK_PACKSIZE_FIELD + 1] != (uint8_t)'\n') {
            xx_str_free(name);
            goto fail;
        }
        header_size = line_end + 2 + XX_BORLANDPACK_SIZELINE_SIZE;
        data_offset = offset + header_size;
        /* The packed size is what advances the walk, so a payload running
         * past EOF is a rejection rather than a short read later. */
        if (!xx_borlandpack_parse_size(size_line,
                                       XX_BORLANDPACK_PACKSIZE_FIELD,
                                       span - data_offset,
                                       &compressed_size) ||
            !xx_borlandpack_range_within(span, data_offset, compressed_size)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = header_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = method;
        member.timestamp = ((uint64_t)dos_date << 16) | (uint64_t)dos_time;
        if (!xx_borlandpack_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        offset = data_offset + compressed_size;
    }

    /* No trailer, no central directory, no member count: landing exactly on
     * EOF is the only end-of-archive signal there is. A leftover byte means
     * truncation, and publishing the members parsed so far would advertise a
     * partial listing as a complete one. */
    if (offset != span || stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_borlandpack_stream_free(stream);
    return NULL;
}


/* The two-digit ":CM nn" token. Only 01 is attested, and it is a Unix
 * compress LZW stream. Any other token is refused rather than guessed:
 * there is no per-member method field to fall back on, and treating an
 * unknown method as stored produces garbage that looks like data. */
/* The declared original size is an 11 digit text field, so a crafted header
 * can name a member far larger than any buffer. Refuse rather than attempt
 * the allocation the container merely claims to need. */

static bool xx_borlandpack_decode(Abstractformat *self,
                                  const xx_borlandpack_member *member,
                                  uint8_t **out, size_t *out_size,
                                  xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    xx_io_device *source = NULL;
    xx_io_device *destination = NULL;
    size_t packed_size = 0U;
    int64_t written = -1;
    bool result = false;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->method != XX_BORLANDPACK_METHOD_LZW) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_BORLANDPACK_MAX_DECODED ||
        member->compressed_size > (int64_t)XX_BORLANDPACK_MAX_DECODED) {
        return false;
    }
    if ((uint64_t)member->compressed_size > (uint64_t)(SIZE_MAX - 2U)) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    /* The library decoder consumes a complete .Z transport: 1F 9D then the
     * flags byte. The container stores the flags byte and drops the magic,
     * so the magic is prepended here. Nothing else about the stream differs
     * - the code groups are measured from the first code bit either way. */
    packed_size = (size_t)member->compressed_size + 2U;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed) return false;
    packed[0] = XX_COMPRESS_MAGIC0;
    packed[1] = XX_COMPRESS_MAGIC1;
    if (!xx_borlandpack_read_at(self, member->data_offset, packed + 2,
                                (size_t)member->compressed_size)) {
        goto cleanup;
    }
    if (pd && xx_pd_is_stopped(pd)) goto cleanup;

    plain = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!plain) goto cleanup;

    source = xx_io_mem_open_ro(packed, packed_size);
    /* The destination is capped at exactly the declared length, so a stream
     * that decodes to more than the container promises runs out of room and
     * fails here rather than being silently truncated. */
    destination = xx_io_mem_open(plain, (size_t)member->uncompressed_size);
    if (!source || !destination) goto cleanup;

    if (!xx_compress_decode_device(source, 0, (int64_t)packed_size,
                                   destination, &written, pd)) {
        goto cleanup;
    }
    /* Exactly the promised length or nothing: a partially decoded member
     * reported as success is the one failure a caller cannot detect. */
    if (written != member->uncompressed_size) goto cleanup;
    if (pd && xx_pd_is_stopped(pd)) goto cleanup;

    *out = plain;
    *out_size = (size_t)member->uncompressed_size;
    plain = NULL;
    result = true;

cleanup:
    if (source) xx_io_close(source);
    if (destination) xx_io_close(destination);
    if (plain) xx_mem_free(plain);
    if (packed) xx_mem_free(packed);
    return result;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_borlandpack_init(xx_borlandpack *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_BORLANDPACK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-borland-pack");
    xx_format_set_extension(&archive->format, "arc");
    archive->format.check_is_valid = xx_borlandpack_check_is_valid;
    archive->format.handle_base_info = xx_borlandpack_handle_base_info;
    archive->format.get_format_size = xx_borlandpack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_borlandpack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_borlandpack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_borlandpack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_borlandpack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_borlandpack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_borlandpack_free_archive_records_reading;
    archive->format.destroy = xx_borlandpack_vtable_destroy;
}

xx_borlandpack *xx_borlandpack_create(xx_io_device *device, int64_t base_address) {
    xx_borlandpack *archive = (xx_borlandpack *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_borlandpack_init(archive, device, base_address);
    return archive;
}

void xx_borlandpack_destroy(xx_borlandpack *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_borlandpack_free(xx_borlandpack *archive) {
    if (!archive) return;
    xx_borlandpack_destroy(archive);
    xx_mem_free(archive);
}

static void xx_borlandpack_vtable_destroy(Abstractformat *self) {
    xx_borlandpack_destroy((xx_borlandpack *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_borlandpack_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_borlandpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_borlandpack_parse(self, pd);
    if (!stream) return false;
    xx_borlandpack_stream_free(stream);
    return true;
}

bool xx_borlandpack_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_borlandpack *archive = (xx_borlandpack *)self;
    xx_borlandpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_borlandpack_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_borlandpack_stream_free(stream);
    return true;
}

int64_t xx_borlandpack_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_borlandpack_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_borlandpack *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_borlandpack_set_record(xx_archive_record *record,
                                 const xx_borlandpack_member *member) {
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

static bool xx_borlandpack_copy_options(xx_list_s *target,
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

static const xx_var *xx_borlandpack_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_borlandpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_borlandpack_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_borlandpack_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_borlandpack_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_borlandpack_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_borlandpack_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_borlandpack_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_borlandpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_borlandpack_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_borlandpack_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_borlandpack_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_borlandpack_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_borlandpack_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_borlandpack_stream *stream;
    const xx_borlandpack_member *member;
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
    stream = (xx_borlandpack_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_borlandpack_path_safe(member->name)) return false;

    path_option = xx_borlandpack_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_borlandpack_decode(self, member, &plain, &plain_size, pd);
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
        !xx_borlandpack_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_borlandpack_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
