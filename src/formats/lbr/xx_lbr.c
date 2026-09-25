/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CP/M LU library (.LBR).  xx_lbr.h carries the directory entry layout.
 * Written from the format description (LU 3.00 / NULU documentation) and
 * checked against Deark's lbr module and XArchive's archives/xlbr.cpp.
 *
 * The first 32-byte entry is the directory describing itself: status 0,
 * eleven spaces, first sector 0 and a nonzero sector count.  That is the
 * only fixed signature, so acceptance also rests on the directory walk:
 * every status byte is 0x00, 0xFE or 0xFF, every active entry carries a
 * well-formed 8.3 name, no member starts inside the directory, no two members
 * share a sector, and at least one non-empty member is actually present.
 *
 * The member list ends at the first unused (0xFF) entry, as in LU and Deark;
 * like XArchive, the entries after it must all be unused (or deleted).
 * Members whose data runs past EOF (a truncated download) are left out of the
 * listing and counted in truncated_entries.  Members are copied out raw: the
 * squeezed / crunched members LBRs usually hold are separate formats.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lbr/xx_lbr.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as LBR is registered there. */
#ifdef LBR
#define XX_LBR_FILE_TYPE XX_FILE_TYPE_LBR
#else
#define XX_LBR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define LBR_SECTOR ((int64_t)XX_LBR_SECTOR_SIZE)
#define LBR_ENTRY ((int64_t)XX_LBR_ENTRY_SIZE)
#define LBR_ENTRIES_PER_SECTOR 4U
/* The directory is read 4 KiB at a time, never whole: its sector count is a
 * u16, so a hostile header could otherwise ask for 8 MiB up front. */
#define LBR_CHUNK_ENTRIES 128U
#define LBR_STATUS_ACTIVE 0x00U
#define LBR_STATUS_DELETED 0xFEU
#define LBR_STATUS_UNUSED 0xFFU
#define LBR_NAME_FIELD 8U
#define LBR_EXT_FIELD 3U
/* No CP/M disk ever held this many files; a directory claiming more active
 * entries is not a library, and the cap bounds every allocation below. */
#define LBR_MAX_MEMBERS 65535U
/* "NAME.EXT" (12) + " (" + up to 6 digits + ")" for a renamed duplicate
 * (9), plus the terminator. */
#define LBR_NAME_BUFFER 24U
#define LBR_COPY_CHUNK 65536U
/* LBR dates count days from 1977-12-31 (day 1 is 1978-01-01); that day is
 * 2921 days after the Unix epoch. */
#define LBR_EPOCH_DAYS INT64_C(2921)

typedef struct lbr_member_s {
    char name[LBR_NAME_BUFFER];
    int64_t header_offset; /**< Absolute offset of the directory entry. */
    int64_t offset;        /**< Absolute offset of the member data. */
    int64_t size;          /**< Sectors * 128 minus the pad count. */
    uint32_t start_sector;
    uint32_t sectors;
    uint32_t entry; /**< Directory entry index. */
    uint64_t timestamp;
    bool has_timestamp;
} lbr_member;

typedef struct lbr_stream_s {
    lbr_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t directory_sectors;
    uint32_t active_entries;
    uint32_t truncated_entries;
} lbr_stream;

typedef enum lbr_entry_kind_e {
    LBR_ENTRY_ACTIVE = 0,
    LBR_ENTRY_DELETED,
    LBR_ENTRY_END,
    LBR_ENTRY_BAD
} lbr_entry_kind;

typedef struct lbr_dir_reader_s {
    xx_io_device *device;
    int64_t base;
    uint32_t entries;
    uint32_t first;
    uint32_t loaded;
    uint8_t buffer[LBR_CHUNK_ENTRIES * XX_LBR_ENTRY_SIZE];
} lbr_dir_reader;

typedef struct lbr_extent_s {
    int64_t start;
    int64_t end;
} lbr_extent;

typedef struct lbr_name_key_s {
    char key[LBR_NAME_BUFFER];
    uint32_t item;
} lbr_name_key;

static uint32_t lbr_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static bool lbr_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Stream `size` bytes at `offset` into `destination` (or just read them
 * through when it is NULL) in fixed chunks. */
