/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ej-technologies install4j / exe4j launcher.  xx_ej_technologies_install.h
 * carries the container layout.
 *
 * Ported from XArchive installers/xinstall4jsfx.cpp (MIT License, Copyright
 * (c) 2026 hors): the detection predicate (magic, variable count and first
 * variable length both inside (0, 0x400), first key 101), the walk over the
 * two variable tables, the ';'-joined member list of key 2003, the zero word
 * that marks the later builds and the big-endian trailer.  Differences from
 * that reference:
 *
 *  - The overlay is found by reading the PE section table directly (the
 *    end of the last section's raw data, also tried rounded up to the file
 *    alignment and to 0x200).  Nothing is searched for and no code runs.
 *  - The generation (zero word or not) is decided for the whole container,
 *    not per member: when the word behind the first size is zero every
 *    member must carry the zero word.  Only when that walk finds no trailer,
 *    runs out of file or meets a member without the word is the plain
 *    layout tried, and it is taken only if it walks completely and ends
 *    cleanly.  A member whose plaintext happens to open with 88 88 88 88
 *    therefore cannot flip the walk half way.
 *  - A member that runs past the end of the file ends the walk instead of
 *    rejecting the launcher; the complete members are still listed and the
 *    container is flagged truncated.
 *  - Member names are refused on extraction when they are unsafe (absolute,
 *    drive or stream colons, "." / ".." components, control characters,
 *    reserved punctuation, Windows device names) and names that collide,
 *    ignoring ASCII and Latin-1 letter case, are renamed so no member
 *    overwrites another.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ej_technologies_install/xx_ej_technologies_install.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef EJ_TECHNOLOGIES_INSTALL
#define XX_EJ_TECHNOLOGIES_INSTALL_FILE_TYPE \
    XX_FILE_TYPE_EJ_TECHNOLOGIES_INSTALL
#else
#define XX_EJ_TECHNOLOGIES_INSTALL_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Stored D5 13 E4 E8 in front of the variable table, read little-endian... */
#define EJTI_MAGIC_HEAD 0xE8E413D5U
/* ...and the same bytes reversed, E8 E4 13 D5, in front of the trailer. */
#define EJTI_MAGIC_TAIL 0xD513E4E8U
#define EJTI_KEY_PRODUCT 101
#define EJTI_KEY_FILELIST 2003
/* The reference implementation's own bounds: the variable count and the
 * first variable's length are both inside (0, 0x400), and the member list
 * is at most 64 KiB. */
#define EJTI_MAX_COUNT 0x400
#define EJTI_MAX_LIST 0x10000
/* Ceilings this reader adds so a desynchronised walk stops. */
#define EJTI_MAX_EXTRA 0x10000
#define EJTI_MAX_MEMBERS 4096U
#define EJTI_MAX_TRAILER_NAME 1024U
#define EJTI_NAME_ROUNDS 8U
/* PE: the loader's section limit and the part of the optional header read
 * (FileAlignment at +36 and SizeOfHeaders at +60 in both PE32 and PE32+). */
#define EJTI_MAX_SECTIONS 96U
#define EJTI_SECTION_SIZE 40U
#define EJTI_OPTIONAL_READ 64U
#define EJTI_PE32_MAGIC 0x10BU
#define EJTI_PE64_MAGIC 0x20BU
#define EJTI_HEAD_SIZE 16
#define EJTI_CURSOR_SIZE 4096U
#define EJTI_COPY_CHUNK 65536U
#define EJTI_XOR 0x88U

typedef struct ejti_member_s {
    char *name;             /**< UTF-8, '/' separated, unique ignoring case. */
    int64_t header_offset;  /**< Absolute. */
    int64_t offset;         /**< Absolute offset of the member data. */
    int64_t size;
    uint32_t header_size;
    uint32_t list_index;    /**< Position in the key 2003 list or trailer. */
    bool stored;            /**< Trailer member: no XOR. */
} ejti_member;

typedef struct ejti_stream_s {
    ejti_member *items;
    size_t count;
    size_t index;
    int64_t container_offset; /**< Relative to the base. */
    int64_t records_offset;   /**< Relative to the base. */
    int64_t archive_size;     /**< Relative to the base. */
    uint32_t variable_count;
    uint32_t extra_count;
    uint32_t listed;
    uint32_t trailer_count;
    bool padded;
    bool has_trailer;
    bool truncated;
    char product[XX_EJ_TECHNOLOGIES_INSTALL_PRODUCT_MAX];
} ejti_stream;

/* A small read-through window over the device, so the variable tables are
 * walked without one seek per 8-byte entry.  Offsets are relative to base. */
typedef struct ejti_cursor_s {
    xx_io_device *device;
    int64_t base;
    int64_t size;
    int64_t start;
    size_t length;
    uint8_t buffer[EJTI_CURSOR_SIZE];
} ejti_cursor;

