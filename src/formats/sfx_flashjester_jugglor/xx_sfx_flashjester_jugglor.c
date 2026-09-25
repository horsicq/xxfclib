/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FlashJester Jugglor 2.x bundles and the "Exe Attachment" variant: a
 * Delphi PE stub with its files appended in the PE overlay, each one a
 * fixed-size header and a zlib stream.  xx_sfx_flashjester_jugglor.h
 * carries the field tables.
 *
 * Only the PE headers are parsed, to find the overlay; nothing in the stub
 * is run or emulated.  The record chain is walked forward from the overlay
 * the way U3's "SFX FlashJester" and "SFX ExeAttachment" handlers walk it,
 * and a Jugglor chain must end in its 220-byte trailer, whose fields are
 * cross-checked as XArchive archives/xjugglor.cpp checks them (MIT License,
 * Copyright (c) 2026 hors): the overlay offset, the chain size, the member
 * count, the zero word, the sum of the unpacked sizes and the
 * "Jester Jugglor Version " string.  Walking forward, the payload does not
 * have to end the file, so an embedded bundle is found as well.
 *
 * Every member is inflated through a counting sink: the stream may not
 * produce more than its declared size, must produce exactly that size, and
 * its Adler-32 must match.  Names are Delphi ShortStrings in the ANSI code
 * page (converted from Windows-1252 to UTF-8); the source directory is kept
 * without its drive, as U3 lays the files out.  Components that would alias
 * something on Windows ("..", "CON", "a:b", a trailing dot) are listed but
 * refused on extraction, and later duplicates (ASCII case-insensitive) get a
 * "_<member number>" suffix before the extension.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested; this
 * picks up the real file type as soon as the format is registered. */
#ifdef SFX_FLASHJESTER_JUGGLOR
#define XX_SFX_FLASHJESTER_JUGGLOR_FILE_TYPE \
    XX_FILE_TYPE_SFX_FLASHJESTER_JUGGLOR
#else
#define XX_SFX_FLASHJESTER_JUGGLOR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* ---- layout ------------------------------------------------------------ */

#define JG_MAGIC UINT32_C(0x6A4A61A3)
#define JG_SEAL UINT32_C(0x8CF9BF0D)

#define JG_NAME_AT 0x004U
#define JG_DIR_AT 0x104U

/* Jugglor 2.x member header and trailer. */
#define JG_J_HEADER 0x31CU
#define JG_J_USIZE_AT 0x304U
#define JG_J_CSIZE_AT 0x308U
#define JG_J_TIME_AT 0x30CU
#define JG_J_SEAL_AT 0x318U
#define JG_TRAILER 0xDCU
#define JG_TRAILER_SEAL_AT 0xD8U
#define JG_TRAILER_VERSION_AT 0x18U
/* The version ShortString ends before the unknown dword at +0xD4. */
#define JG_TRAILER_VERSION_MAX (0xD4U - JG_TRAILER_VERSION_AT - 1U)
#define JG_VERSION_PREFIX "Jester Jugglor Version "

/* Exe Attachment header and the record behind every stream. */
#define JG_A_HEADER 0x220U
#define JG_A_USIZE_AT 0x204U
#define JG_A_CSIZE_AT 0x208U
#define JG_A_TIME_AT 0x20CU
#define JG_A_SEAL_AT 0x21CU
#define JG_A_RECORD 0x116U
#define JG_A_RECORD_SEAL_AT 0x112U

/* ---- limits ------------------------------------------------------------ */

/* Smallest zlib stream: two header bytes, an empty two-byte block and the
 * Adler-32. */
#define JG_MIN_STREAM 8U
/* Deflate cannot expand past 1032:1, so a larger declared size is garbage. */
#define JG_MAX_RATIO UINT64_C(1032)
#define JG_MAX_MEMBERS 16384U
#define JG_MAX_PE_SECTIONS 96U
#define JG_PE_ROW 40U
#define JG_MIN_OPTIONAL 0x60U
#define JG_MAX_OPTIONAL 0x1000U
#define JG_RENAME_ROUNDS 4U
/* A ShortString of 255 Windows-1252 bytes is at most 765 UTF-8 bytes. */
#define JG_NAME_BUFFER (3U * (255U + 255U) + 16U)

