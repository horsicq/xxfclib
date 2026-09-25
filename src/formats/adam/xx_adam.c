/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Coleco Adam disk images carrying the EOS filesystem.  xx_adam.h carries
 * the layout.  Written from the format's structure; MAME's
 * src/lib/formats/fs_adam_eos.cpp and adam_dsk.cpp (BSD-3-Clause, AJR) were
 * read to confirm the block interleave, the field offsets and which entries
 * a listing shows.  No code was taken from them.
 *
 * There is no magic in the first 64 bytes: block 0 is Z80 boot code.  What
 * every EOS disk has is the directory check 55 AA 00 FF in the volume
 * descriptor and the BOOT and DIRECTORY bookkeeping entries behind it, all
 * at fixed image offsets 0x40D..0x444, so the probe is one 56-byte read
 * after a size test.  Everything else is decided by the directory walk,
 * which is bounded by the directory size byte (at most 127 blocks).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/adam/xx_adam.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as ADAM is registered there. */
#ifdef ADAM
#define XX_ADAM_FILE_TYPE XX_FILE_TYPE_ADAM
#else
#define XX_ADAM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ADAM_SECTOR 512U
#define ADAM_BLOCK 1024U
#define ADAM_ENTRY 26U
#define ADAM_NAME 12U
#define ADAM_ETX 0x03U
/* Entries never straddle a block: after the slot at 988 the walk moves on,
 * so each block holds 39 slots (block 1: the descriptor and 38 entries). */
#define ADAM_LAST_SLOT (ADAM_BLOCK - ADAM_ENTRY)
#define ADAM_SLOTS_PER_BLOCK 39U
#define ADAM_MAX_DIRECTORY_BLOCKS 127U

#define ADAM_ATTR_DELETED 0x04U
#define ADAM_ATTR_HOLE 0x01U

/* Offsets inside a directory entry (and the volume descriptor). */
#define ADAM_E_ATTR 12U
#define ADAM_E_START 13U
#define ADAM_E_MAX 17U
#define ADAM_E_USED 19U
#define ADAM_E_LAST 21U
#define ADAM_E_DATE 23U

/* The fixed part of block 1 that every EOS formatter writes, as image
 * offsets: descriptor check word, then BOOT (block 0, 1 allocated, 1 used)
 * and DIRECTORY (starting at block 1).  0x100 marks a byte that varies. */
#define ADAM_SIGNATURE_OFFSET 0x40DU
#define ADAM_SIGNATURE_SIZE 0x38U
#define ADAM_ANY 0x100U
static const uint16_t adam_signature[ADAM_SIGNATURE_SIZE] = {
    0x55, 0xAA, 0x00, 0xFF,                          /* 0x40D check word  */
    ADAM_ANY, ADAM_ANY, ADAM_ANY, ADAM_ANY,          /* volume size       */
    ADAM_ANY, ADAM_ANY, ADAM_ANY, ADAM_ANY, ADAM_ANY, /* reserved, date   */
    'B', 'O', 'O', 'T', 0x03,                        /* 0x41A BOOT name   */
    ADAM_ANY, ADAM_ANY, ADAM_ANY, ADAM_ANY, ADAM_ANY, ADAM_ANY, ADAM_ANY,
    ADAM_ANY,                                        /* attributes        */
    0x00, 0x00, 0x00, 0x00,                          /* first block 0     */
    0x01, 0x00, 0x01, 0x00,                          /* 1 allocated, used */
    ADAM_ANY, ADAM_ANY, ADAM_ANY, ADAM_ANY, ADAM_ANY, /* last count, date */
    'D', 'I', 'R', 'E', 'C', 'T', 'O', 'R', 'Y', 0x03, /* 0x434 DIRECTORY */
    ADAM_ANY, ADAM_ANY,
    ADAM_ANY,                                        /* attributes        */
    0x01, 0x00, 0x00, 0x00                           /* first block 1     */
};

/* The capacities an Adam .dsk image comes in. */
static const int64_t adam_image_sizes[] = {
    INT64_C(163840), INT64_C(327680), INT64_C(737280), INT64_C(1474560)
};
#define ADAM_MIN_IMAGE INT64_C(163840)

/* '_' + 11 name bytes + '~' + 4 digits + '.' + "STX" + NUL, rounded up. */
#define ADAM_NAME_BUFFER 32U

