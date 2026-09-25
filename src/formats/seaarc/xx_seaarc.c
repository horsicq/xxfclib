/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SEA ARC archives, the original System Enhancement Associates
 * container, also written by PKPAK/PKARC and by NoGate PAK.
 *
 * There is no file header and no central directory: the archive is a chain
 * of member records read front to back, each one located by the size of the
 * one before it.
 *
 *   member record, 25 or 29 bytes, then the payload:
 *     0x00  u8   0x1A, the DOS EOF byte, used here as the record marker
 *     0x01  u8   compression method
 *     0x02  char name[13], NUL terminated, the tail zero filled
 *     0x0F  u32 LE compressed size
 *     0x13  u16 LE DOS date
 *     0x15  u16 LE DOS time
 *     0x17  u16 LE CRC-16/ARC of the plaintext
 *     0x19  u32 LE original size   -- ONLY when method != 1
 *
 *   The header length is a function of the method, not a constant: method 1
 *   (the original "stored") predates the original-size field and so uses a
 *   25 byte header, while every later method uses 29. Budgeting a flat 29
 *   misparses method-1 archives that period tools still list correctly.
 *
 *   end record, 2 bytes: 0x1A 0x00. Method 0 exists only as this marker; it
 *   never carries a name or a payload. The archive ends there, and anything
 *   after it is overlay (PAK 2.x appends its 0xFE extended records -- remarks,
 *   original directories, a security envelope -- there; they are not read,
 *   and members are extracted flat, as ARC, PAK without /PATH, unar and U3
 *   all do).
 *
 * Compression methods, each decoded here:
 *     1   stored, old (no original-size field)
 *     2   stored
 *     3   packed        -- 0x90 run-length only
 *     4   squeezed      -- Huffman node table, then 0x90 run-length
 *     5   crunched      -- 12-bit hash-table LZW, old hash, no run-length
 *     6   crunched      -- 12-bit hash-table LZW, old hash, + run-length
 *     7   crunched      -- 12-bit hash-table LZW, new hash, + run-length
 *     8   crunched      -- dynamic-width LZW (leading max-bits byte) + RLE
 *     9   squashed      -- dynamic-width 13-bit LZW, no run-length
 *     10  crushed       -- PAK 2.x adaptive LZW with LRU reuse, + run-length
 *     11  distilled     -- PAK 2.x static Huffman + 8 KiB LZ77
 *     0x7f compressed   -- Unix compress LZW, leading flag byte
 *
 * Method 10 is also ARC 7's "trimmed" (LH1-style); only the PAK meaning is
 * decoded, and a trimmed member fails the crushed decode (and the CRC), so
 * it is refused rather than extracted as garbage.
 *
 * Every decoded member is checked against the header's CRC-16/ARC (poly
 * 0xA001 reflected, init 0) and its declared original size. Either mismatch
 * is a failure, never a partial success.
 *
 * Ported code (MIT, same author and licence as this file):
 *   - methods 5/6/7, 8/9/0x7f and 4 follow XArchive Algos/xarcdecoder.cpp
 *     (arcHashInsert/arcDecodeHash, ArcCodeReader/arcDecodeLzw,
 *     arcDecodeSqueeze);
 *   - methods 10/11 follow XArchive Algos/xpakdecoder.cpp (CrushedState/
 *     decodeCrushed, decodeDistilled), which was itself cross-checked
 *     against unarc-rs by Mike Krueger (used under its MIT option).
 *   Both were rewritten in C over an in-memory buffer; the state machines
 *   and bounds are unchanged.
 *
 * The container carries no self-identifying magic whatsoever: byte 0 is
 * 0x1A and byte 1 is a small integer. That pair occurs constantly inside
 * executables and inside compressed data, so detection rests entirely on
 * walking the whole chain and landing exactly on a 1A 00 end record.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/seaarc/xx_seaarc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

typedef struct xx_seaarc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    uint16_t crc;
    bool is_folder;
} xx_seaarc_member;

typedef struct xx_seaarc_stream_s {
    xx_seaarc_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_seaarc_stream;

static void xx_seaarc_vtable_destroy(Abstractformat *self);

#define XX_SEAARC_MARKER 0x1AU
#define XX_SEAARC_NAME_OFFSET 2
#define XX_SEAARC_NAME_SIZE 13
#define XX_SEAARC_HEADER_SIZE_OLD 25
#define XX_SEAARC_HEADER_SIZE 29
#define XX_SEAARC_END_SIZE 2
#define XX_SEAARC_METHOD_END 0U
#define XX_SEAARC_METHOD_STORE_OLD 1U
#define XX_SEAARC_METHOD_STORE 2U
#define XX_SEAARC_METHOD_PACKED 3U
#define XX_SEAARC_METHOD_SQUEEZED 4U
#define XX_SEAARC_METHOD_CRUNCHED1 5U
#define XX_SEAARC_METHOD_CRUNCHED2 6U
#define XX_SEAARC_METHOD_CRUNCHED3 7U
#define XX_SEAARC_METHOD_CRUNCHED4 8U
#define XX_SEAARC_METHOD_SQUASHED 9U
#define XX_SEAARC_METHOD_CRUSHED 10U
#define XX_SEAARC_METHOD_DISTILLED 11U
#define XX_SEAARC_METHOD_COMPRESSED 0x7FU
/* ARC never stored more than a few thousand members; this is a sanity cap on
 * a chain walk whose length is not declared anywhere. */
#define XX_SEAARC_MAX_MEMBERS 65536
/* Both sizes are attacker-controlled u32 fields: refuse rather than attempt
 * the work. */
#define XX_SEAARC_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* ------------------------------------------------------------- helpers -- */

static bool xx_seaarc_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_seaarc_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

static uint32_t xx_seaarc_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_seaarc_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static char xx_seaarc_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the first @p stem bytes of @p name spell the upper-case
 * @p word exactly, ignoring case. */
static bool xx_seaarc_stem_is(const char *name, size_t stem,
                              const char *word) {
    size_t index;

    for (index = 0U; index < stem; ++index) {
        if (!word[index] || xx_seaarc_upper(name[index]) != word[index]) {
            return false;
        }
    }
    return word[stem] == 0;
}

/* Extraction writes <base>/<name>. The parser already refused separators,
 * drive colons and control bytes; this also refuses names that Windows would
 * resolve to "." or ".." (only dots and spaces), the other reserved
 * punctuation, and device names such as CON, LPT1.EXT or CONIN$, with or
 * without an extension and in any case. */
static bool xx_seaarc_output_name_safe(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length, stem = 0U, index;
    bool meaningful = false;

    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*') {
            return false;
        }
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        if (xx_seaarc_stem_is(name, stem, devices[index])) return false;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((xx_seaarc_upper(name[0]) == 'C' && xx_seaarc_upper(name[1]) == 'O' &&
          xx_seaarc_upper(name[2]) == 'M') ||
         (xx_seaarc_upper(name[0]) == 'L' && xx_seaarc_upper(name[1]) == 'P' &&
          xx_seaarc_upper(name[2]) == 'T'))) {
        return false;
    }
    return true;
}