typedef struct jg_member {
    char *name;         /**< Published relative path, UTF-8, '/'-separated. */
    int64_t header;     /**< Header offset from the base. */
    int64_t data;       /**< zlib stream offset from the base. */
    int64_t packed;     /**< zlib stream size. */
    uint64_t size;      /**< Declared unpacked size. */
    uint64_t filetime;  /**< Raw FILETIME-shaped last-write value. */
    uint32_t header_size;
    bool unsafe;        /**< Listed, but refused on extraction. */
    bool pending;       /**< Scratch flag of jg_resolve_duplicates. */
} jg_member;

typedef struct jg_layout {
    uint32_t variant;
    int64_t size;       /**< Bytes from the base to the end of the device. */
    int64_t overlay;    /**< First header, from the base. */
    int64_t end;        /**< End of the trailer / last record. */
    uint64_t count;
    uint64_t unpacked;
    char version[JG_TRAILER_VERSION_MAX + 1U];
} jg_layout;

/* ---- helpers ----------------------------------------------------------- */

static uint32_t jg_le16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U);
}

static uint32_t jg_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint32_t jg_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static uint64_t jg_le64(const uint8_t *p) {
    return (uint64_t)jg_le32(p) | ((uint64_t)jg_le32(p + 4U) << 32U);
}

