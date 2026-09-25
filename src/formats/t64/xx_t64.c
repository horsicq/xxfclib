/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Commodore 64 T64 tape container.  xx_t64.h carries the field table.
 * Written from the format description (Peter Schepers' T64.TXT) and checked
 * against Deark's t64 module, XArchive's games/xt64.cpp and VICE's c1541.
 *
 * Acceptance: the description starts with one of the strings VICE accepts
 * ("C64 tape image file", "C64S tape file", "C64S tape image file"; "C64
 * tape file" is tolerated too), the directory has at least one slot, and at
 * least one slot holds an entry of type 1..5 whose data starts past its own
 * slot and inside the file.  Slots with a reserved type (6..255) or with
 * data pointing into the header or directory are skipped and counted, as
 * VICE skips what it does not understand.  The version and the used-slot
 * count are not checked: VICE ignores both and writers are known to leave
 * the count at 0.
 *
 * Directory: the slot count at 0x22 is trusted only as far as the file and
 * the data go.  Reading stops at the end of the file and at the first slot
 * that would lie inside member data already seen, so a header that
 * overstates its slot count does not turn program bytes into entries.
 *
 * Lengths: end - start (an end address of 0 means 0x10000).  Many T64s carry
 * a wrong end address (the $C3C6 converter bug among others), so when that
 * length is impossible or reaches past the next member's data (in offset
 * order) or past the end of the file, the member gets the gap to the next
 * member instead, capped at what fits above its load address.  VICE fixes
 * these files the same way.  Members that share an offset share one span:
 * all but the last of them in slot order are empty, so a directory full of
 * aliases cannot multiply the file on extraction.
 *
 * Output: the 2-byte load address, then the data - the PRG layout Deark and
 * c1541 write.  Names are PETSCII: printable ASCII is kept, the shifted
 * letters 0xC1..0xDA become A..Z, a shifted space becomes a space and
 * everything else (graphics, control codes, the Windows reserved
 * punctuation) becomes '_'.  '~' is never produced by that mapping, so it
 * can mark renamed duplicates.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/t64/xx_t64.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as T64 is registered there. */
#ifdef T64
#define XX_T64_FILE_TYPE XX_FILE_TYPE_T64
#else
#define XX_T64_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define T64_HEADER ((int64_t)XX_T64_HEADER_SIZE)
#define T64_ENTRY ((int64_t)XX_T64_ENTRY_SIZE)
/* The directory is read 4 KiB at a time, never whole: the slot count is a
 * u16, so a hostile header could otherwise ask for 2 MiB up front. */
#define T64_CHUNK_ENTRIES 128U
#define T64_TYPE_FREE 0U
#define T64_TYPE_MAX 5U
#define T64_NAME_FIELD 16U
/* Decoded name (16) or "FILE<slot>" (9), an optional '_' prefix for a
 * device name, "~<slot>" for a renamed duplicate (6), the terminator. */
#define T64_NAME_BUFFER 32U
#define T64_LOAD_ADDRESS_SIZE 2
#define T64_ADDRESS_SPACE INT64_C(0x10000)
#define T64_COPY_CHUNK 65536U

typedef struct t64_member_s {
    char name[T64_NAME_BUFFER];
    int64_t header_offset; /**< Absolute offset of the directory slot. */
    int64_t offset;        /**< Absolute offset of the data. */
    int64_t size;          /**< Data bytes taken from the container. */
    int64_t declared;      /**< end - start, or -1 when impossible. */
    uint32_t slot;
    uint32_t start_address;
    uint32_t end_address;
    uint8_t entry_type;
    uint8_t file_type;
    bool end_fixed;
} t64_member;

typedef struct t64_stream_s {
    t64_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t version;
    uint32_t directory_slots;
    uint32_t scanned_slots;
    uint32_t used_slots;
    uint32_t truncated_entries;
    uint32_t skipped_entries;
    uint32_t fixed_entries;
    char tape_name[25];
} t64_stream;

typedef struct t64_dir_reader_s {
    xx_io_device *device;
    int64_t base;
    uint32_t entries;
    uint32_t first;
    uint32_t loaded;
    uint8_t buffer[T64_CHUNK_ENTRIES * XX_T64_ENTRY_SIZE];
} t64_dir_reader;

typedef struct t64_order_s {
    int64_t offset;
    uint32_t item;
} t64_order;

typedef struct t64_name_key_s {
    char key[T64_NAME_BUFFER];
    uint32_t item;
} t64_name_key;

static uint32_t t64_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t t64_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool t64_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool t64_write_all(xx_io_device *destination, const uint8_t *data,
                          size_t size) {
    size_t written = 0U;
    while (written < size) {
        ssize_t amount = xx_io_write(destination, data + written,
                                     size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

/* Stream `size` bytes at `offset` into `destination` (or just read them
 * through when it is NULL) in fixed chunks. */
static bool t64_copy_range(xx_io_device *source, int64_t offset, int64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t remaining = size;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(T64_COPY_CHUNK);
    if (!buffer) return false;
    while (remaining > 0) {
        size_t chunk = remaining > (int64_t)T64_COPY_CHUNK
                           ? (size_t)T64_COPY_CHUNK
                           : (size_t)remaining;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !t64_read_at(source, offset + (size - remaining), buffer, chunk) ||
            (destination && !t64_write_all(destination, buffer, chunk))) {
            ok = false;
            break;
        }
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* Directory slot `index`, reading the directory a chunk at a time. */
static const uint8_t *t64_dir_entry(t64_dir_reader *reader, uint32_t index) {
    if (index >= reader->entries) return NULL;
    if (reader->loaded == 0U || index < reader->first ||
        index - reader->first >= reader->loaded) {
        uint32_t first = index - index % T64_CHUNK_ENTRIES;
        uint32_t count = reader->entries - first;
        if (count > T64_CHUNK_ENTRIES) count = T64_CHUNK_ENTRIES;
        reader->loaded = 0U;
        if (!t64_read_at(reader->device,
                         reader->base + T64_HEADER + (int64_t)first * T64_ENTRY,
                         reader->buffer, (size_t)count * XX_T64_ENTRY_SIZE))
            return NULL;
        reader->first = first;
        reader->loaded = count;
    }
    return reader->buffer +
           (size_t)(index - reader->first) * XX_T64_ENTRY_SIZE;
}

static char t64_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* The bytes at `text` spell `word` (given in upper case), ignoring case. */
static bool t64_word_is(const uint8_t *text, const char *word) {
    size_t index;
    for (index = 0U; word[index]; ++index)
        if (t64_upper((char)text[index]) != word[index]) return false;
    return true;
}

/* "C64" ["S"] " TAPE " ["IMAGE "] "FILE", the tape words in any case.  The
 * longest form ends at byte 20 of the 32-byte description. */
static bool t64_signature_ok(const uint8_t *header) {
    size_t at = 3U;
    if (header[0] != 'C' || header[1] != '6' || header[2] != '4') return false;
    if (header[at] == 'S') ++at;
    if (header[at] != ' ') return false;
    ++at;
    if (!t64_word_is(header + at, "TAPE ")) return false;
    at += 5U;
    if (t64_word_is(header + at, "IMAGE ")) at += 6U;
    return t64_word_is(header + at, "FILE");
}

static bool t64_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || t64_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$ and CLOCK$, with or
 * without an extension, in any case, trailing spaces of the stem ignored. */
static bool t64_is_device_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length, stem = 0U, index;
    length = xx_str_len(name);
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (t64_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((t64_upper(name[0]) == 'C' && t64_upper(name[1]) == 'O' &&
             t64_upper(name[2]) == 'M') ||
            (t64_upper(name[0]) == 'L' && t64_upper(name[1]) == 'P' &&
             t64_upper(name[2]) == 'T'));
}

static void t64_append_number(char *name, uint32_t value) {
    char digits[12];
    size_t length = xx_str_len(name), count = 0U;
    if (value == 0U) digits[count++] = '0';
    while (value != 0U && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    }
    while (count > 0U && length + 1U < T64_NAME_BUFFER)
        name[length++] = digits[--count];
    name[length] = 0;
}

/* The 16-byte PETSCII name, as a name that is safe to create anywhere.
 * The field ends at a NUL; trailing spaces and shifted spaces are padding.
 * Trailing dots become '_' (Windows would drop them and merge names), an
 * empty name becomes FILE<slot> and a device name gets a '_' in front. */
static void t64_decode_name(const uint8_t *field, uint32_t slot, char *out) {
    size_t length = 0U, used = 0U, index;
    char decoded[T64_NAME_FIELD + 1U];
    while (length < T64_NAME_FIELD && field[length] != 0U) ++length;
    while (length > 0U &&
           (field[length - 1U] == 0x20U || field[length - 1U] == 0xA0U))
        --length;
    for (index = 0U; index < length; ++index) {
        uint8_t c = field[index];
        char mapped;
        if (c == 0xA0U)
            mapped = ' ';
        else if (c >= 0xC1U && c <= 0xDAU)
            mapped = (char)(c - 0x80U);
        else if (c < 0x20U || c > 0x7DU || c == '/' || c == '\\' ||
                 c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                 c == '>' || c == '|')
            mapped = '_';
        else
            mapped = (char)c;
        decoded[used++] = mapped;
    }
    decoded[used] = 0;
    for (index = used; index > 0U && decoded[index - 1U] == '.'; --index)
        decoded[index - 1U] = '_';
    out[0] = 0;
    if (used == 0U) {
        out[0] = 'F';
        out[1] = 'I';
        out[2] = 'L';
        out[3] = 'E';
        out[4] = 0;
        t64_append_number(out, slot);
        return;
    }
    index = 0U;
    if (t64_is_device_name(decoded)) out[index++] = '_';
    xx_rt_memcpy(out + index, decoded, used + 1U);
}

static int t64_compare_order(const void *left, const void *right) {
    const t64_order *a = (const t64_order *)left;
    const t64_order *b = (const t64_order *)right;
    if (a->offset != b->offset) return a->offset < b->offset ? -1 : 1;
    return a->item < b->item ? -1 : (a->item > b->item ? 1 : 0);
}

static int t64_compare_keys(const void *left, const void *right) {
    const t64_name_key *a = (const t64_name_key *)left;
    const t64_name_key *b = (const t64_name_key *)right;
    int order = xx_str_cmp(a->key, b->key);
    if (order != 0) return order;
    return a->item < b->item ? -1 : (a->item > b->item ? 1 : 0);
}

/* Names equal but for case would overwrite each other on extraction: every
 * later member of such a group gets "~<slot>".  Decoded names never hold a
 * '~' and slots are unique, so one pass leaves every name unique. */
static bool t64_make_names_unique(t64_member *items, size_t count) {
    t64_name_key *keys;
    size_t index;
    if (count < 2U) return true;
    keys = (t64_name_key *)xx_mem_alloc(count * sizeof(*keys));
    if (!keys) return false;
    for (index = 0U; index < count; ++index) {
        size_t at;
        for (at = 0U; at + 1U < T64_NAME_BUFFER && items[index].name[at]; ++at)
            keys[index].key[at] = t64_upper(items[index].name[at]);
        keys[index].key[at] = 0;
        keys[index].item = (uint32_t)index;
    }
    xx_rt_qsort(keys, count, sizeof(*keys), t64_compare_keys);
    for (index = 1U; index < count; ++index) {
        t64_member *member;
        size_t length;
        if (xx_str_cmp(keys[index].key, keys[index - 1U].key) != 0) continue;
        member = &items[keys[index].item];
        length = xx_str_len(member->name);
        if (length + 7U > T64_NAME_BUFFER) {
            xx_mem_free(keys);
            return false;
        }
        member->name[length] = '~';
        member->name[length + 1U] = 0;
        t64_append_number(member->name, member->slot);
    }
    xx_mem_free(keys);
    return true;
}

/* Assign every member its data length (see the file comment).  `size` is
 * the number of bytes present from the container start. */
static bool t64_assign_sizes(t64_member *items, size_t count, int64_t base,
                             int64_t size, uint32_t *fixed) {
    t64_order *order;
    size_t index;
    *fixed = 0U;
    if (count == 0U) return true;
    order = (t64_order *)xx_mem_alloc(count * sizeof(*order));
    if (!order) return false;
    for (index = 0U; index < count; ++index) {
        order[index].offset = items[index].offset;
        order[index].item = (uint32_t)index;
    }
    xx_rt_qsort(order, count, sizeof(*order), t64_compare_order);
    for (index = 0U; index < count; ++index) {
        t64_member *member = &items[order[index].item];
        int64_t next = index + 1U < count ? order[index + 1U].offset
                                          : base + size;
        int64_t limit = next - member->offset;
        int64_t room = T64_ADDRESS_SPACE - (int64_t)member->start_address;
        if (limit < 0) limit = 0;
        if (member->declared >= 0 && member->declared <= limit) {
            member->size = member->declared;
        } else {
            member->size = limit < room ? limit : room;
            member->end_fixed = true;
            ++*fixed;
        }
    }
    xx_mem_free(order);
    return true;
}

static void t64_stream_free(void *opaque) {
    t64_stream *stream = (t64_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static void t64_decode_tape_name(const uint8_t *field, char *out) {
    size_t length = 24U, index;
    while (length > 0U && (field[length - 1U] == 0x20U ||
                           field[length - 1U] == 0xA0U ||
                           field[length - 1U] == 0U))
        --length;
    for (index = 0U; index < length; ++index)
        out[index] = (field[index] >= 0x20U && field[index] <= 0x7EU)
                         ? (char)field[index] : '_';
    out[length] = 0;
}

static bool t64_parse(Abstractformat *format, t64_stream **result) {
    t64_dir_reader *reader = NULL;
    t64_member *items = NULL;
    t64_stream *stream = NULL;
    uint8_t header[XX_T64_HEADER_SIZE];
    int64_t total, size, archive_size;
    uint32_t slots, readable, scanned = 0U, index;
    uint32_t present = 0U, truncated = 0U, skipped = 0U, fixed = 0U;
    size_t count = 0U, pass;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    *result = NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* The header, one slot and at least the member's first data byte. */
    if (size < T64_HEADER + T64_ENTRY ||
        !t64_read_at(format->device, format->base_address, header,
                     sizeof(header)) ||
        !t64_signature_ok(header))
        return false;
    slots = t64_le16(header + 0x22U);
    if (slots == 0U) return false;
    readable = (uint32_t)((size - T64_HEADER) / T64_ENTRY < (int64_t)slots
                              ? (size - T64_HEADER) / T64_ENTRY
                              : (int64_t)slots);

    reader = (t64_dir_reader *)xx_mem_alloc(sizeof(*reader));
    if (!reader) return false;

    /* Pass 0 validates and counts, so garbage falls out before anything
     * sized by the directory is allocated; pass 1 fills the member table.
     * Both passes stop at the same slot: the stop depends only on bytes
     * already read. */
    for (pass = 0U; pass < 2U; ++pass) {
        int64_t lowest = size; /* Lowest data offset seen, relative. */
        xx_mem_zero(reader, sizeof(*reader));
        reader->device = format->device;
        reader->base = format->base_address;
        reader->entries = readable;
        for (index = 0U; index < readable; ++index) {
            int64_t position = T64_HEADER + (int64_t)index * T64_ENTRY;
            const uint8_t *entry;
            t64_member member;
            uint32_t start, end;
            int64_t offset, declared;
            if (position >= lowest) break;
            entry = t64_dir_entry(reader, index);
            if (!entry) goto fail;
            if (pass == 0U) scanned = index + 1U;
            if (entry[0] == T64_TYPE_FREE) continue;
            offset = (int64_t)t64_le32(entry + 8U);
            /* A reserved entry type, or data that would start in the header
             * or the directory, cannot be extracted: the slot is skipped,
             * as VICE skips entries it does not understand. */
            if (entry[0] > T64_TYPE_MAX || offset < position + T64_ENTRY) {
                if (pass == 0U) ++skipped;
                continue;
            }
            start = t64_le16(entry + 2U);
            end = t64_le16(entry + 4U);
            if (end > start)
                declared = (int64_t)(end - start);
            else if (end == 0U)
                declared = T64_ADDRESS_SPACE - (int64_t)start;
            else if (end == start)
                declared = 0;
            else
                declared = -1;
            /* Data that starts past the end of the file (or at it, for a
             * non-empty member) is a truncated container: not listed. */
            if (offset > size || (offset == size && declared != 0)) {
                if (pass == 0U) ++truncated;
                continue;
            }
            if (offset < lowest) lowest = offset;
            if (pass == 0U) {
                ++present;
                continue;
            }
            if (count >= (size_t)present) goto fail;
            xx_mem_zero(&member, sizeof(member));
            member.slot = index;
            member.header_offset = format->base_address + position;
            member.offset = format->base_address + offset;
            member.declared = declared;
            member.start_address = start;
            member.end_address = end;
            member.entry_type = entry[0];
            member.file_type = entry[1];
            t64_decode_name(entry + 0x10U, index, member.name);
            items[count++] = member;
        }
        if (pass == 0U) {
            if (present == 0U) goto fail;
            items = (t64_member *)xx_mem_alloc((size_t)present *
                                               sizeof(*items));
            if (!items) goto fail;
        }
    }
    if (count != (size_t)present ||
        !t64_assign_sizes(items, count, format->base_address, size, &fixed) ||
        !t64_make_names_unique(items, count))
        goto fail;

    archive_size = T64_HEADER + (int64_t)scanned * T64_ENTRY;
    for (index = 0U; (size_t)index < count; ++index) {
        int64_t end = items[index].offset - format->base_address +
                      items[index].size;
        if (end > archive_size) archive_size = end;
    }
    if (archive_size > size) archive_size = size;

    stream = (t64_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->items = items;
    stream->count = count;
    stream->archive_size = archive_size;
    stream->version = t64_le16(header + 0x20U);
    stream->directory_slots = slots;
    stream->scanned_slots = scanned;
    stream->used_slots = t64_le16(header + 0x24U);
    stream->truncated_entries = truncated;
    stream->skipped_entries = skipped;
    stream->fixed_entries = fixed;
    t64_decode_tape_name(header + 0x28U, stream->tape_name);
    xx_mem_free(reader);
    *result = stream;
    return true;
fail:
    if (reader) xx_mem_free(reader);
    if (items) xx_mem_free(items);
    return false;
}

static bool t64_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *t64_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* Extraction writes <base>/<name>.  Decoding already produced a safe name;
 * this re-checks it before anything touches the file system: no control
 * bytes, separators, drive colons or reserved punctuation, not only dots
 * and spaces, no trailing dot or space, and no device name. */
static bool t64_safe_output_name(const char *name) {
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
    if (!meaningful || name[length - 1U] == '.' || name[length - 1U] == ' ')
        return false;
    return !t64_is_device_name(name);
}

static bool t64_set_record(xx_archive_record *record,
                           const t64_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = XX_T64_ENTRY_SIZE;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->size + T64_LOAD_ADDRESS_SIZE) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* The PRG image: load address, then the data. */
static bool t64_unpack_member(Abstractformat *format, const t64_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t load[T64_LOAD_ADDRESS_SIZE];
    if (member->size < 0 || member->offset < 0) return false;
    load[0] = (uint8_t)(member->start_address & 0xFFU);
    load[1] = (uint8_t)(member->start_address >> 8U);
    if (destination && !t64_write_all(destination, load, sizeof(load)))
        return false;
    return t64_copy_range(format->device, member->offset, member->size,
                          destination, pd);
}

void xx_t64_init(xx_t64 *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_T64_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-t64");
    xx_format_set_extension(&archive->format, "t64");
    archive->format.check_is_valid = xx_t64_check_is_valid;
    archive->format.handle_base_info = xx_t64_handle_base_info;
    archive->format.get_format_size = xx_t64_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_t64_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_t64_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_t64_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_t64_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_t64_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_t64_free_archive_records_reading;
}

xx_t64 *xx_t64_create(xx_io_device *device, int64_t base_address) {
    xx_t64 *archive = (xx_t64 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_t64_init(archive, device, base_address);
    return archive;
}

void xx_t64_destroy(xx_t64 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_t64_free(xx_t64 *archive) {
    if (!archive) return;
    xx_t64_destroy(archive);
    xx_mem_free(archive);
}

bool xx_t64_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    t64_stream *stream;
    (void)pd;
    if (!t64_parse(format, &stream)) return false;
    t64_stream_free(stream);
    return true;
}

bool xx_t64_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    t64_stream *stream;
    xx_t64 *archive;
    (void)pd;
    if (!format || !t64_parse(format, &stream)) return false;
    archive = (xx_t64 *)format;
    archive->number_of_records = stream->count;
    archive->version = stream->version;
    archive->directory_slots = stream->directory_slots;
    archive->scanned_slots = stream->scanned_slots;
    archive->used_slots = stream->used_slots;
    archive->truncated_entries = stream->truncated_entries;
    archive->skipped_entries = stream->skipped_entries;
    archive->fixed_entries = stream->fixed_entries;
    xx_rt_memcpy(archive->tape_name, stream->tape_name,
                 sizeof(archive->tape_name));
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    t64_stream_free(stream);
    return true;
}

int64_t xx_t64_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_t64_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_t64_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_t64_handle_base_info(format, pd))
               ? ((xx_t64 *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_t64_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    t64_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!t64_parse(format, &stream)) return NULL;
    /* t64_parse never accepts a container without members; kept as a guard. */
    if (stream->count == 0U) {
        t64_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        t64_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = t64_stream_free;
    state->total_records = stream->count;
    if (!t64_copy_options(&state->options, options) ||
        !t64_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_t64_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_t64_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    t64_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (t64_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = t64_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_t64_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    t64_stream *stream;
    t64_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (t64_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = t64_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: read the member through, which verifies it. */
        return t64_unpack_member(format, member, NULL, pd);
    if (!t64_safe_output_name(member->name)) return false;
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
        result = t64_unpack_member(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_t64_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
