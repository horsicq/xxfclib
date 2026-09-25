/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "SFXSTART" self-extracting executable: a PE32 stub whose overlay is a
 * private chain of stored '*name->size' records.  The layout is in
 * xx_sfxstart.h.
 *
 * Sources.  The layout was worked out from the five corpus kits (all five
 * carry the same 18,944-byte stub); U3's parser for "SFX SFXSTART" (tag
 * check, count + 1 records, name length 1..255, the "->" 0xF0 separator)
 * was read for understanding and served as the extraction oracle.  The code
 * is original.  The member-name rules follow the ones the library's
 * sfx_sbx_extractor reader applies (same project, MIT).
 *
 * Only the MZ header, the PE file header and section table, the file's last
 * four bytes and the payload itself are read; nothing in the stub is
 * executed or emulated.
 *
 * Costs.  check_is_valid() reads the DOS header, the PE headers, the section
 * table and 16 bytes at the overlay; only when those carry the tag does it
 * walk the record headers (three small reads per record, at most 65,536
 * records).  If the overlay does not hold the tag it reads the last four
 * bytes and one 16-byte header more.  Member data is never read until a
 * member is unpacked, and then it is copied in 64 KiB pieces.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfxstart/xx_sfxstart.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef SFXSTART
#define XX_SFXSTART_FILE_TYPE XX_FILE_TYPE_SFXSTART
#else
#define XX_SFXSTART_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* ---------------------------------------------------------------------- */
/* Constants                                                               */

/* Payload. */
#define SFS_HEADER XX_SFXSTART_HEADER_SIZE
#define SFS_RECORD_HEAD 5 /* '*', name length */
#define SFS_RECORD_MID 6  /* "->", data size */
#define SFS_SEPARATOR 3   /* "->" 0xF0 */
#define SFS_MIN_RECORD (SFS_RECORD_HEAD + 1 + SFS_RECORD_MID + SFS_SEPARATOR)
#define SFS_TRAILER_FIXED 10 /* '+', command length, '-', header offset */
#define SFS_MAX_NAME XX_SFXSTART_MAX_NAME
#define SFS_MAX_COMMAND 255U
#define SFS_MAX_RECORDS XX_SFXSTART_MAX_RECORDS

/* Carrier. */
#define SFS_DOS_HEADER 0x40
#define SFS_MAX_LFANEW 0x10000
#define SFS_PE_HEADER 24
#define SFS_OPTIONAL_MIN 64 /* up to and including SizeOfHeaders at 60 */
#define SFS_OPTIONAL_MAX 0x1000
#define SFS_MAX_SECTIONS 96
#define SFS_SECTION_SIZE 40

/* Work limits.  The members are copied, never held in memory whole. */
#define SFS_COPY_CHUNK 0x10000
#define SFS_MAX_NAME_BYTES (16U * 1024U * 1024U)
#define SFS_MAX_RENAMES 16U

static const uint8_t sfs_tag[XX_SFXSTART_TAG_SIZE] = {'S', 'F', 'X', 'S',
                                                      'T', 'A', 'R', 'T'};

/* ---------------------------------------------------------------------- */
/* Byte helpers                                                            */

static uint32_t sfs_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t sfs_le32(const uint8_t *bytes) {
    return sfs_le16(bytes) | (sfs_le16(bytes + 2U) << 16U);
}

static bool sfs_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

typedef struct sfs_record_s {
    int64_t header; /**< Base-relative offset of the '*'. */
    int64_t data;   /**< Base-relative offset of the stored bytes. */
    int64_t size;
    int64_t next;   /**< Base-relative offset behind the separator. */
    uint32_t name_length;
    uint8_t name[SFS_MAX_NAME];
} sfs_record;

/* Read and check the record at @p offset.  @p end is the end of the payload
 * the header declared: the record, its data and its separator must all lie
 * before it. */
