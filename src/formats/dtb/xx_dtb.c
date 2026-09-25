/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dtb/xx_dtb.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_DTB exists in the enum. */
#ifdef DTB
#define XX_DTB_FILE_TYPE XX_FILE_TYPE_DTB
#else
#define XX_DTB_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_DTB_MAGIC UINT32_C(0xD00DFEED)
#define XX_DTB_HEADER_SIZE 40U

/* Tokens of the structure block. */
#define XX_DTB_TOKEN_BEGIN_NODE UINT32_C(1)
#define XX_DTB_TOKEN_END_NODE UINT32_C(2)
#define XX_DTB_TOKEN_PROP UINT32_C(3)
#define XX_DTB_TOKEN_NOP UINT32_C(4)
#define XX_DTB_TOKEN_END UINT32_C(9)

/* The oldest layout that still names its blocks in a way this walk can
 * follow, and the newest the specification defines. */
#define XX_DTB_MIN_VERSION 2U
#define XX_DTB_MAX_VERSION 17U

/* Bounds.  The two blocks that are staged in memory are capped outright; the
 * walk is capped by depth, by node count and by property count so that a
 * crafted token stream cannot run away even inside a small blob. */
#define XX_DTB_MAX_STRUCT_SIZE (64U * 1024U * 1024U)
#define XX_DTB_MAX_STRINGS_SIZE (8U * 1024U * 1024U)
#define XX_DTB_MAX_DEPTH 64U
#define XX_DTB_MAX_ENTRIES 200000U
#define XX_DTB_MAX_RESERVATIONS 4096U
#define XX_DTB_MAX_PATH_SIZE 4096U

typedef struct xx_dtb_entry_s {
    char *name;            /**< Path, no leading slash. */
    int64_t data_offset;   /**< Device offset of the property value. */
    int64_t data_size;     /**< Length of the property value. */
    bool is_folder;        /**< True for a node, false for a property. */
} xx_dtb_entry;

typedef struct xx_dtb_private_s {
    xx_dtb_entry *entries;
    size_t count;
    size_t capacity;
    uint8_t *structure;    /**< Staged structure block. */
    uint32_t structure_size;
    uint8_t *strings;      /**< Staged strings block. */
    uint32_t strings_size;
    int64_t input_size;
    int64_t struct_offset; /**< Device offset of the structure block. */
    int64_t external_base; /**< Device offset FIT data-offset counts from. */
    int64_t archive_end;
    uint64_t nodes;
    uint64_t properties;
    uint32_t total_size;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t reservations;
    bool is_fit;
} xx_dtb_private;

typedef struct xx_dtb_archive_stream_s {
    xx_dtb_private parsed;
    size_t index;
} xx_dtb_archive_stream;

static void xx_dtb_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: a device tree may be embedded far
 * into a multi-gigabyte firmware image and long is 32-bit on Win64. */
static bool xx_dtb_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_dtb_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_dtb_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Round a structure-block cursor up to the given power-of-two alignment,
 * refusing to wrap past the block. */
static bool xx_dtb_align(uint32_t value, uint32_t alignment,
                         uint32_t *result) {
    uint32_t mask = alignment - 1U;
    if (!result || value > UINT32_MAX - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

static void xx_dtb_private_cleanup(xx_dtb_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) {
            xx_str_free(parsed->entries[index].name);
        }
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->structure) xx_mem_free(parsed->structure);
    if (parsed->strings) xx_mem_free(parsed->strings);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->struct_offset = -1;
    parsed->external_base = -1;
    parsed->archive_end = -1;
}

static bool xx_dtb_append_entry(xx_dtb_private *parsed, xx_dtb_entry *entry) {
    xx_dtb_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_DTB_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 64U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) {
            return false;
        }
        grown = (xx_dtb_entry *)xx_mem_realloc(
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
 * every host this library builds for.  Device tree node names routinely
 * carry '@' and ',', which are fine; the reserved Windows punctuation and
 * any '..' component are not. */
static bool xx_dtb_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' &&
                 component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* Parse-time check.  A node or property name is one path component, so an
 * embedded separator or a control byte makes it implausible. */
static bool xx_dtb_plausible_name(const char *name, size_t length) {
    size_t index;
    if (!name || length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == '/' || ch == '\\') return false;
    }
    return true;
}

