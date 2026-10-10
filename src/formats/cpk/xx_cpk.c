/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CRI Middleware CPK resource archive.  xx_cpk.h carries the packet, @UTF
 * table and CRILAYLA layouts.  Written from the format's structure; GARbro's
 * ArcFormats/Cri/ArcCPK.cs (MIT, (C) 2016 morkt) was the reference for the
 * member-offset base (min(ContentOffset, TocOffset)), the ITOC-only layout,
 * the UTF obfuscation key and the CRILAYLA semantics (the uncompressed
 * prefix, the backwards bit order and the 2/3/5/8 length fields).  The
 * member-name rules and the index handling follow this library's nsa reader
 * (src/formats/nsa/xx_nsa.c, MIT).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cpk/xx_cpk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef CPK
#define XX_CPK_FILE_TYPE XX_FILE_TYPE_CPK
#else
#define XX_CPK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define CPK_PACKET_HEAD 16U
#define CPK_UTF_HEAD 32U
/* A CpkMaker header table is under 2 KiB; the TOC of a 100,000-file
 * archive is a few MiB. */
#define CPK_MAX_HEADER_TABLE (1U << 20)
#define CPK_MAX_INDEX_TABLE (64U << 20)
#define CPK_MAX_ROWS (1U << 20)
#define CPK_MAX_COLUMNS 256U
#define CPK_MAX_STRING 1024U
#define CPK_NAME_BUFFER (3U * (2U * CPK_MAX_STRING + 1U) + 16U)
/* CRILAYLA is decoded in memory: input and output are each capped. */
#define CPK_MAX_DECODE (256U << 20)
#define CPK_POLL_MASK 0x3ffU

#define CPK_STORAGE_NONE 0x00U
#define CPK_STORAGE_ZERO 0x10U
#define CPK_STORAGE_CONST 0x30U
#define CPK_STORAGE_ROW 0x50U
#define CPK_TYPE_F32 8U
#define CPK_TYPE_F64 9U
#define CPK_TYPE_STRING 10U
#define CPK_TYPE_DATA 11U

#define CPK_METHOD_STORED 0U
#define CPK_METHOD_CRILAYLA 1U

static const uint8_t cpk_type_size[12] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 4, 8};
/* "@UTF" after the XOR obfuscation. */
static const uint8_t cpk_utf_obfuscated[4] = {0x1FU, 0x9EU, 0xF3U, 0xF5U};

typedef struct cpk_column_s {
    uint32_t name;  /**< String-pool offset of the column name. */
    uint32_t value; /**< Constant: table offset; per row: row position. */
    uint8_t storage;
    uint8_t type;
} cpk_column;

typedef struct cpk_utf_s {
    const uint8_t *table;
    size_t size;
    size_t rows_at;
    size_t strings_at;
    size_t data_at;
    uint32_t columns;
    uint32_t width;
    uint32_t rows;
    cpk_column column[CPK_MAX_COLUMNS];
} cpk_utf;

typedef struct cpk_header_s {
    int64_t available;    /**< Bytes from base_address to EOF. */
    int64_t header_end;   /**< End of the "CPK " packet. */
    int64_t content;      /**< ContentOffset, or -1. */
    int64_t content_size; /**< ContentSize, or -1. */
    int64_t toc;          /**< TocOffset (relative), or -1. */
    int64_t toc_size;     /**< Packet size including the 16-byte head. */
    int64_t itoc;
    int64_t itoc_size;
    uint32_t align;
    bool encrypted;
} cpk_header;

typedef struct cpk_member_s {
    int64_t offset; /**< Relative to base_address. */
    int64_t size;
    int64_t extract; /**< ExtractSize, or -1. */
    uint64_t hash;
    uint32_t row; /**< TOC row, or ITOC ID. */
    bool in_range;
    bool renamed;
} cpk_member;

typedef struct cpk_key_s {
    uint64_t hash;
    uint32_t index;
} cpk_key;

typedef struct cpk_stream_s {
    cpk_member *items;
    size_t count;
    size_t index;
    uint8_t *table; /**< The TOC table, kept for the names. */
    cpk_utf *utf;
    int32_t dir_column;
    int32_t file_column;
    bool toc_mode;
    char *name;
    int64_t format_size;
} cpk_stream;

/* ---- I/O --------------------------------------------------------------- */

static size_t cpk_capacity(void)
{
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < 4096U) n = 4096U;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}

static bool cpk_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static uint32_t cpk_be16(const uint8_t *b)
{
    return ((uint32_t)b[0] << 8U) | (uint32_t)b[1];
}

