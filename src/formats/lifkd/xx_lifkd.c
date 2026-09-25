/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Knowledge Dynamics Corp. ".LIF" installer library.
 *
 * This is NOT Hewlett-Packard's LIF disk image format; nothing here looks at
 * an 0x8000 magic word.  A KDC LIF file has no archive header at all.  It is
 * a flat chain of members, each one a 54-byte header followed immediately by
 * that member's packed bytes:
 *
 *   offset size meaning
 *     0     8   ASCII hex, MS-DOS packed date/time (high 16 date, low 16 time)
 *     8     8   ASCII hex, packed (stored) size in bytes
 *    16     8   ASCII hex, original (uncompressed) size in bytes
 *    24     4   ASCII hex, CRC-16/CCITT-FALSE of the PACKED bytes
 *    28     4   ASCII hex, CRC-16/CCITT-FALSE of the ORIGINAL bytes
 *    32     2   ASCII hex, method: 1 = stored, 2 = LZD
 *    34    20   member name, ASCII, NUL padded
 *    54   ...   packed data, exactly "packed size" bytes
 *
 * The first 34 bytes are therefore a hex transcription of a 17-byte binary
 * header, read big-endian.  The next header starts at offset + 54 + packed
 * size with no padding of any kind.
 *
 * There is no signature, no directory and no end marker, so identification
 * has to be structural: every header in the chain must be well formed and the
 * chain must land exactly on end of file.  A file that ends mid-member is
 * rejected outright rather than truncated to the last good member -- a
 * partial match on a format this weakly marked is far more likely to be some
 * other file that happens to start with hex digits.
 *
 * The first header alone is still distinctive enough to gate the detector:
 * 34 hex digits, a method of "01" or "02", then a printable name that is
 * terminated and zero padded inside its 20-byte field.  That test is exported
 * as xx_lifkd_is_member_header() so the detector can run this reader as an
 * ordinary magic-gated format ahead of the structural tail probes (a LIF
 * whose stored member is a PKZIP self-extractor otherwise ends in a ZIP
 * end-of-central-directory record and is claimed as ZIP).
 *
 * Method 2 is Rahul Dhesi's LZD, the same variable-width LZW that ZOO uses
 * for its own method 1, so it forwards to xx_zoo_lzd_decode_memory().  See
 * lifkd_unpack_lzd() for the one packaging difference.  Both CRC-16 fields
 * were confirmed against the corpus (1835 of 1836 members; the single holdout
 * is a genuinely damaged member whose packed CRC disagrees as well), so the
 * plaintext CRC is used as the unpack anchor.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lifkd/xx_lifkd.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zoo/xx_zoo.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef LIFKD
#define XX_LIFKD_FILE_TYPE XX_FILE_TYPE_LIFKD
#else
#define XX_LIFKD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define LIFKD_HEADER_SIZE 54
#define LIFKD_NAME_OFFSET 34U
#define LIFKD_NAME_SIZE 20U
#define LIFKD_METHOD_STORED 1U
#define LIFKD_METHOD_LZD 2U
/* A crafted file cannot be made to iterate forever: every step advances by
 * at least the 54-byte header, and the walk stops at this many members. */
#define LIFKD_MAX_MEMBERS 200000U
/* LZD decodes in memory, so its plaintext is capped like the other
 * in-memory decoders in this library.  Stored members are streamed and are
 * not subject to it. */
#define LIFKD_MAX_PLAIN ((uint64_t)256U * 1024U * 1024U)
/* No single LZD code (at most 13 bits wide, table of 8192 entries) can
 * expand to more than this many bytes, and none is shorter than 9 bits, so a
 * member claiming more than (packed + 1) * this cannot be genuine. */
#define LIFKD_LZD_MAX_RUN 8192U
#define LIFKD_COPY_CHUNK 65536U
/* A duplicate name is retried with "_<record>" and then "_<record>_<n>"
 * this many times before the member is listed but not extracted. */
#define LIFKD_RENAME_TRIES 8U

typedef struct lifkd_header_s {
    uint32_t dos_time;
    uint32_t packed;
    uint32_t unpacked;
    uint16_t crc_packed;
    uint16_t crc_plain;
    uint8_t method;
    size_t name_length;
} lifkd_header;

typedef struct lifkd_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t dos_time;
    uint16_t crc_packed;
    uint16_t crc_plain;
    uint8_t method;
    bool extractable;
} lifkd_member;