static char *xx_dtb_join_name(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_DTB_MAX_PATH_SIZE ||
        name_size > XX_DTB_MAX_PATH_SIZE - prefix_size -
                        (prefix_size != 0U ? 1U : 0U)) {
        return NULL;
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size +
                                    (prefix_size != 0U ? 2U : 1U));
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_rt_memcpy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_rt_memcpy(combined + prefix_size + 1U, name, name_size);
        combined[prefix_size + 1U + name_size] = '\0';
    } else {
        xx_rt_memcpy(combined, name, name_size);
        combined[name_size] = '\0';
    }
    return combined;
}

/* Return a borrowed pointer to the NUL-terminated name at offset in the
 * strings block, or NULL when the offset or the string runs outside it.  A
 * name offset past the strings block is the classic malformed-DTB case and
 * must be refused rather than read. */
static const char *xx_dtb_string_at(const xx_dtb_private *parsed,
                                    uint32_t offset) {
    uint32_t index;
    if (!parsed->strings || offset >= parsed->strings_size) return NULL;
    for (index = offset; index < parsed->strings_size; ++index) {
        if (parsed->strings[index] == 0U) {
            return (const char *)(parsed->strings + offset);
        }
    }
    return NULL;
}

/* Read a NUL-terminated name out of the staged structure block, reporting
 * its length and the cursor position just past the terminator. */
static bool xx_dtb_struct_string(const xx_dtb_private *parsed, uint32_t cursor,
                                 const char **out_name, size_t *out_length,
                                 uint32_t *out_end) {
    uint32_t index;
    if (cursor >= parsed->structure_size) return false;
    for (index = cursor; index < parsed->structure_size; ++index) {
        if (parsed->structure[index] == 0U) {
            *out_name = (const char *)(parsed->structure + cursor);
            *out_length = (size_t)(index - cursor);
            *out_end = index + 1U;
            return true;
        }
    }
    return false;
}

/* FIT images with external data put the payloads past the aligned end of the
 * blob and name them with data-offset / data-size u32 properties.  Both are
 * collected while the node is walked and turned into one member at
 * FDT_END_NODE, because the two properties may appear in either order. */
typedef struct xx_dtb_external_s {
    bool has_offset;
    bool has_size;
    uint32_t offset;
    uint32_t size;
} xx_dtb_external;

/* Walk one node and everything below it.  cursor points just past the node's
 * FDT_BEGIN_NODE token; on success it is left just past the matching
 * FDT_END_NODE. */
