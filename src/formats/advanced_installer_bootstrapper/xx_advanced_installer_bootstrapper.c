/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Advanced Installer (Caphyon) EXE bootstrapper.  The trailer layout is in
 * xx_advanced_installer_bootstrapper.h.
 *
 * Written from the container layout as observed in a complete bootstrapper
 * and as described by XArchive's installers/xadvancedinstaller.cpp (MIT,
 * same author): the footer fields, the 24-byte file records, the
 * "first 0x200 bytes XOR 0xFF" rule for xor_flag 2 and the Authenticode
 * placement of the trailer come from there.  No code was taken from it; in
 * particular XArchive only accepts .msi/.cab/.ini tables and hands the MSI
 * on to its MSI reader, while this reader lists every stored file of any
 * type, which real bootstrappers need (language DLLs, decoder.dll,
 * FILES.7z, an MSI packed in a 7z, ...).
 *
 * What is checked, so that a trailer string alone never detects:
 *   - "MZ" and a PE signature in front (the PE32 or PE32+ optional header
 *     tells where the security directory is; nothing else of the image is
 *     read);
 *   - the marker at the logical end of the file: EOF, or the start of a
 *     well-formed certificate table that ends at EOF, each allowing up to
 *     seven zero bytes of alignment after the marker;
 *   - mode 0/1, version 100, 1..4096 files, metadata_end equal to the
 *     footer's own offset, 32 hex digits, data_offset <= info_offset <
 *     footer, data_offset behind the PE headers;
 *   - every record: xor_flag 0 or 2, a name of 1..2048 UTF-16 units without
 *     NUL, data inside [data_offset, info_offset); the table ends exactly at
 *     the footer (mode 0) or at a well-formed external name block (mode 1);
 *   - no two non-empty data regions overlap, so the output can never be
 *     larger than the input, whatever the table says.
 *
 * Member names use '\' as separator and are turned into '/' paths.  A name
 * that could escape the destination or alias a device is listed but not
 * extracted; duplicate names (compared ASCII case-insensitively) get a
 * "_<record number>" suffix, and without the overwrite option an existing
 * file is never replaced.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/advanced_installer_bootstrapper/xx_advanced_installer_bootstrapper.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef ADVANCED_INSTALLER_BOOTSTRAPPER
#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_FILE_TYPE \
    XX_FILE_TYPE_ADVANCED_INSTALLER_BOOTSTRAPPER
#else
#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define AIB_MARKER XX_ADVANCED_INSTALLER_BOOTSTRAPPER_MARKER
#define AIB_MARKER_SIZE XX_ADVANCED_INSTALLER_BOOTSTRAPPER_MARKER_SIZE
#define AIB_FOOTER_SIZE XX_ADVANCED_INSTALLER_BOOTSTRAPPER_FOOTER_SIZE
#define AIB_TRAILER_SIZE (AIB_FOOTER_SIZE + AIB_MARKER_SIZE)
#define AIB_PAD_MAX 7U
#define AIB_TAIL_READ (AIB_TRAILER_SIZE + AIB_PAD_MAX)
#define AIB_VERSION XX_ADVANCED_INSTALLER_BOOTSTRAPPER_VERSION
#define AIB_RECORD_SIZE XX_ADVANCED_INSTALLER_BOOTSTRAPPER_RECORD_SIZE
#define AIB_MAX_FILES XX_ADVANCED_INSTALLER_BOOTSTRAPPER_MAX_FILES
#define AIB_MAX_NAME XX_ADVANCED_INSTALLER_BOOTSTRAPPER_MAX_NAME
#define AIB_XOR_SIZE XX_ADVANCED_INSTALLER_BOOTSTRAPPER_XOR_SIZE
#define AIB_HEX_OFFSET 28U
#define AIB_HEX_SIZE 32U
#define AIB_CERT_MAX_ENTRIES 64U
#define AIB_RENAME_PASSES 4U
#define AIB_PE_MAGIC32 0x10BU
#define AIB_PE_MAGIC64 0x20BU

typedef struct aib_member_s {
    char *name;            /* UTF-8, '/'-separated, after renaming */
    int64_t header_offset; /* absolute */
    int64_t header_size;
    int64_t data_offset;   /* absolute */
    int64_t size;
    uint32_t type;
    uint32_t index;
    uint32_t xor_flag;
    size_t slot;           /* position in the table */
    bool extractable;
} aib_member;

