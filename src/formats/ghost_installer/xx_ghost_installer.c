/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ghost Installer (Ethalone) package reader.  The container layout is in
 * xx_ghost_installer.h: a chain of Microsoft Cabinets XORed with 0x8D, each
 * closed by a clear "GIPEND" trailer, standing alone (.gip) or appended to
 * the setup.exe stub as its PE overlay.
 *
 * Written from the structure of the two corpus packages and from the
 * Microsoft Cabinet format specification ([MS-CAB]).  The chain walk (a
 * 26-byte or a 34-byte trailer after each cabinet, a missing trailer ends
 * the chain) matches what U3's Ghost Installer handler accepts; no code was
 * taken from it or from any cabinet library.  The codecs are the library's
 * own LZX, Quantum and Deflate decoders.
 *
 * The cabinet is parsed here instead of being handed to the xx_cab reader
 * because unmasking must happen underneath it and because extraction needs
 * what xx_cab does not do: device names and "..", duplicate names across
 * and within cabinets, CFDATA checksums, MSZIP blocks that refer back to
 * the previous block's history, a per-folder memory cap and one decode per
 * folder instead of one per member.
 *
 * What a probe checks, so that the eight masked signature bytes alone never
 * detect: a CFHEADER whose cabinet fits the device, at least one folder and
 * one file, only the three defined flag bits, a folder table with known
 * compression types (LZX window 15..21, Quantum level 1..7 and window
 * 10..21), a CFDATA chain for every folder that stays inside the cabinet
 * with 1..32768 plain bytes per block, and a file table behind the folder
 * table whose entries name a folder of this cabinet (or one of the three
 * "continued" markers) and carry a NUL-terminated name without control
 * characters.  The probe allocates nothing and reads the cabinet's tables
 * through a 4 KiB window; the CFDATA walk is capped at one block per nine
 * cabinet bytes, the smallest a block can occupy.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ghost_installer/xx_ghost_installer.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzx/xx_lzx.h"
#include "xxfclib/algo/quantum/xx_quantum.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef GHOST_INSTALLER
#define XX_GHOST_INSTALLER_FILE_TYPE XX_FILE_TYPE_GHOST_INSTALLER
#else
#define XX_GHOST_INSTALLER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define GI_KEY XX_GHOST_INSTALLER_KEY
#define GI_MAGIC_SIZE XX_GHOST_INSTALLER_MAGIC_SIZE
#define GI_LONG_TRAILER XX_GHOST_INSTALLER_TRAILER_LONG
#define GI_SHORT_TRAILER XX_GHOST_INSTALLER_TRAILER_SHORT
#define GI_MAX_SEGMENTS XX_GHOST_INSTALLER_MAX_SEGMENTS

#define GI_HEADER 36U            /* CFHEADER without the optional fields */
#define GI_FOLDER 8U             /* CFFOLDER without its reserve */
#define GI_FILE 16U              /* CFFILE without its name */
#define GI_DATA 8U               /* CFDATA header without its reserve */
#define GI_SMALLEST_CABINET (GI_HEADER + GI_FOLDER + GI_FILE + 2U + GI_DATA + 1U)
#define GI_MAX_HEADER_RESERVE 60000U
#define GI_MAX_NAME 1024U
#define GI_MAX_TEXT 256U         /* previous / next cabinet and disk names */
#define GI_MAX_BLOCK 32768U      /* plain bytes of one CFDATA block */
#define GI_MAX_RECORDS 262144U
#define GI_MAX_FOLDERS 262144U   /* over all segments of one package */
#define GI_DEFAULT_LIMIT ((uint64_t)256U << 20U) /* folder decode buffers */
#define GI_MSZIP_STEP ((size_t)1U << 20U)          /* first MSZIP output buffer */
#define GI_RENAME_PASSES 4U
#define GI_BUFFER 4096U
#define GI_NO_FOLDER UINT32_MAX

#define GI_FLAG_PREV 1U
#define GI_FLAG_NEXT 2U
#define GI_FLAG_RESERVE 4U

#define GI_STORE 0U
#define GI_MSZIP 1U
#define GI_QUANTUM 2U
#define GI_LZX 3U

#define GI_PE_MAX_LFANEW 0x10000U
#define GI_PE_MAX_SECTIONS 96U
#define GI_PE_SECTION 40U

static const uint8_t gi_magic[GI_MAGIC_SIZE] = {0xC0U, 0xDEU, 0xCEU, 0xCBU,
                                                0x8DU, 0x8DU, 0x8DU, 0x8DU};
static const uint8_t gi_end_marker[6] = {'G', 'I', 'P', 'E', 'N', 'D'};

typedef struct gi_folder_s {
    int64_t data_offset;   /* absolute offset of the first CFDATA */
    uint64_t plain_size;   /* sum of cbUncomp */
    uint64_t packed_size;  /* sum of cbData */
    uint32_t blocks;
    uint16_t type;
    uint8_t data_reserve;
} gi_folder;

typedef struct gi_member_s {
    char *name;            /* UTF-8, '/'-separated, after renaming */
    char *original;        /* name before the first rename, else NULL */
    uint32_t size;
    uint32_t folder_offset;
    uint32_t folder;       /* index into gi_package.folders, or GI_NO_FOLDER */
    uint16_t date;
    uint16_t time;
    uint16_t attrs;
    bool extractable;
} gi_member;

/* Sort order of two members: folded name, original names before renamed
 * ones, then record order. */
static int gi_member_order(const gi_member *members, size_t left, size_t right);

typedef struct gi_package_s {
    gi_folder *folders;
    size_t folder_count;
    size_t folder_capacity;
    gi_member *members;
    size_t member_count;
    size_t member_capacity;
    uint64_t record_count;
    uint32_t folder_total;
    int64_t payload_offset;
    int64_t payload_end;
    uint32_t segment_count;
    uint32_t first_trailer;
    bool is_sfx;
    /* reading session */
    size_t index;
    size_t cached_folder;
    uint8_t *cache;
    size_t cache_size;
} gi_package;

typedef struct gi_reader_s {
    xx_io_device *device;
    int64_t total;
    int64_t start;
    size_t length;
    uint8_t buffer[GI_BUFFER];
} gi_reader;