static bool lbr_copy_range(xx_io_device *source, int64_t offset, int64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t remaining = size;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(LBR_COPY_CHUNK);
    if (!buffer) return false;
    while (ok && remaining > 0) {
        size_t chunk = remaining > (int64_t)LBR_COPY_CHUNK
                           ? (size_t)LBR_COPY_CHUNK
                           : (size_t)remaining;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !lbr_read_at(source, offset + (size - remaining), buffer, chunk)) {
            ok = false;
            break;
        }
        while (destination && written < chunk) {
            ssize_t amount = xx_io_write(destination, buffer + written,
                                         chunk - written);
            if (amount <= 0 || (size_t)amount > chunk - written) {
                ok = false;
                break;
            }
            written += (size_t)amount;
        }
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* Directory entry `index`, reading the directory a chunk at a time. */
static const uint8_t *lbr_dir_entry(lbr_dir_reader *reader, uint32_t index) {
    if (index >= reader->entries) return NULL;
    if (reader->loaded == 0U || index < reader->first ||
        index - reader->first >= reader->loaded) {
        uint32_t first = index - index % LBR_CHUNK_ENTRIES;
        uint32_t count = reader->entries - first;
        if (count > LBR_CHUNK_ENTRIES) count = LBR_CHUNK_ENTRIES;
        reader->loaded = 0U;
        if (!lbr_read_at(reader->device,
                         reader->base + (int64_t)first * LBR_ENTRY,
                         reader->buffer, (size_t)count * XX_LBR_ENTRY_SIZE))
            return NULL;
        reader->first = first;
        reader->loaded = count;
    }
    return reader->buffer +
           (size_t)(index - reader->first) * XX_LBR_ENTRY_SIZE;
}

/* One space-padded 8.3 component.  Bit 7 is a CP/M attribute flag and is
 * dropped.  Characters are printable ASCII, spaces only as trailing padding;
 * the characters Windows reserves become '_'.  So a decoded name never holds
 * a separator, a drive colon, a control byte or a space. */
static bool lbr_decode_field(const uint8_t *field, size_t width, char *out,
                             size_t *length) {
    size_t index, used = 0U;
    bool ended = false;
    for (index = 0U; index < width; ++index) {
        uint8_t c = (uint8_t)(field[index] & 0x7FU);
        if (c == 0x20U) {
            ended = true;
            continue;
        }
        if (ended || c < 0x21U || c > 0x7EU) return false;
        if (c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            c = '_';
        out[used++] = (char)c;
    }
    *length = used;
    return true;
}

/* LBR time of day is in DOS layout; an impossible one is ignored. */
static bool lbr_timestamp(uint32_t date, uint32_t time, uint64_t *out) {
    int64_t seconds;
    uint32_t hours = time >> 11U, minutes = (time >> 5U) & 0x3FU,
             halves = time & 0x1FU;
    if (date == 0U) return false;
    seconds = (LBR_EPOCH_DAYS + (int64_t)date) * INT64_C(86400);
    if (hours < 24U && minutes < 60U && halves < 30U)
        seconds += (int64_t)hours * 3600 + (int64_t)minutes * 60 +
                   (int64_t)halves * 2;
    *out = (uint64_t)seconds;
    return true;
}

/* Classify one directory entry.  For an active entry fill `member` (offsets
 * relative to the library start) and say whether its data is inside the
 * `size` bytes present. */
static lbr_entry_kind lbr_decode_entry(const uint8_t *entry,
                                       uint32_t directory_sectors,
                                       int64_t size, lbr_member *member,
                                       bool *present) {
    size_t name_length, ext_length;
    uint32_t pad;
    uint32_t date, time;
    if (entry[0] == LBR_STATUS_UNUSED) return LBR_ENTRY_END;
    if (entry[0] == LBR_STATUS_DELETED) return LBR_ENTRY_DELETED;
    if (entry[0] != LBR_STATUS_ACTIVE) return LBR_ENTRY_BAD;
    xx_mem_zero(member, sizeof(*member));
    if (!lbr_decode_field(entry + 1U, LBR_NAME_FIELD, member->name,
                          &name_length) ||
        name_length == 0U ||
        !lbr_decode_field(entry + 1U + LBR_NAME_FIELD, LBR_EXT_FIELD,
                          member->name + name_length + 1U, &ext_length))
        return LBR_ENTRY_BAD;
    if (ext_length > 0U) {
        member->name[name_length] = '.';
        member->name[name_length + 1U + ext_length] = 0;
    } else {
        member->name[name_length] = 0;
    }
    member->start_sector = lbr_le16(entry + 12U);
    member->sectors = lbr_le16(entry + 14U);
    pad = entry[26];
    /* LU before 3.0 left the pad count at 0; a value of a whole sector or
     * more is not a pad count at all (Deark ignores it the same way). */
    if (pad >= (uint32_t)LBR_SECTOR || member->sectors == 0U) pad = 0U;
    /* A member may not start inside the directory. */
    if (member->sectors != 0U && member->start_sector < directory_sectors)
        return LBR_ENTRY_BAD;
    member->offset = (int64_t)member->start_sector * LBR_SECTOR;
    member->size = (int64_t)member->sectors * LBR_SECTOR - (int64_t)pad;
    *present = member->sectors == 0U ||
               (member->offset <= size && member->size <= size - member->offset);
    if (member->sectors == 0U) member->offset = 0;
    date = lbr_le16(entry + 20U);
    time = lbr_le16(entry + 24U);
    if (date == 0U) {
        date = lbr_le16(entry + 18U);
        time = lbr_le16(entry + 22U);
    }
    member->has_timestamp = lbr_timestamp(date, time, &member->timestamp);
    return LBR_ENTRY_ACTIVE;
}

static int lbr_compare_extents(const void *left, const void *right) {
    const lbr_extent *a = (const lbr_extent *)left;
    const lbr_extent *b = (const lbr_extent *)right;
    return a->start < b->start ? -1 : (a->start > b->start ? 1 : 0);
}

static int lbr_compare_keys(const void *left, const void *right) {
    const lbr_name_key *a = (const lbr_name_key *)left;
    const lbr_name_key *b = (const lbr_name_key *)right;
    int order = xx_str_cmp(a->key, b->key);
    if (order != 0) return order;
    return a->item < b->item ? -1 : (a->item > b->item ? 1 : 0);
}

/* No two members may share a sector: LU never writes that, and entries that
 * all point at one large extent would turn a small file into a huge
 * extraction. */
static bool lbr_check_overlap(const lbr_member *items, size_t count) {
    lbr_extent *extents;
    size_t used = 0U, index;
    bool ok = true;
    if (count < 2U) return true;
    extents = (lbr_extent *)xx_mem_alloc(count * sizeof(*extents));
    if (!extents) return false;
    for (index = 0U; index < count; ++index) {
        if (items[index].sectors == 0U) continue;
        extents[used].start = (int64_t)items[index].start_sector;
        extents[used].end = (int64_t)items[index].start_sector +
                            (int64_t)items[index].sectors;
        ++used;
    }
    if (used > 1U) {
        xx_rt_qsort(extents, used, sizeof(*extents), lbr_compare_extents);
        for (index = 1U; index < used; ++index)
            if (extents[index].start < extents[index - 1U].end) {
                ok = false;
                break;
            }
    }
    xx_mem_free(extents);
    return ok;
}

/* Append " (<entry>)" to a name.  Decoded names never contain a space, so a
 * renamed member cannot collide with a stored one, and the entry index is
 * unique, so renamed members cannot collide with each other. */
static bool lbr_append_entry(char *name, uint32_t entry) {
    char digits[12];
    size_t length = xx_str_len(name), count = 0U;
    if (entry == 0U) digits[count++] = '0';
    while (entry != 0U && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (char)(entry % 10U));
        entry /= 10U;
    }
    if (length + 3U + count >= LBR_NAME_BUFFER) return false;
    name[length++] = ' ';
    name[length++] = '(';
    while (count > 0U) name[length++] = digits[--count];
    name[length++] = ')';
    name[length] = 0;
    return true;
}

/* LBR names are 8.3 CP/M names; a library can still hold the same name
 * twice, or names equal but for case.  Every later member of such a group
 * is renamed so extraction never overwrites an earlier one, even on a
 * case-insensitive file system. */
static bool lbr_make_names_unique(lbr_member *items, size_t count) {
    lbr_name_key *keys;
    size_t index;
    if (count < 2U) return true;
    keys = (lbr_name_key *)xx_mem_alloc(count * sizeof(*keys));
    if (!keys) return false;
    for (index = 0U; index < count; ++index) {
        size_t at;
        for (at = 0U; at + 1U < LBR_NAME_BUFFER && items[index].name[at];
             ++at) {
            char c = items[index].name[at];
            keys[index].key[at] =
                (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        }
        keys[index].key[at] = 0;
        keys[index].item = (uint32_t)index;
    }
    xx_rt_qsort(keys, count, sizeof(*keys), lbr_compare_keys);
    for (index = 1U; index < count; ++index) {
        lbr_member *member;
        if (xx_str_cmp(keys[index].key, keys[index - 1U].key) != 0) continue;
        member = &items[keys[index].item];
        if (!lbr_append_entry(member->name, member->entry)) {
            xx_mem_free(keys);
            return false;
        }
    }
    xx_mem_free(keys);
    return true;
}

static void lbr_stream_free(void *opaque) {
    lbr_stream *stream = (lbr_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool lbr_parse(Abstractformat *format, lbr_stream **result) {
    lbr_dir_reader *reader = NULL;
    lbr_stream *stream = NULL;
    lbr_member *items = NULL;
    lbr_member member;
    uint8_t header[XX_LBR_ENTRY_SIZE];
    int64_t total, size, directory_size, archive_size;
    uint32_t directory_sectors, entries, index;
    uint32_t active = 0U, present_count = 0U, truncated = 0U;
    uint32_t data_members = 0U;
    size_t count = 0U;
    size_t pass;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    *result = NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < LBR_SECTOR ||
        !lbr_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;
    /* The directory's own entry: active, blank name, first sector 0. */
    if (header[0] != LBR_STATUS_ACTIVE) return false;
    for (index = 1U; index < 12U; ++index)
        if (header[index] != 0x20U) return false;
    if (lbr_le16(header + 12U) != 0U) return false;
    directory_sectors = lbr_le16(header + 14U);
    directory_size = (int64_t)directory_sectors * LBR_SECTOR;
    if (directory_sectors == 0U || directory_size > size) return false;
    entries = directory_sectors * LBR_ENTRIES_PER_SECTOR;

    reader = (lbr_dir_reader *)xx_mem_alloc(sizeof(*reader));
    if (!reader) return false;

    /* Pass 0 validates and counts, so garbage falls out before anything
     * sized by the directory is allocated; pass 1 fills the member table. */
    for (pass = 0U; pass < 2U; ++pass) {
        bool present = false;
        bool ended = false;
        xx_mem_zero(reader, sizeof(*reader));
        reader->device = format->device;
        reader->base = format->base_address;
        reader->entries = entries;
        for (index = 1U; index < entries; ++index) {
            const uint8_t *entry = lbr_dir_entry(reader, index);
            lbr_entry_kind kind;
            if (!entry) goto fail;
            if (ended) {
                /* LU fills the directory in order, so everything after the
                 * first unused entry is unused (or deleted) too.  An active
                 * entry or a stray status byte there is not a library. */
                if (entry[0] != LBR_STATUS_UNUSED &&
                    entry[0] != LBR_STATUS_DELETED)
                    goto fail;
                continue;
            }
            kind = lbr_decode_entry(entry, directory_sectors, size, &member,
                                    &present);
            if (kind == LBR_ENTRY_END) {
                /* Pass 1 only lists; pass 0 already checked the rest. */
                if (pass != 0U) break;
                ended = true;
                continue;
            }
            if (kind == LBR_ENTRY_DELETED) continue;
            if (kind == LBR_ENTRY_BAD) goto fail;
            if (pass == 0U) {
                if (++active > LBR_MAX_MEMBERS) goto fail;
                if (present) {
                    ++present_count;
                    if (member.sectors != 0U) ++data_members;
                } else {
                    ++truncated;
                }
                continue;
            }
            if (!present) continue;
            if (count >= (size_t)present_count) goto fail;
            member.entry = index;
            member.header_offset =
                format->base_address + (int64_t)index * LBR_ENTRY;
            member.offset += format->base_address;
            items[count++] = member;
        }
        if (pass == 0U) {
            /* A library with no member data left in it (no active entry,
             * only empty members, or everything past EOF) has nothing worth
             * listing, and a bare directory header is too weak a signature
             * to claim a file on. */
            if (data_members == 0U) goto fail;
            items = (lbr_member *)xx_mem_alloc((size_t)present_count *
                                               sizeof(*items));
            if (!items) goto fail;
        }
    }
    if (count != (size_t)present_count) goto fail;
    if (!lbr_check_overlap(items, count) ||
        !lbr_make_names_unique(items, count))
        goto fail;

    archive_size = directory_size;
    for (index = 0U; (size_t)index < count; ++index) {
        int64_t end;
        if (items[index].sectors == 0U) continue;
        end = ((int64_t)items[index].start_sector +
               (int64_t)items[index].sectors) * LBR_SECTOR;
        if (end > archive_size) archive_size = end;
    }
    /* A truncated library owns everything that is left of it. */
    if (truncated > 0U || archive_size > size) archive_size = size;

    stream = (lbr_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->items = items;
    stream->count = count;
    stream->archive_size = archive_size;
    stream->directory_sectors = directory_sectors;
    stream->active_entries = active;
    stream->truncated_entries = truncated;
    xx_mem_free(reader);
    *result = stream;
    return true;
fail:
    if (reader) xx_mem_free(reader);
    if (items) xx_mem_free(items);
    return false;
}

static bool lbr_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *lbr_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static char lbr_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool lbr_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || lbr_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* Extraction writes <base>/<name>.  Decoding already removed separators,
 * drive colons, reserved punctuation and control bytes; this also refuses
 * names Windows would resolve to "." or "..", names ending in a dot (which
 * Windows strips, so "A." would land on "A"), and device names such as CON,
 * LPT1.TXT or CONIN$, with or without an extension and in any case. */
static bool lbr_safe_output_name(const char *name) {
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
    if (!meaningful || name[length - 1U] == '.' || name[length - 1U] == ' ')
        return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (lbr_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((lbr_upper(name[0]) == 'C' && lbr_upper(name[1]) == 'O' &&
          lbr_upper(name[2]) == 'M') ||
         (lbr_upper(name[0]) == 'L' && lbr_upper(name[1]) == 'P' &&
          lbr_upper(name[2]) == 'T')))
        return false;
    return true;
}

static bool lbr_set_record(xx_archive_record *record,
                           const lbr_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = XX_LBR_ENTRY_SIZE;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    if (member->has_timestamp &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        member->timestamp))
        return false;
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

void xx_lbr_init(xx_lbr *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LBR_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lbr");
    xx_format_set_extension(&archive->format, "lbr");
    archive->format.check_is_valid = xx_lbr_check_is_valid;
    archive->format.handle_base_info = xx_lbr_handle_base_info;
    archive->format.get_format_size = xx_lbr_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lbr_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lbr_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lbr_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lbr_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lbr_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lbr_free_archive_records_reading;
}

xx_lbr *xx_lbr_create(xx_io_device *device, int64_t base_address) {
    xx_lbr *archive = (xx_lbr *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lbr_init(archive, device, base_address);
    return archive;
}

void xx_lbr_destroy(xx_lbr *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_lbr_free(xx_lbr *archive) {
    if (!archive) return;
    xx_lbr_destroy(archive);
    xx_mem_free(archive);
}

bool xx_lbr_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    lbr_stream *stream;
    (void)pd;
    if (!lbr_parse(format, &stream)) return false;
    lbr_stream_free(stream);
    return true;
}

bool xx_lbr_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    lbr_stream *stream;
    xx_lbr *archive;
    (void)pd;
    if (!format || !lbr_parse(format, &stream)) return false;
    archive = (xx_lbr *)format;
    archive->number_of_records = stream->count;
    archive->directory_sectors = stream->directory_sectors;
    archive->active_entries = stream->active_entries;
    archive->truncated_entries = stream->truncated_entries;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    lbr_stream_free(stream);
    return true;
}

int64_t xx_lbr_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lbr_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_lbr_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lbr_handle_base_info(format, pd))
               ? ((xx_lbr *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_lbr_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    lbr_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!lbr_parse(format, &stream)) return NULL;
    /* lbr_parse never accepts a library without members; kept as a guard. */
    if (stream->count == 0U) {
        lbr_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        lbr_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = lbr_stream_free;
    state->total_records = stream->count;
    if (!lbr_copy_options(&state->options, options) ||
        !lbr_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_lbr_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_lbr_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    lbr_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (lbr_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = lbr_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lbr_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    lbr_stream *stream;
    lbr_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (lbr_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->size < 0 || member->offset < 0) return false;
    path_option = lbr_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: read the member through, which verifies it. */
        return lbr_copy_range(format->device, member->offset, member->size,
                              NULL, pd);
    if (!lbr_safe_output_name(member->name)) return false;
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
        result = lbr_copy_range(format->device, member->offset, member->size,
                                destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_lbr_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
