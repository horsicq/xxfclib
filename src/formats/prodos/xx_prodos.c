/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Apple ProDOS / SOS volume reader.  xx_prodos.h carries the field table.
 *
 * Written from the published ProDOS layout (ProDOS 8 Technical Reference
 * Manual, GS/OS Technical Note #8 for the lower-case flags and the extended
 * file key block); no reference implementation was ported.
 *
 * The volume is walked once, depth first, in directory order.  Every
 * directory block is marked in a bitmap of the volume before it is read, so
 * a chain or a subdirectory pointer that loops is cut instead of followed,
 * and the walk reads each block at most once.  File data is streamed a
 * block at a time through the seedling / sapling / tree index; a zero
 * pointer is a sparse block of zeros, and any pointer past the volume or the
 * image fails that member only.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/prodos/xx_prodos.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as PRODOS is registered there. */
#ifdef PRODOS
#define XX_PRODOS_FILE_TYPE XX_FILE_TYPE_PRODOS
#else
#define XX_PRODOS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PD_BLOCK 512U
#define PD_HALF 256U
#define PD_VOLUME_KEY 2U
#define PD_ENTRY_LENGTH 0x27U
#define PD_ENTRIES_PER_BLOCK 0x0DU
/* Two boot blocks, one directory block, one bitmap block and room for a
 * file: anything smaller is not a volume anyone formatted. */
#define PD_MIN_BLOCKS 6U
#define PD_MAX_BLOCKS 65535U
#define PD_DOS_IMAGE_SIZE INT64_C(143360)
#define PD_DOS_BLOCKS 280U
#define PD_MAX_MEMBERS 100000U
#define PD_MAX_DEPTH 64U
#define PD_MAX_SUFFIX 10000U
#define PD_COMPONENT_MAX 24U

#define PD_ST_DELETED 0x0U
#define PD_ST_SEEDLING 0x1U
#define PD_ST_SAPLING 0x2U
#define PD_ST_TREE 0x3U
#define PD_ST_EXTENDED 0x5U
#define PD_ST_SUBDIR 0xDU
#define PD_ST_SUBDIR_HEADER 0xEU
#define PD_ST_VOLUME_HEADER 0xFU

/* Entry fields (offsets inside one directory entry). */
#define PD_E_TYPE 0x10U
#define PD_E_KEY 0x11U
#define PD_E_BLOCKS 0x13U
#define PD_E_EOF 0x15U
#define PD_E_CASE 0x1CU
#define PD_E_ACCESS 0x1EU
#define PD_E_AUX 0x1FU
#define PD_E_MOD_DATE 0x21U
#define PD_E_MOD_TIME 0x23U
/* Header fields. */
#define PD_H_VOLUME_CASE 0x16U
#define PD_H_ENTRY_LENGTH 0x1FU
#define PD_H_ENTRIES 0x20U
#define PD_H_FILE_COUNT 0x21U
#define PD_H_BITMAP 0x23U
#define PD_H_TOTAL 0x25U

/* DOS 3.3 logical sector holding each ProDOS 256-byte half-block of a
 * track: ProDOS block k of a track is halves 2k and 2k+1. */
static const uint8_t k_dos_sector[16] = {0U, 14U, 13U, 12U, 11U, 10U, 9U, 8U,
                                         7U, 6U,  5U,  4U,  3U,  2U,  1U, 15U};

typedef struct pd_volume_s {
    xx_io_device *device;
    int64_t base;
    int64_t image_size;   /**< Bytes available from base. */
    uint32_t total_blocks;
    uint32_t file_count;
    bool dos_order;
    char name[PD_COMPONENT_MAX];
} pd_volume;

typedef struct pd_member_s {
    char *name;
    int64_t header_offset; /**< Device offset of the directory entry. */
    int64_t data_offset;   /**< Device offset of the key block, or -1. */
    uint32_t eof;
    uint32_t key;
    uint32_t blocks_used;
    uint32_t modified;
    uint16_t aux_type;
    uint8_t storage;
    uint8_t file_type;
    uint8_t access;
    bool folder;
    bool resource;
    bool damaged;
} pd_member;

typedef struct pd_list_s {
    pd_volume volume;
    pd_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    uint32_t *slots;  /**< Case-folded name set: member index + 1, 0 free. */
    size_t slot_count;
    bool damaged;
} pd_list;