typedef struct lifkd_stream_s {
    lifkd_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} lifkd_stream;

/* The chain walk reads headers through this window, so a library of many
 * small members costs one device read per window rather than one per
 * member (200000 empty members: about 2600 reads instead of 200000). */
#define LIFKD_WINDOW_SIZE 4096U

typedef struct lifkd_window_s {
    uint8_t bytes[LIFKD_WINDOW_SIZE];
    int64_t start;
    size_t size;
} lifkd_window;

static bool lifkd_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Returns the 54 header bytes at @p offset (relative to the device), which
 * the caller has already checked lie before @p end.  The window is refilled
 * from @p offset, never past @p end, whenever the header is not wholly
 * inside it. */
static const uint8_t *lifkd_window_header(xx_io_device *device,
                                          lifkd_window *window, int64_t offset,
                                          int64_t end) {
    size_t amount = LIFKD_WINDOW_SIZE;
    if (window->size != 0U && offset >= window->start &&
        offset - window->start <=
            (int64_t)window->size - (int64_t)LIFKD_HEADER_SIZE)
        return window->bytes + (size_t)(offset - window->start);
    if (end - offset < (int64_t)amount) amount = (size_t)(end - offset);
    window->size = 0U;
    if (amount < (size_t)LIFKD_HEADER_SIZE ||
        !lifkd_read_at(device, offset, window->bytes, amount))
        return NULL;
    window->start = offset;
    window->size = amount;
    return window->bytes;
}

/* All samples write lowercase, but nothing in the format forbids uppercase,
 * so both are accepted.  Anything else makes the header invalid. */
static bool lifkd_hex_digit(uint8_t c, uint32_t *value) {
    if (c >= '0' && c <= '9') {
        *value = (uint32_t)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *value = (uint32_t)(c - 'a') + 10U;
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *value = (uint32_t)(c - 'A') + 10U;
        return true;
    }
    return false;
}

static bool lifkd_hex_field(const uint8_t *bytes, unsigned count,
                            uint32_t *result) {
    uint32_t value = 0U;
    unsigned index;
    for (index = 0U; index < count; ++index) {
        uint32_t digit;
        if (!lifkd_hex_digit(bytes[index], &digit)) return false;
        value = (value << 4U) | digit;
    }
    *result = value;
    return true;
}

/*
 * Decode and validate one 54-byte header.  This is the whole per-member
 * structural test, shared by the detector gate and the chain walk so the two
 * can never disagree.  The method is checked first because it is the cheapest
 * rejection for arbitrary data.
 */
static bool lifkd_decode_header(const uint8_t *bytes, lifkd_header *out) {
    uint32_t method, crc_packed, crc_plain;
    size_t length = 0U, index;
    if (!bytes || !out || !lifkd_hex_field(bytes + 32U, 2U, &method) ||
        (method != LIFKD_METHOD_STORED && method != LIFKD_METHOD_LZD) ||
        !lifkd_hex_field(bytes, 8U, &out->dos_time) ||
        !lifkd_hex_field(bytes + 8U, 8U, &out->packed) ||
        !lifkd_hex_field(bytes + 16U, 8U, &out->unpacked) ||
        !lifkd_hex_field(bytes + 24U, 4U, &crc_packed) ||
        !lifkd_hex_field(bytes + 28U, 4U, &crc_plain))
        return false;
    /* A stored member cannot shrink or grow; anything else here means the
     * header is not really a LIF header. */
    if (method == LIFKD_METHOD_STORED && out->packed != out->unpacked)
        return false;
    /* The name is at least one printable ASCII byte, terminated inside the
     * 20-byte field, and everything behind the terminator is zero.  Every
     * member in the corpus (1836 of 1836) has this shape. */
    while (length < LIFKD_NAME_SIZE &&
           bytes[LIFKD_NAME_OFFSET + length] != 0U) {
        uint8_t c = bytes[LIFKD_NAME_OFFSET + length];
        if (c < 0x20U || c > 0x7eU) return false;
        ++length;
    }
    if (length == 0U || length == LIFKD_NAME_SIZE) return false;
    for (index = length; index < LIFKD_NAME_SIZE; ++index)
        if (bytes[LIFKD_NAME_OFFSET + index] != 0U) return false;
    out->crc_packed = (uint16_t)crc_packed;
    out->crc_plain = (uint16_t)crc_plain;
    out->method = (uint8_t)method;
    out->name_length = length;
    return true;
}

