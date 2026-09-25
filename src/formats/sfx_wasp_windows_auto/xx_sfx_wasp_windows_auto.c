/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * WASP, the Windows Auto Setup Package of Cerious Software (1994).  The
 * layout is in xx_sfx_wasp_windows_auto.h.
 *
 * Written from the structure of the Win16 NE resource table and of the one
 * package in the reference corpus (ThumbsPlus 2.0, whose 19 members are an
 * MS Setup 2 bootstrap: SETUP.EXE, SETUP.LST and KWAJ compressed ".XX_"
 * files).  No code was taken from any other tool.  The executable is only
 * parsed, never run: its MZ and NE headers locate the resource table, the
 * RT_STRING table names and sizes each member and the "FILE" resource of
 * the same number holds its bytes.
 *
 * The members are the files exactly as the stub writes them to its
 * temporary directory.  Expanding the KWAJ streams among them is the job of
 * the program the package runs (and of the KWAJ reader, recursively), not
 * of this container.
 *
 * Hostile input: the resource table is read once, whole, and is at most
 * 64 KiB (both its bounds are u16 offsets); every walk over it is bounded
 * by its length.  A string block is read through a 4 KiB window, the most
 * sixteen length-prefixed strings can occupy.  The member list stops at
 * WASP_MAX_MEMBERS.  The probe (check_is_valid) reads the two headers, the
 * resource table and one string block, allocates only the table and the
 * string window, and wants the first member's bytes inside the file.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_wasp_windows_auto/xx_sfx_wasp_windows_auto.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef SFX_WASP_WINDOWS_AUTO
#define XX_SFX_WASP_WINDOWS_AUTO_FILE_TYPE XX_FILE_TYPE_SFX_WASP_WINDOWS_AUTO
#else
#define XX_SFX_WASP_WINDOWS_AUTO_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define WASP_MZ_HEADER 0x40U
#define WASP_NE_HEADER 0x40U
#define WASP_TYPE_INFO 8U
#define WASP_NAME_INFO 12U
#define WASP_RT_STRING 0x8006U
#define WASP_INTEGER_ID 0x8000U
/* Windows' own resource compilers use shifts of 4..9; anything wider than
 * this would place a u16 offset beyond 4 GiB. */
#define WASP_MAX_SHIFT 16U
#define WASP_STRINGS_PER_BLOCK 16U
/* Sixteen strings of a length byte and at most 255 characters. */
#define WASP_BLOCK_WINDOW (WASP_STRINGS_PER_BLOCK * 256U)
#define WASP_MAX_MEMBERS 1024U
/* Longest name a string can carry ("N,0" leaves 253 bytes), plus the
 * "_<id>" a duplicate gets and the terminator. */
#define WASP_NAME_CAPACITY 272U
/* Segment tables longer than this are not walked for the format size. */
#define WASP_MAX_SEGMENTS 4096U

typedef struct wasp_table_s {
    uint8_t *data;       /**< The whole resource table. */
    uint32_t length;
    uint32_t shift;
    uint32_t file_entries; /**< Offset of the first "FILE" name info. */
    uint32_t file_count;
    uint32_t string_entries;
    uint32_t string_count;
    uint32_t ne_offset;
    uint8_t ne_header[WASP_NE_HEADER];
    int64_t base;
    int64_t size;        /**< Bytes from the base address to the end. */
} wasp_table;

typedef struct wasp_strings_s {
    uint32_t block;      /**< Loaded block number, 0 when none. */
    bool present;        /**< The loaded block exists at all. */
    uint16_t offset[WASP_STRINGS_PER_BLOCK];
    uint8_t length[WASP_STRINGS_PER_BLOCK];
    uint8_t data[WASP_BLOCK_WINDOW];
} wasp_strings;

typedef struct wasp_member_s {
    char name[WASP_NAME_CAPACITY];
    uint32_t id;
    uint32_t size;
    uint32_t hash;
    int64_t data_offset; /**< Absolute device offset. */
    int64_t extent;      /**< Resource bytes, alignment padding included. */
    bool run;
    bool extractable;
} wasp_member;