typedef struct pd_frame_s {
    uint8_t data[PD_BLOCK];
    uint32_t block;
    uint32_t slot;
    uint32_t entry_length;
    uint32_t entries_per_block;
    size_t folder; /**< Member index of this directory, SIZE_MAX for root. */
} pd_frame;

static uint32_t pd_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t pd_le24(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U);
}

static bool pd_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Image offset (relative to the volume start) of byte @p at of @p block. */
static int64_t pd_block_offset(const pd_volume *volume, uint32_t block,
                               uint32_t at) {
    uint32_t half;
    if (!volume->dos_order)
        return (int64_t)block * PD_BLOCK + (int64_t)at;
    half = (block % 8U) * 2U + (at >= PD_HALF ? 1U : 0U);
    return (int64_t)(block / 8U) * 4096 +
           (int64_t)k_dos_sector[half] * PD_HALF + (int64_t)(at % PD_HALF);
}

static bool pd_read_block(const pd_volume *volume, uint32_t block,
                          uint8_t *buffer) {
    int64_t first, second;
    if (block >= volume->total_blocks) return false;
    first = pd_block_offset(volume, block, 0U);
    if (!volume->dos_order) {
        if (first > volume->image_size - (int64_t)PD_BLOCK) return false;
        return pd_read_at(volume->device, volume->base + first, buffer,
                          PD_BLOCK);
    }
    second = pd_block_offset(volume, block, PD_HALF);
    if (first > volume->image_size - (int64_t)PD_HALF ||
        second > volume->image_size - (int64_t)PD_HALF)
        return false;
    return pd_read_at(volume->device, volume->base + first, buffer, PD_HALF) &&
           pd_read_at(volume->device, volume->base + second, buffer + PD_HALF,
                      PD_HALF);
}

static bool pd_name_char(uint8_t c, bool first) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
    return !first && ((c >= '0' && c <= '9') || c == '.');
}

static char pd_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the first @p stem bytes of @p name spell @p word, any case. */
static bool pd_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || pd_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

static bool pd_device_name(const char *name) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index;
    while (name[stem] && name[stem] != '.') ++stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (pd_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           (pd_stem_is(name, 3U, "COM") || pd_stem_is(name, 3U, "LPT"));
}

/* Decode one entry name (length in the low nibble of byte 0, up to 15
 * characters from byte 1) into a host-safe path component.  The GS/OS
 * lower-case word is honoured when its bit 15 is set.  ProDOS only allows
 * A-Z, 0-9 and '.', starting with a letter, so anything else - which only
 * a damaged or hostile entry can hold - becomes '_'; a leading or trailing
 * '.' (never valid ProDOS, and "." / ".." or a name Windows would trim)
 * becomes '_' too, and a Windows device stem gets a '_' prefix. */
static void pd_component(const uint8_t *raw, uint32_t length, uint32_t flags,
                         char *out) {
    uint32_t index, at = 0U;
    bool use_case = (flags & 0x8000U) != 0U;
    if (length == 0U || length > 15U) length = 0U;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)(raw[index] & 0x7FU);
        if (!pd_name_char(c, false)) c = '_';
        if (use_case && c >= 'A' && c <= 'Z' &&
            (flags & (0x4000U >> index)) != 0U)
            c = (uint8_t)(c - 'A' + 'a');
        out[at++] = (char)c;
    }
    if (at == 0U) out[at++] = '_';
    out[at] = 0;
    if (out[0] == '.') out[0] = '_';
    if (out[at - 1U] == '.') out[at - 1U] = '_';
    if (pd_device_name(out)) {
        for (index = at + 1U; index > 0U; --index) out[index] = out[index - 1U];
        out[0] = '_';
    }
}

/* ProDOS packs the date as yyyyyyym mmmddddd and the time as 000hhhhh
 * 00mmmmmm.  Years below 40 are 20xx (ProDOS 8 Technical Note #28), 40..99
 * are 19xx, and the GS/OS values 100..127 are 2000..2027.  The result uses
 * the Binary II reader's packed layout: year << 20 | month << 16 | day << 11
 * | hour << 6 | minute. */
