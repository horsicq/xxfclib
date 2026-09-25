/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * O'Setup95 / O'Setup for Windows self-installing package.  xx_o_setup.h
 * carries the field table and the member-name rules.
 *
 * Written from the format's structure as measured on real packages.  U3's
 * "SFX OSetup" handler (record walk from the trailer's count, 44-byte
 * headers, Unix times) was read for understanding only; no code is taken
 * from it.
 *
 * The executable itself is not parsed: the trailer at the end of the file
 * names the first record, and the record chain - which has to end exactly at
 * the trailer - is the validator.  The probe reads the last 14 bytes of an
 * MZ file and stops there unless they are an O'Setup trailer.  SZDD members
 * are expanded with the shared Microsoft LZSS decoder.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/o_setup/xx_o_setup.h"

#include "xxfclib/algo/mscompress/xx_mscompress.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef O_SETUP
#define XX_O_SETUP_FILE_TYPE XX_FILE_TYPE_O_SETUP
#else
#define XX_O_SETUP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define OS_TRAILER_LONG 14
#define OS_TRAILER_SHORT 12
#define OS_RECORD_HEADER 44
#define OS_SIZE_OFFSET 4
#define OS_TIME_OFFSET 8
#define OS_NAME_OFFSET 12
#define OS_NAME_FIELD 13
/* The stub is at least an MZ header, so no record starts before it. */
#define OS_MIN_FIRST_RECORD 0x40

/* SZDD member: 8-byte magic, mode 'A', missing name character, u32 size. */
#define OS_SZDD_HEADER 14U
#define OS_SZDD_BIAS 16U
/* One flag byte and eight 2-byte matches (17 input bytes) give at most
 * 8 * 18 = 144 output bytes, so an honest stream never expands more than
 * 9 times over, plus one group.  A larger claimed size cannot decode. */
#define OS_SZDD_RATIO 9U
#define OS_SZDD_SLACK 144U
/* A member is decoded in memory; these caps only refuse fields that are
 * plainly not from a 1990s installer. */
#define OS_SZDD_MAX_INPUT (UINT64_C(128) * 1024U * 1024U)
#define OS_SZDD_MAX_OUTPUT (UINT64_C(512) * 1024U * 1024U)

/* A converted name: "%XX" per raw byte (at most 12 before the NUL), then
 * "%_", up to five digits of record index and the terminator. */
#define OS_NAME_BUFFER (3 * (OS_NAME_FIELD - 1) + 2 + 5 + 1)
#define OS_COPY_CHUNK 65536U
#define OS_POLL_MASK 0x3FFU

typedef struct os_layout_s {
    int64_t total;   /**< Device size. */
    int64_t first;   /**< Absolute offset of the first record. */
    int64_t trailer; /**< Absolute offset of the trailer. */
    uint32_t count;
    uint32_t variant;
} os_layout;

typedef struct os_item_s {
    int64_t header; /**< Absolute offset of the record header. */
    uint32_t size;  /**< Stored bytes after the header. */
    uint32_t mtime;
    uint32_t unpacked;
    uint32_t method;
    uint32_t index;
    bool duplicate; /**< An earlier record has the same name. */
    char name[OS_NAME_BUFFER];
} os_item;

typedef struct os_stream_s {
    os_layout layout;
    os_item *items;
    size_t count;
    size_t index; /**< Current record. */
} os_stream;

/* A write-only device that counts, caps and forwards (or discards). */
typedef struct os_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t limit;
    uint64_t written;
} os_sink;

static uint32_t os_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t os_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool os_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ---- output sink ------------------------------------------------------- */

static ssize_t os_sink_write(xx_io_device *self, const void *buffer,
                             size_t n) {
    os_sink *sink = self ? (os_sink *)self->priv : NULL;
    size_t done = 0U;
    if (!sink || (!buffer && n != 0U)) return -1;
    if ((uint64_t)n > sink->limit - sink->written) return -1;
    while (sink->target && done < n) {
        ssize_t amount = xx_io_write(sink->target,
                                     (const uint8_t *)buffer + done, n - done);
        if (amount <= 0 || (size_t)amount > n - done) return -1;
        done += (size_t)amount;
    }
    sink->written += (uint64_t)n;
    return (ssize_t)n;
}

