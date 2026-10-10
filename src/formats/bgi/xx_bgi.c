/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BGI / Ethornell version 1 "PackFile    " resource archive.  xx_bgi.h
 * carries the field table and the member-name rules.  The layout and the
 * acceptance limits (count up to 0xFFFFF, index inside the file, every member
 * inside the file) follow GARbro's ArcFormats/Ethornell/ArcBGI.cs, class
 * ArcOpener (MIT, Copyright (C) 2014-2015 by morkt).  The code is adapted
 * from this library's bgi2 reader (src/formats/bgi2/xx_bgi2.c, MIT), which
 * handles the version 2 "BURIKO ARC20" archive.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bgi/xx_bgi.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef BGI
#define XX_BGI_FILE_TYPE XX_FILE_TYPE_BGI
#else
#define XX_BGI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BGI_MAGIC "PackFile    "
#define BGI_MAGIC_SIZE 12
#define BGI_HEADER_SIZE 16
#define BGI_ENTRY_SIZE 0x20
#define BGI_NAME_SIZE 0x10
#define BGI_OFFSET_FIELD 0x10
#define BGI_SIZE_FIELD 0x14
/* GARbro's ArcOpener refuses count > 0xFFFFF; an empty archive (count 0)
 * has nothing to list and is refused here. */
#define BGI_MAX_MEMBERS 0xFFFFFU
/* The index is read 1024 entries (32 KiB) at a time. */
#define BGI_CHUNK_ENTRIES 1024U
/* A converted name: at most "%XX" per raw byte, then "%_" and up to ten
 * digits of entry index, then the terminator. */
#define BGI_NAME_BUFFER (3 * BGI_NAME_SIZE + 2 + 10 + 1)
#define BGI_POLL_MASK 0x3ffU

typedef struct bgi_member_s {
    int64_t data_offset; /**< Absolute offset of the data. */
    int64_t size;
    bool renamed; /**< Duplicate name: "%_<index>" is inserted. */
} bgi_member;

typedef struct bgi_key_s {
    uint64_t hash; /**< Of the converted name, ASCII folded to lower case. */
    uint32_t index;
} bgi_key;

typedef struct bgi_layout_s {
    uint32_t count;
    int64_t data_base;   /**< 0x10 + 0x20 * count, relative. */
    int64_t format_size; /**< End of the furthest member, relative. */
} bgi_layout;

typedef struct bgi_stream_s {
    bgi_member *items;
    size_t count;
    size_t index;
    char *name; /**< BGI_NAME_BUFFER bytes: the current member's name. */
} bgi_stream;

static size_t bgi_capacity(void)
{
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < 4096U) n = 4096U;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}

static bool bgi_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Stream `size` bytes at `offset` into `destination` (or just read them
 * through when it is NULL) in fixed chunks. */
static bool bgi_copy_range(xx_io_device *source, int64_t offset, int64_t size, xx_io_device *destination, xx_pd_struct *pd)
{
    const size_t capacity = bgi_capacity();
    uint8_t *buffer;
    int64_t remaining = size;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (ok && remaining > 0) {
        size_t chunk = remaining > (int64_t)capacity ? capacity : (size_t)remaining;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) || !bgi_read_at(source, offset + (size - remaining), buffer, chunk)) {
            ok = false;
            break;
        }
        while (destination && written < chunk) {
            ssize_t amount = xx_io_write(destination, buffer + written, chunk - written);
            if (amount <= 0 || (size_t)amount > chunk - written) {
                ok = false;
                break;
            }
            written += (size_t)amount;
        }
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* ---- member names ------------------------------------------------------ */

static bool bgi_is_sjis_lead(uint8_t c)
{
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}

static bool bgi_is_sjis_trail(uint8_t c)
{
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}

static size_t bgi_put_escape(char *out, uint8_t c)
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

/* Length of the raw name: up to the first NUL, at most BGI_NAME_SIZE. */
static size_t bgi_raw_length(const uint8_t *raw)
{
    size_t length = 0U;
    while (length < BGI_NAME_SIZE && raw[length] != 0U) ++length;
    return length;
}

/* Raw Shift-JIS name -> ASCII path (see xx_bgi.h).  A double-byte character
 * is escaped as a unit, so a trail byte of 0x5C never becomes a separator.
 * `out` holds at least 3 * length + 1 bytes; returns the converted length. */
static size_t bgi_convert_name(const uint8_t *raw, size_t length, char *out)
{
    size_t at = 0U;
    size_t index = 0U;
    while (index < length) {
        uint8_t c = raw[index];
        if (bgi_is_sjis_lead(c) && index + 1U < length && bgi_is_sjis_trail(raw[index + 1U])) {
            at += bgi_put_escape(out + at, c);
            at += bgi_put_escape(out + at, raw[index + 1U]);
            index += 2U;
            continue;
        }
        if (c >= 0x80U || c < 0x20U || c == 0x7fU || c == (uint8_t)'%') at += bgi_put_escape(out + at, c);
        else if (c == (uint8_t)'\\') out[at++] = '/';
        else out[at++] = (char)c;
        ++index;
    }
    out[at] = 0;
    return at;
}

