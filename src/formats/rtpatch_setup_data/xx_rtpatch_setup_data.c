/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RTPatch Setup data volumes (DISK1.001, DISK2.001, ...): the member chain
 * the Pocket Soft RTPatch distribution setup expands.  xx_rtpatch_setup_data.h
 * carries the field table.
 *
 * Written from the format's structure as measured on real volumes.  U3's
 * "RTPatch Setup" handler (16-byte record header, name length 2..13 ending
 * in NUL, the 0xB59C stream magic behind the name) was read for
 * understanding only; no code is taken from it.  Members are expanded with
 * the shared RTPatch codec (xx_rtpatch_decode_memory).  The member-name
 * conversion and the duplicate suffix follow xx_o_setup.c of this library
 * (MIT, same author) so that DOS installer readers name members alike.
 *
 * The volume has no header, so a record is only accepted when every field
 * the setup engine and the codec depend on holds: the name length and its
 * NUL, printable name bytes, DOS attribute bits only, both sizes
 * non-negative as int32, a packed size that holds at least the codec header,
 * an unpacked size the packed bytes could possibly decode to, and the
 * codec's own 8-byte stream header.  The walk reads at most 37 bytes per
 * record and never allocates while probing.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rtpatch_setup_data/xx_rtpatch_setup_data.h"

#include "xxfclib/algo/rtpatch/xx_rtpatch.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef RTPATCH_SETUP_DATA
#define XX_RTPATCH_SETUP_DATA_FILE_TYPE XX_FILE_TYPE_RTPATCH_SETUP_DATA
#else
#define XX_RTPATCH_SETUP_DATA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RSD_HEADER 16
#define RSD_NAME_LENGTH_OFFSET 14
#define RSD_NAME_MIN 2U
#define RSD_NAME_MAX 13U
/* The codec header: magic, raw-literal flag, 0xFF, the two 12-bit rescale
 * periods and the window selector (60 bits, padded to 8 bytes). */
#define RSD_STREAM_HEADER 8
#define RSD_WINDOW (RSD_HEADER + RSD_NAME_MAX + RSD_STREAM_HEADER)
#define RSD_MIN_RECORD (RSD_HEADER + RSD_NAME_MIN + RSD_STREAM_HEADER)
/* DOS read-only, hidden, system and archive. */
#define RSD_ATTRIBUTE_MASK 0x27U
/* A match costs at least its flag bit and six distance bits and yields at
 * most 127 bytes, so a stream never expands more than 146 times over; the
 * bound below leaves ample room and still refuses a size that no packed
 * stream of that length could produce. */
#define RSD_RATIO 256
#define RSD_RATIO_SLACK 4096
/* Runaway guard, not a format limit: a 1.44 MB disk holds at most about
 * 55,000 of the smallest possible records. */
#define RSD_MAX_RECORDS 65536U
/* Members are decoded in memory; these caps only refuse fields that are
 * plainly not from a floppy-disk distribution. */
#define RSD_MAX_PACKED (INT64_C(256) * 1024 * 1024)
#define RSD_MAX_UNPACKED (INT64_C(256) * 1024 * 1024)
/* A converted name: "%XX" per raw byte (at most 12 before the NUL), then
 * "%_", up to five digits of record index and the terminator. */
#define RSD_NAME_BUFFER (3 * (RSD_NAME_MAX - 1) + 2 + 5 + 1)
#define RSD_POLL_MASK 0x3FFU

typedef struct rsd_item_s {
    int64_t header;   /**< Absolute offset of the record header. */
    int64_t data;     /**< Absolute offset of the stream. */
    int64_t packed;   /**< Declared stream length. */
    int64_t present;  /**< Stream bytes in this volume. */
    int64_t unpacked;
    uint32_t attributes;
    uint32_t dos_date;
    uint32_t dos_time;
    uint32_t header_size; /**< 16 + the name length. */
    uint32_t index;
    bool split;     /**< The stream continues on another volume. */
    bool duplicate; /**< An earlier record has the same name. */
    char name[RSD_NAME_BUFFER];
} rsd_item;