static bool sfs_read_record(xx_io_device *device, int64_t base, int64_t end,
                            int64_t offset, sfs_record *record) {
    uint8_t head[SFS_RECORD_HEAD];
    uint8_t body[SFS_MAX_NAME + SFS_RECORD_MID];
    uint8_t separator[SFS_SEPARATOR];
    uint32_t name_length;
    int64_t data, size;

    if (offset < 0 || end < SFS_MIN_RECORD || offset > end - SFS_MIN_RECORD ||
        !sfs_read_at(device, base + offset, head, sizeof(head)) ||
        head[0] != '*')
        return false;
    name_length = sfs_le32(head + 1);
    if (name_length == 0U || name_length > SFS_MAX_NAME ||
        (int64_t)name_length >
            end - offset - (SFS_RECORD_HEAD + SFS_RECORD_MID + SFS_SEPARATOR) ||
        !sfs_read_at(device, base + offset + SFS_RECORD_HEAD, body,
                     (size_t)name_length + SFS_RECORD_MID) ||
        body[name_length] != '-' || body[name_length + 1U] != '>')
        return false;
    size = (int64_t)sfs_le32(body + name_length + 2U);
    data = offset + SFS_RECORD_HEAD + (int64_t)name_length + SFS_RECORD_MID;
    if (size > end - data - SFS_SEPARATOR ||
        !sfs_read_at(device, base + data + size, separator,
                     sizeof(separator)) ||
        separator[0] != '-' || separator[1] != '>' || separator[2] != 0xF0U)
        return false;
    record->header = offset;
    record->data = data;
    record->size = size;
    record->next = data + size + SFS_SEPARATOR;
    record->name_length = name_length;
    xx_rt_memcpy(record->name, body, name_length);
    return true;
}

typedef struct sfs_layout_s {
    int64_t start;     /**< "SFXSTART", base-relative. */
    int64_t end;       /**< start + 16 + payload size. */
    int64_t chain_end; /**< Behind the last separator. */
    int64_t trailer;   /**< The '+', or -1. */
    uint64_t stored_total;
    uint32_t records;
    uint32_t locator;
    char command[SFS_MAX_COMMAND + 1U];
} sfs_layout;

/* Check the 16-byte header at @p at and fill the layout's bounds. */
static bool sfs_read_header(xx_io_device *device, int64_t base,
                            int64_t available, int64_t at,
                            sfs_layout *layout) {
    uint8_t head[SFS_HEADER];
    uint32_t size, last;

    if (at < SFS_DOS_HEADER || available < SFS_HEADER + SFS_MIN_RECORD ||
        at > available - SFS_HEADER - SFS_MIN_RECORD ||
        !sfs_read_at(device, base + at, head, sizeof(head)) ||
        xx_rt_memcmp(head, sfs_tag, sizeof(sfs_tag)) != 0)
        return false;
    size = sfs_le32(head + 8);
    last = sfs_le32(head + 12);
    if (last >= SFS_MAX_RECORDS) return false;
    /* Every record costs at least SFS_MIN_RECORD bytes. */
    if ((uint64_t)size < ((uint64_t)last + 1U) * SFS_MIN_RECORD ||
        (int64_t)size > available - at - SFS_HEADER)
        return false;
    layout->start = at;
    layout->end = at + SFS_HEADER + (int64_t)size;
    layout->records = last + 1U;
    layout->chain_end = -1;
    layout->trailer = -1;
    layout->stored_total = 0U;
    layout->command[0] = 0;
    return true;
}

struct sfs_table_s;
static bool sfs_publish(struct sfs_table_s *table, const sfs_record *record);

/* Walk the declared number of records.  @p table is NULL for a check. */
static bool sfs_walk(xx_io_device *device, int64_t base, sfs_layout *layout,
                     struct sfs_table_s *table, xx_pd_struct *pd) {
    int64_t offset = layout->start + SFS_HEADER;
    uint64_t total = 0U;
    uint32_t index;
    sfs_record record;

    for (index = 0U; index < layout->records; ++index) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!sfs_read_record(device, base, layout->end, offset, &record))
            return false;
        if (table && !sfs_publish(table, &record)) return false;
        total += (uint64_t)record.size;
        offset = record.next;
    }
    layout->chain_end = offset;
    layout->stored_total = total;
    return true;
}

/* The trailer is optional for the overlay route (U3 does not need it
 * either); it is recorded only when it is complete, fills the rest of the
 * payload exactly and points back at the header. */