/* ------------------------------------------------------------ reading -- */

static uint16_t gi_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t gi_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static bool gi_raw_read(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || offset < 0 || (!buffer && size != 0U) ||
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

static bool gi_reader_open(gi_reader *reader, xx_io_device *device) {
    reader->device = device;
    reader->total = device ? xx_io_total_size(device) : -1;
    reader->start = 0;
    reader->length = 0U;
    return reader->total > 0;
}

/** Clear bytes at @p offset, through a 4 KiB window for small requests. */
static bool gi_get(gi_reader *reader, int64_t offset, void *out, size_t size) {
    if (offset < 0 || offset > reader->total ||
        (uint64_t)size > (uint64_t)(reader->total - offset))
        return false;
    if (size > GI_BUFFER) return gi_raw_read(reader->device, offset, out, size);
    if (reader->length == 0U || offset < reader->start ||
        (uint64_t)(offset - reader->start) + size > reader->length) {
        size_t want = GI_BUFFER;
        if ((uint64_t)want > (uint64_t)(reader->total - offset))
            want = (size_t)(reader->total - offset);
        if (!gi_raw_read(reader->device, offset, reader->buffer, want)) {
            reader->length = 0U;
            return false;
        }
        reader->start = offset;
        reader->length = want;
    }
    xx_mem_copy(out, reader->buffer + (size_t)(offset - reader->start), size);
    return true;
}

static void gi_unmask(uint8_t *data, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index) data[index] ^= (uint8_t)GI_KEY;
}

/** Cabinet bytes at @p offset, unmasked. */
static bool gi_get_cabinet(gi_reader *reader, int64_t offset, uint8_t *out,
                           size_t size) {
    if (!gi_get(reader, offset, out, size)) return false;
    gi_unmask(out, size);
    return true;
}

static bool gi_has_magic(gi_reader *reader, int64_t offset) {
    uint8_t head[GI_MAGIC_SIZE];
    return offset >= 0 && offset <= reader->total &&
           reader->total - offset >= (int64_t)GI_SMALLEST_CABINET &&
           gi_get(reader, offset, head, sizeof(head)) &&
           xx_rt_memcmp(head, gi_magic, sizeof(head)) == 0;
}

/* ------------------------------------------------------ payload locator -- */

/**
 * End of the PE image at @p base: the largest end of a section's raw data,
 * or SizeOfHeaders when that is larger.  Only the DOS header, the PE
 * header, SizeOfHeaders and the section table are read.
 */
