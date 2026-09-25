/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Godot Engine resource pack (PCK) reader.  xx_godot_engine_pck.h carries
 * the field tables.
 *
 * Written from the structure of the format.  XArchive's games/xgodotpck.cpp
 * (MIT, same author) was a cross-check for the version 1 and 2 headers; no
 * code was taken from it or from godotdec.
 *
 * What makes a pack valid here, so that four magic bytes alone never do:
 * a known format version (0..4) with a plausible engine version, a zero
 * reserved block, only the pack flags defined for that version, and a
 * directory whose every entry fits the pack: a path of 1..4096 bytes that
 * is valid UTF-8 without control characters, only the member flags defined
 * for that version, and a member that lies inside the pack behind the
 * directory (versions 0..2) or between the file base and the directory
 * (versions 3 and 4).  The walk reads the directory sequentially through
 * one 64 KiB window and allocates nothing else; the entry count is capped
 * by what the pack can physically hold.
 *
 * Member offsets of versions 0 and 1 (and of version 2 without the
 * relative-file-base flag) count from the start of the file that holds the
 * pack.  For a pack found through the executable trailer or the PE section
 * that is the reader's base address; for a pack at the base address it is
 * the base itself, and only when that does not fit, the start of the device
 * (a pack carved out of the executable it was exported into).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/godot_engine_pck/xx_godot_engine_pck.h"

#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef GODOT_ENGINE_PCK
#define XX_GODOT_ENGINE_PCK_FILE_TYPE XX_FILE_TYPE_GODOT_ENGINE_PCK
#else
#define XX_GODOT_ENGINE_PCK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define GDPCK_MAGIC "GDPC"
/* Offset of the u32 file count for versions 0/1 and 2; versions 3 and 4 keep
 * their reserved block up to 0x68 and store the directory elsewhere. */
#define GDPCK_V1_COUNT 0x54
#define GDPCK_V2_COUNT 0x60
#define GDPCK_V3_HEADER 0x68
#define GDPCK_MIN_PACK (GDPCK_V1_COUNT + 4)
#define GDPCK_TRAILER 12
/* md5[16] + u64 plain size + iv[16] in front of AES-CFB ciphertext. */
#define GDPCK_CRYPT_HEADER 40
/* Smallest entry: u32 length, one path byte and the fixed tail. */
#define GDPCK_MIN_ENTRY_V1 (4 + 1 + 32)
#define GDPCK_MIN_ENTRY_V2 (4 + 1 + 36)
#define GDPCK_WINDOW 65536U
/* Flags each pack version may carry.  The sparse-bundle and delta flags came
 * with the later Godot 4 packs; this reader takes them from version 3 on. */
#define GDPCK_PACK_FLAGS_V2 \
    (XX_GODOT_ENGINE_PCK_FLAG_DIR_ENCRYPTED | \
     XX_GODOT_ENGINE_PCK_FLAG_REL_FILEBASE)
#define GDPCK_PACK_FLAGS_V3 \
    (GDPCK_PACK_FLAGS_V2 | XX_GODOT_ENGINE_PCK_FLAG_SPARSE_BUNDLE)
#define GDPCK_FILE_FLAGS_V2 \
    (XX_GODOT_ENGINE_PCK_FILE_ENCRYPTED | XX_GODOT_ENGINE_PCK_FILE_REMOVAL)
#define GDPCK_FILE_FLAGS_V3 \
    (GDPCK_FILE_FLAGS_V2 | XX_GODOT_ENGINE_PCK_FILE_DELTA)
/* Version 4 keeps a 32-byte salt at +0x28 when the directory is encrypted
 * and the pack is a sparse bundle; the rest of the reserved block is zero. */
#define GDPCK_V4_SALT_END 0x48U
#define GDPCK_PE_MAX_SECTIONS 96U
#define GDPCK_MAX_SUFFIX_TRIES 10000U

typedef struct gdpck_layout_s {
    int64_t base;
    int64_t total;
    int64_t pack;        /* absolute offset of "GDPC" */
    int64_t region_end;  /* the pack's bytes end here (trailer excluded) */
    xx_godot_engine_pck_placement_t placement;
    uint32_t version;
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    uint32_t flags;
    uint64_t stored_file_base;
    int64_t dir_start;   /* absolute offset of the u32 file count */
    uint32_t file_count;
    bool dir_encrypted;
    /* Resolved by the walk. */
    int64_t file_base;   /* absolute origin of member offsets */
    int64_t dir_end;
    int64_t data_end;
    int64_t format_end;
    uint64_t listed;
    bool any_encrypted;
} gdpck_layout;

typedef struct gdpck_input_s {
    xx_io_device *device;
    uint8_t *window;
    int64_t window_offset;
    size_t window_size;
    int64_t position;
    int64_t limit;
} gdpck_input;

typedef struct gdpck_entry_s {
    int64_t header_offset;
    int64_t header_size;
    size_t name_length;  /* bytes in front of the first NUL */
    uint64_t size;
    uint8_t md5[16];
    uint32_t flags;
    bool removal;        /* a patch entry that deletes a path: not listed */
    bool external;       /* sparse bundle: the data is a file of its own */
    int64_t data_offset; /* absolute; -1 when the pack holds no data */
    int64_t stored_size;
} gdpck_entry;

/* Output paths handed out in one session, as 64-bit hashes of their
 * case-folded form, plus the next numeric suffix to try for a stem. */
typedef struct gdpck_set_s {
    uint64_t *keys;
    uint32_t *values;
    size_t mask;
    size_t used;
} gdpck_set;