/* 64-bit FNV-1a with ASCII folded to lower case. */
static uint64_t bgi_name_hash(const char *name, size_t length)
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

/* Insert "%_<index>" before the extension of the last component (or append
 * it).  `name` has room for BGI_NAME_BUFFER bytes. */
static void bgi_insert_suffix(char *name, size_t length, uint32_t index)
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
    if (length + suffix_length >= BGI_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at) name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* A Windows device name as the part of a component before its first '.',
 * trailing spaces ignored. */
static bool bgi_reserved_component(const char *segment, size_t length)
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

/* Refuse absolute paths, drive letters and streams, empty components,
 * components ending in '.' or ' ' (covers "." and ".."), device names,
 * control characters and characters no Windows path may carry. */
static bool bgi_safe_name(const char *name)
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
            if (length == 0U || segment[length - 1U] == '.' || segment[length - 1U] == ' ' || bgi_reserved_component(segment, length)) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* Converted name of entry `index` from its raw 0x10 bytes into `out`
 * (BGI_NAME_BUFFER bytes); an empty name becomes "%_<index>". */
static size_t bgi_make_name(const uint8_t *raw, uint32_t index, char *out)
{
    size_t length = bgi_convert_name(raw, bgi_raw_length(raw), out);
    if (length == 0U) {
        bgi_insert_suffix(out, 0U, index);
        length = xx_str_len(out);
    }
    return length;
}

/* ---- index walk -------------------------------------------------------- */

static bool bgi_read_header(Abstractformat *format, bgi_layout *layout, int64_t *size_out)
{
    uint8_t header[BGI_HEADER_SIZE];
    int64_t total, size;
    uint32_t count;
    if (!format || !format->device || !layout || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)BGI_HEADER_SIZE || !bgi_read_at(format->device, format->base_address, header, sizeof(header)) ||
        xx_rt_memcmp(header, BGI_MAGIC, BGI_MAGIC_SIZE) != 0)
        return false;
    count = xx_data_get_u32(header + BGI_MAGIC_SIZE, 4, 0, false);
    if (count == 0U || count > BGI_MAX_MEMBERS) return false;
    layout->count = count;
    layout->data_base = (int64_t)BGI_HEADER_SIZE + (int64_t)count * BGI_ENTRY_SIZE;
    if (layout->data_base > size) return false;
    layout->format_size = layout->data_base;
    *size_out = size;
    return true;
}

/* Walk the whole index.  Every member must lie inside the file.  With
 * `items` non-NULL it also fills items[] and keys[] (layout->count entries
 * each), using `name` (BGI_NAME_BUFFER bytes) to hash every name. */
static bool bgi_walk(Abstractformat *format, bgi_layout *layout, int64_t archive_size, bgi_member *items, bgi_key *keys, char *name, xx_pd_struct *pd)
{
    uint8_t *chunk;
    uint32_t done = 0U;
    int64_t furthest = layout->data_base;
    bool ok = true;
    chunk = (uint8_t *)xx_mem_alloc((size_t)BGI_CHUNK_ENTRIES * BGI_ENTRY_SIZE);
    if (!chunk) return false;
    while (ok && done < layout->count) {
        uint32_t take = layout->count - done, i;
        if (take > BGI_CHUNK_ENTRIES) take = BGI_CHUNK_ENTRIES;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !bgi_read_at(format->device, format->base_address + BGI_HEADER_SIZE + (int64_t)done * BGI_ENTRY_SIZE, chunk, (size_t)take * BGI_ENTRY_SIZE)) {
            ok = false;
            break;
        }
        for (i = 0U; i < take; ++i) {
            const uint8_t *entry = chunk + (size_t)i * BGI_ENTRY_SIZE;
            int64_t offset = (int64_t)xx_data_get_u32(entry + BGI_OFFSET_FIELD, 4, 0, false);
            int64_t size = (int64_t)xx_data_get_u32(entry + BGI_SIZE_FIELD, 4, 0, false);
            int64_t start = layout->data_base + offset;
            uint32_t index = done + i;
            if (start > archive_size || size > archive_size - start) {
                ok = false;
                break;
            }
            if (start + size > furthest) furthest = start + size;
            if (items) {
                size_t length = bgi_make_name(entry, index, name);
                items[index].data_offset = format->base_address + start;
                items[index].size = size;
                items[index].renamed = false;
                keys[index].hash = bgi_name_hash(name, length);
                keys[index].index = index;
            }
        }
        done += take;
    }
    xx_mem_free(chunk);
    if (ok) layout->format_size = furthest;
    return ok;
}

