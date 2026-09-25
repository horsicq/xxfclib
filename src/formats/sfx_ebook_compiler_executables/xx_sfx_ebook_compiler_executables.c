/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * eBook compiler executables: eBook Creator and SBook Builder books, a
 * viewer .exe with the pages appended.  xx_sfx_ebook_compiler_executables.h
 * carries both layouts.
 *
 * Written from the file structure of the known books.  The signature checks
 * match what U3 tests (a header of 43 bytes for eBook Creator, u32 5 and
 * "Sbook"/"Ebook" for SBook Builder); the name table, the chunk chain and
 * the trailers were worked out from the books themselves.  The name
 * handling and the counting sink follow the pattern of this library's
 * sfx_nullsoft_pimp reader (MIT, same author).
 *
 * The executable is parsed only as far as its section table; nothing in it
 * is executed.  Every stream is inflated through a sink that counts, caps
 * and checksums the output, so a file is extracted only when its size and
 * its Adler-32 trailers match.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_ebook_compiler_executables/xx_sfx_ebook_compiler_executables.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* xxfc_defs.h is shared and is not edited from here, so the file-type
 * constant is resolved through the alias macro that the enumerator defines. */
#ifdef SFX_EBOOK_COMPILER_EXECUTABLES
#define XX_SFX_EBOOK_COMPILER_EXECUTABLES_FILE_TYPE \
    XX_FILE_TYPE_SFX_EBOOK_COMPILER_EXECUTABLES
#else
#define XX_SFX_EBOOK_COMPILER_EXECUTABLES_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- limits -------------------------------------------------------------- */

#define EBK_MIN_FILE 0x200
#define EBK_MAX_LFANEW INT64_C(0x10000000)
#define EBK_MAX_SECTIONS 96U
#define EBK_MAX_NAME 0x400U
/* The smallest zlib stream: header, one empty fixed block, Adler-32. */
#define EBK_MIN_PACKED 8U
/* Deflate cannot expand one input byte to more than 1032 output bytes
 * (a 258-byte match coded in two bits); a size beyond that is garbage. */
#define EBK_DEFLATE_RATIO UINT64_C(1032)
#define EBK_DEFLATE_SLACK UINT64_C(258)
#define EBK_METHOD_DEFLATE 8U
#define EBK_RENAME_LIMIT 100000U

#define EBC_HEADER 43U
/* Where the first element's class record starts. */
#define EBC_ELEMENT0_CLASS 24
/* Class reference and id in front of every later element. */
#define EBC_ELEMENT_REF 4U
/* Settings list header and the name table's element header. */
#define EBC_NAMES_HEADER 14U
#define EBC_ENTRY_TAIL 16U
#define EBC_DIR_REF 0x8001U
#define EBC_CLASS_REF 0x8003U
#define EBC_TYPE_NAMES 2U
#define EBC_TRAILER 6
/* The name table of the largest known book is 16 KiB. */
#define EBC_MAX_TABLE 0x800000U

#define SB_HEADER 13U
#define SB_CHUNK 0x4000U
/* A 0x4000-byte chunk coded with nothing but 9-bit literals, plus room for
 * block headers and the zlib wrapper: no real chunk is larger. */
#define SB_MAX_PACKED_CHUNK (SB_CHUNK + SB_CHUNK / 8U + 64U)
#define SB_MAX_FILES 0x10000U
#define SB_TRAILER 4

/* --- small helpers --------------------------------------------------------- */

static uint32_t ebk_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint32_t ebk_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static bool ebk_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool ebk_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}

/* RFC 1950 header: Deflate, a window of at most 32 KiB, no preset
 * dictionary, and the check bits. */
static bool ebk_zlib_header_ok(const uint8_t *header) {
    return (header[0] & 0x0fU) == 8U && (header[0] >> 4U) <= 7U &&
           (header[1] & 0x20U) == 0U &&
           (((uint32_t)header[0] << 8U) | header[1]) % 31U == 0U;
}

/* --- names ----------------------------------------------------------------- */

/* Windows-1252 0x80..0x9F.  The five undefined bytes keep their C1 code
 * point, which the name check refuses. */
static const uint16_t ebk_cp1252_high[32] = {
    0x20ACU, 0x0081U, 0x201AU, 0x0192U, 0x201EU, 0x2026U, 0x2020U, 0x2021U,
    0x02C6U, 0x2030U, 0x0160U, 0x2039U, 0x0152U, 0x008DU, 0x017DU, 0x008FU,
    0x0090U, 0x2018U, 0x2019U, 0x201CU, 0x201DU, 0x2022U, 0x2013U, 0x2014U,
    0x02DCU, 0x2122U, 0x0161U, 0x203AU, 0x0153U, 0x009DU, 0x017EU, 0x0178U};