static void os_sink_init(os_sink *sink, xx_io_device *target,
                         uint64_t limit) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->device.write = os_sink_write;
    sink->device.priv = sink;
    sink->target = target;
    sink->limit = limit;
}

/* Stream `size` stored bytes at `offset` into the sink in fixed chunks. */
static bool os_copy_range(xx_io_device *source, int64_t offset, int64_t size,
                          os_sink *sink, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(OS_COPY_CHUNK);
    if (!buffer) return false;
    while (ok && done < size) {
        size_t chunk = size - done > (int64_t)OS_COPY_CHUNK
                           ? (size_t)OS_COPY_CHUNK
                           : (size_t)(size - done);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !os_read_at(source, offset + done, buffer, chunk) ||
            os_sink_write(&sink->device, buffer, chunk) != (ssize_t)chunk)
            ok = false;
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* ---- member names ------------------------------------------------------ */

static bool os_escaped(uint8_t c) {
    return c < 0x21U || c > 0x7EU || c == (uint8_t)'%' || c == (uint8_t)'/' ||
           c == (uint8_t)'\\' || c == (uint8_t)':' || c == (uint8_t)'*' ||
           c == (uint8_t)'?' || c == (uint8_t)'"' || c == (uint8_t)'<' ||
           c == (uint8_t)'>' || c == (uint8_t)'|';
}

/* Raw name -> one ASCII path component (see the header).  `out` holds
 * OS_NAME_BUFFER bytes; returns the converted length. */
static size_t os_convert_name(const uint8_t *raw, size_t length, char *out) {
    static const char digits[] = "0123456789ABCDEF";
    size_t at = 0U, index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (os_escaped(c)) {
            out[at++] = '%';
            out[at++] = digits[(c >> 4U) & 0x0FU];
            out[at++] = digits[c & 0x0FU];
        } else {
            out[at++] = (char)c;
        }
    }
    out[at] = 0;
    return at;
}

static int os_compare_folded(const char *left, const char *right) {
    for (;; ++left, ++right) {
        uint8_t a = (uint8_t)*left, b = (uint8_t)*right;
        if (a >= (uint8_t)'A' && a <= (uint8_t)'Z')
            a = (uint8_t)(a - (uint8_t)'A' + (uint8_t)'a');
        if (b >= (uint8_t)'A' && b <= (uint8_t)'Z')
            b = (uint8_t)(b - (uint8_t)'A' + (uint8_t)'a');
        if (a != b) return a < b ? -1 : 1;
        if (a == 0U) return 0;
    }
}