static bool cpk_read_at(Abstractformat *format, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    int64_t at;
    if (!format || !format->device || offset < 0 || offset > INT64_MAX - format->base_address) return false;
    at = format->base_address + offset;
    if (xx_io_seek64(format->device, at, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(format->device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool cpk_write_all(xx_io_device *destination, const uint8_t *data, size_t size)
{
    size_t done = 0U;
    if (!destination) return true;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool cpk_copy_range(Abstractformat *format, int64_t offset, int64_t size, xx_io_device *destination, xx_pd_struct *pd)
{
    const size_t capacity = cpk_capacity();
    uint8_t *buffer;
    int64_t done = 0;
    bool ok = true;
    if (offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (done < size) {
        size_t chunk = size - done > (int64_t)capacity ? capacity : (size_t)(size - done);
        if (cpk_stopped(pd) || !cpk_read_at(format, offset + done, buffer, chunk) || !cpk_write_all(destination, buffer, chunk)) {
            ok = false;
            break;
        }
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

static int64_t cpk_available(Abstractformat *format)
{
    int64_t total;
    if (!format || !format->device || format->base_address < 0) return -1;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return -1;
    return total - format->base_address;
}

/* ---- @UTF tables ------------------------------------------------------- */

static void cpk_deobfuscate(uint8_t *data, size_t size)
{
    uint32_t key = 0x655FU;
    size_t index;
    for (index = 0U; index < size; ++index) {
        data[index] ^= (uint8_t)key;
        key *= 0x4115U;
    }
}

/* A NUL-terminated string at @p offset of the string pool. */
static bool cpk_utf_string(const cpk_utf *utf, uint64_t offset, const uint8_t **text, size_t *length)
{
    uint64_t at = (uint64_t)utf->strings_at + offset;
    size_t limit, index;
    if (offset >= (uint64_t)utf->size || at >= (uint64_t)utf->size) return false;
    limit = utf->size - (size_t)at;
    if (limit > CPK_MAX_STRING + 1U) limit = CPK_MAX_STRING + 1U;
    for (index = 0U; index < limit; ++index)
        if (utf->table[(size_t)at + index] == 0U) {
            *text = utf->table + (size_t)at;
            *length = index;
            return true;
        }
    return false;
}

static bool cpk_utf_open(const uint8_t *table, size_t size, cpk_utf *utf)
{
    uint64_t declared, rows_at, strings_at, data_at, row_bytes;
    uint32_t index, width = 0U;
    size_t at;
    if (!table || size < CPK_UTF_HEAD || xx_rt_memcmp(table, "@UTF", 4U) != 0) return false;
    declared = (uint64_t)xx_data_get_u32(table + 4, 4, 0, true) + 8U;
    if (declared < CPK_UTF_HEAD || declared > (uint64_t)size) return false;
    utf->table = table;
    utf->size = (size_t)declared;
    rows_at = 8U + (uint64_t)cpk_be16(table + 0x0A);
    strings_at = 8U + (uint64_t)xx_data_get_u32(table + 0x0C, 4, 0, true);
    data_at = 8U + (uint64_t)xx_data_get_u32(table + 0x10, 4, 0, true);
    utf->columns = cpk_be16(table + 0x18);
    utf->width = cpk_be16(table + 0x1A);
    utf->rows = xx_data_get_u32(table + 0x1C, 4, 0, true);
    if (rows_at < CPK_UTF_HEAD || strings_at < rows_at || data_at < strings_at || data_at > declared || utf->columns == 0U || utf->columns > CPK_MAX_COLUMNS ||
        utf->rows > CPK_MAX_ROWS)
        return false;
    row_bytes = (uint64_t)utf->rows * utf->width;
    if (row_bytes > strings_at - rows_at) return false;
    utf->rows_at = (size_t)rows_at;
    utf->strings_at = (size_t)strings_at;
    utf->data_at = (size_t)data_at;
    at = CPK_UTF_HEAD;
    for (index = 0U; index < utf->columns; ++index) {
        cpk_column *column = &utf->column[index];
        const uint8_t *text;
        size_t length;
        uint8_t flags;
        if (at + 5U > utf->rows_at) return false;
        flags = table[at];
        column->storage = (uint8_t)(flags & 0xF0U);
        column->type = (uint8_t)(flags & 0x0FU);
        column->name = xx_data_get_u32(table + at + 1U, 4, 0, true);
        at += 5U;
        if (column->type > CPK_TYPE_DATA || !cpk_utf_string(utf, column->name, &text, &length)) return false;
        switch (column->storage) {
            case CPK_STORAGE_NONE:
            case CPK_STORAGE_ZERO: column->value = 0U; break;
            case CPK_STORAGE_CONST:
                if (cpk_type_size[column->type] > utf->rows_at - at) return false;
                column->value = (uint32_t)at;
                at += cpk_type_size[column->type];
                break;
            case CPK_STORAGE_ROW:
                column->value = width;
                width += cpk_type_size[column->type];
                break;
            default: return false;
        }
    }
    return width <= utf->width;
}

static int32_t cpk_utf_find(const cpk_utf *utf, const char *name)
{
    size_t wanted = xx_str_len(name);
    uint32_t index;
    for (index = 0U; index < utf->columns; ++index) {
        const uint8_t *text;
        size_t length;
        if (cpk_utf_string(utf, utf->column[index].name, &text, &length) && length == wanted && xx_rt_memcmp(text, name, wanted) == 0) return (int32_t)index;
    }
    return -1;
}

/* The bytes of one cell, or NULL for a zero / absent value. */
static const uint8_t *cpk_utf_cell(const cpk_utf *utf, uint32_t row, int32_t column_index)
{
    const cpk_column *column;
    if (column_index < 0 || (uint32_t)column_index >= utf->columns || row >= utf->rows) return NULL;
    column = &utf->column[column_index];
    if (column->storage == CPK_STORAGE_CONST) return utf->table + column->value;
    if (column->storage == CPK_STORAGE_ROW) return utf->table + utf->rows_at + (size_t)row * utf->width + column->value;
    return NULL;
}

/* An integer cell.  Absent columns and zero storage read as 0; negative
 * values, floats, strings and data are refused. */
static bool cpk_utf_int(const cpk_utf *utf, uint32_t row, int32_t column_index, int64_t *value)
{
    const uint8_t *cell;
    uint8_t type;
    *value = 0;
    if (column_index < 0) return true;
    type = utf->column[column_index].type;
    if (type >= CPK_TYPE_F32) return false;
    cell = cpk_utf_cell(utf, row, column_index);
    if (!cell) return true;
    switch (type) {
        case 0: *value = cell[0]; break;
        case 1: *value = (int8_t)cell[0]; break;
        case 2: *value = (int64_t)cpk_be16(cell); break;
        case 3: *value = (int16_t)cpk_be16(cell); break;
        case 4: *value = (int64_t)xx_data_get_u32(cell, 4, 0, true); break;
        case 5: *value = (int32_t)xx_data_get_u32(cell, 4, 0, true); break;
        default: {
            uint64_t v = ((uint64_t)xx_data_get_u32(cell, 4, 0, true) << 32U) | xx_data_get_u32(cell + 4, 4, 0, true);
            if (v > (uint64_t)INT64_MAX) return false;
            *value = (int64_t)v;
            break;
        }
    }
    return *value >= 0;
}

static bool cpk_utf_str(const cpk_utf *utf, uint32_t row, int32_t column_index, const uint8_t **text, size_t *length)
{
    const uint8_t *cell;
    *text = (const uint8_t *)"";
    *length = 0U;
    if (column_index < 0) return true;
    if (utf->column[column_index].type != CPK_TYPE_STRING) return false;
    cell = cpk_utf_cell(utf, row, column_index);
    if (!cell) return true;
    return cpk_utf_string(utf, xx_data_get_u32(cell, 4, 0, true), text, length);
}

static bool cpk_utf_data(const cpk_utf *utf, uint32_t row, int32_t column_index, const uint8_t **data, size_t *length)
{
    const uint8_t *cell;
    uint64_t at, size;
    *data = NULL;
    *length = 0U;
    if (column_index < 0) return true;
    if (utf->column[column_index].type != CPK_TYPE_DATA) return false;
    cell = cpk_utf_cell(utf, row, column_index);
    if (!cell) return true;
    at = (uint64_t)utf->data_at + xx_data_get_u32(cell, 4, 0, true);
    size = xx_data_get_u32(cell + 4, 4, 0, true);
    if (at > (uint64_t)utf->size || size > (uint64_t)utf->size - at) return false;
    *data = utf->table + (size_t)at;
    *length = (size_t)size;
    return true;
}

/* Loads the table of the packet at @p offset (relative).  The caller frees
 * *table. */
static bool cpk_load_packet(Abstractformat *format, int64_t available, int64_t offset, const char *signature, uint32_t cap, uint8_t **table, size_t *size,
                            int64_t *packet_size, bool *obfuscated)
{
    uint8_t head[CPK_PACKET_HEAD];
    uint64_t length;
    uint8_t *buffer;
    *table = NULL;
    if (offset < 0 || offset > available || (uint64_t)(available - offset) < CPK_PACKET_HEAD || !cpk_read_at(format, offset, head, sizeof(head)) ||
        xx_rt_memcmp(head, signature, 4U) != 0)
        return false;
    length = xx_data_get_u64(head + 8, 8, 0, false);
    if (length < CPK_UTF_HEAD || length > cap || length > (uint64_t)(available - offset) - CPK_PACKET_HEAD) return false;
    buffer = (uint8_t *)xx_mem_alloc((size_t)length);
    if (!buffer) return false;
    if (!cpk_read_at(format, offset + (int64_t)CPK_PACKET_HEAD, buffer, (size_t)length)) goto fail;
    if (xx_rt_memcmp(buffer, "@UTF", 4U) == 0) {
        if (obfuscated) *obfuscated = false;
    } else if (xx_rt_memcmp(buffer, cpk_utf_obfuscated, 4U) == 0) {
        cpk_deobfuscate(buffer, (size_t)length);
        if (obfuscated) *obfuscated = true;
    } else goto fail;
    *table = buffer;
    *size = (size_t)length;
    if (packet_size) *packet_size = (int64_t)length + (int64_t)CPK_PACKET_HEAD;
    return true;
fail:
    xx_mem_free(buffer);
    return false;
}

/* Checks the 16-byte head of a packet the header points at. */
static bool cpk_probe_packet(Abstractformat *format, int64_t available, int64_t offset, const char *signature, int64_t *packet_size)
{
    uint8_t head[CPK_PACKET_HEAD];
    uint64_t length;
    if (offset < (int64_t)CPK_PACKET_HEAD || offset > available || (uint64_t)(available - offset) < CPK_PACKET_HEAD || !cpk_read_at(format, offset, head, sizeof(head)) ||
        xx_rt_memcmp(head, signature, 4U) != 0)
        return false;
    length = xx_data_get_u64(head + 8, 8, 0, false);
    if (length < CPK_UTF_HEAD || length > CPK_MAX_INDEX_TABLE || length > (uint64_t)(available - offset) - CPK_PACKET_HEAD) return false;
    *packet_size = (int64_t)length + (int64_t)CPK_PACKET_HEAD;
    return true;
}

static bool cpk_read_header(Abstractformat *format, cpk_header *header)
{
    uint8_t *table = NULL;
    size_t size = 0U;
    int64_t packet = 0, value;
    cpk_utf *utf = NULL;
    bool ok = false;
    xx_mem_zero(header, sizeof(*header));
    header->available = cpk_available(format);
    if (header->available < (int64_t)(CPK_PACKET_HEAD + CPK_UTF_HEAD) ||
        !cpk_load_packet(format, header->available, 0, "CPK ", CPK_MAX_HEADER_TABLE, &table, &size, &packet, &header->encrypted))
        return false;
    utf = (cpk_utf *)xx_mem_alloc(sizeof(*utf));
    if (!utf || !cpk_utf_open(table, size, utf) || utf->rows == 0U) goto done;
    header->header_end = packet;
    header->content = header->content_size = -1;
    header->toc = header->itoc = -1;
    if (!cpk_utf_int(utf, 0, cpk_utf_find(utf, "ContentOffset"), &value)) goto done;
    if (value > 0) header->content = value;
    if (!cpk_utf_int(utf, 0, cpk_utf_find(utf, "ContentSize"), &value)) goto done;
    if (value > 0) header->content_size = value;
    if (!cpk_utf_int(utf, 0, cpk_utf_find(utf, "TocOffset"), &value)) goto done;
    if (value > 0) {
        if (!cpk_probe_packet(format, header->available, value, "TOC ", &header->toc_size)) goto done;
        header->toc = value;
    }
    if (!cpk_utf_int(utf, 0, cpk_utf_find(utf, "ItocOffset"), &value)) goto done;
    if (value > 0) {
        if (!cpk_probe_packet(format, header->available, value, "ITOC", &header->itoc_size)) goto done;
        header->itoc = value;
    }
    if (!cpk_utf_int(utf, 0, cpk_utf_find(utf, "Align"), &value) || value > 0x7fffffff) goto done;
    header->align = (uint32_t)value;
    /* A table of contents is what makes the file listable. */
    ok = header->toc > 0 || (header->itoc > 0 && header->content > 0);
done:
    if (utf) xx_mem_free(utf);
    xx_mem_free(table);
    return ok;
}

/* ---- member names ------------------------------------------------------ */

static bool cpk_valid_utf8(const uint8_t *s, size_t n)
{
    size_t i = 0U;
    while (i < n) {
        uint8_t c = s[i];
        size_t extra, k;
        uint32_t cp;
        if (c < 0x80U) {
            ++i;
            continue;
        }
        if (c >= 0xC2U && c <= 0xDFU) {
            extra = 1U;
            cp = c & 0x1FU;
        } else if (c >= 0xE0U && c <= 0xEFU) {
            extra = 2U;
            cp = c & 0x0FU;
        } else if (c >= 0xF0U && c <= 0xF4U) {
            extra = 3U;
            cp = c & 0x07U;
        } else return false;
        if (n - i <= extra) return false;
        for (k = 1U; k <= extra; ++k) {
            if ((s[i + k] & 0xC0U) != 0x80U) return false;
            cp = (cp << 6U) | (s[i + k] & 0x3FU);
        }
        if ((extra == 2U && (cp < 0x800U || (cp >= 0xD800U && cp <= 0xDFFFU))) || (extra == 3U && (cp < 0x10000U || cp > 0x10FFFFU))) return false;
        i += extra + 1U;
    }
    return true;
}

/* Appends @p n raw bytes; non-UTF-8 names (Shift-JIS) are %XX escaped. */
static size_t cpk_append(char *out, size_t at, const uint8_t *s, size_t n, bool escape)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t i;
    for (i = 0U; i < n; ++i) {
        uint8_t c = s[i];
        if (escape && (c >= 0x80U || c == (uint8_t)'%')) {
            out[at++] = '%';
            out[at++] = digits[c >> 4U];
            out[at++] = digits[c & 0x0FU];
        } else out[at++] = c == (uint8_t)'\\' ? '/' : (char)c;
    }
    out[at] = 0;
    return at;
}

/* Builds member @p index's name into stream->name (CPK_NAME_BUFFER). */
static bool cpk_build_name(cpk_stream *stream, size_t index, size_t *length)
{
    const cpk_member *member = &stream->items[index];
    size_t at = 0U;
    char *out = stream->name;
    out[0] = 0;
    if (stream->toc_mode) {
        const uint8_t *dir, *file;
        size_t dir_length, file_length;
        bool escape;
        if (!cpk_utf_str(stream->utf, member->row, stream->dir_column, &dir, &dir_length) ||
            !cpk_utf_str(stream->utf, member->row, stream->file_column, &file, &file_length))
            return false;
        escape = !cpk_valid_utf8(dir, dir_length) || !cpk_valid_utf8(file, file_length);
        if (file_length) {
            if (dir_length) {
                at = cpk_append(out, at, dir, dir_length, escape);
                out[at++] = '/';
            }
            at = cpk_append(out, at, file, file_length, escape);
        }
    }
    if (at == 0U) at = (size_t)xx_rt_snprintf(out, CPK_NAME_BUFFER, "%05u", (unsigned)member->row);
    *length = at;
    return true;
}

static uint64_t cpk_name_hash(const char *name, size_t length)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* "dir/name.ext" -> "dir/name%_<index>.ext". */
static void cpk_insert_suffix(char *name, size_t length, uint32_t index)
{
    char suffix[2 + 10];
    char digits[10];
    size_t suffix_length = 0U, digit_count = 0U, component = 0U, at, tail;
    size_t dot = length;
    for (at = 0U; at < length; ++at)
        if (name[at] == '/') component = at + 1U;
    for (at = length; at > component + 1U; --at)
        if (name[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    do {
        digits[digit_count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    } while (index != 0U && digit_count < sizeof(digits));
    suffix[suffix_length++] = '%';
    suffix[suffix_length++] = '_';
    while (digit_count != 0U) suffix[suffix_length++] = digits[--digit_count];
    if (length + suffix_length >= CPK_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at) name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

static bool cpk_reserved_component(const char *segment, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ') --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') || (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length && xx_rt_memcmp(stem, devices[index], stem_length) == 0) return true;
    return false;
}

/* Relative, '/'-separated, no empty / "." / ".." components, no drive
 * colons, control characters or Windows device names. */
static bool cpk_safe_name(const char *name)
{
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == '\\' || c == 0x7fU || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' || segment[length - 1U] == ' ' || cpk_reserved_component(segment, length)) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- index ------------------------------------------------------------- */

static void cpk_stream_free(void *opaque)
{
    cpk_stream *stream = (cpk_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->table) xx_mem_free(stream->table);
    if (stream->utf) xx_mem_free(stream->utf);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static int cpk_compare_keys(const void *left, const void *right)
{
    const cpk_key *a = (const cpk_key *)left;
    const cpk_key *b = (const cpk_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static bool cpk_read_toc(Abstractformat *format, const cpk_header *header, cpk_stream *stream, xx_pd_struct *pd)
{
    size_t size = 0U;
    uint32_t row;
    int32_t size_column, extract_column, offset_column;
    int64_t base;
    if (!cpk_load_packet(format, header->available, header->toc, "TOC ", CPK_MAX_INDEX_TABLE, &stream->table, &size, NULL, NULL) ||
        !cpk_utf_open(stream->table, size, stream->utf))
        return false;
    stream->toc_mode = true;
    stream->dir_column = cpk_utf_find(stream->utf, "DirName");
    stream->file_column = cpk_utf_find(stream->utf, "FileName");
    size_column = cpk_utf_find(stream->utf, "FileSize");
    extract_column = cpk_utf_find(stream->utf, "ExtractSize");
    offset_column = cpk_utf_find(stream->utf, "FileOffset");
    if (size_column < 0 || offset_column < 0) return false;
    base = header->content > 0 && header->content < header->toc ? header->content : header->toc;
    stream->count = stream->utf->rows;
    if (stream->count) {
        stream->items = (cpk_member *)xx_mem_calloc(stream->count, sizeof(cpk_member));
        if (!stream->items) return false;
    }
    for (row = 0U; row < stream->utf->rows; ++row) {
        cpk_member *member = &stream->items[row];
        int64_t offset, member_size, extract;
        if ((row & CPK_POLL_MASK) == 0U && cpk_stopped(pd)) return false;
        if (!cpk_utf_int(stream->utf, row, offset_column, &offset) || !cpk_utf_int(stream->utf, row, size_column, &member_size) ||
            !cpk_utf_int(stream->utf, row, extract_column, &extract))
            return false;
        member->row = row;
        member->size = member_size;
        member->extract = extract_column >= 0 ? extract : -1;
        member->offset = offset <= INT64_MAX - base ? base + offset : -1;
        member->in_range = member->offset >= 0 && member->offset <= header->available && member_size <= header->available - member->offset;
    }
    return true;
}

typedef struct cpk_itoc_entry_s {
    uint32_t id;
    uint32_t order;
    int64_t size;
    int64_t extract;
} cpk_itoc_entry;

static int cpk_compare_itoc(const void *left, const void *right)
{
    const cpk_itoc_entry *a = (const cpk_itoc_entry *)left;
    const cpk_itoc_entry *b = (const cpk_itoc_entry *)right;
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    return a->order < b->order ? -1 : (a->order > b->order ? 1 : 0);
}

static bool cpk_read_itoc(Abstractformat *format, const cpk_header *header, cpk_stream *stream, xx_pd_struct *pd)
{
    uint8_t *table = NULL;
    size_t size = 0U, total = 0U, index, part;
    cpk_utf *sub = NULL;
    cpk_itoc_entry *entries = NULL;
    const uint8_t *data[2];
    size_t data_length[2];
    int64_t position;
    bool ok = false;
    if (!cpk_load_packet(format, header->available, header->itoc, "ITOC", CPK_MAX_INDEX_TABLE, &table, &size, NULL, NULL) || !cpk_utf_open(table, size, stream->utf) ||
        stream->utf->rows == 0U || !cpk_utf_data(stream->utf, 0, cpk_utf_find(stream->utf, "DataL"), &data[0], &data_length[0]) ||
        !cpk_utf_data(stream->utf, 0, cpk_utf_find(stream->utf, "DataH"), &data[1], &data_length[1]))
        goto done;
    sub = (cpk_utf *)xx_mem_alloc(sizeof(*sub));
    if (!sub) goto done;
    /* Count first, then fill. */
    for (part = 0U; part < 2U; ++part) {
        if (!data_length[part]) continue;
        if (!cpk_utf_open(data[part], data_length[part], sub)) goto done;
        total += sub->rows;
    }
    if (total > CPK_MAX_ROWS) goto done;
    if (total) {
        entries = (cpk_itoc_entry *)xx_mem_calloc(total, sizeof(*entries));
        stream->items = (cpk_member *)xx_mem_calloc(total, sizeof(cpk_member));
        if (!entries || !stream->items) goto done;
    }
    index = 0U;
    for (part = 0U; part < 2U; ++part) {
        int32_t id_column, size_column, extract_column;
        uint32_t row;
        if (!data_length[part]) continue;
        if (!cpk_utf_open(data[part], data_length[part], sub)) goto done;
        id_column = cpk_utf_find(sub, "ID");
        size_column = cpk_utf_find(sub, "FileSize");
        extract_column = cpk_utf_find(sub, "ExtractSize");
        if (id_column < 0 || size_column < 0) goto done;
        for (row = 0U; row < sub->rows; ++row, ++index) {
            int64_t id, member_size, extract;
            if ((row & CPK_POLL_MASK) == 0U && cpk_stopped(pd)) goto done;
            if (!cpk_utf_int(sub, row, id_column, &id) || id > 0xffffffffLL || !cpk_utf_int(sub, row, size_column, &member_size) ||
                !cpk_utf_int(sub, row, extract_column, &extract))
                goto done;
            entries[index].id = (uint32_t)id;
            entries[index].order = (uint32_t)index;
            entries[index].size = member_size;
            entries[index].extract = extract_column >= 0 ? extract : -1;
        }
    }
    if (total > 1U) xx_rt_qsort(entries, total, sizeof(*entries), cpk_compare_itoc);
    position = header->content;
    for (index = 0U; index < total; ++index) {
        cpk_member *member = &stream->items[index];
        member->row = entries[index].id;
        member->size = entries[index].size;
        member->extract = entries[index].extract;
        member->offset = position;
        member->in_range = position >= 0 && position <= header->available && member->size <= header->available - position;
        if (position >= 0) {
            uint64_t next = (uint64_t)position + (uint64_t)member->size;
            if (header->align > 1U && next % header->align) next += header->align - next % header->align;
            position = next > (uint64_t)INT64_MAX ? -1 : (int64_t)next;
        }
    }
    stream->count = total;
    stream->toc_mode = false;
    ok = true;
done:
    if (sub) xx_mem_free(sub);
    if (entries) xx_mem_free(entries);
    xx_mem_free(table);
    return ok;
}

static bool cpk_open_stream(Abstractformat *format, cpk_stream **result, xx_pd_struct *pd)
{
    cpk_header header;
    cpk_stream *stream;
    cpk_key *keys = NULL;
    size_t index, length;
    int64_t end;
    if (!result || !cpk_read_header(format, &header)) return false;
    stream = (cpk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->utf = (cpk_utf *)xx_mem_alloc(sizeof(cpk_utf));
    stream->name = (char *)xx_mem_alloc(CPK_NAME_BUFFER);
    if (!stream->utf || !stream->name) goto fail;
    stream->dir_column = stream->file_column = -1;
    if (header.toc > 0 ? !cpk_read_toc(format, &header, stream, pd) : !cpk_read_itoc(format, &header, stream, pd)) goto fail;
    if (!stream->toc_mode) {
        /* ITOC names come from the IDs alone: the tables are not needed. */
        xx_mem_free(stream->utf);
        stream->utf = NULL;
    }
    end = header.header_end;
    if (header.toc > 0 && header.toc + header.toc_size > end) end = header.toc + header.toc_size;
    if (header.itoc > 0 && header.itoc + header.itoc_size > end) end = header.itoc + header.itoc_size;
    if (stream->count) {
        keys = (cpk_key *)xx_mem_alloc(stream->count * sizeof(*keys));
        if (!keys) goto fail;
    }
    for (index = 0U; index < stream->count; ++index) {
        cpk_member *member = &stream->items[index];
        if ((index & CPK_POLL_MASK) == 0U && cpk_stopped(pd)) goto fail;
        if (!cpk_build_name(stream, index, &length)) goto fail;
        member->hash = cpk_name_hash(stream->name, length);
        keys[index].hash = member->hash;
        keys[index].index = (uint32_t)index;
        if (member->in_range && member->offset + member->size > end) end = member->offset + member->size;
    }
    if (stream->count > 1U) {
        xx_rt_qsort(keys, stream->count, sizeof(*keys), cpk_compare_keys);
        for (index = 1U; index < stream->count; ++index)
            if (keys[index].hash == keys[index - 1U].hash) stream->items[keys[index].index].renamed = true;
    }
    if (keys) xx_mem_free(keys);
    if (header.content > 0 && header.content_size > 0 && header.content <= header.available && header.content_size <= header.available - header.content &&
        header.content + header.content_size > end)
        end = header.content + header.content_size;
    stream->format_size = end;
    *result = stream;
    return true;
fail:
    if (keys) xx_mem_free(keys);
    cpk_stream_free(stream);
    return false;
}

static bool cpk_load_name(cpk_stream *stream, size_t index)
{
    size_t length;
    if (!cpk_build_name(stream, index, &length)) return false;
    if (stream->items[index].renamed) cpk_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

/* ---- CRILAYLA ---------------------------------------------------------- */

typedef struct cpk_bits_s {
    const uint8_t *data;
    size_t low;      /**< First byte that may be read. */
    size_t position; /**< One past the next byte (reading backwards). */
    uint32_t accumulator;
    unsigned count;
} cpk_bits;

static bool cpk_bits_get(cpk_bits *bits, unsigned width, uint32_t *value)
{
    while (bits->count < width) {
        if (bits->position <= bits->low) return false;
        bits->accumulator = (bits->accumulator << 8U) | bits->data[--bits->position];
        bits->count += 8U;
    }
    bits->count -= width;
    *value = (bits->accumulator >> bits->count) & ((1U << width) - 1U);
    bits->accumulator &= (1U << bits->count) - 1U;
    return true;
}

bool xx_cpk_crilayla_decode(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size)
{
    static const unsigned widths[4] = {2U, 3U, 5U, 8U};
    uint32_t unpacked, packed;
    size_t prefix, produced = 0U, top;
    cpk_bits bits;
    if (!input || !output || input_size < 16U || xx_rt_memcmp(input, "CRILAYLA", 8U) != 0) return false;
    unpacked = xx_data_get_u32(input + 8, 4, 0, false);
    packed = xx_data_get_u32(input + 12, 4, 0, false);
    if (packed > input_size - 16U) return false;
    prefix = input_size - 16U - packed;
    if ((size_t)unpacked > SIZE_MAX - prefix || output_size != prefix + (size_t)unpacked) return false;
    bits.data = input;
    bits.low = 16U;
    bits.position = 16U + (size_t)packed;
    bits.accumulator = 0U;
    bits.count = 0U;
    /* The decoded bytes run back to front from the end of the output. */
    top = output_size;
    while (produced < unpacked) {
        uint32_t flag, value;
        if (!cpk_bits_get(&bits, 1U, &flag)) return false;
        if (!flag) {
            if (!cpk_bits_get(&bits, 8U, &value)) return false;
            output[top - 1U - produced] = (uint8_t)value;
            ++produced;
        } else {
            size_t distance, length = 3U, k, remaining = unpacked - produced;
            unsigned level = 0U;
            if (!cpk_bits_get(&bits, 13U, &value)) return false;
            distance = (size_t)value + 3U;
            if (distance > produced) return false;
            for (;;) {
                uint32_t step, all = (1U << widths[level]) - 1U;
                if (!cpk_bits_get(&bits, widths[level], &step)) return false;
                length += step;
                if (length > remaining) return false;
                if (step != all) break;
                if (level < 3U) ++level;
            }
            for (k = 0U; k < length; ++k) {
                size_t at = top - 1U - produced;
                output[at] = output[at + distance];
                ++produced;
            }
        }
    }
    if (prefix) xx_rt_memcpy(output, input + 16U + packed, prefix);
    return true;
}

/* Reads the CRILAYLA header of a member: true with the output size when
 * the member is compressed the way GARbro decides it (size >= 16, magic,
 * unpacked < 2^31, packed within the member). */
static bool cpk_member_packed(Abstractformat *format, const cpk_member *member, uint64_t *output)
{
    uint8_t head[16];
    uint32_t unpacked, packed;
    if (!member->in_range || member->size < 16 || !cpk_read_at(format, member->offset, head, sizeof(head)) || xx_rt_memcmp(head, "CRILAYLA", 8U) != 0) return false;
    unpacked = xx_data_get_u32(head + 8, 4, 0, false);
    packed = xx_data_get_u32(head + 12, 4, 0, false);
    if (unpacked > 0x7fffffffU || (int64_t)packed > member->size - 16) return false;
    *output = (uint64_t)(member->size - 16 - (int64_t)packed) + unpacked;
    return true;
}

static bool cpk_unpack_member(Abstractformat *format, const cpk_member *member, xx_io_device *destination, xx_pd_struct *pd)
{
    uint64_t output_size;
    uint8_t *input = NULL, *output = NULL;
    bool ok = false;
    if (!member->in_range) return false;
    if (!cpk_member_packed(format, member, &output_size)) return cpk_copy_range(format, member->offset, member->size, destination, pd);
    /* One input byte yields at most 255 output bytes (an all-ones 8-bit
     * length field), so a larger claim is refused before allocating. */
    if (member->size > (int64_t)CPK_MAX_DECODE || output_size > (uint64_t)CPK_MAX_DECODE || output_size > (uint64_t)member->size * 256U + 0x1000U) return false;
    input = (uint8_t *)xx_mem_alloc((size_t)member->size);
    output = (uint8_t *)xx_mem_alloc(output_size ? (size_t)output_size : 1U);
    if (!input || !output || cpk_stopped(pd) || !cpk_read_at(format, member->offset, input, (size_t)member->size) ||
        !xx_cpk_crilayla_decode(input, (size_t)member->size, output, (size_t)output_size) || cpk_stopped(pd))
        goto done;
    ok = cpk_write_all(destination, output, (size_t)output_size);
done:
    if (input) xx_mem_free(input);
    if (output) xx_mem_free(output);
    return ok;
}

/* ---- records ----------------------------------------------------------- */

static bool cpk_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *cpk_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool cpk_set_record(Abstractformat *format, xx_archive_record *record, cpk_stream *stream, size_t index)
{
    const cpk_member *member = &stream->items[index];
    uint64_t output = 0U;
    bool packed = cpk_member_packed(format, member, &output);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!cpk_load_name(stream, index)) return false;
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, stream->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, packed ? output : (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, packed ? CPK_METHOD_CRILAYLA : CPK_METHOD_STORED) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_cpk_init(xx_cpk *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_CPK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "cpk");
    archive->format.check_is_valid = xx_cpk_check_is_valid;
    archive->format.handle_base_info = xx_cpk_handle_base_info;
    archive->format.get_format_size = xx_cpk_get_format_size;
    archive->format.get_number_of_archive_records = xx_cpk_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_cpk_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_cpk_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_cpk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_cpk_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_cpk_free_archive_records_reading;
    archive->toc_offset = archive->itoc_offset = archive->content_offset = -1;
}

xx_cpk *xx_cpk_create(xx_io_device *device, int64_t base_address)
{
    xx_cpk *archive = (xx_cpk *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_cpk_init(archive, device, base_address);
    return archive;
}

void xx_cpk_destroy(xx_cpk *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_cpk_free(xx_cpk *archive)
{
    if (!archive) return;
    xx_cpk_destroy(archive);
    xx_mem_free(archive);
}

bool xx_cpk_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    cpk_header header;
    (void)pd;
    return cpk_read_header(format, &header);
}

bool xx_cpk_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    cpk_header header;
    cpk_stream *stream;
    xx_cpk *archive;
    if (!format || !cpk_read_header(format, &header) || !cpk_open_stream(format, &stream, pd)) return false;
    archive = (xx_cpk *)format;
    archive->number_of_records = stream->count;
    archive->toc_offset = header.toc > 0 ? format->base_address + header.toc : -1;
    archive->itoc_offset = header.itoc > 0 ? format->base_address + header.itoc : -1;
    archive->content_offset = header.content > 0 ? format->base_address + header.content : -1;
    archive->encrypted = header.encrypted;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    cpk_stream_free(stream);
    return true;
}

int64_t xx_cpk_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_cpk_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_cpk_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_cpk_handle_base_info(format, pd)) ? ((xx_cpk *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_cpk_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    cpk_stream *stream;
    xx_archive_record_state *state;
    if (!cpk_open_stream(format, &stream, pd)) return NULL;
    if (stream->count == 0U) {
        cpk_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        cpk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = cpk_stream_free;
    state->total_records = stream->count;
    if (!cpk_copy_options(&state->options, options) || !cpk_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_cpk_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_cpk_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    cpk_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (cpk_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = cpk_set_record(format, &state->current_record, stream, stream->index);
    return state->has_record;
}

bool xx_cpk_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    cpk_stream *stream;
    const cpk_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (cpk_stream *)state->internal_state) || stream->index >= stream->count ||
        cpk_stopped(pd))
        return false;
    member = &stream->items[stream->index];
    path_option = cpk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) /* No destination: decode into nothing, which verifies the member. */
        return cpk_unpack_member(format, member, NULL, pd);
    if (!cpk_safe_name(stream->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", stream->name)
                                                                                                  : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = cpk_unpack_member(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_cpk_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
