/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "die_engine_primitives.h"
#include "xxfclib/memory/xx_memory.h"
#include <string.h>

static bool valid_range(const void *data, size_t data_size,
    uint64_t offset, uint64_t length)
{
    return (data || !data_size) && offset <= data_size
        && length <= data_size - offset;
}

bool die_find_any_bytes(const void *data, size_t data_size,
    uint64_t offset, uint64_t length, const DieBytePattern *patterns,
    size_t pattern_count, DieBytesMatch *result)
{
    int buckets[256], links[DIE_BYTES_PATTERN_MAX];
    size_t i, budget = 0, distinct = 0;
    uint64_t position;
    uint8_t only_byte = 0;
    const uint8_t *bytes = data;
    if (!result) return false;
    result->found = false;
    result->offset = 0;
    result->pattern_index = 0;
    if (!valid_range(data, data_size, offset, length)
        || pattern_count > DIE_BYTES_PATTERN_MAX
        || (pattern_count && !patterns)) return false;
    for (i = 0; i < 256; ++i) buckets[i] = -1;
    for (i = 0; i < pattern_count; ++i) {
        if (!patterns[i].data || !patterns[i].size
            || patterns[i].size > DIE_BYTES_PATTERN_BUDGET - budget)
            return false;
        budget += patterns[i].size;
    }
    /* Reverse insertion makes each first-byte bucket ascend by pattern index,
     * preserving the specified tie break without comparing later matches. */
    for (i = pattern_count; i > 0; --i) {
        size_t id = i - 1;
        uint8_t first = patterns[id].data[0];
        if (buckets[first] == -1) { ++distinct; only_byte = first; }
        links[id] = buckets[first];
        buckets[first] = (int)id;
    }
    if (!pattern_count || !length) return true;
    for (position = 0; position < length; ++position) {
        int id;
        if (distinct == 1) {
            const uint8_t *hit = memchr(bytes + (size_t)offset + (size_t)position,
                only_byte, (size_t)(length - position));
            if (!hit) break;
            position = (uint64_t)(hit - (bytes + (size_t)offset));
        }
        for (id = buckets[bytes[(size_t)offset + (size_t)position]];
             id != -1; id = links[id]) {
            const DieBytePattern *pattern = &patterns[id];
            if (pattern->size <= length - position
                && !memcmp(bytes + (size_t)offset + (size_t)position,
                    pattern->data, pattern->size)) {
                result->found = true;
                result->offset = offset + position;
                result->pattern_index = (size_t)id;
                return true;
            }
        }
    }
    return true;
}

static uint32_t relation_mask(const uint8_t *bytes, size_t position,
    const DieByteRelationGroup *groups, size_t group_count)
{
    uint32_t mask = 0;
    size_t group_id;
    for (group_id = 0; group_id < group_count; ++group_id) {
        const DieByteRelationGroup *group = &groups[group_id];
        size_t pair_id;
        for (pair_id = 0; pair_id < group->count; ++pair_id) {
            const DieByteRelationPair *pair = &group->pairs[pair_id];
            if (bytes[position + pair->a] != bytes[position + pair->b]) break;
        }
        if (pair_id == group->count) mask |= UINT32_C(1) << group_id;
    }
    return mask;
}

bool die_find_byte_relation_candidates(const void *data, size_t data_size,
    uint64_t offset, uint64_t length, const DieByteRelationGroup *groups,
    size_t group_count, uint64_t tail_bytes, uint32_t **values,
    size_t *value_count)
{
    const uint8_t *bytes;
    size_t i, pair_budget = 0, candidate_end, hits = 0, cursor = 0;
    uint32_t *output;
    if (!values || !value_count) return false;
    *values = NULL;
    *value_count = 0;
    if (!valid_range(data, data_size, offset, length)
        || length > DIE_RELATION_RANGE_MAX
        || group_count > DIE_RELATION_GROUP_MAX
        || (group_count && !groups)) return false;
    for (i = 0; i < group_count; ++i) {
        size_t j;
        if (!groups[i].pairs || !groups[i].count
            || groups[i].count > DIE_RELATION_PAIR_MAX - pair_budget)
            return false;
        pair_budget += groups[i].count;
        for (j = 0; j < groups[i].count; ++j) {
            if (groups[i].pairs[j].a > tail_bytes
                || groups[i].pairs[j].b > tail_bytes) return false;
        }
    }
    if (!group_count || tail_bytes >= length) return true;
    candidate_end = (size_t)(length - tail_bytes);
    bytes = (const uint8_t *)data + (size_t)offset;
    /* Count exactly before allocation, so sparse matches consume sparse memory
     * and a dense result is returned whole without dropping the range tail. */
    for (i = 0; i < candidate_end; ++i)
        if (relation_mask(bytes, i, groups, group_count)) ++hits;
    if (!hits) return true;
    output = xx_mem_alloc(hits * 2u * sizeof(*output));
    if (!output) return false;
    for (i = 0; i < candidate_end; ++i) {
        uint32_t mask = relation_mask(bytes, i, groups, group_count);
        if (mask) { output[cursor++] = (uint32_t)i; output[cursor++] = mask; }
    }
    *values = output;
    *value_count = cursor;
    return true;
}

void die_byte_relation_results_free(uint32_t *values)
{
    xx_mem_free(values);
}

bool die_map_virtual_range(uint64_t image_base, uint64_t file_size,
    const DieVirtualSection *sections, size_t section_count, uint64_t va,
    uint64_t length, uint32_t required_flags, bool file_backed,
    DieVAToOffset va_to_offset, void *context, DieVirtualMapping *result)
{
    uint64_t rva;
    size_t i;
    if (!result) return false;
    result->found = false;
    result->section_index = 0;
    result->file_offset = 0;
    if (!length || length > UINT64_MAX - va || va < image_base
        || section_count > DIE_VIRTUAL_SECTION_MAX
        || (section_count && !sections)
        || (file_backed && section_count && !va_to_offset)) return false;
    rva = va - image_base;
    for (i = 0; i < section_count; ++i) {
        const DieVirtualSection *section = &sections[i];
        uint64_t extent = section->file_size, delta, file_offset = 0;
        if (!file_backed && section->virtual_size > extent)
            extent = section->virtual_size;
        if ((section->flags & required_flags) != required_flags || rva < section->rva)
            continue;
        delta = rva - section->rva;
        if (delta > extent || length > extent - delta) continue;
        if (file_backed) {
            int64_t first, last;
            /* The script stops at the first flag/range-eligible section in
             * this branch. A damaged raw mapping must not be rescued by a
             * later overlapping section; endpoint validation is asymmetric
             * with the flag/extent checks above for that reason. */
            if (delta > UINT64_MAX - section->file_offset) return true;
            file_offset = section->file_offset + delta;
            if (file_offset > file_size || length > file_size - file_offset) return true;
            first = va_to_offset(context, va);
            last = va_to_offset(context, va + length - 1u);
            if (first < 0 || last < 0 || (uint64_t)first != file_offset
                || (uint64_t)last != file_offset + length - 1u) return true;
        }
        result->found = true;
        result->section_index = i;
        result->file_offset = file_offset;
        return true;
    }
    return true;
}
