/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IzPack (com.izforge.izpack) installer pack file -- the "packN" members that
 * live inside an IzPack installer JAR.  Layout ported from the XArchive
 * reference module installers/xizpack.cpp.
 *
 * The container is a Java object-serialization stream written by
 * ObjectOutputStream, but it is NOT general Java serialization: IzPack writes
 * one fixed script, so only one narrow path has to be understood.
 *
 *   AC ED 00 05                 stream magic, version 5
 *   77 04 <int32be count>       TC_BLOCKDATA: number of PackFile records
 *   then, `count` times:
 *     TC_OBJECT ('s') plus either a full TC_CLASSDESC ('r') for the first
 *     record or TC_REFERENCE ('q' 00 7E 00 00) for every later one, followed
 *     by the PackFile field values in declaration order, then the member's
 *     bytes as a run of TC_BLOCKDATA ('w' + u8 len) / TC_BLOCKDATALONG
 *     ('z' + u32be len) chunks -- the ObjectOutputStream's own framing.
 *
 * The class descriptor is the version oracle: the serialVersionUID together
 * with the declared field count selects one of seven PackFile layouts
 * (v1..v7).  Nothing else in the stream states a version, and the class name
 * "com.izforge.izpack.PackFile" sitting at a fixed offset is what keeps this
 * reader out of every other Java serialization stream in the world.
 *
 * Records whose offsetInPreviousPack is not -1 hold no bytes in this pack and
 * are omitted, as are pack200-compressed JAR members (their payload is a
 * Pack200 archive, not the file).  A trailing section after the last record
 * (the installer's parsable/executable lists) is reported as an overlay.
 *
 * Member bytes are stored, not compressed: a pack that fits in one frame is
 * a plain contiguous run, and anything longer only has to have the chunk
 * tags lifted back out.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/izpack/xx_izpack.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef IZPACK
#define XX_IZPACK_FILE_TYPE XX_FILE_TYPE_IZPACK
#else
#define XX_IZPACK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Java object-serialization tags.  Only the handful IzPack actually emits are
 * implemented; anything else makes the walk fail closed. */
#define IZ_TC_NULL UINT8_C(0x70)
#define IZ_TC_REFERENCE UINT8_C(0x71)
#define IZ_TC_CLASSDESC UINT8_C(0x72)
#define IZ_TC_OBJECT UINT8_C(0x73)
#define IZ_TC_STRING UINT8_C(0x74)
#define IZ_TC_BLOCKDATA UINT8_C(0x77)
#define IZ_TC_ENDBLOCKDATA UINT8_C(0x78)
#define IZ_TC_BLOCKDATALONG UINT8_C(0x7a)
#define IZ_TC_LONGSTRING UINT8_C(0x7c)
#define IZ_BASE_WIRE_HANDLE INT32_C(0x7e0000)

/* "AC ED 00 05" + "77 04" + int32 record count: the fixed part of the header
 * is ten bytes, and the class descriptor that follows is inspected in place. */
#define IZ_HEADER_SIZE 10
#define IZ_PROBE_SIZE 0x34
#define IZ_CLASS_NAME_SIZE 27
#define IZ_MAX_RECORDS 1000000
#define IZ_MAX_HANDLES (1 << 20)
#define IZ_MAX_DEPTH 64
#define IZ_MAX_FIELDS 4096
#define IZ_MAX_MEMBER_SIZE INT64_C(0x40000000) /* 1 GiB */
#define IZ_MAX_NAME 4096
#define IZ_BUFFER_SIZE 0x10000

/* serialVersionUID (as stored) plus the declared field count is the only
 * version marker in the stream.  The pairing matters: v5, v6 and v7 share one
 * UID and differ solely in how many fields the descriptor declares. */
typedef struct iz_version_s {
    uint64_t uid;
    uint16_t field_count;
    int32_t version;
} iz_version;

static const iz_version g_iz_versions[] = {
    {UINT64_C(0x76a523b293a5e5c3), 2U, 1},
    {UINT64_C(0x11e206a67610577a), 5U, 2},
    {UINT64_C(0x9856d67cc286140e), 7U, 3},
    {UINT64_C(0x99e53bc9ae638cb7), 8U, 4},
    {UINT64_C(0xf46bb277b6f32403), 10U, 5},
    {UINT64_C(0xf46bb277b6f32403), 11U, 6},
    {UINT64_C(0xf46bb277b6f32403), 12U, 7},
};

typedef struct iz_record_s {
    char *name;
    int64_t record_offset;
    int64_t record_size;
    int64_t stream_offset;
    int64_t stream_size;
    int64_t unpacked_size;
    int64_t mtime; /**< Java milliseconds since the epoch, 0 if absent. */
    bool framed;   /**< The bytes still carry block-data chunk tags. */
    bool folder;
} iz_record;

typedef struct iz_stream_s {
    iz_record *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    int32_t version;
    int32_t declared_count;
} iz_stream;

/* ---------------------------------------------------------- cursor ------ */

/* A forward-only cursor with its own window over the device.  The record walk
 * touches the stream a byte or a tag at a time; going through the device for
 * each of those would make a five-megabyte pack unusable. */
typedef struct iz_cursor_s {
    xx_io_device *device;
    int64_t base;
    int64_t size;
    int64_t position;
    int64_t window_offset;
    int64_t window_size;
    uint8_t *window;
    bool failed;
} iz_cursor;

static bool iz_read_at(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool iz_cursor_fill(iz_cursor *cursor, int64_t offset) {
    int64_t portion;
    if (cursor->window_offset >= 0 && offset >= cursor->window_offset &&
        offset < cursor->window_offset + cursor->window_size)
        return true;
    portion = cursor->size - offset;
    if (portion <= 0) return false;
    if (portion > IZ_BUFFER_SIZE) portion = IZ_BUFFER_SIZE;
    if (!iz_read_at(cursor->device, cursor->base + offset, cursor->window,
                    (size_t)portion)) {
        cursor->window_offset = -1;
        cursor->window_size = 0;
        return false;
    }
    cursor->window_offset = offset;
    cursor->window_size = portion;
    return true;
}

static bool iz_read(iz_cursor *cursor, uint8_t *out, int64_t size) {
    int64_t left = size;
    if (cursor->failed || size < 0 || size > cursor->size - cursor->position) {
        cursor->failed = true;
        return false;
    }
    while (left > 0) {
        int64_t available;
        int64_t portion;
        if (!iz_cursor_fill(cursor, cursor->position)) {
            cursor->failed = true;
            return false;
        }
        available = cursor->window_size - (cursor->position -
                                           cursor->window_offset);
        if (available <= 0) {
            cursor->failed = true;
            return false;
        }
        portion = available < left ? available : left;
        if (out) {
            xx_rt_memcpy(out,
                         cursor->window +
                             (cursor->position - cursor->window_offset),
                         (size_t)portion);
            out += portion;
        }
        cursor->position += portion;
        left -= portion;
    }
    return true;
}

static bool iz_skip(iz_cursor *cursor, int64_t size) {
    if (cursor->failed || size < 0 || size > cursor->size - cursor->position) {
        cursor->failed = true;
        return false;
    }
    cursor->position += size;
    return true;
}

static uint8_t iz_u8(iz_cursor *cursor) {
    uint8_t value = 0U;
    if (!iz_read(cursor, &value, 1)) return 0U;
    return value;
}

static uint16_t iz_u16(iz_cursor *cursor) {
    uint8_t buffer[2] = {0U, 0U};
    if (!iz_read(cursor, buffer, 2)) return 0U;
    return (uint16_t)(((uint16_t)buffer[0] << 8U) | buffer[1]);
}

static uint32_t iz_u32(iz_cursor *cursor) {
    uint8_t buffer[4] = {0U, 0U, 0U, 0U};
    if (!iz_read(cursor, buffer, 4)) return 0U;
    return ((uint32_t)buffer[0] << 24U) | ((uint32_t)buffer[1] << 16U) |
           ((uint32_t)buffer[2] << 8U) | (uint32_t)buffer[3];
}

static int32_t iz_i32(iz_cursor *cursor) {
    return (int32_t)iz_u32(cursor);
}

static int64_t iz_i64(iz_cursor *cursor) {
    uint8_t buffer[8];
    uint64_t value = 0U;
    int index;
    xx_mem_zero(buffer, sizeof(buffer));
    if (!iz_read(cursor, buffer, 8)) return 0;
    for (index = 0; index < 8; ++index)
        value = (value << 8U) | (uint64_t)buffer[index];
    return (int64_t)value;
}

/* ---------------------------------------------------------- parser ------ */

/* One entry of the serialization handle table.  Only class descriptors carry
 * anything worth remembering: their field type codes, needed to skip a
 * back-referenced object's values. */
typedef struct iz_handle_s {
    char *types;
    size_t type_count;
    bool has_types;
} iz_handle;

typedef struct iz_parser_s {
    iz_cursor *cursor;
    iz_handle *handles;
    size_t handle_count;
    size_t handle_capacity;
    int32_t depth;
} iz_parser;

static void iz_parser_cleanup(iz_parser *parser) {
    size_t index;
    if (!parser || !parser->handles) return;
    for (index = 0U; index < parser->handle_count; ++index)
        if (parser->handles[index].types) xx_mem_free(parser->handles[index].types);
    xx_mem_free(parser->handles);
    parser->handles = NULL;
    parser->handle_count = 0U;
    parser->handle_capacity = 0U;
}

static bool iz_append_handle(iz_parser *parser, bool has_types) {
    if (parser->handle_count >= (size_t)IZ_MAX_HANDLES) return false;
    if (parser->handle_count == parser->handle_capacity) {
        size_t grown = parser->handle_capacity ? parser->handle_capacity * 2U
                                               : 64U;
        iz_handle *bigger;
        if (grown > SIZE_MAX / sizeof(*bigger)) return false;
        bigger = (iz_handle *)xx_mem_realloc(parser->handles,
                                             grown * sizeof(*bigger));
        if (!bigger) return false;
        parser->handles = bigger;
        parser->handle_capacity = grown;
    }
    parser->handles[parser->handle_count].types = NULL;
    parser->handles[parser->handle_count].type_count = 0U;
    parser->handles[parser->handle_count].has_types = has_types;
    ++parser->handle_count;
    return true;
}

static bool iz_skip_utf(iz_parser *parser) {
    uint16_t length = iz_u16(parser->cursor);
    if (parser->cursor->failed) return false;
    return length == 0U ? true : iz_skip(parser->cursor, length);
}

static bool iz_read_content(iz_parser *parser);

/* TC_CLASSDESC body: class name, serialVersionUID, flags, field table,
 * TC_ENDBLOCKDATA, superclass (always TC_NULL in this stream). */
static bool iz_class_desc(iz_parser *parser) {
    uint16_t field_count;
    size_t handle_index;
    char *types;
    uint16_t index;
    if (!iz_skip_utf(parser)) return false;
    if (!iz_skip(parser->cursor, 9)) return false; /* uid(8) + flags(1) */
    field_count = iz_u16(parser->cursor);
    if (parser->cursor->failed || field_count > IZ_MAX_FIELDS) return false;
    /* The handle is claimed before the field table is read, because a field
     * type string inside it may already reference this descriptor. */
    if (!iz_append_handle(parser, true)) return false;
    handle_index = parser->handle_count - 1U;
    types = (char *)xx_mem_alloc((size_t)field_count + 1U);
    if (!types) return false;
    for (index = 0U; index < field_count; ++index) {
        uint8_t code = iz_u8(parser->cursor);
        if (parser->cursor->failed || !iz_skip_utf(parser)) {
            xx_mem_free(types);
            return false;
        }
        if (code == 'L' || code == '[') {
            if (!iz_read_content(parser)) {
                xx_mem_free(types);
                return false;
            }
        } else if (code != 'B' && code != 'C' && code != 'D' && code != 'F' &&
                   code != 'I' && code != 'J' && code != 'S' && code != 'Z') {
            xx_mem_free(types);
            return false;
        }
        types[index] = (char)code;
    }
    types[field_count] = 0;
    if (parser->handles[handle_index].types)
        xx_mem_free(parser->handles[handle_index].types);
    parser->handles[handle_index].types = types;
    parser->handles[handle_index].type_count = field_count;
    if (iz_u8(parser->cursor) != IZ_TC_ENDBLOCKDATA) return false;
    if (iz_u8(parser->cursor) != IZ_TC_NULL) return false;
    return !parser->cursor->failed;
}

/* Skip the field values of an object whose descriptor is already known. */
static bool iz_skip_fields(iz_parser *parser, const char *types,
                           size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        char code = types[index];
        if (code == 'B' || code == 'C' || code == 'Z') {
            if (!iz_skip(parser->cursor, 1)) return false;
        } else if (code == 'S') {
            if (!iz_skip(parser->cursor, 2)) return false;
        } else if (code == 'F' || code == 'I') {
            if (!iz_skip(parser->cursor, 4)) return false;
        } else if (code == 'J' || code == 'D') {
            if (!iz_skip(parser->cursor, 8)) return false;
        } else if (code == 'L' || code == '[') {
            if (!iz_read_content(parser)) return false;
        } else {
            return false;
        }
    }
    return true;
}

/* Skip one serialized value of any shape IzPack can put in a PackFile field.
 * Nothing is materialised: the walk only has to land on the byte after it. */
static bool iz_read_content_body(iz_parser *parser) {
    uint8_t tag = iz_u8(parser->cursor);
    if (parser->cursor->failed) return false;
    if (tag == IZ_TC_NULL) return true;
    if (tag == IZ_TC_STRING)
        return iz_skip_utf(parser) && iz_append_handle(parser, false);
    if (tag == IZ_TC_LONGSTRING) {
        uint32_t length = iz_u32(parser->cursor);
        if (parser->cursor->failed) return false;
        if (length && !iz_skip(parser->cursor, (int64_t)length)) return false;
        return iz_append_handle(parser, false);
    }
    if (tag == IZ_TC_REFERENCE) {
        int32_t handle = iz_i32(parser->cursor) - IZ_BASE_WIRE_HANDLE;
        return !parser->cursor->failed && handle >= 0 &&
               (size_t)handle < parser->handle_count;
    }
    if (tag == IZ_TC_CLASSDESC) return iz_class_desc(parser);
    if (tag == IZ_TC_BLOCKDATA) {
        uint8_t length = iz_u8(parser->cursor);
        if (parser->cursor->failed) return false;
        return length == 0U ? true : iz_skip(parser->cursor, length);
    }
    if (tag == IZ_TC_ENDBLOCKDATA) return true;
    if (tag == IZ_TC_OBJECT) {
        uint8_t inner = iz_u8(parser->cursor);
        int64_t descriptor;
        if (parser->cursor->failed) return false;
        if (inner == IZ_TC_CLASSDESC) {
            if (!iz_class_desc(parser)) return false;
            descriptor = (int64_t)parser->handle_count - 1;
        } else if (inner == IZ_TC_REFERENCE) {
            descriptor = (int64_t)iz_i32(parser->cursor) - IZ_BASE_WIRE_HANDLE;
            if (parser->cursor->failed) return false;
        } else {
            return false;
        }
        if (!iz_append_handle(parser, false)) return false;
        if (descriptor < 0 || (size_t)descriptor >= parser->handle_count)
            return false;
        if (!parser->handles[descriptor].has_types ||
            !parser->handles[descriptor].types)
            return false;
        return iz_skip_fields(parser, parser->handles[descriptor].types,
                              parser->handles[descriptor].type_count);
    }
    return false;
}

static bool iz_read_content(iz_parser *parser) {
    bool result;
    if (parser->depth >= IZ_MAX_DEPTH) return false;
    ++parser->depth;
    result = iz_read_content_body(parser);
    --parser->depth;
    return result;
}

/* TC_STRING / TC_LONGSTRING / TC_NULL, materialised.  Used for sourcePath and
 * targetPath. */
static bool iz_read_string(iz_parser *parser, char **result) {
    uint8_t tag;
    int64_t length;
    char *value;
    if (result) *result = NULL;
    tag = iz_u8(parser->cursor);
    if (parser->cursor->failed) return false;
    if (tag == IZ_TC_STRING) {
        length = (int64_t)iz_u16(parser->cursor);
    } else if (tag == IZ_TC_LONGSTRING) {
        /* The reference reads a 32-bit length here; a real TC_LONGSTRING is
         * 64-bit, but IzPack never emits one and matching the reference keeps
         * the byte positions identical. */
        length = (int64_t)iz_u32(parser->cursor);
    } else if (tag == IZ_TC_NULL) {
        return true; /* No handle is allocated for a null. */
    } else {
        return false;
    }
    if (parser->cursor->failed || length < 0 || length > IZ_MAX_NAME)
        return false;
    value = (char *)xx_mem_alloc((size_t)length + 1U);
    if (!value) return false;
    if (length && !iz_read(parser->cursor, (uint8_t *)value, length)) {
        xx_mem_free(value);
        return false;
    }
    value[length] = 0;
    if (result)
        *result = value;
    else
        xx_mem_free(value);
    return iz_append_handle(parser, false);
}

/* The osConstraints java.util.ArrayList: descriptor, size, the writeObject
 * capacity block, the elements, TC_ENDBLOCKDATA. */
static bool iz_read_array_list(iz_parser *parser) {
    uint8_t tag = iz_u8(parser->cursor);
    int32_t size;
    int32_t index;
    if (parser->cursor->failed) return false;
    if (tag == IZ_TC_REFERENCE) {
        parser->cursor->position -= 1;
        return iz_read_content(parser);
    }
    if (tag != IZ_TC_OBJECT) return false;
    if (!iz_read_content(parser)) return false; /* the class descriptor */
    if (!iz_append_handle(parser, false)) return false;
    size = iz_i32(parser->cursor);
    if (parser->cursor->failed || size < 0 || size > IZ_MAX_FIELDS)
        return false;
    if (!iz_read_content(parser)) return false; /* the capacity block */
    for (index = 0; index < size; ++index)
        if (!iz_read_content(parser)) return false;
    return iz_u8(parser->cursor) == IZ_TC_ENDBLOCKDATA &&
           !parser->cursor->failed;
}

/* Walk the block-data framing that carries a member's bytes.
 *
 * The last chunk may run past the member: ObjectOutputStream had already
 * buffered the next few primitive writes when it flushed.  The reference
 * accepts an overrun of exactly 4, 8 or 12 bytes and skips it, and the record
 * layout only stays aligned if we do the same. */
static bool iz_scan_block_data(iz_parser *parser, int64_t size,
                               int64_t *first_data, int32_t *chunks,
                               int64_t *excess) {
    int64_t left = size;
    bool first = true;
    *first_data = parser->cursor->position;
    *chunks = 0;
    *excess = 0;
    while (left > 0) {
        uint8_t tag = iz_u8(parser->cursor);
        int64_t chunk;
        if (parser->cursor->failed) return false;
        if (tag == IZ_TC_BLOCKDATALONG) {
            chunk = (int64_t)iz_u32(parser->cursor);
        } else if (tag == IZ_TC_BLOCKDATA) {
            chunk = (int64_t)iz_u8(parser->cursor);
        } else {
            return false;
        }
        if (parser->cursor->failed || chunk <= 0) return false;
        if (left < chunk) {
            int64_t over = chunk - left;
            if (over != 4 && over != 8 && over != 12) return false;
            *excess = over;
            chunk = left;
        }
        if (first) {
            *first_data = parser->cursor->position;
            first = false;
        }
        if (!iz_skip(parser->cursor, chunk)) return false;
        left -= chunk;
        ++(*chunks);
    }
    if (*excess > 0 && !iz_skip(parser->cursor, *excess)) return false;
    return true;
}

/* --------------------------------------------------------- container ---- */

static int32_t iz_version_from_class_desc(uint64_t uid, uint16_t fields) {
    size_t index;
    for (index = 0U;
         index < sizeof(g_iz_versions) / sizeof(g_iz_versions[0]); ++index)
        if (g_iz_versions[index].uid == uid &&
            g_iz_versions[index].field_count == fields)
            return g_iz_versions[index].version;
    return 0;
}

/* Target paths are IzPack variable expressions such as
 * "$INSTALL_PATH/bin/x.jar".  Separators are normalized and traversal
 * components are dropped so the result can never escape the extraction root. */
static char *iz_normalize_name(const char *raw, size_t index) {
    size_t length = raw ? xx_rt_strlen(raw) : 0U;
    char *name;
    size_t input = 0U;
    size_t output = 0U;
    if (length > IZ_MAX_NAME) return NULL;
    name = (char *)xx_mem_alloc(length + 24U);
    if (!name) return NULL;
    while (input < length) {
        size_t start;
        size_t end;
        size_t component;
        while (input < length && (raw[input] == '/' || raw[input] == '\\'))
            ++input;
        start = input;
        while (input < length && raw[input] != '/' && raw[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && raw[start] == '.')) continue;
        if (end - start == 2U && raw[start] == '.' && raw[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component = output;
        while (start < end) {
            unsigned char c = (unsigned char)raw[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|')
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component) name[output++] = '_';
    }
    if (output == 0U) {
        const char *stem = "izpack_";
        size_t stem_length = xx_rt_strlen(stem);
        xx_rt_memcpy(name, stem, stem_length);
        output = stem_length;
        if (index >= 100U) name[output++] = (char)('0' + (index / 100U) % 10U);
        if (index >= 10U) name[output++] = (char)('0' + (index / 10U) % 10U);
        name[output++] = (char)('0' + index % 10U);
    }
    name[output] = 0;
    return name;
}

static bool iz_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void iz_stream_free(void *opaque) {
    iz_stream *stream = (iz_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool iz_add_record(iz_stream *stream, const iz_record *record) {
    iz_record *grown;
    if (!stream || !record || stream->count >= (size_t)IZ_MAX_RECORDS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (iz_record *)xx_mem_realloc(stream->items,
                                        (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *record;
    return true;
}

static bool iz_parse(Abstractformat *format, iz_stream **result) {
    uint8_t probe[IZ_PROBE_SIZE];
    iz_stream *stream = NULL;
    iz_cursor cursor;
    iz_parser parser;
    int64_t total;
    int64_t available;
    int64_t base;
    uint64_t uid;
    uint16_t fields;
    uint32_t mask;
    int32_t version;
    int32_t index;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    available = total - base;
    if (available < IZ_PROBE_SIZE) return false;
    if (!iz_read_at(format->device, base, probe, sizeof(probe))) return false;

    /* Java stream magic and version, then the record-count block-data. */
    if (probe[0] != 0xacU || probe[1] != 0xedU || probe[2] != 0x00U ||
        probe[3] != 0x05U)
        return false;
    if (probe[4] != IZ_TC_BLOCKDATA || probe[5] != 0x04U) return false;
    /* The first record's descriptor is inlined right after the count, and its
     * class name is the discriminator that keeps this reader out of every
     * other Java serialization stream in the world. */
    if (probe[10] != IZ_TC_OBJECT || probe[11] != IZ_TC_CLASSDESC) return false;
    if (((uint16_t)probe[12] << 8U | probe[13]) != IZ_CLASS_NAME_SIZE)
        return false;
    {
        static const char expected[] = "com.izforge.izpack.PackFile";
        if (xx_rt_memcmp(probe + 14, expected, IZ_CLASS_NAME_SIZE) != 0)
            return false;
    }

    uid = 0U;
    for (index = 0; index < 8; ++index)
        uid = (uid << 8U) | (uint64_t)probe[0x29 + index];
    fields = (uint16_t)(((uint16_t)probe[0x32] << 8U) | probe[0x33]);
    version = iz_version_from_class_desc(uid, fields);
    if (version == 0) return false;

    stream = (iz_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->version = version;
    stream->declared_count =
        (int32_t)(((uint32_t)probe[6] << 24U) | ((uint32_t)probe[7] << 16U) |
                  ((uint32_t)probe[8] << 8U) | (uint32_t)probe[9]);
    if (stream->declared_count < 0 || stream->declared_count > IZ_MAX_RECORDS)
        goto fail;
    /* Each record costs at least its TC_OBJECT tag and reference, so a count
     * that could not possibly fit is refused before anything is allocated. */
    if ((int64_t)stream->declared_count > available) goto fail;

    xx_mem_zero(&cursor, sizeof(cursor));
    cursor.device = format->device;
    cursor.base = base;
    cursor.size = available;
    cursor.position = IZ_HEADER_SIZE;
    cursor.window_offset = -1;
    cursor.window = (uint8_t *)xx_mem_alloc(IZ_BUFFER_SIZE);
    if (!cursor.window) goto fail;
    xx_mem_zero(&parser, sizeof(parser));
    parser.cursor = &cursor;

    mask = 1U << (unsigned)version; /* version is 1..7 */

    for (index = 0; index < stream->declared_count; ++index) {
        iz_record record;
        uint8_t tag;
        uint8_t is_directory = 0U;
        int64_t length;
        int64_t mtime = 0;
        int64_t offset_in_previous = -1;
        int64_t int_fields;
        int64_t block_start;
        int64_t first_data = 0;
        int64_t excess = 0;
        int32_t chunks = 0;
        char *target = NULL;
        bool pack200 = false;

        xx_mem_zero(&record, sizeof(record));
        record.record_offset = base + cursor.position;

        if (iz_u8(&cursor) != IZ_TC_OBJECT) goto walk_failed;
        tag = iz_u8(&cursor);
        if (cursor.failed) goto walk_failed;
        if (tag == IZ_TC_CLASSDESC) {
            if (!iz_class_desc(&parser)) goto walk_failed;
        } else if (tag == IZ_TC_REFERENCE) {
            /* Every later record points back at the very first descriptor. */
            if (iz_i32(&cursor) != IZ_BASE_WIRE_HANDLE || cursor.failed)
                goto walk_failed;
        } else {
            goto walk_failed;
        }
        if (!iz_append_handle(&parser, false)) goto walk_failed;

        if (!(mask & 0x0eU)) {
            is_directory = iz_u8(&cursor);
            if (cursor.failed) goto walk_failed;
        }
        length = iz_i64(&cursor);
        if (version != 1) mtime = iz_i64(&cursor);
        if (!(mask & 0x06U)) offset_in_previous = iz_i64(&cursor);
        if (cursor.failed) goto walk_failed;
        /* override / previousPackNumber: two ints up to v5, one from v6 on,
         * none at all in v1. */
        int_fields = 8;
        if (version == 1)
            int_fields = 0;
        else if (version == 2 || version == 6 || version == 7)
            int_fields = 4;
        if (!iz_skip(&cursor, int_fields)) goto walk_failed;

        if (version == 7) {
            uint8_t flag = iz_u8(&cursor);
            if (cursor.failed) goto walk_failed;
            pack200 = flag != 0U;
        }
        if ((mask & 0xe0U) && !iz_read_content(&parser)) goto walk_failed;
        if ((mask & 0xc0U) && !iz_read_content(&parser)) goto walk_failed;
        if (version != 1 && !iz_read_array_list(&parser)) goto walk_failed;
        if ((mask & 0xc0U) && !iz_read_content(&parser)) goto walk_failed;
        if ((mask & 0xe0U) && !iz_read_string(&parser, NULL)) goto walk_failed;
        if (!iz_read_string(&parser, &target)) goto walk_failed;

        if (offset_in_previous != -1) {
            /* The bytes live in an earlier pack of the same set; this record
             * is a pointer, not a member, and carries no block data. */
            if (target) xx_mem_free(target);
            continue;
        }
        if (pack200) {
            /* A Pack200-compressed JAR: the payload is not the file, so the
             * reference steps over exactly four bytes of it and moves on. */
            if (target) xx_mem_free(target);
            if (!iz_scan_block_data(&parser, 4, &first_data, &chunks, &excess))
                goto walk_failed;
            continue;
        }

        record.folder = is_directory != 0U;
        if (!record.folder && length < 0) {
            if (target) xx_mem_free(target);
            goto walk_failed;
        }
        record.unpacked_size = record.folder ? 0 : length;
        if (record.unpacked_size > IZ_MAX_MEMBER_SIZE ||
            record.unpacked_size > available) {
            if (target) xx_mem_free(target);
            goto walk_failed;
        }

        block_start = cursor.position;
        if (!iz_scan_block_data(&parser, record.unpacked_size, &first_data,
                                &chunks, &excess)) {
            if (target) xx_mem_free(target);
            goto walk_failed;
        }
        if (chunks <= 1 && excess == 0) {
            /* A member that fits in one frame is a plain contiguous run, so
             * it is published as a stored stream and needs no de-framing. */
            record.stream_offset = base + first_data;
            record.stream_size = record.unpacked_size;
            record.framed = false;
        } else {
            record.stream_offset = base + block_start;
            record.stream_size = cursor.position - block_start;
            record.framed = true;
        }
        record.mtime = mtime;
        record.record_size = cursor.position - (record.record_offset - base);
        record.name = iz_normalize_name(target, (size_t)index);
        if (target) xx_mem_free(target);
        if (!record.name) goto walk_failed;
        if (!iz_add_record(stream, &record)) {
            xx_mem_free(record.name);
            goto walk_failed;
        }
    }

    stream->archive_size = cursor.position;
    iz_parser_cleanup(&parser);
    xx_mem_free(cursor.window);
    *result = stream;
    return true;

walk_failed:
    iz_parser_cleanup(&parser);
    if (cursor.window) xx_mem_free(cursor.window);
fail:
    iz_stream_free(stream);
    return false;
}

static bool iz_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *iz_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool iz_set_record(xx_archive_record *record, const iz_record *item) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = item->record_offset;
    record->header_size = item->record_size;
    record->data_offset = item->stream_offset;
    record->compressed_size = item->stream_size;
    return xx_archive_record_set_original_name(record, item->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)item->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)item->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          item->framed ? 1U : 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          (uint64_t)item->mtime) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           item->folder);
}

/* Lift the block-data chunk tags back out of a framed member. */
static bool iz_deframe(Abstractformat *format, const iz_record *item,
                       uint8_t *out) {
    int64_t cursor = item->stream_offset;
    int64_t end = item->stream_offset + item->stream_size;
    int64_t left = item->unpacked_size;
    while (left > 0) {
        uint8_t tag;
        int64_t chunk;
        if (cursor >= end) return false;
        if (!iz_read_at(format->device, cursor, &tag, 1)) return false;
        ++cursor;
        if (tag == IZ_TC_BLOCKDATALONG) {
            uint8_t raw[4];
            if (cursor + 4 > end ||
                !iz_read_at(format->device, cursor, raw, sizeof(raw)))
                return false;
            cursor += 4;
            chunk = ((int64_t)raw[0] << 24) | ((int64_t)raw[1] << 16) |
                    ((int64_t)raw[2] << 8) | (int64_t)raw[3];
        } else if (tag == IZ_TC_BLOCKDATA) {
            uint8_t raw;
            if (cursor + 1 > end ||
                !iz_read_at(format->device, cursor, &raw, 1))
                return false;
            ++cursor;
            chunk = (int64_t)raw;
        } else {
            return false;
        }
        if (chunk <= 0) return false;
        if (chunk > left) chunk = left;
        if (cursor + chunk > end ||
            !iz_read_at(format->device, cursor, out, (size_t)chunk))
            return false;
        cursor += chunk;
        out += chunk;
        left -= chunk;
    }
    return true;
}

void xx_izpack_init(xx_izpack *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_IZPACK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-izpack");
    xx_format_set_extension(&archive->format, "pack");
    archive->format.check_is_valid = xx_izpack_check_is_valid;
    archive->format.handle_base_info = xx_izpack_handle_base_info;
    archive->format.get_format_size = xx_izpack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_izpack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_izpack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_izpack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_izpack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_izpack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_izpack_free_archive_records_reading;
}

xx_izpack *xx_izpack_create(xx_io_device *device, int64_t base_address) {
    xx_izpack *archive = (xx_izpack *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_izpack_init(archive, device, base_address);
    return archive;
}

void xx_izpack_destroy(xx_izpack *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_izpack_free(xx_izpack *archive) {
    if (!archive) return;
    xx_izpack_destroy(archive);
    xx_mem_free(archive);
}

bool xx_izpack_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    iz_stream *stream;
    (void)pd;
    if (!iz_parse(format, &stream)) return false;
    iz_stream_free(stream);
    return true;
}

bool xx_izpack_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    iz_stream *stream;
    xx_izpack *archive;
    int64_t available;
    (void)pd;
    if (!format) return false;
    if (!iz_parse(format, &stream)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_izpack *)format;
    archive->number_of_records = stream->count;
    archive->pack_version = stream->version;
    archive->declared_count = stream->declared_count;
    available = xx_io_total_size(format->device) - format->base_address;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    /* Whatever follows the last record is the installer's own parsable and
     * executable lists, not part of the member chain. */
    if (available > stream->archive_size) {
        format->overlay_offset = format->base_address + stream->archive_size;
        format->overlay_size = available - stream->archive_size;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    format->file_type = XX_IZPACK_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    iz_stream_free(stream);
    return true;
}

int64_t xx_izpack_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_izpack_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_izpack_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_izpack_handle_base_info(format, pd))
               ? ((xx_izpack *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_izpack_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    iz_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!iz_parse(format, &stream)) return NULL;
    if (stream->count == 0U) {
        iz_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        iz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = iz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!iz_copy_options(&state->options, options) ||
        !iz_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_izpack_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_izpack_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    iz_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (iz_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = iz_set_record(&state->current_record,
                                      &stream->items[stream->index]);
    return state->has_record;
}

bool xx_izpack_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    iz_stream *stream;
    iz_record *item;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (iz_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    item = &stream->items[stream->index];
    if (!iz_safe_output_name(item->name)) return false;
    path_option = iz_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true; /* A dry run: the member is addressable. */
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", item->name)
               : xx_str_concat(base, item->name);
    if (!path) goto done;
    if (item->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    if (!item->framed) {
        result = xx_store_unpack_device_to_file(format->device,
                                                item->stream_offset,
                                                item->unpacked_size, path, pd);
        goto done;
    }
    if ((uint64_t)item->unpacked_size > SIZE_MAX) goto done;
    plain = (uint8_t *)xx_mem_alloc(item->unpacked_size
                                        ? (size_t)item->unpacked_size
                                        : 1U);
    if (!plain || !iz_deframe(format, item, plain)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        size_t total = (size_t)item->unpacked_size;
        if (!destination) goto done;
        created = true;
        result = true;
        while (written < total) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         total - written);
            if (amount <= 0 || (size_t)amount > total - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && created) xx_io_file_remove_a(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_izpack_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
