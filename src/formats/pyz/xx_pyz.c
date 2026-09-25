/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PyInstaller PYZ archives (the Python module store inside a frozen
 * executable, also found standalone as PYZ-00.pyz).
 *
 * Header (12 bytes):
 *   0x00  char[4]   "PYZ\0" magic; the NUL is part of it
 *   0x04  uint32    the Python bytecode magic of the interpreter that wrote
 *                   the archive - it changes with every release, so it is
 *                   carried through unchecked
 *   0x08  uint32    offset of the table of contents, BIG-endian; this is the
 *                   only big-endian field in the container
 *
 * Member data: zlib (RFC 1950) streams packed between the header and the
 * table of contents. Each stream ends exactly at the length the table of
 * contents records, so its Adler-32 trailer is present.
 *
 * Table of contents: from the offset at 0x08 to EOF, a marshalled Python
 * object, and the only thing in the file with any structure to it:
 *
 *   [ (name, (type, offset, size)), ... ]
 *
 *   name    dotted module name, a marshal string
 *   type    0 = module, 1 = package, 3 = namespace package (no stream at all)
 *   offset  absolute file offset of the zlib stream
 *   size    length of the zlib stream in bytes
 *
 * The container records no uncompressed size and no timestamp; a member's
 * decoded length is whatever its zlib stream produces, and the stream's
 * Adler-32 is what proves the decode ran to the end.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pyz/xx_pyz.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include <stdio.h>

#define XX_PYZ_COPY_CHUNK (64 * 1024)

typedef struct xx_pyz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_pyz_member;

