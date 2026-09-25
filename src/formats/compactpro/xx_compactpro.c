/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Compact Pro (.cpt) archives. All integers are BIG-endian.
 *
 *   fixed header, 8 bytes at offset 0:
 *     0x00  u8       1, the format's only version byte
 *     0x01  u8       volume number (1 for a single-volume archive)
 *     0x02  u16      volume set identifier
 *     0x04  u32 BE   catalogue offset
 *
 *   catalogue, at that offset (it is the last thing the writer emits):
 *     +0x00  u32 BE  CRC-32 register (reflected 0xEDB88320, preset all ones,
 *                    NOT complemented) of everything from +0x04 to the
 *                    catalogue's end
 *     +0x04  u16 BE  number of records in the whole tree
 *     +0x06  u8      archive comment length, then that many comment bytes
 *     then the records, in pre-order.
 *
 *   record: a length byte whose low seven bits are the name length and whose
 *   top bit distinguishes a directory from a file, then the name (Mac OS
 *   Roman).
 *
 *     directory: u16 BE child count, then that many records nested inside
 *       (their own nested records included). The directory and all of its
 *       descendants are drawn from the parent's record total.
 *     file: 45 further bytes -
 *       +0x00  u8      volume
 *       +0x01  u32 BE  offset of the member's data
 *       +0x05  4 bytes Mac file type
 *       +0x09  4 bytes Mac creator
 *       +0x0d  u32 BE  creation date, seconds since 1904
 *       +0x11  u32 BE  modification date, seconds since 1904
 *       +0x15  u16 BE  Finder flags
 *       +0x17  u32 BE  CRC-32 register (as above) over the resource fork's
 *                      plaintext followed by the data fork's plaintext
 *       +0x1b  u16 BE  flags: bit 0 encrypted, bit 1 resource fork is LZH,
 *                      bit 2 data fork is LZH
 *       +0x1d  u32 BE  resource fork plaintext size
 *       +0x21  u32 BE  data fork plaintext size
 *       +0x25  u32 BE  resource fork packed size
 *       +0x29  u32 BE  data fork packed size
 *
 *   The resource fork's bytes sit at the record's offset and the data fork's
 *   immediately after them; all member data lies between the header and the
 *   catalogue. A fork is listed only when it has content, except that a file
 *   with no resource fork always lists its data fork even when that fork is
 *   empty - so every file yields at least one member. Resource forks are
 *   listed under the file's path with ".rsrc" appended. Directories that hold
 *   nothing are listed as folder records so they survive extraction; every
 *   other directory is implied by its members' paths.
 *
 *   Names are converted from Mac OS Roman to UTF-8. Characters a file system
 *   would read as structure or refuse ('/', '\\', ':', '*', '?', '"', '<',
 *   '>', '|', control codes) become '_', trailing dots and spaces are
 *   dropped, Windows device names get a '_' prefix, and two members that
 *   would land on the same output path (compared without case) are kept
 *   apart with a numeric suffix.
 *
 * Because the catalogue sits at an offset the header points at, and the
 * header itself is a single byte 1, the format's real recognition is the
 * catalogue CRC: the whole record walk must land exactly on the byte range
 * whose CRC matches the stored word. Extraction is proven by the per-file
 * CRC, which covers both forks, so a fork is only reported as extracted once
 * the file's whole CRC has been checked.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/compactpro/xx_compactpro.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/compactpro/xx_compactpro.h"

#include <stdio.h>

#define XX_COMPACTPRO_HEADER_SIZE 8
#define XX_COMPACTPRO_VERSION 1U
#define XX_COMPACTPRO_CATALOG_HEAD 7
#define XX_COMPACTPRO_MIN_SIZE (XX_COMPACTPRO_HEADER_SIZE + XX_COMPACTPRO_CATALOG_HEAD)
#define XX_COMPACTPRO_FILE_RECORD_SIZE 45
#define XX_COMPACTPRO_DIR_RECORD_SIZE 2
#define XX_COMPACTPRO_NAME_MASK 0x7fU
/* The largest record: a length byte, a 127-byte name and a file record. */
#define XX_COMPACTPRO_MAX_RECORD (1 + 127 + XX_COMPACTPRO_FILE_RECORD_SIZE)
#define XX_COMPACTPRO_MAX_DEPTH 128
#define XX_COMPACTPRO_MAX_PATH 0x1000
/* Every output name the listing builds, added up. A real catalogue needs a
 * few kilobytes; this only stops a hostile tree of long, deep paths. */
#define XX_COMPACTPRO_MAX_NAME_BYTES ((size_t)0x2000000)
#define XX_COMPACTPRO_READ_STEP ((size_t)0x1000)
#define XX_COMPACTPRO_FLAG_ENCRYPTED 0x0001U
#define XX_COMPACTPRO_FLAG_RSRC_LZH 0x0002U
#define XX_COMPACTPRO_FLAG_DATA_LZH 0x0004U
#define XX_COMPACTPRO_METHOD_STORED 0U
#define XX_COMPACTPRO_METHOD_RLE 1U
#define XX_COMPACTPRO_METHOD_LZH 2U
/* Plaintext and packed sizes come from the catalogue and are
 * attacker-controlled. */
#define XX_COMPACTPRO_MAX_DECODED ((int64_t)0x10000000)
#define XX_COMPACTPRO_NONE ((size_t)-1)

/* One catalogue record as the structural walk found it. File fields are
 * re-read from the catalogue when the listing is built. */