bool xx_lifkd_is_member_header(const uint8_t *bytes, size_t size) {
    lifkd_header header;
    return bytes && size >= (size_t)LIFKD_HEADER_SIZE &&
           lifkd_decode_header(bytes, &header);
}

/* The record name is the stored name with DOS backslashes turned into '/'
 * and, in each component, trailing dots and spaces dropped the way DOS and
 * Windows drop them: KDC writes a name without an extension as "MUAD.", and
 * that file is "MUAD" on disk (deark shows it the same way).  A component
 * made only of dots and spaces is left as it is, so that ".." still reads as
 * ".." and lifkd_safe_output_name() refuses it.  Nothing else is rewritten:
 * whether a name is safe to create is decided at extraction time, which
 * refuses rather than repairs. */
static char *lifkd_make_name(const uint8_t *bytes, size_t length) {
    char *name;
    size_t input = 0U, output = 0U;
    if (!bytes || length == 0U || length >= LIFKD_NAME_SIZE) return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (;;) {
        size_t start = input, end, trimmed;
        while (input < length && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        trimmed = end;
        while (trimmed > start &&
               (bytes[trimmed - 1U] == '.' || bytes[trimmed - 1U] == ' '))
            --trimmed;
        if (trimmed == start) trimmed = end;
        while (start < trimmed) name[output++] = (char)bytes[start++];
        if (input >= length) break;
        name[output++] = '/';
        ++input;
    }
    name[output] = 0;
    return name;
}

static char lifkd_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the @p stem bytes at @p text spell the upper-case @p word. */
static bool lifkd_stem_is(const char *text, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || lifkd_upper(text[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* One path component: not empty, not only dots and spaces (so never "." or
 * ".."), not ending in a dot or space (Windows would silently drop those and
 * could merge two members), and not a device name such as CON, LPT1.TXT or
 * CONIN$, in any case, with or without an extension. */
static bool lifkd_safe_component(const char *text, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t index, stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index)
        if (text[index] != '.' && text[index] != ' ') meaningful = true;
    if (!meaningful || text[length - 1U] == '.' || text[length - 1U] == ' ')
        return false;
    while (stem < length && text[stem] != '.') ++stem;
    while (stem > 0U && text[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (lifkd_stem_is(text, stem, devices[index])) return false;
    if (stem == 4U && text[3] >= '0' && text[3] <= '9' &&
        ((lifkd_upper(text[0]) == 'C' && lifkd_upper(text[1]) == 'O' &&
          lifkd_upper(text[2]) == 'M') ||
         (lifkd_upper(text[0]) == 'L' && lifkd_upper(text[1]) == 'P' &&
          lifkd_upper(text[2]) == 'T')))
        return false;
    return true;
}

/* Extraction writes <base>/<name>.  Refused: absolute names, anything with a
 * colon (drive letters, alternate streams), control bytes and the other
 * reserved punctuation, empty components, and every component
 * lifkd_safe_component() refuses. */
static bool lifkd_safe_output_name(const char *name) {
    size_t start = 0U, index = 0U;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    for (;; ++index) {
        unsigned char c = (unsigned char)name[index];
        if (c == 0U || c == '/' || c == '\\') {
            if (!lifkd_safe_component(name + start, index - start))
                return false;
            if (c == 0U) return true;
            start = index + 1U;
            continue;
        }
        if (c < 0x20U || c > 0x7eU || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
}

static void lifkd_stream_free(void *opaque) {
    lifkd_stream *stream = (lifkd_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Geometric growth, so a crafted file with the maximum number of members
 * costs a logarithmic number of reallocations rather than one per member. */
static bool lifkd_add_member(lifkd_stream *stream, const lifkd_member *member) {
    if (!stream || !member || stream->count >= LIFKD_MAX_MEMBERS) return false;
    if (stream->count == stream->capacity) {
        size_t grown_capacity =
            stream->capacity ? stream->capacity * 2U : 16U;
        lifkd_member *grown;
        if (grown_capacity > LIFKD_MAX_MEMBERS)
            grown_capacity = LIFKD_MAX_MEMBERS;
        if (grown_capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (lifkd_member *)xx_mem_realloc(stream->items,
                                               grown_capacity * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = grown_capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/*
 * Walk the whole chain.  Every declared size is checked against the real
 * remaining file size before it is used to step, so a 54-byte header can
 * never move the cursor past end of file.  Success requires landing exactly
 * on the end and finding at least one member.  With @p stream NULL this only
 * validates and counts, and allocates nothing.
 */
static bool lifkd_walk(Abstractformat *format, lifkd_stream *stream,
                       uint64_t *count_out, int64_t *size_out) {
    int64_t total, size, cursor = 0;
    uint64_t count = 0U;
    lifkd_window window;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < LIFKD_HEADER_SIZE) return false;
    window.start = 0;
    window.size = 0U;
    while (cursor < size) {
        const uint8_t *bytes;
        lifkd_header header;
        if (count >= LIFKD_MAX_MEMBERS || size - cursor < LIFKD_HEADER_SIZE ||
            !(bytes = lifkd_window_header(format->device, &window,
                                          format->base_address + cursor,
                                          total)) ||
            !lifkd_decode_header(bytes, &header) ||
            (int64_t)header.packed > size - cursor - LIFKD_HEADER_SIZE)
            return false;
        if (stream) {
            lifkd_member member;
            xx_mem_zero(&member, sizeof(member));
            member.header_offset = format->base_address + cursor;
            member.data_offset = member.header_offset + LIFKD_HEADER_SIZE;
            member.packed_size = (int64_t)header.packed;
            member.unpacked_size = header.unpacked;
            member.dos_time = header.dos_time;
            member.crc_packed = header.crc_packed;
            member.crc_plain = header.crc_plain;
            member.method = header.method;
            member.extractable = true;
            member.name = lifkd_make_name(bytes + LIFKD_NAME_OFFSET,
                                          header.name_length);
            if (!member.name) return false;
            if (!lifkd_add_member(stream, &member)) {
                xx_mem_free(member.name);
                return false;
            }
        }
        ++count;
        cursor += LIFKD_HEADER_SIZE + (int64_t)header.packed;
    }
    if (cursor != size || count == 0U) return false;
    if (count_out) *count_out = count;
    if (size_out) *size_out = size;
    return true;
}

/* ------------------------------------------------------ duplicate names -- */

static uint32_t lifkd_name_hash(const char *name) {
    uint32_t hash = 2166136261U;
    while (*name) {
        hash ^= (uint8_t)lifkd_upper(*name++);
        hash *= 16777619U;
    }
    return hash;
}

static bool lifkd_name_equal(const char *left, const char *right) {
    while (*left && *right && lifkd_upper(*left) == lifkd_upper(*right)) {
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

static size_t lifkd_put_decimal(char *out, uint64_t value) {
    char digits[24];
    size_t count = 0U, index;
    do {
        digits[count++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (index = 0U; index < count; ++index)
        out[index] = digits[count - 1U - index];
    return count;
}

/* "<stem>_<record>[_<try>]<extension>" in the last path component. */
static char *lifkd_renamed(const char *name, uint64_t record, unsigned attempt) {
    size_t length = xx_str_len(name), component = 0U, insert, index, at;
    size_t suffix_length = 0U;
    char suffix[56];
    char *out;
    for (index = 0U; index < length; ++index)
        if (name[index] == '/') component = index + 1U;
    insert = length;
    for (index = length; index > component + 1U; --index) {
        if (name[index - 1U] == '.') {
            insert = index - 1U;
            break;
        }
    }
    suffix[suffix_length++] = '_';
    suffix_length += lifkd_put_decimal(suffix + suffix_length, record);
    if (attempt > 1U) {
        suffix[suffix_length++] = '_';
        suffix_length += lifkd_put_decimal(suffix + suffix_length, attempt);
    }
    out = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!out) return NULL;
    xx_mem_copy(out, name, insert);
    at = insert;
    xx_mem_copy(out + at, suffix, suffix_length);
    at += suffix_length;
    xx_mem_copy(out + at, name + insert, length - insert);
    at += length - insert;
    out[at] = 0;
    return out;
}

/*
 * Names that compare equal ignoring ASCII case would land on the same file
 * on a case-insensitive filesystem, so every later one is renamed to
 * "<stem>_<record number><ext>" (then "_<record>_<n>" if even that is
 * taken).  A member that still collides after LIFKD_RENAME_TRIES is listed
 * but never extracted.  Open addressing over indices keeps this linear.
 */
static bool lifkd_dedupe(lifkd_stream *stream) {
    uint32_t *slots;
    size_t slot_count = 16U, mask, index;
    if (!stream || stream->count < 2U) return true;
    while (slot_count < stream->count * 2U) slot_count *= 2U;
    slots = (uint32_t *)xx_mem_calloc(slot_count, sizeof(*slots));
    if (!slots) return false;
    mask = slot_count - 1U;
    for (index = 0U; index < stream->count; ++index) {
        lifkd_member *member = &stream->items[index];
        char *candidate = member->name;
        unsigned attempt = 0U;
        for (;;) {
            size_t slot = (size_t)lifkd_name_hash(candidate) & mask;
            bool taken = false;
            while (slots[slot] != 0U) {
                if (lifkd_name_equal(stream->items[slots[slot] - 1U].name,
                                     candidate)) {
                    taken = true;
                    break;
                }
                slot = (slot + 1U) & mask;
            }
            if (!taken) {
                if (candidate != member->name) {
                    xx_mem_free(member->name);
                    member->name = candidate;
                }
                slots[slot] = (uint32_t)(index + 1U);
                break;
            }
            if (candidate != member->name) xx_mem_free(candidate);
            if (++attempt > LIFKD_RENAME_TRIES) {
                member->extractable = false;
                break;
            }
            candidate = lifkd_renamed(member->name, (uint64_t)index + 1U,
                                      attempt);
            if (!candidate) {
                xx_mem_free(slots);
                return false;
            }
        }
    }
    xx_mem_free(slots);
    return true;
}

static bool lifkd_parse(Abstractformat *format, lifkd_stream **result) {
    lifkd_stream *stream;
    int64_t size = 0;
    if (!result) return false;
    stream = (lifkd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!lifkd_walk(format, stream, NULL, &size) || !lifkd_dedupe(stream)) {
        lifkd_stream_free(stream);
        return false;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
}

static bool lifkd_copy_options(xx_list_s *destination,
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

static const xx_var *lifkd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool lifkd_set_record(xx_archive_record *record,
                             const lifkd_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = LIFKD_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           /* There is no 16-bit CRC metadata id, so the plaintext CRC-16
            * travels in the CRC32 slot zero extended, the way the other
            * CRC-16 readers in this library report theirs. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc_plain) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          (member->dos_time >> 16U) & 0xffffU) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time & 0xffffU) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* A NULL destination verifies without writing anything. */
static bool lifkd_write_all(xx_io_device *destination, const uint8_t *data,
                            size_t size) {
    size_t written = 0U;
    if (!destination) return true;
    while (written < size) {
        ssize_t amount = xx_io_write(destination, data + written,
                                     size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

/*
 * A stored member is copied in bounded chunks, whatever its size, with both
 * CRC-16 fields checked over the bytes as they pass.  The caller removes the
 * output if this fails.
 */
static bool lifkd_unpack_stored(Abstractformat *format,
                                const lifkd_member *member,
                                xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *chunk;
    int64_t done = 0;
    uint16_t crc = 0xffffU;
    bool ok = true;
    if ((uint64_t)member->packed_size != member->unpacked_size) return false;
    chunk = (uint8_t *)xx_mem_alloc(LIFKD_COPY_CHUNK);
    if (!chunk) return false;
    while (ok && done < member->packed_size) {
        size_t amount = LIFKD_COPY_CHUNK;
        if ((int64_t)amount > member->packed_size - done)
            amount = (size_t)(member->packed_size - done);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !lifkd_read_at(format->device, member->data_offset + done, chunk,
                           amount) ||
            !lifkd_write_all(destination, chunk, amount)) {
            ok = false;
            break;
        }
        crc = xx_crc16_ccitt_calc(crc, chunk, amount);
        done += (int64_t)amount;
    }
    xx_mem_free(chunk);
    return ok && crc == member->crc_packed && crc == member->crc_plain;
}

/*
 * Method 2 is LZD and forwards to the ZOO method 1 decoder, which works in
 * memory, so the plaintext is capped at LIFKD_MAX_PLAIN and must also be
 * reachable from the packed size at all (see LIFKD_LZD_MAX_RUN).
 *
 * One packaging quirk: the KDC encoder sometimes emits a single extra zero
 * byte after the byte that carried the LZD end code, and
 * xx_zoo_lzd_decode_memory() deliberately insists that the input be consumed
 * exactly.  Rather than loosen that decoder for every caller, retry once here
 * with the trailing zero dropped.  Only a trailing ZERO is dropped and only
 * one of them: any other trailing content still fails, and the plaintext
 * CRC-16 below is what actually decides whether the decode was right.  In the
 * corpus this affects 250 of 1622 compressed members.
 */
static bool lifkd_unpack_lzd(Abstractformat *format, const lifkd_member *member,
                             xx_io_device *destination) {
    uint8_t *packed = NULL, *output = NULL;
    size_t input_size, output_size, written = 0U;
    bool ok = false;
    if (member->packed_size <= 0 || member->unpacked_size > LIFKD_MAX_PLAIN ||
        member->unpacked_size >
            ((uint64_t)member->packed_size + 1U) * LIFKD_LZD_MAX_RUN ||
        (uint64_t)member->packed_size > 2U * member->unpacked_size + 64U)
        return false;
    input_size = (size_t)member->packed_size;
    output_size = (size_t)member->unpacked_size;
    /* The packed CRC is checked before the plaintext buffer exists, so a
     * damaged member never gets that allocation. */
    packed = (uint8_t *)xx_mem_alloc(input_size);
    if (!packed ||
        !lifkd_read_at(format->device, member->data_offset, packed,
                       input_size) ||
        xx_crc16_ccitt_calc(0xffffU, packed, input_size) != member->crc_packed)
        goto done;
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!output) goto done;
    if (!xx_zoo_lzd_decode_memory(packed, input_size, output, output_size,
                                  &written) &&
        !(packed[input_size - 1U] == 0U &&
          xx_zoo_lzd_decode_memory(packed, input_size - 1U, output,
                                   output_size, &written)))
        goto done;
    if (written != output_size ||
        xx_crc16_ccitt_calc(0xffffU, output, written) != member->crc_plain)
        goto done;
    ok = lifkd_write_all(destination, output, written);
done:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return ok;
}

void xx_lifkd_init(xx_lifkd *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    /* The header is ASCII hex text, so there is no endianness to speak of;
     * the decoded 17-byte header is read most significant nibble first. */
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_LIFKD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-kdc-lif");
    xx_format_set_extension(&archive->format, "lif");
    archive->format.check_is_valid = xx_lifkd_check_is_valid;
    archive->format.handle_base_info = xx_lifkd_handle_base_info;
    archive->format.get_format_size = xx_lifkd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lifkd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lifkd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lifkd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lifkd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lifkd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lifkd_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_lifkd *xx_lifkd_create(xx_io_device *device, int64_t base_address) {
    xx_lifkd *archive = (xx_lifkd *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lifkd_init(archive, device, base_address);
    return archive;
}

void xx_lifkd_destroy(xx_lifkd *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_lifkd_free(xx_lifkd *archive) {
    if (!archive) return;
    xx_lifkd_destroy(archive);
    xx_mem_free(archive);
}

bool xx_lifkd_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    (void)pd;
    return lifkd_walk(format, NULL, NULL, NULL);
}

bool xx_lifkd_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    uint64_t count = 0U;
    int64_t size = 0;
    xx_lifkd *archive;
    (void)pd;
    if (!format || !lifkd_walk(format, NULL, &count, &size)) return false;
    archive = (xx_lifkd *)format;
    archive->number_of_records = count;
    archive->archive_end = format->base_address + size;
    format->number_of_archive_records = count;
    format->format_size = size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_lifkd_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lifkd_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_lifkd_get_number_of_archive_records(Abstractformat *format,
                                                xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lifkd_handle_base_info(format, pd))
               ? ((xx_lifkd *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_lifkd_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    lifkd_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!lifkd_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        lifkd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = lifkd_stream_free;
    state->total_records = stream->count;
    if (!lifkd_copy_options(&state->options, options) ||
        !lifkd_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_lifkd_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_lifkd_archive_record_move_to_next(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    lifkd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (lifkd_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = lifkd_set_record(&state->current_record,
                                         &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lifkd_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    lifkd_stream *stream;
    lifkd_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (lifkd_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!member->extractable || !lifkd_safe_output_name(member->name))
        return false;
    path_option = lifkd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: the member is still decoded and verified end to
         * end, just not written anywhere. */
        return member->method == LIFKD_METHOD_STORED
                   ? lifkd_unpack_stored(format, member, NULL, pd)
                   : lifkd_unpack_lzd(format, member, NULL);
    }
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    result = member->method == LIFKD_METHOD_STORED
                 ? lifkd_unpack_stored(format, member, destination, pd)
                 : lifkd_unpack_lzd(format, member, destination);
    if (xx_io_close(destination) != 0) result = false;
    if (!result && created) xx_rt_remove(path);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_lifkd_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