static uint32_t pd_time(uint32_t date, uint32_t time) {
    uint32_t year = (date >> 9U) & 0x7FU, month = (date >> 5U) & 0x0FU;
    uint32_t day = date & 0x1FU, hour = (time >> 8U) & 0x1FU;
    uint32_t minute = time & 0x3FU;
    if (date == 0U) return 0U;
    if (month < 1U || month > 12U || day < 1U || hour > 23U || minute > 59U)
        return 0U;
    year += year < 40U ? 2000U : 1900U;
    return (year << 20U) | (month << 16U) | (day << 11U) | (hour << 6U) |
           minute;
}

/* The volume key block: previous pointer 0, a 0xF header with a legal
 * name, the fixed 0x27 x 13 entry geometry, and a bitmap and next pointer
 * that fit the declared size. */
static bool pd_check_key_block(const uint8_t *block, pd_volume *volume) {
    const uint8_t *header = block + 4U;
    uint32_t length = header[0] & 0x0FU, index, total, bitmap, next;
    if (pd_le16(block) != 0U || (header[0] >> 4U) != PD_ST_VOLUME_HEADER ||
        length == 0U)
        return false;
    for (index = 0U; index < length; ++index)
        if (!pd_name_char(header[1U + index], index == 0U)) return false;
    if (header[PD_H_ENTRY_LENGTH] != PD_ENTRY_LENGTH ||
        header[PD_H_ENTRIES] != PD_ENTRIES_PER_BLOCK)
        return false;
    total = pd_le16(header + PD_H_TOTAL);
    bitmap = pd_le16(header + PD_H_BITMAP);
    next = pd_le16(block + 2U);
    if (total < PD_MIN_BLOCKS) return false;
    if (bitmap < 3U || bitmap + (total + 4095U) / 4096U > total) return false;
    if (next != 0U && (next < 3U || next >= total)) return false;
    volume->total_blocks = total;
    volume->file_count = pd_le16(header + PD_H_FILE_COUNT);
    pd_component(header + 1U, length, pd_le16(header + PD_H_VOLUME_CASE),
                 volume->name);
    return true;
}

static bool pd_open_volume(Abstractformat *format, pd_volume *volume,
                           uint8_t *key_block) {
    int64_t total;
    if (!format || !format->device || !volume || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(volume, sizeof(*volume));
    volume->device = format->device;
    volume->base = format->base_address;
    volume->image_size = total - format->base_address;
    if (volume->image_size < (int64_t)(3U * PD_BLOCK)) return false;
    /* ProDOS (block) order first: the key block is at 0x400. */
    volume->total_blocks = PD_MAX_BLOCKS;
    if (pd_read_block(volume, PD_VOLUME_KEY, key_block) &&
        pd_check_key_block(key_block, volume))
        return true;
    /* A 140 KiB image in DOS 3.3 order keeps block 2 in sectors 11 / 10. */
    if (volume->image_size != PD_DOS_IMAGE_SIZE) return false;
    volume->dos_order = true;
    volume->total_blocks = PD_DOS_BLOCKS;
    return pd_read_block(volume, PD_VOLUME_KEY, key_block) &&
           pd_check_key_block(key_block, volume) &&
           volume->total_blocks == PD_DOS_BLOCKS;
}

static int64_t pd_volume_extent(const pd_volume *volume) {
    int64_t declared = (int64_t)volume->total_blocks * PD_BLOCK;
    if (volume->dos_order) return PD_DOS_IMAGE_SIZE;
    return declared < volume->image_size ? declared : volume->image_size;
}

/* ---------------------------------------------------------------------- */
/* Member list                                                              */

static void pd_list_free(void *opaque) {
    pd_list *list = (pd_list *)opaque;
    size_t index;
    if (!list) return;
    for (index = 0U; index < list->count; ++index)
        if (list->items[index].name) xx_mem_free(list->items[index].name);
    if (list->items) xx_mem_free(list->items);
    if (list->slots) xx_mem_free(list->slots);
    xx_mem_free(list);
}

static uint32_t pd_hash(const char *name) {
    uint32_t hash = 2166136261U;
    for (; *name; ++name) {
        hash ^= (uint32_t)(uint8_t)pd_upper(*name);
        hash *= 16777619U;
    }
    return hash;
}

static bool pd_same_name(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b)
        if (pd_upper(*a) != pd_upper(*b)) return false;
    return *a == *b;
}

