/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Tcl Starkits -- a Metakit v4 datafile holding the mk4vfs filesystem.
 * XArchive has no module for this one.  The container was recovered from U3's
 * Starkit handler (class fmb, VMT 006cf978; recognition predicate
 * decompiled/functions/006c/006cfd80.c -> 006cfc80 -> 006cfa10) plus a
 * byte-level reconstruction against the corpus, and the result is checked
 * against U3's own listing: file count and total uncompressed size agree
 * exactly on all three samples.
 *
 *   file header, 8 bytes at offset 0:
 *     0x00   4  bytes    "JL" 0x1a 0x00
 *     0x04   4  u32 BE   the file's own length
 *
 *   commit footer, 16 bytes at end of file, two (tag, position) pairs:
 *     -16    4  u32 BE   0x80000000
 *     -12    4  u32 BE   position of the footer itself
 *     -8     4  u32 BE   0x80000000 | size of the root block
 *     -4     4  u32 BE   position of the root block
 *
 *   root block:
 *     0x00   1  u8       0x80
 *     0x01   1  u8       0x80 | length of the schema string
 *     0x02   n  char[n]  the schema, which for a Starkit is exactly
 *                        "dirs[name:S,parent:I,files[name:S,size:I,date:I,
 *                         contents:B]]"
 *     then three varints: row count (1), and the size and position of the
 *     "dirs" view descriptor.
 *
 * Everything past that point is Metakit's own encoding, which this reader
 * implements only as far as that one schema needs:
 *
 *   VARINT.  Seven bits per byte, most significant group first, and the byte
 *   with its HIGH BIT SET terminates the number -- the opposite convention
 *   from LEB128, and the single thing that unlocks the rest of the format.
 *
 *   COLUMN.  A varint size; a varint position follows only when the size is
 *   non-zero.
 *
 *   VIEW.  A varint that is always zero, then the row count.  A view with
 *   zero rows stops there and writes no columns at all.  Otherwise each
 *   property contributes: an int property one column; a string ("S") or bytes
 *   ("B") property a data column, then a sizes column -- omitted entirely
 *   when the data column is empty -- then a memo column.
 *
 *   INT COLUMN.  Bit packed, the width being the widest of 32/16/8/4/2/1 bits
 *   that fits the row count in the stored byte size, values little endian
 *   within a byte for the sub-byte widths.
 *
 *   MEMO COLUMN.  Varint triples (row delta, size, position) for the rows
 *   whose value is stored away from the data column -- that is where every
 *   large file body lives.  The delta is counted from the previous memo row
 *   plus one, so a first triple of 1 means row 1.
 *
 *   SUBVIEW COLUMN.  One nested view descriptor per row of the parent view,
 *   written back to back inside the column's extent.
 *
 * So "dirs" is one view of N rows with a name, a parent index and a nested
 * "files" view per row; each file carries its name, its uncompressed size, a
 * Unix timestamp, and its bytes -- stored raw when compression did not pay,
 * otherwise as a zlib stream.  Member paths are rebuilt by walking the parent
 * chain; row 0 is the "<root>" directory and contributes no path component.
 *
 * Recognition is the magic, the self-declared file length, both footer tags,
 * the root block's shape and an exact match on the schema string, followed by
 * the requirement that every descriptor consumes its extent to the byte.  A
 * file that survives all of that is a Starkit.
 *
 * All 3 corpus samples in F:\ARC\ARC\STARKIT parse: 841, 841 and 497
 * members, and every member decodes to exactly its declared size -- the same
 * counts and the same byte totals U3 reports.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/starkit/xx_starkit.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"

#include <stdio.h>

#ifdef STARKIT
#define XX_STARKIT_FILE_TYPE XX_FILE_TYPE_STARKIT
#else
#define XX_STARKIT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_STARKIT_METHOD_STORE 0U
#define XX_STARKIT_METHOD_ZLIB 8U
#define XX_STARKIT_HEADER_SIZE 8
#define XX_STARKIT_FOOTER_SIZE 16
#define XX_STARKIT_SCHEMA \
    "dirs[name:S,parent:I,files[name:S,size:I,date:I,contents:B]]"
