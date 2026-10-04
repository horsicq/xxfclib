/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MS-DOS 3.3-5.x BACKUP set.  xx_ms_dos_backup2.h carries the field table.
 * Written from the layout of CONTROL.nnn; Deark's dosbackup.c (MIT) was
 * read to confirm the item lengths, the field order of a file item and that
 * a directory name is joined to its file names; no code was taken from it.
 *
 * The CONTROL file is a catalogue and BACKUP.nnn holds the member bytes with
 * no header of its own, so only the CONTROL file can be recognised.  Its
 * nine leading bytes (0x8B "BACKUP  ") are the signature; a CONTROL file of
 * the compressed variant some other BACKUP programs wrote differs in the
 * last two of them and is not taken.
 *
 * The catalogue is read into memory (capped) and walked item by item: every
 * item is length-prefixed with a fixed length, so each step consumes at
 * least 34 bytes and the walk ends at the end of the file.  A catalogue cut
 * short keeps the complete items in front of the cut.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ms_dos_backup2/xx_ms_dos_backup2.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as MS_DOS_BACKUP2 is registered. */
#ifdef MS_DOS_BACKUP2
#define XX_MS_DOS_BACKUP2_FILE_TYPE XX_FILE_TYPE_MS_DOS_BACKUP2
#else
#define XX_MS_DOS_BACKUP2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DBK_HEADER_SIZE 0x8BU
#define DBK_LAST_OFFSET 0x8AU
#define DBK_DIR_ITEM 70U
#define DBK_FILE_ITEM 34U
#define DBK_DIR_NAME 63U
#define DBK_FILE_NAME 12U
#define DBK_NO_NEXT 0xFFFFFFFFU
/* A hard-disk-to-hard-disk backup keeps the whole catalogue in one CONTROL
 * file; 32 MiB is about a million file items. */
#define DBK_MAX_CONTROL (32U * 1024U * 1024U)
#define DBK_MAX_MEMBERS 262144U
#define DBK_MAX_FRAGMENTS (DBK_MAX_MEMBERS + XX_MS_DOS_BACKUP2_MAX_VOLUMES)
#define DBK_NONE 0xFFFFFFFFU
/* 63-byte path plus '/' plus 12-byte name, each byte up to 3 UTF-8 bytes. */
#define DBK_MAX_NAME 256U

typedef struct dbk_fragment_s {
    uint32_t offset;
    uint32_t length;
    uint32_t next;   /**< Next fragment of the same member, or DBK_NONE. */
    uint8_t volume;  /**< Index into data[], or prior_data[] when prior. */
    bool prior;
} dbk_fragment;

typedef struct dbk_member_s {
    char *name;      /**< Unique output name, '/'-separated, UTF-8. */
    char *key;       /**< Name before de-duplication, for continuations. */
    bool safe;
    bool broken;     /**< Overruns its size. */
    uint16_t first_number; /**< Fragment number of the first fragment. */
    uint32_t total_size;
    uint64_t stored;
    uint16_t attributes;
    uint16_t dos_time;
    uint16_t dos_date;
    uint16_t last_number;
    int64_t header_offset;
    uint32_t first_fragment;
    uint32_t last_fragment;
    uint32_t fragment_count;
    uint32_t duplicates; /**< Last "__N" suffix handed out for this name. */
} dbk_member;

typedef struct dbk_set_s {
    dbk_member *members;
    size_t count, capacity;
    dbk_fragment *fragments;
    size_t fragment_count, fragment_capacity;
    uint32_t *hash;  /**< member index + 1, 0 = empty */
    size_t hash_capacity;
    uint32_t pending; /**< Member a following fragment may continue. */
    size_t index;
    int64_t format_size;
    uint32_t sequence;
    bool last;
} dbk_set;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

static uint32_t dbk_le16(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U);
}

static uint32_t dbk_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static bool dbk_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    const size_t io_capacity = xx_get_file_buffer_size();
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (request > io_capacity) request = io_capacity;
        amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

static char dbk_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static const uint16_t dbk_cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

/* One CP437 byte as UTF-8.  Control bytes are passed through so the safety
 * check sees them. */
