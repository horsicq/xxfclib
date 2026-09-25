/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM/Lotus "-ZPAK" self-extracting installers: an MZ/NE extraction stub
 * with a private archive appended. The layout below follows the description
 * in XArchive installers/xzpaksfxarchive.{h,cpp} (MIT, same author); the code
 * is written for xxfclib and every number was re-measured on the 16-file,
 * 524-member reference corpus.
 *
 * The archive never starts at offset 0 and nothing in the stub points at it,
 * so it is found from the END of the file. Version 2 is tried first because
 * its trailer is the longer one.
 *
 *   version 2 trailer, the last 38 bytes:
 *     +0x00  i32  member count
 *     +0x04  u32  offset of the "-ZPAK" header, from the start of the file
 *     +0x08  u32  file size + 16 * count (not used)
 *     +0x0C  26   product tag, NUL padded, often empty
 *
 *   version 1 trailer, the last 22 bytes:
 *     +0x00  u16  member count
 *     +0x02  u32  offset of the "-ZPAK" header, from the start of the file
 *     +0x06  u32  file size (not used)
 *     +0x0A  12   product tag, NUL padded, often empty
 *
 *   header, 8 bytes: "-ZPAK" 00, u16 version (1 or 2). The byte after it is
 *   the version gate: version 2 continues with the 0x0A record tag, version
 *   1 directly with the first member's DCL prelude.
 *
 *   version 1: streams back to back from header + 8, then one 114-byte
 *   directory entry per member, in stream order, ending at the trailer:
 *     +0x00  102  name, NUL terminated, zero filled to the end of the field
 *     +0x66  i32  compressed size
 *     +0x6A  u32  reserved, always 0 (checked)
 *     +0x6E  u16  DOS date
 *     +0x70  u16  DOS time
 *   The running sum of the compressed sizes must land exactly on the first
 *   directory byte.
 *
 *   version 2: per member a 16-byte record, the name, then the stream:
 *     +0x00  u8   tag 0x0A
 *     +0x01  u8   unused
 *     +0x02  i32  compressed size
 *     +0x06  4    writer scratch: zero in six corpus archives, a
 *                 per-archive constant (0x11, 0x13, 0x18) in three, so it
 *                 is NOT a reserved field and is not checked
 *     +0x0A  u16  DOS date
 *     +0x0C  u16  DOS time
 *     +0x0E  i16  name size, terminating NUL included
 *   0..6 filler bytes (six in every corpus file) separate the last stream
 *   from the trailer.
 *
 * Every stream carries its two-byte DCL prelude (literal mode 0/1,
 * dictionary bits 4..6). No uncompressed size or checksum is stored
 * anywhere, so a member is measured with xx_dcl_scan_memory when its record
 * is first produced; the stored compressed size must equal the bytes the
 * decoder consumes, which holds for all 524 corpus members.
 *
 * Names are DOS paths that mix both separators ("\INSTALL/DLL\FXSINDEX.DL_")
 * and often start with one (the install root). They are turned into '/'
 * paths with blank padding trimmed from every component. A name that could
 * escape the destination or alias a DOS device is listed but not extracted,
 * and names that collide (ASCII case folded) get a "_<record number>" suffix
 * so no member overwrites another.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ibm_zpak_installer/xx_ibm_zpak_installer.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef IBM_ZPAK_INSTALLER
#define XX_IBM_ZPAK_INSTALLER_FILE_TYPE XX_FILE_TYPE_IBM_ZPAK_INSTALLER
#else
#define XX_IBM_ZPAK_INSTALLER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ZPI_HEADER_SIZE 8
/* The header plus the version gate: the record tag (v2) or the DCL prelude
 * (v1). */
#define ZPI_HEADER_PROBE 10
#define ZPI_V1_TRAILER 22
#define ZPI_V2_TRAILER 38
#define ZPI_V1_NAME 102
#define ZPI_V1_ENTRY 114
#define ZPI_V2_RECORD 16
#define ZPI_V2_TAG 0x0AU
#define ZPI_V2_MAX_GAP 6
/* Two prelude bytes and at least one byte of bit stream. */
#define ZPI_MIN_PACKED 3
/* v1 stores the count as a u16; v2 spends an i32 on it but is held to the
 * same ceiling. */
