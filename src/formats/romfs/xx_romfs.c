/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/romfs/xx_romfs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_ROMFS exists in the enum, and
 * keep the value in step with the enumerator chosen there. */

#define XX_ROMFS_ALIGNMENT 16
#define XX_ROMFS_SUPERBLOCK_SIZE 16
#define XX_ROMFS_FILE_HEADER_SIZE 16
#define XX_ROMFS_MAX_ENTRIES 100000U
#define XX_ROMFS_MAX_NODES 200000U
#define XX_ROMFS_MAX_DEPTH 64U
#define XX_ROMFS_MAX_NAME_SIZE 4096U

/* Low three bits of the "next header" word. */
#define XX_ROMFS_TYPE_HARDLINK 0U
#define XX_ROMFS_TYPE_DIRECTORY 1U
#define XX_ROMFS_TYPE_REGULAR 2U

typedef struct xx_romfs_entry_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    uint32_t data_size;
    bool is_folder;
} xx_romfs_entry;

/* Open-addressing set of already-visited file-header offsets. A romfs image
 * describes its tree with raw offsets, so both the next-header chain and a
 * directory's first-entry pointer can be made to loop. A linear scan would
 * be quadratic in the node cap, so the set is a power-of-two hash table
 * holding offset + 1 (slot value 0 means empty). */
typedef struct xx_romfs_visited_s {
    int64_t *slots;
    size_t capacity;
    size_t count;
} xx_romfs_visited;

typedef struct xx_romfs_private_s {
    xx_romfs_entry *entries;
    size_t count;
    size_t capacity;
    xx_romfs_visited visited;
    size_t nodes;  /**< Headers examined, capped by XX_ROMFS_MAX_NODES. */
    int64_t input_size;
    int64_t archive_end;
    uint32_t volume_size;
    uint32_t checksum;
} xx_romfs_private;

typedef struct xx_romfs_archive_stream_s {
    xx_romfs_private parsed;
    size_t index;
} xx_romfs_archive_stream;

static void xx_romfs_vtable_destroy(Abstractformat *self);