static size_t ebk_put_utf8(char *out, uint8_t c) {
    uint32_t code = (c >= 0x80U && c < 0xA0U) ? ebk_cp1252_high[c - 0x80U] : c;
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
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

static bool ebk_is_separator(uint8_t c) {
    return c == '\\' || c == '/';
}

/* A stored name to UTF-8 with '/' separators.  A leading drive ("C:") and
 * leading separators are dropped and runs of separators collapse, so an
 * absolute Windows path becomes a relative one.  Whatever else is wrong
 * with the name is left for ebk_safe_output_name() to refuse. */
static char *ebk_name(const uint8_t *bytes, size_t length) {
    char *out = (char *)xx_mem_alloc(length * 3U + 1U);
    size_t index = 0U, used = 0U;
    if (!out) return NULL;
    if (length >= 2U && bytes[1] == ':' &&
        ((bytes[0] >= 'A' && bytes[0] <= 'Z') ||
         (bytes[0] >= 'a' && bytes[0] <= 'z')))
        index = 2U;
    while (index < length && ebk_is_separator(bytes[index])) ++index;
    for (; index < length; ++index) {
        uint8_t c = bytes[index];
        if (ebk_is_separator(c)) {
            if (used == 0U || out[used - 1U] == '/') continue;
            out[used++] = '/';
        } else {
            used += ebk_put_utf8(out + used, c);
        }
    }
    out[used] = 0;
    return out;
}

static char ebk_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool ebk_is_device_stem(const char *name, size_t stem) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t k, i;
    for (k = 0U; k < sizeof(devices) / sizeof(devices[0]); ++k) {
        const char *word = devices[k];
        for (i = 0U; i < stem && word[i]; ++i)
            if (ebk_upper(name[i]) != word[i]) break;
        if (i == stem && word[i] == 0) return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((ebk_upper(name[0]) == 'C' && ebk_upper(name[1]) == 'O' &&
          ebk_upper(name[2]) == 'M') ||
         (ebk_upper(name[0]) == 'L' && ebk_upper(name[1]) == 'P' &&
          ebk_upper(name[2]) == 'T')))
        return true;
    return false;
}

/* Whether a '/'-separated UTF-8 name may become a path below the output
 * directory: no empty, "." or ".." component (so nothing absolute and
 * nothing that climbs out), no drive colon or other character Windows
 * refuses, no C0/C1 control, no component that Windows would silently trim
 * (trailing dot or space), and no device name in any component. */
static bool ebk_safe_output_name(const char *name) {
    size_t start = 0U, index = 0U;
    if (!name || !name[0]) return false;
    for (;;) {
        unsigned char c = (unsigned char)name[index];
        if (c == '/' || c == 0U) {
            size_t length = index - start, stem = 0U;
            if (length == 0U) return false;
            if (name[index - 1U] == '.' || name[index - 1U] == ' ')
                return false;
            while (stem < length && name[start + stem] != '.') ++stem;
            while (stem > 0U && name[start + stem - 1U] == ' ') --stem;
            if (ebk_is_device_stem(name + start, stem)) return false;
            if (c == 0U) break;
            start = index + 1U;
        } else if (c < 0x20U || c == 0x7fU || c == '\\' || c == ':' ||
                   c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                   c == '|') {
            return false;
        } else if (c == 0xC2U && (unsigned char)name[index + 1U] >= 0x80U &&
                   (unsigned char)name[index + 1U] <= 0x9FU) {
            return false; /* U+0080..U+009F */
        }
        ++index;
    }
    return true;
}

/* Case folding as NTFS does it for the characters a Windows-1252 name can
 * produce: ASCII, Latin-1 and the Windows-1252 letter pairs.  Every mapping
 * keeps the UTF-8 length, so names fold in place. */
static void ebk_fold(const char *name, char *out) {
    const unsigned char *in = (const unsigned char *)name;
    unsigned char *folded = (unsigned char *)out;
    size_t index = 0U;
    while (in[index]) {
        unsigned char c = in[index];
        unsigned char d = in[index + 1U];
        if (c == 0xC3U && d != 0U) {
            folded[index] = c;
            folded[index + 1U] = d;
            if (d >= 0xA0U && d <= 0xBEU && d != 0xB7U) {
                folded[index + 1U] = (unsigned char)(d - 0x20U);
            } else if (d == 0xBFU) { /* y-diaeresis to U+0178 */
                folded[index] = 0xC5U;
                folded[index + 1U] = 0xB8U;
            }
            index += 2U;
        } else if (c == 0xC5U && d != 0U) {
            folded[index] = c;
            folded[index + 1U] = (d == 0xA1U || d == 0x93U || d == 0xBEU)
                                     ? (unsigned char)(d - 1U) : d;
            index += 2U;
        } else {
            folded[index] = (c >= 'a' && c <= 'z') ? (unsigned char)(c - 0x20U)
                                                   : c;
            ++index;
        }
    }
    folded[index] = 0U;
}

static uint32_t ebk_hash(const char *folded) {
    uint32_t hash = 2166136261U;
    for (; *folded; ++folded) {
        hash ^= (uint8_t)*folded;
        hash *= 16777619U;
    }
    return hash;
}

/* "<name>_<number>", the number going in front of the last component's
 * extension. */