typedef struct rsd_layout_s {
    int64_t span;      /**< Bytes from the base to the end of the device. */
    int64_t chain_end; /**< End of the chain, from the base. */
    uint32_t count;
    bool split;
} rsd_layout;

typedef struct rsd_stream_s {
    rsd_layout layout;
    rsd_item *items;
    size_t count;
    size_t index; /**< Current record. */
} rsd_stream;

static uint32_t rsd_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t rsd_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool rsd_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool rsd_stopped(xx_pd_struct *pd) {
    return pd != NULL && xx_pd_is_stopped(pd);
}

/* ---- member names ------------------------------------------------------ */

static bool rsd_escaped(uint8_t c) {
    return c < 0x21U || c > 0x7EU || c == (uint8_t)'%' || c == (uint8_t)'/' ||
           c == (uint8_t)'\\' || c == (uint8_t)':' || c == (uint8_t)'*' ||
           c == (uint8_t)'?' || c == (uint8_t)'"' || c == (uint8_t)'<' ||
           c == (uint8_t)'>' || c == (uint8_t)'|';
}

/* Raw name (without its NUL) -> one ASCII path component.  `out` holds
 * RSD_NAME_BUFFER bytes. */
static void rsd_convert_name(const uint8_t *raw, size_t length, char *out) {
    static const char digits[] = "0123456789ABCDEF";
    size_t at = 0U, index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (rsd_escaped(c)) {
            out[at++] = '%';
            out[at++] = digits[(c >> 4U) & 0x0FU];
            out[at++] = digits[c & 0x0FU];
        } else {
            out[at++] = (char)c;
        }
    }
    out[at] = 0;
}