static bool xx_dtb_walk_node(xx_dtb_private *parsed, uint32_t *cursor,
                             const char *prefix, unsigned depth,
                             xx_pd_struct *pd) {
    const char *node_name;
    size_t node_name_length;
    char *path = NULL;
    xx_dtb_external external;
    uint32_t position = *cursor;

    if (depth > XX_DTB_MAX_DEPTH) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (!xx_dtb_struct_string(parsed, position, &node_name, &node_name_length,
                              &position) ||
        !xx_dtb_align(position, 4U, &position)) {
        return false;
    }
    xx_mem_zero(&external, sizeof(external));

    if (node_name_length == 0U) {
        /* The root node's name is the empty string.  It owns no path
         * component of its own, so its children hang straight off the top. */
        if (depth != 0U) return false;
        path = xx_str_create("");
        if (!path) return false;
    } else {
        xx_dtb_entry entry;
        if (!xx_dtb_plausible_name(node_name, node_name_length)) return false;
        path = xx_dtb_join_name(prefix, node_name);
        if (!path) return false;
        xx_mem_zero(&entry, sizeof(entry));
        entry.name = xx_str_create(path);
        entry.data_offset = -1;
        entry.data_size = 0;
        entry.is_folder = true;
        if (!entry.name || !xx_dtb_append_entry(parsed, &entry)) {
            if (entry.name) xx_str_free(entry.name);
            xx_str_free(path);
            return false;
        }
        /* An /images node is what makes a device tree a FIT image. */
        if (depth == 1U && xx_str_equals(path, "images")) parsed->is_fit = true;
    }
    ++parsed->nodes;

    for (;;) {
        uint32_t token;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (position > parsed->structure_size - 4U ||
            position + 4U < position) {
            goto fail;
        }
        token = xx_data_get_u32(parsed->structure, parsed->structure_size,
                                position, true);
        position += 4U;
        if (token == XX_DTB_TOKEN_NOP) continue;
        if (token == XX_DTB_TOKEN_END_NODE) break;
        if (token == XX_DTB_TOKEN_BEGIN_NODE) {
            if (!xx_dtb_walk_node(parsed, &position, path, depth + 1U, pd)) {
                goto fail;
            }
            continue;
        }
        if (token != XX_DTB_TOKEN_PROP) goto fail;
        {
            uint32_t length;
            uint32_t name_offset;
            uint32_t value_start;
            const char *property_name;
            char *full_name;
            xx_dtb_entry entry;
            int64_t value_offset;

            if (position > parsed->structure_size - 8U) goto fail;
            length = xx_data_get_u32(parsed->structure, parsed->structure_size,
                                     position, true);
            name_offset = xx_data_get_u32(parsed->structure,
                                          parsed->structure_size,
                                          position + 4U, true);
            position += 8U;
            /* Before version 16 a value of eight bytes or more is 8-byte
             * aligned; from version 16 on everything is 4-byte aligned. */
            if (parsed->version < 16U && length >= 8U) {
                if (!xx_dtb_align(position, 8U, &position)) goto fail;
            }
            value_start = position;
            if (length > parsed->structure_size ||
                value_start > parsed->structure_size - length) {
                goto fail;
            }
            if (!xx_dtb_align(value_start + length, 4U, &position) ||
                position > parsed->structure_size) {
                goto fail;
            }
            property_name = xx_dtb_string_at(parsed, name_offset);
            if (!property_name ||
                !xx_dtb_plausible_name(property_name,
                                       xx_str_len(property_name))) {
                goto fail;
            }
            /* Remember the FIT external-data pair; the member it describes
             * is created once the node closes. */
            if (length == 4U && xx_str_equals(property_name, "data-offset")) {
                external.has_offset = true;
                external.offset = xx_data_get_u32(parsed->structure,
                                                  parsed->structure_size,
                                                  value_start, true);
            } else if (length == 4U &&
                       xx_str_equals(property_name, "data-size")) {
                external.has_size = true;
                external.size = xx_data_get_u32(parsed->structure,
                                                parsed->structure_size,
                                                value_start, true);
            }
            full_name = xx_dtb_join_name(path, property_name);
            if (!full_name) goto fail;
            if (!xx_dtb_add(parsed->struct_offset, value_start,
                            &value_offset)) {
                xx_str_free(full_name);
                goto fail;
            }
            xx_mem_zero(&entry, sizeof(entry));
            entry.name = full_name;
            entry.data_offset = value_offset;
            entry.data_size = (int64_t)length;
            entry.is_folder = false;
            if (!xx_dtb_append_entry(parsed, &entry)) {
                xx_str_free(full_name);
                goto fail;
            }
            ++parsed->properties;
        }
    }

    /* External FIT payload: synthesise the "data" member the node would have
     * carried inline.  A pair that does not resolve inside the device is
     * dropped rather than failing the parse, since the rest of the tree is
     * still perfectly readable. */
    if (external.has_offset && external.has_size &&
        parsed->external_base >= 0 && node_name_length != 0U) {
        int64_t payload;
        if (xx_dtb_add(parsed->external_base, external.offset, &payload) &&
            xx_dtb_range_within(parsed->input_size, payload,
                                (int64_t)external.size)) {
            xx_dtb_entry entry;
            xx_mem_zero(&entry, sizeof(entry));
            entry.name = xx_dtb_join_name(path, "data");
            entry.data_offset = payload;
            entry.data_size = (int64_t)external.size;
            entry.is_folder = false;
            if (entry.name && xx_dtb_append_entry(parsed, &entry)) {
                ++parsed->properties;
            } else if (entry.name) {
                xx_str_free(entry.name);
            }
        }
    }

    xx_str_free(path);
    *cursor = position;
    return true;
fail:
    if (path) xx_str_free(path);
    return false;
}