typedef struct xx_compactpro_entry_s {
    uint32_t record;      /* catalogue offset of the length byte */
    uint16_t children;    /* directories only */
    uint8_t name_length;
    uint8_t depth;        /* 0 for records directly in the root */
    bool folder;
} xx_compactpro_entry;

typedef struct xx_compactpro_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint32_t modified;
    uint32_t finder_flags;
    uint32_t mac_type;
    uint32_t file_crc;    /* stored register over both forks */
    uint32_t prefix;      /* data forks: running CRC-32 after the resource fork */
    size_t pair;          /* the file's other listed fork, or NONE */
    bool prefix_known;
    bool resource;
    bool encrypted;
    bool empty_file;      /* both forks empty: nothing to check */
    bool is_folder;
} xx_compactpro_member;

typedef struct xx_compactpro_stream_s {
    xx_compactpro_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_compactpro_stream;

typedef struct xx_compactpro_scan_s {
    Abstractformat *self;
    xx_pd_struct *pd;
    int64_t span;          /* bytes from base_address to the end */
    int64_t origin;        /* catalogue offset, relative to base_address */
    uint8_t *catalog;      /* the part of the catalogue read so far */
    size_t loaded;
    size_t limit;          /* the most the catalogue can occupy */
    size_t end;            /* catalogue length, once the walk is done */
    xx_compactpro_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
    size_t member_count;
} xx_compactpro_scan;

static void xx_compactpro_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_compactpro_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t xx_compactpro_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static bool xx_compactpro_read_at(Abstractformat *self, int64_t offset,
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

/* Last line of defence at extraction time: nothing absolute, no drive
 * letter, no empty or dot component, no backslash or control byte. The
 * listing never builds such a name; this only proves it. */
static bool xx_compactpro_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') {
            if ((uint8_t)*end < 0x20U || *end == '\\' || *end == ':') {
                return false;
            }
            ++end;
        }
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 1U && cursor[0] == '.') return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_compactpro_stream_free(void *pointer) {
    xx_compactpro_stream *stream = (xx_compactpro_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static void xx_compactpro_scan_cleanup(xx_compactpro_scan *scan) {
    if (scan->catalog) xx_mem_free(scan->catalog);
    if (scan->entries) xx_mem_free(scan->entries);
    scan->catalog = NULL;
    scan->entries = NULL;
}

/* --------------------------------------------------------------- names -- */

/* Mac OS Roman 0x80..0xFF as Unicode (Apple's ROMAN.TXT, with the euro sign
 * Mac OS 8.5 put at 0xDB). */
static const uint16_t xx_compactpro_mac_roman[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

/* Room for one converted name component: a device-name prefix, 127
 * characters of at most three UTF-8 bytes, a "_<n>" suffix and a NUL. */
#define XX_COMPACTPRO_COMPONENT_BUFFER (1U + 127U * 3U + 16U + 1U)

static size_t xx_compactpro_put_utf8(char *out, uint32_t code) {
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
    }
    if (code < 0x800U) {
        out[0] = (char)(0xC0U | (code >> 6));
        out[1] = (char)(0x80U | (code & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code >> 12));
    out[1] = (char)(0x80U | ((code >> 6) & 0x3FU));
    out[2] = (char)(0x80U | (code & 0x3FU));
    return 3U;
}

static char xx_compactpro_upper_ascii(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$ and CLOCK$, with or
 * without an extension, in any case. */
static bool xx_compactpro_is_device(const char *name, size_t length) {
    static const char *const words[] = {"CON", "PRN", "AUX", "NUL",
                                        "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U;
    size_t word;
    size_t index;

    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (word = 0U; word < sizeof(words) / sizeof(words[0]); ++word) {
        const char *text = words[word];
        for (index = 0U; index < stem && text[index]; ++index) {
            if (xx_compactpro_upper_ascii(name[index]) != text[index]) break;
        }
        if (index == stem && text[index] == '\0') return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        char a = xx_compactpro_upper_ascii(name[0]);
        char b = xx_compactpro_upper_ascii(name[1]);
        char c = xx_compactpro_upper_ascii(name[2]);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T')) {
            return true;
        }
    }
    return false;
}

/* One stored Mac OS Roman name made into one safe UTF-8 path component.
 * Returns its length; @p out must hold XX_COMPACTPRO_COMPONENT_BUFFER. */
static size_t xx_compactpro_component(const uint8_t *bytes, size_t size,
                                      char *out) {
    size_t input;
    size_t output = 0U;

    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            out[output++] = '_';
        } else if (c < 0x80U) {
            out[output++] = (char)c;
        } else {
            output += xx_compactpro_put_utf8(
                out + output, xx_compactpro_mac_roman[c - 0x80U]);
        }
    }
    /* Windows drops these, which would let "." and ".." through. */
    while (output != 0U &&
           (out[output - 1U] == ' ' || out[output - 1U] == '.')) {
        --output;
    }
    if (output == 0U) out[output++] = '_';
    if (xx_compactpro_is_device(out, output)) {
        xx_rt_memmove(out + 1, out, output);
        out[0] = '_';
        ++output;
    }
    out[output] = '\0';
    return output;
}

/* Output paths already handed out, compared the way a case-insensitive file
 * system would (ASCII plus the Latin letters Mac OS Roman carries in both
 * cases). Each slot also remembers the next numeric suffix to try for that
 * path, so a run of duplicates stays linear. */
typedef struct xx_compactpro_names_s {
    const char **slots;
    uint32_t *hints;
    size_t mask;
} xx_compactpro_names;

static uint32_t xx_compactpro_fold_next(const char **cursor) {
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t code = s[0];
    size_t used = 1U;

    if (code == 0U) return 0U;
    if ((code & 0xE0U) == 0xC0U && s[1] != 0U) {
        code = ((code & 0x1FU) << 6) | (s[1] & 0x3FU);
        used = 2U;
    } else if ((code & 0xF0U) == 0xE0U && s[1] != 0U && s[2] != 0U) {
        code = ((code & 0x0FU) << 12) | ((uint32_t)(s[1] & 0x3FU) << 6) |
               (s[2] & 0x3FU);
        used = 3U;
    }
    *cursor += used;
    /* Folding more than the file system does only costs a suffix; folding
     * less could let two members share one file. */
    if (code >= 'a' && code <= 'z') return code - 0x20U;
    if (code >= 0xE0U && code <= 0xFEU && code != 0xF7U) return code - 0x20U;
    if (code == 0xFFU) return 0x178U;
    if (code == 0x153U) return 0x152U;
    if (code == 0x131U) return 'I';
    if (code == 0x3C0U) return 0x3A0U;
    return code;
}

static uint32_t xx_compactpro_hash(const char *name) {
    uint32_t hash = 2166136261U;
    uint32_t code;

    while ((code = xx_compactpro_fold_next(&name)) != 0U) {
        hash ^= code;
        hash *= 16777619U;
    }
    return hash;
}

static bool xx_compactpro_same(const char *left, const char *right) {
    for (;;) {
        uint32_t a = xx_compactpro_fold_next(&left);
        uint32_t b = xx_compactpro_fold_next(&right);
        if (a != b) return false;
        if (a == 0U) return true;
    }
}

static bool xx_compactpro_names_init(xx_compactpro_names *names,
                                     size_t expected) {
    size_t size = 16U;

    xx_mem_zero(names, sizeof(*names));
    while (size < expected * 2U + 2U) size *= 2U;
    names->slots = (const char **)xx_mem_calloc(size, sizeof(*names->slots));
    names->hints = (uint32_t *)xx_mem_calloc(size, sizeof(*names->hints));
    names->mask = size - 1U;
    return names->slots && names->hints;
}

static void xx_compactpro_names_cleanup(xx_compactpro_names *names) {
    if (names->slots) xx_mem_free((void *)names->slots);
    if (names->hints) xx_mem_free(names->hints);
    xx_mem_zero(names, sizeof(*names));
}

/* The slot holding @p name, or the empty slot where it belongs. The table is
 * sized to stay at most half full, so the probe always ends. */
static size_t xx_compactpro_names_find(const xx_compactpro_names *names,
                                       const char *name) {
    size_t slot = (size_t)xx_compactpro_hash(name) & names->mask;

    while (names->slots[slot] &&
           !xx_compactpro_same(names->slots[slot], name)) {
        slot = (slot + 1U) & names->mask;
    }
    return slot;
}

/* ------------------------------------------------------------- the walk -- */

/* Make sure catalogue bytes [0, need) are in memory, reading more only as
 * the walk actually reaches them, so a file that merely starts with 1 costs
 * one small read before it is refused. */
static bool xx_compactpro_ensure(xx_compactpro_scan *scan, size_t need) {
    size_t grown;
    uint8_t *buffer;

    if (need <= scan->loaded) return true;
    if (need > scan->limit) return false;
    grown = scan->loaded * 2U;
    if (grown < need) grown = need;
    if (grown < XX_COMPACTPRO_READ_STEP) grown = XX_COMPACTPRO_READ_STEP;
    if (grown > scan->limit) grown = scan->limit;
    buffer = (uint8_t *)xx_mem_realloc(scan->catalog, grown);
    if (!buffer) return false;
    scan->catalog = buffer;
    if (!xx_compactpro_read_at(
            scan->self,
            scan->self->base_address + scan->origin + (int64_t)scan->loaded,
            buffer + scan->loaded, grown - scan->loaded)) {
        return false;
    }
    scan->loaded = grown;
    return true;
}

static bool xx_compactpro_push_entry(xx_compactpro_scan *scan,
                                     const xx_compactpro_entry *entry) {
    if (scan->entry_count == scan->entry_capacity) {
        size_t capacity = scan->entry_capacity ? scan->entry_capacity * 2U
                                               : 64U;
        xx_compactpro_entry *grown = (xx_compactpro_entry *)xx_mem_realloc(
            scan->entries, capacity * sizeof(*grown));
        if (!grown) return false;
        scan->entries = grown;
        scan->entry_capacity = capacity;
    }
    scan->entries[scan->entry_count++] = *entry;
    return true;
}

/* The structural pass: header, record walk and catalogue CRC. Everything a
 * listing needs is left in @p scan; nothing is named yet. */
static bool xx_compactpro_scan_run(Abstractformat *self, xx_pd_struct *pd,
                                   xx_compactpro_scan *scan) {
    uint8_t header[XX_COMPACTPRO_HEADER_SIZE];
    uint32_t remaining[XX_COMPACTPRO_MAX_DEPTH + 1];
    int64_t total;
    int64_t bound;
    uint32_t root_records;
    size_t comment_size;
    size_t position;
    int32_t depth = 0;
    uint32_t stored_crc;

    xx_mem_zero(scan, sizeof(*scan));
    scan->self = self;
    scan->pd = pd;
    if (!self || !self->device || self->base_address < 0) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    scan->span = total - self->base_address;
    if (scan->span < (int64_t)XX_COMPACTPRO_MIN_SIZE) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (!xx_compactpro_read_at(self, self->base_address, header,
                               sizeof(header))) {
        return false;
    }
    /* The whole fixed header is one byte with the value 1. That is far too
     * weak to recognise a format on, which is why nothing is decided here:
     * the catalogue CRC at the bottom of this function is the real test. */
    if (header[0] != (uint8_t)XX_COMPACTPRO_VERSION) return false;

    /* The catalogue follows the 8-byte header and all member data. */
    scan->origin = (int64_t)xx_compactpro_be32(header + 4);
    if (scan->origin < (int64_t)XX_COMPACTPRO_HEADER_SIZE ||
        scan->origin > scan->span - (int64_t)XX_COMPACTPRO_CATALOG_HEAD) {
        return false;
    }
    scan->limit = (size_t)XX_COMPACTPRO_CATALOG_HEAD;
    if (!xx_compactpro_ensure(scan, (size_t)XX_COMPACTPRO_CATALOG_HEAD)) {
        return false;
    }
    stored_crc = xx_compactpro_be32(scan->catalog);
    root_records = (uint32_t)xx_compactpro_be16(scan->catalog + 4);
    comment_size = (size_t)scan->catalog[6];
    if (root_records == 0U) return false;

    /* No catalogue can be longer than its record total allows, so that is
     * the most that will ever be read, whatever follows it in the file. */
    bound = (int64_t)XX_COMPACTPRO_CATALOG_HEAD + (int64_t)comment_size +
            (int64_t)root_records * (int64_t)XX_COMPACTPRO_MAX_RECORD;
    if (bound > scan->span - scan->origin) bound = scan->span - scan->origin;
    scan->limit = (size_t)bound;
    position = (size_t)XX_COMPACTPRO_CATALOG_HEAD + comment_size;
    if (!xx_compactpro_ensure(scan, position)) return false;

    remaining[0] = root_records;
    for (;;) {
        xx_compactpro_entry entry;
        uint32_t name_field;
        size_t name_size;

        while (depth > 0 && remaining[depth] == 0U) --depth;
        if (remaining[depth] == 0U) break;
        if (pd && xx_pd_is_stopped(pd)) return false;

        if (!xx_compactpro_ensure(scan, position + 1U)) return false;
        name_field = (uint32_t)scan->catalog[position];
        name_size = (size_t)(name_field & XX_COMPACTPRO_NAME_MASK);
        /* A record with no name is not a record. */
        if (name_size == 0U) return false;

        xx_mem_zero(&entry, sizeof(entry));
        entry.record = (uint32_t)position;
        entry.name_length = (uint8_t)name_size;
        entry.depth = (uint8_t)depth;
        entry.folder = (name_field & 0x80U) != 0U;
        /* The record itself is one of its parent's records. */
        --remaining[depth];

        if (entry.folder) {
            uint32_t children;
            if (!xx_compactpro_ensure(
                    scan, position + 1U + name_size +
                              (size_t)XX_COMPACTPRO_DIR_RECORD_SIZE)) {
                return false;
            }
            children = (uint32_t)xx_compactpro_be16(scan->catalog + position +
                                                    1U + name_size);
            /* A directory's descendants come out of its parent's budget, so
             * a count that does not fit means the walk has lost sync. */
            if (children > remaining[depth]) return false;
            remaining[depth] -= children;
            entry.children = (uint16_t)children;
            position += 1U + name_size + (size_t)XX_COMPACTPRO_DIR_RECORD_SIZE;
            if (children != 0U) {
                if (depth >= XX_COMPACTPRO_MAX_DEPTH) return false;
                remaining[++depth] = children;
            } else {
                ++scan->member_count;
            }
        } else {
            const uint8_t *meta;
            int64_t offset;
            int64_t resource_raw;
            int64_t data_raw;
            int64_t resource_packed;
            int64_t data_packed;
            if (!xx_compactpro_ensure(
                    scan, position + 1U + name_size +
                              (size_t)XX_COMPACTPRO_FILE_RECORD_SIZE)) {
                return false;
            }
            meta = scan->catalog + position + 1U + name_size;
            offset = (int64_t)xx_compactpro_be32(meta + 1);
            resource_raw = (int64_t)xx_compactpro_be32(meta + 29);
            data_raw = (int64_t)xx_compactpro_be32(meta + 33);
            resource_packed = (int64_t)xx_compactpro_be32(meta + 37);
            data_packed = (int64_t)xx_compactpro_be32(meta + 41);
            /* Both forks, back to back, before the catalogue. Every term is
             * below 2^32, so the sum cannot overflow. */
            if (offset + resource_packed + data_packed > scan->origin) {
                return false;
            }
            scan->member_count += (resource_raw != 0 ? 1U : 0U) +
                                  ((data_raw != 0 || resource_raw == 0) ? 1U
                                                                        : 0U);
            position += 1U + name_size + (size_t)XX_COMPACTPRO_FILE_RECORD_SIZE;
        }
        if (!xx_compactpro_push_entry(scan, &entry)) return false;
    }
    if (scan->member_count == 0U) return false;
    scan->end = position;

    /* THE defence against a false positive. Byte 0 being 1 and a plausible
     * catalogue offset will happen by chance often; a CRC over exactly the
     * bytes the record walk consumed matching the stored word will not. The
     * range is not a stored length - it is wherever the walk stopped - so
     * this check also proves the walk stayed in step with the writer. Never
     * loosen it to "the walk did not fail". */
    if ((xx_crc32_calc(0U, scan->catalog + 4, position - 4U) ^ 0xffffffffU) !=
        stored_crc) {
        return false;
    }
    return !(pd && xx_pd_is_stopped(pd));
}

/* ------------------------------------------------------------ listing -- */

typedef struct xx_compactpro_build_s {
    xx_compactpro_names names;
    char **owned;        /* directory paths, kept for the name table */
    size_t owned_count;
    size_t name_bytes;
} xx_compactpro_build;

static char *xx_compactpro_budget_alloc(xx_compactpro_build *build,
                                        size_t size) {
    if (size > XX_COMPACTPRO_MAX_NAME_BYTES - build->name_bytes) return NULL;
    build->name_bytes += size;
    return (char *)xx_mem_alloc(size);
}

/* "<parent>/<stem>[_<n>]<extension>[.rsrc]", or NULL when it would be too
 * long or memory runs out. */
static char *xx_compactpro_candidate(xx_compactpro_build *build,
                                     const char *parent, const char *component,
                                     size_t component_length, uint32_t suffix,
                                     bool resource) {
    char digits[16];
    size_t digit_count = 0U;
    size_t parent_length = parent ? xx_str_len(parent) : 0U;
    size_t stem = component_length;
    size_t total;
    size_t index;
    char *result;
    char *cursor;

    if (suffix != 0U) {
        int written = xx_rt_snprintf(digits, sizeof(digits), "_%u",
                                     (unsigned)suffix);
        if (written <= 0 || (size_t)written >= sizeof(digits)) return NULL;
        digit_count = (size_t)written;
        /* The number goes before a final extension, never at the start. */
        for (index = component_length; index > 1U; --index) {
            if (component[index - 1U] == '.') {
                stem = index - 1U;
                break;
            }
        }
    }
    total = parent_length + (parent_length ? 1U : 0U) + component_length +
            digit_count + (resource ? 5U : 0U);
    if (total > (size_t)XX_COMPACTPRO_MAX_PATH) return NULL;
    result = xx_compactpro_budget_alloc(build, total + 1U);
    if (!result) return NULL;
    cursor = result;
    if (parent_length) {
        xx_rt_memcpy(cursor, parent, parent_length);
        cursor += parent_length;
        *cursor++ = '/';
    }
    xx_rt_memcpy(cursor, component, stem);
    cursor += stem;
    if (digit_count) {
        xx_rt_memcpy(cursor, digits, digit_count);
        cursor += digit_count;
    }
    xx_rt_memcpy(cursor, component + stem, component_length - stem);
    cursor += component_length - stem;
    if (resource) {
        xx_rt_memcpy(cursor, ".rsrc", 5U);
        cursor += 5;
    }
    *cursor = '\0';
    return result;
}

static void xx_compactpro_release(xx_compactpro_build *build, char *name) {
    if (!name) return;
    build->name_bytes -= xx_str_len(name) + 1U;
    xx_mem_free(name);
}

/* Pick the first free "<stem>[_<n>]" under @p parent. The plain path is
 * always reserved (a directory, a data fork, or just the stem a resource
 * fork hangs off), the ".rsrc" path too when @p want_rsrc. The plain path is
 * returned in @p plain, the resource path in @p rsrc. */
static bool xx_compactpro_claim(xx_compactpro_build *build, const char *parent,
                                const char *component, size_t length,
                                bool want_rsrc, char **plain, char **rsrc) {
    size_t base_slot = XX_COMPACTPRO_NONE;
    uint32_t suffix = 0U;
    uint32_t attempts;

    *plain = NULL;
    *rsrc = NULL;
    for (attempts = 0U; attempts < 0x40000U; ++attempts) {
        char *candidate = xx_compactpro_candidate(build, parent, component,
                                                  length, suffix, false);
        char *resource = NULL;
        size_t slot;
        size_t rsrc_slot = XX_COMPACTPRO_NONE;
        bool free_name;

        if (!candidate) return false;
        slot = xx_compactpro_names_find(&build->names, candidate);
        free_name = build->names.slots[slot] == NULL;
        if (free_name && want_rsrc) {
            resource = xx_compactpro_candidate(build, parent, component,
                                               length, suffix, true);
            if (!resource) {
                xx_compactpro_release(build, candidate);
                return false;
            }
            rsrc_slot = xx_compactpro_names_find(&build->names, resource);
            free_name = build->names.slots[rsrc_slot] == NULL;
        }
        if (free_name) {
            build->names.slots[slot] = candidate;
            build->names.hints[slot] = 1U;
            if (resource) {
                /* Re-found: the plain name may have taken its slot. */
                rsrc_slot = xx_compactpro_names_find(&build->names, resource);
                build->names.slots[rsrc_slot] = resource;
                build->names.hints[rsrc_slot] = 1U;
            }
            if (base_slot != XX_COMPACTPRO_NONE) {
                build->names.hints[base_slot] = suffix + 1U;
            }
            *plain = candidate;
            *rsrc = resource;
            return true;
        }
        if (suffix == 0U) {
            /* Continue from where the last clash on this name stopped. */
            base_slot = xx_compactpro_names_find(&build->names, candidate);
            suffix = build->names.slots[base_slot]
                         ? build->names.hints[base_slot]
                         : 1U;
            if (suffix == 0U) suffix = 1U;
            if (!build->names.slots[base_slot]) base_slot = XX_COMPACTPRO_NONE;
        } else {
            ++suffix;
        }
        xx_compactpro_release(build, resource);
        xx_compactpro_release(build, candidate);
    }
    return false;
}

static void xx_compactpro_set_member_common(xx_compactpro_member *member,
                                            const xx_compactpro_scan *scan,
                                            const xx_compactpro_entry *entry,
                                            int64_t record_size) {
    member->header_offset =
        scan->self->base_address + scan->origin + (int64_t)entry->record;
    member->header_size = record_size;
    member->pair = XX_COMPACTPRO_NONE;
}

/* Turn the walked records into the member list: two forks per file at most,
 * one record per empty directory, every name made safe and unique. */
static xx_compactpro_stream *xx_compactpro_build_stream(
    xx_compactpro_scan *scan) {
    xx_compactpro_build build;
    xx_compactpro_stream *stream = NULL;
    const char *parents[XX_COMPACTPRO_MAX_DEPTH + 1];
    char component[XX_COMPACTPRO_COMPONENT_BUFFER];
    size_t index;
    bool ok = false;

    xx_mem_zero(&build, sizeof(build));
    xx_mem_zero((void *)parents, sizeof(parents));
    stream = (xx_compactpro_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    if (scan->entry_count == 0U || scan->member_count == 0U) goto done;
    stream->items = (xx_compactpro_member *)xx_mem_calloc(
        scan->member_count, sizeof(*stream->items));
    if (!stream->items) goto done;
    /* Each record leaves at most one name that no member owns: a directory
     * path, or the stem of a file listed only by its resource fork. */
    build.owned = (char **)xx_mem_calloc(scan->entry_count, sizeof(char *));
    if (!build.owned) goto done;
    /* At most two reserved names per record. */
    if (!xx_compactpro_names_init(&build.names, scan->entry_count * 2U + 2U)) {
        goto done;
    }

    for (index = 0U; index < scan->entry_count; ++index) {
        const xx_compactpro_entry *entry = &scan->entries[index];
        const char *parent = entry->depth ? parents[entry->depth - 1U] : NULL;
        size_t length;
        char *plain = NULL;
        char *resource = NULL;

        if (entry->depth && !parent) goto done;
        length = xx_compactpro_component(
            scan->catalog + entry->record + 1U, entry->name_length, component);

        if (entry->folder) {
            if (!xx_compactpro_claim(&build, parent, component, length, false,
                                     &plain, &resource)) {
                goto done;
            }
            build.owned[build.owned_count++] = plain;
            parents[entry->depth] = plain;
            if (entry->children == 0U) {
                xx_compactpro_member *member;
                char *copy;
                if (stream->count >= scan->member_count) goto done;
                copy = xx_compactpro_budget_alloc(&build,
                                                  xx_str_len(plain) + 1U);
                if (!copy) goto done;
                xx_rt_memcpy(copy, plain, xx_str_len(plain) + 1U);
                member = &stream->items[stream->count++];
                xx_compactpro_set_member_common(
                    member, scan, entry,
                    1 + (int64_t)entry->name_length +
                        XX_COMPACTPRO_DIR_RECORD_SIZE);
                member->name = copy;
                member->data_offset = member->header_offset;
                member->method = XX_COMPACTPRO_METHOD_STORED;
                member->is_folder = true;
            }
        } else {
            const uint8_t *meta =
                scan->catalog + entry->record + 1U + entry->name_length;
            int64_t offset = (int64_t)xx_compactpro_be32(meta + 1);
            uint32_t flags = (uint32_t)xx_compactpro_be16(meta + 27);
            int64_t resource_raw = (int64_t)xx_compactpro_be32(meta + 29);
            int64_t data_raw = (int64_t)xx_compactpro_be32(meta + 33);
            int64_t resource_packed = (int64_t)xx_compactpro_be32(meta + 37);
            int64_t data_packed = (int64_t)xx_compactpro_be32(meta + 41);
            bool want_rsrc = resource_raw != 0;
            bool want_data = data_raw != 0 || resource_raw == 0;
            int64_t record_size =
                1 + (int64_t)entry->name_length + XX_COMPACTPRO_FILE_RECORD_SIZE;
            size_t first = stream->count;
            size_t forks = (want_rsrc ? 1U : 0U) + (want_data ? 1U : 0U);
            size_t fork;

            if (stream->count + forks > scan->member_count) goto done;
            if (!xx_compactpro_claim(&build, parent, component, length,
                                     want_rsrc, &plain, &resource)) {
                goto done;
            }
            for (fork = 0U; fork < forks; ++fork) {
                xx_compactpro_member *member = &stream->items[stream->count++];
                bool is_rsrc = want_rsrc && fork == 0U;
                int64_t raw = is_rsrc ? resource_raw : data_raw;
                bool lzh = (flags & (is_rsrc ? XX_COMPACTPRO_FLAG_RSRC_LZH
                                             : XX_COMPACTPRO_FLAG_DATA_LZH)) !=
                           0U;
                xx_compactpro_set_member_common(member, scan, entry,
                                                record_size);
                /* The name table keeps pointing at these strings; the
                 * members own them from here on. */
                member->name = is_rsrc ? resource : plain;
                member->resource = is_rsrc;
                member->data_offset =
                    scan->self->base_address + offset +
                    (is_rsrc ? 0 : resource_packed);
                member->compressed_size = is_rsrc ? resource_packed
                                                  : data_packed;
                member->uncompressed_size = raw;
                /* An empty fork carries no codec at all. */
                member->method = raw == 0 ? XX_COMPACTPRO_METHOD_STORED
                                          : (lzh ? XX_COMPACTPRO_METHOD_LZH
                                                 : XX_COMPACTPRO_METHOD_RLE);
                member->mac_type = xx_compactpro_be32(meta + 5);
                member->modified = xx_compactpro_be32(meta + 17);
                member->finder_flags = (uint32_t)xx_compactpro_be16(meta + 21);
                member->file_crc = xx_compactpro_be32(meta + 23);
                member->encrypted =
                    (flags & XX_COMPACTPRO_FLAG_ENCRYPTED) != 0U;
                member->empty_file = resource_raw == 0 && data_raw == 0;
            }
            /* A resource fork with no listed data fork: the stem was only
             * reserved and names no member, but the name table still points
             * at it, so it is kept with the directory paths. */
            if (!want_data) build.owned[build.owned_count++] = plain;
            if (forks == 2U) {
                stream->items[first].pair = first + 1U;
                stream->items[first + 1U].pair = first;
            }
        }
    }
    if (stream->count != scan->member_count) goto done;
    stream->archive_size = scan->origin + (int64_t)scan->end;
    ok = true;

done:
    xx_compactpro_names_cleanup(&build.names);
    for (index = 0U; index < build.owned_count; ++index) {
        xx_mem_free(build.owned[index]);
    }
    if (build.owned) xx_mem_free(build.owned);
    if (!ok) {
        xx_compactpro_stream_free(stream);
        return NULL;
    }
    return stream;
}

/* ------------------------------------------------------------- decode -- */

static bool xx_compactpro_decode(Abstractformat *self,
                                 const xx_compactpro_member *member,
                                 uint8_t **out, size_t *out_size,
                                 xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool ok;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->is_folder) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* Only these three exist. Anything else must fail rather than fall
     * through to a stored copy, which would emit compressed bytes as if they
     * were plaintext. */
    if (member->method != XX_COMPACTPRO_METHOD_STORED &&
        member->method != XX_COMPACTPRO_METHOD_RLE &&
        member->method != XX_COMPACTPRO_METHOD_LZH) {
        return false;
    }
    if (member->uncompressed_size < 0 || member->compressed_size < 0 ||
        member->compressed_size > XX_COMPACTPRO_MAX_DECODED ||
        member->uncompressed_size > XX_COMPACTPRO_MAX_DECODED) {
        return false;
    }

    if (member->method == XX_COMPACTPRO_METHOD_STORED) {
        /* An empty fork is a real member - a file may have a zero-length
         * data fork - so this is a success with no bytes. One byte is
         * allocated because a zero-sized allocation has no defined result. */
        if (member->uncompressed_size != 0) return false;
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        return true;
    }
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_compactpro_read_at(self, member->data_offset, input,
                               (size_t)member->compressed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (member->method == XX_COMPACTPRO_METHOD_LZH) {
        ok = xx_compactpro_lzh_decode_memory(
            input, (size_t)member->compressed_size, output,
            (size_t)member->uncompressed_size, &written);
    } else {
        ok = xx_compactpro_rle_decode_memory(
            input, (size_t)member->compressed_size, output,
            (size_t)member->uncompressed_size, &written);
    }
    xx_mem_free(input);
    /* Both entry points succeed only on an exact-length decode; the length
     * is re-tested so a relaxed decoder cannot turn a partial fork into a
     * silent success. */
    if (!ok || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = written;
    return true;
}

/* Decode a fork only to run it through the file CRC. */
static bool xx_compactpro_fork_crc(Abstractformat *self,
                                   const xx_compactpro_member *member,
                                   uint32_t seed, uint32_t *crc,
                                   xx_pd_struct *pd) {
    uint8_t *plain = NULL;
    size_t plain_size = 0U;

    if (!xx_compactpro_decode(self, member, &plain, &plain_size, pd)) {
        return false;
    }
    *crc = xx_crc32_calc(seed, plain, plain_size);
    xx_mem_free(plain);
    return true;
}

static bool xx_compactpro_write_file(const char *path, const uint8_t *data,
                                     size_t size) {
    xx_io_device *output = xx_io_file_open(path, "wb");
    size_t completed = 0U;
    bool result = true;

    /* Nothing was written to a file that did not open: leave it alone. */
    if (!output) return false;
    while (result && completed < size) {
        ssize_t sent = xx_io_write(output, data + completed, size - completed);
        if (sent <= 0 || (size_t)sent > size - completed) {
            result = false;
            break;
        }
        completed += (size_t)sent;
    }
    if (xx_io_close(output) != 0) result = false;
    if (!result) xx_rt_remove(path);
    return result;
}

/* Decode the current fork, hand it to @p target_path (NULL: verify only),
 * and prove it with the file CRC, which runs over the resource fork and then
 * the data fork. The other fork is decoded for that when its running CRC is
 * not already known, one fork in memory at a time. */
static bool xx_compactpro_extract(Abstractformat *self,
                                  xx_compactpro_stream *stream,
                                  xx_compactpro_member *member,
                                  const char *target_path, xx_pd_struct *pd) {
    xx_compactpro_member *other =
        member->pair != XX_COMPACTPRO_NONE ? &stream->items[member->pair]
                                           : NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    uint32_t crc = 0U;
    bool written = false;
    bool result;

    /* A fork flagged as encrypted is still decoded: there is no password to
     * ask for, but the file CRC below decides. Ciphertext cannot survive the
     * codec and the CRC together, while a writer that set the flag on plain
     * data (unar ignores it too) still extracts. */
    if (!member->resource && other) {
        if (!member->prefix_known) {
            if (!xx_compactpro_fork_crc(self, other, 0U, &member->prefix, pd)) {
                return false;
            }
            member->prefix_known = true;
        }
        crc = member->prefix;
    }
    if (!xx_compactpro_decode(self, member, &plain, &plain_size, pd)) {
        return false;
    }
    crc = xx_crc32_calc(crc, plain, plain_size);
    if (member->resource && other) {
        other->prefix = crc;
        other->prefix_known = true;
    }
    result = true;
    if (target_path) {
        result = xx_compactpro_write_file(target_path, plain, plain_size);
        written = result;
    }
    xx_mem_free(plain);
    if (result && member->resource && other) {
        result = xx_compactpro_fork_crc(self, other, crc, &crc, pd);
    }
    /* A file with nothing in it has nothing to check. */
    if (result && !member->empty_file) {
        result = (crc ^ 0xffffffffU) == member->file_crc;
    }
    if (!result && written) xx_rt_remove(target_path);
    return result;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_compactpro_init(xx_compactpro *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_COMPACTPRO;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-compactpro");
    xx_format_set_extension(&archive->format, "cpt");
    archive->format.check_is_valid = xx_compactpro_check_is_valid;
    archive->format.handle_base_info = xx_compactpro_handle_base_info;
    archive->format.get_format_size = xx_compactpro_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_compactpro_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_compactpro_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_compactpro_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_compactpro_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_compactpro_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_compactpro_free_archive_records_reading;
    archive->format.destroy = xx_compactpro_vtable_destroy;
}

xx_compactpro *xx_compactpro_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_compactpro *archive = (xx_compactpro *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_compactpro_init(archive, device, base_address);
    return archive;
}

void xx_compactpro_destroy(xx_compactpro *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_compactpro_free(xx_compactpro *archive) {
    if (!archive) return;
    xx_compactpro_destroy(archive);
    xx_mem_free(archive);
}

static void xx_compactpro_vtable_destroy(Abstractformat *self) {
    xx_compactpro_destroy((xx_compactpro *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_compactpro_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_compactpro_scan scan;
    bool result;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    result = xx_compactpro_scan_run(self, pd, &scan);
    xx_compactpro_scan_cleanup(&scan);
    return result;
}

bool xx_compactpro_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_compactpro *archive = (xx_compactpro *)self;
    xx_compactpro_scan scan;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    if (!xx_compactpro_scan_run(self, pd, &scan)) {
        xx_compactpro_scan_cleanup(&scan);
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    /* The catalogue is the last thing written, so the archive ends where
     * the walk ended. */
    self->format_size = scan.origin + (int64_t)scan.end;
    self->number_of_archive_records = scan.member_count;
    archive->number_of_records = scan.member_count;
    xx_compactpro_scan_cleanup(&scan);
    return true;
}

int64_t xx_compactpro_get_format_size(Abstractformat *self,
                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_compactpro_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_compactpro *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_compactpro_set_record(xx_archive_record *record,
                                     const xx_compactpro_member *member) {
    bool ok;

    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    ok = xx_archive_record_set_original_name(record, member->name) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->compressed_size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->uncompressed_size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        member->method) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        member->modified) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        member->finder_flags) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                        member->mac_type) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         member->is_folder) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         member->encrypted);
    /* The stored CRC covers both forks together, so it is the CRC of this
     * member's own bytes only when the file has no other listed fork. */
    if (ok && !member->is_folder && member->pair == XX_COMPACTPRO_NONE &&
        !member->empty_file) {
        ok = xx_archive_record_set_meta_u64(
            record, XX_META_ID_CRC32,
            (uint64_t)(member->file_crc ^ 0xffffffffU));
    }
    return ok;
}

static bool xx_compactpro_copy_options(xx_list_s *target,
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

static const xx_var *xx_compactpro_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_compactpro_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_compactpro_scan scan;
    xx_compactpro_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    if (!xx_compactpro_scan_run(self, pd, &scan)) {
        xx_compactpro_scan_cleanup(&scan);
        return NULL;
    }
    stream = xx_compactpro_build_stream(&scan);
    xx_compactpro_scan_cleanup(&scan);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_compactpro_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_compactpro_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_compactpro_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_compactpro_set_record(&state->current_record,
                                   &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_compactpro_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_compactpro_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_compactpro_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_compactpro_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_compactpro_set_record(&state->current_record,
                                                 &stream->items[stream->index]);
    return state->has_record;
}

bool xx_compactpro_unpack_current_archive_record(Abstractformat *self,
                                                 xx_archive_record_state *state,
                                                 xx_pd_struct *pd) {
    xx_compactpro_stream *stream;
    xx_compactpro_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_compactpro_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_compactpro_path_safe(member->name)) return false;

    path_option = xx_compactpro_get_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        return xx_compactpro_extract(self, stream, member, NULL, pd);
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
    if (!xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    result = xx_compactpro_extract(self, stream, member, target_path, pd);
    xx_str_free(target_path);
    return result;
}

void xx_compactpro_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
