/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield 7.x setup.boot (U3 "IS7 BOOT").  xx_installshield_7_setup2.h
 * carries the record layout.
 *
 * Written from the file structure: four NUL-terminated strings per record
 * (short name, long name, version, decimal size) followed by a complete
 * SZDD file.  U3's handler was consulted only to learn which field names
 * the output (the long name).  The name conversion, the duplicate handling
 * and the device-name check follow this library's installshield_7_setup
 * reader (MIT, same project).  The SZDD LZSS decoder here is a streaming
 * one, so no member is ever held in memory whole; it decodes the same
 * grammar as algo/mscompress (flag byte LSB first, 1 = literal; a match is
 * 12 bits of window position and 4 bits of length - 3; a 4 KiB window
 * filled with spaces whose write cursor starts 16 bytes before its end).
 *
 * The file is data only: nothing in it is run or emulated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installshield_7_setup2/xx_installshield_7_setup2.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef INSTALLSHIELD_7_SETUP2
#define XX_INSTALLSHIELD_7_SETUP2_FILE_TYPE XX_FILE_TYPE_INSTALLSHIELD_7_SETUP2
#else
#define XX_INSTALLSHIELD_7_SETUP2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* A name holds 1..IS7B_MAX_STRING bytes before its NUL (MAX_PATH). */
#define IS7B_MAX_STRING 260
#define IS7B_SZDD_HEADER 14
/* Four strings with their NULs, then the member's SZDD header. */
#define IS7B_MAX_HEADER (4 * (IS7B_MAX_STRING + 1) + IS7B_SZDD_HEADER)
/* Real files carry four engine DLLs; this bounds the walk, the
 * bookkeeping (about 40 KiB at the cap) and the files one extraction can
 * create on hostile input.  The chain ends at the cap. */
#define IS7B_MAX_RECORDS 1024U
#define IS7B_MAX_VERSION_PARTS 4U
#define IS7B_MAX_VERSION_DIGITS 10U
/* Eighteen digits always fit in an int64_t (nineteen can overflow it). */
#define IS7B_MAX_SIZE_DIGITS 18U
/* One LZSS match (two input bytes) yields at most 18 output bytes and a
 * literal one, so no stream expands by more than this factor. */
#define IS7B_MAX_EXPANSION 9
#define IS7B_WINDOW 4096U
#define IS7B_WINDOW_START (IS7B_WINDOW - 16U)
#define IS7B_MATCH_MINIMUM 3U
#define IS7B_METHOD_LZSS 0x41U /* 'A' */
#define IS7B_CHUNK 65536U
/* The record walk reads ahead this much at a time. */
#define IS7B_VIEW_BUFFER 16384U
/* A converted name: at most "%XX" per raw byte, then "%_" and up to ten
 * digits of record index, then the terminator. */
#define IS7B_NAME_BUFFER (3 * IS7B_MAX_STRING + 2 + 10 + 1)
#define IS7B_POLL_MASK 0xffU

static const uint8_t is7b_szdd_magic[8] = {'S',   'Z',   'D',   'D',
                                           0x88U, 0xf0U, 0x27U, 0x33U};

typedef struct is7b_fields_s {
    size_t long_offset;
    size_t long_length;
    size_t version_offset;
    size_t version_length;
    size_t header_size; /**< The four strings, up to the member. */
    int64_t size;       /**< The member: SZDD header plus LZSS data. */
    uint32_t unpacked;  /**< From the SZDD header. */
} is7b_fields;

typedef struct is7b_member_s {
    int64_t header_offset; /**< Absolute. */
    int64_t data_offset;   /**< Absolute; the SZDD header. */
    int64_t size;
    uint32_t unpacked;
    uint32_t header_size;
    bool renamed; /**< Name clash: "%_<index>" is inserted. */
} is7b_member;

typedef struct is7b_key_s {
    uint64_t hash; /**< Of the converted name, ASCII folded to lower case. */
    uint32_t index;
} is7b_key;

typedef struct is7b_stream_s {
    is7b_member *items;
    size_t count;
    size_t index;
    char name[IS7B_NAME_BUFFER];
    char version[IS7B_MAX_STRING + 1];
} is7b_stream;

