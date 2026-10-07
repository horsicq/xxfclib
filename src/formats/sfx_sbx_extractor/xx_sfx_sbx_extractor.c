/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SBX self-extractor: the "SBSETUP" installers whose payload is a bare chain
 * of "SB1\0" LZHUF records behind a PE32 or NE stub.  The record layout is in
 * xx_sfx_sbx_extractor.h.
 *
 * Sources.  The container rules (the record layout, the date-before-time
 * order, the chain closing on the last byte, the PE-overlay start and the NE
 * tag search that must survive the stub's own stray tag) follow XArchive
 * installers/xsbx.{h,cpp} (MIT, Copyright (c) 2026 hors<horsicq@gmail.com>);
 * the code below is a C rewrite, not a transliteration.  The codec is the
 * library's own xx_lzhuf_decode_memory(), the same parameter set ZTC uses.
 * U3 served as the extraction oracle only.
 *
 * Only the MZ header, the PE file header and section table (or, for NE, the
 * two signature bytes) are read to find the payload; nothing in the stub is
 * executed or emulated.
 *
 * Costs.  For a PE, check_is_valid() reads the DOS header, the PE headers,
 * the section table and four bytes at the overlay; when those are the tag it
 * walks the record headers, otherwise it searches the first 64 KiB of the
 * overlay for the tag.  For an NE it searches the first MiB for the tag.
 * Both searches walk a bounded number of candidates.  The chain must close on
 * the end of the file, or on a PE's Authenticode certificate table that
 * closes it, after at most 4 KiB of zero padding.  No member is decoded until
 * it is unpacked.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/sfx_sbx_extractor/xx_sfx_sbx_extractor.h"

#include "xxfclib/algo/lzhuf/xx_lzhuf.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef SFX_SBX_EXTRACTOR
#define XX_SFX_SBX_EXTRACTOR_FILE_TYPE XX_FILE_TYPE_SFX_SBX_EXTRACTOR
#else
#define XX_SFX_SBX_EXTRACTOR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* ---------------------------------------------------------------------- */
/* Constants                                                               */

/* Record. */
#define SBX_HEAD 0x0e     /* tag, record size, date, time, attr, name length */
#define SBX_OVERHEAD XX_SFX_SBX_EXTRACTOR_OVERHEAD
#define SBX_MIN_RECORD (SBX_OVERHEAD + 1) /* a name is never empty */
#define SBX_MAX_NAME 255

/* Carrier. */
#define SBX_DOS_HEADER 0x40
#define SBX_MAX_LFANEW 0x10000
#define SBX_PE_HEADER 24
#define SBX_OPTIONAL_MIN 64 /* up to and including SizeOfHeaders at 60 */
#define SBX_OPTIONAL_MAX 0x1000
#define SBX_OPTIONAL_READ 152 /* through PE32+ data directory 4 */
#define SBX_MAX_SECTIONS 96
#define SBX_SECTION_SIZE 40
#define SBX_NE_HEADER 0x40

/* NE search.  The stubs are about 13 KiB, so the first record sits well
 * inside the first MiB; the window only keeps the probe from reading a large
 * NE file end to end.  A stray tag in the stub is normal (all four NE
 * samples have one), a file engineered to hold thousands of them is not. */
#define SBX_NE_SCAN_LIMIT INT64_C(0x100000)
#define SBX_MAX_CANDIDATES 256U
/* PE: when the chain does not open the computed overlay (a section table
 * that does not account for the whole image, alignment padding), the tag is
 * searched for in this much of the overlay only. */
#define SBX_PE_SCAN_WINDOW INT64_C(0x10000)
/* Zero padding tolerated after the last record. */
#define SBX_MAX_TAIL 4096

/* Ceilings.  These are small desktop installers (the largest member of the
 * reference corpus is 380,416 bytes); the limits only stop a corrupt chain
 * from asking for unbounded work or memory.  A member is decoded in memory,
 * so SBX_MAX_MEMBER is also the largest buffer one unpack allocates. */
#define SBX_MAX_MEMBERS 16384U
#define SBX_STEP_BUDGET 65536U /* record headers read per locate, in total */
#define SBX_MAX_MEMBER INT64_C(0x4000000)
/* An LZHUF match symbol costs at least 1 + 3 + 6 bits and yields at most 60
 * bytes, so no stream expands by more than 48 to 1; a record that claims more
 * is not a record. */
#define SBX_MAX_RATIO 48
#define SBX_RATIO_SLACK 64
#define SBX_MAX_NAME_BYTES (16U * 1024U * 1024U)
#define SBX_MAX_INPUT_PER_BYTE 4

static const uint8_t sbx_tag[XX_SFX_SBX_EXTRACTOR_TAG_SIZE] = {'S', 'B', '1',
                                                              0x00};

/* ---------------------------------------------------------------------- */
/* Byte helpers                                                            */

static uint32_t sbx_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static bool sbx_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    const size_t io_capacity = xx_get_file_buffer_size();
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t request = size - done;
        if (request > io_capacity) request = io_capacity;
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

typedef struct sbx_record_s {
    int64_t header;   /**< Base-relative offset of the tag. */
    int64_t size;     /**< The whole record. */
    int64_t data;     /**< Base-relative offset of the packed bytes. */
    int64_t packed;
    int64_t unpacked;
    uint32_t dos_date;
    uint32_t dos_time;
    uint8_t attributes;
    uint8_t name_length;
    uint8_t name[SBX_MAX_NAME];
} sbx_record;

/* Read and check the record at @p offset of a carrier of @p available bytes.
 * Everything that makes the chain advance is checked here: the tag, a name,
 * a record size that covers its own overhead and stays inside the file. */
static bool sbx_read_record(xx_io_device *device, int64_t base,
                            int64_t available, int64_t offset,
                            sbx_record *record) {
    uint8_t head[SBX_HEAD];
    uint8_t tail[SBX_MAX_NAME + 4];
    int64_t size, packed, unpacked;
    uint32_t name_length;

    if (offset < 0 || available < SBX_MIN_RECORD ||
        offset > available - SBX_MIN_RECORD ||
        !sbx_read_at(device, base + offset, head, sizeof(head)) ||
        xx_rt_memcmp(head, sbx_tag, sizeof(sbx_tag)) != 0)
        return false;
    /* Signed on purpose: a size with the top bit set is corrupt, not a
     * two-gigabyte record. */
    size = (int64_t)(int32_t)xx_data_get_u32(head + 4, 4, 0, false);
    name_length = head[0x0d];
    if (name_length == 0U || size < (int64_t)name_length + SBX_OVERHEAD ||
        size > available - offset ||
        !sbx_read_at(device, base + offset + SBX_HEAD, tail,
                     (size_t)name_length + 4U))
        return false;
    unpacked = (int64_t)(int32_t)xx_data_get_u32(tail + name_length, 4, 0, false);
    packed = size - (int64_t)name_length - SBX_OVERHEAD;
    if (unpacked < 0 || unpacked > SBX_MAX_MEMBER) return false;
    /* Bytes to produce and nothing to produce them from is a corrupt
     * record, not an empty file; neither is an impossible expansion. */
    if (packed == 0 && unpacked != 0) return false;
    if (unpacked > packed * SBX_MAX_RATIO + SBX_RATIO_SLACK) return false;
    if (record) {
        record->header = offset;
        record->size = size;
        record->data = offset + SBX_OVERHEAD + (int64_t)name_length;
        record->packed = packed;
        record->unpacked = unpacked;
        record->dos_date = sbx_le16(head + 8);
        record->dos_time = sbx_le16(head + 10);
        record->attributes = head[0x0c];
        record->name_length = (uint8_t)name_length;
        xx_rt_memcpy(record->name, tail, name_length);
    }
    return true;
}

struct sbx_table_s;
static bool sbx_publish(struct sbx_table_s *table, const sbx_record *record);

/* True when [@p offset, @p limit) is at most SBX_MAX_TAIL zero bytes: the
 * padding a signing tool or a copy to an aligned medium leaves behind. */
static bool sbx_zero_tail(xx_io_device *device, int64_t base, int64_t offset,
                          int64_t limit) {
    uint8_t tail[SBX_MAX_TAIL];
    size_t index, length;
    if (offset > limit || limit - offset > SBX_MAX_TAIL) return false;
    length = (size_t)(limit - offset);
    if (length == 0U) return true;
    if (!sbx_read_at(device, base + offset, tail, length)) return false;
    for (index = 0U; index < length; ++index)
        if (tail[index] != 0U) return false;
    return true;
}

/* Walk the chain from @p start.  There is no count and no terminator: the
 * chain is only valid when its last record ends on @p limit (the end of the
 * file, or the start of a PE's Authenticode certificate table that closes the
 * file), optionally followed by up to SBX_MAX_TAIL zero bytes of padding.
 * @p budget is shared by every walk of one locate, so a file full of tags
 * cannot turn the search quadratic. */
static bool sbx_walk(xx_io_device *device, int64_t base, int64_t available,
                     int64_t limit, int64_t start, uint32_t *budget,
                     struct sbx_table_s *table, xx_pd_struct *pd) {
    int64_t offset = start;
    uint32_t count = 0U;
    sbx_record record;

    if (limit > available || start < 0 || start >= limit) return false;
    while (offset < limit) {
        if ((pd && xx_pd_is_stopped(pd)) || *budget == 0U ||
            count >= SBX_MAX_MEMBERS)
            return false;
        --*budget;
        if (!sbx_read_record(device, base, limit, offset, &record))
            return count != 0U &&
                   sbx_zero_tail(device, base, offset, limit);
        if (table && !sbx_publish(table, &record)) return false;
        ++count;
        offset += record.size;
    }
    return offset == limit && count != 0U;
}

/* ---------------------------------------------------------------------- */
/* Carrier                                                                 */

typedef struct sbx_location_s {
    int64_t available;
    int64_t limit; /**< Where the chain (plus zero padding) must end. */
    int64_t start;
    uint32_t carrier;
} sbx_location;

/* End of the PE image as the loader maps it from the file: SizeOfHeaders and
 * the end of every section's raw data.  0 when the headers are not a PE.
 * @p limit_out receives the start of the Authenticode certificate table when
 * one lies after the image and ends exactly on the last byte, otherwise
 * @p available. */
static int64_t sbx_pe_image_end(xx_io_device *device, int64_t base,
                                int64_t available, uint32_t lfanew,
                                int64_t *limit_out) {
    uint8_t pe[SBX_PE_HEADER];
    uint8_t optional[SBX_OPTIONAL_READ];
    uint8_t sections[SBX_MAX_SECTIONS * SBX_SECTION_SIZE];
    uint32_t count, optional_size, index, magic, directories;
    uint64_t end;
    int64_t table;
    size_t optional_read;

    *limit_out = available;

    if ((int64_t)lfanew > available - SBX_PE_HEADER - SBX_OPTIONAL_MIN ||
        !sbx_read_at(device, base + lfanew, pe, sizeof(pe)) || pe[0] != 'P' ||
        pe[1] != 'E' || pe[2] != 0U || pe[3] != 0U)
        return 0;
    count = sbx_le16(pe + 6);
    optional_size = sbx_le16(pe + 20);
    if (count == 0U || count > SBX_MAX_SECTIONS ||
        optional_size < SBX_OPTIONAL_MIN || optional_size > SBX_OPTIONAL_MAX)
        return 0;
    table = (int64_t)lfanew + SBX_PE_HEADER + optional_size;
    optional_read = optional_size < SBX_OPTIONAL_READ ? (size_t)optional_size
                                                      : SBX_OPTIONAL_READ;
    if (table > available - (int64_t)count * SBX_SECTION_SIZE ||
        !sbx_read_at(device, base + lfanew + SBX_PE_HEADER, optional,
                     optional_read))
        return 0;
    magic = sbx_le16(optional);
    if (magic != 0x010bU && magic != 0x020bU) return 0;
    if (!sbx_read_at(device, base + table, sections,
                     (size_t)count * SBX_SECTION_SIZE))
        return 0;
    end = xx_data_get_u32(optional + 60, 4, 0, false);
    for (index = 0U; index < count; ++index) {
        const uint8_t *section = sections + (size_t)index * SBX_SECTION_SIZE;
        uint64_t raw_size = xx_data_get_u32(section + 16, 4, 0, false);
        uint64_t raw_pointer = xx_data_get_u32(section + 20, 4, 0, false);
        if (raw_size != 0U && raw_pointer + raw_size > end)
            end = raw_pointer + raw_size;
    }
    if (end >= (uint64_t)available) return 0;
    /* Data directory 4 (security) holds a file offset, not an RVA. */
    directories = magic == 0x010bU ? 96U : 112U;
    if (optional_read >= directories + 40U &&
        xx_data_get_u32(optional + directories - 4U, 4, 0, false) > 4U) {
        uint64_t certificate = xx_data_get_u32(optional + directories + 32U, 4, 0, false);
        uint64_t certificate_size = xx_data_get_u32(optional + directories + 36U, 4, 0, false);
        if (certificate > end && certificate_size != 0U &&
            certificate + certificate_size == (uint64_t)available)
            *limit_out = (int64_t)certificate;
    }
    return (int64_t)end;
}

/* Search [@p from, @p scan_end) for the tag and keep the first of at most
 * @p max_candidates candidates whose chain closes on @p chain_limit.  NE
 * carriers search their first MiB and skip the stub's stray tags; a PE
 * searches a window of its overlay when the chain does not open it, and only
 * the first tag there counts. */
static bool sbx_tag_search(xx_io_device *device, int64_t base,
                           int64_t available, int64_t chain_limit,
                           int64_t from, int64_t scan_end,
                           uint32_t max_candidates, uint32_t *budget,
                           int64_t *start_out, xx_pd_struct *pd) {
    const size_t window = xx_get_file_buffer_size();
    uint8_t *chunk;
    int64_t limit, position;
    uint32_t candidates = 0U;
    bool found = false, stop = false;

    limit = chain_limit < scan_end ? chain_limit : scan_end;
    if (from < 0 || from > limit - (int64_t)sizeof(sbx_tag)) return false;
    chunk = (uint8_t *)xx_mem_alloc(window);
    if (!chunk) return false;
    for (position = from;
         !found && !stop && position <= limit - (int64_t)sizeof(sbx_tag);
         ) {
        int64_t left = limit - position - (int64_t)sizeof(sbx_tag) + 1;
        size_t length = left > (int64_t)window ? window : (size_t)left;
        size_t index;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !sbx_read_at(device, base + position, chunk, length))
            break;
        for (index = 0U; index < length; ++index) {
            uint8_t frame[sizeof(sbx_tag)];
            const uint8_t *bytes = chunk + index;
            if (chunk[index] != 'S') continue;
            if (length - index < sizeof(sbx_tag)) {
                if (!sbx_read_at(device, base + position + (int64_t)index, frame, sizeof(frame))) continue;
                bytes = frame;
            }
            if (xx_rt_memcmp(bytes, sbx_tag, sizeof(sbx_tag)) != 0) continue;
            if (++candidates > max_candidates) {
                stop = true;
                break;
            }
            if (sbx_walk(device, base, available, chain_limit,
                         position + (int64_t)index, budget, NULL, pd)) {
                *start_out = position + (int64_t)index;
                found = true;
                break;
            }
            if (*budget == 0U || (pd && xx_pd_is_stopped(pd))) {
                stop = true;
                break;
            }
        }
        position += (int64_t)length;
    }
    xx_mem_free(chunk);
    return found;
}

