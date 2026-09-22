/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xx_memory_map.h"

#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>

static bool xx_memory_map_contains_offset(const xx_memory_record *record,
                                          int64_t offset) {
    return record && record->offset >= 0 && record->size > 0 &&
           record->offset <= INT64_MAX - record->size &&
           offset >= record->offset && offset - record->offset < record->size;
}

static bool xx_memory_map_contains_address(const xx_memory_record *record,
                                           uint64_t address) {
    return record && address != XX_INVALID_ADDRESS &&
           record->address != XX_INVALID_ADDRESS && record->size > 0 &&
           (uint64_t)record->size <=
               XX_INVALID_ADDRESS - record->address &&
           address >= record->address &&
           address - record->address < (uint64_t)record->size;
}

static bool xx_memory_map_contains_address_range(
    const xx_memory_record *record, uint64_t address, int64_t size) {
    uint64_t delta;
    return record && size > 0 && address != XX_INVALID_ADDRESS &&
           (uint64_t)size <= XX_INVALID_ADDRESS - address &&
           xx_memory_map_contains_address(record, address) &&
           (delta = address - record->address) <= (uint64_t)record->size &&
           (uint64_t)size <= (uint64_t)record->size - delta;
}

static bool xx_memory_map_add_address(uint64_t base, int64_t relative,
                                      uint64_t *result) {
    uint64_t value;
    if (!result || base == XX_INVALID_ADDRESS || relative < 0 ||
        (uint64_t)relative >= XX_INVALID_ADDRESS - base) {
        return false;
    }
    value = base + (uint64_t)relative;
    if (value == XX_INVALID_ADDRESS) return false;
    *result = value;
    return true;
}