static bool xx_dtb_parse(Abstractformat *self, xx_dtb_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t header[XX_DTB_HEADER_SIZE];
    uint32_t off_struct;
    uint32_t off_strings;
    uint32_t off_rsvmap;
    uint32_t struct_size;
    uint32_t strings_size;
    uint32_t cursor;
    uint32_t token;
    int64_t offset;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->struct_offset = -1;
        parsed->external_base = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_dtb_range_within(parsed->input_size, self->base_address,
                             XX_DTB_HEADER_SIZE) ||
        !xx_dtb_read_at(self->device, self->base_address, header,
                        sizeof(header)) ||
        xx_data_get_u32(header, sizeof(header), 0U, true) != XX_DTB_MAGIC) {
        goto fail;
    }
    parsed->total_size = xx_data_get_u32(header, sizeof(header), 4U, true);
    off_struct = xx_data_get_u32(header, sizeof(header), 8U, true);
    off_strings = xx_data_get_u32(header, sizeof(header), 12U, true);
    off_rsvmap = xx_data_get_u32(header, sizeof(header), 16U, true);
    parsed->version = xx_data_get_u32(header, sizeof(header), 20U, true);
    parsed->last_comp_version =
        xx_data_get_u32(header, sizeof(header), 24U, true);
    parsed->boot_cpuid_phys =
        xx_data_get_u32(header, sizeof(header), 28U, true);

    if (parsed->version < XX_DTB_MIN_VERSION ||
        parsed->version > XX_DTB_MAX_VERSION ||
        parsed->last_comp_version > parsed->version ||
        parsed->last_comp_version < XX_DTB_MIN_VERSION) {
        goto fail;
    }
    /* The blob must be at least a header, and must fit the device. */
    if (parsed->total_size < XX_DTB_HEADER_SIZE ||
        !xx_dtb_add(self->base_address, parsed->total_size,
                    &parsed->archive_end) ||
        parsed->archive_end > parsed->input_size) {
        goto fail;
    }
    /* Both blocks are 4-byte aligned and the reservation block is 8-byte
     * aligned; all three live inside the blob. */
    if ((off_struct & 3U) != 0U || (off_rsvmap & 7U) != 0U ||
        off_struct < XX_DTB_HEADER_SIZE || off_strings < XX_DTB_HEADER_SIZE ||
        off_rsvmap < XX_DTB_HEADER_SIZE || off_struct >= parsed->total_size ||
        off_strings >= parsed->total_size || off_rsvmap >= parsed->total_size) {
        goto fail;
    }
    /* size_dt_strings arrived in version 3 and size_dt_struct in version 17;
     * before that the block sizes are implied by what follows them. */
    if (parsed->version >= 3U) {
        strings_size = xx_data_get_u32(header, sizeof(header), 32U, true);
    } else {
        strings_size = parsed->total_size - off_strings;
    }
    if (parsed->version >= 17U) {
        struct_size = xx_data_get_u32(header, sizeof(header), 36U, true);
    } else if (off_strings > off_struct) {
        struct_size = off_strings - off_struct;
    } else {
        struct_size = parsed->total_size - off_struct;
    }
    if (struct_size < 8U || strings_size == 0U ||
        struct_size > parsed->total_size - off_struct ||
        strings_size > parsed->total_size - off_strings ||
        struct_size > XX_DTB_MAX_STRUCT_SIZE ||
        strings_size > XX_DTB_MAX_STRINGS_SIZE) {
        goto fail;
    }

    /* Count the memory reservation entries.  The block is a list of 16-byte
     * (address, size) pairs closed by an all-zero pair. */
    if (!xx_dtb_add(self->base_address, off_rsvmap, &offset)) goto fail;
    for (;;) {
        uint8_t entry[16];
        if (parsed->reservations > XX_DTB_MAX_RESERVATIONS) goto fail;
        if (!xx_dtb_range_within(parsed->archive_end, offset,
                                 (int64_t)sizeof(entry)) ||
            !xx_dtb_read_at(self->device, offset, entry, sizeof(entry))) {
            goto fail;
        }
        if (xx_data_get_u64(entry, sizeof(entry), 0U, true) == 0U &&
            xx_data_get_u64(entry, sizeof(entry), 8U, true) == 0U) {
            break;
        }
        ++parsed->reservations;
        offset += (int64_t)sizeof(entry);
    }

    /* Stage both blocks.  Their sizes were bounded above, so these are the
     * only two allocations proportional to anything the blob declares. */
    if (!xx_dtb_add(self->base_address, off_struct, &parsed->struct_offset)) {
        goto fail;
    }
    parsed->structure = (uint8_t *)xx_mem_alloc(struct_size);
    parsed->strings = (uint8_t *)xx_mem_alloc(strings_size);
    if (!parsed->structure || !parsed->strings) goto fail;
    parsed->structure_size = struct_size;
    parsed->strings_size = strings_size;
    if (!xx_dtb_read_at(self->device, parsed->struct_offset, parsed->structure,
                        struct_size) ||
        !xx_dtb_add(self->base_address, off_strings, &offset) ||
        !xx_dtb_read_at(self->device, offset, parsed->strings,
                        strings_size)) {
        goto fail;
    }
    /* A FIT image's external payloads are measured from the end of the blob
     * rounded up to 4 bytes. */
    {
        uint32_t aligned;
        if (xx_dtb_align(parsed->total_size, 4U, &aligned)) {
            (void)xx_dtb_add(self->base_address, aligned,
                             &parsed->external_base);
        }
    }

    /* The structure block opens with the root node and closes with FDT_END;
     * leading NOPs are legal. */
    cursor = 0U;
    for (;;) {
        if (cursor > struct_size - 4U) goto fail;
        token = xx_data_get_u32(parsed->structure, struct_size, cursor, true);
        cursor += 4U;
        if (token == XX_DTB_TOKEN_NOP) continue;
        if (token != XX_DTB_TOKEN_BEGIN_NODE) goto fail;
        break;
    }
    if (!xx_dtb_walk_node(parsed, &cursor, "", 0U, pd)) goto fail;
    for (;;) {
        if (cursor > struct_size - 4U) goto fail;
        token = xx_data_get_u32(parsed->structure, struct_size, cursor, true);
        cursor += 4U;
        if (token == XX_DTB_TOKEN_NOP) continue;
        if (token != XX_DTB_TOKEN_END) goto fail;
        break;
    }
    /* The staged blocks stay resident for the life of the parse result.
     * Both were bounded before they were read, and the entries themselves
     * carry device offsets, so extraction never consults them again. */
    return true;