#define ZPI_MAX_MEMBERS 65535
/* The stub in front of the archive is at least an MZ header. */
#define ZPI_MIN_STUB 0x20
/* A DOS/OS2 path is far shorter; a longer v2 name field is a mis-parse. */
#define ZPI_MAX_NAME 1024
/* The plaintext length comes from the bit stream, so both sides of a
 * decode stay bounded. */
#define ZPI_MAX_PACKED (INT64_C(256) * 1024 * 1024)
#define ZPI_MAX_DECODED ((size_t)256 * 1024U * 1024U)
#define ZPI_DCL_MAX_LITERAL_MODE 1U
#define ZPI_DCL_MIN_DICT_BITS 4U
#define ZPI_DCL_MAX_DICT_BITS 6U
/* No method field exists; nonzero so a listing never reads "stored". */
#define ZPI_METHOD_DCL 1U
#define ZPI_RENAME_PASSES 4U

enum { ZPI_UNMEASURED = 0, ZPI_MEASURED = 1, ZPI_UNMEASURABLE = 2 };

typedef struct zpi_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t timestamp;
    uint32_t record;   /* zero-based position in the archive */
    int measure;
    bool safe;         /* the name may be written below the destination */
} zpi_member;

typedef struct zpi_stream_s {
    zpi_member *items;
    size_t count;
    size_t capacity;
    size_t index;
} zpi_stream;

typedef struct zpi_layout_s {
    int64_t span;            /* bytes from base to EOF */
    int64_t header;          /* "-ZPAK" header, relative to base */
    int64_t trailer;         /* trailer, relative to base */
    uint32_t count;
    uint16_t version;
} zpi_layout;

/* ------------------------------------------------------------- helpers -- */