static bool pd_name_taken(const pd_list *list, const char *name) {
    size_t mask, at;
    if (!list->slots) return false;
    mask = list->slot_count - 1U;
    for (at = pd_hash(name) & mask; list->slots[at] != 0U;
         at = (at + 1U) & mask)
        if (pd_same_name(list->items[list->slots[at] - 1U].name, name))
            return true;
    return false;
}

static void pd_slot_insert(pd_list *list, size_t member) {
    size_t mask = list->slot_count - 1U;
    size_t at = pd_hash(list->items[member].name) & mask;
    while (list->slots[at] != 0U) at = (at + 1U) & mask;
    list->slots[at] = (uint32_t)(member + 1U);
}

static bool pd_reserve(pd_list *list) {
    if (list->count >= PD_MAX_MEMBERS) return false;
    if (list->count == list->capacity) {
        size_t grown = list->capacity ? list->capacity * 2U : 64U;
        pd_member *items;
        if (grown > PD_MAX_MEMBERS) grown = PD_MAX_MEMBERS;
        items = (pd_member *)xx_mem_realloc(list->items,
                                            grown * sizeof(*items));
        if (!items) return false;
        list->items = items;
        list->capacity = grown;
    }
    /* Keep the name set at most half full. */
    if ((list->count + 1U) * 2U > list->slot_count) {
        size_t slots = list->slot_count ? list->slot_count * 2U : 128U, index;
        uint32_t *table = (uint32_t *)xx_mem_calloc(slots, sizeof(*table));
        if (!table) return false;
        if (list->slots) xx_mem_free(list->slots);
        list->slots = table;
        list->slot_count = slots;
        for (index = 0U; index < list->count; ++index)
            pd_slot_insert(list, index);
    }
    return true;
}

/* Join @p parent (NULL for the root), @p component and @p extension, then
 * make the path unique among the members so far, ignoring case: a clash
 * gets "~2", "~3", ... appended.  A folder's final name is the prefix of
 * everything below it, so renaming never separates a child from its
 * folder. */
static char *pd_unique_path(const pd_list *list, const char *parent,
                            const char *component, const char *extension) {
    char suffix[16];
    char *base, *candidate;
    uint32_t number;
    base = parent ? xx_str_concat3(parent, "/", component)
                  : xx_str_concat(component, "");
    if (!base) return NULL;
    if (extension) {
        char *joined = xx_str_concat(base, extension);
        xx_str_free(base);
        if (!joined) return NULL;
        base = joined;
    }
    if (!pd_name_taken(list, base)) return base;
    for (number = 2U; number < PD_MAX_SUFFIX; ++number) {
        (void)xx_rt_snprintf(suffix, sizeof(suffix), "~%u", (unsigned)number);
        candidate = xx_str_concat(base, suffix);
        if (!candidate) break;
        if (!pd_name_taken(list, candidate)) {
            xx_str_free(base);
            return candidate;
        }
        xx_str_free(candidate);
    }
    xx_str_free(base);
    return NULL;
}

static bool pd_append(pd_list *list, pd_member *member, const char *parent,
                      const char *component, const char *extension) {
    if (!pd_reserve(list)) return false;
    member->name = pd_unique_path(list, parent, component, extension);
    if (!member->name) return false;
    list->items[list->count] = *member;
    pd_slot_insert(list, list->count);
    ++list->count;
    return true;
}

static bool pd_fork_storage(uint32_t storage) {
    return storage == PD_ST_SEEDLING || storage == PD_ST_SAPLING ||
           storage == PD_ST_TREE;
}

/* A subdirectory key block: previous pointer 0 and a 0xE header whose
 * entry geometry fits one block. */
static bool pd_subdir_geometry(const uint8_t *block, uint32_t *entry_length,
                               uint32_t *entries_per_block) {
    const uint8_t *header = block + 4U;
    uint32_t length = header[PD_H_ENTRY_LENGTH];
    uint32_t count = header[PD_H_ENTRIES];
    if (pd_le16(block) != 0U || (header[0] >> 4U) != PD_ST_SUBDIR_HEADER ||
        length < PD_ENTRY_LENGTH || count == 0U ||
        4U + length * count > PD_BLOCK)
        return false;
    *entry_length = length;
    *entries_per_block = count;
    return true;
}