typedef struct is7b_window_s {
    xx_io_device *device;
    int64_t origin; /**< Absolute offset of relative offset 0. */
    int64_t limit;  /**< Relative end of data. */
    int64_t start;  /**< Relative offset of buffer[0]. */
    size_t length;
    uint8_t buffer[IS7B_VIEW_BUFFER];
} is7b_window;

typedef struct is7b_decoder_s {
    xx_io_device *source;
    xx_io_device *destination; /**< NULL: decode only, to verify. */
    int64_t offset;            /**< Absolute offset of the next input read. */
    int64_t remaining;         /**< Input bytes not read yet. */
    size_t in_at;
    size_t in_length;
    size_t out_length;
    uint32_t window_at;
    uint8_t window[IS7B_WINDOW];
    uint8_t input[IS7B_CHUNK];
    uint8_t output[IS7B_CHUNK];
} is7b_decoder;

static uint32_t is7b_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool is7b_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ---- records ----------------------------------------------------------- */

/* The NUL-terminated string at view[at]: *length gets 0..IS7B_MAX_STRING.
 * False when it runs past the available bytes or is too long. */
static bool is7b_string(const uint8_t *view, size_t avail, size_t at,
                        size_t *length) {
    size_t count = 0U;
    if (at >= avail) return false;
    while (count < avail - at && count <= IS7B_MAX_STRING) {
        if (view[at + count] == 0U) {
            *length = count;
            return true;
        }
        ++count;
    }
    return false;
}

static uint8_t is7b_fold(uint8_t c) {
    return (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
               ? (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a')
               : c;
}

/* 1..4 dot-separated groups of 1..10 decimal digits. */
static bool is7b_dotted_version(const uint8_t *text, size_t length) {
    size_t index, digits = 0U, parts = 1U;
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        if (c >= (uint8_t)'0' && c <= (uint8_t)'9') {
            if (++digits > IS7B_MAX_VERSION_DIGITS) return false;
        } else if (c == (uint8_t)'.' && digits != 0U &&
                   parts < IS7B_MAX_VERSION_PARTS) {
            ++parts;
            digits = 0U;
        } else {
            return false;
        }
    }
    return digits != 0U;
}

/* Decimal digits, no sign, no leading zero (except "0" itself). */
static bool is7b_decimal(const uint8_t *text, size_t length, int64_t *value) {
    size_t index;
    int64_t result = 0;
    if (length == 0U || length > IS7B_MAX_SIZE_DIGITS ||
        (length > 1U && text[0] == (uint8_t)'0'))
        return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        if (c < (uint8_t)'0' || c > (uint8_t)'9') return false;
        result = result * 10 + (int64_t)(c - (uint8_t)'0');
    }
    *value = result;
    return true;
}

/* Split one record out of @p view, which holds min(IS7B_MAX_HEADER, room)
 * bytes of it; @p room is how many bytes are left in the device from the
 * record on.  The short name carries no separator, neither name a control
 * byte; the version is empty or dotted decimal; the member fits in @p room
 * and starts with an SZDD 'A' header whose unpacked size its LZSS data can
 * reach. */
static bool is7b_parse_record(const uint8_t *view, size_t avail, int64_t room,
                              is7b_fields *fields) {
    size_t index, short_length, size_offset, size_length;
    const uint8_t *szdd;
    int64_t packed;
    if (!is7b_string(view, avail, 0U, &short_length) || short_length == 0U)
        return false;
    for (index = 0U; index < short_length; ++index) {
        uint8_t c = view[index];
        if (c < 0x20U || c == 0x7fU || c == (uint8_t)'/' ||
            c == (uint8_t)'\\')
            return false;
    }
    fields->long_offset = short_length + 1U;
    if (!is7b_string(view, avail, fields->long_offset, &fields->long_length) ||
        fields->long_length == 0U)
        return false;
    for (index = 0U; index < fields->long_length; ++index) {
        uint8_t c = view[fields->long_offset + index];
        if (c < 0x20U || c == 0x7fU) return false;
    }
    fields->version_offset = fields->long_offset + fields->long_length + 1U;
    if (!is7b_string(view, avail, fields->version_offset,
                     &fields->version_length) ||
        (fields->version_length != 0U &&
         !is7b_dotted_version(view + fields->version_offset,
                              fields->version_length)))
        return false;
    size_offset = fields->version_offset + fields->version_length + 1U;
    if (!is7b_string(view, avail, size_offset, &size_length) ||
        !is7b_decimal(view + size_offset, size_length, &fields->size))
        return false;
    fields->header_size = size_offset + size_length + 1U;
    /* The four strings take at most IS7B_MAX_HEADER - IS7B_SZDD_HEADER
     * bytes, so the SZDD header is in the view whenever the device has
     * it. */
    if (fields->size < IS7B_SZDD_HEADER ||
        (int64_t)fields->header_size > room ||
        fields->size > room - (int64_t)fields->header_size ||
        avail - fields->header_size < IS7B_SZDD_HEADER)
        return false;
    szdd = view + fields->header_size;
    if (xx_rt_memcmp(szdd, is7b_szdd_magic, sizeof(is7b_szdd_magic)) != 0 ||
        szdd[8] != IS7B_METHOD_LZSS)
        return false;
    fields->unpacked = is7b_le32(szdd + 10U);
    packed = fields->size - IS7B_SZDD_HEADER;
    /* packed < 10^18, so the product cannot overflow. */
    return (int64_t)fields->unpacked <= packed * IS7B_MAX_EXPANSION;
}