static bool gi_pe_overlay(gi_reader *reader, int64_t base, int64_t *overlay) {
    uint8_t dos[64];
    uint8_t pe[24];
    uint8_t section[GI_PE_SECTION];
    uint32_t lfanew;
    uint32_t sections;
    uint32_t optional_size;
    uint32_t index;
    uint64_t end = 0U;
    int64_t at;

    if (reader->total - base < (int64_t)sizeof(dos) ||
        !gi_get(reader, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    lfanew = gi_u32(dos + 0x3CU);
    if (lfanew < 4U || lfanew > GI_PE_MAX_LFANEW ||
        (int64_t)lfanew > reader->total - base - (int64_t)sizeof(pe))
        return false;
    if (!gi_get(reader, base + lfanew, pe, sizeof(pe)) || pe[0] != 'P' ||
        pe[1] != 'E' || pe[2] != 0U || pe[3] != 0U)
        return false;
    sections = gi_u16(pe + 6U);
    optional_size = gi_u16(pe + 20U);
    if (sections == 0U || sections > GI_PE_MAX_SECTIONS) return false;
    at = base + (int64_t)lfanew + (int64_t)sizeof(pe);
    if (optional_size >= 64U) {
        uint8_t headers[4];
        if (!gi_get(reader, at + 60, headers, sizeof(headers))) return false;
        end = gi_u32(headers);
    }
    at += (int64_t)optional_size;
    if (at > reader->total ||
        (int64_t)sections * (int64_t)GI_PE_SECTION > reader->total - at)
        return false;
    for (index = 0U; index < sections; ++index) {
        uint32_t raw_size;
        uint32_t raw_pointer;
        if (!gi_get(reader, at + (int64_t)index * (int64_t)GI_PE_SECTION,
                    section, sizeof(section)))
            return false;
        raw_size = gi_u32(section + 16U);
        raw_pointer = gi_u32(section + 20U);
        if (raw_size != 0U && raw_pointer != 0U &&
            (uint64_t)raw_pointer + raw_size > end)
            end = (uint64_t)raw_pointer + raw_size;
    }
    if (end == 0U || end > (uint64_t)(reader->total - base)) return false;
    *overlay = base + (int64_t)end;
    return true;
}

/**
 * The package starts at @p base (.gip), at the PE overlay of an executable
 * at @p base, or - for an executable whose overlay does not start with it -
 * where the 34-byte trailer that ends the file says (its +0 field), as
 * long as that lies behind the PE image.  Every candidate must carry the
 * masked cabinet signature; the cabinet itself is checked by the caller.
 */
static bool gi_locate(gi_reader *reader, int64_t base, int64_t *payload,
                      bool *is_sfx) {
    int64_t overlay;
    if (gi_has_magic(reader, base)) {
        *payload = base;
        *is_sfx = false;
        return true;
    }
    if (!gi_pe_overlay(reader, base, &overlay)) return false;
    *is_sfx = true;
    if (gi_has_magic(reader, overlay)) {
        *payload = overlay;
        return true;
    }
    if (reader->total - overlay >=
        (int64_t)(GI_SMALLEST_CABINET + GI_LONG_TRAILER)) {
        uint8_t trailer[GI_LONG_TRAILER];
        int64_t start;
        if (gi_get(reader, reader->total - (int64_t)GI_LONG_TRAILER, trailer,
                   sizeof(trailer)) &&
            xx_rt_memcmp(trailer + 28U, gi_end_marker, 6U) == 0) {
            start = base + (int64_t)gi_u32(trailer);
            if (start >= overlay && gi_has_magic(reader, start)) {
                *payload = start;
                return true;
            }
        }
    }
    return false;
}

/** Size of the trailer at @p offset: 26, 34, or 0 when there is none. */
static uint32_t gi_trailer(gi_reader *reader, int64_t offset) {
    uint8_t trailer[GI_LONG_TRAILER];
    if (offset < 0 || offset > reader->total) return 0U;
    if (reader->total - offset >= (int64_t)GI_SHORT_TRAILER &&
        gi_get(reader, offset, trailer, GI_SHORT_TRAILER) &&
        xx_rt_memcmp(trailer + 20U, gi_end_marker, 6U) == 0)
        return GI_SHORT_TRAILER;
    if (reader->total - offset >= (int64_t)GI_LONG_TRAILER &&
        gi_get(reader, offset, trailer, GI_LONG_TRAILER) &&
        xx_rt_memcmp(trailer + 28U, gi_end_marker, 6U) == 0)
        return GI_LONG_TRAILER;
    return 0U;
}

/* ------------------------------------------------------------- names -- */

static bool gi_utf8_valid(const uint8_t *text, size_t length) {
    size_t index = 0U;
    while (index < length) {
        uint8_t c = text[index];
        size_t extra;
        uint32_t value;
        size_t k;
        if (c < 0x80U) {
            ++index;
            continue;
        }
        if (c >= 0xC2U && c <= 0xDFU) {
            extra = 1U;
            value = c & 0x1FU;
        } else if (c >= 0xE0U && c <= 0xEFU) {
            extra = 2U;
            value = c & 0x0FU;
        } else if (c >= 0xF0U && c <= 0xF4U) {
            extra = 3U;
            value = c & 0x07U;
        } else {
            return false;
        }
        if (extra > length - index - 1U) return false;
        for (k = 1U; k <= extra; ++k) {
            uint8_t next = text[index + k];
            if ((next & 0xC0U) != 0x80U) return false;
            value = (value << 6U) | (next & 0x3FU);
        }
        if ((extra == 2U && (value < 0x800U || (value >= 0xD800U &&
                                                value <= 0xDFFFU))) ||
            (extra == 3U && (value < 0x10000U || value > 0x10FFFFU)))
            return false;
        index += extra + 1U;
    }
    return true;
}

/** '\' becomes '/'; a name that is not UTF-8 is read as Latin-1. */
static char *gi_make_name(const uint8_t *raw, size_t length) {
    bool utf8 = gi_utf8_valid(raw, length);
    size_t index;
    size_t at = 0U;
    char *name = (char *)xx_mem_alloc(length * 2U + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c == (uint8_t)'\\') c = (uint8_t)'/';
        if (utf8 || c < 0x80U) {
            name[at++] = (char)c;
        } else {
            name[at++] = (char)(0xC0U | (c >> 6U));
            name[at++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    name[at] = 0;
    return name;
}

static uint8_t gi_fold(uint8_t c) {
    return (c >= (uint8_t)'a' && c <= (uint8_t)'z') ? (uint8_t)(c - 0x20U) : c;
}

static bool gi_is_word(const uint8_t *text, size_t length, const char *word) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (word[index] == '\0' || gi_fold(text[index]) != (uint8_t)word[index])
            return false;
    }
    return word[length] == '\0';
}

/* A component Windows resolves to a device, with or without extension. */
static bool gi_is_device(const uint8_t *component, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U;
    size_t index;
    while (stem < length && component[stem] != (uint8_t)'.') ++stem;
    while (stem > 0U && component[stem - 1U] == (uint8_t)' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (gi_is_word(component, stem, devices[index])) return true;
    if (stem >= 4U &&
        (gi_is_word(component, 3U, "COM") || gi_is_word(component, 3U, "LPT"))) {
        if (stem == 4U && component[3] >= (uint8_t)'0' &&
            component[3] <= (uint8_t)'9')
            return true;
        /* Superscript one, two and three (U+00B9, U+00B2, U+00B3). */
        if (stem == 5U && component[3] == 0xC2U &&
            (component[4] == 0xB9U || component[4] == 0xB2U ||
             component[4] == 0xB3U))
            return true;
    }
    return false;
}

/* Refused: empty, absolute, empty / "." / ".." components, a component
 * ending in '.' or ' ', control characters, the characters Windows reserves
 * (':' covers drive letters and streams) and device names. */
static bool gi_name_safe(const char *name) {
    const uint8_t *raw = (const uint8_t *)name;
    size_t start = 0U;
    if (!raw || raw[0] == 0U || raw[0] == (uint8_t)'/') return false;
    for (;;) {
        size_t end = start;
        size_t index;
        while (raw[end] != 0U && raw[end] != (uint8_t)'/') ++end;
        if (end == start) return false;
        if (raw[end - 1U] == (uint8_t)'.' || raw[end - 1U] == (uint8_t)' ')
            return false;
        for (index = start; index < end; ++index) {
            uint8_t c = raw[index];
            if (c < 0x20U || c == 0x7FU || c == (uint8_t)':' ||
                c == (uint8_t)'<' || c == (uint8_t)'>' || c == (uint8_t)'"' ||
                c == (uint8_t)'|' || c == (uint8_t)'?' || c == (uint8_t)'*' ||
                c == (uint8_t)'\\')
                return false;
        }
        if (gi_is_device(raw + start, end - start)) return false;
        if (raw[end] == 0U) return true;
        start = end + 1U;
    }
}

static int gi_name_compare(const char *left, const char *right) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (;;) {
        uint8_t x = gi_fold(*a++);
        uint8_t y = gi_fold(*b++);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}

static int gi_member_order(const gi_member *members, size_t left,
                           size_t right) {
    int names = gi_name_compare(members[left].name, members[right].name);
    bool left_renamed = members[left].original != NULL;
    bool right_renamed = members[right].original != NULL;
    if (names != 0) return names;
    if (left_renamed != right_renamed) return left_renamed ? 1 : -1;
    return left < right ? -1 : (left > right ? 1 : 0);
}

/* Shell sort of member indices by gi_member_order. */
static void gi_sort(size_t *order, size_t count, const gi_member *members) {
    size_t gap = 1U;
    size_t index;
    for (index = 0U; index < count; ++index) order[index] = index;
    while (gap < count / 3U) gap = gap * 3U + 1U;
    for (; gap > 0U; gap /= 3U) {
        for (index = gap; index < count; ++index) {
            size_t value = order[index];
            size_t slot = index;
            while (slot >= gap) {
                size_t other = order[slot - gap];
                if (gi_member_order(members, value, other) > 0) break;
                order[slot] = other;
                slot -= gap;
            }
            order[slot] = value;
        }
    }
}

static size_t gi_put_decimal(char *out, uint64_t value) {
    char digits[24];
    size_t count = 0U;
    size_t index;
    do {
        digits[count++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (index = 0U; index < count; ++index)
        out[index] = digits[count - 1U - index];
    return count;
}

/* "<stem>_<number>[_<pass>]<extension>" in the last path component. */
static char *gi_renamed(const char *name, size_t number, unsigned pass) {
    size_t length = xx_str_len(name);
    size_t component = 0U;
    size_t insert = length;
    size_t index;
    size_t at;
    char suffix[56];
    size_t suffix_length = 0U;
    char *out;
    for (index = 0U; index < length; ++index)
        if (name[index] == '/') component = index + 1U;
    for (index = length; index > component + 1U; --index) {
        if (name[index - 1U] == '.') {
            insert = index - 1U;
            break;
        }
    }
    suffix[suffix_length++] = '_';
    suffix_length += gi_put_decimal(suffix + suffix_length, (uint64_t)number);
    if (pass != 0U) {
        suffix[suffix_length++] = '_';
        suffix_length += gi_put_decimal(suffix + suffix_length, pass);
    }
    out = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!out) return NULL;
    at = 0U;
    xx_mem_copy(out, name, insert);
    at += insert;
    xx_mem_copy(out + at, suffix, suffix_length);
    at += suffix_length;
    xx_mem_copy(out + at, name + insert, length - insert);
    at += length - insert;
    out[at] = 0;
    return out;
}

/**
 * Names compared ASCII case-insensitively are made unique: every later
 * duplicate gets "_<record number>" before its extension, always derived
 * from its original name, and a name the archive itself carries is never
 * the one renamed in favour of a renamed one.  A name that is still shared
 * after a few passes is listed but never extracted.
 */
static bool gi_dedupe(gi_package *package) {
    size_t count = package->member_count;
    size_t *order;
    uint8_t *victims;
    unsigned pass;
    size_t index;
    bool result = false;
    if (count < 2U) return true;
    order = (size_t *)xx_mem_calloc(count, sizeof(*order));
    victims = (uint8_t *)xx_mem_calloc(count, 1U);
    if (!order || !victims) goto done;
    for (pass = 0U; pass <= GI_RENAME_PASSES; ++pass) {
        bool changed = false;
        /* Decide every victim against the names as they stand, then
         * rename, so a fresh name is only judged in the next pass. */
        gi_sort(order, count, package->members);
        xx_mem_zero(victims, count);
        for (index = 1U; index < count; ++index) {
            if (gi_name_compare(package->members[order[index - 1U]].name,
                                package->members[order[index]].name) == 0)
                victims[order[index]] = 1U;
        }
        for (index = 0U; index < count; ++index) {
            gi_member *victim = &package->members[index];
            const char *source;
            char *fresh;
            if (!victims[index]) continue;
            if (pass == GI_RENAME_PASSES) {
                victim->extractable = false;
                continue;
            }
            source = victim->original ? victim->original : victim->name;
            fresh = gi_renamed(source, index + 1U, pass);
            if (!fresh) goto done;
            if (victim->original)
                xx_mem_free(victim->name);
            else
                victim->original = victim->name;
            victim->name = fresh;
            changed = true;
        }
        if (!changed) break;
    }
    result = true;
done:
    if (order) xx_mem_free(order);
    if (victims) xx_mem_free(victims);
    return result;
}

/* ----------------------------------------------------------- cabinets -- */

static bool gi_type_known(uint16_t type) {
    uint32_t method = type & 0x0FU;
    uint32_t window = ((uint32_t)type >> 8U) & 0x1FU;
    uint32_t level = ((uint32_t)type >> 4U) & 0x0FU;
    switch (method) {
    case GI_STORE:
    case GI_MSZIP:
        return (type & 0xFFF0U) == 0U;
    case GI_QUANTUM:
        return (type & 0xE000U) == 0U && level >= 1U && level <= 7U &&
               window >= 10U && window <= 21U;
    case GI_LZX:
        return (type & 0xE0F0U) == 0U && window >= 15U && window <= 21U;
    default:
        return false;
    }
}

/** Skip one NUL-terminated string of 1..GI_MAX_TEXT bytes. */
static bool gi_skip_text(gi_reader *reader, int64_t end, int64_t *at) {
    size_t length = 0U;
    uint8_t c;
    for (;;) {
        if (*at + (int64_t)length >= end ||
            !gi_get_cabinet(reader, *at + (int64_t)length, &c, 1U))
            return false;
        if (c == 0U) break;
        if (++length > GI_MAX_TEXT) return false;
    }
    *at += (int64_t)length + 1;
    return true;
}

static void gi_member_release(gi_member *member) {
    if (member->name) xx_mem_free(member->name);
    if (member->original) xx_mem_free(member->original);
    member->name = NULL;
    member->original = NULL;
}

static bool gi_grow(void **items, size_t *capacity, size_t needed,
                    size_t item_size) {
    size_t fresh;
    void *grown;
    if (needed <= *capacity) return true;
    fresh = *capacity ? *capacity : 16U;
    while (fresh < needed) {
        if (fresh > SIZE_MAX / 2U) return false;
        fresh *= 2U;
    }
    if (fresh > SIZE_MAX / item_size) return false;
    grown = xx_mem_realloc(*items, fresh * item_size);
    if (!grown) return false;
    *items = grown;
    *capacity = fresh;
    return true;
}

/**
 * Validate the cabinet at @p at and, with @p package, append its folders
 * and members.  On failure nothing is left appended.
 */
static bool gi_parse_cabinet(gi_reader *reader, int64_t at,
                             gi_package *package, uint32_t *folder_total,
                             int64_t *cabinet_end, uint32_t *file_count) {
    uint8_t header[GI_HEADER];
    uint32_t cabinet;
    uint32_t files_at;
    uint32_t folders;
    uint32_t files;
    uint32_t flags;
    uint32_t folder_reserve = 0U;
    uint32_t data_reserve = 0U;
    uint64_t budget;
    int64_t end;
    int64_t position;
    size_t first_folder = package ? package->folder_count : 0U;
    size_t first_member = package ? package->member_count : 0U;
    uint32_t index;
    uint8_t name[GI_MAX_NAME + 1U];

    if (!gi_get_cabinet(reader, at, header, sizeof(header)) ||
        header[0] != 'M' || header[1] != 'S' || header[2] != 'C' ||
        header[3] != 'F' || gi_u32(header + 4U) != 0U)
        return false;
    cabinet = gi_u32(header + 8U);
    files_at = gi_u32(header + 16U);
    folders = gi_u16(header + 26U);
    files = gi_u16(header + 28U);
    flags = gi_u16(header + 30U);
    if (cabinet < GI_SMALLEST_CABINET ||
        (int64_t)cabinet > reader->total - at || folders == 0U ||
        files == 0U || (flags & ~(GI_FLAG_PREV | GI_FLAG_NEXT | GI_FLAG_RESERVE)) != 0U ||
        files_at >= cabinet)
        return false;
    end = at + (int64_t)cabinet;
    position = at + (int64_t)GI_HEADER;
    if ((flags & GI_FLAG_RESERVE) != 0U) {
        uint8_t reserve[4];
        uint32_t header_reserve;
        if (!gi_get_cabinet(reader, position, reserve, sizeof(reserve)))
            return false;
        header_reserve = gi_u16(reserve);
        folder_reserve = reserve[2];
        data_reserve = reserve[3];
        position += 4;
        if (header_reserve > GI_MAX_HEADER_RESERVE ||
            (int64_t)header_reserve > end - position)
            return false;
        position += (int64_t)header_reserve;
    }
    if ((flags & GI_FLAG_PREV) != 0U &&
        (!gi_skip_text(reader, end, &position) ||
         !gi_skip_text(reader, end, &position)))
        return false;
    if ((flags & GI_FLAG_NEXT) != 0U &&
        (!gi_skip_text(reader, end, &position) ||
         !gi_skip_text(reader, end, &position)))
        return false;

    if (folders > GI_MAX_FOLDERS - *folder_total ||
        (package &&
         !gi_grow((void **)&package->folders, &package->folder_capacity,
                  package->folder_count + folders, sizeof(gi_folder))))
        return false;
    /* No two blocks of a well-formed cabinet overlap and each takes at least
     * nine bytes, so this bounds the walk whatever the folders claim. */
    budget = (uint64_t)cabinet / (GI_DATA + 1U) + 1U;
    for (index = 0U; index < folders; ++index) {
        uint8_t entry[GI_FOLDER];
        gi_folder folder;
        uint32_t block;
        int64_t cursor;
        if (position > end ||
            (int64_t)(GI_FOLDER + folder_reserve) > end - position ||
            !gi_get_cabinet(reader, position, entry, sizeof(entry)))
            goto fail;
        position += (int64_t)(GI_FOLDER + folder_reserve);
        xx_mem_zero(&folder, sizeof(folder));
        folder.data_offset = at + (int64_t)gi_u32(entry);
        folder.blocks = gi_u16(entry + 4U);
        folder.type = gi_u16(entry + 6U);
        folder.data_reserve = (uint8_t)data_reserve;
        if (gi_u32(entry) < GI_HEADER || gi_u32(entry) >= cabinet ||
            folder.blocks == 0U || !gi_type_known(folder.type))
            goto fail;
        cursor = folder.data_offset;
        for (block = 0U; block < folder.blocks; ++block) {
            uint8_t data[GI_DATA];
            uint32_t packed;
            uint32_t plain;
            if (budget == 0U) goto fail;
            --budget;
            if ((int64_t)(GI_DATA + data_reserve) > end - cursor ||
                !gi_get_cabinet(reader, cursor, data, sizeof(data)))
                goto fail;
            packed = gi_u16(data + 4U);
            plain = gi_u16(data + 6U);
            if (packed == 0U || plain == 0U || plain > GI_MAX_BLOCK ||
                ((folder.type & 0x0FU) == GI_STORE && packed != plain))
                goto fail;
            cursor += (int64_t)(GI_DATA + data_reserve);
            if ((int64_t)packed > end - cursor) goto fail;
            cursor += (int64_t)packed;
            folder.plain_size += plain;
            folder.packed_size += packed;
        }
        if (package) package->folders[package->folder_count++] = folder;
    }

    /* The file table follows the folder table. */
    if ((int64_t)files_at < position - at) goto fail;
    position = at + (int64_t)files_at;
    if (package &&
        !gi_grow((void **)&package->members, &package->member_capacity,
                 package->member_count + files, sizeof(gi_member)))
        goto fail;
    for (index = 0U; index < files; ++index) {
        uint8_t entry[GI_FILE];
        size_t length = 0U;
        uint32_t folder;
        if ((int64_t)GI_FILE > end - position ||
            !gi_get_cabinet(reader, position, entry, sizeof(entry)))
            goto fail;
        position += (int64_t)GI_FILE;
        for (;;) {
            if (position + (int64_t)length >= end ||
                !gi_get_cabinet(reader, position + (int64_t)length,
                                name + length, 1U))
                goto fail;
            if (name[length] == 0U) break;
            if (name[length] < 0x20U || ++length > GI_MAX_NAME) goto fail;
        }
        if (length == 0U) goto fail;
        position += (int64_t)length + 1;
        folder = gi_u16(entry + 8U);
        if (folder >= folders && folder < 0xFFFDU) goto fail;
        if (package) {
            gi_member member;
            xx_mem_zero(&member, sizeof(member));
            member.size = gi_u32(entry);
            member.folder_offset = gi_u32(entry + 4U);
            member.date = gi_u16(entry + 10U);
            member.time = gi_u16(entry + 12U);
            member.attrs = gi_u16(entry + 14U);
            member.name = gi_make_name(name, length);
            if (!member.name) goto fail;
            if (folder < folders) {
                const gi_folder *owner;
                member.folder = (uint32_t)(first_folder + folder);
                owner = &package->folders[member.folder];
                member.extractable =
                    (uint64_t)member.folder_offset + member.size <=
                    owner->plain_size;
            } else {
                /* Continued from or into another cabinet of a set. */
                member.folder = GI_NO_FOLDER;
                member.extractable = false;
            }
            if (!gi_name_safe(member.name)) member.extractable = false;
            package->members[package->member_count++] = member;
        }
    }
    *cabinet_end = end;
    *file_count = files;
    *folder_total += folders;
    return true;
fail:
    if (package) {
        while (package->member_count > first_member)
            gi_member_release(&package->members[--package->member_count]);
        package->folder_count = first_folder;
    }
    return false;
}

static void gi_package_free(void *opaque) {
    gi_package *package = (gi_package *)opaque;
    size_t index;
    if (!package) return;
    for (index = 0U; index < package->member_count; ++index)
        gi_member_release(&package->members[index]);
    if (package->members) xx_mem_free(package->members);
    if (package->folders) xx_mem_free(package->folders);
    if (package->cache) xx_mem_free(package->cache);
    xx_mem_free(package);
}

/**
 * Locate and walk the package.  @p first_only stops after the first
 * cabinet (the probe); @p collect keeps folders and members.
 */
static bool gi_parse(Abstractformat *format, bool first_only, bool collect,
                     gi_package *out) {
    gi_reader stack_reader;
    gi_reader *reader = &stack_reader;
    gi_package *target = collect ? out : NULL;
    int64_t at;
    uint32_t segment;
    bool result = false;

    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    if (!gi_reader_open(reader, format->device) ||
        format->base_address >= reader->total ||
        !gi_locate(reader, format->base_address, &out->payload_offset,
                   &out->is_sfx))
        goto done;
    at = out->payload_offset;
    for (segment = 0U; segment < GI_MAX_SEGMENTS; ++segment) {
        int64_t cabinet_end = 0;
        uint32_t files = 0U;
        uint32_t trailer;
        size_t folders_before = target ? target->folder_count : 0U;
        if (segment != 0U && !gi_has_magic(reader, at)) break;
        if (!gi_parse_cabinet(reader, at, target, &out->folder_total,
                              &cabinet_end, &files)) {
            if (segment == 0U) goto done;
            break;
        }
        if (out->record_count + files > GI_MAX_RECORDS) {
            if (segment == 0U) goto done;
            if (target) {
                /* Drop the cabinet that does not fit. */
                while (target->member_count > out->record_count)
                    gi_member_release(&target->members[--target->member_count]);
                target->folder_count = folders_before;
            }
            break;
        }
        out->record_count += files;
        ++out->segment_count;
        trailer = gi_trailer(reader, cabinet_end);
        if (segment == 0U) out->first_trailer = trailer;
        at = cabinet_end + (int64_t)trailer;
        out->payload_end = at;
        if (first_only || trailer == 0U || at >= reader->total) break;
    }
    result = out->segment_count != 0U;
done:
    return result;
}

/* ------------------------------------------------------------ folders -- */

/* [MS-CAB] 2.1.4: XOR of the little-endian 32-bit words, the 1..3 bytes
 * left over folded in as a big-endian-ordered tail. */
static uint32_t gi_checksum(const uint8_t *data, size_t size, uint32_t seed) {
    uint32_t sum = seed;
    size_t words = size / 4U;
    size_t index;
    uint32_t tail = 0U;
    for (index = 0U; index < words; ++index) sum ^= gi_u32(data + index * 4U);
    data += words * 4U;
    switch (size & 3U) {
    case 3U:
        tail = ((uint32_t)data[0] << 16U) | ((uint32_t)data[1] << 8U) | data[2];
        break;
    case 2U:
        tail = ((uint32_t)data[0] << 8U) | data[1];
        break;
    case 1U:
        tail = data[0];
        break;
    default:
        break;
    }
    return sum ^ tail;
}

/**
 * One MSZIP block: "CK" and a Deflate stream that may refer back into the
 * previous 32 KiB of the folder.  That history is supplied by decoding it
 * as a non-final stored block placed in front of the block's own stream.
 */
static bool gi_mszip_block(const uint8_t *packed, size_t packed_size,
                           const uint8_t *history, size_t history_size,
                           uint8_t *scratch_in, uint8_t *scratch_out,
                           uint8_t *out, size_t plain) {
    size_t prefix = 0U;
    size_t wrote = 0U;
    if (packed_size < 3U || packed[0] != 'C' || packed[1] != 'K') return false;
    if (history_size > 32768U) {
        history += history_size - 32768U;
        history_size = 32768U;
    }
    if (history_size != 0U) {
        scratch_in[0] = 0U; /* BFINAL 0, BTYPE 00 */
        scratch_in[1] = (uint8_t)history_size;
        scratch_in[2] = (uint8_t)(history_size >> 8U);
        scratch_in[3] = (uint8_t)~history_size;
        scratch_in[4] = (uint8_t)(~history_size >> 8U);
        xx_mem_copy(scratch_in + 5U, history, history_size);
        prefix = 5U + history_size;
    }
    xx_mem_copy(scratch_in + prefix, packed + 2U, packed_size - 2U);
    if (!xx_deflate_decompress_memory(scratch_in, prefix + packed_size - 2U,
                                      scratch_out, history_size + plain,
                                      &wrote, false) ||
        wrote != history_size + plain)
        return false;
    xx_mem_copy(out, scratch_out + history_size, plain);
    return true;
}

/**
 * Bytes one folder may take in memory (its packed and its plain bytes).
 * XX_META_ID_OPT_MEMORY_LIMIT, when given, replaces the built-in 256 MiB so
 * that a caller can allow a larger folder; a negative value allows nothing.
 */
static uint64_t gi_memory_limit(Abstractformat *format,
                                const xx_list_s *options) {
    const xx_var *option = xx_format_resolve_extra_parameter(
        format, options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (!option) return GI_DEFAULT_LIMIT;
    switch ((xx_var_type_t)option->type) {
    case XX_VAR_TYPE_INT8:
    case XX_VAR_TYPE_INT16:
    case XX_VAR_TYPE_INT32:
    case XX_VAR_TYPE_INT64:
        return xx_var_get_i64(option) < 0 ? 0U
                                          : (uint64_t)xx_var_get_i64(option);
    default:
        return xx_var_get_u64(option);
    }
}

/** Decode folder @p index of @p package into its cache. */
static bool gi_decode_folder(Abstractformat *format, gi_package *package,
                             size_t index, uint64_t limit, xx_pd_struct *pd) {
    const gi_folder *folder = &package->folders[index];
    uint32_t method = folder->type & 0x0FU;
    unsigned window = ((unsigned)folder->type >> 8U) & 0x1FU;
    uint8_t *output = NULL;
    uint8_t *packed = NULL;
    const uint8_t **blocks = NULL;
    size_t *packed_sizes = NULL;
    size_t *plain_sizes = NULL;
    uint8_t *scratch_in = NULL;
    uint8_t *scratch_out = NULL;
    size_t packed_used = 0U;
    size_t plain_used = 0U;
    size_t output_capacity;
    int64_t cursor = folder->data_offset;
    uint32_t block;
    bool ok = false;

    if (package->cache && package->cached_folder == index) return true;
    if (package->cache) {
        xx_mem_free(package->cache);
        package->cache = NULL;
        package->cache_size = 0U;
    }
    if (folder->plain_size == 0U || folder->plain_size > limit ||
        folder->packed_size > limit - folder->plain_size ||
        folder->plain_size > SIZE_MAX || folder->packed_size > SIZE_MAX)
        return false;
    /* MSZIP grows its output block by block, so a folder that claims far
     * more than its data holds fails before the whole buffer exists; LZX
     * and Quantum decode the folder in one call into its full size. */
    output_capacity = (size_t)folder->plain_size;
    if (method == GI_MSZIP && output_capacity > GI_MSZIP_STEP)
        output_capacity = GI_MSZIP_STEP;
    output = (uint8_t *)xx_mem_alloc(output_capacity);
    packed = (uint8_t *)xx_mem_alloc((size_t)folder->packed_size);
    blocks = (const uint8_t **)xx_mem_calloc(folder->blocks, sizeof(*blocks));
    packed_sizes = (size_t *)xx_mem_calloc(folder->blocks, sizeof(*packed_sizes));
    plain_sizes = (size_t *)xx_mem_calloc(folder->blocks, sizeof(*plain_sizes));
    if (!output || !packed || !blocks || !packed_sizes || !plain_sizes)
        goto done;

    for (block = 0U; block < folder->blocks; ++block) {
        uint8_t header[GI_DATA];
        uint32_t stored_sum;
        size_t size;
        size_t plain;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!gi_raw_read(format->device, cursor, header, sizeof(header)))
            goto done;
        gi_unmask(header, sizeof(header));
        stored_sum = gi_u32(header);
        size = gi_u16(header + 4U);
        plain = gi_u16(header + 6U);
        if (size == 0U || plain == 0U || plain > GI_MAX_BLOCK ||
            size > (size_t)folder->packed_size - packed_used ||
            plain > (size_t)folder->plain_size - plain_used)
            goto done;
        cursor += (int64_t)(GI_DATA + folder->data_reserve);
        if (!gi_raw_read(format->device, cursor, packed + packed_used, size))
            goto done;
        gi_unmask(packed + packed_used, size);
        cursor += (int64_t)size;
        if (stored_sum != 0U && folder->data_reserve == 0U &&
            gi_checksum(header + 4U, 4U,
                        gi_checksum(packed + packed_used, size, 0U)) !=
                stored_sum)
            goto done;
        blocks[block] = packed + packed_used;
        packed_sizes[block] = size;
        plain_sizes[block] = plain;
        packed_used += size;
        plain_used += plain;
    }
    if (plain_used != (size_t)folder->plain_size) goto done;

    switch (method) {
    case GI_STORE:
        plain_used = 0U;
        for (block = 0U; block < folder->blocks; ++block) {
            if (packed_sizes[block] != plain_sizes[block]) goto done;
            xx_mem_copy(output + plain_used, blocks[block], plain_sizes[block]);
            plain_used += plain_sizes[block];
        }
        ok = true;
        break;
    case GI_MSZIP:
        scratch_in = (uint8_t *)xx_mem_alloc(5U + 32768U + 65536U);
        scratch_out = (uint8_t *)xx_mem_alloc(32768U + GI_MAX_BLOCK);
        if (!scratch_in || !scratch_out) goto done;
        plain_used = 0U;
        for (block = 0U; block < folder->blocks; ++block) {
            if (pd && xx_pd_is_stopped(pd)) goto done;
            if (plain_sizes[block] > output_capacity - plain_used) {
                size_t whole = (size_t)folder->plain_size;
                size_t grown_capacity = output_capacity;
                uint8_t *grown;
                /* Doubling, capped at the folder size, which the block
                 * walk above showed is where plain_used ends. */
                while (plain_sizes[block] > grown_capacity - plain_used)
                    grown_capacity = grown_capacity > whole / 2U
                                         ? whole
                                         : grown_capacity * 2U;
                grown = (uint8_t *)xx_mem_realloc(output, grown_capacity);
                if (!grown) goto done;
                output = grown;
                output_capacity = grown_capacity;
            }
            if (!gi_mszip_block(blocks[block], packed_sizes[block], output,
                                plain_used, scratch_in, scratch_out,
                                output + plain_used, plain_sizes[block]))
                goto done;
            plain_used += plain_sizes[block];
        }
        ok = true;
        break;
    case GI_LZX: {
        size_t wrote = 0U;
        ok = xx_lzx_cab_decode(blocks, packed_sizes, plain_sizes,
                               folder->blocks, window, output,
                               (size_t)folder->plain_size, &wrote) &&
             wrote == (size_t)folder->plain_size;
        break;
    }
    case GI_QUANTUM: {
        size_t wrote = 0U;
        ok = xx_quantum_cab_decode(blocks, packed_sizes, plain_sizes,
                                   folder->blocks, window, output,
                                   (size_t)folder->plain_size, &wrote) &&
             wrote == (size_t)folder->plain_size;
        break;
    }
    default:
        break;
    }
    if (ok) {
        package->cache = output;
        package->cache_size = (size_t)folder->plain_size;
        package->cached_folder = index;
        output = NULL;
    }
done:
    if (output) xx_mem_free(output);
    if (packed) xx_mem_free(packed);
    if (blocks) xx_mem_free((void *)blocks);
    if (packed_sizes) xx_mem_free(packed_sizes);
    if (plain_sizes) xx_mem_free(plain_sizes);
    if (scratch_in) xx_mem_free(scratch_in);
    if (scratch_out) xx_mem_free(scratch_out);
    return ok;
}

/* ------------------------------------------------------------ records -- */

static bool gi_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool gi_set_record(xx_archive_record *record, const gi_package *package,
                          size_t index) {
    const gi_member *member = &package->members[index];
    uint32_t method = member->folder == GI_NO_FOLDER
                          ? 0U
                          : package->folders[member->folder].type & 0x0FU;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->data_offset = -1;
    record->compressed_size = -1;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attrs) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_TIMESTAMP,
               ((uint64_t)member->date << 16U) | member->time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool gi_write_all(xx_io_device *output, const uint8_t *data,
                         size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(output, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---------------------------------------------------------- lifecycle -- */

void xx_ghost_installer_init(xx_ghost_installer *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GHOST_INSTALLER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "gip");
    archive->format.check_is_valid = xx_ghost_installer_check_is_valid;
    archive->format.handle_base_info = xx_ghost_installer_handle_base_info;
    archive->format.get_format_size = xx_ghost_installer_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ghost_installer_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ghost_installer_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ghost_installer_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ghost_installer_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ghost_installer_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ghost_installer_free_archive_records_reading;
    archive->payload_offset = -1;
    archive->payload_end = -1;
}

xx_ghost_installer *xx_ghost_installer_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_ghost_installer *archive =
        (xx_ghost_installer *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ghost_installer_init(archive, device, base_address);
    return archive;
}

void xx_ghost_installer_destroy(xx_ghost_installer *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ghost_installer_free(xx_ghost_installer *archive) {
    if (!archive) return;
    xx_ghost_installer_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ghost_installer_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    gi_package package;
    (void)pd;
    xx_mem_zero(&package, sizeof(package));
    return gi_parse(self, true, false, &package);
}

bool xx_ghost_installer_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd) {
    gi_package package;
    xx_ghost_installer *archive = (xx_ghost_installer *)self;
    (void)pd;
    if (!self) return false;
    xx_mem_zero(&package, sizeof(package));
    if (!gi_parse(self, false, false, &package)) return false;
    archive->number_of_records = package.record_count;
    archive->payload_offset = package.payload_offset;
    archive->payload_end = package.payload_end;
    archive->segment_count = package.segment_count;
    archive->first_trailer = package.first_trailer;
    archive->is_sfx = package.is_sfx;
    if (package.is_sfx) xx_format_set_extension(self, "exe");
    self->number_of_archive_records = package.record_count;
    self->format_size = package.payload_end - self->base_address;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_ghost_installer_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_ghost_installer_handle_base_info(self, pd))
               ? self->format_size
               : -1;
}

uint64_t xx_ghost_installer_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_ghost_installer_handle_base_info(self, pd))
               ? ((xx_ghost_installer *)self)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_ghost_installer_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    gi_package *package;
    xx_archive_record_state *state;
    (void)pd;
    if (!self) return NULL;
    package = (gi_package *)xx_mem_calloc(1U, sizeof(*package));
    if (!package) return NULL;
    if (!gi_parse(self, false, true, package) || package->member_count == 0U ||
        !gi_dedupe(package)) {
        gi_package_free(package);
        return NULL;
    }
    package->cached_folder = SIZE_MAX;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        gi_package_free(package);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = package;
    state->free_internal = gi_package_free;
    state->total_records = (int64_t)package->member_count;
    if (!gi_copy_options(&state->options, options) ||
        !gi_set_record(&state->current_record, package, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ghost_installer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ghost_installer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    gi_package *package;
    (void)pd;
    if (!self || !state || state->format != self ||
        !(package = (gi_package *)state->internal_state) ||
        package->index + 1U >= package->member_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++package->index;
    ++state->current_index;
    state->has_record = gi_set_record(&state->current_record, package,
                                      package->index);
    return state->has_record;
}

bool xx_ghost_installer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    gi_package *package;
    const gi_member *member;
    const xx_var *option;
    const char *base_path = NULL;
    char *converted = NULL;
    char *target = NULL;
    xx_io_device *output;
    size_t base_length;
    bool overwrite;
    bool result;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd)))
        return false;
    package = (gi_package *)state->internal_state;
    if (!package || package->index >= package->member_count) return false;
    member = &package->members[package->index];
    if (member->folder == GI_NO_FOLDER) return false;

    option = xx_format_resolve_extra_parameter(self, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->size > xx_var_get_u64(option)) return false;
    if (!gi_decode_folder(self, package, member->folder,
                          gi_memory_limit(self, &state->options), pd) ||
        (uint64_t)member->folder_offset + member->size > package->cache_size)
        return false;

    option = xx_format_resolve_extra_parameter(self, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    /* No destination: the member decoded, which is all there is to check. */
    if (!option) return true;
    if (!member->extractable || !gi_name_safe(member->name)) return false;
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
        base_path[base_length - 1U] != '\\')
        target = xx_str_concat3(base_path, "/", member->name);
    else
        target = xx_str_concat(base_path, member->name);
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
    result = gi_write_all(output, package->cache + member->folder_offset,
                          member->size);
    if (xx_io_close(output) != 0) result = false;
    if (!result) xx_rt_remove(target);
    xx_str_free(target);
    return result;
}

void xx_ghost_installer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ getters -- */

int64_t xx_ghost_installer_get_payload_offset(
    const xx_ghost_installer *archive) {
    return archive ? archive->payload_offset : -1;
}

uint32_t xx_ghost_installer_get_segment_count(
    const xx_ghost_installer *archive) {
    return archive ? archive->segment_count : 0U;
}

bool xx_ghost_installer_is_sfx(const xx_ghost_installer *archive) {
    return archive ? archive->is_sfx : false;
}
