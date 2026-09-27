/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef DIE_ENGINE_PRIMITIVES_H
#define DIE_ENGINE_PRIMITIVES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIE_BYTES_PATTERN_MAX 128u
#define DIE_BYTES_PATTERN_BUDGET 65536u
#define DIE_RELATION_GROUP_MAX 32u
#define DIE_RELATION_PAIR_MAX 256u
#define DIE_RELATION_RANGE_MAX (16u * 1024u * 1024u)
#define DIE_VIRTUAL_SECTION_MAX 65535u

typedef struct {
    const uint8_t *data;
    size_t size;
} DieBytePattern;

typedef struct {
    bool found;
    uint64_t offset;
    size_t pattern_index;
} DieBytesMatch;

/* Exact bytes, strict half-open range. True with found=false means no hit;
 * false means invalid bounds, pointers or pattern limits. No allocation. */
bool die_find_any_bytes(const void *data, size_t data_size,
    uint64_t offset, uint64_t length, const DieBytePattern *patterns,
    size_t pattern_count, DieBytesMatch *result);

typedef struct {
    uint32_t a;
    uint32_t b;
} DieByteRelationPair;

typedef struct {
    const DieByteRelationPair *pairs;
    size_t count;
} DieByteRelationGroup;

/* All pairs within a group are ANDed; groups are ORed. Candidate starts obey
 * the exclusive p < length - tail_bytes rule. Pair offsets must be no greater
 * than tail_bytes. Empty groups are invalid; an empty group list is valid.
 * True returns a flat [relative_offset,group_mask,...] allocation (or NULL/0
 * for no candidates). False means invalid input, budget or allocation failure.
 * Exact allocation requires two bounded passes; maximum output is 128 MiB.
 * Release successful values with die_byte_relation_results_free(). */
bool die_find_byte_relation_candidates(const void *data, size_t data_size,
    uint64_t offset, uint64_t length, const DieByteRelationGroup *groups,
    size_t group_count, uint64_t tail_bytes, uint32_t **values,
    size_t *value_count);

void die_byte_relation_results_free(uint32_t *values);

typedef struct {
    uint64_t rva;
    uint64_t virtual_size;
    uint64_t file_size;
    uint64_t file_offset;
    uint32_t flags;
} DieVirtualSection;

typedef struct {
    bool found;
    size_t section_index;
    uint64_t file_offset; /* Present only for file-backed mappings. */
} DieVirtualMapping;

typedef int64_t (*DieVAToOffset)(void *context, uint64_t va);

/* Sections retain their original order. A file-backed match additionally
 * confirms both inclusive endpoints through the legacy VA-to-offset callback.
 * Raw EOF/mapping failure of the first flag/range-eligible section stops the
 * search, preserving the script's behavior for overlapping sections.
 * False means invalid arguments; true+found=false means no eligible section. */
bool die_map_virtual_range(uint64_t image_base, uint64_t file_size,
    const DieVirtualSection *sections, size_t section_count, uint64_t va,
    uint64_t length, uint32_t required_flags, bool file_backed,
    DieVAToOffset va_to_offset, void *context, DieVirtualMapping *result);

#endif