static bool sbx_locate(Abstractformat *format, sbx_location *location,
                       xx_pd_struct *pd) {
    uint8_t dos[SBX_DOS_HEADER];
    uint8_t signature[2];
    int64_t total, available, base, start = 0, limit = 0;
    uint32_t lfanew, budget = SBX_STEP_BUDGET;

    if (!format || !format->device || format->base_address < 0) return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    available = total - base;
    if (available < SBX_DOS_HEADER + SBX_MIN_RECORD ||
        !sbx_read_at(format->device, base, dos, sizeof(dos)) ||
        dos[0] != 'M' || dos[1] != 'Z')
        return false;
    lfanew = xx_data_get_u32(dos + 0x3c, 4, 0, false);
    if (lfanew < SBX_DOS_HEADER || lfanew > SBX_MAX_LFANEW ||
        (int64_t)lfanew > available - SBX_NE_HEADER - SBX_MIN_RECORD ||
        !sbx_read_at(format->device, base + lfanew, signature,
                     sizeof(signature)))
        return false;

    if (signature[0] == 'P' && signature[1] == 'E') {
        /* The chain normally opens the overlay exactly, and then it must
         * close.  When the overlay does not start with the tag (padding, or a
         * section table that does not cover the whole image), the first tag
         * within SBX_PE_SCAN_WINDOW bytes is taken, and only that one: a
         * later tag would be a record in the middle of a broken chain. */
        uint8_t head[XX_SFX_SBX_EXTRACTOR_TAG_SIZE];
        int64_t end = sbx_pe_image_end(format->device, base, available,
                                       lfanew, &limit);
        if (end <= 0 || end > limit - SBX_MIN_RECORD ||
            !sbx_read_at(format->device, base + end, head, sizeof(head)))
            return false;
        if (xx_rt_memcmp(head, sbx_tag, sizeof(head)) == 0) {
            if (!sbx_walk(format->device, base, available, limit, end,
                          &budget, NULL, pd))
                return false;
            start = end;
        } else if (!sbx_tag_search(format->device, base, available, limit,
                                   end + 1, end + SBX_PE_SCAN_WINDOW, 1U,
                                   &budget, &start, pd)) {
            return false;
        }
        location->carrier = XX_SFX_SBX_EXTRACTOR_CARRIER_PE;
    } else if (signature[0] == 'N' && signature[1] == 'E') {
        limit = available;
        if (!sbx_tag_search(format->device, base, available, available,
                            (int64_t)lfanew + SBX_NE_HEADER,
                            SBX_NE_SCAN_LIMIT, SBX_MAX_CANDIDATES, &budget,
                            &start, pd))
            return false;
        location->carrier = XX_SFX_SBX_EXTRACTOR_CARRIER_NE;
    } else {
        return false;
    }
    location->available = available;
    location->limit = limit;
    location->start = start;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member table                                                            */

typedef struct sbx_member_s {
    char *name; /**< UTF-8, '/' separated, reserved characters replaced. */
    char *key;  /**< Case-folded form used to keep duplicates apart. */
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    int64_t unpacked_size;
    uint32_t dos_date;
    uint32_t dos_time;
    uint8_t attributes;
    bool unsafe; /**< Listed, never written. */
} sbx_member;

typedef struct sbx_table_s {
    sbx_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    uint32_t *slots;
    size_t slot_count;
    size_t name_bytes;
    uint64_t unpacked_total;
    int64_t start;
    int64_t available;
    uint32_t carrier;
} sbx_table;

static void sbx_table_free(sbx_table *table) {
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

static void sbx_table_free_opaque(void *opaque) {
    sbx_table_free((sbx_table *)opaque);
}

static uint32_t sbx_hash(const char *key) {
    uint32_t hash = 2166136261U;
    while (*key) {
        hash ^= (uint8_t)*key++;
        hash *= 16777619U;
    }
    return hash;
}

static bool sbx_same(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static bool sbx_key_taken(const sbx_table *table, const char *key) {
    size_t mask, slot;
    if (!table->slots) return false;
    mask = table->slot_count - 1U;
    slot = sbx_hash(key) & mask;
    while (table->slots[slot] != 0U) {
        if (sbx_same(table->items[table->slots[slot] - 1U].key, key))
            return true;
        slot = (slot + 1U) & mask;
    }
    return false;
}

/* Insert member @p member's key; grows (and rebuilds) at half load. */
static bool sbx_key_insert(sbx_table *table, size_t member) {
    size_t mask, slot, index;
    if (table->slot_count == 0U ||
        (table->count + 1U) * 2U > table->slot_count) {
        size_t grown = table->slot_count ? table->slot_count * 2U : 64U;
        uint32_t *slots = (uint32_t *)xx_mem_alloc(grown * sizeof(uint32_t));
        if (!slots) return false;
        xx_rt_memset(slots, 0, grown * sizeof(uint32_t));
        mask = grown - 1U;
        for (index = 0U; index < table->count; ++index) {
            slot = sbx_hash(table->items[index].key) & mask;
            while (slots[slot] != 0U) slot = (slot + 1U) & mask;
            slots[slot] = (uint32_t)(index + 1U);
        }
        if (table->slots) xx_mem_free(table->slots);
        table->slots = slots;
        table->slot_count = grown;
    }
    mask = table->slot_count - 1U;
    slot = sbx_hash(table->items[member].key) & mask;
    while (table->slots[slot] != 0U) slot = (slot + 1U) & mask;
    table->slots[slot] = (uint32_t)(member + 1U);
    return true;
}

/* Append @p suffix to the heap string @p text. */
static bool sbx_append(char **text, const char *suffix) {
    size_t length = xx_str_len(*text), extra = xx_str_len(suffix);
    char *grown = (char *)xx_mem_alloc(length + extra + 1U);
    if (!grown) return false;
    xx_rt_memcpy(grown, *text, length);
    xx_rt_memcpy(grown + length, suffix, extra + 1U);
    xx_mem_free(*text);
    *text = grown;
    return true;
}

static void sbx_decimal(char *out, char prefix, size_t value, unsigned width) {
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
static const uint16_t sbx_cp1252_high[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178};

static uint8_t sbx_upper_ascii(uint8_t value) {
    return (value >= 'a' && value <= 'z') ? (uint8_t)(value - 0x20U) : value;
}

/* Lower-case fold of a code point of a listed name (every one of them comes
 * from Windows-1252, or is ASCII), as the file system would see it. */
static uint32_t sbx_fold(uint32_t code) {
    if (code >= 'A' && code <= 'Z') return code + 0x20U;
    if (code >= 0xC0U && code <= 0xDEU && code != 0xD7U) return code + 0x20U;
    if (code == 0x0160U || code == 0x0152U || code == 0x017DU)
        return code + 1U; /* S and Z with caron, the OE ligature */
    if (code == 0x0178U) return 0xFFU; /* Y with diaeresis */
    return code;
}

static size_t sbx_utf8_put(char *out, uint32_t code) {
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

/* The fold key of a listed (output) name: every component folded to lower
 * case and stripped of the trailing dots and spaces Windows drops.  It is
 * always derived from the exact name that would be written, so two members
 * that would land on the same file share a key.  Listed names are
 * well-formed UTF-8 of at most three bytes per code point (sbx_build_name
 * and the ASCII suffixes make them), and folding keeps the width of each. */
static char *sbx_make_key(const char *name) {
    size_t length = xx_str_len(name), in = 0U, out = 0U, component = 0U;
    char *key = (char *)xx_mem_alloc(length * 3U + 1U);
    if (!key) return NULL;
    for (;;) {
        uint8_t lead = (uint8_t)name[in];
        uint32_t code;
        if (lead == 0U || lead == '/') {
            while (out > component &&
                   (key[out - 1U] == '.' || key[out - 1U] == ' '))
                --out;
            if (lead == 0U) break;
            key[out++] = '/';
            component = out;
            ++in;
            continue;
        }
        if ((lead & 0xE0U) == 0xC0U && in + 1U < length) {
            code = ((uint32_t)(lead & 0x1FU) << 6U) |
                   ((uint8_t)name[in + 1U] & 0x3FU);
            in += 2U;
        } else if ((lead & 0xF0U) == 0xE0U && in + 2U < length) {
            code = ((uint32_t)(lead & 0x0FU) << 12U) |
                   ((uint32_t)((uint8_t)name[in + 1U] & 0x3FU) << 6U) |
                   ((uint8_t)name[in + 2U] & 0x3FU);
            in += 3U;
        } else {
            code = lead; /* ASCII (anything else is not produced here) */
            in += 1U;
            if (lead >= 0x80U) {
                key[out++] = (char)lead;
                continue;
            }
        }
        out += sbx_utf8_put(key + out, sbx_fold(code));
    }
    key[out] = 0;
    return key;
}

/* The characters Windows reserves in a name, and control bytes. */
static bool sbx_reserved_char(uint8_t c) {
    return c < 0x20U || c == 0x7FU || c == '<' || c == '>' || c == ':' ||
           c == '"' || c == '|' || c == '?' || c == '*';
}

/* COM and LPT take a digit or a superscript one, two or three, which is one
 * Windows-1252 byte in a raw name and two UTF-8 bytes in a listed one. */
static bool sbx_is_port_suffix(const uint8_t *text, size_t length,
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
static bool sbx_is_device(const uint8_t *text, size_t length, bool utf8) {
    static const char *const names[] = {"CON",    "PRN",     "AUX",
                                        "NUL",    "CONIN$",  "CONOUT$",
                                        "CLOCK$"};
    size_t stem = 0U, index, position;
    while (stem < length && text[stem] != '.') ++stem;
    while (stem > 0U && text[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *name = names[index];
        for (position = 0U; position < stem && name[position]; ++position)
            if (sbx_upper_ascii(text[position]) != (uint8_t)name[position])
                break;
        if (position == stem && name[position] == 0) return true;
    }
    if (stem >= 4U && sbx_is_port_suffix(text + 3, stem - 3U, utf8)) {
        uint8_t a = sbx_upper_ascii(text[0]), b = sbx_upper_ascii(text[1]),
                c = sbx_upper_ascii(text[2]);
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
 * component and the reserved ':'. */
static bool sbx_build_name(const uint8_t *raw, size_t length, char **name_out,
                           bool *unsafe_out) {
    uint8_t clean[SBX_MAX_NAME * 2U + 2U];
    size_t clean_length = 0U, start = 0U, index;
    bool unsafe = false;
    char *name;
    size_t out = 0U;

    for (index = 0U; index <= length; ++index) {
        size_t part, position;
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
        if (clean_length != 0U) clean[clean_length++] = '/';
        if (meaningful && sbx_is_device(raw + start, part, false)) {
            unsafe = true;
            clean[clean_length++] = '_';
        }
        for (position = start; position < index; ++position) {
            uint8_t c = raw[position];
            if (sbx_reserved_char(c)) unsafe = true;
            clean[clean_length++] =
                (!meaningful || sbx_reserved_char(c)) ? (uint8_t)'_' : c;
        }
        start = index + 1U;
    }
    if (clean_length == 0U) {
        clean[clean_length++] = '_';
        unsafe = true;
    }

    name = (char *)xx_mem_alloc(clean_length * 3U + 1U);
    if (!name) return false;
    for (index = 0U; index < clean_length; ++index) {
        uint32_t code = clean[index];
        /* The five unassigned Windows-1252 bytes are written as '_'. */
        if (code >= 0x80U && code < 0xA0U) code = sbx_cp1252_high[code - 0x80U];
        if (code == 0U) code = '_';
        out += sbx_utf8_put(name + out, code);
    }
    name[out] = 0;
    *name_out = name;
    *unsafe_out = unsafe;
    return true;
}

/* Replace @p member's key with the key of its current name. */
static bool sbx_rekey(sbx_member *member) {
    char *key = sbx_make_key(member->name);
    if (!key) return false;
    if (member->key) xx_mem_free(member->key);
    member->key = key;
    return true;
}

/* Add one record to the table.  Two members that fold to the same name must
 * not overwrite each other: the later one gets its member index appended.
 * The key is rebuilt from the name after every change, so the key checked is
 * always the key of the path that would be written. */
static bool sbx_publish(sbx_table *table, const sbx_record *record) {
    sbx_member member;
    char suffix[32];
    unsigned attempt;

    if (table->count >= SBX_MAX_MEMBERS) return false;
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2U : 16U;
        sbx_member *grown = (sbx_member *)xx_mem_realloc(
            table->items, capacity * sizeof(*grown));
        if (!grown) return false;
        table->items = grown;
        table->capacity = capacity;
    }
    xx_mem_zero(&member, sizeof(member));
    if (!sbx_build_name(record->name, record->name_length, &member.name,
                        &member.unsafe))
        return false;
    if (!sbx_rekey(&member)) goto fail;
    if (sbx_key_taken(table, member.key)) {
        sbx_decimal(suffix, '.', table->count, 4U);
        if (!sbx_append(&member.name, suffix) || !sbx_rekey(&member))
            goto fail;
        for (attempt = 1U; sbx_key_taken(table, member.key); ++attempt) {
            if (attempt > 16U) goto fail;
            sbx_decimal(suffix, '_', attempt, 1U);
            if (!sbx_append(&member.name, suffix) || !sbx_rekey(&member))
                goto fail;
        }
    }
    table->name_bytes += xx_str_len(member.name) + xx_str_len(member.key);
    if (table->name_bytes > SBX_MAX_NAME_BYTES) goto fail;
    member.header_offset = record->header;
    member.data_offset = record->data;
    member.packed_size = record->packed;
    member.unpacked_size = record->unpacked;
    member.dos_date = record->dos_date;
    member.dos_time = record->dos_time;
    member.attributes = record->attributes;
    table->items[table->count] = member;
    if (!sbx_key_insert(table, table->count)) {
        table->items[table->count].name = NULL;
        table->items[table->count].key = NULL;
        goto fail;
    }
    ++table->count;
    table->unpacked_total += (uint64_t)record->unpacked;
    return true;
fail:
    if (member.name) xx_mem_free(member.name);
    if (member.key) xx_mem_free(member.key);
    return false;
}

static bool sbx_build_table(Abstractformat *format, sbx_table **out,
                            xx_pd_struct *pd) {
    sbx_location location;
    sbx_table *table;
    uint32_t budget = SBX_STEP_BUDGET;

    *out = NULL;
    xx_mem_zero(&location, sizeof(location));
    if (!sbx_locate(format, &location, pd)) return false;
    table = (sbx_table *)xx_mem_alloc(sizeof(*table));
    if (!table) return false;
    xx_mem_zero(table, sizeof(*table));
    table->start = location.start;
    table->available = location.available;
    table->carrier = location.carrier;
    if (!sbx_walk(format->device, format->base_address, location.available,
                  location.limit, location.start, &budget, table, pd) ||
        table->count == 0U) {
        sbx_table_free(table);
        return false;
    }
    *out = table;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Decoding                                                                */

/* Decode one member into @p destination (NULL only verifies). */
static bool sbx_unpack_member(Abstractformat *format,
                              const sbx_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U, done = 0U;
    int64_t input;
    bool result = false;

    if (member->packed_size < 0 || member->unpacked_size < 0 ||
        member->unpacked_size > SBX_MAX_MEMBER)
        return false;
    if (member->unpacked_size == 0) {
        /* Nothing to produce: an empty file, whatever the packed bytes. */
        return true;
    }
    /* The decoder stops at the stored size and tolerates trailing bytes.  The
     * adaptive code keeps its total frequency at or below 0x8000, and a
     * Huffman tree of depth d with weights of at least 1 needs a total of at
     * least Fibonacci(d + 3) - 1 (Fibonacci(24) = 46,368), so no code is
     * deeper than 20 bits.  A literal therefore costs at most 20 bits and a
     * match (three or more bytes) at most 20 + 14, so a member never needs
     * more than four packed bytes per output byte.  Reading only that much
     * keeps a record that claims a huge packed size for a small file from
     * pulling it all into memory: one unpack holds at most 4 * 64 MiB + 64
     * packed bytes and 64 MiB of output. */
    input = member->unpacked_size * SBX_MAX_INPUT_PER_BYTE + 64;
    if (input > member->packed_size) input = member->packed_size;
    if ((uint64_t)input > (uint64_t)SIZE_MAX ||
        (uint64_t)member->unpacked_size > (uint64_t)SIZE_MAX)
        return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    packed = (uint8_t *)xx_mem_alloc(input != 0 ? (size_t)input : 1U);
    plain = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!packed || !plain ||
        !sbx_read_at(format->device,
                     format->base_address + member->data_offset, packed,
                     (size_t)input))
        goto done;
    /* No end symbol: the stored size is the only stop condition, and the
     * decoder succeeds only when it produced exactly that many bytes. */
    if (!xx_lzhuf_decode_memory(packed, (size_t)input, plain,
                                (size_t)member->unpacked_size, &written) ||
        written != (size_t)member->unpacked_size)
        goto done;
    if (destination) {
        while (done < written) {
            ssize_t sent = xx_io_write(destination, plain + done,
                                       written - done);
            if (sent <= 0 || (size_t)sent > written - done) goto done;
            done += (size_t)sent;
        }
    }
    result = true;
done:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records API helpers                                                     */

static bool sbx_copy_options(xx_list_s *destination,
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

static const xx_var *sbx_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sbx_set_record(Abstractformat *format, xx_archive_record *record,
                           const sbx_member *member) {
    uint64_t method = member->unpacked_size == 0
                          ? XX_SFX_SBX_EXTRACTOR_METHOD_STORED
                          : XX_SFX_SBX_EXTRACTOR_METHOD_LZHUF;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = format->base_address + member->data_offset;
    record->compressed_size = member->packed_size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->packed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->unpacked_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        method) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        member->attributes) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    /* The packed MS-DOS stamp, time in the low word and date in the high
     * one, as the ARJ reader publishes it.  A zero date is "no date". */
    if (member->dos_date != 0U &&
        !xx_archive_record_set_meta_u64(
            record, XX_META_ID_TIMESTAMP,
            ((uint64_t)member->dos_date << 16U) | member->dos_time))
        return false;
    return true;
}

/* The table already marked unsafe names; this is the last gate before a name
 * reaches the file system. */
static bool sbx_safe_output_name(const char *name) {
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
                if (sbx_reserved_char(c)) return false;
                if (c != '.' && c != ' ') meaningful = true;
            }
            if (!meaningful ||
                sbx_is_device((const uint8_t *)name + start, part, true))
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

static void sbx_vtable_destroy(Abstractformat *format) {
    xx_sfx_sbx_extractor *archive = (xx_sfx_sbx_extractor *)format;
    if (archive && archive->table) {
        sbx_table_free((sbx_table *)archive->table);
        archive->table = NULL;
    }
}

void xx_sfx_sbx_extractor_init(xx_sfx_sbx_extractor *archive,
                               xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_SBX_EXTRACTOR_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sbx-sfx");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_sbx_extractor_check_is_valid;
    archive->format.handle_base_info = xx_sfx_sbx_extractor_handle_base_info;
    archive->format.get_format_size = xx_sfx_sbx_extractor_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_sbx_extractor_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_sbx_extractor_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_sbx_extractor_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_sbx_extractor_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_sbx_extractor_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_sbx_extractor_free_archive_records_reading;
    archive->format.destroy = sbx_vtable_destroy;
    archive->chain_offset = -1;
}

xx_sfx_sbx_extractor *xx_sfx_sbx_extractor_create(xx_io_device *device,
                                                  int64_t base_address) {
    xx_sfx_sbx_extractor *archive =
        (xx_sfx_sbx_extractor *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_sbx_extractor_init(archive, device, base_address);
    return archive;
}

void xx_sfx_sbx_extractor_destroy(xx_sfx_sbx_extractor *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper above. */
    sbx_vtable_destroy(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_sfx_sbx_extractor_free(xx_sfx_sbx_extractor *archive) {
    if (!archive) return;
    xx_sfx_sbx_extractor_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_sbx_extractor_check_is_valid(Abstractformat *format,
                                         xx_pd_struct *pd) {
    sbx_location location;
    if (!format || (pd && xx_pd_is_stopped(pd))) return false;
    xx_mem_zero(&location, sizeof(location));
    return sbx_locate(format, &location, pd);
}

bool xx_sfx_sbx_extractor_handle_base_info(Abstractformat *format,
                                           xx_pd_struct *pd) {
    xx_sfx_sbx_extractor *archive;
    sbx_table *table = NULL;
    if (!format || (pd && xx_pd_is_stopped(pd))) return false;
    archive = (xx_sfx_sbx_extractor *)format;
    if (archive->table) {
        sbx_table_free((sbx_table *)archive->table);
        archive->table = NULL;
    }
    if (!sbx_build_table(format, &table, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        archive->number_of_records = 0U;
        return false;
    }
    archive->table = table;
    archive->number_of_records = table->count;
    archive->chain_offset = table->start;
    archive->unpacked_total = table->unpacked_total;
    archive->carrier = table->carrier;
    format->number_of_archive_records = table->count;
    /* The chain closes on the last byte, so carrier plus chain is the whole
     * file from the base on. */
    format->format_size = table->available;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_SFX_SBX_EXTRACTOR_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_sbx_extractor_get_format_size(Abstractformat *format,
                                             xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_sbx_extractor_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_sfx_sbx_extractor_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_sbx_extractor_handle_base_info(format, pd))
               ? ((xx_sfx_sbx_extractor *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_sfx_sbx_extractor_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_sfx_sbx_extractor *archive;
    xx_archive_record_state *state;
    sbx_table *table = NULL;
    if (!format || !format->device) return NULL;
    archive = (xx_sfx_sbx_extractor *)format;
    /* The table handle_base_info built is handed over rather than walked a
     * second time; a later listing walks again. */
    if (archive->table) {
        table = (sbx_table *)archive->table;
        archive->table = NULL;
    } else if (!sbx_build_table(format, &table, pd)) {
        return NULL;
    }
    table->index = 0U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sbx_table_free(table);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = table;
    state->free_internal = sbx_table_free_opaque;
    state->total_records = (int64_t)table->count;
    if (!sbx_copy_options(&state->options, options) ||
        !sbx_set_record(format, &state->current_record, &table->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sfx_sbx_extractor_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sfx_sbx_extractor_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    sbx_table *table;
    if (!format || !state || state->format != format || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    table = (sbx_table *)state->internal_state;
    if (!table || table->index + 1U >= table->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++table->index;
    ++state->current_index;
    state->has_record = sbx_set_record(format, &state->current_record,
                                       &table->items[table->index]);
    return state->has_record;
}

bool xx_sfx_sbx_extractor_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    sbx_table *table;
    const sbx_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(table = (sbx_table *)state->internal_state) ||
        table->index >= table->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &table->items[table->index];
    path_option = sbx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    /* No destination: decode and discard, which verifies the member. */
    if (!path_option) return sbx_unpack_member(format, member, NULL, pd);
    if (member->unsafe || !sbx_safe_output_name(member->name)) return false;
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
        result = sbx_unpack_member(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_io_file_remove_a(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_sbx_extractor_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