typedef struct aib_stream_s {
    aib_member *items;
    size_t count;
    size_t current;
    int64_t archive_end;   /* absolute */
    int64_t footer_offset; /* absolute */
    uint32_t mode;
    uint32_t info_offset;
    uint32_t data_offset;
    bool is_signed;
    bool is_pe64;
    char guid[AIB_HEX_SIZE + 1U];
    char *external_name;
} aib_stream;

typedef struct aib_pe_s {
    int64_t headers_end; /* relative to the executable start */
    bool pe64;
    bool has_cert;
    int64_t cert_offset; /* relative */
} aib_pe;

static void aib_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool aib_read_at(xx_io_device *device, int64_t offset, void *data,
                        size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || offset < 0 || (!data && size != 0U) ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static uint16_t aib_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t aib_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool aib_is_hex(uint8_t c) {
    return (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
           (c >= (uint8_t)'A' && c <= (uint8_t)'F') ||
           (c >= (uint8_t)'a' && c <= (uint8_t)'f');
}

static uint8_t aib_fold(uint8_t c) {
    return (c >= (uint8_t)'a' && c <= (uint8_t)'z') ? (uint8_t)(c - 32U) : c;
}

static void aib_stream_free(void *pointer) {
    aib_stream *stream = (aib_stream *)pointer;
    size_t index;
    if (!stream) return;
    if (stream->items) {
        for (index = 0U; index < stream->count; ++index) {
            if (stream->items[index].name) {
                xx_str_free(stream->items[index].name);
            }
        }
        xx_mem_free(stream->items);
    }
    if (stream->external_name) xx_str_free(stream->external_name);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------ PE -- */

/* The WIN_CERTIFICATE chain that must fill [offset, avail) exactly. */
static bool aib_cert_table_ok(xx_io_device *device, int64_t base,
                              int64_t offset, int64_t size, int64_t avail) {
    int64_t position = offset;
    int64_t remaining = size;
    unsigned entries = 0U;
    if (size < 8 || offset < 0 || (offset & 7) != 0 || offset > avail ||
        size != avail - offset) {
        return false;
    }
    while (remaining > 0) {
        uint8_t header[8];
        uint32_t length;
        uint16_t revision;
        uint16_t type;
        int64_t aligned;
        if (remaining < 8 || ++entries > AIB_CERT_MAX_ENTRIES ||
            !aib_read_at(device, base + position, header, sizeof(header))) {
            return false;
        }
        length = aib_u32(header);
        revision = aib_u16(header + 4);
        type = aib_u16(header + 6);
        if (length < 8U || (int64_t)length > remaining ||
            (revision != 0x0100U && revision != 0x0200U) || type < 1U ||
            type > 4U) {
            return false;
        }
        aligned = ((int64_t)length + 7) & ~(int64_t)7;
        /* The last entry may stop short of its alignment padding. */
        if (aligned > remaining) aligned = remaining;
        position += aligned;
        remaining -= aligned;
    }
    return true;
}

static bool aib_parse_pe(xx_io_device *device, int64_t base, int64_t avail,
                         aib_pe *pe) {
    uint8_t dos[64];
    uint8_t nt[26];
    uint8_t dirs[44];
    uint32_t e_lfanew;
    uint16_t optional_size;
    uint16_t magic;
    uint32_t directory_offset;
    xx_mem_zero(pe, sizeof(*pe));
    if (avail < (int64_t)(sizeof(dos) + AIB_TRAILER_SIZE) ||
        !aib_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z') {
        return false;
    }
    e_lfanew = aib_u32(dos + 0x3C);
    if (e_lfanew < 4U || (int64_t)e_lfanew > avail - (int64_t)sizeof(nt) ||
        !aib_read_at(device, base + e_lfanew, nt, sizeof(nt)) ||
        nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U) {
        return false;
    }
    optional_size = aib_u16(nt + 20);
    magic = aib_u16(nt + 24);
    if (magic == AIB_PE_MAGIC32) {
        directory_offset = 96U;
    } else if (magic == AIB_PE_MAGIC64) {
        directory_offset = 112U;
        pe->pe64 = true;
    } else {
        return false;
    }
    pe->headers_end = (int64_t)e_lfanew + 24 + optional_size;
    /* NumberOfRvaAndSizes sits right before the directories; entry 4 is the
     * security directory, whose "RVA" is a file offset. */
    if (optional_size >= directory_offset + 5U * 8U &&
        (int64_t)e_lfanew + 24 + directory_offset + 40 <= avail &&
        aib_read_at(device, base + e_lfanew + 24 + directory_offset - 4U,
                    dirs, sizeof(dirs)) &&
        aib_u32(dirs) >= 5U) {
        uint32_t cert_offset = aib_u32(dirs + 4 + 32);
        uint32_t cert_size = aib_u32(dirs + 4 + 36);
        if (cert_size != 0U && (int64_t)cert_offset >= pe->headers_end &&
            aib_cert_table_ok(device, base, cert_offset, cert_size, avail)) {
            pe->has_cert = true;
            pe->cert_offset = cert_offset;
        }
    }
    return true;
}

/* The marker immediately before `end` (relative), allowing up to seven zero
 * bytes behind it.  Returns the marker's relative offset, or -1. */
static int64_t aib_find_marker(xx_io_device *device, int64_t base,
                               int64_t end) {
    uint8_t tail[AIB_TAIL_READ];
    size_t have;
    unsigned pad;
    if (end < (int64_t)AIB_TRAILER_SIZE) return -1;
    have = end < (int64_t)AIB_TAIL_READ ? (size_t)end : AIB_TAIL_READ;
    if (!aib_read_at(device, base + end - (int64_t)have, tail, have)) {
        return -1;
    }
    for (pad = 0U; pad <= AIB_PAD_MAX; ++pad) {
        size_t at;
        size_t index;
        bool zero = true;
        if (have < AIB_TRAILER_SIZE + pad) break;
        at = have - pad - AIB_MARKER_SIZE;
        for (index = at + AIB_MARKER_SIZE; index < have; ++index) {
            if (tail[index] != 0U) zero = false;
        }
        if (zero && xx_rt_memcmp(tail + at, AIB_MARKER, AIB_MARKER_SIZE) == 0) {
            return end - (int64_t)pad - (int64_t)AIB_MARKER_SIZE;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------- names -- */

/* UTF-16LE units to UTF-8 with '\' turned into '/'.  An unpaired surrogate
 * becomes U+FFFD and marks the name unsafe. */
static char *aib_name_to_utf8(const uint8_t *units, size_t count,
                              bool *unsafe) {
    char *out = xx_str_create_len(count * 3U);
    size_t index = 0U;
    size_t length = 0U;
    if (!out) return NULL;
    while (index < count) {
        uint32_t code = aib_u16(units + index * 2U);
        ++index;
        if (code >= 0xD800U && code <= 0xDBFFU && index < count) {
            uint32_t low = aib_u16(units + index * 2U);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code = 0x10000U + ((code - 0xD800U) << 10) + (low - 0xDC00U);
                ++index;
            }
        }
        if (code >= 0xD800U && code <= 0xDFFFU) {
            code = 0xFFFDU;
            *unsafe = true;
        }
        if (code == 0x5CU) code = 0x2FU;
        if (code < 0x80U) {
            out[length++] = (char)code;
        } else if (code < 0x800U) {
            out[length++] = (char)(0xC0U | (code >> 6));
            out[length++] = (char)(0x80U | (code & 0x3FU));
        } else if (code < 0x10000U) {
            out[length++] = (char)(0xE0U | (code >> 12));
            out[length++] = (char)(0x80U | ((code >> 6) & 0x3FU));
            out[length++] = (char)(0x80U | (code & 0x3FU));
        } else {
            /* Four bytes from two units: still within count * 3. */
            out[length++] = (char)(0xF0U | (code >> 18));
            out[length++] = (char)(0x80U | ((code >> 12) & 0x3FU));
            out[length++] = (char)(0x80U | ((code >> 6) & 0x3FU));
            out[length++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    out[length] = '\0';
    return out;
}

static bool aib_is_word(const uint8_t *text, size_t length, const char *word) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (word[index] == '\0' || aib_fold(text[index]) != (uint8_t)word[index]) {
            return false;
        }
    }
    return word[length] == '\0';
}

/* A component Windows resolves to a device, with or without extension. */
static bool aib_is_device(const uint8_t *component, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U;
    size_t index;
    while (stem < length && component[stem] != (uint8_t)'.') ++stem;
    while (stem > 0U && component[stem - 1U] == (uint8_t)' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        if (aib_is_word(component, stem, devices[index])) return true;
    }
    if (stem >= 4U && (aib_is_word(component, 3U, "COM") ||
                       aib_is_word(component, 3U, "LPT"))) {
        if (stem == 4U && component[3] >= (uint8_t)'0' &&
            component[3] <= (uint8_t)'9') {
            return true;
        }
        /* Superscript one, two and three (U+00B9, U+00B2, U+00B3). */
        if (stem == 5U && component[3] == 0xC2U &&
            (component[4] == 0xB9U || component[4] == 0xB2U ||
             component[4] == 0xB3U)) {
            return true;
        }
    }
    return false;
}

/* Refused: empty, absolute, empty / "." / ".." components, a component
 * ending in '.' or ' ', control characters, the characters Windows reserves
 * (':' covers drive letters and streams) and device names. */
static bool aib_name_safe(const char *name) {
    const uint8_t *raw = (const uint8_t *)name;
    size_t start = 0U;
    if (!raw || raw[0] == 0U || raw[0] == (uint8_t)'/') return false;
    for (;;) {
        size_t end = start;
        size_t index;
        while (raw[end] != 0U && raw[end] != (uint8_t)'/') ++end;
        if (end == start) return false;
        if (raw[end - 1U] == (uint8_t)'.' || raw[end - 1U] == (uint8_t)' ') {
            return false;
        }
        for (index = start; index < end; ++index) {
            uint8_t c = raw[index];
            if (c < 0x20U || c == 0x7FU || c == (uint8_t)':' ||
                c == (uint8_t)'<' || c == (uint8_t)'>' || c == (uint8_t)'"' ||
                c == (uint8_t)'|' || c == (uint8_t)'?' || c == (uint8_t)'*') {
                return false;
            }
        }
        if (aib_is_device(raw + start, end - start)) return false;
        if (raw[end] == 0U) return true;
        start = end + 1U;
    }
}

static int aib_name_compare(const char *left, const char *right) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (;;) {
        uint8_t x = aib_fold(*a++);
        uint8_t y = aib_fold(*b++);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}

/* Shell sort of an index array; `less` decides the order. */
static void aib_sort(size_t *order, size_t count, const aib_member *items,
                     bool (*less)(const aib_member *, const aib_member *)) {
    size_t gap = 1U;
    size_t index;
    for (index = 0U; index < count; ++index) order[index] = index;
    while (gap < count / 3U) gap = gap * 3U + 1U;
    for (; gap > 0U; gap /= 3U) {
        for (index = gap; index < count; ++index) {
            size_t value = order[index];
            size_t slot = index;
            while (slot >= gap && less(&items[value], &items[order[slot - gap]])) {
                order[slot] = order[slot - gap];
                slot -= gap;
            }
            order[slot] = value;
        }
    }
}

static bool aib_less_by_offset(const aib_member *a, const aib_member *b) {
    if (a->data_offset != b->data_offset) return a->data_offset < b->data_offset;
    return a->size < b->size;
}

static bool aib_less_by_name(const aib_member *a, const aib_member *b) {
    int order = aib_name_compare(a->name, b->name);
    if (order != 0) return order < 0;
    return a->slot < b->slot;
}

static size_t aib_put_decimal(char *out, uint64_t value) {
    char digits[24];
    size_t count = 0U;
    size_t index;
    do {
        digits[count++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (index = 0U; index < count; ++index) out[index] = digits[count - 1U - index];
    return count;
}

/* "<stem>_<number>[_<pass>]<extension>" in the last path component. */
static char *aib_renamed(const char *name, size_t number, unsigned pass) {
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
    suffix_length += aib_put_decimal(suffix + suffix_length, number);
    if (pass > 1U) {
        suffix[suffix_length++] = '_';
        suffix_length += aib_put_decimal(suffix + suffix_length, pass);
    }
    out = xx_str_create_len(length + suffix_length);
    if (!out) return NULL;
    at = 0U;
    xx_rt_memcpy(out, name, insert);
    at += insert;
    xx_rt_memcpy(out + at, suffix, suffix_length);
    at += suffix_length;
    xx_rt_memcpy(out + at, name + insert, length - insert);
    out[length + suffix_length] = '\0';
    return out;
}

/* Give each member a name no other member shares (ASCII case folded).  The
 * earliest record keeps its name; a later one is renamed with its record
 * number; whatever still collides after the last pass is not extracted. */
static bool aib_make_unique(aib_stream *stream) {
    size_t *order;
    unsigned pass;
    size_t index;
    if (stream->count < 2U) return true;
    order = (size_t *)xx_mem_alloc(stream->count * sizeof(*order));
    if (!order) return false;
    for (pass = 1U; pass <= AIB_RENAME_PASSES + 1U; ++pass) {
        bool changed = false;
        size_t anchor;
        aib_sort(order, stream->count, stream->items, aib_less_by_name);
        /* `anchor` is the earliest record of a run of equal names; it keeps
         * its name and every later record of the run is renamed. */
        anchor = order[0];
        for (index = 1U; index < stream->count; ++index) {
            aib_member *later = &stream->items[order[index]];
            char *renamed;
            if (aib_name_compare(stream->items[anchor].name, later->name) !=
                0) {
                anchor = order[index];
                continue;
            }
            if (pass > AIB_RENAME_PASSES) {
                later->extractable = false;
                continue;
            }
            renamed = aib_renamed(later->name, later->slot + 1U, pass);
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

typedef struct aib_footer_s {
    uint32_t mode;
    uint32_t external;
    uint32_t count;
    uint32_t info;
    uint32_t data;
    int64_t table_end; /* relative */
    char guid[AIB_HEX_SIZE + 1U];
} aib_footer;

static bool aib_check_footer(const uint8_t *raw, int64_t footer,
                             const aib_pe *pe, aib_footer *out) {
    unsigned index;
    uint32_t metadata_end;
    if (footer < 0 || footer > (int64_t)UINT32_MAX) return false;
    out->mode = aib_u32(raw + 0);
    out->external = aib_u32(raw + 4);
    out->count = aib_u32(raw + 8);
    metadata_end = aib_u32(raw + 16);
    out->info = aib_u32(raw + 20);
    out->data = aib_u32(raw + 24);
    if (out->mode > 1U || aib_u32(raw + 12) != AIB_VERSION ||
        out->count == 0U || out->count > AIB_MAX_FILES ||
        (int64_t)metadata_end != footer || (int64_t)out->info >= footer ||
        out->data > out->info || (int64_t)out->data < pe->headers_end) {
        return false;
    }
    for (index = 0U; index < AIB_HEX_SIZE; ++index) {
        uint8_t c = raw[AIB_HEX_OFFSET + index];
        if (!aib_is_hex(c)) return false;
        out->guid[index] = (char)c;
    }
    out->guid[AIB_HEX_SIZE] = '\0';
    if (out->mode == 0U) {
        if ((int64_t)out->external != footer) return false;
        out->table_end = footer;
    } else {
        if (out->external < out->info || (int64_t)out->external + 8 > footer) {
            return false;
        }
        out->table_end = out->external;
    }
    return out->table_end - (int64_t)out->info >=
           (int64_t)out->count * (int64_t)AIB_RECORD_SIZE;
}

/* Mode 1: {u32 0, u32 n, UTF-16LE name[n]} from `external` to the footer. */
static bool aib_read_external(xx_io_device *device, int64_t base,
                              const aib_footer *footer, int64_t footer_offset,
                              aib_stream *stream) {
    uint8_t header[8];
    uint8_t *units;
    uint32_t count;
    size_t index;
    bool unsafe = false;
    if (!aib_read_at(device, base + footer->external, header, sizeof(header)) ||
        aib_u32(header) != 0U) {
        return false;
    }
    count = aib_u32(header + 4);
    if (count == 0U || count > AIB_MAX_NAME ||
        (int64_t)footer->external + 8 + (int64_t)count * 2 != footer_offset) {
        return false;
    }
    units = (uint8_t *)xx_mem_alloc((size_t)count * 2U);
    if (!units) return false;
    if (!aib_read_at(device, base + footer->external + 8, units,
                     (size_t)count * 2U)) {
        xx_mem_free(units);
        return false;
    }
    for (index = 0U; index < count; ++index) {
        if (aib_u16(units + index * 2U) == 0U) {
            xx_mem_free(units);
            return false;
        }
    }
    stream->external_name = aib_name_to_utf8(units, count, &unsafe);
    xx_mem_free(units);
    return stream->external_name != NULL;
}

static bool aib_read_table(xx_io_device *device, int64_t base,
                           const aib_footer *footer, aib_stream *stream,
                           xx_pd_struct *pd) {
    uint8_t record[AIB_RECORD_SIZE];
    uint8_t *units;
    int64_t position = footer->info;
    size_t *order;
    size_t index;
    int64_t previous_end;
    stream->items =
        (aib_member *)xx_mem_calloc(footer->count, sizeof(*stream->items));
    units = (uint8_t *)xx_mem_alloc((size_t)AIB_MAX_NAME * 2U);
    if (!stream->items || !units) {
        if (units) xx_mem_free(units);
        return false;
    }
    for (index = 0U; index < footer->count; ++index) {
        aib_member *member = &stream->items[index];
        uint32_t name_units;
        uint32_t offset;
        uint32_t size;
        size_t unit;
        bool unsafe = false;
        if ((pd && xx_pd_is_stopped(pd)) ||
            position > footer->table_end - (int64_t)AIB_RECORD_SIZE ||
            !aib_read_at(device, base + position, record, sizeof(record))) {
            xx_mem_free(units);
            return false;
        }
        name_units = aib_u32(record + 20);
        if (name_units == 0U || name_units > AIB_MAX_NAME ||
            position + (int64_t)AIB_RECORD_SIZE + (int64_t)name_units * 2 >
                footer->table_end ||
            !aib_read_at(device, base + position + AIB_RECORD_SIZE, units,
                         (size_t)name_units * 2U)) {
            xx_mem_free(units);
            return false;
        }
        for (unit = 0U; unit < name_units; ++unit) {
            if (aib_u16(units + unit * 2U) == 0U) {
                xx_mem_free(units);
                return false;
            }
        }
        member->type = aib_u32(record);
        member->index = aib_u32(record + 4);
        member->xor_flag = aib_u32(record + 8);
        size = aib_u32(record + 12);
        offset = aib_u32(record + 16);
        if ((member->xor_flag != 0U && member->xor_flag != 2U) ||
            offset < footer->data || offset > footer->info ||
            size > footer->info - offset) {
            xx_mem_free(units);
            return false;
        }
        member->header_offset = base + position;
        member->header_size = (int64_t)AIB_RECORD_SIZE + (int64_t)name_units * 2;
        member->data_offset = base + offset;
        member->size = size;
        member->slot = index;
        member->name = aib_name_to_utf8(units, name_units, &unsafe);
        ++stream->count;
        if (!member->name) {
            xx_mem_free(units);
            return false;
        }
        member->extractable = !unsafe && aib_name_safe(member->name);
        position += member->header_size;
    }
    xx_mem_free(units);
    if (position != footer->table_end) return false;

    /* Non-empty regions must not overlap. */
    order = (size_t *)xx_mem_alloc(stream->count * sizeof(*order));
    if (!order) return false;
    aib_sort(order, stream->count, stream->items, aib_less_by_offset);
    previous_end = -1;
    for (index = 0U; index < stream->count; ++index) {
        const aib_member *member = &stream->items[order[index]];
        if (member->size == 0) continue;
        if (member->data_offset < previous_end) {
            xx_mem_free(order);
            return false;
        }
        previous_end = member->data_offset + member->size;
    }
    xx_mem_free(order);
    return true;
}

static bool aib_try_trailer(xx_io_device *device, int64_t base,
                            const aib_pe *pe, int64_t marker,
                            aib_stream *stream, xx_pd_struct *pd) {
    uint8_t raw[AIB_FOOTER_SIZE];
    aib_footer footer;
    int64_t footer_offset = marker - (int64_t)AIB_FOOTER_SIZE;
    xx_mem_zero(&footer, sizeof(footer));
    if (footer_offset < pe->headers_end ||
        !aib_read_at(device, base + footer_offset, raw, sizeof(raw)) ||
        !aib_check_footer(raw, footer_offset, pe, &footer)) {
        return false;
    }
    if (footer.mode == 1U &&
        !aib_read_external(device, base, &footer, footer_offset, stream)) {
        return false;
    }
    if (!aib_read_table(device, base, &footer, stream, pd)) return false;
    stream->footer_offset = base + footer_offset;
    stream->mode = footer.mode;
    stream->info_offset = footer.info;
    stream->data_offset = footer.data;
    xx_rt_memcpy(stream->guid, footer.guid, sizeof(stream->guid));
    return true;
}

static void aib_stream_reset(aib_stream *stream) {
    size_t index;
    if (stream->items) {
        for (index = 0U; index < stream->count; ++index) {
            if (stream->items[index].name) {
                xx_str_free(stream->items[index].name);
            }
        }
        xx_mem_free(stream->items);
    }
    if (stream->external_name) xx_str_free(stream->external_name);
    xx_mem_zero(stream, sizeof(*stream));
}

static aib_stream *aib_parse(Abstractformat *self, xx_pd_struct *pd) {
    aib_pe pe;
    aib_stream *stream;
    int64_t total;
    int64_t avail;
    int64_t base;
    int64_t marker;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return NULL;
    }
    base = self->base_address;
    total = xx_io_total_size(self->device);
    if (total < 0 || base > total) return NULL;
    avail = total - base;
    if (!aib_parse_pe(self->device, base, avail, &pe)) return NULL;
    stream = (aib_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->is_pe64 = pe.pe64;

    /* A signed bootstrapper: the trailer ends where the certificate table
     * starts.  Otherwise it ends the file. */
    if (pe.has_cert) {
        marker = aib_find_marker(self->device, base, pe.cert_offset);
        if (marker >= 0 &&
            aib_try_trailer(self->device, base, &pe, marker, stream, pd)) {
            stream->is_signed = true;
            stream->is_pe64 = pe.pe64;
            stream->archive_end = total;
            return stream;
        }
        aib_stream_reset(stream);
        stream->is_pe64 = pe.pe64;
    }
    marker = aib_find_marker(self->device, base, avail);
    if (marker >= 0 &&
        aib_try_trailer(self->device, base, &pe, marker, stream, pd)) {
        stream->archive_end = total;
        return stream;
    }
    aib_stream_free(stream);
    return NULL;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_advanced_installer_bootstrapper_init(
    xx_advanced_installer_bootstrapper *archive, xx_io_device *device,
    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_ADVANCED_INSTALLER_BOOTSTRAPPER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.os = XX_OS_WINDOWS;
    archive->format.is_archive = true;
    archive->format.is_executable = true;
    xx_format_set_mime_type(&archive->format,
                            "application/vnd.microsoft.portable-executable");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_advanced_installer_bootstrapper_check_is_valid;
    archive->format.handle_base_info =
        xx_advanced_installer_bootstrapper_handle_base_info;
    archive->format.get_format_size =
        xx_advanced_installer_bootstrapper_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_advanced_installer_bootstrapper_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_advanced_installer_bootstrapper_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_advanced_installer_bootstrapper_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_advanced_installer_bootstrapper_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_advanced_installer_bootstrapper_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_advanced_installer_bootstrapper_free_archive_records_reading;
    archive->format.destroy = aib_vtable_destroy;
    archive->footer_offset = -1;
}

xx_advanced_installer_bootstrapper *xx_advanced_installer_bootstrapper_create(
    xx_io_device *device, int64_t base_address) {
    xx_advanced_installer_bootstrapper *archive =
        (xx_advanced_installer_bootstrapper *)xx_mem_alloc(sizeof(*archive));
    if (archive) {
        xx_advanced_installer_bootstrapper_init(archive, device, base_address);
    }
    return archive;
}

void xx_advanced_installer_bootstrapper_destroy(
    xx_advanced_installer_bootstrapper *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: that dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    if (archive->external_name) {
        xx_str_free(archive->external_name);
        archive->external_name = NULL;
    }
    archive->number_of_records = 0U;
}

void xx_advanced_installer_bootstrapper_free(
    xx_advanced_installer_bootstrapper *archive) {
    if (!archive) return;
    xx_advanced_installer_bootstrapper_destroy(archive);
    xx_mem_free(archive);
}

static void aib_vtable_destroy(Abstractformat *self) {
    xx_advanced_installer_bootstrapper_destroy(
        (xx_advanced_installer_bootstrapper *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_advanced_installer_bootstrapper_check_is_valid(Abstractformat *self,
                                                       xx_pd_struct *pd) {
    aib_stream *stream = aib_parse(self, pd);
    if (!stream) return false;
    aib_stream_free(stream);
    return true;
}

bool xx_advanced_installer_bootstrapper_handle_base_info(Abstractformat *self,
                                                         xx_pd_struct *pd) {
    xx_advanced_installer_bootstrapper *archive =
        (xx_advanced_installer_bootstrapper *)self;
    aib_stream *stream;
    char version[8];
    size_t length;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = aib_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_end - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = stream->count;
    self->is_signed = stream->is_signed;
    self->arch = stream->is_pe64 ? XX_ARCH_X86_64 : XX_ARCH_X86;
    length = aib_put_decimal(version, AIB_VERSION);
    version[length] = '\0';
    xx_format_set_version(self, version);
    archive->number_of_records = stream->count;
    archive->mode = stream->mode;
    archive->info_offset = stream->info_offset;
    archive->data_offset = stream->data_offset;
    archive->footer_offset = stream->footer_offset;
    archive->is_signed = stream->is_signed;
    archive->is_pe64 = stream->is_pe64;
    xx_rt_memcpy(archive->guid, stream->guid, sizeof(archive->guid));
    if (archive->external_name) xx_str_free(archive->external_name);
    archive->external_name = stream->external_name;
    stream->external_name = NULL;
    aib_stream_free(stream);
    return true;
}

int64_t xx_advanced_installer_bootstrapper_get_format_size(Abstractformat *self,
                                                           xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_advanced_installer_bootstrapper_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid
               ? ((xx_advanced_installer_bootstrapper *)self)->number_of_records
               : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool aib_set_record(xx_archive_record *record,
                           const aib_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           /* Stored; xor_flag 2 only obfuscates the first 0x200 bytes and
            * is undone on extraction. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool aib_copy_options(xx_list_s *target, const xx_list_s *options) {
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

xx_archive_record_state *
xx_advanced_installer_bootstrapper_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    aib_stream *stream;
    xx_archive_record_state *state;
    if (!self || !self->device) return NULL;
    stream = aib_parse(self, pd);
    if (!stream) return NULL;
    if (stream->count == 0U || !aib_make_unique(stream)) {
        aib_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        aib_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = aib_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!aib_copy_options(&state->options, options) ||
        !aib_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *
xx_advanced_installer_bootstrapper_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_advanced_installer_bootstrapper_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    aib_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (aib_stream *)state->internal_state;
    if (!stream || stream->current + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->current;
    ++state->current_index;
    state->has_record =
        aib_set_record(&state->current_record, &stream->items[stream->current]);
    return state->has_record;
}

static bool aib_write_all(xx_io_device *output, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) return false;
        done += (size_t)sent;
    }
    return true;
}

/* Copy one member to `output`, undoing the XOR of its first 0x200 bytes. */
static bool aib_extract(xx_io_device *device, const aib_member *member,
                        xx_io_device *output, xx_pd_struct *pd) {
    uint8_t head[AIB_XOR_SIZE];
    int64_t head_size = 0;
    int64_t index;
    int64_t total = xx_io_total_size(device);
    if (member->data_offset < 0 || member->size < 0 || total < 0 ||
        member->data_offset > total ||
        member->size > total - member->data_offset) {
        return false;
    }
    if (!output) return true;
    if (member->xor_flag == 2U && member->size > 0) {
        head_size = member->size < (int64_t)AIB_XOR_SIZE ? member->size
                                                         : (int64_t)AIB_XOR_SIZE;
        if (!aib_read_at(device, member->data_offset, head, (size_t)head_size)) {
            return false;
        }
        for (index = 0; index < head_size; ++index) head[index] ^= 0xFFU;
        if (!aib_write_all(output, head, (size_t)head_size)) return false;
    }
    if (member->size > head_size) {
        return xx_store_unpack_device(device, member->data_offset + head_size,
                                      member->size - head_size, output, pd);
    }
    return true;
}

bool xx_advanced_installer_bootstrapper_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    aib_stream *stream;
    const aib_member *member;
    const xx_var *option;
    const char *base_path = NULL;
    char *converted = NULL;
    char *target = NULL;
    xx_io_device *output;
    size_t base_length;
    bool overwrite;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (aib_stream *)state->internal_state;
    if (!stream || stream->current >= stream->count) return false;
    member = &stream->items[stream->current];

    option = xx_format_resolve_extra_parameter(self, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->size > xx_var_get_u64(option)) {
        return false;
    }
    option = xx_format_resolve_extra_parameter(self, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the member is addressable. */
        return aib_extract(self->device, member, NULL, pd);
    }
    if (!member->extractable || !aib_name_safe(member->name)) return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base_path = converted;
    }
    if (!base_path) {
        if (converted) xx_str_free(converted);
        return false;
    }
    base_length = xx_str_len(base_path);
    if (base_length != 0U && base_path[base_length - 1U] != '/' &&
        base_path[base_length - 1U] != '\\') {
        target = xx_str_concat3(base_path, "/", member->name);
    } else {
        target = xx_str_concat(base_path, member->name);
    }
    if (converted) xx_str_free(converted);
    if (!target) return false;
    if (!xx_store_create_dirs_a(target, false)) {
        xx_str_free(target);
        return false;
    }
    option = xx_format_resolve_extra_parameter(self, &state->options,
                                               XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    /* Without the overwrite option an existing file is never replaced. */
    output = xx_io_file_open(target, overwrite ? "wb" : "wbx");
    if (!output) {
        xx_str_free(target);
        return false;
    }
    result = aib_extract(self->device, member, output, pd);
    if (xx_io_close(output) != 0) result = false;
    if (!result) xx_rt_remove(target);
    xx_str_free(target);
    return result;
}

void xx_advanced_installer_bootstrapper_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------- getters -- */

uint32_t xx_advanced_installer_bootstrapper_get_mode(
    const xx_advanced_installer_bootstrapper *archive) {
    return archive ? archive->mode : 0U;
}

const char *xx_advanced_installer_bootstrapper_get_guid(
    const xx_advanced_installer_bootstrapper *archive) {
    return (archive && archive->guid[0] != '\0') ? archive->guid : NULL;
}

const char *xx_advanced_installer_bootstrapper_get_external_name(
    const xx_advanced_installer_bootstrapper *archive) {
    return archive ? archive->external_name : NULL;
}