static size_t dbk_put(uint8_t ch, char *out) {
    uint32_t cp;
    if (ch < 0x80U) {
        out[0] = (char)ch;
        return 1U;
    }
    cp = dbk_cp437[ch - 0x80U];
    if (cp < 0x800U) {
        out[0] = (char)(0xC0U | (cp >> 6U));
        out[1] = (char)(0x80U | (cp & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (cp >> 12U));
    out[1] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (cp & 0x3FU));
    return 3U;
}

static bool dbk_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || dbk_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* One path component that is safe to create: not empty, not only dots and
 * spaces (which Windows resolves to "." or ".."), no control or reserved
 * characters, and not a device name with or without an extension. */
static bool dbk_safe_component(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*' || c == '\\')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (dbk_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        (dbk_stem_is(name, 3U, "COM") || dbk_stem_is(name, 3U, "LPT")))
        return false;
    if (stem == 5U && (uint8_t)name[3] == 0xC2U &&
        ((uint8_t)name[4] == 0xB9U || (uint8_t)name[4] == 0xB2U ||
         (uint8_t)name[4] == 0xB3U) &&
        (dbk_stem_is(name, 3U, "COM") || dbk_stem_is(name, 3U, "LPT")))
        return false;
    return true;
}

static bool dbk_safe_path(const char *path) {
    size_t start = 0U, index = 0U;
    if (!path || !path[0]) return false;
    for (;;) {
        if (path[index] == '/' || path[index] == 0) {
            if (!dbk_safe_component(path + start, index - start)) return false;
            if (!path[index]) break;
            start = index + 1U;
        }
        ++index;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* The member set                                                          */

static void dbk_set_free(dbk_set *set) {
    size_t index;
    if (!set) return;
    for (index = 0U; index < set->count; ++index) {
        if (set->members[index].name) xx_str_free(set->members[index].name);
        if (set->members[index].key) xx_str_free(set->members[index].key);
    }
    if (set->members) xx_mem_free(set->members);
    if (set->fragments) xx_mem_free(set->fragments);
    if (set->hash) xx_mem_free(set->hash);
    xx_mem_free(set);
}

static void dbk_set_free_opaque(void *opaque) { dbk_set_free((dbk_set *)opaque); }

static uint32_t dbk_hash_name(const char *name) {
    uint32_t h = 2166136261U;
    for (; *name; ++name) {
        h ^= (uint8_t)dbk_upper(*name);
        h *= 16777619U;
    }
    return h;
}

static bool dbk_names_equal(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b)
        if (dbk_upper(*a) != dbk_upper(*b)) return false;
    return *a == *b;
}

/* The member already named @p name (case-insensitively), or DBK_NONE. */
static uint32_t dbk_hash_find(const dbk_set *set, const char *name) {
    size_t slot;
    if (!set->hash) return DBK_NONE;
    slot = dbk_hash_name(name) & (set->hash_capacity - 1U);
    while (set->hash[slot]) {
        if (dbk_names_equal(set->members[set->hash[slot] - 1U].name, name))
            return set->hash[slot] - 1U;
        slot = (slot + 1U) & (set->hash_capacity - 1U);
    }
    return DBK_NONE;
}

static void dbk_hash_put(dbk_set *set, uint32_t member) {
    size_t slot = dbk_hash_name(set->members[member].name) &
                  (set->hash_capacity - 1U);
    while (set->hash[slot]) slot = (slot + 1U) & (set->hash_capacity - 1U);
    set->hash[slot] = member + 1U;
}

/* Keeps the table at most half full. */
static bool dbk_hash_reserve(dbk_set *set, size_t members) {
    size_t capacity = set->hash_capacity ? set->hash_capacity : 64U, index;
    uint32_t *table;
    while (capacity < members * 2U) capacity *= 2U;
    if (capacity == set->hash_capacity) return true;
    table = (uint32_t *)xx_mem_calloc(capacity, sizeof(uint32_t));
    if (!table) return false;
    if (set->hash) xx_mem_free(set->hash);
    set->hash = table;
    set->hash_capacity = capacity;
    for (index = 0U; index < set->count; ++index)
        dbk_hash_put(set, (uint32_t)index);
    return true;
}

static bool dbk_add_fragment(dbk_set *set, dbk_member *member, uint8_t volume,
                             uint32_t offset, uint32_t length) {
    dbk_fragment *fragment;
    uint32_t index;
    if (set->fragment_count >= DBK_MAX_FRAGMENTS) return false;
    if (set->fragment_count == set->fragment_capacity) {
        size_t capacity = set->fragment_capacity ? set->fragment_capacity * 2U : 32U;
        dbk_fragment *grown;
        if (capacity > DBK_MAX_FRAGMENTS) capacity = DBK_MAX_FRAGMENTS;
        grown = (dbk_fragment *)xx_mem_realloc(set->fragments,
                                               capacity * sizeof(*grown));
        if (!grown) return false;
        set->fragments = grown;
        set->fragment_capacity = capacity;
    }
    index = (uint32_t)set->fragment_count++;
    fragment = &set->fragments[index];
    fragment->offset = offset;
    fragment->length = length;
    fragment->next = DBK_NONE;
    fragment->volume = volume;
    fragment->prior = false;
    if (member->fragment_count == 0U)
        member->first_fragment = index;
    else
        set->fragments[member->last_fragment].next = index;
    member->last_fragment = index;
    ++member->fragment_count;
    member->stored += length;
    if (member->stored > member->total_size) member->broken = true;
    return true;
}

/* Puts a fragment of an earlier volume in front of the member's first. */
static bool dbk_prepend_fragment(dbk_set *set, dbk_member *member,
                                 uint8_t prior_volume, uint32_t offset,
                                 uint32_t length) {
    uint32_t first = member->first_fragment, last = member->last_fragment;
    uint32_t index;
    if (member->fragment_count == 0U || first >= set->fragment_count ||
        last >= set->fragment_count ||
        !dbk_add_fragment(set, member, prior_volume, offset, length))
        return false;
    /* add_fragment linked it behind the old last one; move it to the front. */
    index = member->last_fragment;
    set->fragments[last].next = DBK_NONE;
    member->last_fragment = last;
    set->fragments[index].prior = true;
    set->fragments[index].next = first;
    member->first_fragment = index;
    return true;
}

/* Builds "<dir>/<name>" in UTF-8.  Separators in the directory become '/';
 * a leading separator or drive prefix is dropped from the output but makes
 * the name unsafe, as does any component safe_path refuses. */
static bool dbk_build_name(const uint8_t *dir, const uint8_t *file, char *out,
                           bool *safe) {
    size_t length = 0U, index;
    bool ok = true;
    for (index = 0U; index < DBK_DIR_NAME && dir[index]; ++index) {
        uint8_t c = dir[index];
        if (c == '\\' || c == '/') {
            if (length == 0U || out[length - 1U] == '/') {
                /* An absolute path or an empty component: never produced by
                 * BACKUP itself. */
                ok = false;
                continue;
            }
            out[length++] = '/';
        } else {
            if (c == ':') ok = false;
            length += dbk_put(c, out + length);
        }
    }
    if (length > 0U && out[length - 1U] != '/') out[length++] = '/';
    {
        size_t start = length;
        for (index = 0U; index < DBK_FILE_NAME && file[index]; ++index) {
            uint8_t c = file[index];
            if (c == '/' || c == '\\') c = '_';
            length += dbk_put(c, out + length);
        }
        if (length == start) out[length++] = '_';
    }
    out[length] = 0;
    *safe = ok && dbk_safe_path(out);
    return true;
}

static bool dbk_add_member(dbk_set *set, const char *key, const uint8_t *item,
                           int64_t header_offset, bool safe) {
    dbk_member *member;
    char *name;
    uint32_t suffix, original, attempts = 0U;
    if (set->count >= DBK_MAX_MEMBERS) return false;
    if (set->count == set->capacity) {
        size_t capacity = set->capacity ? set->capacity * 2U : 32U;
        dbk_member *grown;
        if (capacity > DBK_MAX_MEMBERS) capacity = DBK_MAX_MEMBERS;
        grown = (dbk_member *)xx_mem_realloc(set->members,
                                             capacity * sizeof(*grown));
        if (!grown) return false;
        set->members = grown;
        set->capacity = capacity;
    }
    if (!dbk_hash_reserve(set, set->count + 1U)) return false;
    name = xx_str_dup(key);
    if (!name) return false;
    /* Two items of one set never share a name, but a hostile catalogue can
     * repeat one: every record keeps a distinct output name.  The member that
     * owns the plain name remembers the last suffix used, so a name repeated
     * N times costs N probes, not N squared. */
    original = dbk_hash_find(set, name);
    if (original != DBK_NONE) {
        suffix = set->members[original].duplicates < 1U
                     ? 1U : set->members[original].duplicates;
        for (;;) {
            char tail[16];
            char *replacement;
            if (++attempts > DBK_MAX_MEMBERS + 1U) {
                xx_str_free(name);
                return false;
            }
            xx_rt_snprintf(tail, sizeof(tail), "__%u", (unsigned)++suffix);
            replacement = xx_str_concat(key, tail);
            xx_str_free(name);
            if (!replacement) return false;
            name = replacement;
            if (dbk_hash_find(set, name) == DBK_NONE) break;
        }
        set->members[original].duplicates = suffix;
    }
    member = &set->members[set->count];
    xx_mem_zero(member, sizeof(*member));
    member->name = name;
    member->key = xx_str_dup(key);
    if (!member->key) {
        xx_str_free(name);
        member->name = NULL;
        return false;
    }
    member->safe = safe;
    member->total_size = dbk_le32(item + 0x0E);
    member->attributes = (uint16_t)dbk_le16(item + 0x1C);
    member->dos_time = (uint16_t)dbk_le16(item + 0x1E);
    member->dos_date = (uint16_t)dbk_le16(item + 0x20);
    member->header_offset = header_offset;
    member->first_fragment = DBK_NONE;
    member->last_fragment = DBK_NONE;
    dbk_hash_put(set, (uint32_t)set->count);
    ++set->count;
    return true;
}

static bool dbk_header_ok(const uint8_t *header) {
    return header[0] == DBK_HEADER_SIZE &&
           xx_rt_memcmp(header + 1, "BACKUP  ", 8U) == 0 && header[9] != 0U;
}

/* Parses one CONTROL file.  Returns false only when the file is not a
 * CONTROL file at all (bad header or no complete first directory item) or
 * memory runs out; a catalogue cut short keeps its complete items. */
static bool dbk_parse_volume(dbk_set *set, xx_io_device *device,
                             int64_t base, uint8_t volume, int64_t *end) {
    int64_t total;
    size_t size, position;
    uint8_t *buffer;
    bool result = false, first = true;
    char key[DBK_MAX_NAME];
    if (!device || base < 0) return false;
    total = xx_io_total_size(device);
    if (total < base || total - base < (int64_t)(DBK_HEADER_SIZE + DBK_DIR_ITEM))
        return false;
    size = (total - base) > (int64_t)DBK_MAX_CONTROL ? DBK_MAX_CONTROL
                                                     : (size_t)(total - base);
    {
        uint8_t header[DBK_HEADER_SIZE + 1U];
        if (!dbk_read_at(device, base, header, sizeof(header)) ||
            !dbk_header_ok(header) || header[DBK_HEADER_SIZE] != DBK_DIR_ITEM)
            return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(size);
    if (!buffer) return false;
    if (!dbk_read_at(device, base, buffer, size)) goto done;
    if (volume == 0U) set->sequence = buffer[9];
    set->last = buffer[DBK_LAST_OFFSET] != 0U;
    position = DBK_HEADER_SIZE;
    *end = (int64_t)position;
    for (;;) {
        const uint8_t *dir;
        uint32_t entries, next, index;
        if (size - position < DBK_DIR_ITEM || buffer[position] != DBK_DIR_ITEM)
            break;
        dir = buffer + position;
        entries = dbk_le16(dir + 0x40);
        next = dbk_le32(dir + 0x42);
        position += DBK_DIR_ITEM;
        *end = (int64_t)position;
        first = false;
        for (index = 0U; index < entries; ++index) {
            const uint8_t *item;
            uint32_t number;
            bool safe;
            if (size - position < DBK_FILE_ITEM ||
                buffer[position] != DBK_FILE_ITEM)
                goto finished;
            item = buffer + position;
            dbk_build_name(dir + 1, item + 1, key, &safe);
            number = dbk_le16(item + 0x12);
            {
                dbk_member *pending = set->pending != DBK_NONE
                                          ? &set->members[set->pending] : NULL;
                if (number >= 2U && pending &&
                    number == (uint32_t)pending->last_number + 1U &&
                    pending->stored < pending->total_size &&
                    pending->total_size == dbk_le32(item + 0x0E) &&
                    dbk_names_equal(pending->key, key)) {
                    /* The continuation of the file split at the end of the
                     * previous volume. */
                } else {
                    if (!dbk_add_member(set, key, item,
                                        base + (int64_t)position, safe))
                        goto finished;
                    pending = &set->members[set->count - 1U];
                    set->pending = (uint32_t)(set->count - 1U);
                    /* Anything but 1 starts mid-file: incomplete unless an
                     * earlier volume supplies the fragments before it. */
                    pending->first_number = (uint16_t)number;
                }
                pending->last_number = (uint16_t)number;
                if (!dbk_add_fragment(set, pending, volume, dbk_le32(item + 0x14),
                                      dbk_le32(item + 0x18)))
                    goto finished;
            }
            position += DBK_FILE_ITEM;
            *end = (int64_t)position;
        }
        if (next == DBK_NO_NEXT) break;
    }
finished:
    result = !first;
done:
    xx_mem_free(buffer);
    return result;
}

/* The fragment numbered @p number of a member continued on the volume after
 * @p control: it must be the last file item of that earlier CONTROL file,
 * with the member's name and size, and the volume must be numbered
 * @p sequence and not be marked last. */
static bool dbk_prior_fragment(xx_io_device *control, uint32_t sequence,
                               const char *key, uint32_t total_size,
                               uint32_t number, uint32_t *offset,
                               uint32_t *length) {
    uint8_t header[DBK_HEADER_SIZE];
    dbk_set *earlier;
    int64_t end = 0;
    bool ok = false;
    if (!control || !key || sequence == 0U || sequence > 255U || number == 0U ||
        !dbk_read_at(control, 0, header, sizeof(header)) ||
        !dbk_header_ok(header) || header[9] != sequence ||
        header[DBK_LAST_OFFSET] != 0U)
        return false;
    earlier = (dbk_set *)xx_mem_calloc(1U, sizeof(*earlier));
    if (!earlier) return false;
    earlier->pending = DBK_NONE;
    if (dbk_parse_volume(earlier, control, 0, 0U, &end) && earlier->count > 0U) {
        const dbk_member *last = &earlier->members[earlier->count - 1U];
        if (!last->broken && last->fragment_count == 1U &&
            last->first_number == number && last->last_number == number &&
            last->total_size == total_size && last->stored < total_size &&
            dbk_names_equal(last->key, key)) {
            const dbk_fragment *fragment =
                &earlier->fragments[last->first_fragment];
            *offset = fragment->offset;
            *length = fragment->length;
            ok = true;
        }
    }
    dbk_set_free(earlier);
    return ok;
}

/* Completes the first member of the first CONTROL file, when it continues a
 * file from the volume before, from the attached earlier volumes. */
static void dbk_backfill(dbk_set *set, const xx_ms_dos_backup2 *archive) {
    dbk_member *member;
    uint32_t prior, number;
    if (set->count == 0U || archive->prior_count == 0U) return;
    member = &set->members[0];
    if (member->fragment_count == 0U ||
        member->first_fragment >= set->fragment_count ||
        set->fragments[member->first_fragment].volume != 0U ||
        set->fragments[member->first_fragment].prior)
        return;
    number = member->first_number;
    for (prior = 0U; prior < archive->prior_count && number > 1U; ++prior) {
        uint32_t offset = 0U, length = 0U;
        if (set->sequence < prior + 2U ||
            !dbk_prior_fragment(archive->prior_control[prior],
                                set->sequence - 1U - prior, member->key,
                                member->total_size, number - 1U, &offset,
                                &length) ||
            !dbk_prepend_fragment(set, member, (uint8_t)prior, offset, length))
            return;
        --number;
        member->first_number = (uint16_t)number;
    }
}

static dbk_set *dbk_load(xx_ms_dos_backup2 *archive) {
    dbk_set *set;
    uint32_t volume;
    int64_t end = 0;
    if (!archive || !archive->format.device) return NULL;
    /* Nothing attached: look beside the CONTROL file once, when the device
     * is that file itself. */
    if (!archive->companions_tried) {
        archive->companions_tried = true;
        if (archive->format.base_address == 0 && archive->volume_count == 1U &&
            !archive->data[0] && archive->prior_count == 0U) {
            const char *path = xx_io_source_path(archive->format.device);
            if (path && path[0])
                (void)xx_ms_dos_backup2_open_volume_files(archive, path);
        }
    }
    set = (dbk_set *)xx_mem_calloc(1U, sizeof(*set));
    if (!set) return NULL;
    set->pending = DBK_NONE;
    if (!dbk_parse_volume(set, archive->format.device,
                          archive->format.base_address, 0U, &end)) {
        dbk_set_free(set);
        return NULL;
    }
    set->format_size = end;
    for (volume = 1U; volume < archive->volume_count; ++volume) {
        uint8_t header[DBK_HEADER_SIZE];
        int64_t ignored = 0;
        uint32_t sequence = set->sequence + volume;
        /* Volumes must follow one another; anything after the one marked
         * last, or out of order, is not part of this set. */
        if (set->last || sequence > 255U || !archive->control[volume] ||
            !dbk_read_at(archive->control[volume], 0, header, sizeof(header)) ||
            !dbk_header_ok(header) || header[9] != sequence)
            break;
        if (!dbk_parse_volume(set, archive->control[volume], 0, (uint8_t)volume,
                              &ignored))
            break;
    }
    dbk_backfill(set, archive);
    return set;
}

/* ---------------------------------------------------------------------- */
/* Records and extraction                                                  */

static bool dbk_complete(const dbk_member *member) {
    return !member->broken && member->first_number == 1U &&
           member->fragment_count > 0U && member->stored == member->total_size;
}

static bool dbk_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *dbk_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool dbk_set_record(xx_archive_record *record, const dbk_set *set,
                           const dbk_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = DBK_FILE_ITEM;
    record->data_offset = member->fragment_count
                              ? (int64_t)set->fragments[member->first_fragment].offset
                              : -1;
    record->compressed_size = (int64_t)member->stored;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->stored) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->total_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Every fragment must lie inside its volume's BACKUP file; with a NULL
 * destination this only verifies. */
static bool dbk_copy_member(xx_ms_dos_backup2 *archive, const dbk_set *set,
                            const dbk_member *member, xx_io_device *destination,
                            xx_pd_struct *pd) {
    uint32_t index, steps = 0U;
    if (!dbk_complete(member)) return false;
    for (index = member->first_fragment; index != DBK_NONE;
         index = set->fragments[index].next) {
        const dbk_fragment *fragment;
        xx_io_device *data;
        int64_t size;
        if (index >= set->fragment_count || ++steps > member->fragment_count)
            return false;
        fragment = &set->fragments[index];
        if (fragment->prior) {
            if (fragment->volume >= archive->prior_count ||
                !(data = archive->prior_data[fragment->volume]))
                return false;
        } else if (fragment->volume >= archive->volume_count ||
                   !(data = archive->data[fragment->volume])) {
            return false;
        }
        size = xx_io_total_size(data);
        if (size < 0 ||
            (int64_t)fragment->offset + (int64_t)fragment->length > size)
            return false;
        if (destination && fragment->length > 0U &&
            !xx_store_unpack_device(data, (int64_t)fragment->offset,
                                    (int64_t)fragment->length, destination, pd))
            return false;
        if (pd && xx_pd_is_stopped(pd)) return false;
    }
    return steps == member->fragment_count;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_ms_dos_backup2_init(xx_ms_dos_backup2 *archive, xx_io_device *device,
                            int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MS_DOS_BACKUP2_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdos-backup");
    xx_format_set_extension(&archive->format, "001");
    archive->format.check_is_valid = xx_ms_dos_backup2_check_is_valid;
    archive->format.handle_base_info = xx_ms_dos_backup2_handle_base_info;
    archive->format.get_format_size = xx_ms_dos_backup2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ms_dos_backup2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ms_dos_backup2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ms_dos_backup2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ms_dos_backup2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ms_dos_backup2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ms_dos_backup2_free_archive_records_reading;
    archive->volume_count = 1U;
    archive->control[0] = device;
}

xx_ms_dos_backup2 *xx_ms_dos_backup2_create(xx_io_device *device,
                                            int64_t base_address) {
    xx_ms_dos_backup2 *archive =
        (xx_ms_dos_backup2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ms_dos_backup2_init(archive, device, base_address);
    return archive;
}

void xx_ms_dos_backup2_destroy(xx_ms_dos_backup2 *archive) {
    uint32_t index;
    if (!archive) return;
    for (index = 0U; index < XX_MS_DOS_BACKUP2_MAX_VOLUMES; ++index) {
        if (archive->control_owned[index] && archive->control[index])
            xx_io_close(archive->control[index]);
        if (archive->data_owned[index] && archive->data[index])
            xx_io_close(archive->data[index]);
        archive->control_owned[index] = archive->data_owned[index] = false;
        archive->data[index] = NULL;
        if (index) archive->control[index] = NULL;
        if (archive->prior_control_owned[index] && archive->prior_control[index])
            xx_io_close(archive->prior_control[index]);
        if (archive->prior_data_owned[index] && archive->prior_data[index])
            xx_io_close(archive->prior_data[index]);
        archive->prior_control_owned[index] = archive->prior_data_owned[index] =
            false;
        archive->prior_control[index] = archive->prior_data[index] = NULL;
    }
    archive->volume_count = 1U;
    archive->prior_count = 0U;
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ms_dos_backup2_free(xx_ms_dos_backup2 *archive) {
    if (!archive) return;
    xx_ms_dos_backup2_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ms_dos_backup2_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    int64_t end = 0;
    dbk_set *set;
    bool result;
    (void)pd;
    if (!format || !format->device) return false;
    set = (dbk_set *)xx_mem_calloc(1U, sizeof(*set));
    if (!set) return false;
    set->pending = DBK_NONE;
    result = dbk_parse_volume(set, format->device, format->base_address, 0U, &end);
    dbk_set_free(set);
    return result;
}

bool xx_ms_dos_backup2_handle_base_info(Abstractformat *format,
                                        xx_pd_struct *pd) {
    xx_ms_dos_backup2 *archive = (xx_ms_dos_backup2 *)format;
    dbk_set *set;
    (void)pd;
    if (!format || !(set = dbk_load(archive))) return false;
    archive->number_of_records = set->count;
    archive->sequence = set->sequence;
    archive->last_volume = set->last;
    format->number_of_archive_records = set->count;
    format->format_size = set->format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    dbk_set_free(set);
    return true;
}

int64_t xx_ms_dos_backup2_get_format_size(Abstractformat *format,
                                          xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ms_dos_backup2_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_ms_dos_backup2_get_number_of_archive_records(Abstractformat *format,
                                                         xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ms_dos_backup2_handle_base_info(format, pd))
               ? ((xx_ms_dos_backup2 *)format)->number_of_records : 0U;
}

bool xx_ms_dos_backup2_set_data_device(xx_ms_dos_backup2 *archive,
                                       xx_io_device *data) {
    if (!archive) return false;
    if (archive->data_owned[0] && archive->data[0]) xx_io_close(archive->data[0]);
    archive->data[0] = data;
    archive->data_owned[0] = false;
    return true;
}

bool xx_ms_dos_backup2_add_volume(xx_ms_dos_backup2 *archive,
                                  xx_io_device *control, xx_io_device *data) {
    if (!archive || !control ||
        archive->volume_count >= XX_MS_DOS_BACKUP2_MAX_VOLUMES)
        return false;
    archive->control[archive->volume_count] = control;
    archive->data[archive->volume_count] = data;
    archive->control_owned[archive->volume_count] = false;
    archive->data_owned[archive->volume_count] = false;
    ++archive->volume_count;
    archive->format.base_info_handled = false;
    return true;
}

bool xx_ms_dos_backup2_add_prior_volume(xx_ms_dos_backup2 *archive,
                                        xx_io_device *control,
                                        xx_io_device *data) {
    if (!archive || !control ||
        archive->prior_count >= XX_MS_DOS_BACKUP2_MAX_VOLUMES)
        return false;
    archive->prior_control[archive->prior_count] = control;
    archive->prior_data[archive->prior_count] = data;
    archive->prior_control_owned[archive->prior_count] = false;
    archive->prior_data_owned[archive->prior_count] = false;
    ++archive->prior_count;
    archive->format.base_info_handled = false;
    return true;
}

/* <directory><stem>.<ext> with ext printed from a volume number. */
static char *dbk_sibling(const char *directory, size_t directory_length,
                         const char *stem, uint32_t number) {
    char tail[24];
    size_t stem_length = xx_str_len(stem), tail_length;
    char *path;
    xx_rt_snprintf(tail, sizeof(tail), ".%03u", (unsigned)number);
    tail_length = xx_str_len(tail);
    path = (char *)xx_mem_alloc(directory_length + stem_length + tail_length + 1U);
    if (!path) return NULL;
    xx_mem_copy(path, directory, directory_length);
    xx_mem_copy(path + directory_length, stem, stem_length);
    xx_mem_copy(path + directory_length + stem_length, tail, tail_length + 1U);
    return path;
}

uint32_t xx_ms_dos_backup2_open_volume_files(xx_ms_dos_backup2 *archive,
                                             const char *control_path) {
    size_t directory = 0U, index;
    const char *base;
    bool lower;
    uint32_t attached = 0U, sequence;
    uint8_t header[DBK_HEADER_SIZE];
    if (!archive || !control_path || !archive->format.device) return 0U;
    if (!dbk_read_at(archive->format.device, archive->format.base_address,
                     header, sizeof(header)) ||
        !dbk_header_ok(header))
        return 0U;
    for (index = 0U; control_path[index]; ++index)
        if (control_path[index] == '/' || control_path[index] == '\\')
            directory = index + 1U;
    base = control_path + directory;
    /* The companions are named after the volume number in the header, in the
     * case the CONTROL file's own name uses. */
    lower = base[0] == 'c';
    sequence = header[9];
    if (!archive->data[0]) {
        char *path = dbk_sibling(control_path, directory,
                                 lower ? "backup" : "BACKUP", sequence);
        if (path) {
            archive->data[0] = xx_io_file_open(path, "rb");
            archive->data_owned[0] = archive->data[0] != NULL;
            xx_mem_free(path);
        }
    }
    if (archive->data[0]) ++attached;
    for (index = 1U; index < archive->volume_count; ++index)
        if (archive->data[index]) ++attached;
    /* Follow the chain from the last attached volume while it is not the
     * one marked last. */
    while (archive->volume_count < XX_MS_DOS_BACKUP2_MAX_VOLUMES) {
        xx_io_device *last = archive->control[archive->volume_count - 1U];
        int64_t last_base = archive->volume_count == 1U
                                ? archive->format.base_address : 0;
        uint32_t number;
        char *path;
        xx_io_device *control, *data;
        if (!dbk_read_at(last, last_base, header, sizeof(header)) ||
            !dbk_header_ok(header) || header[DBK_LAST_OFFSET] != 0U)
            break;
        number = (uint32_t)header[9] + 1U;
        if (number > 255U) break;
        path = dbk_sibling(control_path, directory,
                           lower ? "control" : "CONTROL", number);
        if (!path) break;
        control = xx_io_file_open(path, "rb");
        xx_mem_free(path);
        if (!control) break;
        if (!dbk_read_at(control, 0, header, sizeof(header)) ||
            !dbk_header_ok(header) || header[9] != number) {
            xx_io_close(control);
            break;
        }
        path = dbk_sibling(control_path, directory,
                           lower ? "backup" : "BACKUP", number);
        data = path ? xx_io_file_open(path, "rb") : NULL;
        if (path) xx_mem_free(path);
        archive->control[archive->volume_count] = control;
        archive->control_owned[archive->volume_count] = true;
        archive->data[archive->volume_count] = data;
        archive->data_owned[archive->volume_count] = data != NULL;
        ++archive->volume_count;
        if (data) ++attached;
    }
    /* The first CONTROL file may open with the continuation of a file split
     * at the end of the volume before it: walk back while each earlier
     * volume holds the fragment in front. */
    if (archive->prior_count == 0U && sequence > 1U) {
        dbk_set *set = (dbk_set *)xx_mem_calloc(1U, sizeof(*set));
        int64_t end = 0;
        if (set) set->pending = DBK_NONE;
        if (set &&
            dbk_parse_volume(set, archive->format.device,
                             archive->format.base_address, 0U, &end) &&
            set->count > 0U && set->members[0].fragment_count > 0U) {
            const dbk_member *member = &set->members[0];
            uint32_t number = member->first_number;
            while (number > 1U && archive->prior_count + 1U < sequence &&
                   archive->prior_count < XX_MS_DOS_BACKUP2_MAX_VOLUMES) {
                uint32_t earlier = sequence - 1U - archive->prior_count;
                uint32_t offset = 0U, length = 0U;
                char *path;
                xx_io_device *control, *data;
                path = dbk_sibling(control_path, directory,
                                   lower ? "control" : "CONTROL", earlier);
                if (!path) break;
                control = xx_io_file_open(path, "rb");
                xx_mem_free(path);
                if (!control) break;
                if (!dbk_prior_fragment(control, earlier, member->key,
                                        member->total_size, number - 1U,
                                        &offset, &length)) {
                    xx_io_close(control);
                    break;
                }
                path = dbk_sibling(control_path, directory,
                                   lower ? "backup" : "BACKUP", earlier);
                data = path ? xx_io_file_open(path, "rb") : NULL;
                if (path) xx_mem_free(path);
                archive->prior_control[archive->prior_count] = control;
                archive->prior_control_owned[archive->prior_count] = true;
                archive->prior_data[archive->prior_count] = data;
                archive->prior_data_owned[archive->prior_count] = data != NULL;
                ++archive->prior_count;
                --number;
            }
        }
        if (set) dbk_set_free(set);
    }
    archive->format.base_info_handled = false;
    return attached;
}

xx_archive_record_state *xx_ms_dos_backup2_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    dbk_set *set;
    xx_archive_record_state *state;
    (void)pd;
    if (!format || !(set = dbk_load((xx_ms_dos_backup2 *)format))) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        dbk_set_free(set);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = set;
    state->free_internal = dbk_set_free_opaque;
    state->total_records = (int64_t)set->count;
    if (!dbk_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (set->count == 0U) {
        state->has_record = false;
        return state;
    }
    if (!dbk_set_record(&state->current_record, set, &set->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ms_dos_backup2_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_ms_dos_backup2_archive_record_move_to_next(Abstractformat *format,
                                                   xx_archive_record_state *state,
                                                   xx_pd_struct *pd) {
    dbk_set *set;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(set = (dbk_set *)state->internal_state) || set->index + 1U >= set->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++set->index;
    ++state->current_index;
    state->has_record =
        dbk_set_record(&state->current_record, set, &set->members[set->index]);
    return state->has_record;
}

bool xx_ms_dos_backup2_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_ms_dos_backup2 *archive = (xx_ms_dos_backup2 *)format;
    dbk_set *set;
    const dbk_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(set = (dbk_set *)state->internal_state) || set->index >= set->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &set->members[set->index];
    path_option = dbk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return dbk_copy_member(archive, set, member, NULL, pd);
    if (!member->safe || !dbk_complete(member)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* Refuse before creating anything when the data is not all there. */
    if (!dbk_copy_member(archive, set, member, NULL, pd)) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        /* Only a file this call created may be removed on failure. */
        created = true;
        result = dbk_copy_member(archive, set, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ms_dos_backup2_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