/* Ceilings.  Descriptors are small; only the file bodies are large, and they
 * are read one member at a time at unpack. */
#define XX_STARKIT_MAX_ROOT 4096
#define XX_STARKIT_MAX_DESCRIPTOR (64 * 1024)
#define XX_STARKIT_MAX_SUBVIEWS (32 * 1024 * 1024)
#define XX_STARKIT_MAX_COLUMN (32 * 1024 * 1024)
#define XX_STARKIT_MAX_DIRS 65536U
#define XX_STARKIT_MAX_ROWS 1000000U
#define XX_STARKIT_MAX_MEMBERS 2000000U
/* A member's plain size is not bounded by the file, so it gets its own
 * ceiling rather than none at all. */
#define XX_STARKIT_MAX_PLAIN (1024ULL * 1024ULL * 1024ULL)

typedef struct xx_starkit_member_s {
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
} xx_starkit_member;

typedef struct xx_starkit_stream_s {
    xx_starkit_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_starkit_stream;

static void xx_starkit_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_starkit_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_starkit_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_starkit_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_starkit_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_starkit_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_starkit_path_safe(const char *path) {
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
static char *xx_starkit_make_name(const uint8_t *raw, size_t size,
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

static void xx_starkit_stream_free(void *pointer) {
    xx_starkit_stream *stream = (xx_starkit_stream *)pointer;
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
static bool xx_starkit_add(xx_starkit_stream *stream,
                          const xx_starkit_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_starkit_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_starkit_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

typedef struct xx_starkit_col_s {
    uint64_t size;
    uint64_t position;
} xx_starkit_col;

/* A Metakit "S" or "B" property: concatenated data, per-row sizes, and the
 * memo list for the rows stored away from the data column. */
typedef struct xx_starkit_prop_s {
    xx_starkit_col data;
    xx_starkit_col sizes;
    xx_starkit_col memo;
} xx_starkit_prop;

typedef struct xx_starkit_cursor_s {
    const uint8_t *buffer;
    size_t size;
    size_t at;
} xx_starkit_cursor;

/* Metakit's varint: seven bits per byte, most significant group first, and
 * the byte with its HIGH BIT SET ends the number. */
static bool xx_starkit_varint(xx_starkit_cursor *cursor, uint64_t *value) {
    uint64_t result = 0U;
    unsigned steps = 0U;

    if (!cursor || !value) return false;
    while (cursor->at < cursor->size) {
        uint8_t byte = cursor->buffer[cursor->at++];
        /* Nine groups is already more than a 63-bit value needs; anything
         * longer is a malformed stream rather than a big number. */
        if (++steps > 9U) return false;
        result = (result << 7) | (uint64_t)(byte & 0x7fU);
        if ((byte & 0x80U) != 0U) {
            *value = result;
            return true;
        }
    }
    return false;
}

/* A column is a size, then a position only when the size is non-zero. */
static bool xx_starkit_column(xx_starkit_cursor *cursor,
                              xx_starkit_col *column) {
    column->position = 0U;
    if (!xx_starkit_varint(cursor, &column->size)) return false;
    return column->size == 0U ||
           xx_starkit_varint(cursor, &column->position);
}

/* An "S"/"B" property.  The sizes column is not written at all when the data
 * column is empty, which is what happens when every value is a memo. */
static bool xx_starkit_property(xx_starkit_cursor *cursor,
                                xx_starkit_prop *prop) {
    if (!xx_starkit_column(cursor, &prop->data)) return false;
    prop->sizes.size = 0U;
    prop->sizes.position = 0U;
    if (prop->data.size != 0U && !xx_starkit_column(cursor, &prop->sizes)) {
        return false;
    }
    return xx_starkit_column(cursor, &prop->memo);
}

/* Int columns are bit packed; the width is the widest of 32/16/8/4/2/1 bits
 * whose packed length still fits in the stored byte size. */
static unsigned xx_starkit_width(uint64_t rows, uint64_t size) {
    static const unsigned choices[6] = {32U, 16U, 8U, 4U, 2U, 1U};
    unsigned index;

    if (rows == 0U || size == 0U) return 0U;
    for (index = 0U; index < 6U; ++index) {
        if ((rows * choices[index] + 7U) / 8U <= size) return choices[index];
    }
    return 0U;
}

static uint64_t xx_starkit_int(const uint8_t *data, size_t size,
                               unsigned width, uint64_t index) {
    if (!data || width == 0U) return 0U;
    if (width >= 8U) {
        size_t bytes = (size_t)(width / 8U);
        size_t at = (size_t)index * bytes;
        uint64_t value = 0U;
        size_t step;
        if (at > size || bytes > size - at) return 0U;
        for (step = 0U; step < bytes; ++step) {
            value |= (uint64_t)data[at + step] << (step * 8U);
        }
        return value;
    }
    {
        unsigned per = 8U / width;
        size_t at = (size_t)(index / per);
        unsigned shift = (unsigned)(index % per) * width;
        if (at >= size) return 0U;
        return ((uint64_t)data[at] >> shift) & ((1U << width) - 1U);
    }
}

/* Read a bounded region of the file into a fresh buffer. */
static uint8_t *xx_starkit_region(Abstractformat *self, int64_t span,
                                  uint64_t offset, uint64_t size,
                                  uint64_t limit) {
    uint8_t *buffer;

    if (size > limit || offset > (uint64_t)span ||
        size > (uint64_t)span - offset) {
        return NULL;
    }
    buffer = (uint8_t *)xx_mem_alloc(size != 0U ? (size_t)size : 1U);
    if (!buffer) return NULL;
    if (size != 0U &&
        !xx_starkit_read_at(self, self->base_address + (int64_t)offset,
                            buffer, (size_t)size)) {
        xx_mem_free(buffer);
        return NULL;
    }
    return buffer;
}

/* Split a name blob into its NUL-terminated entries and hand back the one at
 * @p index.  The per-row lengths INCLUDE the terminator, and they must add up
 * to the blob exactly -- that sum is one of the checks that keeps a
 * mis-parsed descriptor from producing plausible-looking names. */
static bool xx_starkit_names_ok(const uint8_t *sizes, size_t sizes_size,
                                unsigned width, uint64_t rows,
                                uint64_t data_size) {
    uint64_t total = 0U;
    uint64_t index;

    for (index = 0U; index < rows; ++index) {
        uint64_t length = xx_starkit_int(sizes, sizes_size, width, index);
        if (length == 0U || length > data_size - total) return false;
        total += length;
    }
    return total == data_size;
}

static void xx_starkit_free_paths(char **paths, uint64_t count) {
    uint64_t index;

    if (!paths) return;
    for (index = 0U; index < count; ++index) xx_str_free(paths[index]);
    xx_mem_free(paths);
}

/* One memo entry: the row it belongs to, and where its bytes live. */
typedef struct xx_starkit_memo_s {
    uint64_t row;
    uint64_t size;
    uint64_t position;
} xx_starkit_memo;

/* Decode a memo column.  The stored row field is a DELTA from the previous
 * memo row plus one, so the rows come out strictly increasing. */
static xx_starkit_memo *xx_starkit_memos(const uint8_t *buffer, size_t size,
                                         uint64_t rows, uint64_t *count) {
    xx_starkit_cursor cursor;
    xx_starkit_memo *list;
    uint64_t used = 0U;
    uint64_t previous = 0U;
    bool first = true;

    *count = 0U;
    if (size == 0U) return NULL;
    list = (xx_starkit_memo *)xx_mem_alloc((size_t)rows * sizeof(*list));
    if (!list) return NULL;
    cursor.buffer = buffer;
    cursor.size = size;
    cursor.at = 0U;
    while (cursor.at < size) {
        uint64_t delta;
        uint64_t row;
        if (used >= rows ||
            !xx_starkit_varint(&cursor, &delta) ||
            !xx_starkit_varint(&cursor, &list[used].size) ||
            !xx_starkit_varint(&cursor, &list[used].position)) {
            xx_mem_free(list);
            return NULL;
        }
        row = first ? delta : previous + 1U + delta;
        if (row >= rows || (!first && row <= previous)) {
            xx_mem_free(list);
            return NULL;
        }
        list[used].row = row;
        previous = row;
        first = false;
        ++used;
    }
    *count = used;
    return list;
}


/* --------------------------------------------------------------- parse -- */

static xx_starkit_stream *xx_starkit_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_starkit_stream *stream = NULL;
    uint8_t head[XX_STARKIT_HEADER_SIZE];
    uint8_t footer[XX_STARKIT_FOOTER_SIZE];
    uint8_t *root = NULL;
    uint8_t *descriptor = NULL;
    uint8_t *dir_names = NULL;
    uint8_t *dir_sizes = NULL;
    uint8_t *dir_parents = NULL;
    uint8_t *subviews = NULL;
    char **paths = NULL;
    xx_starkit_cursor cursor;
    xx_starkit_cursor views;
    xx_starkit_prop dir_name;
    xx_starkit_col dir_parent;
    xx_starkit_col dir_files;
    int64_t total;
    int64_t span;
    uint64_t root_size;
    uint64_t root_position;
    uint64_t schema_size;
    uint64_t value;
    uint64_t rows;
    uint64_t directories = 0U;
    uint64_t index;
    unsigned dir_name_width;
    unsigned dir_parent_width;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_STARKIT_HEADER_SIZE + XX_STARKIT_FOOTER_SIZE) return NULL;
    if (!xx_starkit_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }
    /* The magic plus the file's own declared length. */
    if (xx_rt_memcmp(head, "JL\x1a\x00", 4U) != 0 ||
        (int64_t)xx_starkit_be32(head + 4) != span) {
        return NULL;
    }
    if (!xx_starkit_read_at(self, self->base_address + span -
                                      XX_STARKIT_FOOTER_SIZE,
                            footer, sizeof(footer))) {
        return NULL;
    }
    /* The commit footer: an end marker that points at itself, then the tagged
     * size and position of the root block. */
    if (xx_starkit_be32(footer) != 0x80000000U ||
        (int64_t)xx_starkit_be32(footer + 4) !=
            span - XX_STARKIT_FOOTER_SIZE ||
        (xx_starkit_be32(footer + 8) & 0x80000000U) == 0U) {
        return NULL;
    }
    root_size = xx_starkit_be32(footer + 8) & 0x7fffffffU;
    root_position = xx_starkit_be32(footer + 12);
    root = xx_starkit_region(self, span, root_position, root_size,
                             XX_STARKIT_MAX_ROOT);
    if (!root) return NULL;
    if (root_size < 3U || root[0] != 0x80U || (root[1] & 0x80U) == 0U) {
        goto fail;
    }
    schema_size = (uint64_t)(root[1] & 0x7fU);
    /* Only the mk4vfs schema is supported, and it is required verbatim: a
     * Metakit database with any other schema is not a Starkit, and guessing
     * at an unknown one would be worse than refusing. */
    if (schema_size != xx_str_len(XX_STARKIT_SCHEMA) ||
        root_size - 2U < schema_size ||
        xx_rt_memcmp(root + 2, XX_STARKIT_SCHEMA, (size_t)schema_size) != 0) {
        goto fail;
    }

    cursor.buffer = root;
    cursor.size = (size_t)root_size;
    cursor.at = (size_t)(2U + schema_size);
    /* Row count of the root view, then the "dirs" subview column. */
    if (!xx_starkit_varint(&cursor, &value) || value != 1U ||
        !xx_starkit_column(&cursor, &dir_files) || dir_files.size == 0U) {
        goto fail;
    }
    descriptor = xx_starkit_region(self, span, dir_files.position,
                                   dir_files.size, XX_STARKIT_MAX_DESCRIPTOR);
    if (!descriptor) goto fail;

    cursor.buffer = descriptor;
    cursor.size = (size_t)dir_files.size;
    cursor.at = 0U;
    if (!xx_starkit_varint(&cursor, &value) || value != 0U ||
        !xx_starkit_varint(&cursor, &rows) || rows == 0U ||
        rows > XX_STARKIT_MAX_DIRS ||
        !xx_starkit_property(&cursor, &dir_name) ||
        !xx_starkit_column(&cursor, &dir_parent) ||
        !xx_starkit_column(&cursor, &dir_files)) {
        goto fail;
    }
    /* The descriptor must be consumed to the byte; a leftover tail means the
     * grammar did not match this file. */
    if (cursor.at != cursor.size || dir_name.memo.size != 0U ||
        dir_files.size == 0U) {
        goto fail;
    }
    directories = rows;

    dir_names = xx_starkit_region(self, span, dir_name.data.position,
                                  dir_name.data.size, XX_STARKIT_MAX_COLUMN);
    dir_sizes = xx_starkit_region(self, span, dir_name.sizes.position,
                                  dir_name.sizes.size, XX_STARKIT_MAX_COLUMN);
    dir_parents = xx_starkit_region(self, span, dir_parent.position,
                                    dir_parent.size, XX_STARKIT_MAX_COLUMN);
    if (!dir_names || !dir_sizes || !dir_parents) goto fail;
    dir_name_width = xx_starkit_width(rows, dir_name.sizes.size);
    dir_parent_width = xx_starkit_width(rows, dir_parent.size);
    if (dir_name_width == 0U || dir_parent_width == 0U) goto fail;
    if (!xx_starkit_names_ok(dir_sizes, (size_t)dir_name.sizes.size,
                             dir_name_width, rows, dir_name.data.size)) {
        goto fail;
    }

    /* Rebuild one path prefix per directory by walking the parent chain.
     * Row 0 is "<root>" and contributes nothing; every other row's parent
     * must already have been resolved, which also rules out a cycle. */
    paths = (char **)xx_mem_alloc((size_t)rows * sizeof(*paths));
    if (!paths) goto fail;
    xx_mem_zero(paths, (size_t)rows * sizeof(*paths));
    {
        uint64_t at = 0U;
        for (index = 0U; index < rows; ++index) {
            uint64_t length =
                xx_starkit_int(dir_sizes, (size_t)dir_name.sizes.size,
                               dir_name_width, index);
            uint64_t parent = xx_starkit_int(dir_parents,
                                             (size_t)dir_parent.size,
                                             dir_parent_width, index);
            char *name;
            if (index == 0U) {
                paths[0] = xx_str_dup("");
                if (!paths[0]) goto fail;
                at += length;
                continue;
            }
            if (parent >= index) goto fail;
            name = xx_starkit_make_name(dir_names + (size_t)at,
                                        (size_t)(length - 1U), false);
            if (!name) goto fail;
            paths[index] = xx_str_concat3(paths[parent], name, "/");
            xx_str_free(name);
            if (!paths[index]) goto fail;
            at += length;
        }
    }

    subviews = xx_starkit_region(self, span, dir_files.position,
                                 dir_files.size, XX_STARKIT_MAX_SUBVIEWS);
    if (!subviews) goto fail;
    views.buffer = subviews;
    views.size = (size_t)dir_files.size;
    views.at = 0U;

    stream = (xx_starkit_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0U; index < directories; ++index) {
        xx_starkit_prop file_name;
        xx_starkit_prop contents;
        xx_starkit_col file_size;
        xx_starkit_col file_date;
        xx_starkit_memo *memos = NULL;
        uint8_t *names = NULL;
        uint8_t *sizes = NULL;
        uint8_t *lengths = NULL;
        uint8_t *content_sizes = NULL;
        uint8_t *memo_bytes = NULL;
        unsigned name_width;
        unsigned size_width;
        unsigned content_width;
        uint64_t memo_count = 0U;
        uint64_t memo_at = 0U;
        uint64_t name_at = 0U;
        uint64_t data_at;
        uint64_t row;
        uint64_t file_rows;
        bool failed = false;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_starkit_varint(&views, &value) || value != 0U ||
            !xx_starkit_varint(&views, &file_rows)) {
            goto fail;
        }
        /* A directory with no files writes no columns at all. */
        if (file_rows == 0U) continue;
        if (file_rows > XX_STARKIT_MAX_ROWS ||
            (uint64_t)stream->count + file_rows > XX_STARKIT_MAX_MEMBERS) {
            goto fail;
        }
        if (!xx_starkit_property(&views, &file_name) ||
            !xx_starkit_column(&views, &file_size) ||
            !xx_starkit_column(&views, &file_date) ||
            !xx_starkit_property(&views, &contents)) {
            goto fail;
        }
        if (file_name.memo.size != 0U) goto fail;

        names = xx_starkit_region(self, span, file_name.data.position,
                                  file_name.data.size, XX_STARKIT_MAX_COLUMN);
        lengths = xx_starkit_region(self, span, file_name.sizes.position,
                                    file_name.sizes.size,
                                    XX_STARKIT_MAX_COLUMN);
        sizes = xx_starkit_region(self, span, file_size.position,
                                  file_size.size, XX_STARKIT_MAX_COLUMN);
        content_sizes = xx_starkit_region(self, span, contents.sizes.position,
                                          contents.sizes.size,
                                          XX_STARKIT_MAX_COLUMN);
        memo_bytes = xx_starkit_region(self, span, contents.memo.position,
                                       contents.memo.size,
                                       XX_STARKIT_MAX_COLUMN);
        name_width = xx_starkit_width(file_rows, file_name.sizes.size);
        size_width = xx_starkit_width(file_rows, file_size.size);
        content_width = xx_starkit_width(file_rows, contents.sizes.size);
        if (!names || !lengths || !sizes || !content_sizes || !memo_bytes ||
            name_width == 0U || size_width == 0U ||
            !xx_starkit_names_ok(lengths, (size_t)file_name.sizes.size,
                                 name_width, file_rows,
                                 file_name.data.size)) {
            failed = true;
        }
        if (!failed && contents.memo.size != 0U) {
            memos = xx_starkit_memos(memo_bytes, (size_t)contents.memo.size,
                                     file_rows, &memo_count);
            if (!memos) failed = true;
        }
        data_at = contents.data.position;
        for (row = 0U; !failed && row < file_rows; ++row) {
            xx_starkit_member member;
            uint64_t name_length =
                xx_starkit_int(lengths, (size_t)file_name.sizes.size,
                               name_width, row);
            uint64_t plain = xx_starkit_int(sizes, (size_t)file_size.size,
                                            size_width, row);
            uint64_t packed;
            uint64_t position;

            if (memo_at < memo_count && memos[memo_at].row == row) {
                packed = memos[memo_at].size;
                position = memos[memo_at].position;
                ++memo_at;
            } else {
                packed = xx_starkit_int(content_sizes,
                                        (size_t)contents.sizes.size,
                                        content_width, row);
                position = data_at;
                data_at += packed;
            }
            /* Every extent is bounded against the real file, and a plain size
             * gets its own ceiling because the file does not bound it. */
            if (plain > XX_STARKIT_MAX_PLAIN || packed > (uint64_t)span ||
                position > (uint64_t)span ||
                packed > (uint64_t)span - position) {
                failed = true;
                break;
            }
            xx_mem_zero(&member, sizeof(member));
            {
                char *leaf = xx_starkit_make_name(names + (size_t)name_at,
                                                  (size_t)(name_length - 1U),
                                                  false);
                if (!leaf) {
                    failed = true;
                    break;
                }
                member.name = xx_str_concat(paths[index], leaf);
                xx_str_free(leaf);
            }
            name_at += name_length;
            if (!member.name) {
                failed = true;
                break;
            }
            member.header_offset =
                self->base_address + (int64_t)file_name.data.position;
            member.header_size = 0;
            member.data_offset = self->base_address + (int64_t)position;
            member.packed_size = (int64_t)packed;
            member.unpacked_size = plain;
            member.method = packed == plain ? XX_STARKIT_METHOD_STORE
                                            : XX_STARKIT_METHOD_ZLIB;
            if (!xx_starkit_add(stream, &member)) {
                xx_str_free(member.name);
                failed = true;
                break;
            }
        }
        /* The inline bodies must account for the data column exactly. */
        if (!failed && data_at != contents.data.position + contents.data.size) {
            failed = true;
        }
        if (memos) xx_mem_free(memos);
        if (names) xx_mem_free(names);
        if (lengths) xx_mem_free(lengths);
        if (sizes) xx_mem_free(sizes);
        if (content_sizes) xx_mem_free(content_sizes);
        if (memo_bytes) xx_mem_free(memo_bytes);
        if (failed) goto fail;
    }

    /* The subview block must be consumed to the byte as well. */
    if (views.at != views.size || stream->count == 0U) goto fail;

    xx_starkit_free_paths(paths, directories);
    xx_mem_free(subviews);
    xx_mem_free(dir_parents);
    xx_mem_free(dir_sizes);
    xx_mem_free(dir_names);
    xx_mem_free(descriptor);
    xx_mem_free(root);
    stream->archive_size = span;
    return stream;

fail:
    xx_starkit_free_paths(paths, directories);
    if (subviews) xx_mem_free(subviews);
    if (dir_parents) xx_mem_free(dir_parents);
    if (dir_sizes) xx_mem_free(dir_sizes);
    if (dir_names) xx_mem_free(dir_names);
    if (descriptor) xx_mem_free(descriptor);
    if (root) xx_mem_free(root);
    xx_starkit_stream_free(stream);
    return NULL;
}

/* mk4vfs stores a body raw when compression did not pay and as a zlib stream
 * otherwise; the row's own uncompressed size is the anchor either way, and a
 * decode that misses it is an error rather than a result. */
static bool xx_starkit_decode(Abstractformat *self,
                              const xx_starkit_member *member, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->unpacked_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->packed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (member->packed_size == 0) {
        return member->unpacked_size == 0U;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!packed) return false;
    if (!xx_starkit_read_at(self, member->data_offset, packed,
                            (size_t)member->packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (member->method == XX_STARKIT_METHOD_STORE) {
        if ((uint64_t)member->packed_size != member->unpacked_size) {
            xx_mem_free(packed);
            return false;
        }
        *out = packed;
        *out_size = (size_t)member->packed_size;
        return true;
    }
    plain = (uint8_t *)xx_mem_alloc(
        member->unpacked_size != 0U ? (size_t)member->unpacked_size : 1U);
    if (!plain ||
        !xx_zlib_stream_decode_memory(packed, (size_t)member->packed_size,
                                      plain, (size_t)member->unpacked_size,
                                      &written) ||
        written != member->unpacked_size) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = written;
    return true;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_starkit_init(xx_starkit *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_STARKIT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-tcl-starkit");
    xx_format_set_extension(&archive->format, "kit");
    archive->format.check_is_valid = xx_starkit_check_is_valid;
    archive->format.handle_base_info = xx_starkit_handle_base_info;
    archive->format.get_format_size = xx_starkit_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_starkit_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_starkit_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_starkit_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_starkit_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_starkit_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_starkit_free_archive_records_reading;
    archive->format.destroy = xx_starkit_vtable_destroy;
}

xx_starkit *xx_starkit_create(xx_io_device *device, int64_t base_address) {
    xx_starkit *archive = (xx_starkit *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_starkit_init(archive, device, base_address);
    return archive;
}

void xx_starkit_destroy(xx_starkit *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_starkit_free(xx_starkit *archive) {
    if (!archive) return;
    xx_starkit_destroy(archive);
    xx_mem_free(archive);
}

static void xx_starkit_vtable_destroy(Abstractformat *self) {
    xx_starkit_destroy((xx_starkit *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_starkit_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_starkit_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_starkit_parse(self, pd);
    if (!stream) return false;
    xx_starkit_stream_free(stream);
    return true;
}

bool xx_starkit_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_starkit *archive = (xx_starkit *)self;
    xx_starkit_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_starkit_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_starkit_stream_free(stream);
    return true;
}

int64_t xx_starkit_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_starkit_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_starkit *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_starkit_set_record(xx_archive_record *record,
                                 const xx_starkit_member *member) {
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

static bool xx_starkit_copy_options(xx_list_s *target,
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

static const xx_var *xx_starkit_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_starkit_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_starkit_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_starkit_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_starkit_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_starkit_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_starkit_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_starkit_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_starkit_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_starkit_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_starkit_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_starkit_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_starkit_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_starkit_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_starkit_stream *stream;
    const xx_starkit_member *member;
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
    stream = (xx_starkit_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_starkit_path_safe(member->name)) return false;

    path_option =
        xx_starkit_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_starkit_decode(self, member, &plain, &plain_size, pd);
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
        !xx_starkit_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_starkit_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