/* ---- member names ------------------------------------------------------ */

/* Raw ANSI name -> output name: '\\' becomes '/', bytes 0x80-0xFF and '%'
 * become "%XX" (so the mapping is one-to-one and never meets a code page).
 * @p out holds at least 3 * length + 1 bytes; returns the converted
 * length. */
static size_t is7b_convert_name(const uint8_t *raw, size_t length,
                                char *out) {
    static const char digits[] = "0123456789ABCDEF";
    size_t at = 0U, index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c >= 0x80U || c == (uint8_t)'%') {
            out[at++] = '%';
            out[at++] = digits[(c >> 4U) & 0x0fU];
            out[at++] = digits[c & 0x0fU];
        } else if (c == (uint8_t)'\\') {
            out[at++] = '/';
        } else {
            out[at++] = (char)c;
        }
    }
    out[at] = 0;
    return at;
}

/* 64-bit FNV-1a with ASCII folded to lower case, so names a
 * case-insensitive file system treats as one hash alike.  A collision
 * between different names only renames a member needlessly. */
static uint64_t is7b_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        hash ^= (uint64_t)is7b_fold((uint8_t)name[index]);
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* Insert "%_<index>" before the extension of the last component (or append
 * it when there is none).  @p name has room for IS7B_NAME_BUFFER bytes. */