typedef struct wasp_list_s {
    wasp_member *members;
    size_t count;
    size_t index;
} wasp_list;

static uint32_t wasp_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t wasp_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool wasp_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static char wasp_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static void wasp_table_free(wasp_table *table) {
    if (table && table->data) {
        xx_mem_free(table->data);
        table->data = NULL;
    }
}

/* ---------------------------------------------------------------------- */
/* NE headers and the resource table                                       */

/* True when the length-prefixed type name at @p at spells "FILE" in any
 * case, the way Windows compares resource names. */
static bool wasp_type_is_file(const wasp_table *table, uint32_t at) {
    static const char word[] = "FILE";
    uint32_t index;
    if (at >= table->length || table->data[at] != 4U ||
        (uint64_t)at + 1U + 4U > table->length)
        return false;
    for (index = 0U; index < 4U; ++index)
        if (wasp_upper((char)table->data[at + 1U + index]) != word[index])
            return false;
    return true;
}

/* MZ header, NE header, then the resource table read whole.  Both bounds
 * of the table are u16 offsets from the NE header, so it is < 64 KiB. */
static bool wasp_load_table(Abstractformat *format, wasp_table *table) {
    uint8_t mz[WASP_MZ_HEADER];
    int64_t total;
    uint32_t resource_table, resident_names, at;
    bool terminated = false;
    xx_mem_zero(table, sizeof(*table));
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    table->base = format->base_address;
    table->size = total - format->base_address;
    if (table->size < (int64_t)(WASP_MZ_HEADER + WASP_NE_HEADER) ||
        !wasp_read_at(format->device, table->base, mz, sizeof(mz)) ||
        mz[0] != 'M' || mz[1] != 'Z')
        return false;
    table->ne_offset = wasp_le32(mz + 0x3CU);
    if (table->ne_offset < WASP_MZ_HEADER ||
        (int64_t)table->ne_offset + (int64_t)WASP_NE_HEADER > table->size ||
        !wasp_read_at(format->device, table->base + table->ne_offset,
                      table->ne_header, WASP_NE_HEADER) ||
        table->ne_header[0] != 'N' || table->ne_header[1] != 'E')
        return false;
    resource_table = wasp_le16(table->ne_header + 0x24U);
    resident_names = wasp_le16(table->ne_header + 0x26U);
    /* Shift, one type block of a single name info and the terminator. */
    if (resource_table < WASP_NE_HEADER ||
        resident_names < resource_table + 2U + WASP_TYPE_INFO +
                             WASP_NAME_INFO + 2U ||
        (int64_t)table->ne_offset + (int64_t)resident_names > table->size)
        return false;
    table->length = resident_names - resource_table;
    table->data = (uint8_t *)xx_mem_alloc(table->length);
    if (!table->data) return false;
    if (!wasp_read_at(format->device,
                      table->base + table->ne_offset + resource_table,
                      table->data, table->length))
        goto fail;
    table->shift = wasp_le16(table->data);
    if (table->shift > WASP_MAX_SHIFT) goto fail;
    /* Every pass moves at least one type info forward. */
    at = 2U;
    while ((uint64_t)at + 2U <= table->length) {
        uint32_t type = wasp_le16(table->data + at), count;
        if (type == 0U) {
            terminated = true;
            break;
        }
        if ((uint64_t)at + WASP_TYPE_INFO > table->length) goto fail;
        count = wasp_le16(table->data + at + 2U);
        if ((uint64_t)at + WASP_TYPE_INFO +
                (uint64_t)count * WASP_NAME_INFO > table->length)
            goto fail;
        if (type == WASP_RT_STRING && table->string_count == 0U) {
            table->string_entries = at + WASP_TYPE_INFO;
            table->string_count = count;
        } else if ((type & WASP_INTEGER_ID) == 0U &&
                   table->file_count == 0U && wasp_type_is_file(table, type)) {
            table->file_entries = at + WASP_TYPE_INFO;
            table->file_count = count;
        }
        at += WASP_TYPE_INFO + count * WASP_NAME_INFO;
    }
    if (!terminated || table->file_count == 0U || table->string_count == 0U)
        goto fail;
    return true;
fail:
    wasp_table_free(table);
    return false;
}