static void xx_memory_map_copy_name(char destination[XX_MEMORY_RECORD_NAME_SIZE],
                                    const char *source) {
    size_t i = 0U;
    if (!destination) return;
    if (source) {
        while (i + 1U < XX_MEMORY_RECORD_NAME_SIZE && source[i]) {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
}

void xx_memory_map_init(xx_memory_map *map) {
    if (!map) return;
    xx_mem_zero(map, sizeof(*map));
    map->module_address = XX_INVALID_ADDRESS;
    map->entry_point_address = XX_INVALID_ADDRESS;
    map->code_base = -1;
    map->start_load_offset = -1;
    map->file_type = XX_FILE_TYPE_UNKNOWN;
    map->format_type = XX_FORMAT_TYPE_UNKNOWN;
    map->endian = XX_ENDIAN_UNKNOWN;
    map->arch = XX_ARCH_UNKNOWN;
    map->mode = XX_MEMORY_MAP_MODE_UNKNOWN;
}

void xx_memory_map_cleanup(xx_memory_map *map) {
    if (!map) return;
    if (map->records) xx_mem_free(map->records);
    xx_memory_map_init(map);
}

bool xx_memory_map_reserve(xx_memory_map *map, size_t capacity) {
    xx_memory_record *records;
    size_t grown;
    if (!map) return false;
    if (capacity <= map->record_capacity) return true;
    grown = map->record_capacity ? map->record_capacity : 8U;
    while (grown < capacity) {
        if (grown > SIZE_MAX / 2U) {
            grown = capacity;
            break;
        }
        grown *= 2U;
    }
    if (grown > SIZE_MAX / sizeof(*records)) return false;
    records = (xx_memory_record *)xx_mem_realloc(
        map->records, grown * sizeof(*records));
    if (!records) return false;
    map->records = records;
    map->record_capacity = grown;
    return true;
}

bool xx_memory_map_add_record(xx_memory_map *map,
                              const xx_memory_record *record) {
    xx_memory_record copy;
    if (!map || !record || map->record_count >= (size_t)INT32_MAX ||
        record->size <= 0 ||
        (record->is_virtual && record->offset != -1) ||
        (!record->is_virtual &&
         (record->offset < 0 ||
          record->offset > INT64_MAX - record->size)) ||
        (record->address != XX_INVALID_ADDRESS &&
         (uint64_t)record->size >
             XX_INVALID_ADDRESS - record->address)) {
        return false;
    }
    if (!xx_memory_map_reserve(map, map->record_count + 1U)) return false;
    copy = *record;
    copy.name[XX_MEMORY_RECORD_NAME_SIZE - 1U] = '\0';
    copy.index = (int32_t)map->record_count;
    map->records[map->record_count++] = copy;
    return true;
}

bool xx_memory_map_add_part(xx_memory_map *map, int64_t offset,
                            int64_t file_size, uint64_t address,
                            int64_t virtual_size, xx_file_part_t file_part,
                            int32_t file_part_number, const char *name,
                            bool is_invisible) {
    xx_memory_record record;
    uint64_t tail_address;
    if (!map || file_size < 0 || virtual_size < 0 ||
        (file_size > 0 && offset < 0)) {
        return false;
    }
    if (file_size > 0) {
        xx_mem_zero(&record, sizeof(record));
        record.offset = offset;
        record.address = address;
        record.size = file_size;
        record.file_part = file_part;
        record.file_part_number = file_part_number;
        record.is_invisible = is_invisible;
        xx_memory_map_copy_name(record.name, name);
        if (!xx_memory_map_add_record(map, &record)) return false;
    }
    if (address != XX_INVALID_ADDRESS && virtual_size > file_size) {
        if (!xx_memory_map_add_address(address, file_size, &tail_address))
            return false;
        xx_mem_zero(&record, sizeof(record));
        record.offset = -1;
        record.address = tail_address;
        record.size = virtual_size - file_size;
        record.file_part = file_part;
        record.file_part_number = file_part_number;
        record.is_virtual = true;
        record.is_invisible = is_invisible;
        if (name && name[0]) {
            (void)xx_rt_snprintf(record.name, sizeof(record.name), "%s (virtual)",
                           name);
        } else {
            xx_memory_map_copy_name(record.name, "Virtual");
        }
        if (!xx_memory_map_add_record(map, &record)) return false;
    }
    return true;
}

bool xx_memory_map_finalize(xx_memory_map *map) {
    uint64_t minimum = XX_INVALID_ADDRESS;
    uint64_t maximum = 0U;
    int64_t binary_end = 0;
    size_t i;
    if (!map || map->binary_offset < 0 || map->binary_size < 0 ||
        (map->binary_size > 0 &&
         map->binary_offset > INT64_MAX - map->binary_size))
        return false;
    if (map->binary_size > 0)
        binary_end = map->binary_offset + map->binary_size;
    for (i = 0U; i < map->record_count; ++i) {
        xx_memory_record *record = &map->records[i];
        record->index = (int32_t)i;
        if (record->address != XX_INVALID_ADDRESS && record->size > 0) {
            uint64_t end;
            if ((uint64_t)record->size > XX_INVALID_ADDRESS - record->address)
                return false;
            end = record->address + (uint64_t)record->size;
            if (record->address < minimum) minimum = record->address;
            if (end > maximum) maximum = end;
        }
        if (!record->is_virtual && record->offset >= 0 && record->size > 0) {
            int64_t end;
            if (record->offset > INT64_MAX - record->size) return false;
            end = record->offset + record->size;
            if (map->binary_size > 0 &&
                (record->offset < map->binary_offset || end > binary_end))
                return false;
        }
    }
    if (minimum != XX_INVALID_ADDRESS) {
        if (maximum < minimum || maximum - minimum > (uint64_t)INT64_MAX)
            return false;
        map->module_address = minimum;
        map->image_size = (int64_t)(maximum - minimum);
    } else {
        map->image_size = 0;
    }
    return true;
}

const xx_memory_record *xx_memory_map_record_by_offset(
    const xx_memory_map *map, int64_t offset) {
    size_t i;
    if (!map || offset < 0) return NULL;
    for (i = map->record_count; i != 0U; --i) {
        const xx_memory_record *record = &map->records[i - 1U];
        if (xx_memory_map_contains_offset(record, offset)) return record;
    }
    return NULL;
}

const xx_memory_record *xx_memory_map_record_by_address(
    const xx_memory_map *map, uint64_t address) {
    size_t i;
    if (!map || address == XX_INVALID_ADDRESS) return NULL;
    for (i = map->record_count; i != 0U; --i) {
        const xx_memory_record *record = &map->records[i - 1U];
        if (xx_memory_map_contains_address(record, address)) return record;
    }
    return NULL;
}

const xx_memory_record *xx_memory_map_record_by_relative_address(
    const xx_memory_map *map, int64_t relative_address) {
    uint64_t address = xx_memory_map_relative_address_to_address(
        map, relative_address);
    if (address == XX_INVALID_ADDRESS) return NULL;
    return xx_memory_map_record_by_address(map, address);
}

const xx_memory_record *xx_memory_map_record_by_index(
    const xx_memory_map *map, int32_t index) {
    return map && index >= 0 && (size_t)index < map->record_count
               ? &map->records[index]
               : NULL;
}

const xx_memory_record *xx_memory_map_physical_record(
    const xx_memory_map *map, int32_t index) {
    size_t i;
    int32_t seen = 0;

    if (!map || index < 0) return NULL;
    for (i = 0U; i < map->record_count; ++i) {
        const xx_memory_record *record = &map->records[i];
        if (record->is_virtual || record->offset < 0) continue;
        if (seen == index) return record;
        ++seen;
    }
    return NULL;
}

uint64_t xx_memory_map_offset_to_address(const xx_memory_map *map,
                                          int64_t offset) {
    return xx_memory_map_offset_to_address_ex(
        map, offset, XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL);
}

uint64_t xx_memory_map_offset_to_address_ex(const xx_memory_map *map,
                                            int64_t offset,
                                            xx_memory_map_lookup_t lookup) {
    size_t i;
    if (!map || offset < 0) return XX_INVALID_ADDRESS;

    if (lookup == XX_MEMORY_MAP_LOOKUP_FIRST_MATCH) {
        for (i = 0U; i < map->record_count; ++i) {
            const xx_memory_record *record = &map->records[i];
            int64_t delta;
            uint64_t result;
            if (!xx_memory_map_contains_offset(record, offset)) continue;
            /* The first record covering the offset answers, even when the
             * answer is "not mapped": an overlay does not become addressable
             * because some later record spans the same bytes. */
            if (record->is_virtual || record->address == XX_INVALID_ADDRESS)
                return XX_INVALID_ADDRESS;
            delta = offset - record->offset;
            return xx_memory_map_add_address(record->address, delta, &result)
                       ? result
                       : XX_INVALID_ADDRESS;
        }
        return XX_INVALID_ADDRESS;
    }

    for (i = map->record_count; i != 0U; --i) {
        const xx_memory_record *record = &map->records[i - 1U];
        int64_t delta;
        uint64_t result;
        if (record->is_virtual || record->address == XX_INVALID_ADDRESS ||
            !xx_memory_map_contains_offset(record, offset))
            continue;
        delta = offset - record->offset;
        if (xx_memory_map_add_address(record->address, delta, &result))
            return result;
    }
    return XX_INVALID_ADDRESS;
}

int64_t xx_memory_map_address_to_offset(const xx_memory_map *map,
                                        uint64_t address) {
    return xx_memory_map_address_to_offset_ex(
        map, address, XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL);
}

int64_t xx_memory_map_address_to_offset_ex(const xx_memory_map *map,
                                           uint64_t address,
                                           xx_memory_map_lookup_t lookup) {
    size_t i;
    if (!map || address == XX_INVALID_ADDRESS) return -1;

    if (lookup == XX_MEMORY_MAP_LOOKUP_FIRST_MATCH) {
        for (i = 0U; i < map->record_count; ++i) {
            const xx_memory_record *record = &map->records[i];
            uint64_t delta;
            /* An unmapped record is invisible to an address lookup, exactly
             * as it is in the other mode; it is the VIRTUAL tail that stops
             * the search here. add_part emits that tail directly after the
             * record it belongs to, so reaching it means the address fell
             * past the file bytes of the record that owns this address
             * range -- which has no offset to give. */
            if (record->address == XX_INVALID_ADDRESS) continue;
            if (!xx_memory_map_contains_address(record, address)) continue;
            if (record->is_virtual || record->offset < 0) return -1;
            delta = address - record->address;
            return (delta <= (uint64_t)(INT64_MAX - record->offset))
                       ? record->offset + (int64_t)delta
                       : -1;
        }
        return -1;
    }

    for (i = map->record_count; i != 0U; --i) {
        const xx_memory_record *record = &map->records[i - 1U];
        uint64_t delta;
        if (record->is_virtual || record->offset < 0 ||
            !xx_memory_map_contains_address(record, address))
            continue;
        delta = address - record->address;
        if (delta <= (uint64_t)(INT64_MAX - record->offset))
            return record->offset + (int64_t)delta;
    }
    return -1;
}


uint64_t xx_memory_map_offset_to_relative_address(const xx_memory_map *map,
                                                   int64_t offset) {
    uint64_t address = xx_memory_map_offset_to_address(map, offset);
    if (!map || address == XX_INVALID_ADDRESS ||
        map->module_address == XX_INVALID_ADDRESS ||
        address < map->module_address)
        return XX_INVALID_ADDRESS;
    return address - map->module_address;
}

int64_t xx_memory_map_relative_address_to_offset(const xx_memory_map *map,
                                                  int64_t relative_address) {
    uint64_t address;
    if (!map || !xx_memory_map_add_address(map->module_address,
                                            relative_address, &address))
        return -1;
    return xx_memory_map_address_to_offset(map, address);
}

uint64_t xx_memory_map_relative_address_to_address(const xx_memory_map *map,
                                                    int64_t relative_address) {
    uint64_t address;
    if (!map || !xx_memory_map_add_address(map->module_address,
                                            relative_address, &address) ||
        !xx_memory_map_is_address_valid(map, address))
        return XX_INVALID_ADDRESS;
    return address;
}

int64_t xx_memory_map_address_to_relative_address(const xx_memory_map *map,
                                                   uint64_t address) {
    uint64_t delta;
    if (!map || map->module_address == XX_INVALID_ADDRESS ||
        address < map->module_address ||
        !xx_memory_map_is_address_valid(map, address))
        return -1;
    delta = address - map->module_address;
    return delta <= (uint64_t)INT64_MAX ? (int64_t)delta : -1;
}

bool xx_memory_map_is_offset_valid(const xx_memory_map *map, int64_t offset) {
    if (!map || offset < 0) return false;
    if (map->binary_size > 0 && map->binary_offset >= 0 &&
        offset >= map->binary_offset)
        return offset - map->binary_offset < map->binary_size;
    return xx_memory_map_record_by_offset(map, offset) != NULL;
}

bool xx_memory_map_is_offset_range_valid(const xx_memory_map *map,
                                          int64_t offset, int64_t size) {
    int64_t end;
    int64_t covered;
    if (!map || offset < 0 || size <= 0 || offset > INT64_MAX - size)
        return false;
    if (map->binary_size > 0) {
        int64_t delta;
        if (map->binary_offset < 0 || offset < map->binary_offset)
            return false;
        delta = offset - map->binary_offset;
        return delta < map->binary_size && size <= map->binary_size - delta;
    }
    end = offset + size;
    covered = offset;
    while (covered < end) {
        int64_t next = covered;
        size_t i;
        for (i = 0U; i < map->record_count; ++i) {
            const xx_memory_record *record = &map->records[i];
            int64_t record_end;
            if (record->offset < 0 || record->size <= 0 ||
                record->offset > covered ||
                record->offset > INT64_MAX - record->size)
                continue;
            record_end = record->offset + record->size;
            if (covered < record_end && record_end > next)
                next = record_end < end ? record_end : end;
        }
        if (next <= covered) return false;
        covered = next;
    }
    return true;
}

bool xx_memory_map_is_address_valid(const xx_memory_map *map,
                                     uint64_t address) {
    if (!map || address == XX_INVALID_ADDRESS) return false;
    if (map->image_size > 0 && map->module_address != XX_INVALID_ADDRESS &&
        address >= map->module_address)
        return address - map->module_address < (uint64_t)map->image_size;
    return xx_memory_map_record_by_address(map, address) != NULL;
}

bool xx_memory_map_is_address_range_valid(const xx_memory_map *map,
                                           uint64_t address, int64_t size) {
    uint64_t end;
    uint64_t covered;
    if (!map || size <= 0 || address == XX_INVALID_ADDRESS ||
        (uint64_t)size > XX_INVALID_ADDRESS - address)
        return false;
    if (map->image_size > 0 && map->module_address != XX_INVALID_ADDRESS) {
        uint64_t delta;
        if (address < map->module_address) return false;
        delta = address - map->module_address;
        return delta < (uint64_t)map->image_size &&
               (uint64_t)size <= (uint64_t)map->image_size - delta;
    }
    end = address + (uint64_t)size;
    covered = address;
    while (covered < end) {
        uint64_t next = covered;
        size_t i;
        for (i = 0U; i < map->record_count; ++i) {
            const xx_memory_record *record = &map->records[i];
            uint64_t record_end;
            if (record->address == XX_INVALID_ADDRESS || record->size <= 0 ||
                record->address > covered ||
                (uint64_t)record->size >
                    XX_INVALID_ADDRESS - record->address)
                continue;
            record_end = record->address + (uint64_t)record->size;
            if (covered < record_end && record_end > next)
                next = record_end < end ? record_end : end;
        }
        if (next <= covered) return false;
        covered = next;
    }
    return true;
}

bool xx_memory_map_is_relative_address_valid(const xx_memory_map *map,
                                              int64_t relative_address) {
    uint64_t address;
    return map && xx_memory_map_add_address(map->module_address,
                                             relative_address, &address) &&
           xx_memory_map_is_address_valid(map, address);
}

bool xx_memory_map_is_address_physical(const xx_memory_map *map,
                                        uint64_t address) {
    return xx_memory_map_address_to_offset(map, address) != -1;
}

bool xx_memory_map_is_relative_address_physical(
    const xx_memory_map *map, int64_t relative_address) {
    return xx_memory_map_relative_address_to_offset(map, relative_address) != -1;
}

bool xx_memory_map_is_solid_address_range(const xx_memory_map *map,
                                           uint64_t address, int64_t size) {
    size_t i;
    if (!map || size <= 0 || address == XX_INVALID_ADDRESS ||
        (uint64_t)size > XX_INVALID_ADDRESS - address)
        return false;
    for (i = map->record_count; i != 0U; --i) {
        if (xx_memory_map_contains_address_range(&map->records[i - 1U],
                                                 address, size))
            return true;
    }
    return false;
}

bool xx_memory_map_is_physical_address_range(const xx_memory_map *map,
                                              uint64_t address, int64_t size) {
    size_t i;
    size_t winner = SIZE_MAX;
    const xx_memory_record *record = NULL;
    int64_t offset;
    if (!map || size <= 0 || address == XX_INVALID_ADDRESS ||
        (uint64_t)size > XX_INVALID_ADDRESS - address)
        return false;
    for (i = map->record_count; i != 0U; --i) {
        const xx_memory_record *candidate = &map->records[i - 1U];
        if (!candidate->is_virtual && candidate->offset >= 0 &&
            xx_memory_map_contains_address(candidate, address)) {
            winner = i - 1U;
            record = candidate;
            break;
        }
    }
    if (!record || !xx_memory_map_contains_address_range(record, address, size))
        return false;
    offset = xx_memory_map_address_to_offset(map, address);
    if (!xx_memory_map_is_offset_range_valid(map, offset, size)) return false;
    /* A later record may take precedence in the middle of the requested range. */
    for (i = winner + 1U; i < map->record_count; ++i) {
        const xx_memory_record *other = &map->records[i];
        uint64_t request_end;
        uint64_t other_end;
        if (other->is_virtual || other->address == XX_INVALID_ADDRESS ||
            other->size <= 0)
            continue;
        if ((uint64_t)other->size >
            XX_INVALID_ADDRESS - other->address)
            return false;
        request_end = address + (uint64_t)size;
        other_end = other->address + (uint64_t)other->size;
        if (other->address < request_end && address < other_end) return false;
    }
    return true;
}