static void xx_seaarc_stream_free(void *pointer) {
    xx_seaarc_stream *stream = (xx_seaarc_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of its name. Geometric growth keeps a
 * 65536-member chain from costing 65536 reallocations. */
static bool xx_seaarc_add(xx_seaarc_stream *stream,
                          const xx_seaarc_member *member) {
    if (stream->count == stream->capacity) {
        size_t grown_capacity =
            stream->capacity ? stream->capacity * 2U : (size_t)16U;
        xx_seaarc_member *grown;
        if (grown_capacity > (size_t)XX_SEAARC_MAX_MEMBERS) {
            grown_capacity = (size_t)XX_SEAARC_MAX_MEMBERS;
        }
        if (grown_capacity <= stream->count) return false;
        grown = (xx_seaarc_member *)xx_mem_realloc(
            stream->items, sizeof(*grown) * grown_capacity);
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = grown_capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Method 0 is the end record only; it is deliberately not "valid" here. */
static bool xx_seaarc_method_valid(uint8_t method) {
    return (method >= XX_SEAARC_METHOD_STORE_OLD &&
            method <= XX_SEAARC_METHOD_DISTILLED) ||
           method == XX_SEAARC_METHOD_COMPRESSED;
}

static int64_t xx_seaarc_header_size(uint8_t method) {
    return method == XX_SEAARC_METHOD_STORE_OLD ? XX_SEAARC_HEADER_SIZE_OLD
                                                : XX_SEAARC_HEADER_SIZE;
}

/* The 13 byte field holds a bare DOS 8.3 name -- the container has no notion
 * of directories, so a separator in it means this is not an ARC header. */
static char *xx_seaarc_make_name(const uint8_t *field) {
    char *name;
    size_t length = 0U;
    size_t index;

    /* A leading space or a leading NUL is what an arbitrary 0x1A <small int>
     * pair in unrelated data most often produces; requiring a printable
     * non-blank first byte is one of the few per-record gates this format
     * offers, so it is stricter than the 0x20..0x7E rule used below. */
    if (field[0] < 0x21U || field[0] > 0x7EU) return NULL;
    while (length < (size_t)XX_SEAARC_NAME_SIZE && field[length] != 0U) {
        ++length;
    }
    for (index = 0U; index < length; ++index) {
        /* ARC is a DOS-only format and stores no high-bit or control bytes
         * in this field. */
        if (field[index] < 0x20U || field[index] > 0x7EU) return NULL;
        if (field[index] == (uint8_t)'/' || field[index] == (uint8_t)'\\' ||
            field[index] == (uint8_t)':') {
            return NULL;
        }
    }
    /* A name may fill all 13 bytes with no terminator: PKPAK wrote the field
     * that way, so an unterminated name is accepted, not rejected. */
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) name[index] = (char)field[index];
    name[length] = '\0';
    return name;
}

/* ------------------------------------------------------ duplicate names -- */

typedef struct xx_seaarc_name_key_s {
    const char *name;
    uint32_t item;
} xx_seaarc_name_key;

/* Length of the part of @p name that names the file on Windows, which drops
 * trailing dots and spaces: "FOO", "FOO." and "FOO " are one file there. */
static size_t xx_seaarc_key_length(const char *name) {
    size_t length = xx_str_len(name);

    while (length > 0U &&
           (name[length - 1U] == '.' || name[length - 1U] == ' ')) {
        --length;
    }
    return length;
}

/* Orders names as the file system that receives them would tell them apart:
 * ignoring case and trailing dots and spaces. */
static int xx_seaarc_compare_names(const char *x, const char *y) {
    size_t x_length = xx_seaarc_key_length(x);
    size_t y_length = xx_seaarc_key_length(y);
    size_t index;

    for (index = 0U; index < x_length && index < y_length; ++index) {
        unsigned char cx = (unsigned char)xx_seaarc_upper(x[index]);
        unsigned char cy = (unsigned char)xx_seaarc_upper(y[index]);
        if (cx != cy) return cx < cy ? -1 : 1;
    }
    if (x_length != y_length) return x_length < y_length ? -1 : 1;
    return 0;
}

static int xx_seaarc_compare_keys(const void *left, const void *right) {
    const xx_seaarc_name_key *a = (const xx_seaarc_name_key *)left;
    const xx_seaarc_name_key *b = (const xx_seaarc_name_key *)right;
    int order = xx_seaarc_compare_names(a->name, b->name);

    if (order != 0) return order;
    /* Equal names keep archive order, so the first member keeps its name. */
    return a->item < b->item ? -1 : (a->item > b->item ? 1 : 0);
}

static bool xx_seaarc_names_equal(const char *x, const char *y) {
    return xx_seaarc_compare_names(x, y) == 0;
}

/* "NAME.EXT" -> "NAME_<n>.EXT" (the suffix goes before the last dot). The
 * trailing dots and spaces Windows would drop are dropped first, so "FOO."
 * becomes "FOO_1", not "FOO_1." or "FOO._1". */
static char *xx_seaarc_suffixed_name(const char *name, uint32_t number) {
    char digits[12];
    size_t digit_count = 0U;
    size_t length = xx_seaarc_key_length(name);
    size_t dot = length;
    size_t index;
    size_t at = 0U;
    char *result;

    for (index = 0U; index < length; ++index) {
        if (name[index] == '.') dot = index;
    }
    do {
        digits[digit_count++] = (char)('0' + (number % 10U));
        number /= 10U;
    } while (number != 0U && digit_count < sizeof(digits));
    result = (char *)xx_mem_alloc(length + digit_count + 2U);
    if (!result) return NULL;
    for (index = 0U; index < dot; ++index) result[at++] = name[index];
    result[at++] = '_';
    while (digit_count != 0U) result[at++] = digits[--digit_count];
    for (index = dot; index < length; ++index) result[at++] = name[index];
    result[at] = '\0';
    return result;
}

/* An open-addressing set of names under the comparison above. It holds
 * pointers to member names and never owns them. */
typedef struct xx_seaarc_name_set_s {
    const char **slots;
    size_t mask;
} xx_seaarc_name_set;

static size_t xx_seaarc_name_hash(const char *name) {
    size_t length = xx_seaarc_key_length(name);
    size_t index;
    uint32_t hash = 2166136261U;

    for (index = 0U; index < length; ++index) {
        hash ^= (uint32_t)(unsigned char)xx_seaarc_upper(name[index]);
        hash *= 16777619U;
    }
    return (size_t)hash;
}

static bool xx_seaarc_name_set_init(xx_seaarc_name_set *set, size_t items) {
    size_t capacity = 16U;

    /* At most two names per member go in (its own and one replacement), so
     * four slots per member keep the table at most half full. */
    while (capacity < items * 4U) capacity *= 2U;
    set->slots = (const char **)xx_mem_alloc(capacity * sizeof(*set->slots));
    if (!set->slots) return false;
    xx_mem_zero((void *)set->slots, capacity * sizeof(*set->slots));
    set->mask = capacity - 1U;
    return true;
}

/* Returns true when an equal name is already present; otherwise inserts
 * @p name when @p insert is set. */
static bool xx_seaarc_name_set_probe(xx_seaarc_name_set *set,
                                     const char *name, bool insert) {
    size_t slot = xx_seaarc_name_hash(name) & set->mask;

    while (set->slots[slot]) {
        if (xx_seaarc_names_equal(set->slots[slot], name)) return true;
        slot = (slot + 1U) & set->mask;
    }
    if (insert) set->slots[slot] = name;
    return false;
}

/* Windows gives a name that is not a plain 8.3 name a second, 8.3 alias
 * such as ABCDEF~1.TXT, and opening that alias opens the long-named file. */
static bool xx_seaarc_is_short_name(const char *name) {
    size_t base = 0U, extension = 0U;
    bool dot = false;

    for (; *name; ++name) {
        char c = *name;
        if (c == '.') {
            if (dot) return false;
            dot = true;
            continue;
        }
        if (c == ' ' || c == '+' || c == ',' || c == ';' || c == '=' ||
            c == '[' || c == ']') {
            return false;
        }
        if (dot ? ++extension > 3U : ++base > 8U) return false;
    }
    return base != 0U;
}

/* One renaming pass: the first member (in archive order) of every group of
 * equal names keeps its name, and every later one becomes NAME_<n>.EXT with
 * the smallest n that matches no stored name and no name given out so far.
 * A rejected candidate always equals one of those at most 2 * count names,
 * and each of them can reject only one (group, n) pair, so the search is
 * linear overall; the bound below only guards that argument. */
static bool xx_seaarc_rename_duplicates(xx_seaarc_stream *stream) {
    xx_seaarc_name_key *keys;
    xx_seaarc_name_set set;
    size_t index;
    size_t group = 0U;
    size_t budget;
    uint32_t number = 1U;
    bool result = false;

    if (stream->count < 2U) return true;
    keys = (xx_seaarc_name_key *)xx_mem_alloc(stream->count * sizeof(*keys));
    if (!keys) return false;
    if (!xx_seaarc_name_set_init(&set, stream->count)) {
        xx_mem_free(keys);
        return false;
    }
    budget = stream->count * 4U + 16U;
    for (index = 0U; index < stream->count; ++index) {
        keys[index].name = stream->items[index].name;
        keys[index].item = (uint32_t)index;
    }
    xx_rt_qsort(keys, stream->count, sizeof(*keys), xx_seaarc_compare_keys);
    /* In sorted order the first member of each group is the one inserted, so
     * the set never points at a name that is about to be replaced. */
    for (index = 0U; index < stream->count; ++index) {
        (void)xx_seaarc_name_set_probe(&set, keys[index].name, true);
    }
    for (index = 1U; index < stream->count; ++index) {
        xx_seaarc_member *member;
        char *replacement = NULL;
        if (!xx_seaarc_names_equal(keys[index].name, keys[group].name)) {
            group = index;
            number = 1U;
            continue;
        }
        member = &stream->items[keys[index].item];
        for (;;) {
            if (budget == 0U || number == 0xFFFFFFFFU) goto done;
            --budget;
            replacement = xx_seaarc_suffixed_name(member->name, number++);
            if (!replacement) goto done;
            if (!xx_seaarc_name_set_probe(&set, replacement, true)) break;
            xx_str_free(replacement);
        }
        xx_str_free(member->name);
        member->name = replacement;
        keys[index].name = NULL;
    }
    result = true;

done:
    xx_mem_free((void *)set.slots);
    xx_mem_free(keys);
    return result;
}

/* ARC names are flat 8.3 names, and an archive can still hold one twice (PAK
 * records the original directories only in trailer records that are not
 * applied here). Every later member of such a group -- compared ignoring
 * case and trailing dots and spaces, as DOS and Windows do -- is renamed so
 * extraction never overwrites an earlier one. When the names then include a
 * long (non-8.3) one, a '~' elsewhere could spell its Windows alias, so every
 * '~' becomes '_' and the renaming runs once more; its new names have no '~'
 * and the archive already has a long name, so nothing further can arise. */
static bool xx_seaarc_make_names_unique(xx_seaarc_stream *stream) {
    bool has_long = false;
    bool has_tilde = false;
    size_t index;

    if (!xx_seaarc_rename_duplicates(stream)) return false;
    for (index = 0U; index < stream->count; ++index) {
        const char *name = stream->items[index].name;
        if (!xx_seaarc_is_short_name(name)) has_long = true;
        for (; *name; ++name) {
            if (*name == '~') has_tilde = true;
        }
    }
    if (!has_long || !has_tilde) return true;
    for (index = 0U; index < stream->count; ++index) {
        char *name = stream->items[index].name;
        for (; *name; ++name) {
            if (*name == '~') *name = '_';
        }
    }
    return xx_seaarc_rename_duplicates(stream);
}

/* --------------------------------------------------------------- parse -- */

static xx_seaarc_stream *xx_seaarc_parse(Abstractformat *self,
                                         xx_pd_struct *pd,
                                         bool unique_names) {
    xx_seaarc_stream *stream;
    uint8_t header[XX_SEAARC_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset = 0;
    bool terminated = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The shortest possible archive is one method-1 member with an empty
     * payload plus the end record. */
    if (span < XX_SEAARC_HEADER_SIZE_OLD + XX_SEAARC_END_SIZE) return NULL;

    stream = (xx_seaarc_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (!terminated) {
        xx_seaarc_member member;
        char *name;
        uint8_t method;
        int64_t header_size;
        int64_t data_offset;
        int64_t compressed_size;
        int64_t uncompressed_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_SEAARC_MAX_MEMBERS) goto fail;
        if (!xx_seaarc_range_within(span, offset, XX_SEAARC_END_SIZE) ||
            !xx_seaarc_read_at(self, self->base_address + offset, header,
                               (size_t)XX_SEAARC_END_SIZE)) {
            goto fail;
        }
        /* Every record, the end record included, starts with the marker. A
         * chain that drifts off the record boundaries fails here rather than
         * silently resynchronising on the next 0x1A. */
        if (header[0] != XX_SEAARC_MARKER) goto fail;
        method = header[1];
        if (method == XX_SEAARC_METHOD_END) {
            offset += XX_SEAARC_END_SIZE;
            terminated = true;
            break;
        }
        if (!xx_seaarc_method_valid(method)) goto fail;

        header_size = xx_seaarc_header_size(method);
        if (!xx_seaarc_range_within(span, offset, header_size) ||
            !xx_seaarc_read_at(self, self->base_address + offset, header,
                               (size_t)header_size)) {
            goto fail;
        }

        compressed_size = (int64_t)xx_seaarc_le32(header + 15);
        if (method == XX_SEAARC_METHOD_STORE_OLD) {
            /* The old header has no original-size field at all; for a stored
             * member the two lengths are the same by definition. */
            uncompressed_size = compressed_size;
        } else {
            uncompressed_size = (int64_t)xx_seaarc_le32(header + 25);
        }

        data_offset = offset + header_size;
        /* A member whose payload runs past EOF is a rejection, not a
         * truncated-but-listable member. Without this the chain walk would
         * read the end marker out of unrelated trailing bytes. */
        if (!xx_seaarc_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }
        /* The stored methods are the only ones whose two lengths must agree,
         * and making them agree here means decode can copy without having to
         * decide which length to trust. */
        if ((method == XX_SEAARC_METHOD_STORE_OLD ||
             method == XX_SEAARC_METHOD_STORE) &&
            compressed_size != uncompressed_size) {
            goto fail;
        }

        name = xx_seaarc_make_name(header + XX_SEAARC_NAME_OFFSET);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = header_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = (uint32_t)method;
        member.crc = xx_seaarc_le16(header + 23);
        /* Date at +0x13, time at +0x15, published as the usual packed dword. */
        member.timestamp = ((uint64_t)xx_seaarc_le16(header + 19) << 16) |
                           (uint64_t)xx_seaarc_le16(header + 21);
        member.is_folder = false;
        if (!xx_seaarc_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        offset = data_offset + compressed_size;
    }

    /* Two rules that together are this format's ONLY defence against a false
     * positive, because it has no magic: the chain must end on a real 1A 00
     * end record (reaching EOF or an unrelated byte is not a substitute), and
     * it must have carried at least one member. A bare "1A 00" pair, or a
     * chain that merely happens to run out of file, is not an archive. */
    if (!terminated) goto fail;
    if (stream->count == 0U) goto fail;
    if (unique_names && !xx_seaarc_make_names_unique(stream)) goto fail;
    stream->archive_size = offset;
    return stream;

fail:
    xx_seaarc_stream_free(stream);
    return NULL;
}

/* ============================================================ decoding === */

/* Output side shared by every method: a buffer that grows on demand up to the
 * member's declared original size (so a bogus 256 MiB declaration on a tiny
 * payload never allocates 256 MiB), plus ARC's optional 0x90 run-length
 * stage. Producing more than the declared size is a failure. */
typedef struct xx_seaarc_sink_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
    size_t limit;
    bool run_length;
    bool in_repeat;
    uint8_t last;
    bool failed;
} xx_seaarc_sink;

#define XX_SEAARC_DLE 0x90U
#define XX_SEAARC_SINK_MIN (64U * 1024U)

static bool xx_seaarc_sink_init(xx_seaarc_sink *sink, size_t limit,
                                size_t hint, bool run_length) {
    size_t capacity = hint < XX_SEAARC_SINK_MIN ? XX_SEAARC_SINK_MIN : hint;

    xx_mem_zero(sink, sizeof(*sink));
    if (capacity > limit) capacity = limit;
    if (capacity == 0U) capacity = 1U;
    sink->data = (uint8_t *)xx_mem_alloc(capacity);
    if (!sink->data) return false;
    sink->capacity = capacity;
    sink->limit = limit;
    sink->run_length = run_length;
    return true;
}

static bool xx_seaarc_sink_complete(const xx_seaarc_sink *sink) {
    return !sink->failed && sink->size == sink->limit;
}

static bool xx_seaarc_sink_emit(xx_seaarc_sink *sink, uint8_t value) {
    if (sink->failed) return false;
    if (sink->size >= sink->limit) {
        sink->failed = true;
        return false;
    }
    if (sink->size == sink->capacity) {
        size_t grown = sink->capacity > sink->limit / 2U ? sink->limit
                                                         : sink->capacity * 2U;
        uint8_t *data = (uint8_t *)xx_mem_realloc(sink->data, grown);
        if (!data) {
            sink->failed = true;
            return false;
        }
        sink->data = data;
        sink->capacity = grown;
    }
    sink->data[sink->size++] = value;
    return true;
}

/* One byte out of the entropy stage. "90 00" is a literal 0x90 and -- as
 * ARC 5.21's own decoder does, measured with arc.exe -- does not become the
 * byte a following run repeats; "90 nn" repeats the previous literal so it
 * appears nn times in all. */
static bool xx_seaarc_sink_put(xx_seaarc_sink *sink, uint8_t value) {
    if (!sink->run_length) return xx_seaarc_sink_emit(sink, value);
    if (sink->in_repeat) {
        uint32_t count;
        sink->in_repeat = false;
        if (value == 0U) return xx_seaarc_sink_emit(sink, XX_SEAARC_DLE);
        if (sink->size == 0U) {
            /* A run before any literal has nothing to repeat. */
            sink->failed = true;
            return false;
        }
        for (count = 1U; count < value; ++count) {
            if (!xx_seaarc_sink_emit(sink, sink->last)) return false;
        }
        return true;
    }
    if (value == XX_SEAARC_DLE) {
        sink->in_repeat = true;
        return true;
    }
    sink->last = value;
    return xx_seaarc_sink_emit(sink, value);
}

/* Input side: a bounded byte cursor with an LSB-first bit accumulator (every
 * bit-oriented ARC/PAK stage reads the low bit of each byte first). */
typedef struct xx_seaarc_source_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint64_t bits;
    uint32_t bit_count;
} xx_seaarc_source;

static void xx_seaarc_source_init(xx_seaarc_source *source,
                                  const uint8_t *data, size_t size) {
    xx_mem_zero(source, sizeof(*source));
    source->data = data;
    source->size = size;
}

static bool xx_seaarc_source_byte(xx_seaarc_source *source, uint8_t *value) {
    if (source->position >= source->size) return false;
    *value = source->data[source->position++];
    return true;
}

static bool xx_seaarc_source_bits(xx_seaarc_source *source, uint32_t count,
                                  uint32_t *value) {
    if (count > 24U) return false;
    if (count == 0U) {
        *value = 0U;
        return true;
    }
    while (source->bit_count < count) {
        uint8_t byte;
        if (!xx_seaarc_source_byte(source, &byte)) return false;
        source->bits |= (uint64_t)byte << source->bit_count;
        source->bit_count += 8U;
    }
    *value = (uint32_t)(source->bits & ((UINT64_C(1) << count) - 1U));
    source->bits >>= count;
    source->bit_count -= count;
    return true;
}

static bool xx_seaarc_stopped(xx_pd_struct *pd, uint32_t *tick) {
    if (((++*tick) & 0xFFFU) != 0U) return false;
    return pd && xx_pd_is_stopped(pd);
}

/* ------------------------------------------------ method 3: packed -- */

static bool xx_seaarc_decode_packed(xx_seaarc_source *source,
                                    xx_seaarc_sink *sink, xx_pd_struct *pd) {
    uint32_t tick = 0U;

    while (!xx_seaarc_sink_complete(sink)) {
        uint8_t byte;
        if (xx_seaarc_stopped(pd, &tick)) return false;
        if (!xx_seaarc_source_byte(source, &byte)) break;
        if (!xx_seaarc_sink_put(sink, byte)) return false;
    }
    return true;
}

/* ---------------------------------------------- method 4: squeezed -- */

/* Greenlaw's squeeze: a node table of signed 16-bit child pairs, where a
 * non-negative child is the next node and a negative one is the leaf
 * -(child + 1); leaf 256 ends the stream. ARC does not always emit that end
 * leaf, so running out of bits once the declared size is reached is also a
 * valid end -- the size and CRC checks decide. */
#define XX_SEAARC_SQUEEZE_VALUES 257
#define XX_SEAARC_SQUEEZE_EOF 256

static bool xx_seaarc_decode_squeezed(xx_seaarc_source *source,
                                      xx_seaarc_sink *sink, xx_pd_struct *pd) {
    int16_t children[XX_SEAARC_SQUEEZE_VALUES * 2];
    uint8_t low, high;
    uint32_t node_count;
    uint32_t index;
    uint32_t tick = 0U;

    if (!xx_seaarc_source_byte(source, &low) ||
        !xx_seaarc_source_byte(source, &high)) {
        return false;
    }
    node_count = (uint32_t)low | ((uint32_t)high << 8);
    if (node_count > (uint32_t)XX_SEAARC_SQUEEZE_VALUES) return false;
    for (index = 0U; index < node_count * 2U; ++index) {
        int32_t child;
        if (!xx_seaarc_source_byte(source, &low) ||
            !xx_seaarc_source_byte(source, &high)) {
            return false;
        }
        child = (int32_t)(int16_t)((uint16_t)low | ((uint16_t)high << 8));
        if (child >= (int32_t)node_count) return false;
        if (child < 0 && -(child + 1) > XX_SEAARC_SQUEEZE_EOF) return false;
        children[index] = (int16_t)child;
    }
    if (node_count == 0U) return sink->limit == 0U;

    while (!xx_seaarc_sink_complete(sink)) {
        int32_t node = 0;
        uint32_t guard = 0U;
        bool out_of_input = false;
        int32_t value;

        if (xx_seaarc_stopped(pd, &tick)) return false;
        for (;;) {
            uint32_t bit;
            if (!xx_seaarc_source_bits(source, 1U, &bit)) {
                out_of_input = true;
                break;
            }
            node = children[(uint32_t)node * 2U + bit];
            if (node < 0) break;
            if (++guard > (uint32_t)XX_SEAARC_SQUEEZE_VALUES) return false;
        }
        if (out_of_input) break;
        value = -(node + 1);
        if (value == XX_SEAARC_SQUEEZE_EOF) break;
        if (!xx_seaarc_sink_put(sink, (uint8_t)value)) return false;
    }
    return true;
}

/* ---------------------------------------- methods 5, 6, 7: crunched -- */

/* ARC's ORIGINAL crunch. Codes are 12-bit slot numbers of a 4096-entry hash
 * table, packed two to three bytes (the high byte, then a shared nybble).
 * The 256 atomic strings are themselves inserted by hashing, so a literal's
 * code is not its byte value, and there is no CLEAR. Methods 5 and 6 hash
 * with the "mid-square" function, method 7 with ARC 4.6's multiplicative one.
 * On a collision the new string goes to the first free slot at or after
 * (end of the colliding chain + 101), and is linked onto that chain. */
#define XX_SEAARC_HASH_SIZE 4096U
#define XX_SEAARC_HASH_MASK 0xFFFU
#define XX_SEAARC_HASH_NONE (-1)

typedef struct xx_seaarc_hash_entry_s {
    uint16_t next;
    int16_t prefix;
    uint8_t suffix;
    uint8_t occupied;
} xx_seaarc_hash_entry;

static int32_t xx_seaarc_hash_insert(xx_seaarc_hash_entry *table,
                                     uint32_t method, int16_t prefix,
                                     uint8_t suffix) {
    uint32_t sum = (uint32_t)((int32_t)prefix + (int32_t)suffix);
    uint32_t slot;

    if (method == XX_SEAARC_METHOD_CRUNCHED3) {
        slot = (sum * 15073U) & XX_SEAARC_HASH_MASK;
    } else {
        uint32_t folded = (sum & 0xFFFFU) | 0x800U;
        slot = ((folded * folded) >> 6) & XX_SEAARC_HASH_MASK;
    }
    if (table[slot].occupied) {
        uint32_t tail = slot;
        uint32_t steps = 0U;
        while (table[tail].next != 0U) {
            tail = table[tail].next;
            if (tail >= XX_SEAARC_HASH_SIZE || ++steps >= XX_SEAARC_HASH_SIZE) {
                return XX_SEAARC_HASH_NONE;
            }
        }
        slot = (tail + 101U) & XX_SEAARC_HASH_MASK;
        steps = 0U;
        while (table[slot].occupied) {
            if (++steps >= XX_SEAARC_HASH_SIZE) return XX_SEAARC_HASH_NONE;
            slot = (slot + 1U) & XX_SEAARC_HASH_MASK;
        }
        table[tail].next = (uint16_t)slot;
    }
    table[slot].occupied = 1U;
    table[slot].next = 0U;
    table[slot].prefix = prefix;
    table[slot].suffix = suffix;
    return (int32_t)slot;
}

/* -1: clean end of input; -2: a lone trailing byte (half a code pair). */
static int32_t xx_seaarc_nybble_code(xx_seaarc_source *source,
                                     int32_t *pending) {
    uint8_t first, second;

    if (!xx_seaarc_source_byte(source, &first)) return -1;
    if (*pending >= 0) {
        int32_t code = (*pending << 8) | (int32_t)first;
        *pending = -1;
        return code;
    }
    if (!xx_seaarc_source_byte(source, &second)) return -2;
    *pending = (int32_t)(second & 0x0FU);
    return ((int32_t)first << 4) | (int32_t)(second >> 4);
}

static bool xx_seaarc_decode_hashed(xx_seaarc_source *source,
                                    xx_seaarc_sink *sink, uint32_t method,
                                    xx_pd_struct *pd) {
    xx_seaarc_hash_entry *table;
    uint8_t *stack;
    int32_t pending = -1;
    int32_t previous;
    int32_t value;
    uint32_t remaining = XX_SEAARC_HASH_SIZE - 256U;
    uint32_t tick = 0U;
    uint8_t first;
    bool result = false;

    table = (xx_seaarc_hash_entry *)xx_mem_alloc(XX_SEAARC_HASH_SIZE *
                                                 sizeof(*table));
    stack = (uint8_t *)xx_mem_alloc(XX_SEAARC_HASH_SIZE);
    if (!table || !stack) goto done;
    xx_mem_zero(table, XX_SEAARC_HASH_SIZE * sizeof(*table));
    for (value = 0; value < 256; ++value) {
        if (xx_seaarc_hash_insert(table, method, XX_SEAARC_HASH_NONE,
                                  (uint8_t)value) < 0) {
            goto done;
        }
    }
    previous = xx_seaarc_nybble_code(source, &pending);
    if (previous == -1) {
        result = true;
        goto done;
    }
    if (previous < 0 || !table[previous].occupied ||
        table[previous].prefix != XX_SEAARC_HASH_NONE) {
        goto done;
    }
    first = table[previous].suffix;
    if (!xx_seaarc_sink_put(sink, first)) goto done;

    while (!xx_seaarc_sink_complete(sink)) {
        int32_t code;
        int32_t cursor;
        size_t length = 0U;
        bool undefined;

        if (xx_seaarc_stopped(pd, &tick)) goto done;
        code = xx_seaarc_nybble_code(source, &pending);
        if (code == -1) break;
        if (code < 0) goto done;
        undefined = !table[code].occupied;
        cursor = code;
        if (undefined) {
            /* The string being defined right now: previous + its first
             * byte. Only legal while the table still grows. */
            if (remaining == 0U) goto done;
            stack[length++] = first;
            cursor = previous;
        }
        while (table[cursor].prefix != XX_SEAARC_HASH_NONE) {
            if (!table[cursor].occupied || length >= XX_SEAARC_HASH_SIZE) {
                goto done;
            }
            stack[length++] = table[cursor].suffix;
            cursor = table[cursor].prefix;
            if (cursor < 0 || cursor >= (int32_t)XX_SEAARC_HASH_SIZE) goto done;
        }
        if (!table[cursor].occupied || length >= XX_SEAARC_HASH_SIZE) goto done;
        first = table[cursor].suffix;
        stack[length++] = first;
        if (remaining != 0U) {
            int32_t assigned = xx_seaarc_hash_insert(
                table, method, (int16_t)previous, first);
            if (assigned < 0 || (undefined && assigned != code)) goto done;
            --remaining;
        }
        while (length != 0U) {
            if (!xx_seaarc_sink_put(sink, stack[--length])) goto done;
        }
        previous = code;
    }
    result = true;

done:
    xx_mem_free(table);
    xx_mem_free(stack);
    return result;
}

/* ---------------------------------- methods 8, 9, 0x7f: dynamic LZW -- */

/* Unix-compress style codes, read in groups of n_bits bytes (eight codes).
 * In steady state that is a flat LSB-first bit stream, but a width change or
 * a CLEAR abandons the rest of the current group, exactly as ARC's getcode()
 * does. The width may grow up to max_bits; at max_bits the limit becomes the
 * full table size, so a stream whose declared width is 9 never "grows". */
#define XX_SEAARC_LZW_MIN_BITS 9U
#define XX_SEAARC_LZW_CRUNCH_MAX_BITS 12U
#define XX_SEAARC_LZW_SQUASH_BITS 13U
#define XX_SEAARC_LZW_MAX_BITS 16U
#define XX_SEAARC_LZW_CLEAR 256U
#define XX_SEAARC_LZW_FIRST 257U
#define XX_SEAARC_COMPRESS_BITS_MASK 0x1FU
#define XX_SEAARC_COMPRESS_RESERVED_MASK 0x60U

typedef struct xx_seaarc_code_reader_s {
    xx_seaarc_source *source;
    uint32_t init_bits;
    uint32_t max_bits;
    uint32_t bits;
    uint32_t offset;
    uint32_t size;
    bool clear_pending;
    uint32_t free_entry;
    uint32_t max_code;
    uint32_t max_max_code;
    uint8_t group[XX_SEAARC_LZW_MAX_BITS];
} xx_seaarc_code_reader;

static uint32_t xx_seaarc_max_code_for(const xx_seaarc_code_reader *reader,
                                       uint32_t bits) {
    return bits == reader->max_bits ? reader->max_max_code
                                    : ((UINT32_C(1) << bits) - 1U);
}

static bool xx_seaarc_code_read(xx_seaarc_code_reader *reader,
                                uint32_t *code) {
    uint32_t value = 0U;
    uint32_t index;

    if (reader->clear_pending || reader->offset >= reader->size ||
        reader->free_entry > reader->max_code) {
        uint32_t got = 0U;
        if (reader->free_entry > reader->max_code) {
            ++reader->bits;
            reader->max_code = xx_seaarc_max_code_for(reader, reader->bits);
        }
        if (reader->clear_pending) {
            reader->bits = reader->init_bits;
            reader->max_code = xx_seaarc_max_code_for(reader, reader->bits);
            reader->clear_pending = false;
        }
        if (reader->bits < 1U || reader->bits > XX_SEAARC_LZW_MAX_BITS) {
            return false;
        }
        while (got < reader->bits) {
            uint8_t byte;
            if (!xx_seaarc_source_byte(reader->source, &byte)) break;
            reader->group[got++] = byte;
        }
        /* Only whole codes: a short final group ends on its last full one,
         * and a group too short for even one code ends the stream (the
         * subtraction below must never wrap). */
        if (got == 0U || got * 8U < reader->bits) return false;
        reader->offset = 0U;
        reader->size = got * 8U - (reader->bits - 1U);
    }
    if (reader->offset >= reader->size) return false;
    for (index = 0U; index < reader->bits; ++index) {
        uint32_t bit = reader->offset + index;
        value |= (uint32_t)((reader->group[bit >> 3] >> (bit & 7U)) & 1U)
                 << index;
    }
    reader->offset += reader->bits;
    *code = value;
    return true;
}

static bool xx_seaarc_decode_lzw(xx_seaarc_source *source,
                                 xx_seaarc_sink *sink, uint32_t method,
                                 xx_pd_struct *pd) {
    xx_seaarc_code_reader reader;
    uint16_t *prefix = NULL;
    uint8_t *suffix = NULL;
    uint8_t *stack = NULL;
    uint32_t max_bits;
    uint32_t table_size;
    uint32_t free_entry = XX_SEAARC_LZW_FIRST;
    uint32_t code;
    uint32_t old_code;
    uint32_t tick = 0U;
    uint8_t final_char;
    bool result = false;

    if (method == XX_SEAARC_METHOD_SQUASHED) {
        max_bits = XX_SEAARC_LZW_SQUASH_BITS;
    } else {
        /* Methods 8 and 0x7f spend their first payload byte on the width.
         * Method 8's own range is a plain 9..12 with no flag bits; 0x7f is a
         * Unix compress flag byte whose 0x60 bits are reserved (ARC always
         * reserves CLEAR, block-mode flag or not). */
        uint8_t declared;
        if (!xx_seaarc_source_byte(source, &declared)) return false;
        if (method == XX_SEAARC_METHOD_COMPRESSED) {
            if ((declared & XX_SEAARC_COMPRESS_RESERVED_MASK) != 0U) {
                return false;
            }
            max_bits = declared & XX_SEAARC_COMPRESS_BITS_MASK;
            if (max_bits < XX_SEAARC_LZW_MIN_BITS ||
                max_bits > XX_SEAARC_LZW_MAX_BITS) {
                return false;
            }
        } else {
            max_bits = declared;
            if (max_bits < XX_SEAARC_LZW_MIN_BITS ||
                max_bits > XX_SEAARC_LZW_CRUNCH_MAX_BITS) {
                return false;
            }
        }
    }
    table_size = UINT32_C(1) << max_bits;

    prefix = (uint16_t *)xx_mem_alloc((size_t)table_size * sizeof(*prefix));
    suffix = (uint8_t *)xx_mem_alloc((size_t)table_size);
    stack = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!prefix || !suffix || !stack) goto done;
    xx_mem_zero(prefix, (size_t)table_size * sizeof(*prefix));
    xx_mem_zero(suffix, (size_t)table_size);
    for (code = 0U; code < 256U; ++code) suffix[code] = (uint8_t)code;

    xx_mem_zero(&reader, sizeof(reader));
    reader.source = source;
    reader.init_bits = XX_SEAARC_LZW_MIN_BITS;
    reader.max_bits = max_bits;
    reader.bits = XX_SEAARC_LZW_MIN_BITS;
    reader.max_max_code = table_size;
    reader.max_code = xx_seaarc_max_code_for(&reader, reader.bits);
    reader.free_entry = free_entry;

    if (!xx_seaarc_code_read(&reader, &code)) {
        /* An empty code stream; the size check decides. */
        result = true;
        goto done;
    }
    if (code >= 256U) goto done;
    old_code = code;
    final_char = (uint8_t)code;
    if (!xx_seaarc_sink_complete(sink) &&
        !xx_seaarc_sink_put(sink, final_char)) {
        goto done;
    }

    while (!xx_seaarc_sink_complete(sink)) {
        uint32_t in_code;
        uint32_t work;
        uint32_t depth = 0U;
        uint32_t stack_size = 0U;

        if (xx_seaarc_stopped(pd, &tick)) goto done;
        if (!xx_seaarc_code_read(&reader, &code)) break;

        if (code == XX_SEAARC_LZW_CLEAR) {
            free_entry = XX_SEAARC_LZW_FIRST;
            reader.free_entry = free_entry;
            reader.clear_pending = true;
            if (!xx_seaarc_code_read(&reader, &code)) break;
            if (code >= 256U) goto done;
            old_code = code;
            final_char = (uint8_t)code;
            if (!xx_seaarc_sink_put(sink, final_char)) goto done;
            continue;
        }

        in_code = code;
        work = code;
        if (work >= free_entry) {
            /* KwKwK: the code currently being defined. */
            if (work > free_entry) goto done;
            stack[stack_size++] = final_char;
            work = old_code;
        }
        while (work >= 256U) {
            if (work >= table_size || stack_size >= table_size ||
                ++depth > table_size) {
                goto done;
            }
            stack[stack_size++] = suffix[work];
            work = prefix[work];
        }
        if (stack_size >= table_size) goto done;
        final_char = suffix[work];
        stack[stack_size++] = final_char;
        while (stack_size > 0U) {
            if (xx_seaarc_sink_complete(sink)) break;
            if (!xx_seaarc_sink_put(sink, stack[--stack_size])) goto done;
        }
        if (free_entry < table_size) {
            prefix[free_entry] = (uint16_t)old_code;
            suffix[free_entry] = final_char;
            ++free_entry;
            reader.free_entry = free_entry;
        }
        old_code = in_code;
    }
    result = true;

done:
    xx_mem_free(prefix);
    xx_mem_free(suffix);
    xx_mem_free(stack);
    return result;
}

/* ------------------------------------------ method 10: PAK crushed -- */

/* PAK 2.x "crushed": LZW over a table of up to 8192 strings, with codes
 * growing from 1 to 13 bits. While fewer than 375 of the last 500 symbols
 * were strings the coder is in "literal mode" (a flag bit, then 8 literal
 * bits or a string code less 256); otherwise every symbol is a plain code
 * and a literal is sent as its complement. Once the table is full, the next
 * string replaces the least recently used entry found by a clock sweep over
 * a small (0..4) usage counter. Symbol 256 ends the stream; the output then
 * goes through ARC's 0x90 run-length stage. */
#define XX_SEAARC_CRUSH_TABLE 8192U
#define XX_SEAARC_CRUSH_RESERVED 257U
#define XX_SEAARC_CRUSH_EOF 256U
#define XX_SEAARC_CRUSH_RING 500U
#define XX_SEAARC_CRUSH_THRESHOLD 375U
#define XX_SEAARC_CRUSH_MAX_BITS 13U

typedef struct xx_seaarc_crush_s {
    int16_t parent[XX_SEAARC_CRUSH_TABLE];
    uint8_t byte[XX_SEAARC_CRUSH_TABLE];
    uint8_t usage[XX_SEAARC_CRUSH_TABLE];
    uint8_t ring[XX_SEAARC_CRUSH_RING];
    uint8_t stack[XX_SEAARC_CRUSH_TABLE + 1U];
    uint32_t table_size;
    uint32_t code_bits;
    uint32_t next_bump;
    bool literal_mode;
    uint32_t ring_position;
    uint32_t string_count;
    uint32_t usage_position;
    uint32_t previous;
    bool has_previous;
} xx_seaarc_crush;

static bool xx_seaarc_crush_symbol(const xx_seaarc_crush *state,
                                   xx_seaarc_source *source,
                                   uint32_t *symbol) {
    uint32_t code;

    if (state->literal_mode) {
        uint32_t is_string;
        if (!xx_seaarc_source_bits(source, 1U, &is_string)) return false;
        if (is_string) {
            if (!xx_seaarc_source_bits(source, state->code_bits, &code)) {
                return false;
            }
            code += 256U;
        } else if (!xx_seaarc_source_bits(source, 8U, &code)) {
            return false;
        }
    } else {
        if (!xx_seaarc_source_bits(source, state->code_bits, &code)) {
            return false;
        }
        if (code < 256U) code ^= 0xFFU;
    }
    *symbol = code;
    return true;
}

static bool xx_seaarc_crush_mark_used(xx_seaarc_crush *state,
                                      uint32_t symbol) {
    uint32_t depth = 0U;

    while (symbol < XX_SEAARC_CRUSH_TABLE) {
        int32_t parent;
        if (++depth > XX_SEAARC_CRUSH_TABLE) return false;
        state->usage[symbol] = 4U;
        parent = state->parent[symbol];
        if (parent < 0) return true;
        if ((uint32_t)parent >= state->table_size) return false;
        symbol = (uint32_t)parent;
    }
    return false;
}

static void xx_seaarc_crush_update_mode(xx_seaarc_crush *state,
                                        bool is_string) {
    bool literal_mode;

    if (state->ring[state->ring_position] && state->string_count != 0U) {
        --state->string_count;
    }
    state->ring[state->ring_position] = is_string ? 1U : 0U;
    if (is_string) ++state->string_count;
    state->ring_position = (state->ring_position + 1U) % XX_SEAARC_CRUSH_RING;
    literal_mode = state->string_count < XX_SEAARC_CRUSH_THRESHOLD;
    if (literal_mode != state->literal_mode) {
        state->literal_mode = literal_mode;
        state->next_bump = UINT32_C(1) << state->code_bits;
        if (!state->literal_mode) state->next_bump -= 0x100U;
    }
}

/* Writes the string for @p symbol into state->stack in REVERSE order and
 * returns its length, or 0 on a malformed chain. */
static uint32_t xx_seaarc_crush_reversed(xx_seaarc_crush *state,
                                         uint32_t symbol) {
    uint32_t length = 0U;

    while (symbol < state->table_size) {
        int32_t parent;
        if (length >= XX_SEAARC_CRUSH_TABLE) return 0U;
        state->stack[length++] = state->byte[symbol];
        parent = state->parent[symbol];
        if (parent < 0) return length;
        if ((uint32_t)parent >= state->table_size) return 0U;
        symbol = (uint32_t)parent;
    }
    return 0U;
}

static bool xx_seaarc_crush_first_byte(const xx_seaarc_crush *state,
                                       uint32_t symbol, uint8_t *value) {
    uint32_t depth = 0U;

    while (symbol < state->table_size) {
        int32_t parent;
        if (++depth > XX_SEAARC_CRUSH_TABLE) return false;
        parent = state->parent[symbol];
        if (parent < 0) {
            *value = state->byte[symbol];
            return true;
        }
        if ((uint32_t)parent >= state->table_size) return false;
        symbol = (uint32_t)parent;
    }
    return false;
}

static bool xx_seaarc_crush_add(xx_seaarc_crush *state, uint32_t symbol) {
    uint8_t first;
    uint32_t slot;

    if (!state->has_previous) {
        state->previous = symbol;
        state->has_previous = true;
        return true;
    }
    if (!xx_seaarc_crush_first_byte(
            state, symbol == state->table_size ? state->previous : symbol,
            &first)) {
        return false;
    }
    if (state->table_size < XX_SEAARC_CRUSH_TABLE) {
        slot = state->table_size++;
    } else {
        uint32_t minimum_index = XX_SEAARC_CRUSH_RESERVED;
        uint8_t minimum_usage = 0xFFU;
        uint32_t index = state->usage_position;
        do {
            ++index;
            if (index >= XX_SEAARC_CRUSH_TABLE) index = XX_SEAARC_CRUSH_RESERVED;
            if (state->usage[index] < minimum_usage) {
                minimum_index = index;
                minimum_usage = state->usage[index];
            }
            if (state->usage[index] > 0U) --state->usage[index];
            if (state->usage[index] == 0U) break;
        } while (index != state->usage_position);
        state->usage_position = index;
        slot = minimum_index;
    }
    state->parent[slot] = (int16_t)state->previous;
    state->byte[slot] = first;
    state->usage[slot] = 2U;
    state->previous = symbol;
    return true;
}

static void xx_seaarc_crush_check_width(xx_seaarc_crush *state) {
    uint32_t added = state->table_size - XX_SEAARC_CRUSH_RESERVED;

    if (added >= state->next_bump &&
        state->code_bits < XX_SEAARC_CRUSH_MAX_BITS) {
        ++state->code_bits;
        state->next_bump = UINT32_C(1) << state->code_bits;
        if (!state->literal_mode) state->next_bump -= 0x100U;
    }
}

static bool xx_seaarc_decode_crushed(xx_seaarc_source *source,
                                     xx_seaarc_sink *sink, xx_pd_struct *pd) {
    xx_seaarc_crush *state;
    uint32_t index;
    uint32_t tick = 0U;
    bool result = false;

    state = (xx_seaarc_crush *)xx_mem_alloc(sizeof(*state));
    if (!state) return false;
    xx_mem_zero(state, sizeof(*state));
    for (index = 0U; index < XX_SEAARC_CRUSH_TABLE; ++index) {
        state->parent[index] = -1;
    }
    for (index = 0U; index < 256U; ++index) state->byte[index] = (uint8_t)index;
    for (index = 0U; index < XX_SEAARC_CRUSH_RESERVED; ++index) {
        state->usage[index] = 4U;
    }
    state->table_size = XX_SEAARC_CRUSH_RESERVED;
    state->code_bits = 1U;
    state->next_bump = 2U;
    state->literal_mode = true;
    state->usage_position = XX_SEAARC_CRUSH_RESERVED;

    for (;;) {
        uint32_t symbol;
        uint32_t length;

        if (xx_seaarc_stopped(pd, &tick)) goto done;
        if (!xx_seaarc_crush_symbol(state, source, &symbol)) goto done;
        if (symbol == XX_SEAARC_CRUSH_EOF) break;
        if (symbol >= XX_SEAARC_CRUSH_TABLE || symbol > state->table_size) {
            goto done;
        }
        if (symbol < state->table_size) {
            if (!xx_seaarc_crush_mark_used(state, symbol)) goto done;
        } else if (!state->has_previous ||
                   !xx_seaarc_crush_mark_used(state, state->previous)) {
            goto done;
        }
        xx_seaarc_crush_update_mode(state, symbol >= 256U);

        if (symbol == state->table_size) {
            /* The string being defined: previous + its own first byte. */
            uint8_t head;
            if (!state->has_previous ||
                state->table_size >= XX_SEAARC_CRUSH_TABLE) {
                goto done;
            }
            length = xx_seaarc_crush_reversed(state, state->previous);
            if (length == 0U) goto done;
            /* stack[] is reversed: its last byte is the string's first. */
            head = state->stack[length - 1U];
            while (length != 0U) {
                if (!xx_seaarc_sink_put(sink, state->stack[--length])) {
                    goto done;
                }
            }
            if (!xx_seaarc_sink_put(sink, head)) goto done;
        } else {
            length = xx_seaarc_crush_reversed(state, symbol);
            if (length == 0U) goto done;
            while (length != 0U) {
                if (!xx_seaarc_sink_put(sink, state->stack[--length])) {
                    goto done;
                }
            }
        }
        if (!xx_seaarc_crush_add(state, symbol)) goto done;
        xx_seaarc_crush_check_width(state);
    }
    result = true;

done:
    xx_mem_free(state);
    return result;
}

/* ---------------------------------------- method 11: PAK distilled -- */

/* PAK 2.x "distilled": a serialized Huffman tree (16-bit node count, 8-bit
 * value width, then that many values; a value >= count is a leaf, anything
 * else is the index of a child pair, the root pair being at count-2), then
 * symbols: 0..255 literal, 256 end, 257+ a match of (symbol - 254) bytes.
 * A match distance is a fixed prefix code for its high bits plus 0..7 raw low
 * bits, the count growing with the output position. The window is 8 KiB and
 * starts filled with spaces. No run-length stage. */
#define XX_SEAARC_DISTILL_MAX_NODES 629U
#define XX_SEAARC_DISTILL_WINDOW 8192U
#define XX_SEAARC_DISTILL_EOF 256U

static uint32_t xx_seaarc_distill_offset_length(uint32_t symbol) {
    if (symbol == 0U) return 3U;
    if (symbol < 4U) return 4U;
    if (symbol < 12U) return 5U;
    if (symbol < 24U) return 6U;
    if (symbol < 48U) return 7U;
    return 8U;
}

static bool xx_seaarc_distill_offset_symbol(xx_seaarc_source *source,
                                            uint32_t *symbol) {
    static const uint8_t codes[64] = {
        0x00, 0x02, 0x04, 0x0c, 0x01, 0x06, 0x0a, 0x0e, 0x11, 0x16, 0x1a,
        0x1e, 0x05, 0x09, 0x0d, 0x15, 0x19, 0x1d, 0x25, 0x29, 0x2d, 0x35,
        0x39, 0x3d, 0x03, 0x07, 0x0b, 0x13, 0x17, 0x1b, 0x23, 0x27, 0x2b,
        0x33, 0x37, 0x3b, 0x43, 0x47, 0x4b, 0x53, 0x57, 0x5b, 0x63, 0x67,
        0x6b, 0x73, 0x77, 0x7b, 0x0f, 0x1f, 0x2f, 0x3f, 0x4f, 0x5f, 0x6f,
        0x7f, 0x8f, 0x9f, 0xaf, 0xbf, 0xcf, 0xdf, 0xef, 0xff};
    uint32_t code = 0U;
    uint32_t length;

    for (length = 1U; length <= 8U; ++length) {
        uint32_t bit;
        uint32_t candidate;
        if (!xx_seaarc_source_bits(source, 1U, &bit)) return false;
        if (bit) code |= UINT32_C(1) << (length - 1U);
        for (candidate = 0U; candidate < 64U; ++candidate) {
            if (xx_seaarc_distill_offset_length(candidate) == length &&
                codes[candidate] == code) {
                *symbol = candidate;
                return true;
            }
        }
    }
    return false;
}

static uint32_t xx_seaarc_distill_extra_bits(size_t position) {
    if (position >= 0x0fc4U) return 7U;
    if (position >= 0x07c4U) return 6U;
    if (position >= 0x03c4U) return 5U;
    if (position >= 0x01c4U) return 4U;
    if (position >= 0x00c4U) return 3U;
    if (position >= 0x0044U) return 2U;
    if (position >= 0x0004U) return 1U;
    return 0U;
}

/* Every pair reachable from the root must be a valid pair index, and no pair
 * may recur on its own path (a shared pair is fine). Iterative DFS with
 * three colours, so a hostile tree cannot recurse or loop. */
static bool xx_seaarc_distill_tree_valid(const uint16_t *nodes,
                                         uint32_t count) {
    uint8_t colors[XX_SEAARC_DISTILL_MAX_NODES];
    uint16_t stack_node[XX_SEAARC_DISTILL_MAX_NODES];
    uint8_t stack_branch[XX_SEAARC_DISTILL_MAX_NODES];
    uint32_t depth = 0U;
    uint32_t index;

    if (count < 2U || count > XX_SEAARC_DISTILL_MAX_NODES) return false;
    for (index = 0U; index < count; ++index) {
        if (nodes[index] < count && nodes[index] > count - 2U) return false;
        colors[index] = 0U;
    }
    colors[count - 2U] = 1U;
    stack_node[0] = (uint16_t)(count - 2U);
    stack_branch[0] = 0U;
    depth = 1U;
    while (depth != 0U) {
        uint32_t top = depth - 1U;
        uint32_t value;
        if (stack_branch[top] >= 2U) {
            colors[stack_node[top]] = 2U;
            --depth;
            continue;
        }
        value = nodes[stack_node[top] + stack_branch[top]];
        ++stack_branch[top];
        if (value >= count) continue;
        if (colors[value] == 1U) return false;
        if (colors[value] == 0U) {
            if (depth >= XX_SEAARC_DISTILL_MAX_NODES) return false;
            colors[value] = 1U;
            stack_node[depth] = (uint16_t)value;
            stack_branch[depth] = 0U;
            ++depth;
        }
    }
    return true;
}

static bool xx_seaarc_distill_symbol(xx_seaarc_source *source,
                                     const uint16_t *nodes, uint32_t count,
                                     uint32_t *symbol) {
    uint32_t node = count - 2U;
    uint32_t depth;

    for (depth = 0U; depth <= count; ++depth) {
        uint32_t bit;
        uint32_t value;
        if (!xx_seaarc_source_bits(source, 1U, &bit) || node > count - 2U) {
            return false;
        }
        value = nodes[node + bit];
        if (value >= count) {
            *symbol = value - count;
            return true;
        }
        node = value;
    }
    return false;
}

static bool xx_seaarc_decode_distilled(xx_seaarc_source *source,
                                       xx_seaarc_sink *sink,
                                       xx_pd_struct *pd) {
    uint16_t nodes[XX_SEAARC_DISTILL_MAX_NODES];
    uint8_t *window;
    uint32_t count;
    uint32_t width;
    uint32_t index;
    uint32_t window_position = 0U;
    uint32_t tick = 0U;
    bool result = false;

    if (!xx_seaarc_source_bits(source, 16U, &count) ||
        !xx_seaarc_source_bits(source, 8U, &width) || count < 2U ||
        count > XX_SEAARC_DISTILL_MAX_NODES || width < 1U || width > 12U) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        uint32_t value;
        if (!xx_seaarc_source_bits(source, width, &value)) return false;
        nodes[index] = (uint16_t)value;
    }
    if (!xx_seaarc_distill_tree_valid(nodes, count)) return false;

    window = (uint8_t *)xx_mem_alloc(XX_SEAARC_DISTILL_WINDOW);
    if (!window) return false;
    xx_rt_memset(window, 0x20, XX_SEAARC_DISTILL_WINDOW);

    for (;;) {
        uint32_t symbol;

        if (xx_seaarc_stopped(pd, &tick)) goto done;
        if (!xx_seaarc_distill_symbol(source, nodes, count, &symbol)) goto done;
        if (symbol < 256U) {
            if (!xx_seaarc_sink_put(sink, (uint8_t)symbol)) goto done;
            window[window_position] = (uint8_t)symbol;
            window_position =
                (window_position + 1U) & (XX_SEAARC_DISTILL_WINDOW - 1U);
        } else if (symbol == XX_SEAARC_DISTILL_EOF) {
            break;
        } else {
            uint32_t length = symbol - 254U;
            uint32_t offset_symbol;
            uint32_t extra_bits;
            uint32_t extra;
            uint32_t distance;
            uint32_t from;
            uint32_t copied;
            if (length < 3U || length > XX_SEAARC_DISTILL_WINDOW) goto done;
            if (!xx_seaarc_distill_offset_symbol(source, &offset_symbol)) {
                goto done;
            }
            extra_bits = xx_seaarc_distill_extra_bits(sink->size);
            if (!xx_seaarc_source_bits(source, extra_bits, &extra)) goto done;
            distance = (offset_symbol << extra_bits) | extra;
            if (distance >= XX_SEAARC_DISTILL_WINDOW) goto done;
            from = (window_position - 1U - distance) &
                   (XX_SEAARC_DISTILL_WINDOW - 1U);
            for (copied = 0U; copied < length; ++copied) {
                uint8_t value = window[from];
                from = (from + 1U) & (XX_SEAARC_DISTILL_WINDOW - 1U);
                if (!xx_seaarc_sink_put(sink, value)) goto done;
                window[window_position] = value;
                window_position =
                    (window_position + 1U) & (XX_SEAARC_DISTILL_WINDOW - 1U);
            }
        }
    }
    result = true;

done:
    xx_mem_free(window);
    return result;
}

/* ----------------------------------------------------------- dispatch -- */

static uint16_t xx_seaarc_crc16(const uint8_t *data, size_t size) {
    uint16_t table[256];
    uint32_t index;
    uint16_t crc = 0U;
    size_t position;

    for (index = 0U; index < 256U; ++index) {
        uint16_t value = (uint16_t)index;
        uint32_t bit;
        for (bit = 0U; bit < 8U; ++bit) {
            value = (uint16_t)((value & 1U) ? ((value >> 1) ^ 0xA001U)
                                            : (value >> 1));
        }
        table[index] = value;
    }
    for (position = 0U; position < size; ++position) {
        crc = (uint16_t)((crc >> 8) ^ table[(crc ^ data[position]) & 0xFFU]);
    }
    return crc;
}

/* Read a member's payload into a fresh buffer. */
static uint8_t *xx_seaarc_load(Abstractformat *self, int64_t data_offset,
                               int64_t size) {
    uint8_t *packed;

    if (size < 0 || (uint64_t)size > (uint64_t)SIZE_MAX) return NULL;
    packed = (uint8_t *)xx_mem_alloc(size != 0 ? (size_t)size : 1U);
    if (!packed) return NULL;
    if (size != 0 &&
        !xx_seaarc_read_at(self, data_offset, packed, (size_t)size)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

static bool xx_seaarc_decode(Abstractformat *self,
                             const xx_seaarc_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    xx_seaarc_source source;
    xx_seaarc_sink sink;
    uint8_t *packed;
    size_t packed_size;
    bool decoded = false;
    bool run_length;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0 ||
        member->compressed_size > XX_SEAARC_MAX_DECODED ||
        member->uncompressed_size > XX_SEAARC_MAX_DECODED) {
        return false;
    }
    if (!xx_seaarc_method_valid((uint8_t)member->method) ||
        member->method > 0xFFU) {
        return false;
    }

    packed = xx_seaarc_load(self, member->data_offset,
                            member->compressed_size);
    if (!packed) return false;
    packed_size = (size_t)member->compressed_size;

    run_length = member->method == XX_SEAARC_METHOD_PACKED ||
                 member->method == XX_SEAARC_METHOD_SQUEEZED ||
                 member->method == XX_SEAARC_METHOD_CRUNCHED2 ||
                 member->method == XX_SEAARC_METHOD_CRUNCHED3 ||
                 member->method == XX_SEAARC_METHOD_CRUNCHED4 ||
                 member->method == XX_SEAARC_METHOD_CRUSHED;
    if (!xx_seaarc_sink_init(&sink, (size_t)member->uncompressed_size,
                             packed_size, run_length)) {
        xx_mem_free(packed);
        return false;
    }
    xx_seaarc_source_init(&source, packed, packed_size);

    switch (member->method) {
        case XX_SEAARC_METHOD_STORE_OLD:
        case XX_SEAARC_METHOD_STORE: {
            size_t index;
            /* parse already required the two lengths to be equal. */
            decoded = true;
            for (index = 0U; index < packed_size && decoded; ++index) {
                decoded = xx_seaarc_sink_emit(&sink, packed[index]);
            }
            break;
        }
        case XX_SEAARC_METHOD_PACKED:
            decoded = xx_seaarc_decode_packed(&source, &sink, pd);
            break;
        case XX_SEAARC_METHOD_SQUEEZED:
            decoded = xx_seaarc_decode_squeezed(&source, &sink, pd);
            break;
        case XX_SEAARC_METHOD_CRUNCHED1:
        case XX_SEAARC_METHOD_CRUNCHED2:
        case XX_SEAARC_METHOD_CRUNCHED3:
            decoded = xx_seaarc_decode_hashed(&source, &sink, member->method,
                                              pd);
            break;
        case XX_SEAARC_METHOD_CRUNCHED4:
        case XX_SEAARC_METHOD_SQUASHED:
        case XX_SEAARC_METHOD_COMPRESSED:
            decoded = xx_seaarc_decode_lzw(&source, &sink, member->method, pd);
            break;
        case XX_SEAARC_METHOD_CRUSHED:
            decoded = xx_seaarc_decode_crushed(&source, &sink, pd);
            break;
        case XX_SEAARC_METHOD_DISTILLED:
            decoded = xx_seaarc_decode_distilled(&source, &sink, pd);
            break;
        default:
            decoded = false;
            break;
    }
    xx_mem_free(packed);

    /* A short decode is the one failure a caller cannot detect once the
     * buffer is handed over, so it is a failure here, never a partial
     * success -- and so is a CRC mismatch (a wrong decoder, a trimmed ARC 7
     * member under method 10, or an ARC-encrypted member). */
    if (!decoded || sink.failed || sink.in_repeat ||
        sink.size != sink.limit ||
        xx_seaarc_crc16(sink.data, sink.size) != member->crc ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(sink.data);
        return false;
    }
    *out = sink.data;
    *out_size = sink.size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_seaarc_init(xx_seaarc *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SEAARC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-arc");
    xx_format_set_extension(&archive->format, "arc");
    archive->format.check_is_valid = xx_seaarc_check_is_valid;
    archive->format.handle_base_info = xx_seaarc_handle_base_info;
    archive->format.get_format_size = xx_seaarc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_seaarc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_seaarc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_seaarc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_seaarc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_seaarc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_seaarc_free_archive_records_reading;
    archive->format.destroy = xx_seaarc_vtable_destroy;
}

xx_seaarc *xx_seaarc_create(xx_io_device *device, int64_t base_address) {
    xx_seaarc *archive = (xx_seaarc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_seaarc_init(archive, device, base_address);
    return archive;
}

void xx_seaarc_destroy(xx_seaarc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_seaarc_free(xx_seaarc *archive) {
    if (!archive) return;
    xx_seaarc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_seaarc_vtable_destroy(Abstractformat *self) {
    xx_seaarc_destroy((xx_seaarc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_seaarc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_seaarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_seaarc_parse(self, pd, false);
    if (!stream) return false;
    xx_seaarc_stream_free(stream);
    return true;
}

bool xx_seaarc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_seaarc *archive = (xx_seaarc *)self;
    xx_seaarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_seaarc_parse(self, pd, false);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_seaarc_stream_free(stream);
    return true;
}

int64_t xx_seaarc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_seaarc_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_seaarc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_seaarc_set_record(xx_archive_record *record,
                                 const xx_seaarc_member *member) {
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

static bool xx_seaarc_copy_options(xx_list_s *target,
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

static const xx_var *xx_seaarc_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_seaarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_seaarc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_seaarc_parse(self, pd, true);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_seaarc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_seaarc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_seaarc_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_seaarc_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_seaarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_seaarc_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_seaarc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_seaarc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_seaarc_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_seaarc_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_seaarc_stream *stream;
    const xx_seaarc_member *member;
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
    stream = (xx_seaarc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_seaarc_output_name_safe(member->name)) return false;

    path_option = xx_seaarc_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_seaarc_decode(self, member, &plain, &plain_size, pd);
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
    /* Decode before creating anything, so a member that fails its CRC
     * leaves no empty file behind. */
    if (!xx_seaarc_decode(self, member, &plain, &plain_size, pd) ||
        !xx_store_create_dirs_a(target_path, false)) {
        xx_mem_free(plain);
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

void xx_seaarc_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