static int os_compare_items(const void *left, const void *right) {
    const os_item *a = *(const os_item *const *)left;
    const os_item *b = *(const os_item *const *)right;
    int order = os_compare_folded(a->name, b->name);
    if (order != 0) return order;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Insert "%_<index>" before the extension (or append it when there is
 * none).  `name` has room for OS_NAME_BUFFER bytes. */
static void os_insert_suffix(char *name, uint32_t index) {
    char suffix[2 + 10];
    char digits[10];
    size_t length = xx_str_len(name);
    size_t suffix_length = 0U, digit_count = 0U, at, tail;
    size_t dot = length;
    for (at = length; at > 1U; --at)
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
    if (length + suffix_length >= OS_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* The first of a group of equal names (ASCII case folded) keeps it; every
 * later one is suffixed with its record index, which is unique and cannot
 * meet another name because "%_" never occurs in a converted one. */
static bool os_mark_duplicates(os_item *items, size_t count) {
    os_item **order;
    size_t index;
    if (count < 2U) return true;
    order = (os_item **)xx_mem_alloc(count * sizeof(*order));
    if (!order) return false;
    for (index = 0U; index < count; ++index) order[index] = &items[index];
    xx_rt_qsort(order, count, sizeof(*order), os_compare_items);
    /* Mark first, rename afterwards: renaming changes the sort keys. */
    for (index = count - 1U; index > 0U; --index)
        if (os_compare_folded(order[index]->name, order[index - 1U]->name) == 0)
            order[index]->duplicate = true;
    for (index = 0U; index < count; ++index)
        if (items[index].duplicate)
            os_insert_suffix(items[index].name, items[index].index);
    xx_mem_free(order);
    return true;
}

/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$, CONIN$,
 * CONOUT$) as the part of the name before its first '.'. */
static bool os_reserved_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",    "AUX",
                                          "NUL",    "CLOCK$", "CONIN$",
                                          "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (name[stem_length] != 0 && name[stem_length] != '.') ++stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = name[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') ||
         (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length &&
            xx_rt_memcmp(stem, devices[index], stem_length) == 0)
            return true;
    return false;
}

/* A converted name is one component with no separator, control or
 * forbidden character left in it; what remains to refuse is the empty name,
 * a name ending in '.' (this covers "." and "..") and device names. */
static bool os_safe_name(const char *name) {
    size_t length;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (name[length - 1U] == '.' || name[length - 1U] == ' ') return false;
    return !os_reserved_name(name);
}

/* ---- structure walk ---------------------------------------------------- */

static bool os_read_trailer(Abstractformat *format, os_layout *out) {
    uint8_t tail[OS_TRAILER_LONG];
    uint8_t mz[2];
    os_layout layout;
    int64_t base, first, room;
    uint32_t size;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    base = format->base_address;
    xx_mem_zero(&layout, sizeof(layout));
    layout.total = xx_io_total_size(format->device);
    if (layout.total < base ||
        layout.total - base < (int64_t)(OS_MIN_FIRST_RECORD +
                                        OS_RECORD_HEADER + OS_TRAILER_SHORT) ||
        !os_read_at(format->device, layout.total - OS_TRAILER_LONG, tail,
                    sizeof(tail)))
        return false;
    if (xx_rt_memcmp(tail, "OSETUP", 6U) == 0 &&
        (tail[6] == (uint8_t)'S' || tail[6] == (uint8_t)'A') && tail[7] == 0U) {
        size = OS_TRAILER_LONG;
        layout.variant = tail[6] == (uint8_t)'S' ? XX_O_SETUP_VARIANT_SETUP
                                                 : XX_O_SETUP_VARIANT_ALL;
    } else if (xx_rt_memcmp(tail + 2, "OSETUP", 6U) == 0) {
        size = OS_TRAILER_SHORT;
        layout.variant = XX_O_SETUP_VARIANT_OLD;
    } else {
        return false;
    }
    layout.count = os_le16(tail + 12);
    first = (int64_t)os_le32(tail + 8);
    layout.trailer = layout.total - (int64_t)size;
    if (layout.count == 0U || first < OS_MIN_FIRST_RECORD ||
        first > layout.trailer - base)
        return false;
    layout.first = base + first;
    room = layout.trailer - layout.first;
    if ((int64_t)layout.count > room / OS_RECORD_HEADER) return false;
    /* The package is appended to an executable. */
    if (!os_read_at(format->device, base, mz, sizeof(mz)) ||
        mz[0] != (uint8_t)'M' || mz[1] != (uint8_t)'Z')
        return false;
    *out = layout;
    return true;
}

/* The fields of one record header.  False when it is not a record. */
static bool os_parse_record(const uint8_t *header, uint32_t *size,
                            uint32_t *mtime, size_t *name_length) {
    size_t length = 0U;
    if (header[0] != (uint8_t)'F' || header[1] != (uint8_t)'I' ||
        header[2] != (uint8_t)'L' || header[3] != (uint8_t)'E')
        return false;
    while (length < OS_NAME_FIELD && header[OS_NAME_OFFSET + length] != 0U)
        ++length;
    /* The writer terminates the name inside the field. */
    if (length == 0U || length >= OS_NAME_FIELD) return false;
    *size = os_le32(header + OS_SIZE_OFFSET);
    *mtime = os_le32(header + OS_TIME_OFFSET);
    *name_length = length;
    return true;
}

/* Read the head of a member's data and tell SZDD from stored. */
static bool os_classify(xx_io_device *device, os_item *item) {
    uint8_t head[OS_SZDD_HEADER];
    static const uint8_t magic[8] = {'S', 'Z', 'D', 'D',
                                     0x88U, 0xF0U, 0x27U, 0x33U};
    item->method = XX_O_SETUP_METHOD_STORED;
    item->unpacked = item->size;
    if (item->size < OS_SZDD_HEADER) return true;
    if (!os_read_at(device, item->header + OS_RECORD_HEADER, head,
                    sizeof(head)))
        return false;
    if (xx_rt_memcmp(head, magic, sizeof(magic)) == 0 &&
        head[8] == (uint8_t)'A') {
        item->method = XX_O_SETUP_METHOD_SZDD;
        item->unpacked = os_le32(head + 10);
    }
    return true;
}

/* Walk every record from the first one; the chain must end exactly at the
 * trailer.  With `items` NULL this is the probe and keeps nothing. */
static bool os_walk(Abstractformat *format, const os_layout *layout,
                    os_item *items, xx_pd_struct *pd) {
    uint8_t header[OS_RECORD_HEADER];
    int64_t cursor = layout->first;
    uint32_t index;
    for (index = 0U; index < layout->count; ++index) {
        uint32_t size, mtime;
        size_t name_length;
        int64_t data;
        if ((index & OS_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        if ((int64_t)OS_RECORD_HEADER > layout->trailer - cursor ||
            !os_read_at(format->device, cursor, header, sizeof(header)) ||
            !os_parse_record(header, &size, &mtime, &name_length))
            return false;
        data = cursor + OS_RECORD_HEADER;
        if ((int64_t)size > layout->trailer - data) return false;
        if (items) {
            os_item *item = &items[index];
            item->header = cursor;
            item->size = size;
            item->mtime = mtime;
            item->index = index;
            (void)os_convert_name(header + OS_NAME_OFFSET, name_length,
                                  item->name);
            if (!os_classify(format->device, item)) return false;
        }
        cursor = data + (int64_t)size;
    }
    return cursor == layout->trailer;
}

static bool os_scan(Abstractformat *format, os_layout *layout,
                    xx_pd_struct *pd) {
    return os_read_trailer(format, layout) &&
           os_walk(format, layout, NULL, pd);
}

static void os_stream_free(void *opaque) {
    os_stream *stream = (os_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool os_open_stream(Abstractformat *format, os_stream **result,
                           xx_pd_struct *pd) {
    os_layout layout;
    os_stream *stream;
    if (!result || !os_scan(format, &layout, pd)) return false;
    stream = (os_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    /* At most 65535 records, each proven by a 44-byte header in the file:
     * the bookkeeping never outgrows the package. */
    stream->items = (os_item *)xx_mem_calloc(layout.count,
                                             sizeof(*stream->items));
    if (!stream->items || !os_walk(format, &layout, stream->items, pd) ||
        !os_mark_duplicates(stream->items, layout.count)) {
        os_stream_free(stream);
        return false;
    }
    stream->layout = layout;
    stream->count = layout.count;
    *result = stream;
    return true;
}

static bool os_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool os_set_record(xx_archive_record *record, const os_item *item) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = item->header;
    record->header_size = OS_RECORD_HEADER;
    record->data_offset = item->header + OS_RECORD_HEADER;
    record->compressed_size = (int64_t)item->size;
    return xx_archive_record_set_original_name(record, item->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          item->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          item->unpacked) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          item->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          item->mtime) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Expand one SZDD member in memory.  The claimed size is checked against
 * what the stored bytes could possibly decode to before anything is
 * allocated, so the output buffer is bounded by the file. */
static bool os_extract_szdd(xx_io_device *device, const os_item *item,
                            os_sink *sink, xx_pd_struct *pd) {
    uint64_t input_size = (uint64_t)item->size - OS_SZDD_HEADER;
    uint64_t output_size = item->unpacked;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t consumed = 0U;
    bool ok = false;
    if (item->size < OS_SZDD_HEADER || input_size > OS_SZDD_MAX_INPUT ||
        output_size > OS_SZDD_MAX_OUTPUT ||
        output_size > input_size * OS_SZDD_RATIO + OS_SZDD_SLACK ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    input = (uint8_t *)xx_mem_alloc(input_size == 0U ? 1U : (size_t)input_size);
    output = (uint8_t *)xx_mem_alloc(output_size == 0U ? 1U
                                                       : (size_t)output_size);
    if (input && output &&
        os_read_at(device,
                   item->header + OS_RECORD_HEADER + (int64_t)OS_SZDD_HEADER,
                   input, (size_t)input_size) &&
        xx_mscompress_lzss_decode(input, (size_t)input_size, output,
                                  (size_t)output_size, OS_SZDD_BIAS,
                                  &consumed) &&
        (pd == NULL || !xx_pd_is_stopped(pd)) &&
        os_sink_write(&sink->device, output, (size_t)output_size) ==
            (ssize_t)output_size)
        ok = true;
    if (output) xx_mem_free(output);
    if (input) xx_mem_free(input);
    return ok;
}

/* Decode one member into `destination` (NULL only verifies). */
static bool os_extract(Abstractformat *format, const os_item *item,
                       xx_io_device *destination, xx_pd_struct *pd) {
    os_sink sink;
    os_sink_init(&sink, destination, item->unpacked);
    if (item->method == XX_O_SETUP_METHOD_SZDD)
        return os_extract_szdd(format->device, item, &sink, pd) &&
               sink.written == item->unpacked;
    return os_copy_range(format->device, item->header + OS_RECORD_HEADER,
                         (int64_t)item->size, &sink, pd) &&
           sink.written == item->size;
}

/* ---- public API -------------------------------------------------------- */

void xx_o_setup_init(xx_o_setup *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_O_SETUP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdownload");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_o_setup_check_is_valid;
    archive->format.handle_base_info = xx_o_setup_handle_base_info;
    archive->format.get_format_size = xx_o_setup_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_o_setup_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_o_setup_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_o_setup_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_o_setup_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_o_setup_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_o_setup_free_archive_records_reading;
}

xx_o_setup *xx_o_setup_create(xx_io_device *device, int64_t base_address) {
    xx_o_setup *archive = (xx_o_setup *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_o_setup_init(archive, device, base_address);
    return archive;
}

void xx_o_setup_destroy(xx_o_setup *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_o_setup_free(xx_o_setup *archive) {
    if (!archive) return;
    xx_o_setup_destroy(archive);
    xx_mem_free(archive);
}

bool xx_o_setup_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    os_layout layout;
    return os_scan(format, &layout, pd);
}

bool xx_o_setup_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    os_layout layout;
    xx_o_setup *archive;
    if (!os_scan(format, &layout, pd)) return false;
    archive = (xx_o_setup *)format;
    archive->number_of_records = layout.count;
    archive->first_record = layout.first;
    archive->trailer_offset = layout.trailer;
    archive->variant = layout.variant;
    format->number_of_archive_records = layout.count;
    /* The whole executable: stub, records and trailer. */
    format->format_size = layout.total - format->base_address;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_o_setup_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_o_setup_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_o_setup_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_o_setup_handle_base_info(format, pd))
               ? ((xx_o_setup *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_o_setup_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    os_stream *stream;
    xx_archive_record_state *state;
    if (!os_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        os_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = os_stream_free;
    state->total_records = (uint64_t)stream->count;
    if (!os_copy_options(&state->options, options) ||
        !os_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_o_setup_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_o_setup_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    os_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (os_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        os_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_o_setup_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    os_stream *stream;
    const os_item *item;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (os_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    item = &stream->items[stream->index];
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)item->unpacked > xx_var_get_u64(option))
        return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option)
        /* No destination: decode the member through, which verifies it. */
        return os_extract(format, item, NULL, pd);
    /* item->name came from the file: refuse it before anything is created
     * when it names a device or is not a usable file name. */
    if (!os_safe_name(item->name)) return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", item->name)
               : xx_str_concat(base, item->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = os_extract(format, item, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_o_setup_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