static void pd_mark(uint8_t *visited, uint32_t block) {
    visited[block >> 3U] = (uint8_t)(visited[block >> 3U] | (1U << (block & 7U)));
}

static bool pd_seen(const uint8_t *visited, uint32_t block) {
    return (visited[block >> 3U] & (1U << (block & 7U))) != 0U;
}

/* One file entry: its data fork, and for an extended file the resource
 * fork as "<name>.rsrc" when it holds anything. */
static bool pd_add_file(pd_list *list, const uint8_t *entry,
                        int64_t header_offset, const char *parent,
                        const char *component) {
    const pd_volume *volume = &list->volume;
    pd_member member;
    uint32_t storage = entry[0] >> 4U;
    xx_mem_zero(&member, sizeof(member));
    member.header_offset = header_offset;
    member.data_offset = -1;
    member.file_type = entry[PD_E_TYPE];
    member.access = entry[PD_E_ACCESS];
    member.aux_type = (uint16_t)pd_le16(entry + PD_E_AUX);
    member.modified = pd_time(pd_le16(entry + PD_E_MOD_DATE),
                              pd_le16(entry + PD_E_MOD_TIME));
    if (storage != PD_ST_EXTENDED) {
        member.storage = (uint8_t)storage;
        member.key = pd_le16(entry + PD_E_KEY);
        member.blocks_used = pd_le16(entry + PD_E_BLOCKS);
        member.eof = pd_le24(entry + PD_E_EOF);
        if (member.key != 0U && member.key < volume->total_blocks)
            member.data_offset =
                volume->base + pd_block_offset(volume, member.key, 0U);
        return pd_append(list, &member, parent, component, NULL);
    }
    {
        uint8_t extended[PD_BLOCK];
        uint32_t key = pd_le16(entry + PD_E_KEY);
        pd_member resource;
        if (key == 0U || !pd_read_block(volume, key, extended)) {
            /* The forks cannot be found: list the file, fail its unpack. */
            member.damaged = true;
            list->damaged = true;
            return pd_append(list, &member, parent, component, NULL);
        }
        member.storage = extended[0];
        member.key = pd_le16(extended + 1U);
        member.blocks_used = pd_le16(extended + 3U);
        member.eof = pd_le24(extended + 5U);
        member.damaged = !pd_fork_storage(member.storage);
        if (member.key != 0U && member.key < volume->total_blocks)
            member.data_offset =
                volume->base + pd_block_offset(volume, member.key, 0U);
        resource = member;
        resource.resource = true;
        resource.storage = extended[PD_HALF];
        resource.key = pd_le16(extended + PD_HALF + 1U);
        resource.blocks_used = pd_le16(extended + PD_HALF + 3U);
        resource.eof = pd_le24(extended + PD_HALF + 5U);
        resource.damaged = !pd_fork_storage(resource.storage);
        resource.data_offset =
            (resource.key != 0U && resource.key < volume->total_blocks)
                ? volume->base + pd_block_offset(volume, resource.key, 0U)
                : -1;
        if (member.damaged || resource.damaged) list->damaged = true;
        if (!pd_append(list, &member, parent, component, NULL)) return false;
        if (resource.eof == 0U) return true;
        return pd_append(list, &resource, parent, component, ".rsrc");
    }
}

/* Walk the volume.  Returns false only when memory runs out; damage
 * (unreadable blocks, loops, bad headers) stops the affected directory and
 * sets list->damaged.  The member cap stops the walk the same way. */