static bool jg_read_at(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* RFC 1950 header: method 8, a window of at most 32 KiB, the check bits,
 * and no preset dictionary. */
static bool jg_zlib_header_ok(const uint8_t *h) {
    return (h[0] & 0x0FU) == 8U && (h[0] >> 4U) <= 7U &&
           ((((uint32_t)h[0] << 8U) | h[1]) % 31U) == 0U &&
           (h[1] & 0x20U) == 0U;
}

/* A ShortString field: a length byte and printable (non-control) bytes. */
static bool jg_short_string_ok(const uint8_t *field, bool allow_empty) {
    uint32_t length = field[0], index;
    if (length == 0U) return allow_empty;
    for (index = 1U; index <= length; ++index)
        if (field[index] < 0x20U || field[index] == 0x7FU) return false;
    return true;
}

/* End of the last section's raw data, from the headers only: DOS header,
 * NT signature, file header, optional-header magic and the section table. */
static bool jg_pe_overlay(Abstractformat *format, int64_t size,
                          int64_t *overlay) {
    uint8_t header[64];
    uint8_t nt[24];
    uint8_t optional[2];
    uint8_t table[JG_MAX_PE_SECTIONS * JG_PE_ROW];
    int64_t base = format->base_address;
    int64_t nt_offset, table_offset, end = 0;
    uint32_t sections, optional_size, index, magic;
    if (size < (int64_t)sizeof(header) ||
        !jg_read_at(format->device, base, header, sizeof(header)) ||
        header[0] != 'M' || header[1] != 'Z')
        return false;
    nt_offset = (int64_t)jg_le32(header + 0x3CU);
    if (nt_offset < 4 || nt_offset > size - (int64_t)sizeof(nt) ||
        !jg_read_at(format->device, base + nt_offset, nt, sizeof(nt)) ||
        xx_rt_memcmp(nt, "PE\0\0", 4U) != 0)
        return false;
    sections = jg_le16(nt + 6U);
    optional_size = jg_le16(nt + 20U);
    if (sections == 0U || sections > JG_MAX_PE_SECTIONS ||
        optional_size < JG_MIN_OPTIONAL || optional_size > JG_MAX_OPTIONAL ||
        nt_offset + 24 > size - (int64_t)sizeof(optional) ||
        !jg_read_at(format->device, base + nt_offset + 24, optional,
                    sizeof(optional)))
        return false;
    magic = jg_le16(optional);
    if (magic != 0x10BU && magic != 0x20BU) return false;
    table_offset = nt_offset + 24 + (int64_t)optional_size;
    if (table_offset > size ||
        (int64_t)(sections * JG_PE_ROW) > size - table_offset ||
        !jg_read_at(format->device, base + table_offset, table,
                    sections * JG_PE_ROW))
        return false;
    for (index = 0U; index < sections; ++index) {
        const uint8_t *row = table + index * JG_PE_ROW;
        int64_t raw_size = (int64_t)jg_le32(row + 16U);
        int64_t raw_offset = (int64_t)jg_le32(row + 20U);
        if (raw_size == 0) continue;
        if (raw_offset + raw_size > end) end = raw_offset + raw_size;
    }
    if (end < table_offset + (int64_t)(sections * JG_PE_ROW) || end >= size)
        return false;
    *overlay = end;
    return true;
}

/* ---- names ------------------------------------------------------------- */

/* Windows-1252 0x80..0x9F; 0 marks the five unassigned bytes. */
static const uint16_t jg_cp1252_high[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

static size_t jg_put_utf8(char *out, uint8_t byte) {
    uint32_t code = byte;
    if (byte < 0x80U) {
        out[0] = (char)byte;
        return 1U;
    }
    if (byte < 0xA0U) {
        code = jg_cp1252_high[byte - 0x80U];
        if (code == 0U) {
            out[0] = '_';
            return 1U;
        }
    }
    if (code < 0x800U) {
        out[0] = (char)(0xC0U | (code >> 6U));
        out[1] = (char)(0x80U | (code & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code >> 12U));
    out[1] = (char)(0x80U | ((code >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (code & 0x3FU));
    return 3U;
}

static uint8_t jg_upper(uint8_t c) {
    return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 'a' + 'A') : c;
}

/* True when the @p stem bytes of @p text spell @p word, ignoring case. */
static bool jg_stem_is(const uint8_t *text, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || jg_upper(text[index]) != (uint8_t)word[index])
            return false;
    return word[stem] == 0;
}

/* One path component, as raw ANSI bytes, that Windows would store under
 * exactly that name: no reserved punctuation, not only dots and spaces, no
 * trailing dot or space, and no device name with or without an extension
 * (COM/LPT also with the superscript digits Windows treats as digits). */
static bool jg_component_safe(const uint8_t *text, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t index, stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || text[length - 1U] == '.' || text[length - 1U] == ' ')
        return false;
    while (stem < length && text[stem] != '.') ++stem;
    while (stem > 0U && text[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (jg_stem_is(text, stem, devices[index])) return false;
    if (stem == 4U &&
        ((text[3] >= '0' && text[3] <= '9') || text[3] == 0xB9U ||
         text[3] == 0xB2U || text[3] == 0xB3U) &&
        (jg_stem_is(text, 3U, "COM") || jg_stem_is(text, 3U, "LPT")))
        return false;
    return true;
}

/* "<directory without drive>/<name>".  The directory is split on both
 * separators; empty components are dropped, and so is a leading "X:".
 * The name is built on the stack and returned in an exact-size copy, so a
 * list of many members holds only what their names need. */
static char *jg_build_name(const uint8_t *dir_field, const uint8_t *name_field,
                           bool *unsafe) {
    char out[JG_NAME_BUFFER];
    char *copy;
    const uint8_t *dir = dir_field + 1U;
    const uint8_t *name = name_field + 1U;
    size_t dir_length = dir_field[0], name_length = name_field[0];
    size_t position = 0U, start = 0U, index;
    bool first = true;
    *unsafe = false;
    while (start < dir_length) {
        size_t stop = start;
        while (stop < dir_length && dir[stop] != '\\' && dir[stop] != '/')
            ++stop;
        if (stop > start) {
            size_t length = stop - start;
            bool drive = first && start == 0U && length == 2U &&
                         dir[1] == ':' &&
                         ((dir[0] >= 'A' && dir[0] <= 'Z') ||
                          (dir[0] >= 'a' && dir[0] <= 'z'));
            if (!drive) {
                if (!jg_component_safe(dir + start, length)) *unsafe = true;
                for (index = start; index < stop; ++index)
                    position += jg_put_utf8(out + position, dir[index]);
                out[position++] = '/';
            }
            first = false;
        }
        start = stop + 1U;
    }
    if (!jg_component_safe(name, name_length)) *unsafe = true;
    for (index = 0U; index < name_length; ++index)
        position += jg_put_utf8(out + position, name[index]);
    copy = (char *)xx_mem_alloc(position + 1U);
    if (!copy) return NULL;
    xx_rt_memcpy(copy, out, position);
    copy[position] = 0;
    return copy;
}

static int jg_compare_folded(const char *a, const char *b) {
    for (;; ++a, ++b) {
        uint8_t x = jg_upper((uint8_t)*a), y = jg_upper((uint8_t)*b);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}

typedef struct jg_key {
    const char *name;
    uint32_t index;
} jg_key;

static int jg_compare_keys(const void *left, const void *right) {
    const jg_key *a = (const jg_key *)left, *b = (const jg_key *)right;
    int order = jg_compare_folded(a->name, b->name);
    if (order != 0) return order;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* "<stem>_<number><extension>": the suffix goes in front of the last
 * component's extension. */
static char *jg_suffixed(const char *name, uint32_t number) {
    char suffix[16];
    size_t length = xx_str_len(name), cut = length, suffix_length, at;
    char *result;
    for (at = length; at > 0U; --at) {
        if (name[at - 1U] == '/') break;
        if (name[at - 1U] == '.' && at - 1U > 0U && name[at - 2U] != '/') {
            cut = at - 1U;
            break;
        }
    }
    if (xx_rt_snprintf(suffix, sizeof(suffix), "_%u", (unsigned)number) <= 0)
        return NULL;
    suffix_length = xx_str_len(suffix);
    result = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, name, cut);
    xx_rt_memcpy(result + cut, suffix, suffix_length);
    xx_rt_memcpy(result + cut + suffix_length, name + cut, length - cut);
    result[length + suffix_length] = 0;
    return result;
}

/* No two members may be written to the same file.  The first of a group of
 * equal names keeps it; the others get "_<member number>".  A renamed name
 * can meet another one, so this repeats; whatever still collides after a
 * few rounds is refused for extraction. */
static bool jg_resolve_duplicates(jg_member *members, uint64_t count) {
    jg_key *keys;
    uint32_t round;
    uint64_t index;
    if (count < 2U) return true;
    keys = (jg_key *)xx_mem_alloc((size_t)count * sizeof(*keys));
    if (!keys) return false;
    for (round = 0U; round <= JG_RENAME_ROUNDS; ++round) {
        bool renamed = false;
        for (index = 0U; index < count; ++index) {
            keys[index].name = members[index].name;
            keys[index].index = (uint32_t)index;
            members[index].pending = false;
        }
        xx_rt_qsort(keys, (size_t)count, sizeof(*keys), jg_compare_keys);
        for (index = 1U; index < count; ++index)
            if (jg_compare_folded(keys[index].name, keys[index - 1U].name) == 0)
                members[keys[index].index].pending = true;
        for (index = 0U; index < count; ++index) {
            jg_member *member = &members[index];
            char *replacement;
            if (!member->pending) continue;
            if (round == JG_RENAME_ROUNDS) {
                member->unsafe = true;
                continue;
            }
            replacement = jg_suffixed(member->name, (uint32_t)index + 1U);
            if (!replacement) {
                xx_mem_free(keys);
                return false;
            }
            xx_mem_free(member->name);
            member->name = replacement;
            renamed = true;
        }
        if (!renamed) break;
    }
    xx_mem_free(keys);
    return true;
}

/* ---- walk -------------------------------------------------------------- */

/* One member header at @p position (from the base).  @p h holds @p avail
 * bytes read there -- the header and, right behind it, the first two bytes
 * of the zlib stream; @p limit is the number of bytes from the base to the
 * end of the device. */
static bool jg_member_parse(const uint8_t *h, size_t avail, uint32_t variant,
                            int64_t position, int64_t limit,
                            jg_member *member) {
    bool jugglor = variant == XX_SFX_FLASHJESTER_JUGGLOR_VARIANT_JUGGLOR;
    uint32_t header_size = jugglor ? JG_J_HEADER : JG_A_HEADER;
    uint64_t size, packed;
    int64_t data;
    if (avail < (size_t)header_size + 2U || jg_le32(h) != JG_MAGIC ||
        jg_le32(h + (jugglor ? JG_J_SEAL_AT : JG_A_SEAL_AT)) != JG_SEAL ||
        !jg_short_string_ok(h + JG_NAME_AT, false) ||
        !jg_short_string_ok(h + JG_DIR_AT, true) ||
        !jg_zlib_header_ok(h + header_size))
        return false;
    size = jg_le32(h + (jugglor ? JG_J_USIZE_AT : JG_A_USIZE_AT));
    packed = jg_le32(h + (jugglor ? JG_J_CSIZE_AT : JG_A_CSIZE_AT));
    if (packed < JG_MIN_STREAM || size > packed * JG_MAX_RATIO) return false;
    data = position + (int64_t)header_size;
    if (data > limit || (int64_t)packed > limit - data) return false;
    xx_mem_zero(member, sizeof(*member));
    member->header = position;
    member->header_size = header_size;
    member->data = data;
    member->packed = (int64_t)packed;
    member->size = size;
    member->filetime =
        jg_le64(h + (jugglor ? JG_J_TIME_AT : JG_A_TIME_AT));
    return true;
}

static bool jg_trailer_parse(const uint8_t *t, const jg_layout *layout,
                             int64_t position, char *version) {
    uint32_t length = t[JG_TRAILER_VERSION_AT], index;
    const uint8_t *text = t + JG_TRAILER_VERSION_AT + 1U;
    size_t prefix = sizeof(JG_VERSION_PREFIX) - 1U;
    if (layout->overlay > (int64_t)UINT32_MAX ||
        position - layout->overlay > (int64_t)UINT32_MAX ||
        jg_le32(t + 4U) != (uint32_t)layout->overlay ||
        jg_le32(t + 8U) != (uint32_t)(position - layout->overlay) ||
        (uint64_t)jg_le32(t + 12U) != layout->count ||
        jg_le32(t + 16U) != 0U ||
        jg_le32(t + 20U) != (uint32_t)layout->unpacked ||
        length < prefix || length > JG_TRAILER_VERSION_MAX)
        return false;
    for (index = 0U; index < length; ++index)
        if (text[index] < 0x20U || text[index] > 0x7EU) return false;
    if (xx_rt_memcmp(text, JG_VERSION_PREFIX, prefix) != 0) return false;
    xx_rt_memcpy(version, text + prefix, length - prefix);
    version[length - prefix] = 0;
    return true;
}

/* Room for one more entry in a growing member list. */
static bool jg_list_reserve(jg_member **list, uint64_t *capacity,
                            uint64_t count) {
    uint64_t grown;
    jg_member *bigger;
    if (count < *capacity) return true;
    grown = *capacity ? *capacity * 2U : 16U;
    if (grown > JG_MAX_MEMBERS) grown = JG_MAX_MEMBERS;
    if (grown <= count) return false;
    bigger = (jg_member *)xx_mem_realloc(*list,
                                         (size_t)grown * sizeof(jg_member));
    if (!bigger) return false;
    xx_mem_zero(bigger + *capacity,
                (size_t)(grown - *capacity) * sizeof(jg_member));
    *list = bigger;
    *capacity = grown;
    return true;
}

/* Walk the chain from the overlay.  With @p list the members are recorded
 * and named in a list grown as needed (the caller frees it, also after a
 * failure); without, this only validates. */
static bool jg_walk(Abstractformat *format, jg_layout *layout,
                    jg_member **list, uint64_t *capacity, xx_pd_struct *pd) {
    uint8_t h[JG_J_HEADER + 2U];
    int64_t total, position;
    size_t avail;
    if (!format || !format->device || !layout || format->base_address < 0 ||
        (list && !capacity))
        return false;
    xx_mem_zero(layout, sizeof(*layout));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    layout->size = total - format->base_address;
    if (!jg_pe_overlay(format, layout->size, &layout->overlay)) return false;
    position = layout->overlay;

    /* The first header decides the variant. */
    avail = layout->size - position < (int64_t)sizeof(h)
                ? (size_t)(layout->size - position) : sizeof(h);
    if (avail < JG_A_HEADER + 2U ||
        !jg_read_at(format->device, format->base_address + position, h,
                    avail) ||
        jg_le32(h) != JG_MAGIC)
        return false;
    if (avail >= JG_J_HEADER + 2U && jg_le32(h + JG_J_SEAL_AT) == JG_SEAL)
        layout->variant = XX_SFX_FLASHJESTER_JUGGLOR_VARIANT_JUGGLOR;
    else if (jg_le32(h + JG_A_SEAL_AT) == JG_SEAL)
        layout->variant = XX_SFX_FLASHJESTER_JUGGLOR_VARIANT_EXE_ATTACHMENT;
    else
        return false;

    for (;;) {
        jg_member member;
        int64_t remaining = layout->size - position;
        size_t want = layout->variant ==
                              XX_SFX_FLASHJESTER_JUGGLOR_VARIANT_JUGGLOR
                          ? JG_J_HEADER + 2U : JG_A_HEADER + 2U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        avail = remaining < (int64_t)want ? (size_t)remaining : want;
        if (layout->variant == XX_SFX_FLASHJESTER_JUGGLOR_VARIANT_JUGGLOR) {
            /* Each step is either the trailer or another member. */
            if (remaining < (int64_t)JG_TRAILER ||
                !jg_read_at(format->device, format->base_address + position,
                            h, avail))
                return false;
            if (jg_le32(h) == JG_MAGIC &&
                jg_le32(h + JG_TRAILER_SEAL_AT) == JG_SEAL) {
                if (layout->count == 0U ||
                    !jg_trailer_parse(h, layout, position, layout->version))
                    return false;
                layout->end = position + (int64_t)JG_TRAILER;
                return true;
            }
        } else {
            /* Exe Attachment groups run until something else follows. */
            if (remaining < (int64_t)JG_A_HEADER ||
                !jg_read_at(format->device, format->base_address + position,
                            h, avail) ||
                jg_le32(h) != JG_MAGIC ||
                jg_le32(h + JG_A_SEAL_AT) != JG_SEAL) {
                if (layout->count == 0U) return false;
                layout->end = position;
                return true;
            }
        }
        if (layout->count >= JG_MAX_MEMBERS ||
            !jg_member_parse(h, avail, layout->variant, position,
                             layout->size, &member))
            return false;
        if (list) {
            bool unsafe = false;
            if (!jg_list_reserve(list, capacity, layout->count)) return false;
            member.name = jg_build_name(h + JG_DIR_AT, h + JG_NAME_AT, &unsafe);
            if (!member.name) return false;
            member.unsafe = unsafe;
            (*list)[layout->count] = member;
        }
        ++layout->count;
        layout->unpacked += member.size;
        position = member.data + member.packed;
        if (layout->variant ==
            XX_SFX_FLASHJESTER_JUGGLOR_VARIANT_EXE_ATTACHMENT) {
            uint8_t record[JG_A_RECORD];
            if (layout->size - position < (int64_t)JG_A_RECORD ||
                !jg_read_at(format->device, format->base_address + position,
                            record, sizeof(record)) ||
                jg_le32(record) != JG_MAGIC ||
                jg_le32(record + JG_A_RECORD_SEAL_AT) != JG_SEAL)
                return false;
            position += (int64_t)JG_A_RECORD;
        }
    }
}

typedef struct jg_stream {
    jg_member *members;
    uint64_t capacity;  /**< Allocated entries; names beyond count are NULL. */
    uint64_t count;
    uint64_t index;
} jg_stream;

static void jg_stream_free(void *opaque) {
    jg_stream *stream = (jg_stream *)opaque;
    uint64_t index;
    if (!stream) return;
    if (stream->members) {
        for (index = 0U; index < stream->capacity; ++index)
            if (stream->members[index].name)
                xx_mem_free(stream->members[index].name);
        xx_mem_free(stream->members);
    }
    xx_mem_free(stream);
}

/* One walk that records and names every member, then the duplicate pass. */
static jg_stream *jg_stream_load(Abstractformat *format, xx_pd_struct *pd) {
    jg_layout layout;
    jg_stream *stream = (jg_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!jg_walk(format, &layout, &stream->members, &stream->capacity, pd) ||
        layout.count == 0U) {
        jg_stream_free(stream);
        return NULL;
    }
    stream->count = layout.count;
    if (!jg_resolve_duplicates(stream->members, stream->count)) {
        jg_stream_free(stream);
        return NULL;
    }
    return stream;
}

/* ---- inflate ----------------------------------------------------------- */

typedef struct jg_sink {
    xx_io_device *target;
    uint64_t expected;
    uint64_t written;
    uint32_t a;
    uint32_t b;
    bool failed;
} jg_sink;

static ssize_t jg_sink_write(xx_io_device *self, const void *buffer,
                             size_t size) {
    jg_sink *sink = self ? (jg_sink *)self->priv : NULL;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t left = size, done = 0U;
    if (!sink || (!buffer && size != 0U) || sink->failed ||
        size > ((size_t)-1 >> 1U))
        return -1;
    if ((uint64_t)size > sink->expected - sink->written) {
        sink->failed = true;
        return -1;
    }
    while (left > 0U) {
        size_t chunk = left < 5552U ? left : 5552U, index;
        for (index = 0U; index < chunk; ++index) {
            sink->a += bytes[size - left + index];
            sink->b += sink->a;
        }
        sink->a %= 65521U;
        sink->b %= 65521U;
        left -= chunk;
    }
    while (sink->target && done < size) {
        ssize_t amount = xx_io_write(sink->target, bytes + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) {
            sink->failed = true;
            return -1;
        }
        done += (size_t)amount;
    }
    sink->written += size;
    return (ssize_t)size;
}

/* Inflate one member into @p destination (NULL only verifies). */
static bool jg_inflate(Abstractformat *format, const jg_member *member,
                       xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t zlib[2], adler[4];
    int64_t data = format->base_address + member->data;
    jg_sink sink;
    xx_io_device device;
    bool result;
    if (member->packed < (int64_t)JG_MIN_STREAM ||
        !jg_read_at(format->device, data, zlib, sizeof(zlib)) ||
        !jg_zlib_header_ok(zlib) ||
        !jg_read_at(format->device, data + member->packed - 4, adler,
                    sizeof(adler)))
        return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.target = destination;
    sink.expected = member->size;
    sink.a = 1U;
    xx_mem_zero(&device, sizeof(device));
    device.write = jg_sink_write;
    device.priv = &sink;
    result = xx_deflate_unpack_device(format->device, data + 2,
                                      member->packed - 6, &device, false, pd);
    return result && !sink.failed && sink.written == sink.expected &&
           ((sink.b << 16U) | sink.a) == jg_be32(adler);
}

/* ---- records ----------------------------------------------------------- */

static bool jg_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool jg_set_record(xx_archive_record *record, Abstractformat *format,
                          const jg_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header;
    record->header_size = member->header_size;
    record->data_offset = format->base_address + member->data;
    record->compressed_size = member->packed;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->packed) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        member->size) ||
        !xx_archive_record_set_meta_u64(
            record, XX_META_ID_COMPRESSION_METHOD,
            XX_SFX_FLASHJESTER_JUGGLOR_METHOD_ZLIB) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (member->filetime != 0U &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        member->filetime))
        return false;
    return true;
}

static const jg_member *jg_current(Abstractformat *format,
                                   xx_archive_record_state *state) {
    jg_stream *stream;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (jg_stream *)state->internal_state) ||
        stream->index >= stream->count)
        return NULL;
    return &stream->members[stream->index];
}

