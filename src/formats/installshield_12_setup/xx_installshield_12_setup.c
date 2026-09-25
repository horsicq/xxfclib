/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield 12..2012 single-file setup.exe ("SFX IS18" in U3's naming):
 * the whole Disk1 media folder rides in the PE overlay of the launcher.
 *
 *   overlay+0  u32 LE  number of files
 *   then, per file, four NUL-terminated UTF-16LE strings
 *       base name        "data1.cab"
 *       relative path    "Disk1\data1.cab"
 *       file version     "0.0.0.0", "17.0.0.717"
 *       decimal size     "566668"
 *   followed by that many raw bytes (stored, never compressed here; the
 *   members are themselves ISc( cabinets, setup.inx, ISSetup.dll, ...).
 *
 * The chain ends at the end of the file, or in a signed launcher at the
 * Authenticode table named by the PE security directory (the gap is the
 * table's 8-byte alignment padding).
 *
 * The PE image is parsed only as far as needed to find where its sections
 * end and whether a certificate follows; nothing is executed or emulated.
 * The layout was measured on the four corpus launchers (IS 17.0 and 30.0
 * engines).  The same record idea, in ANSI and without the count, is the
 * older "Setup Player 2K2" all-in-one; XArchive's installers/xis14sfxarchive
 * (MIT) was read for the ANSI shape, the code here is written from the
 * layout above.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installshield_12_setup/xx_installshield_12_setup.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef INSTALLSHIELD_12_SETUP
#define XX_INSTALLSHIELD_12_SETUP_FILE_TYPE XX_FILE_TYPE_INSTALLSHIELD_12_SETUP
#else
#define XX_INSTALLSHIELD_12_SETUP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IS12_DOS_HEADER 64U
#define IS12_NT_HEADER 24U
#define IS12_OPTIONAL_MAX 240U /* PE32+ header with all 16 directories */
#define IS12_SECTION_SIZE 40U
#define IS12_MAX_SECTIONS 96U
#define IS12_DIRECTORY_SECURITY 4U

/* The launcher writes a u32 count; U3 accepts 1..0xFFFF and so does this. */
#define IS12_MAX_COUNT 65535U
/* String limits, in UTF-16 code units, without the terminator. */
#define IS12_MAX_NAME_UNITS 512U
#define IS12_MAX_VERSION_UNITS 32U
#define IS12_MAX_SIZE_DIGITS 19U /* keeps the value below 2^63 */
#define IS12_HEADER_MAX                                                     \
    ((IS12_MAX_NAME_UNITS + 1U) * 4U + (IS12_MAX_VERSION_UNITS + 1U) * 2U + \
     (IS12_MAX_SIZE_DIGITS + 1U) * 2U)
/* First read of a record header; real headers are 76..98 bytes.  Only a
 * header longer than this is read again at IS12_HEADER_MAX. */
#define IS12_FIRST_READ 256U
/* All member names together.  Bounded by the header bytes present in the
 * file anyway; this keeps a hostile chain from costing more than this. */
#define IS12_MAX_NAME_BYTES (8U * 1024U * 1024U)
#define IS12_RENAME_PASSES 3U
#define IS12_FOLD_ANY 0xFFFFFFFFU

#define IS12_RECORD_OK 0
#define IS12_RECORD_SHORT 1 /* the bytes ran out inside the header */
#define IS12_RECORD_BAD 2   /* not a record */

typedef struct is12_member_s {
    char *name; /* UTF-8, '/' separated */
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;
    unsigned renames;
    bool extractable;
} is12_member;

typedef struct is12_stream_s {
    is12_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    size_t name_bytes;
    uint32_t declared;
    int64_t overlay;     /* relative to base */
    int64_t chain_end;   /* relative to base */
    int64_t format_size; /* relative to base */
    int64_t certificate_offset;
    int64_t certificate_size;
    bool truncated;
} is12_stream;

typedef struct is12_layout_s {
    int64_t overlay;
    int64_t limit;
    int64_t certificate_offset;
    int64_t certificate_size;
} is12_layout;

typedef struct is12_header_s {
    size_t name_position;
    size_t name_units;
    size_t path_position;
    size_t path_units;
    size_t length;
    uint64_t size;
} is12_header;

static uint16_t is12_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t is12_le32(const uint8_t *bytes) {
    return (uint32_t)is12_le16(bytes) | ((uint32_t)is12_le16(bytes + 2U) << 16U);
}

static bool is12_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool is12_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* ---------------------------------------------------------------------- */
/* PE launcher                                                             */

/* Finds the overlay (the end of the furthest section's raw data) and the
 * Authenticode table.  Only header fields are read. */
static bool is12_pe_layout(xx_io_device *device, int64_t base,
                           int64_t available, is12_layout *layout) {
    uint8_t dos[IS12_DOS_HEADER];
    uint8_t nt[IS12_NT_HEADER];
    uint8_t optional[IS12_OPTIONAL_MAX];
    uint8_t sections[IS12_SECTION_SIZE * IS12_MAX_SECTIONS];
    uint32_t nt_offset;
    uint32_t directory_count;
    uint16_t section_count;
    uint16_t optional_size;
    uint16_t optional_magic;
    size_t optional_read;
    size_t directory_base;
    int64_t table;
    int64_t overlay = 0;
    uint16_t index;

    if (available < (int64_t)IS12_DOS_HEADER ||
        !is12_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    nt_offset = is12_le32(dos + 0x3CU);
    if (nt_offset < IS12_DOS_HEADER ||
        !is12_range_within(available, (int64_t)nt_offset, IS12_NT_HEADER) ||
        !is12_read_at(device, base + nt_offset, nt, sizeof(nt)) ||
        nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U)
        return false;
    section_count = is12_le16(nt + 6U);
    optional_size = is12_le16(nt + 20U);
    if (section_count == 0U || section_count > IS12_MAX_SECTIONS ||
        optional_size < 2U)
        return false;
    optional_read = optional_size < IS12_OPTIONAL_MAX ? optional_size
                                                      : IS12_OPTIONAL_MAX;
    if (!is12_range_within(available, (int64_t)nt_offset + IS12_NT_HEADER,
                           (int64_t)optional_read) ||
        !is12_read_at(device, base + nt_offset + IS12_NT_HEADER, optional,
                      optional_read))
        return false;
    optional_magic = is12_le16(optional);
    if (optional_magic == 0x010BU)
        directory_base = 96U;
    else if (optional_magic == 0x020BU)
        directory_base = 112U;
    else
        return false;
    if (optional_read < directory_base) return false;
    directory_count = is12_le32(optional + directory_base - 4U);

    table = (int64_t)nt_offset + IS12_NT_HEADER + optional_size;
    if (!is12_range_within(available, table,
                           (int64_t)section_count * IS12_SECTION_SIZE) ||
        !is12_read_at(device, base + table, sections,
                      (size_t)section_count * IS12_SECTION_SIZE))
        return false;
    for (index = 0U; index < section_count; ++index) {
        const uint8_t *section = sections + (size_t)index * IS12_SECTION_SIZE;
        int64_t raw_size = (int64_t)is12_le32(section + 16U);
        int64_t raw_offset = (int64_t)is12_le32(section + 20U);
        if (raw_size == 0) continue;
        /* A section that runs past the file leaves no overlay to speak of. */
        if (!is12_range_within(available, raw_offset, raw_size)) return false;
        if (raw_offset + raw_size > overlay) overlay = raw_offset + raw_size;
    }
    if (overlay <= 0 || overlay >= available) return false;

    layout->overlay = overlay;
    layout->limit = available;
    layout->certificate_offset = -1;
    layout->certificate_size = 0;
    if (directory_count > IS12_DIRECTORY_SECURITY &&
        optional_read >= directory_base + (IS12_DIRECTORY_SECURITY + 1U) * 8U) {
        /* The security directory holds a file offset, not an RVA. */
        const uint8_t *entry = optional + directory_base +
                               IS12_DIRECTORY_SECURITY * 8U;
        int64_t offset = (int64_t)is12_le32(entry);
        int64_t size = (int64_t)is12_le32(entry + 4U);
        if (offset != 0 && size != 0 && offset >= overlay) {
            layout->certificate_offset = offset;
            layout->certificate_size = size;
            /* A cut-off signed file still ends its chain at the table. */
            layout->limit = offset < available ? offset : available;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Record headers                                                          */

static int is12_scan_string(const uint8_t *buffer, size_t size,
                            size_t *position, size_t max_units,
                            size_t *units) {
    size_t at = *position;
    size_t count = 0U;
    for (;;) {
        uint16_t unit;
        if (at > size || size - at < 2U) return IS12_RECORD_SHORT;
        unit = is12_le16(buffer + at);
        at += 2U;
        if (unit == 0U) break;
        if (++count > max_units) return IS12_RECORD_BAD;
    }
    *units = count;
    *position = at;
    return IS12_RECORD_OK;
}

/* A name or path unit: printable, none of the characters a Windows file
 * name cannot hold.  The separators are judged by the caller. */
static bool is12_unit_ok(uint16_t unit) {
    return unit >= 0x20U && unit != 0x7FU && unit != '"' && unit != '*' &&
           unit != ':' && unit != '<' && unit != '>' && unit != '?' &&
           unit != '|';
}

/* Surrogates must pair up, or the name cannot become UTF-8. */
static bool is12_units_ok(const uint8_t *text, size_t units,
                          bool allow_separators) {
    size_t index;
    for (index = 0U; index < units; ++index) {
        uint16_t unit = is12_le16(text + index * 2U);
        if (!is12_unit_ok(unit)) return false;
        if (!allow_separators && (unit == '\\' || unit == '/')) return false;
        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            uint16_t next;
            if (index + 1U >= units) return false;
            next = is12_le16(text + (index + 1U) * 2U);
            if (next < 0xDC00U || next > 0xDFFFU) return false;
            ++index;
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            return false;
        }
    }
    return true;
}

static uint16_t is12_fold_unit(uint16_t unit) {
    return (unit >= 'a' && unit <= 'z') ? (uint16_t)(unit - 0x20U) : unit;
}

/* The path's last component is the base name (ASCII case ignored). */
static bool is12_path_ends_with(const uint8_t *path, size_t path_units,
                                const uint8_t *name, size_t name_units) {
    size_t start = 0U;
    size_t index;
    for (index = 0U; index < path_units; ++index) {
        uint16_t unit = is12_le16(path + index * 2U);
        if (unit == '\\' || unit == '/') start = index + 1U;
    }
    if (path_units - start != name_units) return false;
    for (index = 0U; index < name_units; ++index)
        if (is12_fold_unit(is12_le16(path + (start + index) * 2U)) !=
            is12_fold_unit(is12_le16(name + index * 2U)))
            return false;
    return true;
}

/* "17.0.0.717": one to four groups of one to ten digits. */
static bool is12_version_ok(const uint8_t *text, size_t units) {
    size_t index;
    size_t digits = 0U;
    size_t groups = 1U;
    if (units == 0U) return false;
    for (index = 0U; index < units; ++index) {
        uint16_t unit = is12_le16(text + index * 2U);
        if (unit >= '0' && unit <= '9') {
            if (++digits > 10U) return false;
        } else if (unit == '.') {
            if (digits == 0U || ++groups > 4U) return false;
            digits = 0U;
        } else {
            return false;
        }
    }
    return digits != 0U;
}

static bool is12_size_value(const uint8_t *text, size_t units,
                            uint64_t *value) {
    size_t index;
    uint64_t result = 0U;
    if (units == 0U || units > IS12_MAX_SIZE_DIGITS) return false;
    for (index = 0U; index < units; ++index) {
        uint16_t unit = is12_le16(text + index * 2U);
        if (unit < '0' || unit > '9') return false;
        result = result * 10U + (uint64_t)(unit - '0');
    }
    *value = result;
    return true;
}

/* Reads the four strings from @p buffer.  Each string is checked as soon
 * as it is found, so garbage is refused after its first string. */
static int is12_parse_header(const uint8_t *buffer, size_t size,
                             is12_header *header) {
    size_t position = 0U;
    size_t version_position, version_units;
    size_t size_position, size_units;
    int result;

    header->name_position = position;
    result = is12_scan_string(buffer, size, &position, IS12_MAX_NAME_UNITS,
                              &header->name_units);
    if (result != IS12_RECORD_OK) return result;
    if (header->name_units == 0U ||
        !is12_units_ok(buffer + header->name_position, header->name_units,
                       false))
        return IS12_RECORD_BAD;

    header->path_position = position;
    result = is12_scan_string(buffer, size, &position, IS12_MAX_NAME_UNITS,
                              &header->path_units);
    if (result != IS12_RECORD_OK) return result;
    if (header->path_units == 0U ||
        !is12_units_ok(buffer + header->path_position, header->path_units,
                       true) ||
        !is12_path_ends_with(buffer + header->path_position,
                             header->path_units,
                             buffer + header->name_position,
                             header->name_units))
        return IS12_RECORD_BAD;

    version_position = position;
    result = is12_scan_string(buffer, size, &position, IS12_MAX_VERSION_UNITS,
                              &version_units);
    if (result != IS12_RECORD_OK) return result;
    if (!is12_version_ok(buffer + version_position, version_units))
        return IS12_RECORD_BAD;

    size_position = position;
    result = is12_scan_string(buffer, size, &position, IS12_MAX_SIZE_DIGITS,
                              &size_units);
    if (result != IS12_RECORD_OK) return result;
    if (!is12_size_value(buffer + size_position, size_units, &header->size))
        return IS12_RECORD_BAD;
    header->length = position;
    return IS12_RECORD_OK;
}

/* Bytes the UTF-8 form of validated UTF-16LE units takes. */
static size_t is12_utf8_length(const uint8_t *text, size_t units) {
    size_t length = 0U;
    size_t index;
    for (index = 0U; index < units; ++index) {
        uint16_t unit = is12_le16(text + index * 2U);
        if (unit < 0x80U)
            length += 1U;
        else if (unit < 0x800U)
            length += 2U;
        else if (unit >= 0xD800U && unit <= 0xDBFFU) {
            length += 4U;
            ++index;
        } else
            length += 3U;
    }
    return length;
}

/* UTF-16LE path -> UTF-8 with '\' as '/'.  The units were validated. */
static char *is12_path_to_utf8(const uint8_t *text, size_t units,
                               size_t length) {
    size_t index;
    char *result = (char *)xx_mem_alloc(length + 1U);
    uint8_t *out;
    if (!result) return NULL;
    out = (uint8_t *)result;
    for (index = 0U; index < units; ++index) {
        uint32_t code = is12_le16(text + index * 2U);
        if (code >= 0xD800U && code <= 0xDBFFU) {
            uint32_t low = is12_le16(text + (index + 1U) * 2U);
            code = 0x10000U + ((code - 0xD800U) << 10U) + (low - 0xDC00U);
            ++index;
        }
        if (code == '\\') code = '/';
        if (code < 0x80U) {
            *out++ = (uint8_t)code;
        } else if (code < 0x800U) {
            *out++ = (uint8_t)(0xC0U | (code >> 6U));
            *out++ = (uint8_t)(0x80U | (code & 0x3FU));
        } else if (code < 0x10000U) {
            *out++ = (uint8_t)(0xE0U | (code >> 12U));
            *out++ = (uint8_t)(0x80U | ((code >> 6U) & 0x3FU));
            *out++ = (uint8_t)(0x80U | (code & 0x3FU));
        } else {
            *out++ = (uint8_t)(0xF0U | (code >> 18U));
            *out++ = (uint8_t)(0x80U | ((code >> 12U) & 0x3FU));
            *out++ = (uint8_t)(0x80U | ((code >> 6U) & 0x3FU));
            *out++ = (uint8_t)(0x80U | (code & 0x3FU));
        }
    }
    *out = 0U;
    return result;
}

/* ---------------------------------------------------------------------- */
/* Distinct output names                                                   */

static uint32_t is12_utf8_next(const char *text, size_t *at) {
    const uint8_t *p = (const uint8_t *)text + *at;
    uint32_t c = p[0];
    if (c < 0x80U) {
        *at += 1U;
        return c;
    }
    if ((c & 0xE0U) == 0xC0U) {
        *at += 2U;
        return ((c & 0x1FU) << 6U) | (p[1] & 0x3FU);
    }
    if ((c & 0xF0U) == 0xE0U) {
        *at += 3U;
        return ((c & 0x0FU) << 12U) | ((uint32_t)(p[1] & 0x3FU) << 6U) |
               (p[2] & 0x3FU);
    }
    *at += 4U;
    return ((c & 0x07U) << 18U) | ((uint32_t)(p[1] & 0x3FU) << 12U) |
           ((uint32_t)(p[2] & 0x3FU) << 6U) | (p[3] & 0x3FU);
}

/* How Windows compares names, made conservative: ASCII letters fold to
 * upper case, U+0131 and U+017F fold to the ASCII letters they upper-case
 * to, and every other non-ASCII character is treated as possibly equal to
 * any other.  Two names that could open the same file therefore always
 * compare equal here; the price is an occasional needless rename. */
static uint32_t is12_fold(uint32_t c) {
    if (c < 0x80U) return (c >= 'a' && c <= 'z') ? c - 0x20U : c;
    if (c == 0x131U) return 'I';
    if (c == 0x17FU) return 'S';
    return IS12_FOLD_ANY;
}

static int is12_name_compare(const char *left, const char *right) {
    size_t a = 0U, b = 0U;
    while (left[a] && right[b]) {
        uint32_t x = is12_fold(is12_utf8_next(left, &a));
        uint32_t y = is12_fold(is12_utf8_next(right, &b));
        if (x != y) return x < y ? -1 : 1;
    }
    if (left[a]) return 1;
    if (right[b]) return -1;
    return 0;
}

/* Folded name first, record number second, so the earliest record of a
 * group of equal names sorts first and keeps its name. */
static bool is12_order_less(const is12_stream *stream, uint32_t left,
                            uint32_t right) {
    int compared = is12_name_compare(stream->items[left].name,
                                     stream->items[right].name);
    return compared < 0 || (compared == 0 && left < right);
}

static void is12_sift_down(const is12_stream *stream, uint32_t *order,
                           size_t root, size_t count) {
    for (;;) {
        size_t child = root * 2U + 1U;
        uint32_t swap;
        if (child >= count) return;
        if (child + 1U < count &&
            is12_order_less(stream, order[child], order[child + 1U]))
            ++child;
        if (!is12_order_less(stream, order[root], order[child])) return;
        swap = order[root];
        order[root] = order[child];
        order[child] = swap;
        root = child;
    }
}

/* Heap sort: O(n log n) whatever names a hostile file carries. */
static void is12_sort(const is12_stream *stream, uint32_t *order) {
    size_t count = stream->count;
    size_t index;
    for (index = 0U; index < count; ++index) order[index] = (uint32_t)index;
    if (count < 2U) return;
    for (index = count / 2U; index > 0U; --index)
        is12_sift_down(stream, order, index - 1U, count);
    for (index = count - 1U; index > 0U; --index) {
        uint32_t swap = order[0];
        order[0] = order[index];
        order[index] = swap;
        is12_sift_down(stream, order, 0U, index);
    }
}

static size_t is12_decimal(char *out, size_t value) {
    char digits[24];
    size_t count = 0U;
    size_t index;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (index = 0U; index < count; ++index)
        out[index] = digits[count - 1U - index];
    return count;
}

/* "Disk1/setup.exe" -> "Disk1/setup_<number>.exe", or with "_<attempt>"
 * added when this member was renamed before. */
static char *is12_renamed(const char *name, size_t number, unsigned attempt) {
    char suffix[56];
    size_t suffix_length = 0U;
    size_t length = xx_rt_strlen(name);
    size_t component = 0U;
    size_t insert;
    size_t index;
    char *result;
    suffix[suffix_length++] = '_';
    suffix_length += is12_decimal(suffix + suffix_length, number);
    if (attempt > 1U) {
        suffix[suffix_length++] = '_';
        suffix_length += is12_decimal(suffix + suffix_length, attempt);
    }
    for (index = 0U; index < length; ++index)
        if (name[index] == '/') component = index + 1U;
    insert = length;
    for (index = length; index > component + 1U; --index)
        if (name[index - 1U] == '.') {
            insert = index - 1U;
            break;
        }
    result = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, name, insert);
    xx_rt_memcpy(result + insert, suffix, suffix_length);
    xx_rt_memcpy(result + insert + suffix_length, name + insert,
                 length - insert);
    result[length + suffix_length] = 0;
    return result;
}

/* Gives every member a name no other member shares (as Windows compares
 * names), so extracting one can never replace another.  In each pass the
 * names are sorted; within a group of equal names the earliest record
 * keeps its name and each later one gets "_<record number>" before its
 * extension.  A rename can collide in turn, so the pass repeats; whatever
 * still collides after the last pass stays listed but is not extracted. */
static bool is12_make_unique(is12_stream *stream) {
    uint32_t *order;
    unsigned pass;
    if (stream->count < 2U) return true;
    order = (uint32_t *)xx_mem_alloc(stream->count * sizeof(*order));
    if (!order) return false;
    for (pass = 1U; pass <= IS12_RENAME_PASSES + 1U; ++pass) {
        bool changed = false;
        uint32_t keeper;
        size_t index;
        is12_sort(stream, order);
        keeper = order[0];
        for (index = 1U; index < stream->count; ++index) {
            is12_member *member = &stream->items[order[index]];
            char *renamed;
            if (is12_name_compare(stream->items[keeper].name, member->name) !=
                0) {
                keeper = order[index];
                continue;
            }
            if (pass > IS12_RENAME_PASSES) {
                member->extractable = false;
                continue;
            }
            renamed = is12_renamed(member->name, (size_t)order[index] + 1U,
                                   ++member->renames);
            if (!renamed) {
                xx_mem_free(order);
                return false;
            }
            xx_mem_free(member->name);
            member->name = renamed;
            changed = true;
        }
        if (!changed) break;
    }
    xx_mem_free(order);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Chain                                                                   */

static void is12_stream_free(void *opaque) {
    is12_stream *stream = (is12_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool is12_add_member(is12_stream *stream, const is12_member *member) {
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity ? stream->capacity * 2U : 16U;
        is12_member *grown;
        if (capacity > IS12_MAX_COUNT) capacity = IS12_MAX_COUNT;
        if (capacity <= stream->count) return false;
        grown = (is12_member *)xx_mem_realloc(stream->items,
                                              capacity * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Walks the chain.  With @p want_names the members are also named and made
 * distinct; the probe and the base info only need the extents, so they
 * skip that (the name budget is still enforced, so the verdict is the
 * same either way). */
static bool is12_parse(Abstractformat *format, xx_pd_struct *pd,
                       bool want_names, is12_stream **result) {
    uint8_t count_bytes[4];
    uint8_t *buffer = NULL;
    is12_stream *stream = NULL;
    is12_layout layout;
    int64_t total, available, base, cursor;
    uint32_t declared;
    uint32_t index;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    available = total - base;
    if (!is12_pe_layout(format->device, base, available, &layout)) return false;
    /* The count and at least one minimal record: four one-unit strings. */
    if (!is12_range_within(layout.limit, layout.overlay, 4 + 16) ||
        !is12_read_at(format->device, base + layout.overlay, count_bytes, 4U))
        return false;
    declared = is12_le32(count_bytes);
    if (declared == 0U || declared > IS12_MAX_COUNT) return false;

    buffer = (uint8_t *)xx_mem_alloc(IS12_HEADER_MAX);
    stream = (is12_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!buffer || !stream) goto fail;
    stream->declared = declared;
    stream->overlay = layout.overlay;
    stream->certificate_offset = layout.certificate_offset;
    stream->certificate_size = layout.certificate_size;

    cursor = layout.overlay + 4;
    for (index = 0U; index < declared; ++index) {
        is12_header header;
        is12_member member;
        int64_t remaining = layout.limit - cursor;
        size_t wanted;
        size_t name_length;
        int parsed;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (remaining <= 0) {
            stream->truncated = true;
            break;
        }
        wanted = remaining < (int64_t)IS12_FIRST_READ ? (size_t)remaining
                                                      : IS12_FIRST_READ;
        if (!is12_read_at(format->device, base + cursor, buffer, wanted))
            goto fail;
        parsed = is12_parse_header(buffer, wanted, &header);
        if (parsed == IS12_RECORD_SHORT && (int64_t)wanted < remaining &&
            wanted < IS12_HEADER_MAX) {
            wanted = remaining < (int64_t)IS12_HEADER_MAX ? (size_t)remaining
                                                          : IS12_HEADER_MAX;
            if (!is12_read_at(format->device, base + cursor, buffer, wanted))
                goto fail;
            parsed = is12_parse_header(buffer, wanted, &header);
        }
        /* The first record decides whether this is the format at all. */
        if (parsed == IS12_RECORD_BAD && index == 0U) goto fail;
        if (parsed != IS12_RECORD_OK) {
            /* The file (or the part before the certificate) ends inside
             * this header, or the header is damaged: a cut-off or corrupted
             * download keeps the complete members in front of it. */
            stream->truncated = true;
            break;
        }
        if (header.size > (uint64_t)(remaining - (int64_t)header.length)) {
            stream->truncated = true;
            break;
        }
        name_length = is12_utf8_length(buffer + header.path_position,
                                       header.path_units);
        /* name_bytes never exceeds the budget, so this cannot wrap. */
        if (name_length + 1U > IS12_MAX_NAME_BYTES - stream->name_bytes)
            goto fail;
        stream->name_bytes += name_length + 1U;
        xx_mem_zero(&member, sizeof(member));
        if (want_names) {
            member.name = is12_path_to_utf8(buffer + header.path_position,
                                            header.path_units, name_length);
            if (!member.name) goto fail;
        }
        member.header_offset = base + cursor;
        member.header_size = (int64_t)header.length;
        member.data_offset = base + cursor + (int64_t)header.length;
        member.size = (int64_t)header.size;
        member.extractable = true;
        if (!is12_add_member(stream, &member)) {
            if (member.name) xx_mem_free(member.name);
            goto fail;
        }
        cursor += (int64_t)header.length + (int64_t)header.size;
    }
    if (stream->count == 0U) goto fail;
    stream->chain_end = cursor;
    if (stream->truncated) {
        /* Whatever follows the last complete member belongs to it. */
        stream->format_size = available;
    } else {
        stream->format_size = cursor;
        /* A signed launcher ends with its certificate table. */
        if (layout.certificate_offset >= cursor &&
            is12_range_within(available, layout.certificate_offset,
                              layout.certificate_size))
            stream->format_size =
                layout.certificate_offset + layout.certificate_size;
    }
    if (want_names && !is12_make_unique(stream)) goto fail;
    xx_mem_free(buffer);
    *result = stream;
    return true;
fail:
    if (buffer) xx_mem_free(buffer);
    is12_stream_free(stream);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

static bool is12_stem_is(const char *segment, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index) {
        char c = segment[index];
        if (c >= 'a' && c <= 'z') c = (char)(c - 0x20);
        if (!word[index] || c != word[index]) return false;
    }
    return word[stem] == 0;
}

/* Refuses a component Windows would resolve to something else: "." and
 * "..", only dots and spaces, a trailing dot or space (stripped by the
 * file system, which would alias another name), and the device names with
 * or without an extension (CON, AUX, NUL, PRN, COM0-9, LPT0-9, COM/LPT with
 * a superscript digit, CONIN$, CONOUT$, CLOCK$). */
static bool is12_safe_segment(const char *segment, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t index;
    size_t stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)segment[index];
        if (c < 0x20U || c == 0x7FU || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || segment[length - 1U] == '.' ||
        segment[length - 1U] == ' ')
        return false;
    while (stem < length && segment[stem] != '.') ++stem;
    while (stem > 0U && segment[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (is12_stem_is(segment, stem, devices[index])) return false;
    if (stem >= 3U && (is12_stem_is(segment, 3U, "COM") ||
                       is12_stem_is(segment, 3U, "LPT"))) {
        if (stem == 4U && segment[3] >= '0' && segment[3] <= '9') return false;
        /* U+00B9, U+00B2, U+00B3 in UTF-8. */
        if (stem == 5U && (unsigned char)segment[3] == 0xC2U &&
            ((unsigned char)segment[4] == 0xB9U ||
             (unsigned char)segment[4] == 0xB2U ||
             (unsigned char)segment[4] == 0xB3U))
            return false;
    }
    return true;
}

static bool is12_safe_output_name(const char *name) {
    size_t start = 0U;
    size_t at;
    if (!name || !name[0] || name[0] == '/') return false;
    for (at = 0U;; ++at) {
        if (name[at] == '/' || name[at] == 0) {
            if (!is12_safe_segment(name + start, at - start)) return false;
            if (name[at] == 0) return true;
            start = at + 1U;
        }
    }
}

static bool is12_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *is12_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool is12_set_record(xx_archive_record *record,
                            const is12_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_installshield_12_setup_init(xx_installshield_12_setup *archive,
                                    xx_io_device *device,
                                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INSTALLSHIELD_12_SETUP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-installshield-setup");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_installshield_12_setup_check_is_valid;
    archive->format.handle_base_info =
        xx_installshield_12_setup_handle_base_info;
    archive->format.get_format_size = xx_installshield_12_setup_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_installshield_12_setup_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_installshield_12_setup_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_installshield_12_setup_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_installshield_12_setup_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_installshield_12_setup_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_installshield_12_setup_free_archive_records_reading;
    archive->payload_offset = -1;
    archive->payload_end = -1;
    archive->certificate_offset = -1;
}

xx_installshield_12_setup *xx_installshield_12_setup_create(
    xx_io_device *device, int64_t base_address) {
    xx_installshield_12_setup *archive =
        (xx_installshield_12_setup *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_installshield_12_setup_init(archive, device, base_address);
    return archive;
}

void xx_installshield_12_setup_destroy(xx_installshield_12_setup *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_installshield_12_setup_free(xx_installshield_12_setup *archive) {
    if (!archive) return;
    xx_installshield_12_setup_destroy(archive);
    xx_mem_free(archive);
}

bool xx_installshield_12_setup_check_is_valid(Abstractformat *format,
                                              xx_pd_struct *pd) {
    is12_stream *stream;
    if (!is12_parse(format, pd, false, &stream)) return false;
    is12_stream_free(stream);
    return true;
}

bool xx_installshield_12_setup_handle_base_info(Abstractformat *format,
                                                xx_pd_struct *pd) {
    is12_stream *stream;
    xx_installshield_12_setup *archive;
    if (!format) return false;
    if (!is12_parse(format, pd, false, &stream)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_installshield_12_setup *)format;
    archive->number_of_records = stream->count;
    archive->declared_count = stream->declared;
    archive->payload_offset = stream->overlay;
    archive->payload_end = stream->chain_end;
    archive->certificate_offset = stream->certificate_offset;
    archive->certificate_size = stream->certificate_size;
    archive->truncated = stream->truncated;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->format_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_INSTALLSHIELD_12_SETUP_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    is12_stream_free(stream);
    return true;
}

int64_t xx_installshield_12_setup_get_format_size(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_12_setup_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_installshield_12_setup_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_12_setup_handle_base_info(format, pd))
               ? ((xx_installshield_12_setup *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_installshield_12_setup_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    is12_stream *stream;
    xx_archive_record_state *state;
    if (!is12_parse(format, pd, true, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        is12_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = is12_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!is12_copy_options(&state->options, options) ||
        !is12_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_installshield_12_setup_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_installshield_12_setup_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    is12_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (is12_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = is12_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_installshield_12_setup_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    is12_stream *stream;
    is12_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (is12_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!member->extractable || !is12_safe_output_name(member->name))
        return false;
    path_option = is12_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true; /* A dry run: the member is readable. */
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
    result = xx_store_unpack_device_to_file(format->device,
                                            member->data_offset, member->size,
                                            path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_installshield_12_setup_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