static bool pd_walk(pd_list *list, const uint8_t *key_block) {
    pd_volume *volume = &list->volume;
    pd_frame *frames;
    uint8_t *visited;
    uint32_t depth = 1U;
    bool result = false;
    frames = (pd_frame *)xx_mem_alloc((PD_MAX_DEPTH + 1U) * sizeof(*frames));
    visited = (uint8_t *)xx_mem_calloc((PD_MAX_BLOCKS + 8U) / 8U, 1U);
    if (!frames || !visited) goto done;
    xx_rt_memcpy(frames[0].data, key_block, PD_BLOCK);
    frames[0].block = PD_VOLUME_KEY;
    frames[0].slot = 1U;
    frames[0].entry_length = PD_ENTRY_LENGTH;
    frames[0].entries_per_block = PD_ENTRIES_PER_BLOCK;
    frames[0].folder = SIZE_MAX;
    pd_mark(visited, PD_VOLUME_KEY);
    while (depth != 0U) {
        pd_frame *frame = &frames[depth - 1U];
        const uint8_t *entry;
        const char *parent;
        char component[PD_COMPONENT_MAX];
        uint32_t storage;
        int64_t header_offset;
        if (frame->slot >= frame->entries_per_block) {
            uint32_t next = pd_le16(frame->data + 2U);
            if (next == 0U) {
                --depth;
                continue;
            }
            if (next >= volume->total_blocks || pd_seen(visited, next) ||
                !pd_read_block(volume, next, frame->data)) {
                list->damaged = true;
                --depth;
                continue;
            }
            pd_mark(visited, next);
            frame->block = next;
            frame->slot = 0U;
            continue;
        }
        header_offset = volume->base +
                        pd_block_offset(volume, frame->block,
                                        4U + frame->slot * frame->entry_length);
        entry = frame->data + 4U + frame->slot * frame->entry_length;
        ++frame->slot;
        storage = entry[0] >> 4U;
        if (storage == PD_ST_DELETED) continue;
        if (list->count >= PD_MAX_MEMBERS) {
            list->damaged = true;
            break;
        }
        parent = frame->folder == SIZE_MAX ? NULL
                                           : list->items[frame->folder].name;
        pd_component(entry + 1U, entry[0] & 0x0FU, pd_le16(entry + PD_E_CASE),
                     component);
        if (storage == PD_ST_SUBDIR) {
            pd_member member;
            uint32_t key = pd_le16(entry + PD_E_KEY), length, count;
            size_t folder = list->count;
            xx_mem_zero(&member, sizeof(member));
            member.folder = true;
            member.storage = (uint8_t)storage;
            member.header_offset = header_offset;
            member.data_offset = -1;
            member.key = key;
            member.file_type = entry[PD_E_TYPE];
            member.access = entry[PD_E_ACCESS];
            member.modified = pd_time(pd_le16(entry + PD_E_MOD_DATE),
                                      pd_le16(entry + PD_E_MOD_TIME));
            if (!pd_append(list, &member, parent, component, NULL)) {
                if (list->count >= PD_MAX_MEMBERS) {
                    list->damaged = true;
                    break;
                }
                goto done;
            }
            if (depth > PD_MAX_DEPTH || key < 3U ||
                key >= volume->total_blocks || pd_seen(visited, key) ||
                !pd_read_block(volume, key, frames[depth].data) ||
                !pd_subdir_geometry(frames[depth].data, &length, &count)) {
                list->damaged = true;
                continue;
            }
            pd_mark(visited, key);
            frames[depth].block = key;
            frames[depth].slot = 1U;
            frames[depth].entry_length = length;
            frames[depth].entries_per_block = count;
            frames[depth].folder = folder;
            ++depth;
            continue;
        }
        if (pd_fork_storage(storage) || storage == PD_ST_EXTENDED) {
            if (!pd_add_file(list, entry, header_offset, parent, component)) {
                if (list->count >= PD_MAX_MEMBERS) {
                    list->damaged = true;
                    break;
                }
                goto done;
            }
            continue;
        }
        /* Pascal areas (4) and anything else are not files ProDOS reads. */
        list->damaged = true;
    }
    result = true;
done:
    if (frames) xx_mem_free(frames);
    if (visited) xx_mem_free(visited);
    return result;
}