/* The name info of type list (@p entries, @p count) with integer id @p id:
 * its offset and length in bytes from the base address. */
static bool wasp_find(const wasp_table *table, uint32_t entries,
                      uint32_t count, uint32_t id, int64_t *offset,
                      int64_t *length) {
    uint32_t index, want = WASP_INTEGER_ID | id;
    if (id == 0U || id >= WASP_INTEGER_ID) return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *info = table->data + entries + index * WASP_NAME_INFO;
        if (wasp_le16(info + 6U) != want) continue;
        *offset = (int64_t)wasp_le16(info) << table->shift;
        *length = (int64_t)wasp_le16(info + 2U) << table->shift;
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* String table                                                            */

/* Load block @p block (strings (block - 1) * 16 .. + 15).  A block that is
 * missing, or cut short by the end of the file, leaves the strings it does
 * not hold empty, which is what LoadString reports for them too. */
static bool wasp_load_block(Abstractformat *format, const wasp_table *table,
                            wasp_strings *strings, uint32_t block) {
    int64_t offset, length;
    size_t window, position = 0U;
    uint32_t index;
    if (strings->block == block) return strings->present;
    xx_mem_zero(strings->offset, sizeof(strings->offset));
    xx_mem_zero(strings->length, sizeof(strings->length));
    strings->block = block;
    strings->present = false;
    if (!wasp_find(table, table->string_entries, table->string_count, block,
                   &offset, &length) ||
        offset >= table->size || length <= 0)
        return false;
    window = length < (int64_t)WASP_BLOCK_WINDOW ? (size_t)length
                                                 : WASP_BLOCK_WINDOW;
    if ((int64_t)window > table->size - offset)
        window = (size_t)(table->size - offset);
    if (!wasp_read_at(format->device, table->base + offset, strings->data,
                      window))
        return false;
    for (index = 0U; index < WASP_STRINGS_PER_BLOCK; ++index) {
        size_t size;
        if (position >= window) break;
        size = strings->data[position];
        if (position + 1U + size > window) break;
        strings->offset[index] = (uint16_t)(position + 1U);
        strings->length[index] = (uint8_t)size;
        position += 1U + size;
    }
    strings->present = true;
    return true;
}

/* "<name>,<decimal size>[*]": the size is everything after the last comma.
 * The name keeps printable ASCII; other bytes of 0x80 and up become '_'. */
static bool wasp_parse_entry(const uint8_t *text, size_t length,
                             wasp_member *member) {
    size_t comma = length, index, digits = 0U, name_length;
    uint64_t value = 0U;
    bool meaningful = false;
    if (length < 3U) return false;
    for (index = length; index-- > 0U;) {
        if (text[index] == ',') {
            comma = index;
            break;
        }
    }
    if (comma == length || comma == 0U) return false;
    name_length = comma;
    for (index = comma + 1U; index < length; ++index) {
        uint8_t c = text[index];
        if (c >= '0' && c <= '9') {
            if (++digits > 10U) return false;
            value = value * 10U + (uint64_t)(c - '0');
            continue;
        }
        if (c == '*' && index + 1U == length && digits != 0U) {
            member->run = true;
            break;
        }
        return false;
    }
    if (digits == 0U || value > UINT32_MAX ||
        name_length + 1U > WASP_NAME_CAPACITY)
        return false;
    for (index = 0U; index < name_length; ++index) {
        uint8_t c = text[index];
        if (c < 0x20U || c == 0x7FU) return false;
        if (c != ' ') meaningful = true;
        member->name[index] = c >= 0x80U ? '_' : (char)c;
    }
    if (!meaningful) return false;
    member->name[name_length] = 0;
    member->size = (uint32_t)value;
    return true;
}

/* Member @p id: its string and its FILE resource.  False ends the list. */
static bool wasp_member_at(Abstractformat *format, const wasp_table *table,
                           wasp_strings *strings, uint32_t id,
                           wasp_member *member) {
    uint32_t slot = id % WASP_STRINGS_PER_BLOCK;
    int64_t offset, length;
    if (id == 0U || id >= WASP_INTEGER_ID) return false;
    xx_mem_zero(member, sizeof(*member));
    if (!wasp_load_block(format, table, strings,
                         id / WASP_STRINGS_PER_BLOCK + 1U) ||
        strings->length[slot] == 0U ||
        !wasp_parse_entry(strings->data + strings->offset[slot],
                          strings->length[slot], member) ||
        !wasp_find(table, table->file_entries, table->file_count, id, &offset,
                   &length) ||
        (int64_t)member->size > length)
        return false;
    member->id = id;
    member->data_offset = table->base + offset;
    member->extent = length;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member names                                                            */

static uint32_t wasp_hash(const char *name) {
    uint32_t hash = 2166136261U;
    for (; *name; ++name) {
        hash ^= (uint8_t)wasp_upper(*name);
        hash *= 16777619U;
    }
    return hash;
}

static bool wasp_same_name(const char *left, const char *right) {
    for (;; ++left, ++right) {
        if (wasp_upper(*left) != wasp_upper(*right)) return false;
        if (!*left) return true;
    }
}

static bool wasp_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || wasp_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* A single path component Windows and POSIX both take literally: no
 * separators, drive colons, wildcards or control bytes, not only dots and
 * spaces, no leading or trailing space, no trailing dot, and no device name
 * (CON, NUL, COM1, LPT1.TXT, CONIN$, ...) with or without an extension. */
static bool wasp_safe_name(const char *name) {
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
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || name[0] == ' ' || name[length - 1U] == ' ' ||
        name[length - 1U] == '.')
        return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (wasp_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((wasp_upper(name[0]) == 'C' && wasp_upper(name[1]) == 'O' &&
          wasp_upper(name[2]) == 'M') ||
         (wasp_upper(name[0]) == 'L' && wasp_upper(name[1]) == 'P' &&
          wasp_upper(name[2]) == 'T')))
        return false;
    return true;
}

static bool wasp_name_taken(const wasp_member *members, size_t count,
                            const char *name, uint32_t hash) {
    size_t index;
    for (index = 0U; index < count; ++index)
        if (members[index].hash == hash &&
            wasp_same_name(members[index].name, name))
            return true;
    return false;
}

static size_t wasp_put_decimal(char *out, uint32_t value) {
    char digits[10];
    size_t count = 0U, index;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    for (index = 0U; index < count; ++index)
        out[index] = digits[count - 1U - index];
    return count;
}

/* Member @p index gets a name no earlier member has (compared ASCII
 * case-insensitively): a duplicate becomes "<stem>_<id><.ext>".  A name
 * still taken after that, or one that is unsafe, is listed but never
 * extracted. */
static void wasp_finish_name(wasp_member *members, size_t index) {
    wasp_member *member = &members[index];
    member->hash = wasp_hash(member->name);
    if (wasp_name_taken(members, index, member->name, member->hash)) {
        char fresh[WASP_NAME_CAPACITY];
        size_t length = xx_str_len(member->name), insert = length, at;
        while (insert > 0U && member->name[insert - 1U] != '.') --insert;
        insert = insert > 0U ? insert - 1U : length;
        xx_mem_copy(fresh, member->name, insert);
        at = insert;
        fresh[at++] = '_';
        at += wasp_put_decimal(fresh + at, member->id);
        if (at + (length - insert) + 1U > sizeof(fresh)) {
            member->extractable = false;
            return;
        }
        xx_mem_copy(fresh + at, member->name + insert, length - insert);
        at += length - insert;
        fresh[at] = 0;
        xx_mem_copy(member->name, fresh, at + 1U);
        member->hash = wasp_hash(member->name);
        if (wasp_name_taken(members, index, member->name, member->hash)) {
            member->extractable = false;
            return;
        }
    }
    member->extractable = wasp_safe_name(member->name);
}

/* ---------------------------------------------------------------------- */
/* Whole package                                                           */

/* Fill @p list (when given) with every member; returns the member count,
 * 0 when the file is not a WASP package. */
static size_t wasp_collect(Abstractformat *format, const wasp_table *table,
                           wasp_list *list, uint64_t *total_unpacked,
                           int32_t *run_index) {
    wasp_strings *strings;
    wasp_member member;
    size_t count = 0U, capacity;
    uint32_t id;
    if (total_unpacked) *total_unpacked = 0U;
    if (run_index) *run_index = -1;
    capacity = table->file_count < WASP_MAX_MEMBERS ? table->file_count
                                                    : WASP_MAX_MEMBERS;
    strings = (wasp_strings *)xx_mem_calloc(1U, sizeof(*strings));
    if (!strings) return 0U;
    if (list) {
        list->members =
            (wasp_member *)xx_mem_calloc(capacity, sizeof(*list->members));
        if (!list->members) {
            xx_mem_free(strings);
            return 0U;
        }
    }
    for (id = 1U; count < capacity; ++id) {
        if (!wasp_member_at(format, table, strings, id, &member)) break;
        /* As in the probe: the first member must be complete. */
        if (id == 1U &&
            (member.data_offset - table->base > table->size ||
             (int64_t)member.size >
                 table->size - (member.data_offset - table->base)))
            break;
        if (total_unpacked) *total_unpacked += member.size;
        if (run_index && member.run && *run_index < 0)
            *run_index = (int32_t)count;
        if (list) {
            list->members[count] = member;
            wasp_finish_name(list->members, count);
        }
        ++count;
    }
    xx_mem_free(strings);
    if (list) {
        list->count = count;
        if (count == 0U) {
            xx_mem_free(list->members);
            list->members = NULL;
        }
    }
    return count;
}

static void wasp_extend(int64_t *end, int64_t candidate) {
    if (candidate > *end) *end = candidate;
}

/* The end of the NE image: headers, name and entry tables, segments with
 * their relocation records, and every resource. */
static int64_t wasp_image_end(Abstractformat *format,
                              const wasp_table *table) {
    const uint8_t *ne = table->ne_header;
    int64_t end, ne_at = (int64_t)table->ne_offset;
    uint32_t index, type_at = 2U;
    uint32_t segments = wasp_le16(ne + 0x1CU);
    uint32_t segment_table = wasp_le16(ne + 0x22U);
    uint32_t segment_shift = wasp_le16(ne + 0x32U);
    end = ne_at + WASP_NE_HEADER;
    wasp_extend(&end, ne_at + (int64_t)wasp_le16(ne + 0x26U));
    wasp_extend(&end, ne_at + (int64_t)wasp_le16(ne + 0x28U) +
                          2 * (int64_t)wasp_le16(ne + 0x1EU));
    wasp_extend(&end, ne_at + (int64_t)wasp_le16(ne + 0x04U) +
                          (int64_t)wasp_le16(ne + 0x06U));
    if (wasp_le16(ne + 0x20U) != 0U)
        wasp_extend(&end, (int64_t)wasp_le32(ne + 0x2CU) +
                              (int64_t)wasp_le16(ne + 0x20U));
    if (segment_shift == 0U) segment_shift = 9U;
    if (segments <= WASP_MAX_SEGMENTS && segment_shift <= WASP_MAX_SHIFT) {
        for (index = 0U; index < segments; ++index) {
            uint8_t entry[8];
            uint8_t word[2];
            int64_t start, stop;
            uint32_t sector, length;
            if (!wasp_read_at(format->device,
                              table->base + ne_at + segment_table + index * 8U,
                              entry, sizeof(entry)))
                break;
            sector = wasp_le16(entry);
            if (sector == 0U) continue;
            length = wasp_le16(entry + 2U);
            if (length == 0U) length = 0x10000U;
            start = (int64_t)sector << segment_shift;
            stop = start + (int64_t)length;
            if ((wasp_le16(entry + 4U) & 0x0100U) != 0U &&
                stop + 2 <= table->size &&
                wasp_read_at(format->device, table->base + stop, word,
                             sizeof(word)))
                stop += 2 + 8 * (int64_t)wasp_le16(word);
            wasp_extend(&end, stop);
        }
    } else {
        end = table->size;
    }
    /* The load table already proved every type block lies in the table. */
    for (;;) {
        uint32_t type = wasp_le16(table->data + type_at), count;
        if (type == 0U) break;
        count = wasp_le16(table->data + type_at + 2U);
        for (index = 0U; index < count; ++index) {
            const uint8_t *info = table->data + type_at + WASP_TYPE_INFO +
                                  index * WASP_NAME_INFO;
            wasp_extend(&end, ((int64_t)wasp_le16(info) << table->shift) +
                                  ((int64_t)wasp_le16(info + 2U)
                                   << table->shift));
        }
        type_at += WASP_TYPE_INFO + count * WASP_NAME_INFO;
    }
    return end < table->size ? end : table->size;
}

static bool wasp_probe(Abstractformat *format) {
    wasp_table table;
    wasp_strings *strings;
    wasp_member member;
    bool result;
    if (!wasp_load_table(format, &table)) return false;
    strings = (wasp_strings *)xx_mem_calloc(1U, sizeof(*strings));
    /* The first member's declared bytes must lie inside the file. */
    result = strings && wasp_member_at(format, &table, strings, 1U, &member) &&
             member.data_offset - table.base <= table.size &&
             (int64_t)member.size <=
                 table.size - (member.data_offset - table.base);
    if (strings) xx_mem_free(strings);
    wasp_table_free(&table);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static void wasp_list_free(void *opaque) {
    wasp_list *list = (wasp_list *)opaque;
    if (!list) return;
    if (list->members) xx_mem_free(list->members);
    xx_mem_free(list);
}

static bool wasp_copy_options(xx_list_s *destination,
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

static bool wasp_set_record(xx_archive_record *record,
                            const wasp_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->data_offset;
    record->header_size = 0;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        member->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        member->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        0U) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    return !member->run ||
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          "run after extraction");
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_sfx_wasp_windows_auto_init(xx_sfx_wasp_windows_auto *archive,
                                   xx_io_device *device,
                                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_WASP_WINDOWS_AUTO_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-wasp-setup-package");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_wasp_windows_auto_check_is_valid;
    archive->format.handle_base_info =
        xx_sfx_wasp_windows_auto_handle_base_info;
    archive->format.get_format_size = xx_sfx_wasp_windows_auto_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_wasp_windows_auto_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_wasp_windows_auto_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_wasp_windows_auto_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_wasp_windows_auto_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_wasp_windows_auto_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_wasp_windows_auto_free_archive_records_reading;
    archive->run_index = -1;
}

xx_sfx_wasp_windows_auto *xx_sfx_wasp_windows_auto_create(
    xx_io_device *device, int64_t base_address) {
    xx_sfx_wasp_windows_auto *archive =
        (xx_sfx_wasp_windows_auto *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_wasp_windows_auto_init(archive, device, base_address);
    return archive;
}

void xx_sfx_wasp_windows_auto_destroy(xx_sfx_wasp_windows_auto *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_wasp_windows_auto_free(xx_sfx_wasp_windows_auto *archive) {
    if (!archive) return;
    xx_sfx_wasp_windows_auto_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_wasp_windows_auto_check_is_valid(Abstractformat *format,
                                             xx_pd_struct *pd) {
    (void)pd;
    return wasp_probe(format);
}

bool xx_sfx_wasp_windows_auto_handle_base_info(Abstractformat *format,
                                               xx_pd_struct *pd) {
    xx_sfx_wasp_windows_auto *archive;
    wasp_table table;
    uint64_t total_unpacked = 0U;
    int32_t run_index = -1;
    size_t count;
    (void)pd;
    if (!format || !wasp_load_table(format, &table)) return false;
    count = wasp_collect(format, &table, NULL, &total_unpacked, &run_index);
    if (count == 0U) {
        wasp_table_free(&table);
        return false;
    }
    archive = (xx_sfx_wasp_windows_auto *)format;
    archive->number_of_records = count;
    archive->total_unpacked = total_unpacked;
    archive->ne_offset = table.ne_offset;
    archive->alignment_shift = table.shift;
    archive->file_resources = table.file_count;
    archive->run_index = run_index;
    format->number_of_archive_records = count;
    format->format_size = wasp_image_end(format, &table);
    format->is_valid = true;
    format->base_info_handled = true;
    wasp_table_free(&table);
    return true;
}

int64_t xx_sfx_wasp_windows_auto_get_format_size(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_wasp_windows_auto_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sfx_wasp_windows_auto_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_wasp_windows_auto_handle_base_info(format, pd))
               ? ((xx_sfx_wasp_windows_auto *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_sfx_wasp_windows_auto_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    wasp_table table;
    wasp_list *list;
    xx_archive_record_state *state;
    (void)pd;
    if (!format || !wasp_load_table(format, &table)) return NULL;
    list = (wasp_list *)xx_mem_calloc(1U, sizeof(*list));
    if (!list) {
        wasp_table_free(&table);
        return NULL;
    }
    (void)wasp_collect(format, &table, list, NULL, NULL);
    wasp_table_free(&table);
    if (list->count == 0U) {
        wasp_list_free(list);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        wasp_list_free(list);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = list;
    state->free_internal = wasp_list_free;
    state->total_records = list->count;
    if (!wasp_copy_options(&state->options, options) ||
        !wasp_set_record(&state->current_record, &list->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfx_wasp_windows_auto_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sfx_wasp_windows_auto_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    wasp_list *list;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(list = (wasp_list *)state->internal_state) ||
        list->index + 1U >= list->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++list->index;
    if (!wasp_set_record(&state->current_record,
                         &list->members[list->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_sfx_wasp_windows_auto_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    wasp_list *list;
    const wasp_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *output;
    size_t base_length;
    int64_t size;
    bool overwrite, result, created = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record ||
        !(list = (wasp_list *)state->internal_state) ||
        list->index >= list->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &list->members[list->index];
    size = xx_io_total_size(format->device);
    /* The declared bytes must all be present. */
    if (member->data_offset < 0 || size < member->data_offset ||
        size - member->data_offset < (int64_t)member->size)
        return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->size > xx_var_get_u64(option))
        return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    /* No destination: the member is complete, which is all there is to
     * check for stored data. */
    if (!option) return true;
    if (!member->extractable) return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) {
        if (owned_base) xx_str_free(owned_base);
        return false;
    }
    base_length = xx_str_len(base);
    path = (base_length != 0U && base[base_length - 1U] != '/' &&
            base[base_length - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (owned_base) xx_str_free(owned_base);
    if (!path) return false;
    if (!xx_store_create_dirs_a(path, false)) {
        xx_str_free(path);
        return false;
    }
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    /* Without the overwrite option an existing file is never replaced. */
    output = xx_io_file_open(path, overwrite ? "wb" : "wbx");
    if (!output) {
        xx_str_free(path);
        return false;
    }
    /* Only a file this call opened is ever deleted again. */
    created = true;
    result = xx_store_unpack_device(format->device, member->data_offset,
                                    (int64_t)member->size, output, pd);
    if (xx_io_close(output) != 0) result = false;
    if (!result && created) xx_rt_remove(path);
    xx_str_free(path);
    return result;
}

void xx_sfx_wasp_windows_auto_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