static uint16_t zpi_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t zpi_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool zpi_read(Abstractformat *self, int64_t relative, uint8_t *buffer,
                     size_t size) {
    size_t done = 0U;
    if (!self || !self->device || relative < 0 ||
        self->base_address > INT64_MAX - relative ||
        xx_io_seek64(self->device, self->base_address + relative, SEEK_SET) !=
            0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(self->device, buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool zpi_is_prelude(const uint8_t *prelude) {
    return prelude[0] <= ZPI_DCL_MAX_LITERAL_MODE &&
           prelude[1] >= ZPI_DCL_MIN_DICT_BITS &&
           prelude[1] <= ZPI_DCL_MAX_DICT_BITS;
}

static bool zpi_prelude_at(Abstractformat *self, int64_t relative) {
    uint8_t prelude[2];
    return zpi_read(self, relative, prelude, sizeof(prelude)) &&
           zpi_is_prelude(prelude);
}

static char zpi_fold(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int zpi_name_compare(const char *a, const char *b) {
    for (;;) {
        char x = zpi_fold(*a++);
        char y = zpi_fold(*b++);
        if (x != y) return (unsigned char)x < (unsigned char)y ? -1 : 1;
        if (x == '\0') return 0;
    }
}

static void zpi_stream_free(void *opaque) {
    zpi_stream *stream = (zpi_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- names -- */

/* The stored name is printable ASCII with at least one character that is
 * neither a separator nor a blank: anything else means the record is not
 * what this parser thinks it is. The probe and the listing apply the same
 * test, so zpi_normalise below never runs out of name. */
static bool zpi_name_printable(const uint8_t *name, size_t length) {
    size_t index;
    bool component = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        if (name[index] < 0x20U || name[index] > 0x7EU) return false;
        if (name[index] != '\\' && name[index] != '/' && name[index] != ' ') {
            component = true;
        }
    }
    return component;
}

/* '\' and '/' both separate; empty components (the leading install-root
 * separator, a doubled one) are dropped and blank padding is trimmed from
 * each component. Returns NULL when nothing is left or allocation fails. */
static char *zpi_normalise(const uint8_t *name, size_t length) {
    char *out = xx_str_create_len(length);
    size_t start = 0U;
    size_t at = 0U;
    size_t index;
    if (!out) return NULL;
    for (index = 0U; index <= length; ++index) {
        size_t begin, end;
        if (index < length && name[index] != '\\' && name[index] != '/') {
            continue;
        }
        begin = start;
        end = index;
        start = index + 1U;
        while (begin < end && name[begin] == ' ') ++begin;
        while (end > begin && name[end - 1U] == ' ') --end;
        if (begin == end) continue;
        if (at != 0U) out[at++] = '/';
        while (begin < end) out[at++] = (char)name[begin++];
    }
    out[at] = '\0';
    if (at == 0U) {
        xx_str_free(out);
        return NULL;
    }
    return out;
}

static bool zpi_stem_is(const char *component, size_t stem,
                        const char *device) {
    size_t index;
    for (index = 0U; index < stem; ++index) {
        if (!device[index] || zpi_fold(component[index]) != device[index]) {
            return false;
        }
    }
    return device[stem] == '\0';
}

/* One '/'-free component of an output path: no reserved punctuation, not
 * only dots and blanks (which Windows resolves to "." or ".."), and not a
 * DOS device name with or without an extension. */
static bool zpi_component_safe(const char *component, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t index;
    size_t stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        char c = component[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' ||
            c == '|' || c == '?' || c == '*') {
            return false;
        }
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && component[stem] != '.') ++stem;
    while (stem > 0U && component[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        if (zpi_stem_is(component, stem, devices[index])) return false;
    }
    if (stem == 4U && component[3] >= '0' && component[3] <= '9' &&
        ((zpi_fold(component[0]) == 'C' && zpi_fold(component[1]) == 'O' &&
          zpi_fold(component[2]) == 'M') ||
         (zpi_fold(component[0]) == 'L' && zpi_fold(component[1]) == 'P' &&
          zpi_fold(component[2]) == 'T'))) {
        return false;
    }
    return true;
}

static bool zpi_path_safe(const char *path) {
    const char *cursor = path;
    if (!path || !path[0] || path[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        while (*end && *end != '/') ++end;
        if (!zpi_component_safe(cursor, (size_t)(end - cursor))) return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static size_t zpi_put_decimal(char *out, uint64_t value) {
    char digits[24];
    size_t count = 0U;
    size_t index;
    do {
        digits[count++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (index = 0U; index < count; ++index) {
        out[index] = digits[count - 1U - index];
    }
    return count;
}

/* "<stem>_<number>[_<pass>]<extension>", in the last path component. */
static char *zpi_renamed(const char *name, uint64_t number, unsigned pass) {
    size_t length = xx_str_len(name);
    size_t component = 0U;
    size_t insert = length;
    size_t index;
    size_t at;
    char suffix[56];
    size_t suffix_length = 0U;
    char *out;
    for (index = 0U; index < length; ++index) {
        if (name[index] == '/') component = index + 1U;
    }
    for (index = length; index > component + 1U; --index) {
        if (name[index - 1U] == '.') {
            insert = index - 1U;
            break;
        }
    }
    suffix[suffix_length++] = '_';
    suffix_length += zpi_put_decimal(suffix + suffix_length, number);
    if (pass > 1U) {
        suffix[suffix_length++] = '_';
        suffix_length += zpi_put_decimal(suffix + suffix_length, pass);
    }
    out = xx_str_create_len(length + suffix_length);
    if (!out) return NULL;
    xx_rt_memcpy(out, name, insert);
    at = insert;
    xx_rt_memcpy(out + at, suffix, suffix_length);
    at += suffix_length;
    xx_rt_memcpy(out + at, name + insert, length - insert);
    out[length + suffix_length] = '\0';
    return out;
}

static bool zpi_less(const zpi_member *a, const zpi_member *b) {
    int order = zpi_name_compare(a->name, b->name);
    if (order != 0) return order < 0;
    return a->record < b->record;
}

/* Shell sort of an index array by folded name, then record number. */
static void zpi_sort(size_t *order, size_t count, const zpi_member *items) {
    size_t gap = 1U;
    size_t index;
    for (index = 0U; index < count; ++index) order[index] = index;
    while (gap < count / 3U) gap = gap * 3U + 1U;
    for (; gap > 0U; gap /= 3U) {
        for (index = gap; index < count; ++index) {
            size_t value = order[index];
            size_t slot = index;
            while (slot >= gap &&
                   zpi_less(&items[value], &items[order[slot - gap]])) {
                order[slot] = order[slot - gap];
                slot -= gap;
            }
            order[slot] = value;
        }
    }
}

/* The earliest record keeps a shared name, later ones get their record
 * number appended; whatever still collides after the last pass is listed
 * but not extracted. */
static bool zpi_make_unique(zpi_stream *stream) {
    size_t *order;
    unsigned pass;
    size_t index;
    if (stream->count < 2U) return true;
    order = (size_t *)xx_mem_alloc(stream->count * sizeof(*order));
    if (!order) return false;
    for (pass = 1U; pass <= ZPI_RENAME_PASSES + 1U; ++pass) {
        bool changed = false;
        size_t anchor;
        zpi_sort(order, stream->count, stream->items);
        anchor = order[0];
        for (index = 1U; index < stream->count; ++index) {
            zpi_member *later = &stream->items[order[index]];
            char *renamed;
            if (zpi_name_compare(stream->items[anchor].name, later->name) !=
                0) {
                anchor = order[index];
                continue;
            }
            if (pass > ZPI_RENAME_PASSES) {
                later->safe = false;
                continue;
            }
            renamed = zpi_renamed(later->name, (uint64_t)later->record + 1U,
                                  pass);
            if (!renamed) {
                xx_mem_free(order);
                return false;
            }
            xx_str_free(later->name);
            later->name = renamed;
            changed = true;
        }
        if (!changed) break;
    }
    xx_mem_free(order);
    return true;
}

/* --------------------------------------------------------------- parse -- */

/* Append a member, taking ownership of its name. */
static bool zpi_add(zpi_stream *stream, const zpi_member *member) {
    if (stream->count == stream->capacity) {
        size_t grown = stream->capacity ? stream->capacity * 2U : 16U;
        zpi_member *items;
        if (grown > ZPI_MAX_MEMBERS) grown = ZPI_MAX_MEMBERS;
        if (grown <= stream->count) return false;
        items = (zpi_member *)xx_mem_realloc(stream->items,
                                             grown * sizeof(*items));
        if (!items) return false;
        stream->items = items;
        stream->capacity = grown;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Records a validated member in @p stream (when one is given). */
static bool zpi_emit(zpi_stream *stream, const uint8_t *name, size_t length,
                     const zpi_member *fields) {
    zpi_member member;
    if (!stream) return true;
    member = *fields;
    member.name = zpi_normalise(name, length);
    if (!member.name) return false;
    member.safe = zpi_path_safe(member.name);
    member.measure = ZPI_UNMEASURED;
    if (!zpi_add(stream, &member)) {
        xx_str_free(member.name);
        return false;
    }
    return true;
}

static bool zpi_walk_v1(Abstractformat *self, const zpi_layout *layout,
                        zpi_stream *stream, xx_pd_struct *pd) {
    int64_t directory = layout->trailer -
                        (int64_t)layout->count * ZPI_V1_ENTRY;
    int64_t offset = layout->header + ZPI_HEADER_SIZE;
    uint32_t index;
    /* The payload must hold at least one minimal stream before the
     * directory. */
    if (directory < offset + ZPI_MIN_PACKED) return false;
    for (index = 0U; index < layout->count; ++index) {
        uint8_t entry[ZPI_V1_ENTRY];
        zpi_member fields;
        size_t terminator = 0U;
        size_t tail;
        int64_t packed;
        int64_t entry_offset = directory + (int64_t)index * ZPI_V1_ENTRY;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!zpi_read(self, entry_offset, entry, sizeof(entry))) return false;
        while (terminator < ZPI_V1_NAME && entry[terminator] != 0U) {
            ++terminator;
        }
        if (terminator == 0U || terminator >= ZPI_V1_NAME) return false;
        /* A fixed, zero-filled field: one stale byte behind the terminator
         * means this is not a directory entry. */
        for (tail = terminator; tail < ZPI_V1_NAME; ++tail) {
            if (entry[tail] != 0U) return false;
        }
        if (!zpi_name_printable(entry, terminator)) return false;
        packed = (int64_t)(int32_t)zpi_le32(entry + ZPI_V1_NAME);
        if (zpi_le32(entry + ZPI_V1_NAME + 4) != 0U) return false;
        if (packed < ZPI_MIN_PACKED || packed > directory - offset) {
            return false;
        }
        if (!zpi_prelude_at(self, offset)) return false;
        xx_mem_zero(&fields, sizeof(fields));
        fields.header_offset = self->base_address + entry_offset;
        fields.header_size = ZPI_V1_ENTRY;
        fields.data_offset = self->base_address + offset;
        fields.compressed_size = packed;
        fields.timestamp =
            ((uint32_t)zpi_le16(entry + ZPI_V1_NAME + 8) << 16) |
            (uint32_t)zpi_le16(entry + ZPI_V1_NAME + 10);
        fields.record = index;
        if (!zpi_emit(stream, entry, terminator, &fields)) return false;
        offset += packed;
    }
    /* The streams have no terminators of their own: the running sum landing
     * on the directory is the layout's integrity check. */
    return offset == directory;
}

static bool zpi_walk_v2(Abstractformat *self, const zpi_layout *layout,
                        zpi_stream *stream, xx_pd_struct *pd) {
    int64_t offset = layout->header + ZPI_HEADER_SIZE;
    uint32_t index;
    for (index = 0U; index < layout->count; ++index) {
        uint8_t record[ZPI_V2_RECORD];
        uint8_t name[ZPI_MAX_NAME];
        zpi_member fields;
        int64_t packed;
        int64_t name_size;
        int64_t data;
        size_t length = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (offset > layout->trailer - ZPI_V2_RECORD ||
            !zpi_read(self, offset, record, sizeof(record)) ||
            record[0] != ZPI_V2_TAG) {
            return false;
        }
        packed = (int64_t)(int32_t)zpi_le32(record + 2);
        /* record + 6 is writer scratch, deliberately not read. */
        name_size = (int64_t)(int16_t)zpi_le16(record + 14);
        if (packed < ZPI_MIN_PACKED || name_size <= 0 ||
            name_size > ZPI_MAX_NAME ||
            name_size > layout->trailer - offset - ZPI_V2_RECORD) {
            return false;
        }
        data = offset + ZPI_V2_RECORD + name_size;
        if (packed > layout->trailer - data) return false;
        if (!zpi_read(self, offset + ZPI_V2_RECORD, name, (size_t)name_size)) {
            return false;
        }
        /* The size counts the terminator; only the bytes before it name
         * the member. */
        while (length < (size_t)name_size && name[length] != 0U) ++length;
        if (!zpi_name_printable(name, length)) return false;
        if (!zpi_prelude_at(self, data)) return false;
        xx_mem_zero(&fields, sizeof(fields));
        fields.header_offset = self->base_address + offset;
        fields.header_size = ZPI_V2_RECORD + name_size;
        fields.data_offset = self->base_address + data;
        fields.compressed_size = packed;
        fields.timestamp = ((uint32_t)zpi_le16(record + 10) << 16) |
                           (uint32_t)zpi_le16(record + 12);
        fields.record = index;
        if (!zpi_emit(stream, name, length, &fields)) return false;
        offset = data + packed;
    }
    /* No directory to land on: the filler in front of the trailer is the
     * only structural check left. */
    return offset <= layout->trailer &&
           layout->trailer - offset <= ZPI_V2_MAX_GAP;
}

/* Reads the trailer of one version and checks the header it points at. */
static bool zpi_locate(Abstractformat *self, int64_t span, uint16_t version,
                       zpi_layout *layout) {
    uint8_t trailer[ZPI_V2_TRAILER];
    uint8_t header[ZPI_HEADER_PROBE];
    int64_t trailer_size = version == 2U ? ZPI_V2_TRAILER : ZPI_V1_TRAILER;
    int64_t trailer_offset = span - trailer_size;
    int64_t header_offset;
    int64_t count;
    if (trailer_offset < ZPI_MIN_STUB + ZPI_HEADER_SIZE + ZPI_MIN_PACKED ||
        !zpi_read(self, trailer_offset, trailer, (size_t)trailer_size)) {
        return false;
    }
    if (version == 2U) {
        count = (int64_t)(int32_t)zpi_le32(trailer);
        header_offset = (int64_t)zpi_le32(trailer + 4);
    } else {
        count = (int64_t)zpi_le16(trailer);
        header_offset = (int64_t)zpi_le32(trailer + 2);
    }
    if (count < 1 || count > ZPI_MAX_MEMBERS) return false;
    /* A self-extractor always has a stub in front of its archive; the bare
     * archive at offset 0 belongs to xx_ibmzpak. */
    if (header_offset < ZPI_MIN_STUB ||
        header_offset > trailer_offset - ZPI_HEADER_PROBE) {
        return false;
    }
    if (!zpi_read(self, header_offset, header, sizeof(header)) ||
        xx_rt_memcmp(header, "-ZPAK", 5U) != 0 || header[5] != 0U ||
        zpi_le16(header + 6) != version) {
        return false;
    }
    if (version == 2U ? header[8] != ZPI_V2_TAG
                      : !zpi_is_prelude(header + 8)) {
        return false;
    }
    layout->span = span;
    layout->header = header_offset;
    layout->trailer = trailer_offset;
    layout->count = (uint32_t)count;
    layout->version = version;
    return true;
}

/* Validates the container; with @p stream also collects the members. */
static bool zpi_parse(Abstractformat *self, zpi_layout *layout,
                      zpi_stream *stream, xx_pd_struct *pd) {
    uint8_t mz[2];
    int64_t total;
    int64_t span;
    uint16_t version;
    if (!self || !self->device || self->base_address < 0 || !layout) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < ZPI_MIN_STUB + ZPI_HEADER_SIZE + ZPI_MIN_PACKED +
                   ZPI_V1_TRAILER ||
        !zpi_read(self, 0, mz, sizeof(mz)) || mz[0] != 'M' || mz[1] != 'Z') {
        return false;
    }
    for (version = 2U; version >= 1U; --version) {
        bool walked;
        xx_mem_zero(layout, sizeof(*layout));
        if (!zpi_locate(self, span, version, layout)) continue;
        walked = version == 2U ? zpi_walk_v2(self, layout, stream, pd)
                               : zpi_walk_v1(self, layout, stream, pd);
        if (walked) {
            return !stream || (stream->count == layout->count &&
                               zpi_make_unique(stream));
        }
        if (stream) {
            size_t index;
            for (index = 0U; index < stream->count; ++index) {
                xx_str_free(stream->items[index].name);
            }
            stream->count = 0U;
        }
    }
    return false;
}

/* ------------------------------------------------------------- members -- */

static uint8_t *zpi_load(Abstractformat *self, const zpi_member *member) {
    uint8_t *packed;
    if (member->compressed_size < ZPI_MIN_PACKED ||
        member->compressed_size > ZPI_MAX_PACKED ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return NULL;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return NULL;
    if (!zpi_read(self, member->data_offset - self->base_address, packed,
                  (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

/* Recovers the plaintext length, which the container does not store. The
 * decoder must stop exactly at the stored compressed size. */
static void zpi_measure(Abstractformat *self, zpi_member *member) {
    uint8_t *packed;
    size_t consumed = 0U;
    size_t produced = 0U;
    if (member->measure != ZPI_UNMEASURED) return;
    member->measure = ZPI_UNMEASURABLE;
    packed = zpi_load(self, member);
    if (!packed) return;
    if (xx_dcl_scan_memory(packed, (size_t)member->compressed_size,
                           ZPI_MAX_DECODED, &consumed, &produced) &&
        consumed == (size_t)member->compressed_size &&
        produced <= ZPI_MAX_DECODED) {
        member->uncompressed_size = (int64_t)produced;
        member->measure = ZPI_MEASURED;
    }
    xx_mem_free(packed);
}

static bool zpi_decode(Abstractformat *self, zpi_member *member,
                       uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed;
    uint8_t *plain;
    size_t written = 0U;
    *out = NULL;
    *out_size = 0U;
    zpi_measure(self, member);
    if (member->measure != ZPI_MEASURED) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->uncompressed_size == 0) return true;
    packed = zpi_load(self, member);
    if (!packed) return false;
    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = written;
    return true;
}

static bool zpi_set_record(Abstractformat *self, xx_archive_record *record,
                           zpi_member *member) {
    bool result;
    zpi_measure(self, member);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    result =
        xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                       (uint64_t)member->compressed_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                       ZPI_METHOD_DCL) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                       member->timestamp) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                        false);
    /* A stream the decoder cannot measure has no honest size to publish. */
    if (result && member->measure == ZPI_MEASURED) {
        result = xx_archive_record_set_meta_u64(
            record, XX_META_ID_UNCOMPRESSED_SIZE,
            (uint64_t)member->uncompressed_size);
    }
    return result;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ibm_zpak_installer_init(xx_ibm_zpak_installer *archive,
                                xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_IBM_ZPAK_INSTALLER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zpak-sfx");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_ibm_zpak_installer_check_is_valid;
    archive->format.handle_base_info = xx_ibm_zpak_installer_handle_base_info;
    archive->format.get_format_size = xx_ibm_zpak_installer_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ibm_zpak_installer_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ibm_zpak_installer_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ibm_zpak_installer_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ibm_zpak_installer_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ibm_zpak_installer_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ibm_zpak_installer_free_archive_records_reading;
}

xx_ibm_zpak_installer *xx_ibm_zpak_installer_create(xx_io_device *device,
                                                    int64_t base_address) {
    xx_ibm_zpak_installer *archive =
        (xx_ibm_zpak_installer *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ibm_zpak_installer_init(archive, device, base_address);
    return archive;
}

void xx_ibm_zpak_installer_destroy(xx_ibm_zpak_installer *archive) {
    if (!archive) return;
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ibm_zpak_installer_free(xx_ibm_zpak_installer *archive) {
    if (!archive) return;
    xx_ibm_zpak_installer_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_ibm_zpak_installer_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd) {
    zpi_layout layout;
    return zpi_parse(self, &layout, NULL, pd);
}

bool xx_ibm_zpak_installer_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd) {
    xx_ibm_zpak_installer *archive = (xx_ibm_zpak_installer *)self;
    zpi_layout layout;
    if (!self) return false;
    self->base_info_handled = true;
    if (!zpi_parse(self, &layout, NULL, pd)) {
        self->is_valid = false;
        self->format_size = 0;
        self->number_of_archive_records = 0U;
        archive->number_of_records = 0U;
        return false;
    }
    self->is_valid = true;
    /* Stub, archive and trailer: the container runs to the end of the
     * file by construction. */
    self->format_size = layout.span;
    self->number_of_archive_records = layout.count;
    archive->number_of_records = layout.count;
    archive->version = layout.version;
    archive->archive_offset = layout.header;
    return true;
}

int64_t xx_ibm_zpak_installer_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ibm_zpak_installer_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ibm_zpak_installer *)self)->number_of_records
                          : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool zpi_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;
    if (!options) return true;
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

static const xx_var *zpi_option(const xx_list_s *options, uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_ibm_zpak_installer_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    zpi_stream *stream;
    zpi_layout layout;
    xx_archive_record_state *state;
    if (!self || !self->device) return NULL;
    stream = (zpi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!zpi_parse(self, &layout, stream, pd) || stream->count == 0U) {
        zpi_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        zpi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = zpi_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!zpi_copy_options(&state->options, options) ||
        !zpi_set_record(self, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ibm_zpak_installer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ibm_zpak_installer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    zpi_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (zpi_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = zpi_set_record(self, &state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ibm_zpak_installer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    zpi_stream *stream;
    zpi_member *member;
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
    stream = (zpi_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];

    path_option = zpi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member. */
        result = zpi_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (!member->safe) return false;
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

    /* Decode first, so a bad stream never leaves an empty file behind. */
    if (!zpi_decode(self, member, &plain, &plain_size, pd) ||
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
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
        if (!result && output && created) xx_rt_remove(target_path);
    }
    xx_mem_free(plain);
    xx_str_free(target_path);
    return result;
}

void xx_ibm_zpak_installer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