static bool pd_parse(Abstractformat *format, pd_list **result) {
    uint8_t key_block[PD_BLOCK];
    pd_list *list;
    if (!result) return false;
    list = (pd_list *)xx_mem_calloc(1U, sizeof(*list));
    if (!list) return false;
    if (!pd_open_volume(format, &list->volume, key_block) ||
        !pd_walk(list, key_block)) {
        pd_list_free(list);
        return false;
    }
    *result = list;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Fork data                                                                */

static bool pd_write_all(xx_io_device *destination, const uint8_t *data,
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

/* Stream one fork (seedling / sapling / tree) of @p eof bytes to
 * @p destination, or only read it through when @p destination is NULL. */
static bool pd_copy_fork(const pd_volume *volume, const pd_member *member,
                         xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *work;
    uint8_t *data, *index, *master;
    uint32_t blocks, logical, loaded = UINT32_MAX;
    bool master_loaded = false, result = false;
    if (member->damaged || !pd_fork_storage(member->storage) ||
        member->eof > 0xFFFFFFU)
        return false;
    if (member->eof == 0U) return true;
    if (member->key == 0U || member->key >= volume->total_blocks) return false;
    work = (uint8_t *)xx_mem_alloc(3U * PD_BLOCK);
    if (!work) return false;
    data = work;
    index = work + PD_BLOCK;
    master = work + 2U * PD_BLOCK;
    blocks = (member->eof + PD_BLOCK - 1U) / PD_BLOCK;
    for (logical = 0U; logical < blocks; ++logical) {
        uint32_t physical = 0U;
        size_t chunk = logical + 1U == blocks
                           ? (size_t)(member->eof - logical * PD_BLOCK)
                           : PD_BLOCK;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (member->storage == PD_ST_SEEDLING) {
            physical = logical == 0U ? member->key : 0U;
        } else if (member->storage == PD_ST_SAPLING) {
            if (logical < 256U) {
                if (loaded != member->key) {
                    if (!pd_read_block(volume, member->key, index)) goto done;
                    loaded = member->key;
                }
                physical = (uint32_t)index[logical] |
                           ((uint32_t)index[256U + logical] << 8U);
            }
        } else {
            uint32_t slot = logical / 256U, inner = logical % 256U, block;
            if (!master_loaded) {
                if (!pd_read_block(volume, member->key, master)) goto done;
                master_loaded = true;
            }
            block = (uint32_t)master[slot] |
                    ((uint32_t)master[256U + slot] << 8U);
            if (block != 0U) {
                if (block >= volume->total_blocks) goto done;
                if (loaded != block) {
                    if (!pd_read_block(volume, block, index)) goto done;
                    loaded = block;
                }
                physical = (uint32_t)index[inner] |
                           ((uint32_t)index[256U + inner] << 8U);
            }
        }
        if (physical == 0U) {
            xx_mem_zero(data, PD_BLOCK);
        } else if (physical >= volume->total_blocks ||
                   !pd_read_block(volume, physical, data)) {
            goto done;
        }
        if (destination && !pd_write_all(destination, data, chunk)) goto done;
    }
    result = true;
done:
    xx_mem_free(work);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                  */

static bool pd_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *pd_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool pd_set_record(xx_archive_record *record, const pd_member *member) {
    uint64_t stored = member->folder ? 0U
                                     : (uint64_t)member->blocks_used * PD_BLOCK;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = PD_ENTRY_LENGTH;
    record->data_offset = member->folder ? -1 : member->data_offset;
    record->compressed_size = (int64_t)stored;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          stored) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->folder ? 0U : member->eof) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->folder ? 0U : member->storage) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->modified) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->access) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->file_type) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_EXTERNAL_ATTRS,
                                          member->aux_type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

/* Defence in depth: the parser only builds names from [A-Za-z0-9._~/] with
 * no empty, "." or ".." segment and no device stem; check that again right
 * before a path reaches the host filesystem. */
static bool pd_safe_output_name(const char *name) {
    const char *segment, *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    segment = name;
    for (at = name;; ++at) {
        char c = *at;
        if (c == '/' || c == 0) {
            size_t length = (size_t)(at - segment);
            char part[PD_COMPONENT_MAX + 16U];
            if (length == 0U || length >= sizeof(part) ||
                segment[0] == '.' || segment[length - 1U] == '.')
                return false;
            xx_rt_memcpy(part, segment, length);
            part[length] = 0;
            if (pd_device_name(part)) return false;
            if (c == 0) return true;
            segment = at + 1;
            continue;
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '~'))
            return false;
    }
}