static bool xx_romfs_read_at(xx_io_device *device, int64_t offset, void *data,
                             size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 || offset > LONG_MAX ||
        xx_io_seek(device, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_romfs_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_romfs_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Round up to the next 16-byte boundary, refusing to overflow. */
static bool xx_romfs_align(int64_t value, int64_t *result) {
    if (!result || value < 0 ||
        value > INT64_MAX - (XX_ROMFS_ALIGNMENT - 1)) {
        return false;
    }
    *result = (value + (XX_ROMFS_ALIGNMENT - 1)) &
              ~(int64_t)(XX_ROMFS_ALIGNMENT - 1);
    return true;
}

static void xx_romfs_visited_cleanup(xx_romfs_visited *visited) {
    if (!visited) return;
    if (visited->slots) xx_mem_free(visited->slots);
    xx_mem_zero(visited, sizeof(*visited));
}

static size_t xx_romfs_visited_slot(const xx_romfs_visited *visited,
                                    int64_t offset) {
    /* The offsets are 16-byte aligned, so the low bits carry no entropy;
     * fold the rest of the value down with a 64-bit odd multiplier. */
    uint64_t key = (uint64_t)offset >> 4U;
    key = (key ^ (key >> 29U)) * UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 32U;
    return (size_t)key & (visited->capacity - 1U);
}

static bool xx_romfs_visited_grow(xx_romfs_visited *visited) {
    int64_t *slots;
    size_t capacity = visited->capacity ? visited->capacity * 2U : 256U;
    size_t index;
    xx_romfs_visited grown;
    if (capacity < visited->capacity ||
        capacity > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = (int64_t *)xx_mem_calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    grown.slots = slots;
    grown.capacity = capacity;
    grown.count = visited->count;
    for (index = 0U; index < visited->capacity; ++index) {
        int64_t stored = visited->slots[index];
        size_t slot;
        if (stored == 0) continue;
        slot = xx_romfs_visited_slot(&grown, stored - 1);
        while (slots[slot] != 0) slot = (slot + 1U) & (capacity - 1U);
        slots[slot] = stored;
    }
    if (visited->slots) xx_mem_free(visited->slots);
    *visited = grown;
    return true;
}

/* Record offset and report whether it had already been seen. Allocation
 * failure is reported as "seen" so the traversal stops rather than looping
 * with a set that can no longer remember anything. */
static bool xx_romfs_visited_mark(xx_romfs_visited *visited, int64_t offset) {
    size_t slot;
    if (!visited || offset < 0) return true;
    if ((visited->count + 1U) * 4U >= visited->capacity * 3U) {
        if (!xx_romfs_visited_grow(visited)) return true;
    }
    slot = xx_romfs_visited_slot(visited, offset);
    while (visited->slots[slot] != 0) {
        if (visited->slots[slot] == offset + 1) return true;
        slot = (slot + 1U) & (visited->capacity - 1U);
    }
    visited->slots[slot] = offset + 1;
    ++visited->count;
    return false;
}

static void xx_romfs_private_cleanup(xx_romfs_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_romfs_visited_cleanup(&parsed->visited);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_romfs_append_entry(xx_romfs_private *parsed,
                                  xx_romfs_entry *entry) {
    xx_romfs_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_ROMFS_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_romfs_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for, so the reserved Windows punctuation is
 * rejected here even though a romfs image may legally carry it. */
static bool xx_romfs_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) return false;
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' || component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* Parse-time check, deliberately looser than xx_romfs_safe_name: a romfs
 * entry name is a single path component, so only control bytes and an
 * embedded separator make it implausible. */
static bool xx_romfs_plausible_name(const char *name, size_t length) {
    size_t index;
    if (!name || length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == '/' || ch == '\\') return false;
    }
    return true;
}

static bool xx_romfs_is_dot_name(const char *name) {
    if (!name || name[0] != '.') return false;
    return name[1] == '\0' || (name[1] == '.' && name[2] == '\0');
}

/* Read the NUL-terminated, 16-byte padded name at offset. On success the
 * caller owns *out_name and *out_end points one byte past the terminator.
 * An empty name is returned as an empty string; only the superblock's
 * volume name is allowed to be empty. */
static bool xx_romfs_read_name(xx_io_device *device, int64_t offset,
                               int64_t total_size, char **out_name,
                               int64_t *out_end) {
    char buffer[XX_ROMFS_MAX_NAME_SIZE];
    size_t used = 0U;
    char *name;
    if (!device || !out_name || !out_end) return false;
    *out_name = NULL;
    *out_end = -1;
    while (used < sizeof(buffer)) {
        uint8_t chunk[XX_ROMFS_ALIGNMENT];
        size_t index;
        size_t available = sizeof(buffer) - used;
        if (!xx_romfs_range_within(total_size, offset, (int64_t)sizeof(chunk)) ||
            !xx_romfs_read_at(device, offset, chunk, sizeof(chunk))) {
            return false;
        }
        for (index = 0U; index < sizeof(chunk); ++index) {
            if (chunk[index] == 0U) break;
        }
        if (index > available) return false;
        if (index != 0U) xx_mem_copy(buffer + used, chunk, index);
        used += index;
        if (index < sizeof(chunk)) {
            /* The terminator was inside this chunk. */
            if (!xx_romfs_add(offset, (uint64_t)index + 1U, out_end)) {
                return false;
            }
            name = (char *)xx_mem_alloc(used + 1U);
            if (!name) return false;
            if (used != 0U) xx_mem_copy(name, buffer, used);
            name[used] = '\0';
            *out_name = name;
            return true;
        }
        if (!xx_romfs_add(offset, (uint64_t)sizeof(chunk), &offset)) {
            return false;
        }
    }
    return false;
}

static char *xx_romfs_join_name(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_ROMFS_MAX_NAME_SIZE ||
        name_size > XX_ROMFS_MAX_NAME_SIZE - prefix_size -
                        (prefix_size != 0U ? 1U : 0U)) {
        return NULL;
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size +
                                    (prefix_size != 0U ? 2U : 1U));
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_mem_copy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_mem_copy(combined + prefix_size + 1U, name, name_size);
        combined[prefix_size + 1U + name_size] = '\0';
    } else {
        xx_mem_copy(combined, name, name_size);
        combined[name_size] = '\0';
    }
    return combined;
}

/* Walk one next-header chain, recursing into the directories it names.
 * A malformed entry ends the whole parse; a cycle, an exhausted node budget
 * or an exhausted depth budget only ends the chain, so that the records
 * gathered before the anomaly remain usable. */
static bool xx_romfs_walk(Abstractformat *self, xx_romfs_private *parsed,
                          int64_t offset, const char *prefix, unsigned depth,
                          xx_pd_struct *pd) {
    if (!self || !parsed) return false;
    if (depth > XX_ROMFS_MAX_DEPTH) return true;
    while (offset > 0) {
        uint8_t header[XX_ROMFS_FILE_HEADER_SIZE];
        uint32_t raw_next;
        uint32_t spec;
        uint32_t size;
        uint32_t type;
        int64_t name_end;
        int64_t data_offset;
        int64_t next = 0;
        int64_t child = 0;
        char *name = NULL;
        char *full_name = NULL;
        xx_romfs_entry entry;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (parsed->count >= XX_ROMFS_MAX_ENTRIES) return true;
        if (parsed->nodes >= XX_ROMFS_MAX_NODES) return true;
        if (xx_romfs_visited_mark(&parsed->visited, offset)) return true;
        ++parsed->nodes;
        if (!xx_romfs_range_within(parsed->input_size, offset,
                                   XX_ROMFS_FILE_HEADER_SIZE) ||
            !xx_romfs_read_at(self->device, offset, header, sizeof(header))) {
            return false;
        }
        raw_next = xx_data_get_u32(header, sizeof(header), 0U, true);
        spec = xx_data_get_u32(header, sizeof(header), 4U, true);
        size = xx_data_get_u32(header, sizeof(header), 8U, true);
        type = raw_next & 7U;
        if (!xx_romfs_read_name(self->device, offset + XX_ROMFS_FILE_HEADER_SIZE,
                                parsed->input_size, &name, &name_end)) {
            return false;
        }
        if (!xx_romfs_plausible_name(name, xx_str_len(name)) ||
            !xx_romfs_align(name_end, &data_offset)) {
            xx_str_free(name);
            return false;
        }
        if ((raw_next & ~UINT32_C(15)) != 0U &&
            !xx_romfs_add(self->base_address, raw_next & ~UINT32_C(15), &next)) {
            xx_str_free(name);
            return false;
        }
        if ((spec & ~UINT32_C(15)) != 0U &&
            !xx_romfs_add(self->base_address, spec & ~UINT32_C(15), &child)) {
            xx_str_free(name);
            return false;
        }
        /* "." and ".." point back up the tree; following them would loop,
         * and they are not members in their own right. */
        if (xx_romfs_is_dot_name(name)) {
            xx_str_free(name);
            offset = next;
            continue;
        }
        full_name = xx_romfs_join_name(prefix, name);
        xx_str_free(name);
        if (!full_name) return false;
        if (type == XX_ROMFS_TYPE_REGULAR) {
            if (!xx_romfs_range_within(parsed->input_size, data_offset, size)) {
                xx_str_free(full_name);
                return false;
            }
            xx_mem_zero(&entry, sizeof(entry));
            entry.name = full_name;
            entry.header_offset = offset;
            entry.header_size = data_offset - offset;
            entry.data_offset = data_offset;
            entry.data_size = size;
            entry.is_folder = false;
            if (!xx_romfs_append_entry(parsed, &entry)) {
                xx_str_free(full_name);
                return false;
            }
        } else if (type == XX_ROMFS_TYPE_DIRECTORY) {
            xx_mem_zero(&entry, sizeof(entry));
            entry.name = full_name;
            entry.header_offset = offset;
            entry.header_size = data_offset - offset;
            entry.data_offset = data_offset;
            entry.data_size = 0U;
            entry.is_folder = true;
            if (!xx_romfs_append_entry(parsed, &entry)) {
                xx_str_free(full_name);
                return false;
            }
            /* The entry owns full_name once it has been appended, so the
             * local pointer is only borrowed for the recursion below and is
             * never freed here - xx_romfs_private_cleanup() releases it. */
            if (child > 0 && !xx_romfs_walk(self, parsed, child, full_name,
                                            depth + 1U, pd)) {
                return false;
            }
            full_name = NULL;
        } else {
            /* Hard links, symlinks, devices, sockets and fifos carry no
             * extractable payload here and are skipped. */
            xx_str_free(full_name);
            full_name = NULL;
        }
        offset = next;
    }
    return true;
}

static bool xx_romfs_parse(Abstractformat *self, xx_romfs_private *parsed,
                           xx_pd_struct *pd) {
    uint8_t superblock[XX_ROMFS_SUPERBLOCK_SIZE];
    int64_t total_size;
    int64_t volume_name_end;
    int64_t root_offset;
    char *volume_name = NULL;
    uint32_t volume_size;
    /* Initialise before the guard clause: callers such as
     * xx_romfs_check_is_valid() run xx_romfs_private_cleanup() on their stack
     * copy whatever this returns, and cleaning up an uninitialised one would
     * free indeterminate pointers. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    if (!xx_romfs_range_within(total_size, self->base_address,
                               XX_ROMFS_SUPERBLOCK_SIZE) ||
        !xx_romfs_read_at(self->device, self->base_address, superblock,
                          sizeof(superblock)) ||
        xx_rt_memcmp(superblock, "-rom1fs-", 8U) != 0) {
        goto fail;
    }
    parsed->input_size = total_size;
    volume_size = xx_data_get_u32(superblock, sizeof(superblock), 8U, true);
    parsed->volume_size = volume_size;
    parsed->checksum = xx_data_get_u32(superblock, sizeof(superblock), 12U, true);
    /* The full-size field counts the accessible bytes of the image. A value
     * that runs past the device is malformed; trailing padding beyond it is
     * reported as overlay. */
    if (volume_size < XX_ROMFS_SUPERBLOCK_SIZE ||
        !xx_romfs_add(self->base_address, volume_size, &parsed->archive_end) ||
        parsed->archive_end > total_size) {
        goto fail;
    }
    /* Skip the volume name to reach the root directory's first entry. */
    if (!xx_romfs_read_name(self->device,
                            self->base_address + XX_ROMFS_SUPERBLOCK_SIZE,
                            total_size, &volume_name, &volume_name_end)) {
        goto fail;
    }
    xx_str_free(volume_name);
    volume_name = NULL;
    if (!xx_romfs_align(volume_name_end, &root_offset) ||
        !xx_romfs_range_within(total_size, root_offset,
                               XX_ROMFS_FILE_HEADER_SIZE) ||
        !xx_romfs_walk(self, parsed, root_offset, "", 0U, pd) ||
        parsed->count == 0U) {
        goto fail;
    }
    return true;
fail:
    if (volume_name) xx_str_free(volume_name);
    xx_romfs_private_cleanup(parsed);
    return false;
}

static bool xx_romfs_copy_options(xx_list_s *destination,
                                  const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
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

static const xx_var *xx_romfs_find_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_romfs_populate_record(xx_archive_record *record,
                                     const xx_romfs_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          entry->data_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_romfs_archive_stream_free(void *pointer) {
    xx_romfs_archive_stream *stream = (xx_romfs_archive_stream *)pointer;
    if (!stream) return;
    xx_romfs_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

void xx_romfs_init(xx_romfs *romfs, xx_io_device *dev, int64_t base_address) {
    if (!romfs) return;
    xx_mem_zero(romfs, sizeof(*romfs));
    xx_format_init(&romfs->format, dev, base_address);
    romfs->format.endian = XX_ENDIAN_BIG;
    romfs->format.file_type = XX_FILE_TYPE_ROMFS;
    romfs->format.format_type = XX_TYPE_ARCHIVE;
    romfs->format.is_archive = true;
    xx_format_set_mime_type(&romfs->format, "application/x-romfs");
    xx_format_set_extension(&romfs->format, "romfs");
    romfs->format.check_is_valid = xx_romfs_check_is_valid;
    romfs->format.handle_base_info = xx_romfs_handle_base_info;
    romfs->format.get_format_size = xx_romfs_get_format_size;
    romfs->format.get_number_of_archive_records =
        xx_romfs_get_number_of_archive_records;
    romfs->format.create_archive_records_reading =
        xx_romfs_create_archive_records_reading;
    romfs->format.get_current_archive_record =
        xx_romfs_get_current_archive_record;
    romfs->format.unpack_current_archive_record =
        xx_romfs_unpack_current_archive_record;
    romfs->format.archive_record_move_to_next =
        xx_romfs_archive_record_move_to_next;
    romfs->format.free_archive_records_reading =
        xx_romfs_free_archive_records_reading;
    romfs->format.destroy = xx_romfs_vtable_destroy;
    romfs->archive_end = -1;
}

xx_romfs *xx_romfs_create(xx_io_device *dev, int64_t base_address) {
    xx_romfs *romfs = (xx_romfs *)xx_mem_alloc(sizeof(*romfs));
    if (romfs) xx_romfs_init(romfs, dev, base_address);
    return romfs;
}

void xx_romfs_destroy(xx_romfs *romfs) {
    if (!romfs) return;
    if (romfs->internal) {
        xx_romfs_private_cleanup((xx_romfs_private *)romfs->internal);
        xx_mem_free(romfs->internal);
        romfs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&romfs->format);
}

static void xx_romfs_vtable_destroy(Abstractformat *self) {
    xx_romfs_destroy((xx_romfs *)self);
}

void xx_romfs_free(xx_romfs *romfs) {
    if (!romfs) return;
    xx_romfs_destroy(romfs);
    xx_mem_free(romfs);
}

bool xx_romfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_romfs_private parsed;
    bool result = xx_romfs_parse(self, &parsed, pd);
    xx_romfs_private_cleanup(&parsed);
    return result;
}

bool xx_romfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_romfs_private *parsed;
    xx_romfs *romfs = (xx_romfs *)self;
    int64_t total_size;
    if (!self || !romfs) return false;
    parsed = (xx_romfs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_romfs_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (romfs->internal) {
        xx_romfs_private_cleanup((xx_romfs_private *)romfs->internal);
        xx_mem_free(romfs->internal);
    }
    romfs->internal = parsed;
    romfs->number_of_records = parsed->count;
    romfs->number_of_members = parsed->count;
    romfs->volume_size = parsed->volume_size;
    romfs->checksum = parsed->checksum;
    romfs->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_romfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_romfs_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_romfs *)self)->number_of_records;
}

xx_archive_record_state *xx_romfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_romfs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_romfs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_romfs_copy_options(&state->options, options) ||
        !xx_romfs_parse(self, &stream->parsed, pd)) {
        xx_romfs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_romfs_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_romfs_populate_record(&state->current_record,
                                 &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_romfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_romfs_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_romfs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_romfs_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_romfs_populate_record(&state->current_record,
                                  &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_romfs_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool folder;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_romfs_safe_name(name)) return false;
    option = xx_romfs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat(base, "/");
        if (!destination) goto cleanup;
        {
            char *joined = xx_str_concat(destination, name);
            xx_str_free(destination);
            destination = joined;
        }
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    folder = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (folder) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                record->data_offset,
                                                record->compressed_size,
                                                destination, pd);
    } else {
        result = false;
    }
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

void xx_romfs_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_romfs_get_number_of_records(const xx_romfs *romfs) {
    return romfs ? romfs->number_of_records : 0U;
}
uint64_t xx_romfs_get_number_of_members(const xx_romfs *romfs) {
    return romfs ? romfs->number_of_members : 0U;
}
uint32_t xx_romfs_get_volume_size(const xx_romfs *romfs) {
    return romfs ? romfs->volume_size : 0U;
}
uint32_t xx_romfs_get_checksum(const xx_romfs *romfs) {
    return romfs ? romfs->checksum : 0U;
}
int64_t xx_romfs_get_archive_end(const xx_romfs *romfs) {
    return romfs ? romfs->archive_end : -1;
}