static char *ebk_with_suffix(const char *name, size_t number) {
    char digits[24];
    size_t count = 0U, length = xx_str_len(name), dot = length, index, used;
    char *out;
    for (index = length; index > 0U; --index) {
        if (name[index - 1U] == '/') break;
        if (name[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    if (dot == 0U || name[dot - 1U] == '/') dot = length;
    do {
        digits[count++] = (char)('0' + number % 10U);
        number /= 10U;
    } while (number != 0U && count < sizeof(digits));
    out = (char *)xx_mem_alloc(length + count + 2U);
    if (!out) return NULL;
    xx_rt_memcpy(out, name, dot);
    used = dot;
    out[used++] = '_';
    while (count != 0U) out[used++] = digits[--count];
    xx_rt_memcpy(out + used, name + dot, length - dot + 1U);
    return out;
}

/* The decimal file index, the name used when a book has no name table. */
static char *ebk_index_name(uint32_t index) {
    char digits[16];
    size_t count = 0U, used = 0U;
    char *out;
    do {
        digits[count++] = (char)('0' + index % 10U);
        index /= 10U;
    } while (index != 0U);
    out = (char *)xx_mem_alloc(count + 1U);
    if (!out) return NULL;
    while (count != 0U) out[used++] = digits[--count];
    out[used] = 0;
    return out;
}

/* --- members --------------------------------------------------------------- */

typedef struct ebk_member_s {
    int64_t header_offset; /**< Relative to the base. */
    int64_t data_offset;   /**< eBook Creator: zlib stream; SBook: 1st chunk. */
    int64_t packed_span;   /**< SBook: all chunks with their size fields. */
    uint32_t header_size;
    uint32_t packed_size;  /**< eBook Creator only. */
    uint32_t raw_size;     /**< eBook Creator only. */
    uint32_t chunks;       /**< SBook only. */
    uint32_t dos_stamp;    /**< SBook only: time in the low half, date high. */
    bool safe;             /**< The name may be used as an output path. */
    char *name;            /**< UTF-8, '/' separators, unique if safe. */
    char *folded;          /**< Case-folded name, only while deduplicating. */
} ebk_member;

typedef struct ebk_info_s {
    uint32_t variant;
    uint32_t count;
    uint32_t header_value;
    uint32_t names_size;
    int64_t payload;     /**< Start of the store. */
    int64_t first;       /**< First member header. */
    int64_t members_end; /**< eBook Creator: after the pack; SBook: marker. */
    int64_t names;       /**< eBook Creator: name table data, or -1. */
    int64_t end;
    bool has_trailer;
    char tag[6];
} ebk_info;

typedef struct ebk_stream_s {
    ebk_member *items;
    size_t count;
    size_t index;
    uint32_t variant;
} ebk_stream;

static void ebk_members_free(ebk_member *items, size_t count) {
    size_t index;
    if (!items) return;
    for (index = 0U; index < count; ++index) {
        if (items[index].name) xx_mem_free(items[index].name);
        if (items[index].folded) xx_mem_free(items[index].folded);
    }
    xx_mem_free(items);
}

static void ebk_stream_free(void *opaque) {
    ebk_stream *stream = (ebk_stream *)opaque;
    if (!stream) return;
    ebk_members_free(stream->items, stream->count);
    xx_mem_free(stream);
}

/* --- the executable ---------------------------------------------------------- */

/* The PE headers must be present.  *overlay receives where the last
 * section's raw data ends, or -1 when a section claims bytes the file does
 * not have; *headers_end the end of the section table. */
static bool ebk_pe(xx_io_device *device, int64_t base, int64_t size,
                   int64_t *overlay, int64_t *headers_end) {
    uint8_t dos[0x40], nt[24], table[EBK_MAX_SECTIONS * 40U];
    uint32_t sections, optional, index;
    int64_t lfanew, table_at, end = 0;
    bool fits = true;
    if (size < EBK_MIN_FILE ||
        !ebk_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    lfanew = (int64_t)ebk_le32(dos + 0x3c);
    if (lfanew < 4 || lfanew > EBK_MAX_LFANEW || lfanew > size - 24 ||
        !ebk_read_at(device, base + lfanew, nt, sizeof(nt)) || nt[0] != 'P' ||
        nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U)
        return false;
    sections = ebk_le16(nt + 6);
    optional = ebk_le16(nt + 20);
    table_at = lfanew + 24 + (int64_t)optional;
    if (sections == 0U || sections > EBK_MAX_SECTIONS ||
        table_at > size - (int64_t)sections * 40 ||
        !ebk_read_at(device, base + table_at, table, (size_t)sections * 40U))
        return false;
    for (index = 0U; index < sections; ++index) {
        const uint8_t *row = table + index * 40U;
        int64_t raw_size = (int64_t)ebk_le32(row + 16);
        int64_t raw_offset = (int64_t)ebk_le32(row + 20);
        if (raw_size == 0) continue;
        if (raw_offset > size || raw_size > size - raw_offset) fits = false;
        else if (raw_offset + raw_size > end) end = raw_offset + raw_size;
    }
    *headers_end = table_at + (int64_t)sections * 40;
    *overlay = (fits && end > *headers_end) ? end : -1;
    return true;
}

/* --- eBook Creator ------------------------------------------------------------ */

static const uint8_t ebc_head_a[20] = {
    0x00, 0x00, 0x02, 0x00, 0xFF, 0xFF, 0x01, 0x00, 0x0A, 0x00,
    'C',  'U',  'p',  'd',  'a',  't',  'e',  'D',  'i',  'r'};
static const uint8_t ebc_head_b[19] = {
    0xFF, 0xFF, 0x01, 0x00, 0x0B, 0x00, 'C', 'U', 'p', 'd',
    'a',  't',  'e',  'E',  'l',  'e',  'm', 0x01, 0x00};

/* Walks the file pack.  The first element's class record and id are part
 * of the 43-byte header; every later element opens with the class reference
 * and its id.  Then u32 block size, u32 unpacked size and a zlib stream of
 * block size - 4 bytes.  Each field is checked against the bytes present
 * before it is used.  With @p items the records are also kept (names come
 * later). */
static bool ebc_walk(xx_io_device *device, int64_t base, int64_t size,
                     int64_t at, uint32_t count, ebk_member *items,
                     int64_t *pack_end, xx_pd_struct *pd) {
    int64_t position = at + EBC_HEADER;
    uint32_t index;
    for (index = 0U; index < count; ++index) {
        uint8_t head[EBC_ELEMENT_REF + 10U];
        const uint8_t *fields = head;
        int64_t header = at + EBC_ELEMENT0_CLASS, sizes_at = position;
        uint32_t block, raw;
        if ((index & 0xFFU) == 0U && ebk_stopped(pd)) return false;
        if (index != 0U) {
            if (size - position < (int64_t)sizeof(head) ||
                !ebk_read_at(device, base + position, head, sizeof(head)) ||
                ebk_le16(head) != EBC_CLASS_REF)
                return false;
            header = position;
            sizes_at = position + EBC_ELEMENT_REF;
            fields = head + EBC_ELEMENT_REF;
        } else if (size - position < 10 ||
                   !ebk_read_at(device, base + position, head, 10U)) {
            return false;
        }
        block = ebk_le32(fields);
        raw = ebk_le32(fields + 4);
        /* The viewer reads both sizes as signed. */
        if (block < 4U + EBK_MIN_PACKED || block > (uint32_t)INT32_MAX ||
            raw > (uint32_t)INT32_MAX ||
            (int64_t)block > size - sizes_at - 4 ||
            (uint64_t)raw > (uint64_t)(block - 4U) * EBK_DEFLATE_RATIO +
                                EBK_DEFLATE_SLACK ||
            !ebk_zlib_header_ok(fields + 8))
            return false;
        if (items) {
            ebk_member *member = &items[index];
            member->header_offset = header;
            member->data_offset = sizes_at + 8;
            member->header_size = (uint32_t)(member->data_offset - header);
            member->packed_size = block - 4U;
            member->packed_span = block - 4U;
            member->raw_size = raw;
        }
        position = sizes_at + 4 + (int64_t)block;
    }
    *pack_end = position;
    return true;
}

/* The settings list right after the pack opens with the name table:
 * CUpdateDir class reference, list id, u16 element count, then the first
 * element (class reference, type 2, u32 size).  Every entry must name a
 * distinct file and the entries must fill the table exactly.  With @p items
 * each named file gets its name.  False means "no usable table", not "no
 * book". */
static bool ebc_names(xx_io_device *device, int64_t base, int64_t size,
                      int64_t at, uint32_t count, ebk_member *items,
                      int64_t *table_at, uint32_t *table_size) {
    uint8_t head[EBC_NAMES_HEADER];
    uint8_t *table = NULL, *seen = NULL;
    uint32_t length, entry;
    size_t position = 0U;
    bool result = false;
    if (size - at < (int64_t)sizeof(head) ||
        !ebk_read_at(device, base + at, head, sizeof(head)) ||
        ebk_le16(head) != EBC_DIR_REF || ebk_le16(head + 4) == 0U ||
        ebk_le16(head + 6) != EBC_CLASS_REF ||
        ebk_le16(head + 8) != EBC_TYPE_NAMES)
        return false;
    length = ebk_le32(head + 10);
    if (length > EBC_MAX_TABLE ||
        (uint64_t)length < (uint64_t)count * (EBC_ENTRY_TAIL + 2U) ||
        (int64_t)length > size - at - (int64_t)sizeof(head))
        return false;
    table = (uint8_t *)xx_mem_alloc(length);
    seen = (uint8_t *)xx_mem_calloc(count, 1U);
    if (!table || !seen ||
        !ebk_read_at(device, base + at + (int64_t)sizeof(head), table, length))
        goto done;
    for (entry = 0U; entry < count; ++entry) {
        size_t name_start = position, name_length;
        uint32_t file;
        while (position < length && table[position] != 0U) {
            if (table[position] < 0x20U || table[position] == 0x7fU) goto done;
            ++position;
        }
        name_length = position - name_start;
        if (position >= length || name_length == 0U ||
            name_length > EBK_MAX_NAME ||
            length - position - 1U < EBC_ENTRY_TAIL)
            goto done;
        file = ebk_le32(table + position + 1U);
        if (file == 0U || file > count || seen[file - 1U]) goto done;
        seen[file - 1U] = 1U;
        if (items) {
            ebk_member *member = &items[file - 1U];
            member->name = ebk_name(table + name_start, name_length);
            if (!member->name) goto done;
        }
        position += 1U + EBC_ENTRY_TAIL;
    }
    if (position != length) goto done;
    *table_at = at + (int64_t)sizeof(head);
    *table_size = length;
    result = true;
done:
    if (!result && items) {
        for (entry = 0U; entry < count; ++entry) {
            if (items[entry].name) {
                xx_mem_free(items[entry].name);
                items[entry].name = NULL;
            }
        }
    }
    if (table) xx_mem_free(table);
    if (seen) xx_mem_free(seen);
    return result;
}

static bool ebc_scan(xx_io_device *device, int64_t base, int64_t size,
                     int64_t at, bool with_names, ebk_info *info,
                     xx_pd_struct *pd) {
    uint8_t head[EBC_HEADER];
    uint8_t tail[4];
    uint32_t count;
    int64_t pack_end = 0;
    if (size - at < (int64_t)sizeof(head) ||
        !ebk_read_at(device, base + at, head, sizeof(head)) ||
        xx_rt_memcmp(head, ebc_head_a, sizeof(ebc_head_a)) != 0 ||
        xx_rt_memcmp(head + EBC_ELEMENT0_CLASS, ebc_head_b,
                     sizeof(ebc_head_b)) != 0)
        return false;
    count = ebk_le16(head + 22);
    /* Every element takes at least its sizes and a minimal zlib stream. */
    if (count == 0U ||
        (int64_t)count > (size - at - (int64_t)EBC_HEADER) /
                             (int64_t)(8U + EBK_MIN_PACKED) ||
        !ebc_walk(device, base, size, at, count, NULL, &pack_end, pd))
        return false;
    info->variant = XX_SFX_EBOOK_VARIANT_EBOOK_CREATOR;
    info->count = count;
    info->header_value = ebk_le16(head + 20);
    info->payload = at;
    info->first = at + EBC_HEADER;
    info->members_end = pack_end;
    info->names = -1;
    info->names_size = 0U;
    info->end = pack_end;
    if (with_names &&
        ebc_names(device, base, size, pack_end, count, NULL, &info->names,
                  &info->names_size))
        info->end = info->names + (int64_t)info->names_size;
    /* The book ends with the store's offset and two letters. */
    info->has_trailer =
        size - info->end >= EBC_TRAILER &&
        ebk_read_at(device, base + size - EBC_TRAILER, tail, sizeof(tail)) &&
        (int64_t)ebk_le32(tail) == at;
    if (info->has_trailer) info->end = size;
    return true;
}

/* --- SBook Builder -------------------------------------------------------------- */

/* Walks the files from @p at up to the end marker.  Each file's chunk chain
 * ends where the next u32 is 0: a chunk is never empty, and the next file
 * (or the marker) starts with a 0 field.  With @p items (room for @p
 * capacity files) the files are also recorded. */
static bool sb_walk(xx_io_device *device, int64_t base, int64_t size,
                    int64_t at, ebk_member *items, uint32_t capacity,
                    uint32_t *count, int64_t *marker, xx_pd_struct *pd) {
    uint8_t record[EBK_MAX_NAME + 8U];
    int64_t position = at;
    uint32_t files = 0U;
    for (;;) {
        uint8_t head[8];
        uint32_t name_length, chunks = 0U;
        int64_t chunk_at;
        if ((files & 0xFFU) == 0U && ebk_stopped(pd)) return false;
        if (size - position < (int64_t)sizeof(head) ||
            !ebk_read_at(device, base + position, head, sizeof(head)))
            return false;
        name_length = ebk_le32(head + 4);
        if (name_length == 0U && ebk_le32(head) == 0U) break;
        if (files >= SB_MAX_FILES || (items && files >= capacity) ||
            name_length == 0U || name_length > EBK_MAX_NAME ||
            (int64_t)name_length + 8 > size - position - 8 ||
            !ebk_read_at(device, base + position + 8, record,
                         (size_t)name_length + 8U))
            return false;
        {
            uint32_t byte;
            for (byte = 0U; byte < name_length; ++byte)
                if (record[byte] < 0x20U || record[byte] == 0x7fU) return false;
        }
        chunk_at = position + 8 + (int64_t)name_length + 8;
        for (;;) {
            uint8_t chunk[6];
            uint32_t packed;
            if (size - chunk_at < 4 ||
                !ebk_read_at(device, base + chunk_at, chunk, 4U))
                return false;
            packed = ebk_le32(chunk);
            if (packed == 0U && chunks != 0U) break;
            if (packed < EBK_MIN_PACKED || packed > SB_MAX_PACKED_CHUNK ||
                (int64_t)packed > size - chunk_at - 4 ||
                !ebk_read_at(device, base + chunk_at + 4, chunk + 4, 2U) ||
                !ebk_zlib_header_ok(chunk + 4))
                return false;
            chunk_at += 4 + (int64_t)packed;
            ++chunks;
            if ((chunks & 0xFFFU) == 0U && ebk_stopped(pd)) return false;
        }
        if (items) {
            ebk_member *member = &items[files];
            member->header_offset = position;
            member->header_size = 8U + name_length + 8U;
            member->data_offset = position + (int64_t)member->header_size;
            member->packed_span = chunk_at - member->data_offset;
            member->chunks = chunks;
            member->dos_stamp = ebk_le32(record + name_length);
            member->name = ebk_name(record, name_length);
            if (!member->name) return false;
        }
        ++files;
        position = chunk_at;
    }
    if (files == 0U) return false;
    *count = files;
    *marker = position;
    return true;
}

static bool sb_scan(xx_io_device *device, int64_t base, int64_t size,
                    int64_t at, ebk_info *info, xx_pd_struct *pd) {
    uint8_t head[SB_HEADER];
    uint8_t tail[4];
    uint32_t count = 0U;
    int64_t marker = 0;
    if (size - at < (int64_t)sizeof(head) ||
        !ebk_read_at(device, base + at, head, sizeof(head)) ||
        ebk_le32(head) != 5U ||
        (xx_rt_memcmp(head + 4, "Sbook", 5U) != 0 &&
         xx_rt_memcmp(head + 4, "Ebook", 5U) != 0) ||
        !sb_walk(device, base, size, at + SB_HEADER, NULL, 0U, &count, &marker,
                 pd))
        return false;
    info->variant = XX_SFX_EBOOK_VARIANT_SBOOK_BUILDER;
    info->count = count;
    info->header_value = ebk_le32(head + 9);
    info->payload = at;
    info->first = at + SB_HEADER;
    info->members_end = marker;
    info->names = -1;
    info->names_size = 0U;
    info->end = marker + 8;
    xx_rt_memcpy(info->tag, head + 4, 5U);
    info->tag[5] = 0;
    /* The last four bytes of the file give the store's offset. */
    info->has_trailer =
        size - info->end >= SB_TRAILER &&
        ebk_read_at(device, base + size - SB_TRAILER, tail, sizeof(tail)) &&
        (int64_t)ebk_le32(tail) == at;
    if (info->has_trailer) info->end = size;
    return true;
}

/* --- locating the store --------------------------------------------------------- */

/* The store starts at the overlay; both builders also record its offset in
 * a trailer, which is tried when the overlay does not hold it. */
static bool ebk_scan(Abstractformat *format, bool with_names, ebk_info *info,
                     xx_pd_struct *pd) {
    int64_t candidates[3];
    int64_t total, size, overlay = -1, headers_end = 0;
    size_t count = 0U, index, other;
    uint8_t tail[6];
    if (!format || !format->device || !info || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    xx_rt_memset(info, 0, sizeof(*info));
    if (!ebk_pe(format->device, format->base_address, size, &overlay,
                &headers_end))
        return false;
    if (overlay >= 0) candidates[count++] = overlay;
    if (ebk_read_at(format->device, format->base_address + size - 6, tail,
                    sizeof(tail))) {
        candidates[count++] = (int64_t)ebk_le32(tail + 2);
        candidates[count++] = (int64_t)ebk_le32(tail);
    }
    for (index = 0U; index < count; ++index) {
        int64_t at = candidates[index];
        bool repeated = false;
        for (other = 0U; other < index; ++other)
            if (candidates[other] == at) repeated = true;
        if (repeated || at < headers_end || at >= size) continue;
        if (ebc_scan(format->device, format->base_address, size, at,
                     with_names, info, pd) ||
            sb_scan(format->device, format->base_address, size, at, info, pd))
            return true;
        if (ebk_stopped(pd)) return false;
    }
    return false;
}

/* Folds @p member's current name and looks it up; *slot receives the slot
 * holding the same name, or the free slot where it would go. */
static bool ebk_name_taken(ebk_member *items, const size_t *table,
                           size_t slots, ebk_member *member, size_t *slot,
                           bool *taken) {
    member->folded = (char *)xx_mem_alloc(xx_str_len(member->name) + 1U);
    if (!member->folded) return false;
    ebk_fold(member->name, member->folded);
    *taken = false;
    *slot = ebk_hash(member->folded) & (slots - 1U);
    while (table[*slot] != SIZE_MAX) {
        if (xx_str_cmp(items[table[*slot]].folded, member->folded) == 0) {
            *taken = true;
            break;
        }
        *slot = (*slot + 1U) & (slots - 1U);
    }
    return true;
}

/* Later duplicates (compared as Windows compares names) get "_2", "_3", ...
 * in front of their extension, so no file overwrites another.  Only names
 * that can be extracted take part. */
static bool ebk_unique_names(ebk_member *items, size_t count) {
    size_t slots = 16U, index;
    size_t *table;
    bool result = false;
    while (slots < count * 2U) slots <<= 1U;
    table = (size_t *)xx_mem_alloc(slots * sizeof(*table));
    if (!table) return false;
    for (index = 0U; index < slots; ++index) table[index] = SIZE_MAX;
    for (index = 0U; index < count; ++index) {
        ebk_member *member = &items[index];
        char *stored = NULL;
        size_t slot = 0U, number = 1U;
        bool taken = false;
        if (!member->safe) continue;
        for (;;) {
            if (!ebk_name_taken(items, table, slots, member, &slot, &taken))
                break;
            if (!taken) break;
            xx_mem_free(member->folded);
            member->folded = NULL;
            /* At most count names are taken, so one of the next count
             * suffixes is free; the limit only guards the arithmetic. */
            if (++number > count + 1U || number > EBK_RENAME_LIMIT) break;
            if (!stored) {
                stored = member->name;
            } else {
                xx_mem_free(member->name);
            }
            member->name = ebk_with_suffix(stored, number);
            if (!member->name) {
                member->name = stored;
                stored = NULL;
                break;
            }
        }
        if (stored) xx_mem_free(stored);
        if (!member->folded || taken) goto done;
        table[slot] = index;
    }
    result = true;
done:
    for (index = 0U; index < count; ++index) {
        if (items[index].folded) {
            xx_mem_free(items[index].folded);
            items[index].folded = NULL;
        }
    }
    xx_mem_free(table);
    return result;
}

/* --- decoding -------------------------------------------------------------- */

/* A write-only device between the inflater and the destination: it counts
 * the output, refuses anything past the limit and keeps the Adler-32. */
typedef struct ebk_sink_s {
    xx_io_device device; /* first, so the sink is its own device */
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
    uint32_t adler_a;
    uint32_t adler_b;
    bool failed;
} ebk_sink;

static ssize_t ebk_sink_write(xx_io_device *self, const void *buffer,
                              size_t size) {
    ebk_sink *sink = (ebk_sink *)self;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t index = 0U, done = 0U;
    if (!sink || (!buffer && size != 0U) || sink->failed ||
        sink->written > sink->limit ||
        (uint64_t)size > sink->limit - sink->written ||
        size > ((size_t)-1 >> 1U)) {
        if (sink) sink->failed = true;
        return -1;
    }
    while (index < size) {
        size_t chunk = size - index < 5552U ? size - index : 5552U;
        size_t stop = index + chunk;
        for (; index < stop; ++index) {
            sink->adler_a += bytes[index];
            sink->adler_b += sink->adler_a;
        }
        sink->adler_a %= 65521U;
        sink->adler_b %= 65521U;
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

/* One zlib stream of @p packed bytes at @p data into the sink, which may
 * grow by at most @p room bytes; the stream's Adler-32 must match. */
static bool ebk_inflate(xx_io_device *device, int64_t data, uint32_t packed,
                        ebk_sink *sink, uint64_t room, xx_pd_struct *pd) {
    uint8_t header[2], trailer[4];
    uint32_t expected;
    if (packed < EBK_MIN_PACKED ||
        !ebk_read_at(device, data, header, sizeof(header)) ||
        !ebk_zlib_header_ok(header) ||
        !ebk_read_at(device, data + (int64_t)packed - 4, trailer,
                     sizeof(trailer)))
        return false;
    expected = ((uint32_t)trailer[0] << 24U) | ((uint32_t)trailer[1] << 16U) |
               ((uint32_t)trailer[2] << 8U) | (uint32_t)trailer[3];
    sink->limit = sink->written + room;
    sink->adler_a = 1U;
    sink->adler_b = 0U;
    if (!xx_deflate_unpack_device(device, data + 2, (int64_t)packed - 6,
                                  &sink->device, false, pd) ||
        sink->failed)
        return false;
    return ((sink->adler_b << 16U) | sink->adler_a) == expected;
}

static bool ebk_decode(Abstractformat *format, uint32_t variant,
                       const ebk_member *member, xx_io_device *target,
                       xx_pd_struct *pd) {
    ebk_sink sink;
    int64_t base = format->base_address, total, chunk_at;
    uint32_t chunk;
    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.device.write = ebk_sink_write;
    sink.target = target;
    if (variant == XX_SFX_EBOOK_VARIANT_EBOOK_CREATOR) {
        return ebk_inflate(format->device, base + member->data_offset,
                           member->packed_size, &sink, member->raw_size, pd) &&
               sink.written == (uint64_t)member->raw_size;
    }
    if (variant != XX_SFX_EBOOK_VARIANT_SBOOK_BUILDER || member->chunks == 0U)
        return false;
    total = xx_io_total_size(format->device);
    chunk_at = base + member->data_offset;
    for (chunk = 0U; chunk < member->chunks; ++chunk) {
        uint8_t field[4];
        uint32_t packed;
        uint64_t before = sink.written;
        if (ebk_stopped(pd) || total - chunk_at < 4 ||
            !ebk_read_at(format->device, chunk_at, field, sizeof(field)))
            return false;
        packed = ebk_le32(field);
        if (packed < EBK_MIN_PACKED || packed > SB_MAX_PACKED_CHUNK ||
            (int64_t)packed > total - chunk_at - 4 ||
            !ebk_inflate(format->device, chunk_at + 4, packed, &sink,
                         SB_CHUNK, pd))
            return false;
        /* Only the last chunk may be short. */
        if (chunk + 1U < member->chunks && sink.written - before != SB_CHUNK)
            return false;
        chunk_at += 4 + (int64_t)packed;
    }
    return chunk_at == base + member->data_offset + member->packed_span;
}

/* --- records --------------------------------------------------------------- */

static bool ebk_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ebk_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ebk_set_record(Abstractformat *format, uint32_t variant,
                           xx_archive_record *record,
                           const ebk_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = format->base_address + member->data_offset;
    record->compressed_size = member->packed_span;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->packed_span) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        EBK_METHOD_DEFLATE) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (variant == XX_SFX_EBOOK_VARIANT_EBOOK_CREATOR)
        return xx_archive_record_set_meta_u64(
            record, XX_META_ID_UNCOMPRESSED_SIZE, member->raw_size);
    /* SBook Builder keeps no unpacked size; the chunks give it only when
     * they are inflated. */
    return xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_stamp & 0xFFFFU) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_stamp >> 16U);
}

/* --- lifecycle ------------------------------------------------------------- */

void xx_sfx_ebook_compiler_executables_init(
    xx_sfx_ebook_compiler_executables *archive, xx_io_device *device,
    int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_EBOOK_COMPILER_EXECUTABLES_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdos-program");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_sfx_ebook_compiler_executables_check_is_valid;
    archive->format.handle_base_info =
        xx_sfx_ebook_compiler_executables_handle_base_info;
    archive->format.get_format_size =
        xx_sfx_ebook_compiler_executables_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_ebook_compiler_executables_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_ebook_compiler_executables_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_ebook_compiler_executables_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_ebook_compiler_executables_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_ebook_compiler_executables_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_ebook_compiler_executables_free_archive_records_reading;
    archive->payload_offset = -1;
    archive->payload_end = -1;
    archive->names_offset = -1;
}

xx_sfx_ebook_compiler_executables *xx_sfx_ebook_compiler_executables_create(
    xx_io_device *device, int64_t base_address) {
    xx_sfx_ebook_compiler_executables *archive =
        (xx_sfx_ebook_compiler_executables *)xx_mem_alloc(sizeof(*archive));
    if (archive)
        xx_sfx_ebook_compiler_executables_init(archive, device, base_address);
    return archive;
}

void xx_sfx_ebook_compiler_executables_destroy(
    xx_sfx_ebook_compiler_executables *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_ebook_compiler_executables_free(
    xx_sfx_ebook_compiler_executables *archive) {
    if (!archive) return;
    xx_sfx_ebook_compiler_executables_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_ebook_compiler_executables_check_is_valid(Abstractformat *format,
                                                      xx_pd_struct *pd) {
    ebk_info info;
    return ebk_scan(format, false, &info, pd);
}

bool xx_sfx_ebook_compiler_executables_handle_base_info(Abstractformat *format,
                                                        xx_pd_struct *pd) {
    ebk_info info;
    xx_sfx_ebook_compiler_executables *archive;
    if (!format || !ebk_scan(format, true, &info, pd)) return false;
    archive = (xx_sfx_ebook_compiler_executables *)format;
    archive->number_of_records = info.count;
    archive->variant = info.variant;
    archive->payload_offset = info.payload;
    archive->payload_end = info.end;
    archive->names_offset = info.names;
    archive->header_value = info.header_value;
    archive->has_names = info.names >= 0;
    archive->has_trailer = info.has_trailer;
    xx_rt_memcpy(archive->tag, info.tag, sizeof(archive->tag));
    format->number_of_archive_records = info.count;
    format->format_size = info.end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_ebook_compiler_executables_get_format_size(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_ebook_compiler_executables_handle_base_info(
                          format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sfx_ebook_compiler_executables_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_ebook_compiler_executables_handle_base_info(
                          format, pd))
               ? ((xx_sfx_ebook_compiler_executables *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_sfx_ebook_compiler_executables_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ebk_info info;
    ebk_stream *stream;
    xx_archive_record_state *state;
    int64_t size, end = 0;
    uint32_t count = 0U, index;
    if (!ebk_scan(format, true, &info, pd)) return NULL;
    size = xx_io_total_size(format->device) - format->base_address;
    stream = (ebk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->variant = info.variant;
    /* At most 65535 eBook Creator files, each proven by ten bytes in the
     * file; SBook files are counted by a walk that read each of them. */
    stream->items = (ebk_member *)xx_mem_calloc(info.count,
                                                sizeof(*stream->items));
    stream->count = info.count;
    if (!stream->items) goto fail;
    if (info.variant == XX_SFX_EBOOK_VARIANT_EBOOK_CREATOR) {
        if (!ebc_walk(format->device, format->base_address, size,
                      info.payload, info.count, stream->items, &end, pd) ||
            end != info.members_end)
            goto fail;
        if (info.names >= 0)
            (void)ebc_names(format->device, format->base_address, size,
                            info.members_end, info.count, stream->items, &end,
                            &count);
        for (index = 0U; index < info.count; ++index) {
            if (!stream->items[index].name &&
                !(stream->items[index].name = ebk_index_name(index)))
                goto fail;
        }
    } else {
        if (!sb_walk(format->device, format->base_address, size, info.first,
                     stream->items, info.count, &count, &end, pd) ||
            count != info.count || end != info.members_end)
            goto fail;
    }
    for (index = 0U; index < info.count; ++index)
        stream->items[index].safe =
            ebk_safe_output_name(stream->items[index].name);
    if (!ebk_unique_names(stream->items, stream->count)) goto fail;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) goto fail;
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ebk_stream_free;
    state->total_records = stream->count;
    if (!ebk_copy_options(&state->options, options) ||
        !ebk_set_record(format, stream->variant, &state->current_record,
                        &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
fail:
    ebk_stream_free(stream);
    return NULL;
}

const xx_archive_record *
xx_sfx_ebook_compiler_executables_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sfx_ebook_compiler_executables_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ebk_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ebk_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    if (!ebk_set_record(format, stream->variant, &state->current_record,
                        &stream->items[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_sfx_ebook_compiler_executables_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ebk_stream *stream;
    const ebk_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ebk_stream *)state->internal_state) ||
        stream->index >= stream->count || ebk_stopped(pd))
        return false;
    member = &stream->items[stream->index];
    path_option = ebk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return ebk_decode(format, stream->variant, member, NULL, pd);
    if (!member->safe || !ebk_safe_output_name(member->name)) return false;
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
        result = ebk_decode(format, stream->variant, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_ebook_compiler_executables_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