static int rsd_compare_folded(const char *left, const char *right) {
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

static int rsd_compare_items(const void *left, const void *right) {
    const rsd_item *a = *(const rsd_item *const *)left;
    const rsd_item *b = *(const rsd_item *const *)right;
    int order = rsd_compare_folded(a->name, b->name);
    if (order != 0) return order;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Insert "%_<index>" before the extension (or append it when there is
 * none).  `name` has room for RSD_NAME_BUFFER bytes. */
static void rsd_insert_suffix(char *name, uint32_t index) {
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
    if (length + suffix_length >= RSD_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* The first of a group of equal names (ASCII case folded) keeps it; every
 * later one is suffixed with its record index, which is unique and cannot
 * meet another name because "%_" never occurs in a converted one. */
static bool rsd_mark_duplicates(rsd_item *items, size_t count) {
    rsd_item **order;
    size_t index;
    if (count < 2U) return true;
    order = (rsd_item **)xx_mem_alloc(count * sizeof(*order));
    if (!order) return false;
    for (index = 0U; index < count; ++index) order[index] = &items[index];
    xx_rt_qsort(order, count, sizeof(*order), rsd_compare_items);
    /* Mark first, rename afterwards: renaming changes the sort keys. */
    for (index = count - 1U; index > 0U; --index)
        if (rsd_compare_folded(order[index]->name,
                               order[index - 1U]->name) == 0)
            order[index]->duplicate = true;
    for (index = 0U; index < count; ++index)
        if (items[index].duplicate)
            rsd_insert_suffix(items[index].name, items[index].index);
    xx_mem_free(order);
    return true;
}

/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$, CONIN$,
 * CONOUT$) as the part of the name before its first '.'. */
static bool rsd_reserved_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",    "AUX",
                                          "NUL",    "CLOCK$", "CONIN$",
                                          "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (name[stem_length] != 0 && name[stem_length] != '.') ++stem_length;
    while (stem_length > 0U && name[stem_length - 1U] == ' ') --stem_length;
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
 * a name ending in '.' (this covers "." and "..") or ' ', and device
 * names. */
static bool rsd_safe_name(const char *name) {
    size_t length;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (name[length - 1U] == '.' || name[length - 1U] == ' ') return false;
    return !rsd_reserved_name(name);
}

/* ---- structure walk ---------------------------------------------------- */

/* One record header in `window` (`available` bytes of the volume from the
 * record's start, at most RSD_WINDOW).  False when it is not a record. */
static bool rsd_parse_record(const uint8_t *window, size_t available,
                             rsd_item *out) {
    const uint8_t *stream;
    uint32_t name_length, attributes, initial_period, update_period, index;
    int64_t packed, unpacked;
    if (available < (size_t)RSD_MIN_RECORD) return false;
    name_length = rsd_le16(window + RSD_NAME_LENGTH_OFFSET);
    if (name_length < RSD_NAME_MIN || name_length > RSD_NAME_MAX ||
        (size_t)RSD_HEADER + name_length + RSD_STREAM_HEADER > available)
        return false;
    /* The name is NUL terminated in its last byte and nowhere else. */
    if (window[RSD_HEADER + name_length - 1U] != 0U) return false;
    for (index = 0U; index + 1U < name_length; ++index) {
        uint8_t c = window[RSD_HEADER + index];
        if (c < 0x20U || c == 0x7FU) return false;
    }
    /* Signed on purpose: the setup engine reads both sizes as int32. */
    packed = (int64_t)(int32_t)rsd_le32(window);
    unpacked = (int64_t)(int32_t)rsd_le32(window + 4);
    if (packed < RSD_STREAM_HEADER || unpacked < 0 ||
        unpacked > packed * RSD_RATIO + RSD_RATIO_SLACK)
        return false;
    attributes = rsd_le16(window + 8);
    if ((attributes & ~RSD_ATTRIBUTE_MASK) != 0U) return false;
    stream = window + RSD_HEADER + name_length;
    initial_period = ((uint32_t)stream[4] << 4U) | ((uint32_t)stream[5] >> 4U);
    update_period = (((uint32_t)stream[5] & 0x0FU) << 8U) | (uint32_t)stream[6];
    if (stream[0] != 0xB5U || stream[1] != 0x9CU || stream[2] > 1U ||
        stream[3] != 0xFFU || initial_period == 0U || update_period == 0U)
        return false;
    if (out) {
        xx_mem_zero(out, sizeof(*out));
        out->packed = packed;
        out->unpacked = unpacked;
        out->attributes = attributes;
        out->dos_date = rsd_le16(window + 10);
        out->dos_time = rsd_le16(window + 12);
        out->header_size = (uint32_t)RSD_HEADER + name_length;
        rsd_convert_name(window + RSD_HEADER, name_length - 1U, out->name);
    }
    return true;
}

/* Walk the chain from the base.  With `items` NULL this is the probe and
 * keeps nothing; otherwise `items` holds `capacity` entries.  The chain ends
 * at the end of the device, at a stream that runs past it (a member split
 * across volumes) or at the first bytes that are not a record; at least one
 * record is required. */
static bool rsd_walk(Abstractformat *format, rsd_layout *layout,
                     rsd_item *items, uint32_t capacity, xx_pd_struct *pd) {
    uint8_t window[RSD_WINDOW];
    rsd_layout result;
    int64_t base, total, cursor = 0;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base || total - base < (int64_t)RSD_MIN_RECORD) return false;
    xx_mem_zero(&result, sizeof(result));
    result.span = total - base;
    while (cursor < result.span) {
        rsd_item record;
        int64_t remaining = result.span - cursor, data;
        size_t available = remaining < (int64_t)RSD_WINDOW
                               ? (size_t)remaining : (size_t)RSD_WINDOW;
        if ((result.count & RSD_POLL_MASK) == 0U && rsd_stopped(pd))
            return false;
        if (!rsd_read_at(format->device, base + cursor, window, available))
            return false;
        if (!rsd_parse_record(window, available, &record)) {
            /* Trailing bytes that are not a record end the volume; a first
             * record that is not one means this is no volume at all. */
            if (result.count == 0U) return false;
            break;
        }
        if (result.count >= RSD_MAX_RECORDS) return false;
        data = cursor + (int64_t)record.header_size;
        record.header = base + cursor;
        record.data = base + data;
        record.index = result.count;
        if (record.packed > result.span - data) {
            /* The disk ended inside this stream: the rest of the member is
             * on another volume, and this volume ends with the file. */
            record.present = result.span - data;
            record.split = true;
            result.split = true;
            cursor = result.span;
        } else {
            record.present = record.packed;
            cursor = data + record.packed;
        }
        if (items) {
            if (result.count >= capacity) return false;
            items[result.count] = record;
        }
        ++result.count;
        if (record.split) break;
    }
    result.chain_end = cursor;
    *layout = result;
    return result.count != 0U;
}

static void rsd_stream_free(void *opaque) {
    rsd_stream *stream = (rsd_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool rsd_open_stream(Abstractformat *format, rsd_stream **result,
                            xx_pd_struct *pd) {
    rsd_layout layout, again;
    rsd_stream *stream;
    if (!result || !rsd_walk(format, &layout, NULL, 0U, pd)) return false;
    stream = (rsd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    /* At most RSD_MAX_RECORDS records, each proven by at least 26 bytes of
     * the file: the bookkeeping never outgrows the volume. */
    stream->items = (rsd_item *)xx_mem_calloc(layout.count,
                                              sizeof(*stream->items));
    if (!stream->items ||
        !rsd_walk(format, &again, stream->items, layout.count, pd) ||
        again.count != layout.count || again.chain_end != layout.chain_end ||
        !rsd_mark_duplicates(stream->items, layout.count)) {
        rsd_stream_free(stream);
        return false;
    }
    stream->layout = layout;
    stream->count = layout.count;
    *result = stream;
    return true;
}

static bool rsd_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool rsd_set_record(xx_archive_record *record, const rsd_item *item) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = item->header;
    record->header_size = (int64_t)item->header_size;
    record->data_offset = item->data;
    record->compressed_size = item->present;
    return xx_archive_record_set_original_name(record, item->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)item->present) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)item->unpacked) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSION_METHOD,
               XX_RTPATCH_SETUP_DATA_METHOD_RTPATCH) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          item->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          item->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          item->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Decode one member in memory.  Every size is checked before anything is
 * allocated, so the buffers are bounded by the caps, the caller's limits
 * and what the packed bytes could possibly decode to. */
static bool rsd_decode(Abstractformat *format, const rsd_item *item,
                       uint64_t max_member, uint64_t memory_limit,
                       uint8_t **plain, size_t *plain_size, xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool result = false;
    *plain = NULL;
    *plain_size = 0U;
    /* The rest of a split member is on another volume. */
    if (item->split || item->present != item->packed) return false;
    if (item->packed < RSD_STREAM_HEADER || item->packed > RSD_MAX_PACKED ||
        item->unpacked < 0 || item->unpacked > RSD_MAX_UNPACKED ||
        (uint64_t)item->unpacked > max_member ||
        (uint64_t)item->packed > memory_limit ||
        (uint64_t)item->unpacked > memory_limit - (uint64_t)item->packed ||
        rsd_stopped(pd))
        return false;
    input = (uint8_t *)xx_mem_alloc((size_t)item->packed);
    output = (uint8_t *)xx_mem_alloc(item->unpacked != 0
                                         ? (size_t)item->unpacked : 1U);
    if (input && output &&
        rsd_read_at(format->device, item->data, input, (size_t)item->packed) &&
        !rsd_stopped(pd) &&
        xx_rtpatch_decode_memory(input, (size_t)item->packed, output,
                                 (size_t)item->unpacked, &written) &&
        written == (size_t)item->unpacked) {
        *plain = output;
        *plain_size = written;
        output = NULL;
        result = true;
    }
    if (output) xx_mem_free(output);
    if (input) xx_mem_free(input);
    return result;
}

static bool rsd_write_all(xx_io_device *destination, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---- public API -------------------------------------------------------- */

void xx_rtpatch_setup_data_init(xx_rtpatch_setup_data *archive,
                                xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_RTPATCH_SETUP_DATA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-rtpatch-setup-data");
    xx_format_set_extension(&archive->format, "001");
    archive->format.check_is_valid = xx_rtpatch_setup_data_check_is_valid;
    archive->format.handle_base_info = xx_rtpatch_setup_data_handle_base_info;
    archive->format.get_format_size = xx_rtpatch_setup_data_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rtpatch_setup_data_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rtpatch_setup_data_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rtpatch_setup_data_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rtpatch_setup_data_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rtpatch_setup_data_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rtpatch_setup_data_free_archive_records_reading;
}

xx_rtpatch_setup_data *xx_rtpatch_setup_data_create(xx_io_device *device,
                                                    int64_t base_address) {
    xx_rtpatch_setup_data *archive =
        (xx_rtpatch_setup_data *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_rtpatch_setup_data_init(archive, device, base_address);
    return archive;
}

void xx_rtpatch_setup_data_destroy(xx_rtpatch_setup_data *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_rtpatch_setup_data_free(xx_rtpatch_setup_data *archive) {
    if (!archive) return;
    xx_rtpatch_setup_data_destroy(archive);
    xx_mem_free(archive);
}

bool xx_rtpatch_setup_data_check_is_valid(Abstractformat *format,
                                          xx_pd_struct *pd) {
    rsd_layout layout;
    return rsd_walk(format, &layout, NULL, 0U, pd);
}

bool xx_rtpatch_setup_data_handle_base_info(Abstractformat *format,
                                            xx_pd_struct *pd) {
    rsd_layout layout;
    xx_rtpatch_setup_data *archive;
    if (!rsd_walk(format, &layout, NULL, 0U, pd)) return false;
    archive = (xx_rtpatch_setup_data *)format;
    archive->number_of_records = layout.count;
    archive->chain_end = layout.chain_end;
    archive->split = layout.split;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.chain_end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_rtpatch_setup_data_get_format_size(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rtpatch_setup_data_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_rtpatch_setup_data_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rtpatch_setup_data_handle_base_info(format, pd))
               ? ((xx_rtpatch_setup_data *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_rtpatch_setup_data_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    rsd_stream *stream;
    xx_archive_record_state *state;
    if (!rsd_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        rsd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = rsd_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!rsd_copy_options(&state->options, options) ||
        !rsd_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_rtpatch_setup_data_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_rtpatch_setup_data_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    rsd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (rsd_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        rsd_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rtpatch_setup_data_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    rsd_stream *stream;
    const rsd_item *item;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, base_length;
    uint64_t max_member = UINT64_MAX, memory_limit = UINT64_MAX;
    xx_io_device *destination;
    bool overwrite, created = false, result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (rsd_stream *)state->internal_state) ||
        stream->index >= stream->count || rsd_stopped(pd))
        return false;
    item = &stream->items[stream->index];

    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option) max_member = xx_var_get_u64(option);
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MEMORY_LIMIT);
    if (option) memory_limit = xx_var_get_u64(option);

    /* The member is decoded before any file is created, so a stream that
     * fails (or a split member) leaves nothing behind. */
    if (!rsd_decode(format, item, max_member, memory_limit, &plain,
                    &plain_size, pd))
        return false;

    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: the member was verified, nothing is written. */
        result = true;
        goto done;
    }
    /* item->name came from the file: refuse it before anything is created
     * when it names a device or is not a usable file name. */
    if (!rsd_safe_name(item->name)) goto done;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    base_length = xx_str_len(base);
    path = (base_length != 0U && base[base_length - 1U] != '/' &&
            base[base_length - 1U] != '\\')
               ? xx_str_concat3(base, "/", item->name)
               : xx_str_concat(base, item->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    destination = xx_io_file_open(path, overwrite ? "wb" : "wbx");
    if (!destination) goto done;
    /* Only a file this call opened is removed again on failure. */
    created = true;
    result = rsd_write_all(destination, plain, plain_size);
    if (xx_io_close(destination) != 0) result = false;
    if (!result && created) xx_rt_remove(path);
done:
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_rtpatch_setup_data_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