typedef struct gdpck_stream_s {
    gdpck_layout layout;
    gdpck_input input;
    uint8_t *memory;       /* window, then the path buffer */
    uint8_t *path;
    uint32_t consumed;     /* directory entries read so far */
    uint64_t index;        /* listed member index of the current record */
    gdpck_entry entry;
    char *output_name;     /* safe unique relative path, NULL if refused */
    gdpck_set taken;
    gdpck_set hints;
} gdpck_stream;

/* ---------------------------------------------------------------- bytes -- */

static uint32_t gdpck_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t gdpck_le64(const uint8_t *bytes) {
    return (uint64_t)gdpck_le32(bytes) |
           ((uint64_t)gdpck_le32(bytes + 4) << 32);
}

static uint16_t gdpck_le16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static bool gdpck_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool gdpck_is_magic(xx_io_device *device, int64_t offset) {
    uint8_t magic[4];
    return gdpck_read_at(device, offset, magic, sizeof(magic)) &&
           xx_rt_memcmp(magic, GDPCK_MAGIC, 4U) == 0;
}

/* Sequential reads that never pass @c limit. */
static bool gdpck_input_read(gdpck_input *in, void *out, size_t size) {
    uint8_t *target = (uint8_t *)out;
    if (in->position < 0 || in->position > in->limit ||
        (uint64_t)size > (uint64_t)(in->limit - in->position))
        return false;
    while (size != 0U) {
        if (in->window_size != 0U && in->position >= in->window_offset &&
            in->position - in->window_offset < (int64_t)in->window_size) {
            size_t at = (size_t)(in->position - in->window_offset);
            size_t take = in->window_size - at;
            if (take > size) take = size;
            xx_rt_memcpy(target, in->window + at, take);
            target += take;
            size -= take;
            in->position += (int64_t)take;
        } else {
            int64_t want = in->limit - in->position;
            if (want > (int64_t)GDPCK_WINDOW) want = (int64_t)GDPCK_WINDOW;
            in->window_size = 0U;
            if (want <= 0 ||
                !gdpck_read_at(in->device, in->position, in->window,
                               (size_t)want))
                return false;
            in->window_offset = in->position;
            in->window_size = (size_t)want;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- paths -- */

/* Strict UTF-8 without C0/C1 controls or DEL: the paths Godot writes come
 * from String::utf8(), so anything else is not a directory entry. */
static bool gdpck_path_text_ok(const uint8_t *text, size_t length) {
    size_t index = 0U;
    while (index < length) {
        uint32_t c = text[index];
        size_t extra, k;
        uint32_t minimum;
        if (c < 0x80U) {
            if (c < 0x20U || c == 0x7FU) return false;
            ++index;
            continue;
        }
        if ((c & 0xE0U) == 0xC0U) {
            extra = 1U;
            c &= 0x1FU;
            minimum = 0x80U;
        } else if ((c & 0xF0U) == 0xE0U) {
            extra = 2U;
            c &= 0x0FU;
            minimum = 0x800U;
        } else if ((c & 0xF8U) == 0xF0U) {
            extra = 3U;
            c &= 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (extra > length - index - 1U) return false;
        for (k = 1U; k <= extra; ++k) {
            uint8_t next = text[index + k];
            if ((next & 0xC0U) != 0x80U) return false;
            c = (c << 6) | (next & 0x3FU);
        }
        if (c < minimum || c > 0x10FFFFU || (c >= 0xD800U && c <= 0xDFFFU) ||
            (c >= 0x80U && c <= 0x9FU))
            return false;
        index += extra + 1U;
    }
    return true;
}

static char gdpck_upper_ascii(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$ and CLOCK$, with or
 * without an extension, in any case. */
static bool gdpck_is_device(const char *name, size_t length) {
    static const char *const words[] = {"CON", "PRN", "AUX", "NUL",
                                        "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, word, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (word = 0U; word < sizeof(words) / sizeof(words[0]); ++word) {
        const char *text = words[word];
        for (index = 0U; index < stem && text[index]; ++index)
            if (gdpck_upper_ascii(name[index]) != text[index]) break;
        if (index == stem && text[index] == '\0') return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        char a = gdpck_upper_ascii(name[0]);
        char b = gdpck_upper_ascii(name[1]);
        char c = gdpck_upper_ascii(name[2]);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T'))
            return true;
    }
    return false;
}

/* A relative path whose every component is a plain name: no root, no
 * drive or stream colon, no empty, "." or ".." component, no component
 * that Windows would trim (trailing dot or space) or resolve to a device,
 * and none of the characters Windows refuses.  Backslashes were already
 * turned into slashes. */
static bool gdpck_path_safe(const char *path) {
    const char *cursor = path;
    if (!path || !path[0] || path[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') {
            char c = *end;
            if ((unsigned char)c < 0x20U || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                return false;
            ++end;
        }
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (cursor[length - 1U] == '.' || cursor[length - 1U] == ' ')
            return false;
        if (gdpck_is_device(cursor, length)) return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* The next code point of a (validated) UTF-8 string, case-folded at least
 * as far as a case-insensitive file system folds it for the scripts game
 * data uses.  Folding more only costs a suffix; folding less could let two
 * members share one file. */
static uint32_t gdpck_fold_next(const char **cursor) {
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t c = s[0];
    size_t used = 1U;
    if (c == 0U) return 0U;
    if ((c & 0xE0U) == 0xC0U && s[1]) {
        c = ((c & 0x1FU) << 6) | (s[1] & 0x3FU);
        used = 2U;
    } else if ((c & 0xF0U) == 0xE0U && s[1] && s[2]) {
        c = ((c & 0x0FU) << 12) | ((uint32_t)(s[1] & 0x3FU) << 6) |
            (s[2] & 0x3FU);
        used = 3U;
    } else if ((c & 0xF8U) == 0xF0U && s[1] && s[2] && s[3]) {
        c = ((c & 0x07U) << 18) | ((uint32_t)(s[1] & 0x3FU) << 12) |
            ((uint32_t)(s[2] & 0x3FU) << 6) | (s[3] & 0x3FU);
        used = 4U;
    }
    *cursor += used;
    if (c >= 'a' && c <= 'z') return c - 0x20U;
    if (c < 0x80U) return c;
    if (c >= 0xE0U && c <= 0xFEU && c != 0xF7U) return c - 0x20U;
    if (c == 0xFFU) return 0x178U;
    if (c == 0x131U) return 'I';
    if (c == 0x17FU) return 'S';
    if ((c >= 0x100U && c <= 0x137U) || (c >= 0x14AU && c <= 0x177U) ||
        (c >= 0x1E00U && c <= 0x1EFFU) || (c >= 0x460U && c <= 0x481U) ||
        (c >= 0x48AU && c <= 0x4BFU) || (c >= 0x4D0U && c <= 0x52FU))
        return c & ~1U;
    if ((c >= 0x139U && c <= 0x148U) || (c >= 0x179U && c <= 0x17EU) ||
        (c >= 0x4C1U && c <= 0x4CEU))
        return (c & 1U) ? c : c - 1U;
    if (c >= 0x180U && c <= 0x24FU) return c & ~1U;
    if (c >= 0x3B1U && c <= 0x3CBU && c != 0x3C2U) return c - 0x20U;
    if (c == 0x3C2U) return 0x3A3U;
    if (c == 0x3ACU) return 0x386U;
    if (c >= 0x3ADU && c <= 0x3AFU) return c - 0x25U;
    if (c == 0x3CCU) return 0x38CU;
    if (c == 0x3CDU || c == 0x3CEU) return c - 0x3FU;
    if (c >= 0x430U && c <= 0x44FU) return c - 0x20U;
    if (c >= 0x450U && c <= 0x45FU) return c - 0x50U;
    if (c >= 0x561U && c <= 0x586U) return c - 0x30U;
    if (c >= 0xFF41U && c <= 0xFF5AU) return c - 0x20U;
    return c;
}

static uint64_t gdpck_hash(const char *text) {
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t c;
    while ((c = gdpck_fold_next(&text)) != 0U) {
        hash ^= (uint64_t)c;
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1U;
}

static bool gdpck_set_init(gdpck_set *set, uint64_t expected) {
    size_t size = 16U;
    xx_mem_zero(set, sizeof(*set));
    while ((uint64_t)size < expected * 2U + 2U && size < ((size_t)1 << 22))
        size *= 2U;
    set->keys = (uint64_t *)xx_mem_calloc(size, sizeof(*set->keys));
    set->values = (uint32_t *)xx_mem_calloc(size, sizeof(*set->values));
    set->mask = size - 1U;
    return set->keys && set->values;
}

static void gdpck_set_cleanup(gdpck_set *set) {
    if (set->keys) xx_mem_free(set->keys);
    if (set->values) xx_mem_free(set->values);
    xx_mem_zero(set, sizeof(*set));
}

/* The slot holding @p key, or the empty slot where it belongs.  The table
 * never gets more than half full, so the probe always ends. */
static size_t gdpck_set_slot(const gdpck_set *set, uint64_t key) {
    size_t slot = (size_t)(key ^ (key >> 29)) & set->mask;
    while (set->keys[slot] && set->keys[slot] != key)
        slot = (slot + 1U) & set->mask;
    return slot;
}

static bool gdpck_set_grow(gdpck_set *set) {
    gdpck_set bigger;
    size_t index, size = set->mask + 1U;
    if (size >= ((size_t)1 << 22)) return false;
    xx_mem_zero(&bigger, sizeof(bigger));
    bigger.keys = (uint64_t *)xx_mem_calloc(size * 2U, sizeof(*bigger.keys));
    bigger.values =
        (uint32_t *)xx_mem_calloc(size * 2U, sizeof(*bigger.values));
    if (!bigger.keys || !bigger.values) {
        gdpck_set_cleanup(&bigger);
        return false;
    }
    bigger.mask = size * 2U - 1U;
    for (index = 0U; index < size; ++index) {
        if (set->keys[index]) {
            size_t slot = gdpck_set_slot(&bigger, set->keys[index]);
            bigger.keys[slot] = set->keys[index];
            bigger.values[slot] = set->values[index];
            ++bigger.used;
        }
    }
    gdpck_set_cleanup(set);
    *set = bigger;
    return true;
}

static bool gdpck_set_put(gdpck_set *set, uint64_t key, uint32_t value) {
    size_t slot;
    if ((set->used + 1U) * 2U > set->mask + 1U && !gdpck_set_grow(set))
        return false;
    slot = gdpck_set_slot(set, key);
    if (!set->keys[slot]) {
        set->keys[slot] = key;
        ++set->used;
    }
    set->values[slot] = value;
    return true;
}

static bool gdpck_set_has(const gdpck_set *set, uint64_t key,
                          uint32_t *value) {
    size_t slot = gdpck_set_slot(set, key);
    if (!set->keys[slot]) return false;
    if (value) *value = set->values[slot];
    return true;
}

/* "dir/name.ext" with "_<n>" in front of the extension of the last
 * component (or at its end when it has none, or only a leading dot). */
static char *gdpck_suffixed(const char *path, uint32_t number) {
    size_t length = xx_str_len(path), last = 0U, dot = length, index;
    char digits[16];
    int written;
    char *out;
    for (index = 0U; index < length; ++index)
        if (path[index] == '/') last = index + 1U;
    for (index = length; index > last + 1U; --index) {
        if (path[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    written = xx_rt_snprintf(digits, sizeof(digits), "_%u", (unsigned)number);
    if (written <= 0 || (size_t)written >= sizeof(digits)) return NULL;
    out = (char *)xx_mem_alloc(length + (size_t)written + 1U);
    if (!out) return NULL;
    xx_rt_memcpy(out, path, dot);
    xx_rt_memcpy(out + dot, digits, (size_t)written);
    xx_rt_memcpy(out + dot + (size_t)written, path + dot, length - dot);
    out[length + (size_t)written] = '\0';
    return out;
}

/* ------------------------------------------------------------ directory -- */

static bool gdpck_next_entry(gdpck_input *in, const gdpck_layout *layout,
                             uint8_t *path, gdpck_entry *entry) {
    uint8_t word[4];
    uint8_t tail[36];
    size_t tail_size = layout->version >= 2U ? 36U : 32U;
    uint32_t path_size;
    size_t length = 0U;
    uint64_t offset;

    xx_mem_zero(entry, sizeof(*entry));
    entry->header_offset = in->position;
    if (!gdpck_input_read(in, word, sizeof(word))) return false;
    path_size = gdpck_le32(word);
    if (path_size == 0U || path_size > XX_GODOT_ENGINE_PCK_MAX_PATH ||
        !gdpck_input_read(in, path, path_size))
        return false;
    path[path_size] = 0U;
    while (length < path_size && path[length]) ++length;
    if (length == 0U || !gdpck_path_text_ok(path, length)) return false;
    entry->name_length = length;
    if (!gdpck_input_read(in, tail, tail_size)) return false;
    entry->header_size = in->position - entry->header_offset;
    offset = gdpck_le64(tail);
    entry->size = gdpck_le64(tail + 8);
    xx_rt_memcpy(entry->md5, tail + 16, 16U);
    entry->flags = layout->version >= 2U ? gdpck_le32(tail + 32) : 0U;
    if (entry->flags & ~(layout->version >= 3U ? GDPCK_FILE_FLAGS_V3
                                               : GDPCK_FILE_FLAGS_V2))
        return false;
    if (entry->size > (uint64_t)INT64_MAX - 64U) return false;
    if (entry->flags & XX_GODOT_ENGINE_PCK_FILE_REMOVAL) {
        entry->removal = true;
        entry->data_offset = -1;
        return true;
    }
    if (layout->flags & XX_GODOT_ENGINE_PCK_FLAG_SPARSE_BUNDLE) {
        /* The exporter wrote the member next to the pack and stores offset
         * zero; the engine ignores the offset. */
        entry->external = true;
        entry->data_offset = -1;
        return true;
    }
    if (offset > (uint64_t)INT64_MAX ||
        (int64_t)offset > INT64_MAX - layout->file_base)
        return false;
    entry->data_offset = layout->file_base + (int64_t)offset;
    entry->stored_size =
        (entry->flags & XX_GODOT_ENGINE_PCK_FILE_ENCRYPTED)
            ? GDPCK_CRYPT_HEADER + (int64_t)((entry->size + 15U) & ~UINT64_C(15))
            : (int64_t)entry->size;
    return true;
}

static uint8_t *gdpck_input_open(gdpck_input *in, xx_io_device *device,
                                 const gdpck_layout *layout,
                                 uint8_t **path) {
    uint8_t *memory = (uint8_t *)xx_mem_alloc(
        GDPCK_WINDOW + XX_GODOT_ENGINE_PCK_MAX_PATH + 1U);
    xx_mem_zero(in, sizeof(*in));
    if (!memory) return NULL;
    in->device = device;
    in->window = memory;
    in->position = layout->dir_start + 4;
    in->limit = layout->region_end;
    *path = memory + GDPCK_WINDOW;
    return memory;
}

/* Every directory entry, against the file base already in @p layout. */
static bool gdpck_walk(xx_io_device *device, gdpck_layout *layout,
                       xx_pd_struct *pd) {
    gdpck_input in;
    gdpck_entry entry;
    uint8_t *path = NULL;
    uint8_t *memory = gdpck_input_open(&in, device, layout, &path);
    int64_t data_low = layout->version >= 2U ? layout->file_base
                                             : layout->pack;
    int64_t data_high = layout->version >= 3U ? layout->dir_start
                                              : layout->region_end;
    int64_t data_min = INT64_MAX, data_max = 0;
    uint64_t listed = 0U;
    bool any_encrypted = false;
    bool result = false;
    uint32_t index;

    if (!memory) return false;
    for (index = 0U; index < layout->file_count; ++index) {
        if ((index & 1023U) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (!gdpck_next_entry(&in, layout, path, &entry)) goto done;
        if (entry.removal) continue;
        if (!entry.external) {
            if (entry.data_offset < data_low ||
                entry.data_offset > data_high ||
                entry.stored_size > data_high - entry.data_offset)
                goto done;
            if (entry.data_offset < data_min) data_min = entry.data_offset;
            if (entry.data_offset + entry.stored_size > data_max)
                data_max = entry.data_offset + entry.stored_size;
        }
        if (entry.flags & XX_GODOT_ENGINE_PCK_FILE_ENCRYPTED)
            any_encrypted = true;
        ++listed;
    }
    layout->dir_end = in.position;
    /* The directory comes first in versions 0..2: data inside it is not a
     * member.  Versions 3/4 already bounded the data by the directory. */
    if (data_min != INT64_MAX && layout->version < 3U &&
        data_min < layout->dir_end)
        goto done;
    layout->data_end = data_max;
    layout->listed = listed;
    layout->any_encrypted = any_encrypted;
    result = true;
done:
    xx_mem_free(memory);
    return result;
}

/* An encrypted directory: only its envelope can be checked. */
static bool gdpck_encrypted_directory(xx_io_device *device,
                                      gdpck_layout *layout) {
    uint8_t envelope[GDPCK_CRYPT_HEADER];
    int64_t start = layout->dir_start + 4;
    uint64_t plain, padded;
    if (layout->region_end - start < GDPCK_CRYPT_HEADER ||
        !gdpck_read_at(device, start, envelope, sizeof(envelope)))
        return false;
    plain = gdpck_le64(envelope + 16);
    if (plain > (uint64_t)INT64_MAX - 64U) return false;
    padded = (plain + 15U) & ~UINT64_C(15);
    if (plain < (uint64_t)layout->file_count * GDPCK_MIN_ENTRY_V2 ||
        padded > (uint64_t)(layout->region_end - start - GDPCK_CRYPT_HEADER))
        return false;
    layout->dir_end = start + GDPCK_CRYPT_HEADER + (int64_t)padded;
    layout->data_end = 0;
    layout->listed = 0U;
    layout->any_encrypted = true;
    return true;
}

/* --------------------------------------------------------------- header -- */

static bool gdpck_header(xx_io_device *device, gdpck_layout *layout) {
    uint8_t header[GDPCK_V3_HEADER];
    uint8_t word[4];
    int64_t available = layout->region_end - layout->pack;
    size_t index, reserved_start, reserved_end;
    uint32_t minimum_entry;
    int64_t entries_room;

    if (available < GDPCK_MIN_PACK ||
        !gdpck_read_at(device, layout->pack, header, 20U) ||
        xx_rt_memcmp(header, GDPCK_MAGIC, 4U) != 0)
        return false;
    layout->version = gdpck_le32(header + 4);
    layout->major = gdpck_le32(header + 8);
    layout->minor = gdpck_le32(header + 12);
    layout->patch = gdpck_le32(header + 16);
    /* Godot 4 packs (versions 2..4) come from engine 4 or later. */
    if (layout->version > 4U || layout->major == 0U || layout->major > 99U ||
        layout->minor > 999U || layout->patch > 9999U ||
        (layout->version >= 2U && layout->major < 4U))
        return false;
    if (layout->version <= 1U) {
        reserved_start = 0x14U;
        reserved_end = GDPCK_V1_COUNT;
        layout->dir_start = layout->pack + GDPCK_V1_COUNT;
    } else if (layout->version == 2U) {
        if (available < GDPCK_V2_COUNT + 4) return false;
        reserved_start = 0x20U;
        reserved_end = GDPCK_V2_COUNT;
        layout->dir_start = layout->pack + GDPCK_V2_COUNT;
    } else {
        if (available < GDPCK_V3_HEADER + 4) return false;
        reserved_start = 0x28U;
        reserved_end = GDPCK_V3_HEADER;
    }
    if (!gdpck_read_at(device, layout->pack + 20, header + 20,
                       reserved_end - 20U))
        return false;
    if (layout->version >= 2U) {
        layout->flags = gdpck_le32(header + 0x14);
        layout->stored_file_base = gdpck_le64(header + 0x18);
        if (layout->flags & ~(layout->version >= 3U ? GDPCK_PACK_FLAGS_V3
                                                    : GDPCK_PACK_FLAGS_V2))
            return false;
        layout->dir_encrypted =
            (layout->flags & XX_GODOT_ENGINE_PCK_FLAG_DIR_ENCRYPTED) != 0U;
        if (layout->version == 4U && layout->dir_encrypted &&
            (layout->flags & XX_GODOT_ENGINE_PCK_FLAG_SPARSE_BUNDLE))
            reserved_start = GDPCK_V4_SALT_END;
        /* A base counted from the pack stays inside it; one counted from
         * the start of the holding file stays inside the device. */
        if (layout->stored_file_base >
            (uint64_t)((layout->version >= 3U ||
                        (layout->flags &
                         XX_GODOT_ENGINE_PCK_FLAG_REL_FILEBASE))
                           ? available
                           : layout->total))
            return false;
    }
    for (index = reserved_start; index < reserved_end; ++index)
        if (header[index] != 0U) return false;
    if (layout->version >= 3U) {
        uint64_t dir_offset = gdpck_le64(header + 0x20);
        if (layout->stored_file_base < GDPCK_V3_HEADER ||
            dir_offset < layout->stored_file_base ||
            dir_offset > (uint64_t)(available - 4))
            return false;
        layout->dir_start = layout->pack + (int64_t)dir_offset;
    }
    if (layout->dir_start > layout->region_end - 4 ||
        !gdpck_read_at(device, layout->dir_start, word, sizeof(word)))
        return false;
    layout->file_count = gdpck_le32(word);
    if (layout->file_count > XX_GODOT_ENGINE_PCK_MAX_ENTRIES) return false;
    if (!layout->dir_encrypted) {
        minimum_entry = layout->version >= 2U ? GDPCK_MIN_ENTRY_V2
                                              : GDPCK_MIN_ENTRY_V1;
        entries_room = layout->region_end - layout->dir_start - 4;
        if ((int64_t)layout->file_count > entries_room / minimum_entry)
            return false;
    }
    return true;
}

/* ------------------------------------------------------------- location -- */

/* A PE image whose section "pck" starts with the pack (Godot looks up to
 * seven bytes past the section start for alignment slack). */
static bool gdpck_pe_section(xx_io_device *device, gdpck_layout *layout) {
    uint8_t dos[64];
    uint8_t nt[24];
    uint8_t table[GDPCK_PE_MAX_SECTIONS * 40U];
    int64_t base = layout->base, total = layout->total, lfanew, table_at;
    uint32_t count, index, slack;
    if (total - base < 64 || !gdpck_read_at(device, base, dos, sizeof(dos)) ||
        dos[0] != 'M' || dos[1] != 'Z')
        return false;
    lfanew = (int64_t)gdpck_le32(dos + 0x3C);
    if (lfanew > INT64_C(0x10000000) || lfanew > total - base - 24 ||
        !gdpck_read_at(device, base + lfanew, nt, sizeof(nt)) ||
        xx_rt_memcmp(nt, "PE\0\0", 4U) != 0)
        return false;
    count = gdpck_le16(nt + 6);
    table_at = base + lfanew + 24 + (int64_t)gdpck_le16(nt + 20);
    if (count == 0U || count > GDPCK_PE_MAX_SECTIONS ||
        table_at > total - (int64_t)count * 40 ||
        !gdpck_read_at(device, table_at, table, (size_t)count * 40U))
        return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *section = table + (size_t)index * 40U;
        int64_t start, end;
        if (xx_rt_memcmp(section, "pck\0\0\0\0\0", 8U) != 0) continue;
        start = base + (int64_t)gdpck_le32(section + 20);
        end = start + (int64_t)gdpck_le32(section + 16);
        if (end > total) end = total;
        for (slack = 0U; slack < 8U; ++slack) {
            if (start + (int64_t)slack > end - GDPCK_MIN_PACK) break;
            if (gdpck_is_magic(device, start + (int64_t)slack)) {
                layout->pack = start + (int64_t)slack;
                layout->region_end = end;
                layout->placement = XX_GODOT_ENGINE_PCK_PE_SECTION;
                return true;
            }
        }
    }
    return false;
}

static bool gdpck_locate(xx_io_device *device, gdpck_layout *layout) {
    uint8_t trailer[GDPCK_TRAILER];
    int64_t base = layout->base, total = layout->total;
    if (total - base < GDPCK_MIN_PACK) return false;
    if (gdpck_is_magic(device, base)) {
        layout->pack = base;
        layout->region_end = total;
        layout->placement = XX_GODOT_ENGINE_PCK_STANDALONE;
        return gdpck_header(device, layout);
    }
    if (total - base >= GDPCK_MIN_PACK + GDPCK_TRAILER &&
        gdpck_read_at(device, total - GDPCK_TRAILER, trailer,
                      sizeof(trailer)) &&
        xx_rt_memcmp(trailer + 8, GDPCK_MAGIC, 4U) == 0) {
        uint64_t size = gdpck_le64(trailer);
        if (size >= GDPCK_MIN_PACK &&
            size <= (uint64_t)(total - GDPCK_TRAILER - base) &&
            gdpck_is_magic(device, total - GDPCK_TRAILER - (int64_t)size)) {
            layout->pack = total - GDPCK_TRAILER - (int64_t)size;
            layout->region_end = total - GDPCK_TRAILER;
            layout->placement = XX_GODOT_ENGINE_PCK_TRAILER;
            return gdpck_header(device, layout);
        }
    }
    return gdpck_pe_section(device, layout) && gdpck_header(device, layout);
}

/* Header, file base and directory; fills the whole layout. */
static bool gdpck_parse(Abstractformat *format, gdpck_layout *layout,
                        xx_pd_struct *pd) {
    int64_t origins[2];
    size_t count = 0U, index;
    if (!format || !format->device || format->base_address < 0) return false;
    xx_mem_zero(layout, sizeof(*layout));
    layout->base = format->base_address;
    layout->total = xx_io_total_size(format->device);
    if (layout->total < 0 || layout->base > layout->total ||
        !gdpck_locate(format->device, layout))
        return false;

    /* Candidate origins of the member offsets, most likely first. */
    if (layout->version >= 3U ||
        (layout->version == 2U &&
         (layout->flags & XX_GODOT_ENGINE_PCK_FLAG_REL_FILEBASE))) {
        origins[count++] = layout->pack;
    } else if (layout->placement == XX_GODOT_ENGINE_PCK_STANDALONE) {
        origins[count++] = layout->pack;
        if (layout->pack > 0) origins[count++] = 0;
    } else {
        origins[count++] = layout->base;
        if (layout->pack != layout->base) origins[count++] = layout->pack;
    }
    for (index = 0U; index < count; ++index) {
        if (layout->stored_file_base > (uint64_t)(INT64_MAX - origins[index]))
            continue;
        layout->file_base = origins[index] + (int64_t)layout->stored_file_base;
        if (layout->dir_encrypted ? gdpck_encrypted_directory(format->device,
                                                               layout)
                                  : gdpck_walk(format->device, layout, pd))
            break;
        /* An encrypted directory does not depend on the origin. */
        if (layout->dir_encrypted) return false;
    }
    if (index == count) return false;

    if (layout->placement != XX_GODOT_ENGINE_PCK_STANDALONE) {
        /* The whole executable is the container. */
        layout->format_end = layout->total;
    } else if (layout->dir_encrypted && layout->version == 2U) {
        /* The members follow a directory that cannot be read. */
        layout->format_end = layout->region_end;
    } else {
        uint8_t pad[16];
        int64_t end = layout->dir_end > layout->data_end ? layout->dir_end
                                                         : layout->data_end;
        size_t gap = (size_t)((16 - ((end - layout->pack) & 15)) & 15);
        /* Godot pads the last member to 16 bytes; the zeros belong to it. */
        if (gap != 0U && end <= layout->total - (int64_t)gap &&
            gdpck_read_at(format->device, end, pad, gap)) {
            for (index = 0U; index < gap && pad[index] == 0U; ++index) {
            }
            if (index == gap) end += (int64_t)gap;
        }
        layout->format_end = end;
    }
    return true;
}

/* ------------------------------------------------------------- lifecycle -- */

void xx_godot_engine_pck_init(xx_godot_engine_pck *archive,
                              xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GODOT_ENGINE_PCK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-godot-resource-pack");
    xx_format_set_extension(&archive->format, "pck");
    archive->format.check_is_valid = xx_godot_engine_pck_check_is_valid;
    archive->format.handle_base_info = xx_godot_engine_pck_handle_base_info;
    archive->format.get_format_size = xx_godot_engine_pck_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_godot_engine_pck_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_godot_engine_pck_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_godot_engine_pck_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_godot_engine_pck_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_godot_engine_pck_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_godot_engine_pck_free_archive_records_reading;
    archive->pack_offset = -1;
    archive->file_base = -1;
    archive->directory_offset = -1;
}

xx_godot_engine_pck *xx_godot_engine_pck_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_godot_engine_pck *archive =
        (xx_godot_engine_pck *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_godot_engine_pck_init(archive, device, base_address);
    return archive;
}

void xx_godot_engine_pck_destroy(xx_godot_engine_pck *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_godot_engine_pck_free(xx_godot_engine_pck *archive) {
    if (!archive) return;
    xx_godot_engine_pck_destroy(archive);
    xx_mem_free(archive);
}

bool xx_godot_engine_pck_check_is_valid(Abstractformat *format,
                                        xx_pd_struct *pd) {
    gdpck_layout layout;
    return gdpck_parse(format, &layout, pd);
}

bool xx_godot_engine_pck_handle_base_info(Abstractformat *format,
                                          xx_pd_struct *pd) {
    gdpck_layout layout;
    xx_godot_engine_pck *archive;
    char version[32];
    if (!format || !gdpck_parse(format, &layout, pd)) return false;
    archive = (xx_godot_engine_pck *)format;
    archive->number_of_records = layout.listed;
    archive->pack_version = layout.version;
    archive->engine_major = layout.major;
    archive->engine_minor = layout.minor;
    archive->engine_patch = layout.patch;
    archive->pack_flags = layout.flags;
    archive->file_count = layout.file_count;
    archive->pack_offset = layout.pack;
    archive->file_base = layout.file_base;
    archive->directory_offset = layout.dir_start;
    archive->placement = layout.placement;
    archive->directory_encrypted = layout.dir_encrypted;
    (void)xx_rt_snprintf(version, sizeof(version), "%u (Godot %u.%u.%u)",
                         (unsigned)layout.version, (unsigned)layout.major,
                         (unsigned)layout.minor, (unsigned)layout.patch);
    xx_format_set_version(format, version);
    format->is_crypted = layout.any_encrypted;
    format->number_of_archive_records = layout.listed;
    format->format_size = layout.format_end - layout.base;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_godot_engine_pck_get_format_size(Abstractformat *format,
                                            xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_godot_engine_pck_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_godot_engine_pck_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_godot_engine_pck_handle_base_info(format, pd))
               ? ((xx_godot_engine_pck *)format)->number_of_records
               : 0U;
}

/* --------------------------------------------------------------- records -- */

static void gdpck_stream_free(void *opaque) {
    gdpck_stream *stream = (gdpck_stream *)opaque;
    if (!stream) return;
    if (stream->memory) xx_mem_free(stream->memory);
    if (stream->output_name) xx_mem_free(stream->output_name);
    gdpck_set_cleanup(&stream->taken);
    gdpck_set_cleanup(&stream->hints);
    xx_mem_free(stream);
}

static bool gdpck_copy_options(xx_list_s *destination,
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

static const xx_var *gdpck_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* The member's path as the pack names it, without "res://". */
static const char *gdpck_display_name(gdpck_stream *stream) {
    const char *text = (const char *)stream->path;
    if (stream->entry.name_length > 6U &&
        xx_rt_memcmp(text, "res://", 6U) == 0)
        text += 6;
    return text;
}

/* A safe, session-unique output path for the current entry, or NULL when
 * its path is refused (or no free name could be found). */
static char *gdpck_output_name(gdpck_stream *stream, const char *display) {
    size_t length = xx_str_len(display), index;
    char *path = (char *)xx_mem_alloc(length + 1U);
    uint64_t key, stem_key;
    uint32_t next = 1U, tries;
    if (!path) return NULL;
    for (index = 0U; index <= length; ++index)
        path[index] = display[index] == '\\' ? '/' : display[index];
    if (!gdpck_path_safe(path)) {
        xx_mem_free(path);
        return NULL;
    }
    key = gdpck_hash(path);
    if (!gdpck_set_has(&stream->taken, key, NULL)) {
        if (!gdpck_set_put(&stream->taken, key, 1U)) {
            xx_mem_free(path);
            return NULL;
        }
        return path;
    }
    /* A duplicate: "name_<n>.ext" with the lowest n not handed out yet;
     * the hint keeps a run of duplicates of one path linear. */
    stem_key = key ^ UINT64_C(0x9E3779B97F4A7C15);
    if (!stem_key) stem_key = 1U;
    (void)gdpck_set_has(&stream->hints, stem_key, &next);
    for (tries = 0U; tries < GDPCK_MAX_SUFFIX_TRIES; ++tries, ++next) {
        char *candidate = gdpck_suffixed(path, next);
        uint64_t candidate_key;
        if (!candidate) break;
        candidate_key = gdpck_hash(candidate);
        if (!gdpck_set_has(&stream->taken, candidate_key, NULL)) {
            if (!gdpck_set_put(&stream->taken, candidate_key, 1U) ||
                !gdpck_set_put(&stream->hints, stem_key, next + 1U)) {
                xx_mem_free(candidate);
                break;
            }
            xx_mem_free(path);
            return candidate;
        }
        xx_mem_free(candidate);
    }
    xx_mem_free(path);
    return NULL;
}

static bool gdpck_set_record(xx_archive_record *record,
                             const gdpck_entry *entry, const char *name) {
    bool encrypted =
        (entry->flags & XX_GODOT_ENGINE_PCK_FILE_ENCRYPTED) != 0U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->stored_size;
    return xx_archive_record_set_original_name(record, name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->stored_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          entry->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

/* Read directory entries up to the next listed member and make it the
 * current record.  False at the end of the directory or on any error. */
static bool gdpck_advance(gdpck_stream *stream,
                          xx_archive_record_state *state) {
    const char *display;
    if (stream->output_name) {
        xx_mem_free(stream->output_name);
        stream->output_name = NULL;
    }
    for (;;) {
        if (stream->consumed >= stream->layout.file_count) return false;
        ++stream->consumed;
        if (!gdpck_next_entry(&stream->input, &stream->layout, stream->path,
                              &stream->entry))
            return false;
        if (!stream->entry.removal) break;
    }
    display = gdpck_display_name(stream);
    stream->output_name = gdpck_output_name(stream, display);
    return gdpck_set_record(&state->current_record, &stream->entry,
                            stream->output_name ? stream->output_name
                                                : display);
}

xx_archive_record_state *xx_godot_engine_pck_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    gdpck_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (gdpck_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!gdpck_parse(format, &stream->layout, pd) ||
        !gdpck_set_init(&stream->taken, stream->layout.listed) ||
        !gdpck_set_init(&stream->hints, 8U)) {
        gdpck_stream_free(stream);
        return NULL;
    }
    stream->memory = gdpck_input_open(&stream->input, format->device,
                                      &stream->layout, &stream->path);
    if (!stream->memory) {
        gdpck_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        gdpck_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = gdpck_stream_free;
    state->total_records = (int64_t)stream->layout.listed;
    if (!gdpck_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = stream->layout.listed != 0U &&
                        !stream->layout.dir_encrypted &&
                        gdpck_advance(stream, state);
    return state;
}

const xx_archive_record *xx_godot_engine_pck_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_godot_engine_pck_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    gdpck_stream *stream;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (gdpck_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    if (stream->index + 1U >= stream->layout.listed ||
        !gdpck_advance(stream, state)) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    return true;
}

/* Copy the stored member to @p destination (NULL only verifies), checking
 * the directory's MD5 when the writer filled it in. */
static bool gdpck_copy_member(xx_io_device *device, const gdpck_entry *entry,
                              xx_io_device *destination, xx_pd_struct *pd) {
    static const uint8_t zero[16] = {0};
    xx_hash_context md5;
    uint8_t digest[16];
    uint8_t *buffer;
    int64_t offset = entry->data_offset;
    uint64_t remaining = entry->size;
    bool check = xx_rt_memcmp(entry->md5, zero, 16U) != 0;
    bool result = false;

    if (check && !xx_hash_init(&md5, XX_HASH_MD5)) return false;
    buffer = (uint8_t *)xx_mem_alloc(GDPCK_WINDOW);
    if (!buffer) return false;
    while (remaining != 0U) {
        size_t chunk = remaining > GDPCK_WINDOW ? GDPCK_WINDOW
                                                : (size_t)remaining;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !gdpck_read_at(device, offset, buffer, chunk))
            goto done;
        if (check) xx_hash_update(&md5, buffer, chunk);
        while (destination && written < chunk) {
            ssize_t sent = xx_io_write(destination, buffer + written,
                                       chunk - written);
            if (sent <= 0 || (size_t)sent > chunk - written) goto done;
            written += (size_t)sent;
        }
        offset += (int64_t)chunk;
        remaining -= chunk;
    }
    if (check && (!xx_hash_final(&md5, digest, sizeof(digest)) ||
                  xx_rt_memcmp(digest, entry->md5, 16U) != 0))
        goto done;
    result = true;
done:
    xx_mem_free(buffer);
    return result;
}

bool xx_godot_engine_pck_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    gdpck_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool created = false;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (gdpck_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    /* Not in the pack (sparse bundle), or the stored bytes are not the
     * member: ciphertext without the key, or a delta against another pack. */
    if (stream->entry.data_offset < 0 ||
        (stream->entry.flags & (XX_GODOT_ENGINE_PCK_FILE_ENCRYPTED |
                                XX_GODOT_ENGINE_PCK_FILE_DELTA)))
        return false;
    path_option = gdpck_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return gdpck_copy_member(format->device, &stream->entry, NULL, pd);
    if (!stream->output_name) return false;
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
               ? xx_str_concat3(base, "/", stream->output_name)
               : xx_str_concat(base, stream->output_name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = gdpck_copy_member(format->device, &stream->entry,
                                   destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_godot_engine_pck_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
