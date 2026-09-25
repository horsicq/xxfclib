/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/iso9660/xx_iso9660.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define XX_ISO9660_SECTOR_SIZE 2048U
#define XX_ISO9660_PVD_SECTOR 16U
#define XX_ISO9660_MAX_DESCRIPTORS 64U
#define XX_ISO9660_MAX_ENTRIES 100000U
#define XX_ISO9660_MAX_DEPTH 64U
#define XX_ISO9660_MAX_NAME_SIZE 4096U

typedef struct xx_iso9660_entry_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    uint32_t data_size;
    uint8_t flags;
} xx_iso9660_entry;

typedef struct xx_iso9660_visited_s {
    uint32_t extent;
    uint32_t size;
} xx_iso9660_visited;

typedef struct xx_iso9660_private_s {
    xx_iso9660_entry *entries;
    size_t count;
    size_t capacity;
    xx_iso9660_visited *visited;
    size_t visited_count;
    size_t visited_capacity;
    int64_t pvd_offset;
    int64_t volume_end;
    uint32_t block_size;
    uint32_t volume_space_size;
} xx_iso9660_private;

typedef struct xx_iso9660_archive_stream_s {
    xx_iso9660_private parsed;
    size_t index;
} xx_iso9660_archive_stream;

static void xx_iso9660_vtable_destroy(Abstractformat *self);