typedef struct ejti_key_s {
    const char *name;
    size_t item;
} ejti_key;

/* Outcome of one walk over the members.  Only a LAYOUT result (a zero word
 * missing where the padded layout needs one) sends the parser to the other
 * layout; BAD rejects the container. */
#define EJTI_WALK_COMPLETE 1
#define EJTI_WALK_TRUNCATED 0
#define EJTI_WALK_LAYOUT (-1)
#define EJTI_WALK_BAD (-2)

typedef struct ejti_walk_s {
    ejti_member *items; /**< Capacity: listed + trailer members. */
    size_t capacity;
    size_t count;
    int64_t end;        /**< Relative end of the walk. */
    uint32_t trailer_count;
    bool has_trailer;
    int result;
} ejti_walk;

static uint32_t ejti_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t ejti_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint32_t ejti_be16(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 8U) | (uint32_t)bytes[1];
}

static uint32_t ejti_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint64_t ejti_be64(const uint8_t *bytes) {
    return ((uint64_t)ejti_be32(bytes) << 32U) | (uint64_t)ejti_be32(bytes + 4);
}

static bool ejti_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Copy `size` bytes at relative `offset` out of the cursor window, refilling
 * it from the device when the range is not already buffered. */
static bool ejti_get(ejti_cursor *cursor, int64_t offset, void *out,
                     size_t size) {
    if (!cursor || !out || offset < 0 || size > EJTI_CURSOR_SIZE ||
        offset > cursor->size || (int64_t)size > cursor->size - offset)
        return false;
    if (cursor->length == 0U || offset < cursor->start ||
        offset - cursor->start > (int64_t)cursor->length ||
        (int64_t)size > (int64_t)cursor->length - (offset - cursor->start)) {
        int64_t want = cursor->size - offset;
        if (want > (int64_t)EJTI_CURSOR_SIZE) want = (int64_t)EJTI_CURSOR_SIZE;
        cursor->length = 0U;
        if (!ejti_read_at(cursor->device, cursor->base + offset,
                          cursor->buffer, (size_t)want))
            return false;
        cursor->start = offset;
        cursor->length = (size_t)want;
    }
    xx_rt_memcpy(out, cursor->buffer + (size_t)(offset - cursor->start), size);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Locating the container                                                  */
/* ---------------------------------------------------------------------- */

/* The sixteen bytes the reference implementation (and U3) recognise the
 * container by. */
static bool ejti_head_ok(const uint8_t *head) {
    int32_t count = (int32_t)ejti_le32(head + 4);
    int32_t first = (int32_t)ejti_le32(head + 12);
    return ejti_le32(head) == EJTI_MAGIC_HEAD && count > 0 &&
           count < EJTI_MAX_COUNT &&
           (int32_t)ejti_le32(head + 8) == EJTI_KEY_PRODUCT && first > 0 &&
           first < EJTI_MAX_COUNT;
}

static bool ejti_try_offset(ejti_cursor *cursor, uint64_t candidate,
                            int64_t *container) {
    uint8_t head[EJTI_HEAD_SIZE];
    if (candidate == 0U || candidate > (uint64_t)cursor->size ||
        (int64_t)candidate > cursor->size - EJTI_HEAD_SIZE)
        return false;
    if (!ejti_get(cursor, (int64_t)candidate, head, sizeof(head)) ||
        !ejti_head_ok(head))
        return false;
    *container = (int64_t)candidate;
    return true;
}

static uint64_t ejti_align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

/* Parse the PE headers only as far as the section table, take the end of
 * the raw section data as the overlay offset and look for the container
 * head there.  Every read is bounded by the device. */
static bool ejti_locate(ejti_cursor *cursor, int64_t *container) {
    uint8_t dos[0x40];
    uint8_t nt[24];
    uint8_t optional[EJTI_OPTIONAL_READ];
    uint8_t sections[EJTI_MAX_SECTIONS * EJTI_SECTION_SIZE];
    uint32_t lfanew, count, optional_size, magic, alignment, index;
    uint64_t raw_end, table;
    if (cursor->size < (int64_t)(sizeof(dos) + EJTI_HEAD_SIZE) ||
        !ejti_get(cursor, 0, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    lfanew = ejti_le32(dos + 0x3C);
    if (lfanew < 4U || (int64_t)lfanew > cursor->size - (int64_t)sizeof(nt) ||
        !ejti_get(cursor, (int64_t)lfanew, nt, sizeof(nt)) || nt[0] != 'P' ||
        nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U)
        return false;
    count = ejti_le16(nt + 6);
    optional_size = ejti_le16(nt + 20);
    if (count == 0U || count > EJTI_MAX_SECTIONS ||
        optional_size < EJTI_OPTIONAL_READ ||
        !ejti_get(cursor, (int64_t)lfanew + (int64_t)sizeof(nt), optional,
                  sizeof(optional)))
        return false;
    magic = ejti_le16(optional);
    if (magic != EJTI_PE32_MAGIC && magic != EJTI_PE64_MAGIC) return false;
    alignment = ejti_le32(optional + 36);
    raw_end = ejti_le32(optional + 60);
    table = (uint64_t)lfanew + sizeof(nt) + optional_size;
    if (table > (uint64_t)cursor->size ||
        (uint64_t)count * EJTI_SECTION_SIZE > (uint64_t)cursor->size - table ||
        !ejti_get(cursor, (int64_t)table, sections,
                  (size_t)count * EJTI_SECTION_SIZE))
        return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *section = sections + (size_t)index * EJTI_SECTION_SIZE;
        uint32_t raw_size = ejti_le32(section + 16);
        uint32_t raw_pointer = ejti_le32(section + 20);
        uint64_t end = (uint64_t)raw_pointer + raw_size;
        if (raw_size != 0U && end > raw_end) raw_end = end;
    }
    if (raw_end == 0U || raw_end >= (uint64_t)cursor->size) return false;
    /* The overlay starts where the raw data ends; a linker that left the
     * last SizeOfRawData unrounded puts it at the next file-alignment (or
     * sector) boundary instead.  At most three 16-byte reads. */
    {
        uint64_t candidates[3];
        size_t used = 0U, index2, seen;
        candidates[used++] = raw_end;
        if (alignment >= 2U && alignment <= 0x10000U &&
            (alignment & (alignment - 1U)) == 0U)
            candidates[used++] = ejti_align_up(raw_end, alignment);
        candidates[used++] = ejti_align_up(raw_end, 0x200U);
        for (index2 = 0U; index2 < used; ++index2) {
            bool duplicate = false;
            for (seen = 0U; seen < index2; ++seen)
                if (candidates[seen] == candidates[index2]) duplicate = true;
            if (!duplicate &&
                ejti_try_offset(cursor, candidates[index2], container))
                return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */
/* ---------------------------------------------------------------------- */

/* Container names are single bytes (Latin-1); they become UTF-8 here, with
 * '\' turned into '/' and a NUL byte, which a C string cannot carry, into
 * U+FFFD. */
static char *ejti_name_from_bytes(const uint8_t *bytes, size_t length) {
    size_t needed = 0U, index, at = 0U;
    char *name;
    for (index = 0U; index < length; ++index)
        needed += bytes[index] == 0U ? 3U : (bytes[index] < 0x80U ? 1U : 2U);
    name = (char *)xx_mem_alloc(needed + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U) {
            static const char replacement[] = "\xEF\xBF\xBD";
            xx_rt_memcpy(name + at, replacement, 3U);
            at += 3U;
        } else if (c < 0x80U) {
            name[at++] = c == '\\' ? '/' : (char)c;
        } else {
            name[at++] = (char)(0xC0U | (c >> 6U));
            name[at++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    name[at] = 0;
    return name;
}

/* The reference names a member the list leaves unnamed by its index. */
static char *ejti_index_name(uint32_t index) {
    char digits[12];
    size_t count = 0U, at = 0U;
    char *name;
    if (index == 0U) digits[count++] = '0';
    while (index != 0U && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    }
    name = (char *)xx_mem_alloc(count + 1U);
    if (!name) return NULL;
    while (count > 0U) name[at++] = digits[--count];
    name[at] = 0;
    return name;
}

/* "<name> (<index>)" */
static char *ejti_suffixed_name(const char *name, size_t index) {
    char digits[24];
    size_t count = 0U, length = xx_str_len(name), at;
    char *result;
    if (index == 0U) digits[count++] = '0';
    while (index != 0U && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    }
    result = (char *)xx_mem_alloc(length + count + 4U);
    if (!result) return NULL;
    xx_rt_memcpy(result, name, length);
    at = length;
    result[at++] = ' ';
    result[at++] = '(';
    while (count > 0U) result[at++] = digits[--count];
    result[at++] = ')';
    result[at] = 0;
    return result;
}

/* Case-insensitive order over the names this reader builds: ASCII letters
 * and the Latin-1 letters U+00C0..U+00DE / U+00E0..U+00FE (UTF-8 C3 80..9E
 * and C3 A0..BE, the multiplication and division signs excepted) fold
 * together, as a Windows file system folds them.  A C3 lead byte in one
 * name meets a C3 in the other whenever the names agree so far, so the
 * continuation byte can be folded by looking at the previous byte alone. */
static unsigned ejti_fold(unsigned char c, unsigned char previous) {
    if (previous == 0xC3U) {
        if (c >= 0x80U && c <= 0x9EU && c != 0x97U) return c + 0x20U;
        return c;
    }
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int ejti_name_icmp(const char *left, const char *right) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    unsigned char previous = 0U;
    for (;;) {
        unsigned x = ejti_fold(*a, previous);
        unsigned y = ejti_fold(*b, previous);
        if (x != y) return x < y ? -1 : 1;
        if (*a == 0U) return 0;
        previous = *a;
        ++a;
        ++b;
    }
}

static int ejti_compare_keys(const void *left, const void *right) {
    const ejti_key *a = (const ejti_key *)left;
    const ejti_key *b = (const ejti_key *)right;
    int order = ejti_name_icmp(a->name, b->name);
    if (order != 0) return order;
    return a->item < b->item ? -1 : (a->item > b->item ? 1 : 0);
}

/* Names equal but for case would overwrite each other on extraction.  Every
 * later member of such a group gets " (<its index>)" appended; a renamed
 * name could in turn meet a stored one, so the check repeats a bounded
 * number of rounds. */
static bool ejti_make_names_unique(ejti_member *items, size_t count) {
    ejti_key *keys;
    size_t round, index;
    if (count < 2U) return true;
    keys = (ejti_key *)xx_mem_alloc(count * sizeof(*keys));
    if (!keys) return false;
    for (round = 0U; round < EJTI_NAME_ROUNDS; ++round) {
        size_t group = 0U;
        bool changed = false;
        for (index = 0U; index < count; ++index) {
            keys[index].name = items[index].name;
            keys[index].item = index;
        }
        xx_rt_qsort(keys, count, sizeof(*keys), ejti_compare_keys);
        for (index = 1U; index < count; ++index) {
            ejti_member *member;
            char *renamed;
            if (ejti_name_icmp(keys[index].name, keys[group].name) != 0) {
                group = index;
                continue;
            }
            member = &items[keys[index].item];
            renamed = ejti_suffixed_name(member->name, keys[index].item);
            if (!renamed) {
                xx_mem_free(keys);
                return false;
            }
            xx_mem_free(member->name);
            member->name = renamed;
            changed = true;
        }
        if (!changed) {
            xx_mem_free(keys);
            return true;
        }
    }
    xx_mem_free(keys);
    return false;
}

static char ejti_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool ejti_stem_is(const char *stem, size_t length, const char *word) {
    size_t index;
    for (index = 0U; index < length; ++index)
        if (!word[index] || ejti_upper(stem[index]) != word[index])
            return false;
    return word[length] == 0;
}

/* One path component: not empty, not only dots and spaces, not ending in a
 * dot or space (Windows strips those), and not a device name such as CON,
 * LPT1.TXT, COM¹ or CONIN$, with or without an extension, in any case. */
static bool ejti_safe_component(const char *component, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index)
        if (component[index] != '.' && component[index] != ' ')
            meaningful = true;
    if (!meaningful || component[length - 1U] == '.' ||
        component[length - 1U] == ' ')
        return false;
    while (stem < length && component[stem] != '.') ++stem;
    while (stem > 0U && component[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (ejti_stem_is(component, stem, devices[index])) return false;
    if (stem >= 4U &&
        ((ejti_upper(component[0]) == 'C' && ejti_upper(component[1]) == 'O' &&
          ejti_upper(component[2]) == 'M') ||
         (ejti_upper(component[0]) == 'L' && ejti_upper(component[1]) == 'P' &&
          ejti_upper(component[2]) == 'T'))) {
        const uint8_t *tail = (const uint8_t *)component + 3;
        if (stem == 4U && tail[0] >= '0' && tail[0] <= '9') return false;
        /* Superscript one, two and three are device digits too. */
        if (stem == 5U && tail[0] == 0xC2U &&
            (tail[1] == 0xB9U || tail[1] == 0xB2U || tail[1] == 0xB3U))
            return false;
    }
    return true;
}

/* Extraction writes <base>/<name>; the name must stay below <base>. */
static bool ejti_safe_output_name(const char *name) {
    size_t length, index, start = 0U;
    if (!name || !name[0] || name[0] == '/') return false;
    length = xx_str_len(name);
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)name[index];
        if (c < 0x20U || c == 0x7FU || c == '\\' || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    for (index = 0U; index <= length; ++index) {
        if (index == length || name[index] == '/') {
            if (!ejti_safe_component(name + start, index - start))
                return false;
            start = index + 1U;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Parsing                                                                 */
/* ---------------------------------------------------------------------- */

static void ejti_free_items(ejti_member *items, size_t count) {
    size_t index;
    if (!items) return;
    for (index = 0U; index < count; ++index)
        if (items[index].name) xx_mem_free(items[index].name);
    xx_mem_free(items);
}

static void ejti_stream_free(void *opaque) {
    ejti_stream *stream = (ejti_stream *)opaque;
    if (!stream) return;
    ejti_free_items(stream->items, stream->count);
    xx_mem_free(stream);
}

static void ejti_set_product(ejti_stream *stream, const uint8_t *bytes,
                             size_t length) {
    size_t index, at = 0U;
    for (index = 0U; index < length; ++index) {
        uint8_t c = bytes[index];
        size_t need = c < 0x80U ? 1U : 2U;
        if (at + need >= sizeof(stream->product)) break;
        if (c == 0U) {
            stream->product[at++] = '?';
        } else if (c < 0x80U) {
            stream->product[at++] = (char)c;
        } else {
            stream->product[at++] = (char)(0xC0U | (c >> 6U));
            stream->product[at++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    stream->product[at] = 0;
}

/* Walk both variable tables.  Keeps the product name and a copy of the
 * member list; everything else is only stepped over. */
static bool ejti_parse_variables(ejti_cursor *cursor, ejti_stream *stream,
                                 uint8_t **list, uint32_t *list_length,
                                 xx_pd_struct *pd) {
    int64_t position = stream->container_offset + 4;
    uint32_t table;
    bool found = false;
    *list = NULL;
    *list_length = 0U;
    for (table = 0U; table < 2U; ++table) {
        uint8_t word[8];
        int32_t count, index;
        if (!ejti_get(cursor, position, word, 4U)) goto fail;
        count = (int32_t)ejti_le32(word);
        position += 4;
        if (table == 0U) {
            if (count <= 0 || count >= EJTI_MAX_COUNT) goto fail;
            stream->variable_count = (uint32_t)count;
        } else {
            if (count < 0 || count > EJTI_MAX_EXTRA) goto fail;
            stream->extra_count = (uint32_t)count;
        }
        for (index = 0; index < count; ++index) {
            int32_t key, length;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !ejti_get(cursor, position, word, 8U))
                goto fail;
            key = (int32_t)ejti_le32(word);
            length = (int32_t)ejti_le32(word + 4);
            position += 8;
            if (key < 0 || length < 0 ||
                (int64_t)length > cursor->size - position)
                goto fail;
            if (table == 0U && index == 0 && key == EJTI_KEY_PRODUCT) {
                uint8_t value[EJTI_MAX_COUNT];
                if (length >= EJTI_MAX_COUNT ||
                    !ejti_get(cursor, position, value, (size_t)length))
                    goto fail;
                ejti_set_product(stream, value, (size_t)length);
            } else if (table == 0U && key == EJTI_KEY_FILELIST) {
                /* A second list would mean the walk is not reading the
                 * table the format describes. */
                if (found || length > EJTI_MAX_LIST) goto fail;
                if (length > 0) {
                    *list = (uint8_t *)xx_mem_alloc((size_t)length);
                    if (!*list ||
                        !ejti_read_at(cursor->device, cursor->base + position,
                                      *list, (size_t)length))
                        goto fail;
                }
                *list_length = (uint32_t)length;
                found = true;
            }
            position += length;
        }
    }
    if (!found) goto fail;
    stream->records_offset = position;
    return true;
fail:
    if (*list) xx_mem_free(*list);
    *list = NULL;
    *list_length = 0U;
    return false;
}

/* Number of names in the ';'-joined list.  The list is ';'-terminated, so
 * nothing behind the last ';' is not a name; an unterminated tail still is
 * one.  An empty name in the middle is a member without a name. */
static uint32_t ejti_count_names(const uint8_t *list, uint32_t length) {
    uint32_t index, count = 0U;
    for (index = 0U; index < length; ++index)
        if (list[index] == ';') ++count;
    if (length > 0U && list[length - 1U] != ';') ++count;
    return count;
}

/* The trailer behind the last member: E8 E4 13 D5, a big-endian count and
 * that many stored members.  `walk->end` is where it would start. */
static int ejti_parse_trailer(ejti_cursor *cursor, ejti_walk *walk,
                              int64_t base, xx_pd_struct *pd) {
    uint8_t word[8];
    uint32_t count, index;
    int64_t position = walk->end;
    if (position > cursor->size - 8 || !ejti_get(cursor, position, word, 8U) ||
        ejti_le32(word) != EJTI_MAGIC_TAIL)
        return EJTI_WALK_COMPLETE;
    count = ejti_be32(word + 4);
    if ((uint64_t)count > (uint64_t)(walk->capacity - walk->count))
        return EJTI_WALK_BAD;
    walk->has_trailer = true;
    walk->trailer_count = count;
    position += 8;
    walk->end = position;
    for (index = 0U; index < count; ++index) {
        uint8_t name[EJTI_MAX_TRAILER_NAME];
        uint32_t name_length;
        int64_t header = position;
        uint64_t size;
        ejti_member *member;
        if (pd && xx_pd_is_stopped(pd)) return EJTI_WALK_BAD;
        if (position > cursor->size - 2 || !ejti_get(cursor, position, word, 2U))
            return EJTI_WALK_TRUNCATED;
        name_length = ejti_be16(word);
        if (name_length == 0U || name_length > EJTI_MAX_TRAILER_NAME)
            return EJTI_WALK_BAD;
        position += 2;
        if ((int64_t)name_length > cursor->size - position ||
            !ejti_get(cursor, position, name, name_length))
            return EJTI_WALK_TRUNCATED;
        position += (int64_t)name_length;
        if (position > cursor->size - 8 || !ejti_get(cursor, position, word, 8U))
            return EJTI_WALK_TRUNCATED;
        size = ejti_be64(word);
        position += 8;
        if (size > (uint64_t)(cursor->size - position))
            return EJTI_WALK_TRUNCATED;
        member = &walk->items[walk->count];
        member->name = ejti_name_from_bytes(name, name_length);
        if (!member->name) return EJTI_WALK_BAD;
        member->header_offset = base + header;
        member->header_size = (uint32_t)(position - header);
        member->offset = base + position;
        member->size = (int64_t)size;
        member->list_index = index;
        member->stored = true;
        ++walk->count;
        position += (int64_t)size;
        walk->end = position;
    }
    return EJTI_WALK_COMPLETE;
}

/* Walk the listed members in one layout.  Names are attached afterwards. */
static void ejti_walk_members(ejti_cursor *cursor, const ejti_stream *stream,
                              bool padded, int64_t base, ejti_walk *walk,
                              xx_pd_struct *pd) {
    int64_t position = stream->records_offset;
    uint32_t index;
    walk->count = 0U;
    walk->has_trailer = false;
    walk->trailer_count = 0U;
    walk->result = EJTI_WALK_COMPLETE;
    for (index = 0U; index < stream->listed; ++index) {
        uint8_t word[4];
        int64_t header = position;
        uint32_t size;
        ejti_member *member;
        if (pd && xx_pd_is_stopped(pd)) {
            walk->result = EJTI_WALK_BAD;
            return;
        }
        if (position > cursor->size - 4 || !ejti_get(cursor, position, word, 4U)) {
            walk->result = EJTI_WALK_TRUNCATED;
            break;
        }
        size = ejti_le32(word);
        position += 4;
        if (padded) {
            if (position > cursor->size - 4 ||
                !ejti_get(cursor, position, word, 4U)) {
                walk->result = EJTI_WALK_TRUNCATED;
                break;
            }
            if (ejti_le32(word) != 0U) {
                walk->result = EJTI_WALK_LAYOUT;
                return;
            }
            position += 4;
        }
        if ((int64_t)size > cursor->size - position) {
            walk->result = EJTI_WALK_TRUNCATED;
            break;
        }
        member = &walk->items[walk->count++];
        member->name = NULL;
        member->header_offset = base + header;
        member->header_size = (uint32_t)(position - header);
        member->offset = base + position;
        member->size = (int64_t)size;
        member->list_index = index;
        member->stored = false;
        position += (int64_t)size;
    }
    walk->end = position;
    if (walk->result == EJTI_WALK_COMPLETE)
        walk->result = ejti_parse_trailer(cursor, walk, base, pd);
}

static void ejti_walk_reset(ejti_walk *walk) {
    size_t index;
    for (index = 0U; index < walk->count; ++index) {
        if (walk->items[index].name) xx_mem_free(walk->items[index].name);
        walk->items[index].name = NULL;
    }
    walk->count = 0U;
}

/* Name the listed members from the key 2003 list, in list order. */
static bool ejti_attach_names(ejti_member *items, size_t count,
                              const uint8_t *list, uint32_t list_length) {
    uint32_t start = 0U, index, name_index = 0U;
    for (index = 0U; index <= list_length && name_index < count; ++index) {
        ejti_member *member;
        if (index < list_length && list[index] != ';') continue;
        if (index == list_length && start == list_length) break;
        member = &items[name_index];
        if (!member->stored) {
            member->name = index > start
                               ? ejti_name_from_bytes(list + start, index - start)
                               : ejti_index_name(name_index);
            if (!member->name) return false;
        }
        ++name_index;
        start = index + 1U;
    }
    for (index = 0U; index < count; ++index)
        if (!items[index].name) return false;
    return true;
}

/* A walk ends cleanly at the end of the file, at a trailer, or where only
 * zero bytes follow (the launchers pad the file to a 0x200 boundary). */
#define EJTI_CLEAN_WINDOW 64U

static bool ejti_clean_end(ejti_cursor *cursor, const ejti_walk *walk) {
    uint8_t tail[EJTI_CLEAN_WINDOW];
    int64_t available;
    size_t index;
    if (walk->has_trailer || walk->end == cursor->size) return true;
    if (walk->end > cursor->size) return false;
    available = cursor->size - walk->end;
    if (available > (int64_t)EJTI_CLEAN_WINDOW)
        available = (int64_t)EJTI_CLEAN_WINDOW;
    if (!ejti_get(cursor, walk->end, tail, (size_t)available)) return false;
    for (index = 0U; index < (size_t)available; ++index)
        if (tail[index] != 0U) return false;
    return true;
}

static bool ejti_parse(Abstractformat *format, ejti_stream **result,
                       xx_pd_struct *pd) {
    ejti_cursor *cursor = NULL;
    ejti_stream *stream = NULL;
    uint8_t *list = NULL;
    uint32_t list_length = 0U;
    ejti_walk walk;
    int64_t total, container = 0;
    bool padded = false;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    *result = NULL;
    xx_mem_zero(&walk, sizeof(walk));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    cursor = (ejti_cursor *)xx_mem_alloc(sizeof(*cursor));
    if (!cursor) return false;
    cursor->device = format->device;
    cursor->base = format->base_address;
    cursor->size = total - format->base_address;
    cursor->start = 0;
    cursor->length = 0U;
    /* Every MZ file reaches this probe: the PE headers and the 16-byte
     * container head are checked before anything else is allocated. */
    if (!ejti_locate(cursor, &container)) goto fail;
    stream = (ejti_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->container_offset = container;
    if (!ejti_parse_variables(cursor, stream, &list, &list_length, pd))
        goto fail;
    stream->listed = ejti_count_names(list, list_length);
    if (stream->listed == 0U || stream->listed > EJTI_MAX_MEMBERS) goto fail;

    walk.capacity = EJTI_MAX_MEMBERS;
    walk.items = (ejti_member *)xx_mem_calloc(walk.capacity,
                                              sizeof(*walk.items));
    if (!walk.items) goto fail;

    /* The zero word behind the first size says "later build".  The padded
     * layout is then walked for every member.  A padded walk that reaches
     * the trailer settles it.  When a later member lacks its zero word, or
     * the padded walk found no trailer or ran out of file, the first
     * member's plaintext may simply have opened with 88 88 88 88 in an old
     * build: the plain layout is taken instead if it walks completely and
     * ends cleanly (at the end of the file, before zero padding or at a
     * trailer).  Otherwise the padded reading stands; re-reading a padded
     * container as plain would shift every offset by four. */
    if (stream->records_offset <= cursor->size - 8) {
        uint8_t word[8];
        if (ejti_get(cursor, stream->records_offset, word, 8U) &&
            ejti_le32(word + 4) == 0U)
            padded = true;
    }
    ejti_walk_members(cursor, stream, padded, format->base_address, &walk,
                      pd);
    if (padded && walk.result != EJTI_WALK_BAD &&
        !(walk.result == EJTI_WALK_COMPLETE && walk.has_trailer)) {
        bool layout = walk.result == EJTI_WALK_LAYOUT;
        ejti_walk_reset(&walk);
        ejti_walk_members(cursor, stream, false, format->base_address, &walk,
                          pd);
        if (walk.result == EJTI_WALK_COMPLETE &&
            ejti_clean_end(cursor, &walk)) {
            padded = false;
        } else if (layout) {
            /* Neither layout walks: a zero word is missing in the middle of
             * a padded container, or the plain reading runs off the end. */
            goto fail;
        } else {
            ejti_walk_reset(&walk);
            ejti_walk_members(cursor, stream, true, format->base_address,
                              &walk, pd);
        }
    }
    if (walk.result != EJTI_WALK_COMPLETE &&
        walk.result != EJTI_WALK_TRUNCATED)
        goto fail;
    if (!ejti_attach_names(walk.items, walk.count, list, list_length) ||
        !ejti_make_names_unique(walk.items, walk.count))
        goto fail;

    stream->padded = padded;
    stream->has_trailer = walk.has_trailer;
    stream->trailer_count = walk.trailer_count;
    stream->truncated = walk.result == EJTI_WALK_TRUNCATED;
    stream->archive_size = stream->truncated ? cursor->size : walk.end;
    stream->count = walk.count;
    if (walk.count > 0U) {
        stream->items = (ejti_member *)xx_mem_alloc(walk.count *
                                                    sizeof(*stream->items));
        if (!stream->items) goto fail;
        xx_rt_memcpy(stream->items, walk.items,
                     walk.count * sizeof(*stream->items));
    }
    xx_mem_free(walk.items);
    if (list) xx_mem_free(list);
    xx_mem_free(cursor);
    *result = stream;
    return true;
fail:
    if (walk.items) ejti_free_items(walk.items, walk.count);
    if (list) xx_mem_free(list);
    if (stream) {
        stream->count = 0U;
        ejti_stream_free(stream);
    }
    if (cursor) xx_mem_free(cursor);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */
/* ---------------------------------------------------------------------- */

/* Stream `size` bytes at `offset` to `destination` (or only read them when
 * it is NULL), undoing the XOR unless the member is stored. */
static bool ejti_copy_member(xx_io_device *source, const ejti_member *member,
                             xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0;
    bool ok = true;
    if (!source || member->offset < 0 || member->size < 0) return false;
    if (member->size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(EJTI_COPY_CHUNK);
    if (!buffer) return false;
    while (ok && done < member->size) {
        size_t chunk = member->size - done > (int64_t)EJTI_COPY_CHUNK
                           ? (size_t)EJTI_COPY_CHUNK
                           : (size_t)(member->size - done);
        size_t written = 0U, index;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !ejti_read_at(source, member->offset + done, buffer, chunk)) {
            ok = false;
            break;
        }
        if (!member->stored)
            for (index = 0U; index < chunk; ++index) buffer[index] ^= EJTI_XOR;
        while (destination && written < chunk) {
            ssize_t amount = xx_io_write(destination, buffer + written,
                                         chunk - written);
            if (amount <= 0 || (size_t)amount > chunk - written) {
                ok = false;
                break;
            }
            written += (size_t)amount;
        }
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

static bool ejti_copy_options(xx_list_s *destination,
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

static const xx_var *ejti_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ejti_set_record(xx_archive_record *record,
                            const ejti_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSION_METHOD,
               member->stored ? XX_EJ_TECHNOLOGIES_INSTALL_METHOD_STORED
                              : XX_EJ_TECHNOLOGIES_INSTALL_METHOD_XOR88) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */
/* ---------------------------------------------------------------------- */

void xx_ej_technologies_install_init(xx_ej_technologies_install *archive,
                                     xx_io_device *device,
                                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_EJ_TECHNOLOGIES_INSTALL_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-install4j-launcher");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_ej_technologies_install_check_is_valid;
    archive->format.handle_base_info =
        xx_ej_technologies_install_handle_base_info;
    archive->format.get_format_size =
        xx_ej_technologies_install_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ej_technologies_install_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ej_technologies_install_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ej_technologies_install_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ej_technologies_install_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ej_technologies_install_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ej_technologies_install_free_archive_records_reading;
    archive->container_offset = -1;
    archive->records_offset = -1;
}

xx_ej_technologies_install *xx_ej_technologies_install_create(
    xx_io_device *device, int64_t base_address) {
    xx_ej_technologies_install *archive =
        (xx_ej_technologies_install *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ej_technologies_install_init(archive, device, base_address);
    return archive;
}

void xx_ej_technologies_install_destroy(xx_ej_technologies_install *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ej_technologies_install_free(xx_ej_technologies_install *archive) {
    if (!archive) return;
    xx_ej_technologies_install_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ej_technologies_install_check_is_valid(Abstractformat *format,
                                               xx_pd_struct *pd) {
    ejti_stream *stream;
    if (!ejti_parse(format, &stream, pd)) return false;
    ejti_stream_free(stream);
    return true;
}

bool xx_ej_technologies_install_handle_base_info(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    ejti_stream *stream;
    xx_ej_technologies_install *archive;
    if (!format || !ejti_parse(format, &stream, pd)) return false;
    archive = (xx_ej_technologies_install *)format;
    archive->number_of_records = stream->count;
    archive->container_offset = stream->container_offset;
    archive->records_offset = stream->records_offset;
    archive->variable_count = stream->variable_count;
    archive->extra_count = stream->extra_count;
    archive->listed_members = stream->listed;
    archive->trailer_members = stream->trailer_count;
    archive->padded = stream->padded;
    archive->has_trailer = stream->has_trailer;
    archive->truncated = stream->truncated;
    xx_rt_memcpy(archive->product_name, stream->product,
                 sizeof(archive->product_name));
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    ejti_stream_free(stream);
    return true;
}

int64_t xx_ej_technologies_install_get_format_size(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ej_technologies_install_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_ej_technologies_install_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ej_technologies_install_handle_base_info(format, pd))
               ? ((xx_ej_technologies_install *)format)->number_of_records
               : 0U;
}

const char *xx_ej_technologies_install_get_product_name(
    const xx_ej_technologies_install *archive) {
    return archive ? archive->product_name : "";
}

xx_archive_record_state *
xx_ej_technologies_install_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ejti_stream *stream;
    xx_archive_record_state *state;
    if (!ejti_parse(format, &stream, pd)) return NULL;
    if (stream->count == 0U) {
        ejti_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ejti_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ejti_stream_free;
    state->total_records = stream->count;
    if (!ejti_copy_options(&state->options, options) ||
        !ejti_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ej_technologies_install_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_ej_technologies_install_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    ejti_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ejti_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ejti_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ej_technologies_install_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    ejti_stream *stream;
    const ejti_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ejti_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = ejti_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: read the member through, which verifies it. */
        return ejti_copy_member(format->device, member, NULL, pd);
    if (!ejti_safe_output_name(member->name)) return false;
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = ejti_copy_member(format->device, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ej_technologies_install_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