/* ---- public API -------------------------------------------------------- */

void xx_sfx_flashjester_jugglor_init(xx_sfx_flashjester_jugglor *archive,
                                     xx_io_device *device,
                                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_FLASHJESTER_JUGGLOR_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-flashjester-jugglor");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_flashjester_jugglor_check_is_valid;
    archive->format.handle_base_info =
        xx_sfx_flashjester_jugglor_handle_base_info;
    archive->format.get_format_size =
        xx_sfx_flashjester_jugglor_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_flashjester_jugglor_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_flashjester_jugglor_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_flashjester_jugglor_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_flashjester_jugglor_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_flashjester_jugglor_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_flashjester_jugglor_free_archive_records_reading;
    archive->payload_offset = -1;
    archive->payload_end = -1;
}

xx_sfx_flashjester_jugglor *xx_sfx_flashjester_jugglor_create(
    xx_io_device *device, int64_t base_address) {
    xx_sfx_flashjester_jugglor *archive =
        (xx_sfx_flashjester_jugglor *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_flashjester_jugglor_init(archive, device, base_address);
    return archive;
}

void xx_sfx_flashjester_jugglor_destroy(xx_sfx_flashjester_jugglor *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_flashjester_jugglor_free(xx_sfx_flashjester_jugglor *archive) {
    if (!archive) return;
    xx_sfx_flashjester_jugglor_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_flashjester_jugglor_check_is_valid(Abstractformat *format,
                                               xx_pd_struct *pd) {
    jg_layout layout;
    return jg_walk(format, &layout, NULL, NULL, pd);
}

bool xx_sfx_flashjester_jugglor_handle_base_info(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    xx_sfx_flashjester_jugglor *archive;
    jg_layout layout;
    if (!format || !jg_walk(format, &layout, NULL, NULL, pd)) return false;
    archive = (xx_sfx_flashjester_jugglor *)format;
    archive->variant = layout.variant;
    archive->number_of_records = layout.count;
    archive->unpacked_size = layout.unpacked;
    archive->payload_offset = layout.overlay;
    archive->payload_end = layout.end;
    if (layout.version[0]) xx_format_set_version(format, layout.version);
    format->number_of_archive_records = layout.count;
    format->format_size = layout.end;
    format->overlay_offset =
        layout.end < layout.size ? format->base_address + layout.end : -1;
    format->overlay_size = layout.size - layout.end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_flashjester_jugglor_get_format_size(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_flashjester_jugglor_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sfx_flashjester_jugglor_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_flashjester_jugglor_handle_base_info(format, pd))
               ? ((xx_sfx_flashjester_jugglor *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_sfx_flashjester_jugglor_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    jg_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = jg_stream_load(format, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        jg_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = jg_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!jg_copy_options(&state->options, options) ||
        !jg_set_record(&state->current_record, format, &stream->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sfx_flashjester_jugglor_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sfx_flashjester_jugglor_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    jg_stream *stream;
    (void)pd;
    if (!jg_current(format, state)) {
        if (state) state->has_record = false;
        return false;
    }
    stream = (jg_stream *)state->internal_state;
    if (++stream->index >= stream->count ||
        !jg_set_record(&state->current_record, format,
                       &stream->members[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_sfx_flashjester_jugglor_unpack_current_to_device(
    Abstractformat *format, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd) {
    const jg_member *member = jg_current(format, state);
    if (!member || (pd && xx_pd_is_stopped(pd))) return false;
    return jg_inflate(format, member, destination, pd);
}

bool xx_sfx_flashjester_jugglor_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    const jg_member *member = jg_current(format, state);
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!member || (pd && xx_pd_is_stopped(pd))) return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && member->size > xx_var_get_u64(option)) return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return jg_inflate(format, member, NULL, pd);
    if (member->unsafe) return false;
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = jg_inflate(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_flashjester_jugglor_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