static uint16_t xx_iso9660_read16le(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}
static uint16_t xx_iso9660_read16be(const uint8_t *data) {
    return ((uint16_t)data[0] << 8U) | (uint16_t)data[1];
}
static uint32_t xx_iso9660_read32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}
static uint32_t xx_iso9660_read32be(const uint8_t *data) {
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

static bool xx_iso9660_read_at(xx_io_device *device, int64_t offset,
                               void *data, size_t size) {
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

static bool xx_iso9660_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

static bool xx_iso9660_extent_offset(Abstractformat *self, uint32_t extent,
                                     uint32_t block_size, int64_t *offset) {
    uint64_t relative;
    if (!self || !offset || block_size == 0U ||
        extent > UINT64_MAX / block_size) {
        return false;
    }
    relative = (uint64_t)extent * block_size;
    return xx_iso9660_add(self->base_address, relative, offset);
}

static void xx_iso9660_private_cleanup(xx_iso9660_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->visited) xx_mem_free(parsed->visited);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->pvd_offset = -1;
    parsed->volume_end = -1;
}

static bool xx_iso9660_append_entry(xx_iso9660_private *parsed,
                                    xx_iso9660_entry *entry) {
    xx_iso9660_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name || parsed->count >= XX_ISO9660_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_iso9660_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

static bool xx_iso9660_seen_directory(xx_iso9660_private *parsed,
                                      uint32_t extent, uint32_t size) {
    xx_iso9660_visited *grown;
    size_t index;
    size_t capacity;
    if (!parsed) return true;
    for (index = 0U; index < parsed->visited_count; ++index) {
        if (parsed->visited[index].extent == extent &&
            parsed->visited[index].size == size) return true;
    }
    if (parsed->visited_count == parsed->visited_capacity) {
        capacity = parsed->visited_capacity ? parsed->visited_capacity * 2U : 16U;
        if (capacity < parsed->visited_count ||
            capacity > SIZE_MAX / sizeof(*parsed->visited)) return true;
        grown = (xx_iso9660_visited *)xx_mem_realloc(
            parsed->visited, capacity * sizeof(*parsed->visited));
        if (!grown) return true;
        parsed->visited = grown;
        parsed->visited_capacity = capacity;
    }
    parsed->visited[parsed->visited_count].extent = extent;
    parsed->visited[parsed->visited_count].size = size;
    ++parsed->visited_count;
    return false;
}

static bool xx_iso9660_safe_name(const char *name) {
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

static char *xx_iso9660_name_from_identifier(const uint8_t *identifier,
                                              size_t size) {
    char *name;
    size_t length = size;
    size_t index;
    if (!identifier || size == 0U || size > 255U || identifier[0] == 0U ||
        identifier[0] == 1U) return NULL;
    for (index = 0U; index < size; ++index) {
        if (identifier[index] == ';') {
            length = index;
            break;
        }
        if (identifier[index] < 32U || identifier[index] == '/' ||
            identifier[index] == '\\') return NULL;
    }
    if (length == 0U) return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    xx_mem_copy(name, identifier, length);
    name[length] = '\0';
    if (!xx_iso9660_safe_name(name)) {
        xx_str_free(name);
        return NULL;
    }
    return name;
}

static char *xx_iso9660_join_name(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_ISO9660_MAX_NAME_SIZE ||
        name_size > XX_ISO9660_MAX_NAME_SIZE - prefix_size -
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

static bool xx_iso9660_parse_directory(Abstractformat *self,
                                        xx_iso9660_private *parsed,
                                        uint32_t extent, uint32_t size,
                                        const char *prefix, unsigned depth,
                                        xx_pd_struct *pd) {
    int64_t directory_offset;
    int64_t total_size;
    uint64_t position = 0U;
    if (!self || !parsed || depth > XX_ISO9660_MAX_DEPTH ||
        size == 0U || (pd && xx_pd_is_stopped(pd)) ||
        !xx_iso9660_extent_offset(self, extent, parsed->block_size,
                                  &directory_offset)) {
        return false;
    }
    if (xx_iso9660_seen_directory(parsed, extent, size)) return true;
    total_size = xx_io_total_size(self->device);
    if (directory_offset > total_size || size > (uint64_t)(total_size - directory_offset)) {
        return false;
    }
    while (position < size) {
        uint8_t record[255];
        uint8_t record_length;
        uint8_t identifier_length;
        uint32_t data_extent;
        uint32_t data_extent_be;
        uint32_t data_size;
        uint32_t data_size_be;
        int64_t record_offset;
        int64_t data_offset;
        uint64_t sector_position;
        char *component = NULL;
        char *full_name = NULL;
        xx_iso9660_entry entry;

        if (pd && xx_pd_is_stopped(pd) ||
            position > (uint64_t)INT64_MAX ||
            !xx_iso9660_add(directory_offset, position, &record_offset) ||
            !xx_iso9660_read_at(self->device, record_offset, record, 1U)) {
            return false;
        }
        record_length = record[0];
        sector_position = position % parsed->block_size;
        if (record_length == 0U) {
            position += parsed->block_size - sector_position;
            continue;
        }
        if (record_length < 34U || record_length > size - position ||
            record_length > parsed->block_size - sector_position ||
            !xx_iso9660_read_at(self->device, record_offset, record,
                                record_length)) {
            return false;
        }
        identifier_length = record[32];
        if ((size_t)33U + identifier_length > record_length ||
            (record[25] & UINT8_C(0x80)) != 0U) {
            return false;
        }
        data_extent = xx_iso9660_read32le(record + 2U);
        data_extent_be = xx_iso9660_read32be(record + 6U);
        data_size = xx_iso9660_read32le(record + 10U);
        data_size_be = xx_iso9660_read32be(record + 14U);
        if (data_extent != data_extent_be || data_size != data_size_be ||
            !xx_iso9660_extent_offset(self, data_extent, parsed->block_size,
                                      &data_offset) ||
            data_offset > total_size ||
            data_size > (uint64_t)(total_size - data_offset)) {
            return false;
        }
        component = xx_iso9660_name_from_identifier(record + 33U,
                                                     identifier_length);
        if (component) {
            xx_mem_zero(&entry, sizeof(entry));
            full_name = xx_iso9660_join_name(prefix, component);
            xx_str_free(component);
            if (!full_name) return false;
            entry.name = full_name;
            entry.header_offset = record_offset;
            entry.data_offset = data_offset;
            entry.data_size = data_size;
            entry.flags = record[25];
            if (!xx_iso9660_append_entry(parsed, &entry)) {
                xx_str_free(full_name);
                return false;
            }
            if ((record[25] & UINT8_C(0x02)) != 0U &&
                !xx_iso9660_parse_directory(self, parsed, data_extent,
                                            data_size, full_name, depth + 1U,
                                            pd)) {
                return false;
            }
        } else if (identifier_length != 1U ||
                   (record[33] != 0U && record[33] != 1U)) {
            return false;
        }
        position += record_length;
    }
    return true;
}

static bool xx_iso9660_parse(Abstractformat *self, xx_iso9660_private *parsed,
                             xx_pd_struct *pd) {
    uint8_t descriptor[XX_ISO9660_SECTOR_SIZE];
    uint8_t pvd[XX_ISO9660_SECTOR_SIZE];
    uint32_t volume_space_le;
    uint32_t volume_space_be;
    uint16_t block_size_le;
    uint16_t block_size_be;
    uint32_t root_extent;
    uint32_t root_extent_be;
    uint32_t root_size;
    uint32_t root_size_be;
    int64_t total_size;
    int64_t descriptor_offset;
    bool have_pvd = false;
    bool have_terminator = false;
    unsigned index;
    /* Initialise before the guard clause: callers such as
     * xx_iso9660_check_is_valid() run xx_iso9660_private_cleanup() on their
     * stack copy whatever this returns, and cleaning up an uninitialised one
     * would free indeterminate pointers. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->pvd_offset = -1;
        parsed->volume_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address <
            (int64_t)(XX_ISO9660_PVD_SECTOR + 1U) * XX_ISO9660_SECTOR_SIZE) {
        return false;
    }
    for (index = 0U; index < XX_ISO9660_MAX_DESCRIPTORS; ++index) {
        uint64_t sector = (uint64_t)XX_ISO9660_PVD_SECTOR + index;
        if (sector > UINT64_MAX / XX_ISO9660_SECTOR_SIZE ||
            !xx_iso9660_add(self->base_address,
                             sector * XX_ISO9660_SECTOR_SIZE,
                             &descriptor_offset) ||
            !xx_iso9660_read_at(self->device, descriptor_offset, descriptor,
                                sizeof(descriptor)) ||
            xx_rt_memcmp(descriptor + 1U, "CD001", 5U) != 0 || descriptor[6] != 1U) {
            goto fail;
        }
        if (descriptor[0] == 1U && !have_pvd) {
            xx_mem_copy(pvd, descriptor, sizeof(pvd));
            parsed->pvd_offset = descriptor_offset;
            have_pvd = true;
        }
        if (descriptor[0] == 255U) {
            have_terminator = true;
            break;
        }
    }
    if (!have_pvd || !have_terminator) goto fail;
    volume_space_le = xx_iso9660_read32le(pvd + 80U);
    volume_space_be = xx_iso9660_read32be(pvd + 84U);
    block_size_le = xx_iso9660_read16le(pvd + 128U);
    block_size_be = xx_iso9660_read16be(pvd + 130U);
    if (volume_space_le == 0U || volume_space_le != volume_space_be ||
        block_size_le != XX_ISO9660_SECTOR_SIZE || block_size_le != block_size_be ||
        volume_space_le > UINT64_MAX / block_size_le ||
        !xx_iso9660_add(self->base_address,
                         (uint64_t)volume_space_le * block_size_le,
                         &parsed->volume_end) || parsed->volume_end > total_size ||
        pvd[156] < 34U) goto fail;
    root_extent = xx_iso9660_read32le(pvd + 158U);
    root_extent_be = xx_iso9660_read32be(pvd + 162U);
    root_size = xx_iso9660_read32le(pvd + 166U);
    root_size_be = xx_iso9660_read32be(pvd + 170U);
    parsed->block_size = block_size_le;
    parsed->volume_space_size = volume_space_le;
    if (root_extent != root_extent_be || root_size != root_size_be ||
        (pvd[181] & UINT8_C(0x02)) == 0U ||
        !xx_iso9660_parse_directory(self, parsed, root_extent, root_size,
                                    "", 0U, pd)) goto fail;
    return true;
fail:
    xx_iso9660_private_cleanup(parsed);
    return false;
}

static bool xx_iso9660_copy_options(xx_list_s *destination,
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

static const xx_var *xx_iso9660_find_option(const xx_list_s *options,
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

static bool xx_iso9660_populate_record(xx_archive_record *record,
                                       const xx_iso9660_entry *entry) {
    bool folder;
    if (!record || !entry || !entry->name) return false;
    folder = (entry->flags & UINT8_C(0x02)) != 0U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = -1;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->data_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSED_SIZE,
                                          entry->data_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           folder);
}

static void xx_iso9660_archive_stream_free(void *pointer) {
    xx_iso9660_archive_stream *stream = (xx_iso9660_archive_stream *)pointer;
    if (!stream) return;
    xx_iso9660_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

void xx_iso9660_init(xx_iso9660 *iso, xx_io_device *dev,
                     int64_t base_address) {
    if (!iso) return;
    xx_mem_zero(iso, sizeof(*iso));
    xx_format_init(&iso->format, dev, base_address);
    iso->format.endian = XX_ENDIAN_LITTLE;
    iso->format.file_type = XX_FILE_TYPE_ISO9660;
    iso->format.format_type = XX_TYPE_ARCHIVE;
    iso->format.is_archive = true;
    xx_format_set_mime_type(&iso->format, "application/x-iso9660-image");
    xx_format_set_extension(&iso->format, "iso");
    iso->format.check_is_valid = xx_iso9660_check_is_valid;
    iso->format.handle_base_info = xx_iso9660_handle_base_info;
    iso->format.get_format_size = xx_iso9660_get_format_size;
    iso->format.get_number_of_archive_records =
        xx_iso9660_get_number_of_archive_records;
    iso->format.create_archive_records_reading =
        xx_iso9660_create_archive_records_reading;
    iso->format.get_current_archive_record =
        xx_iso9660_get_current_archive_record;
    iso->format.unpack_current_archive_record =
        xx_iso9660_unpack_current_archive_record;
    iso->format.archive_record_move_to_next =
        xx_iso9660_archive_record_move_to_next;
    iso->format.free_archive_records_reading =
        xx_iso9660_free_archive_records_reading;
    iso->format.destroy = xx_iso9660_vtable_destroy;
    iso->volume_end = -1;
}

xx_iso9660 *xx_iso9660_create(xx_io_device *dev, int64_t base_address) {
    xx_iso9660 *iso = (xx_iso9660 *)xx_mem_alloc(sizeof(*iso));
    if (iso) xx_iso9660_init(iso, dev, base_address);
    return iso;
}

void xx_iso9660_destroy(xx_iso9660 *iso) {
    if (!iso) return;
    if (iso->internal) {
        xx_iso9660_private_cleanup((xx_iso9660_private *)iso->internal);
        xx_mem_free(iso->internal);
        iso->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&iso->format);
}

static void xx_iso9660_vtable_destroy(Abstractformat *self) {
    xx_iso9660_destroy((xx_iso9660 *)self);
}

void xx_iso9660_free(xx_iso9660 *iso) {
    if (!iso) return;
    xx_iso9660_destroy(iso);
    xx_mem_free(iso);
}

bool xx_iso9660_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_iso9660_private parsed;
    bool result = xx_iso9660_parse(self, &parsed, pd);
    xx_iso9660_private_cleanup(&parsed);
    return result;
}

bool xx_iso9660_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_iso9660_private *parsed;
    xx_iso9660 *iso = (xx_iso9660 *)self;
    int64_t total_size;
    if (!self || !iso) return false;
    parsed = (xx_iso9660_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_iso9660_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (iso->internal) {
        xx_iso9660_private_cleanup((xx_iso9660_private *)iso->internal);
        xx_mem_free(iso->internal);
    }
    iso->internal = parsed;
    iso->number_of_records = parsed->count;
    iso->number_of_members = parsed->count;
    iso->logical_block_size = parsed->block_size;
    iso->volume_space_size = parsed->volume_space_size;
    iso->volume_end = parsed->volume_end;
    self->format_size = parsed->volume_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->volume_end) {
        self->overlay_offset = parsed->volume_end;
        self->overlay_size = total_size - parsed->volume_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_iso9660_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_iso9660_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_iso9660 *)self)->number_of_records;
}

xx_archive_record_state *xx_iso9660_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_iso9660_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_iso9660_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_iso9660_copy_options(&state->options, options) ||
        !xx_iso9660_parse(self, &stream->parsed, pd)) {
        xx_iso9660_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_iso9660_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_iso9660_populate_record(&state->current_record,
                                   &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_iso9660_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_iso9660_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_iso9660_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_iso9660_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_iso9660_populate_record(&state->current_record,
                                    &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_iso9660_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
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
    if (!xx_iso9660_safe_name(name)) return false;
    option = xx_iso9660_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
        result = xx_store_unpack_device_to_file(self->device, record->data_offset,
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

void xx_iso9660_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_iso9660_get_number_of_records(const xx_iso9660 *iso) {
    return iso ? iso->number_of_records : 0U;
}
uint64_t xx_iso9660_get_number_of_members(const xx_iso9660 *iso) {
    return iso ? iso->number_of_members : 0U;
}
uint32_t xx_iso9660_get_logical_block_size(const xx_iso9660 *iso) {
    return iso ? iso->logical_block_size : 0U;
}
uint32_t xx_iso9660_get_volume_space_size(const xx_iso9660 *iso) {
    return iso ? iso->volume_space_size : 0U;
}
int64_t xx_iso9660_get_volume_end(const xx_iso9660 *iso) {
    return iso ? iso->volume_end : -1;
}