static bool sfs_read_trailer(xx_io_device *device, int64_t base,
                             sfs_layout *layout) {
    uint8_t head[5];
    uint8_t body[SFS_MAX_COMMAND + 5U];
    uint32_t length;
    int64_t left = layout->end - layout->chain_end;

    layout->trailer = -1;
    layout->command[0] = 0;
    if (layout->chain_end < 0 || left < SFS_TRAILER_FIXED ||
        !sfs_read_at(device, base + layout->chain_end, head, sizeof(head)) ||
        head[0] != '+')
        return false;
    length = sfs_le32(head + 1);
    if (length > SFS_MAX_COMMAND ||
        left != (int64_t)SFS_TRAILER_FIXED + (int64_t)length ||
        !sfs_read_at(device, base + layout->chain_end + 5, body,
                     (size_t)length + 5U) ||
        body[length] != '-' ||
        (int64_t)sfs_le32(body + length + 1U) != layout->start)
        return false;
    xx_rt_memcpy(layout->command, body, length);
    layout->command[length] = 0;
    layout->trailer = layout->chain_end;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Carrier                                                                 */

/* End of the PE image as the loader maps it from the file: SizeOfHeaders and
 * the end of every section's raw data.  0 when the headers are not a PE. */
static int64_t sfs_pe_image_end(xx_io_device *device, int64_t base,
                                int64_t available, uint32_t lfanew) {
    uint8_t pe[SFS_PE_HEADER];
    uint8_t optional[SFS_OPTIONAL_MIN];
    uint8_t sections[SFS_MAX_SECTIONS * SFS_SECTION_SIZE];
    uint32_t count, optional_size, index, magic;
    uint64_t end;
    int64_t table;

    if ((int64_t)lfanew > available - SFS_PE_HEADER - SFS_OPTIONAL_MIN ||
        !sfs_read_at(device, base + lfanew, pe, sizeof(pe)) || pe[0] != 'P' ||
        pe[1] != 'E' || pe[2] != 0U || pe[3] != 0U)
        return 0;
    count = sfs_le16(pe + 6);
    optional_size = sfs_le16(pe + 20);
    if (count == 0U || count > SFS_MAX_SECTIONS ||
        optional_size < SFS_OPTIONAL_MIN || optional_size > SFS_OPTIONAL_MAX)
        return 0;
    table = (int64_t)lfanew + SFS_PE_HEADER + optional_size;
    if (table > available - (int64_t)count * SFS_SECTION_SIZE ||
        !sfs_read_at(device, base + lfanew + SFS_PE_HEADER, optional,
                     sizeof(optional)))
        return 0;
    magic = sfs_le16(optional);
    if (magic != 0x010bU && magic != 0x020bU) return 0;
    if (!sfs_read_at(device, base + table, sections,
                     (size_t)count * SFS_SECTION_SIZE))
        return 0;
    end = sfs_le32(optional + 60);
    for (index = 0U; index < count; ++index) {
        const uint8_t *section = sections + (size_t)index * SFS_SECTION_SIZE;
        uint64_t raw_size = sfs_le32(section + 16);
        uint64_t raw_pointer = sfs_le32(section + 20);
        if (raw_size != 0U && raw_pointer + raw_size > end)
            end = raw_pointer + raw_size;
    }
    return end < (uint64_t)available ? (int64_t)end : 0;
}

/* Header, chain and (optional) trailer at @p at. */
static bool sfs_try(xx_io_device *device, int64_t base, int64_t available,
                    int64_t at, sfs_layout *layout, xx_pd_struct *pd) {
    if (!sfs_read_header(device, base, available, at, layout) ||
        !sfs_walk(device, base, layout, NULL, pd))
        return false;
    (void)sfs_read_trailer(device, base, layout);
    return true;
}

static bool sfs_locate(Abstractformat *format, sfs_layout *layout,
                       xx_pd_struct *pd) {
    uint8_t dos[SFS_DOS_HEADER];
    uint8_t tail[4];
    int64_t total, available, base, image_end = 0, pointer;
    uint32_t lfanew;
    xx_io_device *device;

    if (!format || !format->device || format->base_address < 0) return false;
    device = format->device;
    base = format->base_address;
    total = xx_io_total_size(device);
    if (total < base) return false;
    available = total - base;
    if (available < SFS_DOS_HEADER + SFS_HEADER + SFS_MIN_RECORD ||
        !sfs_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;

    /* The stub's own layout: the payload opens the PE overlay. */
    lfanew = sfs_le32(dos + 0x3c);
    if (lfanew >= SFS_DOS_HEADER && lfanew <= SFS_MAX_LFANEW)
        image_end = sfs_pe_image_end(device, base, available, lfanew);
    if (image_end > 0 &&
        sfs_try(device, base, available, image_end, layout, pd)) {
        layout->locator = XX_SFXSTART_LOCATOR_PE_OVERLAY;
        return true;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    /* Otherwise the trailer's back pointer, which then has to be there in
     * full and end on the last byte. */
    if (!sfs_read_at(device, base + available - 4, tail, sizeof(tail)))
        return false;
    pointer = (int64_t)sfs_le32(tail);
    if (pointer == image_end || pointer < SFS_DOS_HEADER ||
        pointer > available - SFS_HEADER - SFS_MIN_RECORD -
                      SFS_TRAILER_FIXED ||
        !sfs_try(device, base, available, pointer, layout, pd) ||
        layout->end != available || layout->trailer < 0)
        return false;
    layout->locator = XX_SFXSTART_LOCATOR_TRAILER;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member table                                                            */

typedef struct sfs_member_s {
    char *name; /**< UTF-8, '/' separated, reserved characters replaced. */
    char *key;  /**< Case-folded form used to keep duplicates apart. */
    int64_t header_offset;
    int64_t data_offset;
    int64_t size;
    bool unsafe; /**< Listed, never written. */
} sfs_member;

typedef struct sfs_table_s {
    sfs_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    uint32_t *slots;
    size_t slot_count;
    size_t name_bytes;
    sfs_layout layout;
} sfs_table;

static void sfs_table_free(sfs_table *table) {
    size_t index;
    if (!table) return;
    for (index = 0U; index < table->count; ++index) {
        if (table->items[index].name) xx_mem_free(table->items[index].name);
        if (table->items[index].key) xx_mem_free(table->items[index].key);
    }
    if (table->items) xx_mem_free(table->items);
    if (table->slots) xx_mem_free(table->slots);
    xx_mem_free(table);
}

static void sfs_table_free_opaque(void *opaque) {
    sfs_table_free((sfs_table *)opaque);
}

static uint32_t sfs_hash(const char *key) {
    uint32_t hash = 2166136261U;
    while (*key) {
        hash ^= (uint8_t)*key++;
        hash *= 16777619U;
    }
    return hash;
}

static bool sfs_same(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static bool sfs_key_taken(const sfs_table *table, const char *key) {
    size_t mask, slot;
    if (!table->slots) return false;
    mask = table->slot_count - 1U;
    slot = sfs_hash(key) & mask;
    while (table->slots[slot] != 0U) {
        if (sfs_same(table->items[table->slots[slot] - 1U].key, key))
            return true;
        slot = (slot + 1U) & mask;
    }
    return false;
}

/* Insert member @p member's key; the slot array grows at half load. */
static bool sfs_key_insert(sfs_table *table, size_t member) {
    size_t mask, slot, index;
    if (table->slot_count == 0U ||
        (table->count + 1U) * 2U > table->slot_count) {
        size_t grown = table->slot_count ? table->slot_count * 2U : 64U;
        uint32_t *slots = (uint32_t *)xx_mem_alloc(grown * sizeof(uint32_t));
        if (!slots) return false;
        xx_rt_memset(slots, 0, grown * sizeof(uint32_t));
        mask = grown - 1U;
        for (index = 0U; index < table->count; ++index) {
            slot = sfs_hash(table->items[index].key) & mask;
            while (slots[slot] != 0U) slot = (slot + 1U) & mask;
            slots[slot] = (uint32_t)(index + 1U);
        }
        if (table->slots) xx_mem_free(table->slots);
        table->slots = slots;
        table->slot_count = grown;
    }
    mask = table->slot_count - 1U;
    slot = sfs_hash(table->items[member].key) & mask;
    while (table->slots[slot] != 0U) slot = (slot + 1U) & mask;
    table->slots[slot] = (uint32_t)(member + 1U);
    return true;
}

/* Append @p suffix to the heap string @p text. */
static bool sfs_append(char **text, const char *suffix) {
    size_t length = xx_str_len(*text), extra = xx_str_len(suffix);
    char *grown = (char *)xx_mem_alloc(length + extra + 1U);
    if (!grown) return false;
    xx_rt_memcpy(grown, *text, length);
    xx_rt_memcpy(grown + length, suffix, extra + 1U);
    xx_mem_free(*text);
    *text = grown;
    return true;
}

static void sfs_decimal(char *out, char prefix, size_t value, unsigned width) {
    char digits[24];
    unsigned count = 0U;
    size_t position = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    while (count < width && count < sizeof(digits)) digits[count++] = '0';
    out[position++] = prefix;
    while (count) out[position++] = digits[--count];
    out[position] = 0;
}

/* Windows-1252 bytes 0x80..0x9F; 0 marks the five unassigned positions. */
static const uint16_t sfs_cp1252_high[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178};

static uint8_t sfs_upper_ascii(uint8_t value) {
    return (value >= 'a' && value <= 'z') ? (uint8_t)(value - 0x20U) : value;
}

/* Lower-case fold of a Windows-1252 byte, as the file system would see it. */
static uint8_t sfs_fold(uint8_t value) {
    if (value >= 'A' && value <= 'Z') return (uint8_t)(value + 0x20U);
    if (value >= 0xC0U && value <= 0xDEU && value != 0xD7U)
        return (uint8_t)(value + 0x20U);
    if (value == 0x8AU || value == 0x8CU || value == 0x8EU)
        return (uint8_t)(value + 0x10U);
    if (value == 0x9FU) return 0xFFU;
    return value;
}

/* The characters Windows reserves in a name, and control bytes. */
static bool sfs_reserved_char(uint8_t c) {
    return c < 0x20U || c == 0x7FU || c == '<' || c == '>' || c == ':' ||
           c == '"' || c == '|' || c == '?' || c == '*';
}

/* A Windows-1252 byte with no character behind it. */
static bool sfs_unassigned(uint8_t c) {
    return c >= 0x80U && c < 0xA0U && sfs_cp1252_high[c - 0x80U] == 0U;
}

/* COM and LPT take a digit or a superscript one, two or three, which is one
 * Windows-1252 byte in a raw name and two UTF-8 bytes in a listed one. */
static bool sfs_is_port_suffix(const uint8_t *text, size_t length,
                               bool utf8) {
    if (length == 1U && text[0] >= '0' && text[0] <= '9') return true;
    if (!utf8)
        return length == 1U &&
               (text[0] == 0xB9U || text[0] == 0xB2U || text[0] == 0xB3U);
    return length == 2U && text[0] == 0xC2U &&
           (text[1] == 0xB9U || text[1] == 0xB2U || text[1] == 0xB3U);
}

/* CON, PRN, AUX, NUL, COM0-9, LPT0-9 (and the superscript ports), CONIN$,
 * CONOUT$ and CLOCK$, with or without an extension, in any case. */
static bool sfs_is_device(const uint8_t *text, size_t length, bool utf8) {
    static const char *const names[] = {"CON",    "PRN",     "AUX",
                                        "NUL",    "CONIN$",  "CONOUT$",
                                        "CLOCK$"};
    size_t stem = 0U, index, position;
    while (stem < length && text[stem] != '.') ++stem;
    while (stem > 0U && text[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *name = names[index];
        for (position = 0U; position < stem && name[position]; ++position)
            if (sfs_upper_ascii(text[position]) != (uint8_t)name[position])
                break;
        if (position == stem && name[position] == 0) return true;
    }
    if (stem >= 4U && sfs_is_port_suffix(text + 3, stem - 3U, utf8)) {
        uint8_t a = sfs_upper_ascii(text[0]), b = sfs_upper_ascii(text[1]),
                c = sfs_upper_ascii(text[2]);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T'))
            return true;
    }
    return false;
}

/* Build the listed name and its fold key from the raw record name.
 *
 * Both separators become '/'.  A component that is empty, made only of dots
 * and spaces ("." and ".." among them), a device name, or that carries a
 * reserved or control character makes the member unsafe: it is still listed,
 * under a harmless spelling ('_' for each reserved byte, '_' in front of a
 * device name, '_' for each character of a dots-only component, empty
 * components dropped), but it is never written.  A leading separator
 * (absolute path) and a drive letter ("C:") are covered by the empty
 * component and the reserved ':'.  A byte Windows-1252 leaves unassigned
 * becomes '_' in the name and in the key alike, so two such names cannot
 * collide on disk unnoticed. */
static bool sfs_build_name(const uint8_t *raw, size_t length, char **name_out,
                           char **key_out, bool *unsafe_out) {
    uint8_t clean[SFS_MAX_NAME * 2U + 2U];
    uint8_t key_bytes[SFS_MAX_NAME * 2U + 2U];
    size_t clean_length = 0U, key_length = 0U, start = 0U, index;
    bool unsafe = false;
    char *name, *key;
    size_t out = 0U;

    if (length > SFS_MAX_NAME) return false;
    for (index = 0U; index <= length; ++index) {
        size_t part, position, component_start, trimmed;
        bool meaningful = false;
        if (index < length && raw[index] != '\\' && raw[index] != '/') continue;
        part = index - start;
        if (part == 0U) {
            unsafe = true; /* absolute, doubled or trailing separator */
            start = index + 1U;
            continue;
        }
        for (position = start; position < index; ++position)
            if (raw[position] != '.' && raw[position] != ' ') meaningful = true;
        if (!meaningful) unsafe = true;
        if (clean_length != 0U) {
            clean[clean_length++] = '/';
            key_bytes[key_length++] = '/';
        }
        component_start = clean_length;
        if (meaningful && sfs_is_device(raw + start, part, false)) {
            unsafe = true;
            clean[clean_length++] = '_';
        }
        for (position = start; position < index; ++position) {
            uint8_t c = raw[position];
            if (sfs_reserved_char(c)) unsafe = true;
            clean[clean_length++] = (!meaningful || sfs_reserved_char(c) ||
                                     sfs_unassigned(c))
                                        ? (uint8_t)'_'
                                        : c;
        }
        /* Windows drops trailing dots and spaces, so "a.txt." is "A.TXT". */
        trimmed = clean_length;
        while (trimmed > component_start &&
               (clean[trimmed - 1U] == '.' || clean[trimmed - 1U] == ' '))
            --trimmed;
        for (position = component_start; position < trimmed; ++position)
            key_bytes[key_length++] = sfs_fold(clean[position]);
        start = index + 1U;
    }
    if (clean_length == 0U) {
        clean[clean_length++] = '_';
        key_bytes[key_length++] = '_';
        unsafe = true;
    }

    name = (char *)xx_mem_alloc(clean_length * 3U + 1U);
    key = (char *)xx_mem_alloc(key_length + 1U);
    if (!name || !key) {
        if (name) xx_mem_free(name);
        if (key) xx_mem_free(key);
        return false;
    }
    for (index = 0U; index < clean_length; ++index) {
        uint32_t code = clean[index];
        if (code >= 0x80U && code < 0xA0U) code = sfs_cp1252_high[code - 0x80U];
        if (code < 0x80U) {
            name[out++] = (char)code;
        } else if (code < 0x800U) {
            name[out++] = (char)(0xC0U | (code >> 6U));
            name[out++] = (char)(0x80U | (code & 0x3FU));
        } else {
            name[out++] = (char)(0xE0U | (code >> 12U));
            name[out++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
            name[out++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    name[out] = 0;
    /* A NUL cannot occur in the key: it is reserved and became '_'. */
    for (index = 0U; index < key_length; ++index)
        key[index] = (char)key_bytes[index];
    key[key_length] = 0;
    *name_out = name;
    *key_out = key;
    *unsafe_out = unsafe;
    return true;
}

/* Add one record to the table.  Two members that fold to the same name must
 * not overwrite each other: the later one gets its member index appended. */
static bool sfs_publish(sfs_table *table, const sfs_record *record) {
    sfs_member member;
    char suffix[32];
    unsigned attempt;

    if (table->count >= SFS_MAX_RECORDS) return false;
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2U : 16U;
        sfs_member *grown = (sfs_member *)xx_mem_realloc(
            table->items, capacity * sizeof(*grown));
        if (!grown) return false;
        table->items = grown;
        table->capacity = capacity;
    }
    xx_mem_zero(&member, sizeof(member));
    if (!sfs_build_name(record->name, record->name_length, &member.name,
                        &member.key, &member.unsafe))
        return false;
    if (sfs_key_taken(table, member.key)) {
        sfs_decimal(suffix, '.', table->count, 4U);
        if (!sfs_append(&member.name, suffix) ||
            !sfs_append(&member.key, suffix))
            goto fail;
        for (attempt = 1U; sfs_key_taken(table, member.key); ++attempt) {
            if (attempt > SFS_MAX_RENAMES) goto fail;
            sfs_decimal(suffix, '_', attempt, 1U);
            if (!sfs_append(&member.name, suffix) ||
                !sfs_append(&member.key, suffix))
                goto fail;
        }
    }
    table->name_bytes += xx_str_len(member.name) + xx_str_len(member.key);
    if (table->name_bytes > SFS_MAX_NAME_BYTES) goto fail;
    member.header_offset = record->header;
    member.data_offset = record->data;
    member.size = record->size;
    table->items[table->count] = member;
    if (!sfs_key_insert(table, table->count)) {
        table->items[table->count].name = NULL;
        table->items[table->count].key = NULL;
        goto fail;
    }
    ++table->count;
    return true;
fail:
    if (member.name) xx_mem_free(member.name);
    if (member.key) xx_mem_free(member.key);
    return false;
}

static bool sfs_build_table(Abstractformat *format, sfs_table **out,
                            xx_pd_struct *pd) {
    sfs_layout layout;
    sfs_table *table;

    *out = NULL;
    xx_mem_zero(&layout, sizeof(layout));
    if (!sfs_locate(format, &layout, pd)) return false;
    table = (sfs_table *)xx_mem_alloc(sizeof(*table));
    if (!table) return false;
    xx_mem_zero(table, sizeof(*table));
    table->layout = layout;
    if (!sfs_walk(format->device, format->base_address, &table->layout,
                  table, pd) ||
        table->count != layout.records ||
        table->layout.chain_end != layout.chain_end) {
        sfs_table_free(table);
        return false;
    }
    *out = table;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Copying                                                                 */

/* Copy one stored member to @p destination (NULL only reads it through). */
static bool sfs_copy_member(Abstractformat *format, const sfs_member *member,
                            xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t left = member->size;
    int64_t position = format->base_address + member->data_offset;
    size_t chunk;
    bool result = false;

    if (member->size < 0 || member->data_offset < 0) return false;
    chunk = left < SFS_COPY_CHUNK ? (size_t)(left ? left : 1)
                                  : (size_t)SFS_COPY_CHUNK;
    buffer = (uint8_t *)xx_mem_alloc(chunk);
    if (!buffer) return false;
    while (left > 0) {
        size_t amount = left < (int64_t)chunk ? (size_t)left : chunk;
        size_t done = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !sfs_read_at(format->device, position, buffer, amount))
            goto done;
        if (destination) {
            while (done < amount) {
                ssize_t sent = xx_io_write(destination, buffer + done,
                                           amount - done);
                if (sent <= 0 || (size_t)sent > amount - done) goto done;
                done += (size_t)sent;
            }
        }
        position += (int64_t)amount;
        left -= (int64_t)amount;
    }
    result = true;
done:
    xx_mem_free(buffer);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records API helpers                                                     */

static bool sfs_copy_options(xx_list_s *destination,
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

static const xx_var *sfs_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sfs_set_record(Abstractformat *format, xx_archive_record *record,
                           const sfs_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = format->base_address + member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          XX_SFXSTART_METHOD_STORED) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

/* The table already marked unsafe names; this is the last gate before a name
 * reaches the file system. */
static bool sfs_safe_output_name(const char *name) {
    size_t index, start = 0U, length;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    length = xx_str_len(name);
    for (index = 0U; index <= length; ++index) {
        if (index == length || name[index] == '/') {
            size_t part = index - start, position;
            bool meaningful = false;
            if (part == 0U) return false;
            for (position = start; position < index; ++position) {
                uint8_t c = (uint8_t)name[position];
                if (sfs_reserved_char(c)) return false;
                if (c != '.' && c != ' ') meaningful = true;
            }
            if (!meaningful ||
                sfs_is_device((const uint8_t *)name + start, part, true))
                return false;
            start = index + 1U;
        } else if (name[index] == '\\') {
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

static void sfs_vtable_destroy(Abstractformat *format) {
    xx_sfxstart *archive = (xx_sfxstart *)format;
    if (archive && archive->table) {
        sfs_table_free((sfs_table *)archive->table);
        archive->table = NULL;
    }
}

void xx_sfxstart_init(xx_sfxstart *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFXSTART_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sfxstart-sfx");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfxstart_check_is_valid;
    archive->format.handle_base_info = xx_sfxstart_handle_base_info;
    archive->format.get_format_size = xx_sfxstart_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfxstart_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfxstart_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfxstart_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfxstart_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfxstart_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfxstart_free_archive_records_reading;
    archive->format.destroy = sfs_vtable_destroy;
    archive->payload_offset = -1;
    archive->payload_end = -1;
    archive->trailer_offset = -1;
}

xx_sfxstart *xx_sfxstart_create(xx_io_device *device, int64_t base_address) {
    xx_sfxstart *archive = (xx_sfxstart *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfxstart_init(archive, device, base_address);
    return archive;
}

void xx_sfxstart_destroy(xx_sfxstart *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper above. */
    sfs_vtable_destroy(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_sfxstart_free(xx_sfxstart *archive) {
    if (!archive) return;
    xx_sfxstart_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfxstart_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    sfs_layout layout;
    if (!format || (pd && xx_pd_is_stopped(pd))) return false;
    xx_mem_zero(&layout, sizeof(layout));
    return sfs_locate(format, &layout, pd);
}

bool xx_sfxstart_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_sfxstart *archive;
    sfs_table *table = NULL;
    int64_t total;
    if (!format || (pd && xx_pd_is_stopped(pd))) return false;
    archive = (xx_sfxstart *)format;
    if (archive->table) {
        sfs_table_free((sfs_table *)archive->table);
        archive->table = NULL;
    }
    if (!sfs_build_table(format, &table, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        archive->number_of_records = 0U;
        return false;
    }
    archive->table = table;
    archive->number_of_records = table->count;
    archive->payload_offset = table->layout.start;
    archive->payload_end = table->layout.end;
    archive->trailer_offset = table->layout.trailer;
    archive->stored_total = table->layout.stored_total;
    archive->locator = table->layout.locator;
    xx_rt_memcpy(archive->run_command, table->layout.command,
                 sizeof(archive->run_command));
    archive->run_command[sizeof(archive->run_command) - 1U] = 0;
    format->number_of_archive_records = table->count;
    /* The stub plus the payload its header declares. */
    format->format_size = table->layout.end;
    total = xx_io_total_size(format->device);
    if (total - format->base_address > table->layout.end) {
        format->overlay_offset = format->base_address + table->layout.end;
        format->overlay_size = total - format->overlay_offset;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    format->file_type = XX_SFXSTART_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfxstart_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfxstart_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_sfxstart_get_number_of_archive_records(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfxstart_handle_base_info(format, pd))
               ? ((xx_sfxstart *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_sfxstart_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_sfxstart *archive;
    xx_archive_record_state *state;
    sfs_table *table = NULL;
    if (!format || !format->device) return NULL;
    archive = (xx_sfxstart *)format;
    /* The table handle_base_info built is handed over rather than walked a
     * second time; a later listing walks again. */
    if (archive->table) {
        table = (sfs_table *)archive->table;
        archive->table = NULL;
    } else if (!sfs_build_table(format, &table, pd)) {
        return NULL;
    }
    table->index = 0U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sfs_table_free(table);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = table;
    state->free_internal = sfs_table_free_opaque;
    state->total_records = (int64_t)table->count;
    if (table->count == 0U || !sfs_copy_options(&state->options, options) ||
        !sfs_set_record(format, &state->current_record, &table->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sfxstart_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sfxstart_archive_record_move_to_next(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    sfs_table *table;
    if (!format || !state || state->format != format || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    table = (sfs_table *)state->internal_state;
    if (!table || table->index + 1U >= table->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++table->index;
    ++state->current_index;
    state->has_record = sfs_set_record(format, &state->current_record,
                                       &table->items[table->index]);
    return state->has_record;
}

bool xx_sfxstart_unpack_current_archive_record(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    sfs_table *table;
    const sfs_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(table = (sfs_table *)state->internal_state) ||
        table->index >= table->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &table->items[table->index];
    path_option = sfs_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    /* No destination: read the member through, which verifies it. */
    if (!path_option) return sfs_copy_member(format, member, NULL, pd);
    if (member->unsafe || !sfs_safe_output_name(member->name)) return false;
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
        result = sfs_copy_member(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_io_file_remove_a(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfxstart_free_archive_records_reading(Abstractformat *format,
                                              xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