fail:
    xx_dtb_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_dtb_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
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

static const xx_var *xx_dtb_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_dtb_populate_record(xx_archive_record *record,
                                   const xx_dtb_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_dtb_archive_stream_free(void *pointer) {
    xx_dtb_archive_stream *stream = (xx_dtb_archive_stream *)pointer;
    if (!stream) return;
    xx_dtb_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_dtb_init(xx_dtb *dtb, xx_io_device *dev, int64_t base_address) {
    if (!dtb) return;
    xx_mem_zero(dtb, sizeof(*dtb));
    xx_format_init(&dtb->format, dev, base_address);
    dtb->format.endian = XX_ENDIAN_BIG;
    dtb->format.file_type = XX_DTB_FILE_TYPE;
    dtb->format.format_type = XX_TYPE_ARCHIVE;
    dtb->format.is_archive = true;
    xx_format_set_mime_type(&dtb->format, "application/x-devicetree");
    xx_format_set_extension(&dtb->format, "dtb");
    dtb->format.check_is_valid = xx_dtb_check_is_valid;
    dtb->format.handle_base_info = xx_dtb_handle_base_info;
    dtb->format.get_format_size = xx_dtb_get_format_size;
    dtb->format.get_number_of_archive_records =
        xx_dtb_get_number_of_archive_records;
    dtb->format.create_archive_records_reading =
        xx_dtb_create_archive_records_reading;
    dtb->format.get_current_archive_record = xx_dtb_get_current_archive_record;
    dtb->format.unpack_current_archive_record =
        xx_dtb_unpack_current_archive_record;
    dtb->format.archive_record_move_to_next = xx_dtb_archive_record_move_to_next;
    dtb->format.free_archive_records_reading =
        xx_dtb_free_archive_records_reading;
    dtb->format.destroy = xx_dtb_vtable_destroy;
    dtb->archive_end = -1;
}

xx_dtb *xx_dtb_create(xx_io_device *dev, int64_t base_address) {
    xx_dtb *dtb = (xx_dtb *)xx_mem_alloc(sizeof(*dtb));
    if (dtb) xx_dtb_init(dtb, dev, base_address);
    return dtb;
}

void xx_dtb_destroy(xx_dtb *dtb) {
    if (!dtb) return;
    if (dtb->internal) {
        xx_dtb_private_cleanup((xx_dtb_private *)dtb->internal);
        xx_mem_free(dtb->internal);
        dtb->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&dtb->format);
}

static void xx_dtb_vtable_destroy(Abstractformat *self) {
    xx_dtb_destroy((xx_dtb *)self);
}

void xx_dtb_free(xx_dtb *dtb) {
    if (!dtb) return;
    xx_dtb_destroy(dtb);
    xx_mem_free(dtb);
}

bool xx_dtb_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dtb_private parsed;
    bool result = xx_dtb_parse(self, &parsed, pd);
    xx_dtb_private_cleanup(&parsed);
    return result;
}

bool xx_dtb_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dtb_private *parsed;
    xx_dtb *dtb = (xx_dtb *)self;
    int64_t total_size;
    if (!self || !dtb) return false;
    parsed = (xx_dtb_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_dtb_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (dtb->internal) {
        xx_dtb_private_cleanup((xx_dtb_private *)dtb->internal);
        xx_mem_free(dtb->internal);
    }
    dtb->internal = parsed;
    dtb->number_of_records = parsed->count;
    dtb->number_of_members = parsed->count;
    dtb->number_of_nodes = parsed->nodes;
    dtb->number_of_properties = parsed->properties;
    dtb->total_size = parsed->total_size;
    dtb->version = parsed->version;
    dtb->last_comp_version = parsed->last_comp_version;
    dtb->boot_cpuid_phys = parsed->boot_cpuid_phys;
    dtb->struct_size = parsed->structure_size;
    dtb->strings_size = parsed->strings_size;
    dtb->reservations = parsed->reservations;
    dtb->is_fit = parsed->is_fit;
    dtb->archive_end = parsed->archive_end;
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

int64_t xx_dtb_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_dtb_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_dtb *)self)->number_of_records;
}