typedef struct xx_pyz_stream_s {
    xx_pyz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_pyz_stream;

static void xx_pyz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_pyz_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_pyz_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_pyz_path_safe(const char *name) {
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

static void xx_pyz_stream_free(void *pointer) {
    xx_pyz_stream *stream = (xx_pyz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_pyz_add(xx_pyz_stream *stream,
                          const xx_pyz_member *member) {
    xx_pyz_member *grown = (xx_pyz_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_PYZ_HEADER_SIZE 12
#define XX_PYZ_MIN_SIZE 16
#define XX_PYZ_MAX_TOC (64 * 1024 * 1024)
#define XX_PYZ_MAX_MEMBERS 20000
#define XX_PYZ_MAX_OBJECTS 500000
#define XX_PYZ_MAX_DEPTH 64
#define XX_PYZ_MAX_SEQUENCE 200000
#define XX_PYZ_MAX_NAME 255
#define XX_PYZ_MIN_STREAM 6
#define XX_PYZ_MAX_DECODED (256 * 1024 * 1024)
#define XX_PYZ_INT64_MAX 0x7fffffffffffffffLL
#define XX_PYZ_ENTRY_MODULE 0
#define XX_PYZ_ENTRY_PACKAGE 1
#define XX_PYZ_ENTRY_NAMESPACE 3
#define XX_PYZ_KIND_OTHER 0
#define XX_PYZ_KIND_INT 1
#define XX_PYZ_KIND_STR 2
#define XX_PYZ_KIND_SEQ 3

typedef struct xx_pyz_value_s {
    int kind;
    int64_t number; /* KIND_INT */
    int64_t offset; /* KIND_STR: byte offset inside the TOC buffer */
    int64_t size;   /* KIND_STR: byte length */
    int64_t count;  /* KIND_SEQ: elements that follow */
} xx_pyz_value;
typedef struct xx_pyz_marshal_s {
    const uint8_t *data;
    int64_t size;
    int64_t position;
    int64_t objects;
    xx_pyz_value *refs;
    int64_t ref_count;
} xx_pyz_marshal;

/* Container constants. These live with the parse because the
 * generated file emits parse before decode. */

/* ------------------------------------------------- marshal (TOC) reader -- */


/* A marshalled object, reduced to the three shapes a PYZ table of contents is
 * allowed to contain. A sequence's elements are NOT consumed here: the caller
 * knows how many it expects and reads them itself, which is what lets this
 * reader refuse an unexpected shape instead of skipping over it. */




static uint16_t xx_pyz_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_pyz_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The TOC offset is the one big-endian field in the container. */
static uint32_t xx_pyz_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

/* marshal writes its lengths and small integers signed, so a value with the
 * top bit set is a malformed blob, not a 2GB+ one. */
static int64_t xx_pyz_le_i32(const uint8_t *data)
{
    uint32_t value = xx_pyz_le32(data);

    if (value & 0x80000000u) {
        return (int64_t)value - (int64_t)0x100000000LL;
    }
    return (int64_t)value;
}

static int64_t xx_pyz_le_i64(const uint8_t *data)
{
    uint64_t value =
        (uint64_t)xx_pyz_le32(data) | ((uint64_t)xx_pyz_le32(data + 4) << 32);

    /* Two's complement without a conversion whose result the standard leaves
     * implementation-defined. */
    if (value > (uint64_t)XX_PYZ_INT64_MAX) {
        return -(int64_t)(~value) - 1;
    }
    return (int64_t)value;
}

static bool xx_pyz_take(xx_pyz_marshal *reader, int64_t size,
                        const uint8_t **out)
{
    if ((size < 0) || (reader->position < 0) ||
        (reader->position > reader->size) ||
        (size > (reader->size - reader->position))) {
        return false;
    }
    *out = reader->data + reader->position;
    reader->position += size;
    return true;
}

static bool xx_pyz_read_length(xx_pyz_marshal *reader, int64_t *out)
{
    const uint8_t *raw = NULL;
    int64_t length;

    if (!xx_pyz_take(reader, 4, &raw)) {
        return false;
    }
    length = xx_pyz_le_i32(raw);
    /* The cap is what keeps a corrupt length from sizing a loop; marshal
     * itself refuses anything larger in a table of contents. */
    if ((length < 0) || (length > XX_PYZ_MAX_SEQUENCE)) {
        return false;
    }
    *out = length;
    return true;
}

/* marshal assigns a back-reference slot BEFORE the object is parsed, so the
 * slot has to be reserved in the same order here. */
static bool xx_pyz_ref_reserve(xx_pyz_marshal *reader, int64_t *index)
{
    xx_pyz_value *grown = (xx_pyz_value *)xx_mem_realloc(
        reader->refs, sizeof(xx_pyz_value) * (size_t)(reader->ref_count + 1));

    if (!grown) {
        return false;
    }
    reader->refs = grown;
    xx_mem_zero(&reader->refs[reader->ref_count], sizeof(xx_pyz_value));
    reader->refs[reader->ref_count].kind = XX_PYZ_KIND_OTHER;
    *index = reader->ref_count;
    reader->ref_count++;
    return true;
}

static bool xx_pyz_read_object(xx_pyz_marshal *reader, xx_pyz_value *value,
                               int depth)
{
    const uint8_t *raw = NULL;
    uint8_t code;
    char type;
    bool store;
    int64_t reference = -1;
    int64_t length = 0;
    int64_t index;
    int64_t digits;
    int64_t digit_count;
    uint64_t magnitude = 0;
    uint64_t term;
    uint16_t digit;

    if (depth > XX_PYZ_MAX_DEPTH) {
        return false;
    }
    reader->objects++;
    if (reader->objects > XX_PYZ_MAX_OBJECTS) {
        return false;
    }
    xx_mem_zero(value, sizeof(*value));
    value->kind = XX_PYZ_KIND_OTHER;
    if (!xx_pyz_take(reader, 1, &raw)) {
        return false;
    }
    code = raw[0];
    /* marshal sets the top bit on every object it may later back-reference. */
    store = (code & 0x80u) != 0;
    type = (char)(code & 0x7fu);
    if (store && !xx_pyz_ref_reserve(reader, &reference)) {
        return false;
    }

    if ((type == 'N') || (type == '0') || (type == 'F') || (type == 'T')) {
        /* None, the stop code, False and True carry no payload and are never a
         * name, an offset or a size, so they stay KIND_OTHER and every use
         * site rejects them. */
    } else if ((type == 'i') || (type == 'I')) {
        if (!xx_pyz_take(reader, (type == 'i') ? 4 : 8, &raw)) {
            return false;
        }
        value->kind = XX_PYZ_KIND_INT;
        value->number = (type == 'i') ? xx_pyz_le_i32(raw) : xx_pyz_le_i64(raw);
    } else if (type == 'l') {
        if (!xx_pyz_take(reader, 4, &raw)) {
            return false;
        }
        /* A marshal long is a sign-carrying count of base-2^15 digits. */
        digits = xx_pyz_le_i32(raw);
        digit_count = (digits < 0) ? -digits : digits;
        /* Five digits already cover every offset a 64-bit file can have; more
         * than that is a real big integer, which a TOC never holds. */
        if (digit_count > 5) {
            return false;
        }
        for (index = 0; index < digit_count; index++) {
            if (!xx_pyz_take(reader, 2, &raw)) {
                return false;
            }
            digit = xx_pyz_le16(raw);
            if (digit >= 0x8000u) {
                return false;
            }
            term = (uint64_t)digit << (15 * index);
            /* The fifth digit can shift its own bits off the end of a 64-bit
             * accumulator: that is a value this reader cannot represent, not
             * one it may silently truncate. */
            if ((term >> (15 * index)) != (uint64_t)digit) {
                return false;
            }
            if (magnitude > ((uint64_t)XX_PYZ_INT64_MAX - term)) {
                return false;
            }
            magnitude += term;
        }
        value->kind = XX_PYZ_KIND_INT;
        value->number =
            (digits < 0) ? -(int64_t)magnitude : (int64_t)magnitude;
    } else if ((type == 's') || (type == 't') || (type == 'u') ||
               (type == 'a') || (type == 'A')) {
        /* bytes, interned string, unicode, ascii, interned ascii: all of them
         * are a 4-byte length followed by that many bytes. */
        if (!xx_pyz_read_length(reader, &length) ||
            !xx_pyz_take(reader, length, &raw)) {
            return false;
        }
        value->kind = XX_PYZ_KIND_STR;
        value->offset = (int64_t)(raw - reader->data);
        value->size = length;
    } else if ((type == 'z') || (type == 'Z')) {
        /* The short-ascii forms store the length in a single byte. */
        if (!xx_pyz_take(reader, 1, &raw)) {
            return false;
        }
        length = (int64_t)raw[0];
        if (!xx_pyz_take(reader, length, &raw)) {
            return false;
        }
        value->kind = XX_PYZ_KIND_STR;
        value->offset = (int64_t)(raw - reader->data);
        value->size = length;
    } else if ((type == '(') || (type == '[')) {
        if (!xx_pyz_read_length(reader, &length)) {
            return false;
        }
        value->kind = XX_PYZ_KIND_SEQ;
        value->count = length;
    } else if (type == ')') {
        /* Small tuple: the element count is one byte, not four. */
        if (!xx_pyz_take(reader, 1, &raw)) {
            return false;
        }
        value->kind = XX_PYZ_KIND_SEQ;
        value->count = (int64_t)raw[0];
    } else if (type == 'r') {
        if (!xx_pyz_take(reader, 4, &raw)) {
            return false;
        }
        index = xx_pyz_le_i32(raw);
        if ((index < 0) || (index >= reader->ref_count)) {
            return false;
        }
        /* A back-reference to a container would have to replay its children,
         * and they are not in the stream a second time. Refuse it rather than
         * desynchronise the reader - a desynchronised reader still produces
         * member-shaped output. */
        if (reader->refs[index].kind == XX_PYZ_KIND_SEQ) {
            return false;
        }
        *value = reader->refs[index];
    } else {
        /* Every other marshal code - code objects, dicts, sets, floats,
         * complex - is one a PYZ table of contents never contains, and
         * accepting one would mean guessing at its length. */
        return false;
    }

    if (store) {
        reader->refs[reference] = *value;
    }
    return true;
}

/* A PYZ key is a dotted Python module name, so its alphabet is an
 * identifier's. This is the format's real signature: "PYZ\0" plus a 32-bit
 * offset is four bytes of magic and one number, both of which random data
 * reaches often enough, and everything after them is attacker-shaped. It is
 * also what makes the '.'-to-'/' rewrite below safe: no '/', no '\\' and no
 * ".." can survive it, so no member name can escape the output directory. Do
 * not widen this to "printable ASCII". */
static bool xx_pyz_name_valid(const uint8_t *data, int64_t size)
{
    int64_t index;
    uint8_t character;

    if ((size <= 0) || (size > XX_PYZ_MAX_NAME)) {
        return false;
    }
    /* A leading, trailing or doubled dot would produce an empty path
     * component, which is not a module name in the first place. */
    if ((data[0] == '.') || (data[size - 1] == '.')) {
        return false;
    }
    for (index = 0; index < size; index++) {
        character = data[index];
        if (!(((character >= 'a') && (character <= 'z')) ||
              ((character >= 'A') && (character <= 'Z')) ||
              ((character >= '0') && (character <= '9')) ||
              (character == '_') || (character == '.'))) {
            return false;
        }
        if ((character == '.') && (index > 0) && (data[index - 1] == '.')) {
            return false;
        }
    }
    return true;
}

static char *xx_pyz_build_name(const uint8_t *data, int64_t size,
                               bool is_package)
{
    /* Bounded by XX_PYZ_MAX_NAME plus the longest suffix. */
    char path[XX_PYZ_MAX_NAME + 32];
    const char *suffix =
        is_package ? "/__init__.pyc.marshal" : ".pyc.marshal";
    int64_t position = 0;
    int64_t index;

    /* The stream is a marshalled code object, not a .pyc file: it has no pyc
     * header, so the name says both what it is and where it came from. */
    for (index = 0; index < size; index++) {
        path[position++] = (data[index] == '.') ? '/' : (char)data[index];
    }
    for (index = 0; suffix[index] != 0; index++) {
        path[position++] = suffix[index];
    }
    path[position] = 0;
    return xx_str_dup(path);
}

/* ---------------------------------------------------------------- parse -- */

static xx_pyz_stream *xx_pyz_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_pyz_stream *stream = NULL;
    xx_pyz_marshal reader;
    xx_pyz_value root;
    xx_pyz_value item;
    xx_pyz_value name;
    xx_pyz_value descriptor;
    xx_pyz_value field[3];
    xx_pyz_member member;
    uint8_t header[XX_PYZ_HEADER_SIZE];
    uint8_t signature[2];
    uint8_t *toc = NULL;
    int64_t total;
    int64_t span;
    int64_t toc_offset;
    int64_t toc_size;
    int64_t count;
    int64_t index;
    int64_t field_index;
    int64_t other;
    int64_t other_begin;
    int64_t other_end;
    int64_t entry_type;
    int64_t data_offset;
    int64_t data_size;

    xx_mem_zero(&reader, sizeof(reader));
    total = xx_io_total_size(self->device);
    span = total - self->base_address;
    if (span < XX_PYZ_MIN_SIZE) {
        return NULL;
    }
    if (!xx_pyz_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* The trailing NUL is part of the magic: "PYZx" is not this format. */
    if (xx_rt_memcmp(header, "PYZ\0", 4) != 0) {
        return NULL;
    }
    /* header[4..7] is the writing interpreter's bytecode magic. It changes
     * with every Python release, so there is no value to check it against. */
    toc_offset = (int64_t)xx_pyz_be32(header + 8);
    /* The table of contents lives after the header and ends at EOF; a TOC
     * that starts inside the header, or at or past EOF, is not a PYZ. */
    if ((toc_offset < XX_PYZ_HEADER_SIZE) || (toc_offset >= span)) {
        return NULL;
    }
    toc_size = span - toc_offset;
    if (toc_size > XX_PYZ_MAX_TOC) {
        return NULL;
    }

    toc = (uint8_t *)xx_mem_alloc((size_t)toc_size);
    if (!toc) {
        return NULL;
    }
    if (!xx_pyz_read_at(self, self->base_address + toc_offset, toc,
                        (size_t)toc_size)) {
        xx_mem_free(toc);
        return NULL;
    }

    stream = (xx_pyz_stream *)xx_mem_alloc(sizeof(xx_pyz_stream));
    if (!stream) {
        xx_mem_free(toc);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(xx_pyz_stream));

    reader.data = toc;
    reader.size = toc_size;

    if (!xx_pyz_read_object(&reader, &root, 0)) {
        goto fail;
    }
    if (root.kind != XX_PYZ_KIND_SEQ) {
        goto fail;
    }
    count = root.count;
    /* An empty table of contents is not a degenerate-but-valid archive:
     * PyInstaller never writes one, and accepting it would make twelve bytes
     * plus an empty list a match. */
    if ((count <= 0) || (count > XX_PYZ_MAX_MEMBERS)) {
        goto fail;
    }

    for (index = 0; index < count; index++) {
        if (pd && xx_pd_is_stopped(pd)) {
            goto fail;
        }
        /* Shape first: (name, (type, offset, size)) and nothing else. Each of
         * these counts is exact - a tuple of a different width means the blob
         * is not a PYZ table of contents, whatever else it parses as. */
        if (!xx_pyz_read_object(&reader, &item, 1)) {
            goto fail;
        }
        if ((item.kind != XX_PYZ_KIND_SEQ) || (item.count != 2)) {
            goto fail;
        }
        if (!xx_pyz_read_object(&reader, &name, 2)) {
            goto fail;
        }
        if (name.kind != XX_PYZ_KIND_STR) {
            goto fail;
        }
        if (!xx_pyz_read_object(&reader, &descriptor, 2)) {
            goto fail;
        }
        if ((descriptor.kind != XX_PYZ_KIND_SEQ) || (descriptor.count != 3)) {
            goto fail;
        }
        for (field_index = 0; field_index < 3; field_index++) {
            if (!xx_pyz_read_object(&reader, &field[field_index], 3)) {
                goto fail;
            }
            if (field[field_index].kind != XX_PYZ_KIND_INT) {
                goto fail;
            }
        }
        entry_type = field[0].number;
        data_offset = field[1].number;
        data_size = field[2].number;

        /* Member data lives strictly between the header and the table of
         * contents. Using toc_offset rather than the span as the limit is the
         * containment rule that ties the TOC offset, the member offsets and
         * the file length together: a member reaching into the TOC, or past
         * EOF, is a rejection. */
        if ((data_offset < XX_PYZ_HEADER_SIZE) || (data_size < 0) ||
            !xx_pyz_range_within(toc_offset, data_offset, data_size)) {
            goto fail;
        }

        if (entry_type == XX_PYZ_ENTRY_NAMESPACE) {
            /* A namespace package is a marker with no byte stream at all, so
             * it publishes no member; a nonzero size on one is a
             * contradiction, not a member to fall back on. */
            if (data_size != 0) {
                goto fail;
            }
            continue;
        }
        if ((entry_type != XX_PYZ_ENTRY_MODULE) &&
            (entry_type != XX_PYZ_ENTRY_PACKAGE)) {
            goto fail;
        }
        if (data_size < XX_PYZ_MIN_STREAM) {
            goto fail;
        }

        /* Quadratic, which is what XX_PYZ_MAX_MEMBERS is for. Two members
         * sharing bytes means the offsets are not a real directory: a writer
         * packs the streams end to end. */
        for (other = 0; other < (int64_t)stream->count; other++) {
            other_begin = stream->items[other].data_offset - self->base_address;
            other_end = other_begin + stream->items[other].compressed_size;
            if ((data_offset < other_end) &&
                (other_begin < (data_offset + data_size))) {
                goto fail;
            }
        }

        if (!xx_pyz_name_valid(toc + name.offset, name.size)) {
            goto fail;
        }
        /* Every member is a zlib stream. The two-byte header's
         * multiple-of-31 rule costs one read per member here and is what makes
         * a pair of plausible-looking numbers implausible; the decode proves
         * the stream is complete, but only when something asks for the bytes,
         * and validity must not depend on that. */
        if (!xx_pyz_read_at(self, self->base_address + data_offset, signature,
                            sizeof(signature))) {
            goto fail;
        }
        if (!xx_zlib_stream_header_is_valid(signature, sizeof(signature))) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_pyz_build_name(toc + name.offset, name.size,
                                        entry_type == XX_PYZ_ENTRY_PACKAGE);
        if (!member.name) {
            goto fail;
        }
        /* There is no per-member header: the TOC entry is a marshalled object
         * of no fixed width, so every member points at the file header, which
         * is what the reference reader reports too. */
        member.header_offset = self->base_address;
        member.header_size = XX_PYZ_HEADER_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        /* Not stated by the container, and deliberately not measured here:
         * the parse must stay cheap and must not depend on a decode. -1 is
         * the library's 'not yet known'; xx_pyz_measure() fills it in when a
         * record is built, which is the first point a caller can see it. */
        member.uncompressed_size = -1;
        /* The raw entry type, so a listing shows what the archive says. */
        member.method = (uint32_t)entry_type;
        member.timestamp = 0;
        member.is_folder = false;
        if (!xx_pyz_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    /* The marshalled blob must end exactly at EOF. Together with the entry
     * shape above this is the strongest check in the format: a random tail
     * that happens to start with a list code has to consume itself to the
     * last byte and no further. Do not relax this to "<=". */
    if (reader.position != reader.size) {
        goto fail;
    }
    /* A table of contents of nothing but namespace markers has no bytes in
     * it, so there is no archive to report. */
    if (stream->count == 0) {
        goto fail;
    }
    stream->archive_size = span;
    xx_mem_free(reader.refs);
    xx_mem_free(toc);
    return stream;

fail:
    xx_mem_free(reader.refs);
    xx_mem_free(toc);
    xx_pyz_stream_free(stream);
    return NULL;
}

/* ----------------------------------------------------------- constants -- */

/* The table of contents is read whole, so its span is what bounds the read. */
/* The overlap check below is quadratic; this cap is what keeps it bounded. */
/* 2 header bytes + at least one Deflate byte + the 4-byte Adler-32. */

/* Table-of-contents entry types, as the container numbers them. */

/* -------------------------------------------------------------- decode -- */

static bool xx_pyz_decode_core(Abstractformat *self,
                               const xx_pyz_member *member, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd,
                               bool require_trailer)
{
    uint8_t *compressed = NULL;
    uint8_t *plain = NULL;
    size_t compressed_size;
    size_t capacity;
    size_t written = 0;
    bool decoded = false;
    bool failed = false;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) {
        return false;
    }
    /* method carries the entry TYPE, because the container has no method
     * field: every member is zlib. Refusing any value the parse does not
     * itself publish keeps a future entry type from being inflated as if it
     * were a module - silently guessing is how garbage gets written out as
     * data. */
    if ((member->method != (uint32_t)XX_PYZ_ENTRY_MODULE) &&
        (member->method != (uint32_t)XX_PYZ_ENTRY_PACKAGE)) {
        return false;
    }
    if ((member->compressed_size < XX_PYZ_MIN_STREAM) ||
        (member->compressed_size > (int64_t)XX_PYZ_MAX_DECODED)) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        return false;
    }

    compressed_size = (size_t)member->compressed_size;
    compressed = (uint8_t *)xx_mem_alloc(compressed_size);
    if (!compressed) {
        return false;
    }
    if (!xx_pyz_read_at(self, member->data_offset, compressed,
                        compressed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(compressed);
        return false;
    }

    /* PYZ stores no uncompressed size, so the output capacity has to be
     * guessed and grown. Deflate on marshalled bytecode rarely beats 8:1, so
     * the first guess is almost always the only one; the cap is what keeps a
     * hostile member from turning a few bytes into an unbounded allocation. */
    capacity = (compressed_size < (size_t)(XX_PYZ_MAX_DECODED / 8))
                   ? (compressed_size * 8)
                   : (size_t)XX_PYZ_MAX_DECODED;
    if (capacity < 4096) {
        capacity = 4096;
    }

    while (!decoded && !failed) {
        plain = (uint8_t *)xx_mem_alloc(capacity);
        if (!plain) {
            failed = true;
            break;
        }
        if (xx_zlib_stream_decode_memory(compressed, compressed_size, plain,
                                         capacity, &written) &&
            (written <= capacity)) {
            /* The Adler-32 in the trailer covers the WHOLE plaintext, and the
             * table of contents cuts the stream at its last byte, so a match
             * proves the decode ran to the end. With no stored uncompressed
             * size to compare against, this is the only thing standing
             * between a truncated member and a caller that believes it. */
            if (!require_trailer ||
                xx_zlib_stream_trailer_matches(compressed, compressed_size,
                                               plain, written)) {
                decoded = true;
                break;
            }
            /* Header and Deflate were both fine and the checksum still
             * disagrees: a bigger buffer cannot change that. */
            xx_mem_free(plain);
            plain = NULL;
            failed = true;
            break;
        }
        xx_mem_free(plain);
        plain = NULL;
        if (capacity >= (size_t)XX_PYZ_MAX_DECODED) {
            failed = true;
            break;
        }
        capacity = (capacity > (size_t)(XX_PYZ_MAX_DECODED / 2))
                       ? (size_t)XX_PYZ_MAX_DECODED
                       : (capacity * 2);
        if (pd && xx_pd_is_stopped(pd)) {
            failed = true;
        }
    }

    xx_mem_free(compressed);
    if (!decoded) {
        xx_mem_free(plain);
        return false;
    }
    /* The block is larger than the plaintext when the guess overshot; the
     * caller only ever reads *out_size bytes of it. */
    *out = plain;
    *out_size = written;
    return true;
}

static bool xx_pyz_decode(Abstractformat *self, const xx_pyz_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    /* Handing bytes to a caller requires the Adler-32 to agree: it is the
     * only proof the stream decoded whole. */
    return xx_pyz_decode_core(self, member, out, out_size, pd, true);
}

/* The container records no plaintext length, so the only way to state one is
 * to run the decode. That is done here, once per member, and cached: the
 * result is metadata, not content, so a member whose Adler-32 disagrees still
 * gets a length reported while xx_pyz_decode() keeps refusing its bytes.
 * Reporting "unknown" instead would be worse than wrong - a caller that skips
 * unknown-size members never asks for the bytes, so the checksum never gets
 * to refuse anything. */
static void xx_pyz_measure(Abstractformat *self, xx_pyz_member *member) {
    uint8_t *plain = NULL;
    size_t plain_size = 0U;

    if (!member || member->uncompressed_size >= 0 || member->is_folder) return;
    if (xx_pyz_decode_core(self, member, &plain, &plain_size, NULL, false)) {
        member->uncompressed_size = (int64_t)plain_size;
    } else {
        /* Not even raw Deflate got through, so there is no length to state.
         * Zero rather than unstated, for the same reason: a caller that skips
         * unknown-size members never asks for the bytes, and the refusal in
         * xx_pyz_decode() never gets to happen. */
        member->uncompressed_size = 0;
    }
    xx_mem_free(plain);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_pyz_init(xx_pyz *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PYZ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pyz");
    xx_format_set_extension(&archive->format, "pyz");
    archive->format.check_is_valid = xx_pyz_check_is_valid;
    archive->format.handle_base_info = xx_pyz_handle_base_info;
    archive->format.get_format_size = xx_pyz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pyz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pyz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pyz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pyz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pyz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pyz_free_archive_records_reading;
    archive->format.destroy = xx_pyz_vtable_destroy;
}

xx_pyz *xx_pyz_create(xx_io_device *device, int64_t base_address) {
    xx_pyz *archive = (xx_pyz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_pyz_init(archive, device, base_address);
    return archive;
}

void xx_pyz_destroy(xx_pyz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_pyz_free(xx_pyz *archive) {
    if (!archive) return;
    xx_pyz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_pyz_vtable_destroy(Abstractformat *self) {
    xx_pyz_destroy((xx_pyz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_pyz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pyz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_pyz_parse(self, pd);
    if (!stream) return false;
    xx_pyz_stream_free(stream);
    return true;
}

bool xx_pyz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pyz *archive = (xx_pyz *)self;
    xx_pyz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_pyz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_pyz_stream_free(stream);
    return true;
}

int64_t xx_pyz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pyz_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_pyz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_pyz_set_record(Abstractformat *self, xx_archive_record *record,
                                 xx_pyz_member *member) {
    xx_pyz_measure(self, member);
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

static bool xx_pyz_copy_options(xx_list_s *target,
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

static const xx_var *xx_pyz_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_pyz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_pyz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_pyz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_pyz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_pyz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_pyz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_pyz_set_record(self, &state->current_record,
                            &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_pyz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pyz_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_pyz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pyz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_pyz_set_record(self, &state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pyz_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_pyz_stream *stream;
    const xx_pyz_member *member;
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
    stream = (xx_pyz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_pyz_path_safe(member->name)) return false;

    path_option = xx_pyz_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_pyz_decode(self, member, &plain, &plain_size, pd);
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
        !xx_pyz_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_pyz_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