typedef struct adam_member_s {
    char name[ADAM_NAME_BUFFER];
    int64_t size;
    uint64_t timestamp;
    uint32_t first_block;
    uint32_t used_blocks;
    uint32_t last_count;  /**< Bytes of the last used block, 0..1024. */
    uint32_t entry;       /**< Directory slot index (descriptor = 0). */
    uint8_t attributes;
    bool has_timestamp;
    bool in_range;        /**< All used blocks lie inside the volume. */
} adam_member;

typedef struct adam_volume_s {
    int64_t image_size;
    uint32_t volume_blocks;
    uint32_t declared_blocks;
    uint32_t directory_blocks;
    char name[XX_ADAM_VOLUME_NAME_FIELD + 1];
} adam_volume;

typedef struct adam_stream_s {
    adam_volume volume;
    adam_member *items;
    size_t count;
    size_t index;
} adam_stream;

static uint32_t adam_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t adam_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool adam_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* EOS block @p block is image sectors 2B and 2B ^ 5.  Both lie in the same
 * group of eight sectors, and every image size is a whole number of groups,
 * so a block below volume_blocks is always fully inside the image. */
static bool adam_read_block(xx_io_device *device, int64_t base,
                            uint32_t volume_blocks, uint32_t block,
                            uint8_t *out) {
    uint32_t low, high;
    if (block >= volume_blocks) return false;
    low = block * 2U;
    high = low ^ 5U;
    if (high >= volume_blocks * 2U) return false;
    return adam_read_at(device, base + (int64_t)low * ADAM_SECTOR, out,
                        ADAM_SECTOR) &&
           adam_read_at(device, base + (int64_t)high * ADAM_SECTOR,
                        out + ADAM_SECTOR, ADAM_SECTOR);
}

static bool adam_is_standard_size(int64_t size) {
    size_t index;
    for (index = 0U; index < sizeof(adam_image_sizes) /
                                 sizeof(adam_image_sizes[0]);
         ++index)
        if (adam_image_sizes[index] == size) return true;
    return false;
}

/* File-type bytes EOS software writes: SmartBASIC 'A' and 'H', their
 * lower-case backup forms, 'C' from CopyCart, and 0x02 for boot programs. */
static bool adam_is_file_type(uint8_t type) {
    return type == 'A' || type == 'a' || type == 'H' || type == 'h' ||
           type == 'C' || type == 0x02U;
}

/* The volume check: size test, then the fixed bytes of block 1, then the
 * descriptor's directory size.  Two small reads at most, so it is cheap
 * enough to run as a late probe over every undetected file. */
static bool adam_probe(Abstractformat *format, adam_volume *out) {
    uint8_t window[ADAM_SIGNATURE_SIZE];
    uint8_t head[ADAM_SIGNATURE_OFFSET - 0x400U];
    adam_volume volume;
    int64_t total, size;
    size_t index;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < ADAM_MIN_IMAGE) return false;
    if (!adam_read_at(format->device,
                      format->base_address + ADAM_SIGNATURE_OFFSET, window,
                      sizeof(window)))
        return false;
    for (index = 0U; index < ADAM_SIGNATURE_SIZE; ++index)
        if (adam_signature[index] != ADAM_ANY &&
            adam_signature[index] != (uint16_t)window[index])
            return false;
    xx_mem_zero(&volume, sizeof(volume));
    /* window[4..7] is the descriptor's volume size (image offset 0x411). */
    volume.declared_blocks = adam_le32(window + 4U);
    if (adam_is_standard_size(size)) {
        volume.image_size = size;
    } else {
        /* Inside a larger device (a format search, or trailing data), the
         * descriptor has to name a standard capacity the device can hold. */
        int64_t declared = (int64_t)volume.declared_blocks * ADAM_BLOCK;
        if (!adam_is_standard_size(declared) || declared > size) return false;
        volume.image_size = declared;
    }
    volume.volume_blocks = (uint32_t)(volume.image_size / ADAM_BLOCK);
    /* Volume name and directory size: image offsets 0x400..0x40C. */
    if (!adam_read_at(format->device, format->base_address + 0x400,
                      head, sizeof(head)))
        return false;
    volume.directory_blocks = (uint32_t)head[ADAM_NAME] & 0x7FU;
    if (volume.directory_blocks == 0U ||
        volume.directory_blocks >= volume.volume_blocks)
        return false;
    for (index = 0U; index < ADAM_NAME && head[index] != ADAM_ETX; ++index)
        volume.name[index] = (head[index] >= 0x20U && head[index] < 0x7FU)
                                 ? (char)head[index] : '_';
    volume.name[index] = 0;
    *out = volume;
    return true;
}