void xx_prodos_init(xx_prodos *volume, xx_io_device *device,
                    int64_t base_address) {
    if (!volume) return;
    xx_mem_zero(volume, sizeof(*volume));
    xx_format_init(&volume->format, device, base_address);
    volume->format.endian = XX_ENDIAN_LITTLE;
    volume->format.file_type = XX_PRODOS_FILE_TYPE;
    volume->format.format_type = XX_TYPE_ARCHIVE;
    volume->format.is_archive = true;
    xx_format_set_mime_type(&volume->format, "application/x-prodos-image");
    xx_format_set_extension(&volume->format, "po");
    volume->format.check_is_valid = xx_prodos_check_is_valid;
    volume->format.handle_base_info = xx_prodos_handle_base_info;
    volume->format.get_format_size = xx_prodos_get_format_size;
    volume->format.get_number_of_archive_records =
        xx_prodos_get_number_of_archive_records;
    volume->format.create_archive_records_reading =
        xx_prodos_create_archive_records_reading;
    volume->format.get_current_archive_record =
        xx_prodos_get_current_archive_record;
    volume->format.unpack_current_archive_record =
        xx_prodos_unpack_current_archive_record;
    volume->format.archive_record_move_to_next =
        xx_prodos_archive_record_move_to_next;
    volume->format.free_archive_records_reading =
        xx_prodos_free_archive_records_reading;
}

xx_prodos *xx_prodos_create(xx_io_device *device, int64_t base_address) {
    xx_prodos *volume = (xx_prodos *)xx_mem_alloc(sizeof(*volume));
    if (volume) xx_prodos_init(volume, device, base_address);
    return volume;
}

void xx_prodos_destroy(xx_prodos *volume) {
    if (volume) xx_format_cleanup_extra_parameters(&volume->format);
}

void xx_prodos_free(xx_prodos *volume) {
    if (!volume) return;
    xx_prodos_destroy(volume);
    xx_mem_free(volume);
}

/* The probe: one 512-byte read (two for a 140 KiB image that is not in
 * ProDOS order), no allocation. */
bool xx_prodos_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    pd_volume volume;
    uint8_t key_block[PD_BLOCK];
    (void)pd;
    return pd_open_volume(format, &volume, key_block);
}

bool xx_prodos_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    pd_list *list;
    xx_prodos *volume;
    int64_t total, extent, end;
    (void)pd;
    if (!format || !pd_parse(format, &list)) return false;
    volume = (xx_prodos *)format;
    total = xx_io_total_size(format->device);
    extent = pd_volume_extent(&list->volume);
    end = format->base_address + extent;
    volume->number_of_records = list->count;
    volume->total_blocks = list->volume.total_blocks;
    volume->file_count = list->volume.file_count;
    volume->dos_order = list->volume.dos_order;
    volume->truncated = !list->volume.dos_order &&
                        (int64_t)list->volume.total_blocks * PD_BLOCK >
                            list->volume.image_size;
    volume->damaged = list->damaged;
    xx_rt_memcpy(volume->volume_name, list->volume.name,
                 sizeof(volume->volume_name) - 1U);
    volume->volume_name[sizeof(volume->volume_name) - 1U] = 0;
    format->number_of_archive_records = list->count;
    format->format_size = extent;
    format->overlay_offset = end < total ? end : -1;
    format->overlay_size = end < total ? total - end : 0;
    format->is_valid = true;
    format->base_info_handled = true;
    pd_list_free(list);
    return true;
}

int64_t xx_prodos_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_prodos_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_prodos_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_prodos_handle_base_info(format, pd))
               ? ((xx_prodos *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_prodos_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pd_list *list;
    xx_archive_record_state *state;
    (void)pd;
    if (!pd_parse(format, &list)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pd_list_free(list);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = list;
    state->free_internal = pd_list_free;
    state->total_records = list->count;
    if (!pd_copy_options(&state->options, options) ||
        (list->count != 0U &&
         !pd_set_record(&state->current_record, &list->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = list->count != 0U;
    return state;
}

const xx_archive_record *xx_prodos_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_prodos_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    pd_list *list;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(list = (pd_list *)state->internal_state) ||
        list->index + 1U >= list->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++list->index;
    ++state->current_index;
    state->has_record = pd_set_record(&state->current_record,
                                      &list->items[list->index]);
    return state->has_record;
}

bool xx_prodos_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    pd_list *list;
    const pd_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(list = (pd_list *)state->internal_state) ||
        list->index >= list->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &list->items[list->index];
    path_option = pd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return member->folder ? true
                              : pd_copy_fork(&list->volume, member, NULL, pd);
    if (!pd_safe_output_name(member->name)) return false;
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
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = pd_copy_fork(&list->volume, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_prodos_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