static void is7b_insert_suffix(char *name, size_t length, uint32_t index) {
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
    if (length + suffix_length >= IS7B_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$,
 * CONIN$, CONOUT$) as the part of a component before its first '.',
 * trailing spaces ignored. */
static bool is7b_reserved_component(const char *segment, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",    "AUX",
                                          "NUL",    "CLOCK$", "CONIN$",
                                          "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ')
        --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
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

/* Refuse absolute paths, drive letters and streams (any ':'), empty
 * components, components ending in '.' or ' ' (this covers "." and ".."),
 * device names, control characters and the characters no Windows path may
 * carry. */
static bool is7b_safe_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 0x7fU ||
            (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ' ||
                is7b_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- chain walk -------------------------------------------------------- */

/* Return a pointer to relative offset @p pos with
 * min(IS7B_MAX_HEADER, limit - pos) bytes behind it; *avail gets that
 * count.  Records of small members sit back to back, so the buffer is
 * refilled only when the bytes wanted are not in it already. */
static const uint8_t *is7b_view(is7b_window *window, int64_t pos,
                                size_t *avail) {
    int64_t want = window->limit - pos;
    if (pos < 0 || want <= 0) return NULL;
    if (want > IS7B_MAX_HEADER) want = IS7B_MAX_HEADER;
    if (pos < window->start ||
        pos + want > window->start + (int64_t)window->length) {
        int64_t chunk = window->limit - pos;
        if (chunk > (int64_t)IS7B_VIEW_BUFFER) chunk = IS7B_VIEW_BUFFER;
        window->length = 0U;
        if (!is7b_read_at(window->device, window->origin + pos, window->buffer,
                          (size_t)chunk))
            return NULL;
        window->start = pos;
        window->length = (size_t)chunk;
    }
    *avail = (size_t)want;
    return window->buffer + (pos - window->start);
}

/* Walk the record chain from base_address.  With @p items NULL this is the
 * probe and keeps nothing; otherwise it fills items[] and keys[]
 * (@p capacity entries each), using @p name to hash every converted name.
 * *count gets the number of records, *end where the chain ended
 * (relative).  At least one record is required. */
static bool is7b_walk(Abstractformat *format, is7b_member *items,
                      is7b_key *keys, size_t capacity, char *name,
                      size_t *count, int64_t *end, xx_pd_struct *pd) {
    is7b_window *window;
    int64_t total, pos = 0;
    size_t records = 0U;
    bool ok = false;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total <= format->base_address) return false;
    window = (is7b_window *)xx_mem_alloc(sizeof(*window));
    if (!window) return false;
    window->device = format->device;
    window->origin = format->base_address;
    window->limit = total - format->base_address;
    window->start = 0;
    window->length = 0U;
    while (pos < window->limit && records < IS7B_MAX_RECORDS) {
        const uint8_t *view;
        size_t avail = 0U;
        is7b_fields fields;
        int64_t data;
        if ((records & IS7B_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            goto done;
        view = is7b_view(window, pos, &avail);
        if (!view) goto done;
        /* The chain ends where no complete record starts. */
        if (!is7b_parse_record(view, avail, window->limit - pos, &fields))
            break;
        data = pos + (int64_t)fields.header_size;
        if (items) {
            size_t converted;
            if (records >= capacity) goto done;
            converted = is7b_convert_name(view + fields.long_offset,
                                          fields.long_length, name);
            items[records].header_offset = format->base_address + pos;
            items[records].data_offset = format->base_address + data;
            items[records].size = fields.size;
            items[records].unpacked = fields.unpacked;
            items[records].header_size = (uint32_t)fields.header_size;
            items[records].renamed = false;
            keys[records].hash = is7b_name_hash(name, converted);
            keys[records].index = (uint32_t)records;
        }
        pos = data + fields.size;
        ++records;
    }
    if (records == 0U) goto done;
    if (count) *count = records;
    if (end) *end = pos;
    ok = true;
done:
    xx_mem_free(window);
    return ok;
}

static int is7b_compare_keys(const void *left, const void *right) {
    const is7b_key *a = (const is7b_key *)left;
    const is7b_key *b = (const is7b_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Sorting puts every group of equal (case-folded) names together, lowest
 * record index first; that one keeps its name and the rest are renamed. */
static void is7b_mark_duplicates(is7b_member *items, is7b_key *keys,
                                 size_t count) {
    size_t index;
    if (count < 2U) return;
    xx_rt_qsort(keys, count, sizeof(*keys), is7b_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash &&
            keys[index].index < count)
            items[keys[index].index].renamed = true;
}

static void is7b_stream_free(void *opaque) {
    is7b_stream *stream = (is7b_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool is7b_open_stream(Abstractformat *format, is7b_stream **result,
                             xx_pd_struct *pd) {
    is7b_key *keys = NULL;
    is7b_stream *stream = NULL;
    size_t count = 0U, filled = 0U;
    if (!result ||
        !is7b_walk(format, NULL, NULL, 0U, NULL, &count, NULL, pd))
        return false;
    stream = (is7b_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->items = (is7b_member *)xx_mem_calloc(count, sizeof(*stream->items));
    keys = (is7b_key *)xx_mem_calloc(count, sizeof(*keys));
    if (!stream->items || !keys ||
        !is7b_walk(format, stream->items, keys, count, stream->name, &filled,
                   NULL, pd) ||
        filled != count)
        goto fail;
    is7b_mark_duplicates(stream->items, keys, count);
    xx_mem_free(keys);
    stream->count = count;
    *result = stream;
    return true;
fail:
    if (keys) xx_mem_free(keys);
    is7b_stream_free(stream);
    return false;
}

static bool is7b_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
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

static const xx_var *is7b_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* Re-read record @p index and leave its converted (and, for a clash,
 * suffixed) name in stream->name and its version in stream->version. */
static bool is7b_load_record(Abstractformat *format, is7b_stream *stream,
                             size_t index) {
    const is7b_member *member = &stream->items[index];
    uint8_t header[IS7B_MAX_HEADER];
    is7b_fields fields;
    size_t length, want;
    int64_t total = xx_io_total_size(format->device);
    if (member->header_size == 0U ||
        member->header_size > IS7B_MAX_HEADER - IS7B_SZDD_HEADER ||
        member->header_offset < 0 || total < member->header_offset)
        return false;
    want = member->header_size + IS7B_SZDD_HEADER;
    if (!is7b_read_at(format->device, member->header_offset, header, want) ||
        !is7b_parse_record(header, want, total - member->header_offset,
                           &fields) ||
        fields.header_size != member->header_size ||
        fields.size != member->size || fields.unpacked != member->unpacked)
        return false;
    length = is7b_convert_name(header + fields.long_offset, fields.long_length,
                               stream->name);
    if (member->renamed)
        is7b_insert_suffix(stream->name, length, (uint32_t)index);
    xx_rt_memcpy(stream->version, header + fields.version_offset,
                 fields.version_length);
    stream->version[fields.version_length] = 0;
    return true;
}

static bool is7b_set_record(Abstractformat *format, xx_archive_record *record,
                            is7b_stream *stream, size_t index) {
    const is7b_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!is7b_load_record(format, stream, index)) return false;
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->unpacked) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          IS7B_METHOD_LZSS) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          stream->version) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- SZDD LZSS, streamed ----------------------------------------------- */

static bool is7b_flush(is7b_decoder *decoder, xx_pd_struct *pd) {
    size_t written = 0U;
    if (pd && xx_pd_is_stopped(pd)) return false;
    while (decoder->destination && written < decoder->out_length) {
        ssize_t amount = xx_io_write(decoder->destination,
                                     decoder->output + written,
                                     decoder->out_length - written);
        if (amount <= 0 || (size_t)amount > decoder->out_length - written)
            return false;
        written += (size_t)amount;
    }
    decoder->out_length = 0U;
    return true;
}

static bool is7b_next(is7b_decoder *decoder, uint8_t *value) {
    if (decoder->in_at == decoder->in_length) {
        size_t chunk;
        if (decoder->remaining <= 0) return false;
        chunk = decoder->remaining > (int64_t)IS7B_CHUNK
                    ? (size_t)IS7B_CHUNK
                    : (size_t)decoder->remaining;
        if (!is7b_read_at(decoder->source, decoder->offset, decoder->input,
                          chunk))
            return false;
        decoder->offset += (int64_t)chunk;
        decoder->remaining -= (int64_t)chunk;
        decoder->in_at = 0U;
        decoder->in_length = chunk;
    }
    *value = decoder->input[decoder->in_at++];
    return true;
}

static bool is7b_emit(is7b_decoder *decoder, uint8_t value, xx_pd_struct *pd) {
    decoder->window[decoder->window_at] = value;
    decoder->window_at = (decoder->window_at + 1U) & (IS7B_WINDOW - 1U);
    decoder->output[decoder->out_length++] = value;
    return decoder->out_length < IS7B_CHUNK || is7b_flush(decoder, pd);
}

/* Decode member @p member into @p destination (or only check that it
 * decodes when that is NULL).  The stream must yield exactly the unpacked
 * size from the member's own bytes; bytes left over behind the last token
 * are ignored. */
static bool is7b_decode(xx_io_device *source, const is7b_member *member,
                        xx_io_device *destination, xx_pd_struct *pd) {
    is7b_decoder *decoder;
    uint64_t produced = 0U;
    unsigned bit = 0U;
    uint8_t flags = 0U;
    bool ok = false;
    if (!source || member->size < IS7B_SZDD_HEADER || member->data_offset < 0)
        return false;
    decoder = (is7b_decoder *)xx_mem_alloc(sizeof(*decoder));
    if (!decoder) return false;
    decoder->source = source;
    decoder->destination = destination;
    decoder->offset = member->data_offset + IS7B_SZDD_HEADER;
    decoder->remaining = member->size - IS7B_SZDD_HEADER;
    decoder->in_at = 0U;
    decoder->in_length = 0U;
    decoder->out_length = 0U;
    decoder->window_at = IS7B_WINDOW_START;
    xx_rt_memset(decoder->window, 0x20, sizeof(decoder->window));
    /* Every token yields at least one byte, so this runs at most
     * member->unpacked times. */
    while (produced < member->unpacked) {
        uint8_t value;
        if (bit == 0U && !is7b_next(decoder, &flags)) goto done;
        if ((flags & (uint8_t)(1U << bit)) != 0U) {
            if (!is7b_next(decoder, &value) || !is7b_emit(decoder, value, pd))
                goto done;
            ++produced;
        } else {
            uint8_t low, high;
            uint32_t position, length, index;
            if (!is7b_next(decoder, &low) || !is7b_next(decoder, &high))
                goto done;
            position = (uint32_t)low | ((uint32_t)(high & 0xf0U) << 4U);
            length = (uint32_t)(high & 0x0fU) + IS7B_MATCH_MINIMUM;
            if ((uint64_t)length > member->unpacked - produced) goto done;
            for (index = 0U; index < length; ++index) {
                value = decoder->window[(position + index) &
                                        (IS7B_WINDOW - 1U)];
                if (!is7b_emit(decoder, value, pd)) goto done;
            }
            produced += length;
        }
        bit = (bit + 1U) & 7U;
    }
    ok = is7b_flush(decoder, pd);
done:
    xx_mem_free(decoder);
    return ok;
}

/* ---- public API -------------------------------------------------------- */

void xx_installshield_7_setup2_init(xx_installshield_7_setup2 *archive,
                                    xx_io_device *device,
                                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INSTALLSHIELD_7_SETUP2_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "boot");
    archive->format.check_is_valid = xx_installshield_7_setup2_check_is_valid;
    archive->format.handle_base_info =
        xx_installshield_7_setup2_handle_base_info;
    archive->format.get_format_size =
        xx_installshield_7_setup2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_installshield_7_setup2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_installshield_7_setup2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_installshield_7_setup2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_installshield_7_setup2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_installshield_7_setup2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_installshield_7_setup2_free_archive_records_reading;
    archive->chain_end = -1;
}

xx_installshield_7_setup2 *xx_installshield_7_setup2_create(
    xx_io_device *device, int64_t base_address) {
    xx_installshield_7_setup2 *archive =
        (xx_installshield_7_setup2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_installshield_7_setup2_init(archive, device, base_address);
    return archive;
}

void xx_installshield_7_setup2_destroy(xx_installshield_7_setup2 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_installshield_7_setup2_free(xx_installshield_7_setup2 *archive) {
    if (!archive) return;
    xx_installshield_7_setup2_destroy(archive);
    xx_mem_free(archive);
}

bool xx_installshield_7_setup2_check_is_valid(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return is7b_walk(format, NULL, NULL, 0U, NULL, NULL, NULL, pd);
}

bool xx_installshield_7_setup2_handle_base_info(Abstractformat *format,
                                                xx_pd_struct *pd) {
    xx_installshield_7_setup2 *archive;
    size_t count = 0U;
    int64_t end = 0;
    if (!is7b_walk(format, NULL, NULL, 0U, NULL, &count, &end, pd))
        return false;
    archive = (xx_installshield_7_setup2 *)format;
    archive->number_of_records = (uint64_t)count;
    archive->chain_end = end;
    format->number_of_archive_records = (uint64_t)count;
    format->format_size = end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_installshield_7_setup2_get_format_size(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_7_setup2_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_installshield_7_setup2_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_7_setup2_handle_base_info(format, pd))
               ? ((xx_installshield_7_setup2 *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_installshield_7_setup2_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    is7b_stream *stream;
    xx_archive_record_state *state;
    if (!is7b_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        is7b_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = is7b_stream_free;
    state->total_records = stream->count;
    if (!is7b_copy_options(&state->options, options) ||
        !is7b_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_installshield_7_setup2_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_installshield_7_setup2_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    is7b_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (is7b_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = is7b_set_record(format, &state->current_record, stream,
                                        stream->index);
    return state->has_record;
}

bool xx_installshield_7_setup2_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    is7b_stream *stream;
    const is7b_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (is7b_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = is7b_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: decode the member through, which verifies it. */
        return is7b_decode(format->device, member, NULL, pd);
    /* stream->name came from the file: refuse it before anything is created
     * when it could escape the output folder or name a device. */
    if (!is7b_safe_name(stream->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = is7b_decode(format->device, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_installshield_7_setup2_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