/* Days from 1970-01-01 to the given proleptic Gregorian date. */
static int64_t adam_days_from_civil(int64_t year, uint32_t month,
                                    uint32_t day) {
    int64_t era, yoe, doy, doe;
    int64_t shifted = month > 2U ? (int64_t)month - 3 : (int64_t)month + 9;
    year -= month <= 2U ? 1 : 0;
    era = (year >= 0 ? year : year - 399) / 400;
    yoe = year - era * 400;
    doy = (153 * shifted + 2) / 5 + (int64_t)day - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool adam_bcd(uint8_t value, uint32_t *out) {
    if ((value & 0x0FU) > 9U || (value >> 4U) > 9U) return false;
    *out = (uint32_t)(value >> 4U) * 10U + (uint32_t)(value & 0x0FU);
    return true;
}

/* EOS stores BCD year (two digits), month, day.  00-49 are read as 20xx. */
static bool adam_timestamp(const uint8_t *date, uint64_t *out) {
    uint32_t year, month, day;
    int64_t days;
    if (!adam_bcd(date[0], &year) || !adam_bcd(date[1], &month) ||
        !adam_bcd(date[2], &day) || month < 1U || month > 12U || day < 1U ||
        day > 31U)
        return false;
    year += year < 50U ? 2000U : 1900U;
    days = adam_days_from_civil((int64_t)year, month, day);
    if (days < 0) return false;
    *out = (uint64_t)days * 86400U;
    return true;
}

static char adam_fold(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the first @p stem bytes of @p name are @p word, ignoring case. */
static bool adam_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || adam_fold(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* Windows resolves these stems to devices whatever the extension. */
static bool adam_is_device_stem(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && (name[stem - 1U] == ' ' || name[stem - 1U] == '.'))
        --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (adam_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((adam_fold(name[0]) == 'C' && adam_fold(name[1]) == 'O' &&
             adam_fold(name[2]) == 'M') ||
            (adam_fold(name[0]) == 'L' && adam_fold(name[1]) == 'P' &&
             adam_fold(name[2]) == 'T'));
}

/* "<name>.<type>" from the name bytes before the type byte.  Printable ASCII
 * survives except the separators and wildcards Windows reserves and '~',
 * which is kept free for the duplicate suffix. */
static void adam_make_name(const uint8_t *field, size_t name_length,
                           uint8_t type, char *out) {
    char stem[ADAM_NAME + 2U];
    size_t length = 0U, at = 0U, index;
    for (index = 0U; index < name_length && index < ADAM_NAME; ++index) {
        uint8_t c = field[index];
        bool keep = c >= 0x20U && c < 0x7FU && c != '/' && c != '\\' &&
                    c != ':' && c != '*' && c != '?' && c != '"' &&
                    c != '<' && c != '>' && c != '|' && c != '~';
        stem[length++] = keep ? (char)c : '_';
    }
    if (length == 0U) stem[length++] = '_';
    stem[length] = 0;
    if (adam_is_device_stem(stem, length)) out[at++] = '_';
    for (index = 0U; index < length; ++index) out[at++] = stem[index];
    out[at++] = '.';
    if (type == 0x02U) {
        out[at++] = 'S';
        out[at++] = 'T';
        out[at++] = 'X';
    } else {
        out[at++] = (char)type;
    }
    out[at] = 0;
}

static int adam_compare_names(const void *left, const void *right) {
    const adam_member *a = *(const adam_member *const *)left;
    const adam_member *b = *(const adam_member *const *)right;
    size_t index;
    for (index = 0U; index < ADAM_NAME_BUFFER; ++index) {
        char x = adam_fold(a->name[index]);
        char y = adam_fold(b->name[index]);
        if (x != y) return (unsigned char)x < (unsigned char)y ? -1 : 1;
        if (x == 0) break;
    }
    return a->entry < b->entry ? -1 : (a->entry > b->entry ? 1 : 0);
}

/* Insert "~<entry>" in front of the ".<type>" that ends @p member's name. */
static bool adam_add_suffix(adam_member *member) {
    char digits[12];
    size_t length = xx_str_len(member->name), dot = length, count = 0U;
    size_t index;
    uint32_t value = member->entry;
    while (dot > 0U && member->name[dot - 1U] != '.') --dot;
    if (dot == 0U) return false;
    --dot;
    do {
        digits[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    if (length + 1U + count >= ADAM_NAME_BUFFER) return false;
    for (index = length + 1U; index > dot; --index)
        member->name[index - 1U + 1U + count] = member->name[index - 1U];
    member->name[dot] = '~';
    for (index = 0U; index < count; ++index)
        member->name[dot + 1U + index] = digits[count - 1U - index];
    return true;
}

/* Sorting by folded name then entry puts every case-insensitive group
 * together, earliest entry first.  That one keeps its name; the rest get
 * their unique entry number, and since no name from the disk contains '~'
 * a suffixed name cannot meet a plain one. */
static bool adam_make_names_unique(adam_member *items, size_t count) {
    adam_member **order;
    size_t index;
    bool ok = true;
    if (count < 2U) return true;
    order = (adam_member **)xx_mem_alloc(count * sizeof(*order));
    if (!order) return false;
    for (index = 0U; index < count; ++index) order[index] = &items[index];
    xx_rt_qsort(order, count, sizeof(*order), adam_compare_names);
    for (index = count - 1U; index > 0U && ok; --index) {
        const adam_member *a = order[index - 1U];
        adam_member *b = order[index];
        size_t at;
        bool same = true;
        for (at = 0U; at < ADAM_NAME_BUFFER; ++at) {
            if (adam_fold(a->name[at]) != adam_fold(b->name[at])) {
                same = false;
                break;
            }
            if (a->name[at] == 0) break;
        }
        if (same) ok = adam_add_suffix(b);
    }
    xx_mem_free(order);
    return ok;
}

static void adam_stream_free(void *opaque) {
    adam_stream *stream = (adam_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Walk the directory blocks 1..directory_blocks, stopping at the hole. */
static bool adam_parse(Abstractformat *format, adam_stream **result) {
    adam_volume volume;
    adam_stream *stream = NULL;
    adam_member *items = NULL;
    uint8_t *block = NULL;
    size_t capacity, count = 0U;
    uint32_t current, offset, entry;
    if (!result || !adam_probe(format, &volume)) return false;
    capacity = (size_t)volume.directory_blocks * ADAM_SLOTS_PER_BLOCK;
    block = (uint8_t *)xx_mem_alloc(ADAM_BLOCK);
    items = (adam_member *)xx_mem_calloc(capacity, sizeof(*items));
    if (!block || !items) goto fail;
    current = 1U;
    offset = ADAM_ENTRY;
    entry = 1U;
    if (!adam_read_block(format->device, format->base_address,
                         volume.volume_blocks, current, block))
        goto fail;
    while (current <= volume.directory_blocks) {
        const uint8_t *slot = block + offset;
        uint8_t attributes = slot[ADAM_E_ATTR];
        if ((attributes & ADAM_ATTR_HOLE) != 0U) break;
        if ((attributes & ADAM_ATTR_DELETED) == 0U) {
            size_t length = 0U;
            while (length < ADAM_NAME && slot[length] != ADAM_ETX) ++length;
            if (length != 0U && adam_is_file_type(slot[length - 1U])) {
                adam_member *member;
                uint32_t used = adam_le16(slot + ADAM_E_USED);
                uint32_t last = adam_le16(slot + ADAM_E_LAST);
                uint32_t first = adam_le32(slot + ADAM_E_START);
                if (count >= capacity) goto fail;
                member = &items[count++];
                adam_make_name(slot, length - 1U, slot[length - 1U],
                               member->name);
                if (last > ADAM_BLOCK) last = ADAM_BLOCK;
                member->first_block = first;
                member->used_blocks = used;
                member->last_count = last;
                member->entry = entry;
                member->attributes = attributes;
                member->size = used == 0U
                                   ? 0
                                   : (int64_t)(used - 1U) * ADAM_BLOCK +
                                         (int64_t)last;
                member->in_range =
                    used == 0U || (first < volume.volume_blocks &&
                                   used <= volume.volume_blocks - first);
                member->has_timestamp =
                    adam_timestamp(slot + ADAM_E_DATE, &member->timestamp);
            }
        }
        ++entry;
        offset += ADAM_ENTRY;
        if (offset > ADAM_LAST_SLOT) {
            offset = 0U;
            if (++current > volume.directory_blocks) break;
            if (!adam_read_block(format->device, format->base_address,
                                 volume.volume_blocks, current, block))
                goto fail;
        }
    }
    if (!adam_make_names_unique(items, count)) goto fail;
    stream = (adam_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->volume = volume;
    stream->items = items;
    stream->count = count;
    xx_mem_free(block);
    *result = stream;
    return true;
fail:
    if (block) xx_mem_free(block);
    if (items) xx_mem_free(items);
    return false;
}

/* Stream a member's blocks to @p destination (or just read them through). */
static bool adam_copy_member(Abstractformat *format, const adam_volume *volume,
                             const adam_member *member,
                             xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *block;
    uint32_t index;
    bool ok = true;
    if (!member->in_range) return false;
    if (member->used_blocks == 0U) return true;
    block = (uint8_t *)xx_mem_alloc(ADAM_BLOCK);
    if (!block) return false;
    for (index = 0U; ok && index < member->used_blocks; ++index) {
        size_t amount = index + 1U == member->used_blocks
                            ? (size_t)member->last_count : ADAM_BLOCK;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !adam_read_block(format->device, format->base_address,
                             volume->volume_blocks,
                             member->first_block + index, block)) {
            ok = false;
            break;
        }
        while (destination && written < amount) {
            ssize_t done = xx_io_write(destination, block + written,
                                       amount - written);
            if (done <= 0 || (size_t)done > amount - written) {
                ok = false;
                break;
            }
            written += (size_t)done;
        }
    }
    xx_mem_free(block);
    return ok;
}

/* Names are built by adam_make_name and adam_add_suffix; this re-checks the
 * result before anything is created on disk. */
static bool adam_safe_output_name(const char *name) {
    size_t length, index;
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
    return meaningful && !adam_is_device_stem(name, length);
}

static bool adam_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *adam_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool adam_set_record(Abstractformat *format, xx_archive_record *record,
                            const adam_member *member) {
    int64_t offset = format->base_address +
                     (int64_t)(member->first_block * 2U) * ADAM_SECTOR;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + 0x400;
    record->header_size = 0;
    record->data_offset = member->in_range ? offset : 0;
    record->compressed_size = member->size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        0U) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        member->attributes) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (member->has_timestamp &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        member->timestamp))
        return false;
    return true;
}

void xx_adam_init(xx_adam *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_ADAM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-coleco-adam-disk");
    xx_format_set_extension(&archive->format, "dsk");
    archive->format.check_is_valid = xx_adam_check_is_valid;
    archive->format.handle_base_info = xx_adam_handle_base_info;
    archive->format.get_format_size = xx_adam_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_adam_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_adam_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_adam_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_adam_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_adam_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_adam_free_archive_records_reading;
    archive->image_size = -1;
}

xx_adam *xx_adam_create(xx_io_device *device, int64_t base_address) {
    xx_adam *archive = (xx_adam *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_adam_init(archive, device, base_address);
    return archive;
}

void xx_adam_destroy(xx_adam *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_adam_free(xx_adam *archive) {
    if (!archive) return;
    xx_adam_destroy(archive);
    xx_mem_free(archive);
}

bool xx_adam_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    adam_volume volume;
    (void)pd;
    return adam_probe(format, &volume);
}

bool xx_adam_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    adam_stream *stream;
    xx_adam *archive;
    (void)pd;
    if (!format || !adam_parse(format, &stream)) return false;
    archive = (xx_adam *)format;
    archive->number_of_records = stream->count;
    archive->image_size = stream->volume.image_size;
    archive->volume_blocks = stream->volume.volume_blocks;
    archive->declared_blocks = stream->volume.declared_blocks;
    archive->directory_blocks = stream->volume.directory_blocks;
    xx_rt_memcpy(archive->volume_name, stream->volume.name,
                 sizeof(archive->volume_name));
    format->number_of_archive_records = stream->count;
    format->format_size = stream->volume.image_size;
    format->is_valid = true;
    format->base_info_handled = true;
    adam_stream_free(stream);
    return true;
}

int64_t xx_adam_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_adam_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_adam_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_adam_handle_base_info(format, pd))
               ? ((xx_adam *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_adam_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    adam_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!adam_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        adam_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = adam_stream_free;
    state->total_records = stream->count;
    if (!adam_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count == 0U) {
        state->has_record = false;
        return state;
    }
    if (!adam_set_record(format, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_adam_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_adam_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    adam_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (adam_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = adam_set_record(format, &state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_adam_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    adam_stream *stream;
    const adam_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (adam_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!member->in_range) return false;
    path_option = adam_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: read the blocks through, which verifies them. */
        return adam_copy_member(format, &stream->volume, member, NULL, pd);
    if (!adam_safe_output_name(member->name)) return false;
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
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = adam_copy_member(format, &stream->volume, member,
                                  destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_adam_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