static int bgi_compare_keys(const void *left, const void *right)
{
    const bgi_key *a = (const bgi_key *)left;
    const bgi_key *b = (const bgi_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static void bgi_mark_duplicates(bgi_member *items, bgi_key *keys, size_t count)
{
    size_t index;
    if (count < 2U) return;
    xx_rt_qsort(keys, count, sizeof(*keys), bgi_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash && keys[index].index < count) items[keys[index].index].renamed = true;
}

static void bgi_stream_free(void *opaque)
{
    bgi_stream *stream = (bgi_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool bgi_open_stream(Abstractformat *format, bgi_stream **result, xx_pd_struct *pd)
{
    bgi_layout layout;
    int64_t archive_size = 0;
    bgi_member *items = NULL;
    bgi_key *keys = NULL;
    char *name = NULL;
    bgi_stream *stream = NULL;
    if (!result || !bgi_read_header(format, &layout, &archive_size)) return false;
    /* At most 0xFFFFF entries: about 40 MiB of bookkeeping, and only after
     * the index itself (32 bytes of file per 40 bytes here) was found to fit
     * inside the file. */
    items = (bgi_member *)xx_mem_calloc(layout.count, sizeof(*items));
    keys = (bgi_key *)xx_mem_alloc((size_t)layout.count * sizeof(*keys));
    name = (char *)xx_mem_alloc(BGI_NAME_BUFFER);
    stream = (bgi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!items || !keys || !name || !stream || !bgi_walk(format, &layout, archive_size, items, keys, name, pd)) goto fail;
    bgi_mark_duplicates(items, keys, layout.count);
    xx_mem_free(keys);
    stream->items = items;
    stream->count = layout.count;
    stream->name = name;
    *result = stream;
    return true;
fail:
    if (items) xx_mem_free(items);
    if (keys) xx_mem_free(keys);
    if (name) xx_mem_free(name);
    if (stream) xx_mem_free(stream);
    return false;
}

static int64_t bgi_entry_offset(Abstractformat *format, size_t index)
{
    return format->base_address + BGI_HEADER_SIZE + (int64_t)index * BGI_ENTRY_SIZE;
}

static bool bgi_load_name(Abstractformat *format, bgi_stream *stream, size_t index)
{
    uint8_t raw[BGI_NAME_SIZE];
    size_t length;
    if (!bgi_read_at(format->device, bgi_entry_offset(format, index), raw, sizeof(raw))) return false;
    length = bgi_make_name(raw, (uint32_t)index, stream->name);
    if (stream->items[index].renamed) bgi_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

static bool bgi_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *bgi_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool bgi_set_record(Abstractformat *format, xx_archive_record *record, bgi_stream *stream, size_t index)
{
    const bgi_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!bgi_load_name(format, stream, index)) return false;
    record->header_offset = bgi_entry_offset(format, index);
    record->header_size = BGI_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, stream->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_bgi_init(xx_bgi *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BGI_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "arc");
    archive->format.check_is_valid = xx_bgi_check_is_valid;
    archive->format.handle_base_info = xx_bgi_handle_base_info;
    archive->format.get_format_size = xx_bgi_get_format_size;
    archive->format.get_number_of_archive_records = xx_bgi_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_bgi_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_bgi_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_bgi_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_bgi_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_bgi_free_archive_records_reading;
    archive->data_base = -1;
}

xx_bgi *xx_bgi_create(xx_io_device *device, int64_t base_address)
{
    xx_bgi *archive = (xx_bgi *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_bgi_init(archive, device, base_address);
    return archive;
}

void xx_bgi_destroy(xx_bgi *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_bgi_free(xx_bgi *archive)
{
    if (!archive) return;
    xx_bgi_destroy(archive);
    xx_mem_free(archive);
}

bool xx_bgi_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    bgi_layout layout;
    int64_t archive_size = 0;
    return bgi_read_header(format, &layout, &archive_size) && bgi_walk(format, &layout, archive_size, NULL, NULL, NULL, pd);
}

bool xx_bgi_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    bgi_layout layout;
    int64_t archive_size = 0;
    xx_bgi *archive;
    if (!bgi_read_header(format, &layout, &archive_size) || !bgi_walk(format, &layout, archive_size, NULL, NULL, NULL, pd)) return false;
    archive = (xx_bgi *)format;
    archive->number_of_records = layout.count;
    archive->data_base = layout.data_base;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_bgi_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_bgi_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_bgi_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_bgi_handle_base_info(format, pd)) ? ((xx_bgi *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_bgi_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    bgi_stream *stream;
    xx_archive_record_state *state;
    if (!bgi_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        bgi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = bgi_stream_free;
    state->total_records = stream->count;
    if (!bgi_copy_options(&state->options, options) || !bgi_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_bgi_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_bgi_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    bgi_stream *stream;
    if (!format || !state || state->format != format || !(stream = (bgi_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    if ((stream->index & BGI_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = bgi_set_record(format, &state->current_record, stream, stream->index);
    return state->has_record;
}

bool xx_bgi_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    bgi_stream *stream;
    const bgi_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (bgi_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->size < 0 || member->data_offset < 0) return false;
    path_option = bgi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return bgi_copy_range(format->device, member->data_offset, member->size, NULL, pd);
    if (!bgi_safe_name(stream->name)) return false;
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
        if (!destination) goto done;
        created = true;
        result = bgi_copy_range(format->device, member->data_offset, member->size, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_bgi_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