xx_archive_record_state *xx_dtb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_dtb_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_dtb_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_dtb_copy_options(&state->options, options) ||
        !xx_dtb_parse(self, &stream->parsed, pd)) {
        xx_dtb_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_dtb_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_dtb_populate_record(&state->current_record,
                               &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_dtb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dtb_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_dtb_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dtb_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_dtb_populate_record(&state->current_record,
                                &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_dtb_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool folder;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_dtb_safe_name(name)) return false;
    option = xx_dtb_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        folder = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER,
                                                 false);
        return folder || (record->data_offset >= 0 &&
                          record->compressed_size >= 0 &&
                          record->data_offset <= total &&
                          record->compressed_size <=
                              total - record->data_offset);
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
        destination = xx_str_concat3(base, "/", name);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    folder = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER,
                                             false);
    if (folder) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                record->data_offset,
                                                record->compressed_size,
                                                destination, pd);
    }

cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_dtb_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_dtb_get_number_of_records(const xx_dtb *dtb) {
    return dtb ? dtb->number_of_records : 0U;
}
uint64_t xx_dtb_get_number_of_members(const xx_dtb *dtb) {
    return dtb ? dtb->number_of_members : 0U;
}
uint64_t xx_dtb_get_number_of_nodes(const xx_dtb *dtb) {
    return dtb ? dtb->number_of_nodes : 0U;
}
uint64_t xx_dtb_get_number_of_properties(const xx_dtb *dtb) {
    return dtb ? dtb->number_of_properties : 0U;
}
uint32_t xx_dtb_get_total_size(const xx_dtb *dtb) {
    return dtb ? dtb->total_size : 0U;
}
uint32_t xx_dtb_get_version(const xx_dtb *dtb) {
    return dtb ? dtb->version : 0U;
}
bool xx_dtb_get_is_fit(const xx_dtb *dtb) { return dtb ? dtb->is_fit : false; }
int64_t xx_dtb_get_archive_end(const xx_dtb *dtb) {
    return dtb ? dtb->archive_end : -1;
}
