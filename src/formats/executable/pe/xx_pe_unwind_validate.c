/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/pe/xx_pe_unwind_validate.h"
#include "xxfclib/rt/xx_rt.h"

/* These checks deliberately mirror the generic DIE heuristic, including its
 * permissive DWORD-aligned pointers. They do not attempt to validate the ABI
 * more strictly than the script does. */
typedef struct {
    uint64_t begin;
    uint64_t end;
    uint64_t maximum_end;
    size_t maximum_index;
} unwind_mapped_range;

typedef struct {
    const xx_pe_inspection *pe;
    xx_io_device *device;
    uint64_t file_size;
} unwind_context;

static uint32_t unwind_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int unwind_compare_ranges(const void *left, const void *right)
{
    const unwind_mapped_range *a = (const unwind_mapped_range *)left;
    const unwind_mapped_range *b = (const unwind_mapped_range *)right;
    return a->begin < b->begin ? -1 : a->begin > b->begin ? 1 : 0;
}

/* Keep the script's first-containing-section rule. A mapping mismatch in
 * that section must fail rather than falling through to an overlapping one.
 * PE.VAToOffset uses LAST_PHYSICAL, unlike the inspection RVA helper. */
static int unwind_file_backed(const unwind_context *context, uint64_t rva, uint64_t size, int64_t *offset)
{
    int i;
    if (size == 0) return 0;
    for (i = 0; i < context->pe->nSectionCount; i++) {
        const xx_pe_inspect_section *section = context->pe->pSections + i;
        uint64_t section_rva = section->nVirtualAddress;
        uint64_t relative;
        uint64_t raw_offset;
        uint64_t address;
        if (rva < section_rva) continue;
        relative = rva - section_rva;
        if (relative > section->nSizeOfRawData || size > (uint64_t)section->nSizeOfRawData - relative) continue;
        raw_offset = (uint64_t)section->nPointerToRawData + relative;
        if (raw_offset > context->file_size || size > context->file_size - raw_offset) return 0;
        address = context->pe->map.module_address + rva;
        if (xx_memory_map_address_to_offset_ex(&context->pe->map, address, XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL) != (int64_t)raw_offset ||
            xx_memory_map_address_to_offset_ex(&context->pe->map, address + size - 1, XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL) != (int64_t)(raw_offset + size - 1))
            return 0;
        *offset = (int64_t)raw_offset;
        return 1;
    }
    return 0;
}

/* Mirrors isVirtualAddressRangeFileBacked(imageBase + rva, 1,
 * IMAGE_SCN_MEM_EXECUTE). Non-executable sections are skipped, while a
 * translation mismatch in the first executable section that contains the RVA
 * fails immediately instead of falling through to an overlapping section. */
static int guard_cf_function_file_backed(const unwind_context *context, uint32_t rva)
{
    int i;
    for (i = 0; i < context->pe->nSectionCount; i++) {
        const xx_pe_inspect_section *section = context->pe->pSections + i;
        uint64_t section_rva = section->nVirtualAddress;
        uint64_t relative;
        uint64_t raw_offset;
        uint64_t address;
        if ((section->nCharacteristics & UINT32_C(0x20000000)) == 0 || rva < section_rva) continue;
        relative = (uint64_t)rva - section_rva;
        if (relative >= section->nSizeOfRawData) continue;
        raw_offset = (uint64_t)section->nPointerToRawData + relative;
        if (raw_offset >= context->file_size) return 0;
        address = context->pe->map.module_address + rva;
        return xx_memory_map_address_to_offset_ex(&context->pe->map, address, XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL) == (int64_t)raw_offset;
    }
    return 0;
}

static size_t unwind_find_range(const unwind_mapped_range *ranges, size_t count, uint64_t begin, uint64_t end)
{
    size_t lower = 0;
    size_t upper = count;
    while (lower < upper) {
        size_t middle = lower + (upper - lower) / 2;
        if (ranges[middle].begin <= begin) lower = middle + 1;
        else upper = middle;
    }
    if (lower != 0 && ranges[lower - 1].maximum_end >= end) return ranges[lower - 1].maximum_index;
    return (size_t)-1;
}

static int unwind_range_contains(const unwind_mapped_range *ranges, size_t count, size_t *cached, uint64_t begin, uint64_t end)
{
    size_t index = *cached;
    if (index < count && begin >= ranges[index].begin && end <= ranges[index].end) return 1;
    index = unwind_find_range(ranges, count, begin, end);
    if (index == (size_t)-1) return 0;
    *cached = index;
    return 1;
}

/* What an entry's UNWIND_INFO declares about its frame.
 *
 * The record is a version and five flag bits, the prologue's length, a count
 * of unwind codes, and a frame register with its scaled offset, followed by
 * that many two-byte codes. Only the codes that describe the frame itself are
 * decoded -- the pushes and the two allocation forms -- because those are what
 * a caller reconstructing the frame needs; the saves of registers and XMM
 * lanes are skipped by their documented lengths.
 *
 * A chained entry (UNW_FLAG_CHAININFO) carries a RUNTIME_FUNCTION after its
 * codes naming the entry it continues, which is followed to the primary. The
 * chain is bounded, so a record that points at itself or forms a cycle stops
 * rather than looping. Returns 1 when the record was read whole. */
static int unwind_describe(const unwind_context *context, uint32_t unwind_rva, xx_pe_amd64_function *out, uint32_t own_begin)
{
    uint32_t depth;
    uint32_t begin = own_begin;
    for (depth = 0; depth < 8u; depth++) {
        unsigned char header[4];
        unsigned char codes[2 * 255];
        int64_t offset;
        uint32_t count;
        uint32_t at;
        unsigned flags;
        if ((unwind_rva & 3u) != 0) return 0;
        if (!unwind_file_backed(context, unwind_rva, 4, &offset)) return 0;
        if (!xx_io_read_at(context->device, offset, header, sizeof(header))) return 0;
        count = header[2];
        if (count != 0) {
            if (!unwind_file_backed(context, unwind_rva + 4u, count * 2u, &offset)) return 0;
            if (!xx_io_read_at(context->device, offset, codes, count * 2u)) return 0;
        }
        flags = (unsigned)(header[0] >> 3) & 0x1fu;
        if (depth == 0) {
            out->unwind_version = (uint8_t)(header[0] & 7u);
            out->unwind_flags = (uint8_t)flags;
            out->prolog_size = header[1];
            out->frame_register = (uint8_t)(header[3] & 0x0fu);
            out->frame_offset = (uint8_t)((header[3] >> 4) & 0x0fu);
            out->unwind_read = 1;
            for (at = 0; at + 1 < count * 2u;) {
                unsigned operation = codes[at + 1] & 0x0fu;
                unsigned info = (unsigned)(codes[at + 1] >> 4) & 0x0fu;
                unsigned slots = 1;
                if (operation == 0) { /* PUSH_NONVOL */
                    if (out->pushed_registers < 255u) out->pushed_registers++;
                } else if (operation == 1) { /* ALLOC_LARGE */
                    slots = info == 0 ? 2u : 3u;
                    if (at + slots * 2u > count * 2u) break;
                    if (info == 0) out->frame_allocation += (uint32_t)((uint32_t)codes[at + 2] | ((uint32_t)codes[at + 3] << 8)) * 8u;
                    else out->frame_allocation += unwind_u32(codes + at + 2);
                } else if (operation == 2) { /* ALLOC_SMALL */
                    out->frame_allocation += info * 8u + 8u;
                } else if (operation == 4 || operation == 8) { /* SAVE_NONVOL, SAVE_XMM128 */
                    slots = 2;
                } else if (operation == 5 || operation == 9) { /* the FAR forms */
                    slots = 3;
                } else if (operation == 10) { /* PUSH_MACHFRAME */
                    slots = 1;
                }
                at += slots * 2u;
            }
        }
        if ((flags & XX_PE_AMD64_UNW_CHAININFO) == 0) {
            out->primary_begin_rva = begin;
            return 1;
        }
        /* The chained RUNTIME_FUNCTION follows the codes, padded to a DWORD. */
        at = unwind_rva + 4u + ((count + 1u) & ~1u) * 2u;
        {
            unsigned char parent[12];
            uint32_t next;
            if (!unwind_file_backed(context, at, 12, &offset)) return 0;
            if (!xx_io_read_at(context->device, offset, parent, sizeof(parent))) return 0;
            next = unwind_u32(parent + 8);
            begin = unwind_u32(parent);
            if (next == unwind_rva || next == 0) {
                out->primary_begin_rva = begin;
                return 1;
            }
            unwind_rva = next;
        }
    }
    out->primary_begin_rva = begin;
    return 1;
}

int xx_pe_inspect_amd64_functions(const xx_pe_inspection *pe, xx_io_device *device, xx_pe_amd64_function *functions, uint32_t capacity, uint32_t *total)
{
    unwind_context context;
    uint32_t directory_rva;
    uint32_t directory_size;
    uint32_t count;
    uint32_t index;
    uint32_t stored = 0;
    int64_t base;
    if (total != NULL) *total = 0;
    if (pe == NULL || device == NULL || !pe->bValid || pe->nNumberOfRvaAndSizes <= 3) return 0;
    if (capacity != 0 && functions == NULL) return 0;
    directory_rva = pe->pDirRVA[3];
    directory_size = pe->pDirSize[3];
    if (directory_rva == 0 || directory_size < 12) return 0;
    context.pe = pe;
    context.device = device;
    context.file_size = (uint64_t)xx_io_get_size(device);
    if (!unwind_file_backed(&context, directory_rva, 12, &base)) return 0;
    count = directory_size / 12;
    for (index = 0; index < count; index++) {
        unsigned char bytes[12];
        int64_t offset;
        uint32_t begin;
        uint32_t end;
        if (!unwind_file_backed(&context, directory_rva + index * 12u, 12, &offset)) return 0;
        if (!xx_io_read_at(device, offset, bytes, sizeof(bytes))) return 0;
        begin = unwind_u32(bytes);
        end = unwind_u32(bytes + 4);
        /* A padded tail ends the table. */
        if (begin == 0 && end == 0 && unwind_u32(bytes + 8) == 0) break;
        if (end <= begin) return 0;
        if (stored < capacity) {
            xx_pe_amd64_function *entry = functions + stored;
            xx_rt_memset(entry, 0, sizeof(*entry));
            entry->begin_rva = begin;
            entry->end_rva = end;
            entry->unwind_rva = unwind_u32(bytes + 8);
            entry->primary_begin_rva = begin;
            if (!unwind_describe(&context, entry->unwind_rva, entry, begin)) {
                /* Unreadable unwind data is reported as unread, not guessed:
                 * the directory's begin and end still stand on their own. */
                entry->unwind_read = 0;
                entry->primary_begin_rva = begin;
            }
        }
        stored++;
    }
    if (total != NULL) *total = stored;
    return 1;
}

/* 1 = structurally valid, 0 = invalid, -1 = I/O unavailable. The largest
 * supported record is four header bytes, 256 code slots, and a 12-byte chain. */
static int unwind_word_aligned_info(const unwind_context *context, uint32_t rva)
{
    unsigned char bytes[528];
    int64_t offset;
    unsigned int flags;
    unsigned int count;
    unsigned int prolog_size;
    unsigned int size;
    unsigned int index;
    if (!unwind_file_backed(context, rva, 4, &offset)) return 0;
    if (!xx_io_read_at(context->device, offset, bytes, 4)) return -1;
    flags = bytes[0] >> 3;
    if ((bytes[0] & 7) != 1 || flags > 7 || ((flags & 4) != 0 && (flags & 3) != 0) || ((bytes[3] & 15) == 0 && (bytes[3] >> 4) != 0)) return 0;
    prolog_size = bytes[1];
    count = bytes[2];
    size = 4 + (count + count % 2) * 2;
    if ((flags & 4) != 0) size += 12;
    else if ((flags & 3) != 0) size += 4;
    if (!unwind_file_backed(context, rva, size, &offset)) return 0;
    if (!xx_io_read_at(context->device, offset, bytes, size)) return -1;
    index = 0;
    while (index < count) {
        unsigned int code_offset = bytes[4 + index * 2];
        unsigned int operation = bytes[5 + index * 2] & 15;
        unsigned int info = bytes[5 + index * 2] >> 4;
        unsigned int slots = 1;
        if (code_offset > prolog_size) return 0;
        if (operation == 1) {
            if (info == 0) slots = 2;
            else if (info == 1) slots = 3;
            else return 0;
        } else if (operation == 3) {
            if (info != 0) return 0;
        } else if (operation == 4 || operation == 8) {
            slots = 2;
        } else if (operation == 5 || operation == 9) {
            slots = 3;
        } else if (operation == 6 || operation == 7 || operation > 10 || (operation == 10 && info > 1)) {
            return 0;
        }
        if (index + slots > count) return 0;
        index += slots;
    }
    return 1;
}

int xx_pe_inspect_validate_amd64_unwind(const xx_pe_inspection *pe, xx_io_device *device, uint32_t directory_rva, uint32_t directory_size,
                                        xx_pe_amd64_unwind_result *result)
{
    unwind_context context;
    unwind_mapped_range *ranges = NULL;
    unsigned char *buffer = NULL;
    size_t count;
    size_t i;
    size_t cached_body = (size_t)-1;
    size_t cached_pointer = (size_t)-1;
    size_t maximum_index = (size_t)-1;
    uint64_t maximum_end = 0;
    uint64_t position;
    int64_t directory_offset;
    int64_t file_size;
    size_t buffer_size;
    int completed = 0;
    if (!result) return 0;
    xx_rt_memset(result, 0, sizeof(*result));
    if (!pe || !device || !directory_rva || !directory_size || pe->nSectionCount < 0 || (pe->nSectionCount && !pe->pSections) ||
        (pe->map.record_count && !pe->map.records))
        return 0;
    /* Every address used by the script must remain an exact JavaScript
     * integer. Let it handle exotic image bases where Number rounding could
     * change a range or translation; never substitute native integer math. */
    if (pe->map.module_address > UINT64_C(9007199254740991) - UINT64_C(8589934590)) return 0;
    file_size = xx_io_total_size(device);
    if (file_size < 0 || (uint64_t)file_size > UINT64_C(9007199254740991)) return 0;
    context.pe = pe;
    context.device = device;
    context.file_size = (uint64_t)file_size;
    if (!unwind_file_backed(&context, directory_rva, directory_size, &directory_offset)) return 0;
    if (directory_size < 12 || directory_size % 12 != 0) {
        result->status = XX_PE_AMD64_UNWIND_INCOMPLETE_ENTRY;
        return 1;
    }
    count = (size_t)pe->nSectionCount;
    if (count > (size_t)-1 / sizeof(*ranges)) return 0;
    ranges = (unwind_mapped_range *)xx_rt_malloc(count * sizeof(*ranges));
    buffer_size = directory_size < UINT32_C(49152) ? (size_t)directory_size : (size_t)49152;
    buffer = (unsigned char *)xx_rt_malloc(buffer_size);
    if (!ranges || !buffer) goto cleanup;
    for (i = 0; i < count; i++) {
        const xx_pe_inspect_section *section = pe->pSections + i;
        uint32_t mapped_size = section->nVirtualSize > section->nSizeOfRawData ? section->nVirtualSize : section->nSizeOfRawData;
        ranges[i].begin = section->nVirtualAddress;
        ranges[i].end = ranges[i].begin + mapped_size;
    }
    xx_rt_qsort(ranges, count, sizeof(*ranges), unwind_compare_ranges);
    for (i = 0; i < count; i++) {
        if (ranges[i].end > maximum_end) {
            maximum_end = ranges[i].end;
            maximum_index = i;
        }
        ranges[i].maximum_end = maximum_end;
        ranges[i].maximum_index = maximum_index;
    }
    for (position = 0; position < directory_size; position += buffer_size) {
        uint64_t remaining = (uint64_t)directory_size - position;
        size_t bytes = remaining < buffer_size ? (size_t)remaining : buffer_size;
        if (!xx_io_read_at(device, directory_offset + (int64_t)position, buffer, bytes)) goto cleanup;
        for (i = 0; i < bytes; i += 12) {
            uint32_t begin = unwind_u32(buffer + i);
            uint32_t end = unwind_u32(buffer + i + 4);
            uint32_t unwind = unwind_u32(buffer + i + 8);
            int body_valid = 0;
            int pointer_valid = begin == 0 && end == 0 && unwind == 0;
            if (end > begin) body_valid = unwind_range_contains(ranges, count, &cached_body, begin, end);
            if ((unwind & 1) != 0) {
                uint64_t indirect = (uint64_t)unwind - 1;
                pointer_valid = indirect >= directory_rva && indirect - directory_rva + 12 <= directory_size && (indirect - directory_rva) % 12 == 0;
            } else if (unwind_range_contains(ranges, count, &cached_pointer, unwind, (uint64_t)unwind + 4)) {
                if ((unwind & 3) == 0) pointer_valid = 1;
                else {
                    pointer_valid = unwind_word_aligned_info(&context, unwind);
                    if (pointer_valid < 0) goto cleanup;
                }
            }
            if ((end > begin && !body_valid) || !pointer_valid) {
                result->status = end > begin && !body_valid ? XX_PE_AMD64_UNWIND_UNMAPPED_BODY : XX_PE_AMD64_UNWIND_INVALID_UNWIND;
                result->entry_index = (uint32_t)((position + i) / 12);
                result->begin_rva = begin;
                result->end_rva = end;
                result->unwind_rva = unwind;
                completed = 1;
                goto cleanup;
            }
        }
    }
    completed = 1;
cleanup:
    xx_rt_free(buffer);
    xx_rt_free(ranges);
    return completed;
}

int xx_pe_inspect_validate_guard_cf_function_table(const xx_pe_inspection *pe, xx_io_device *device, uint32_t table_rva, uint32_t entry_count, uint32_t entry_size,
                                                   xx_pe_guard_cf_result *result)
{
    enum {
        GUARD_CF_BUFFER_LIMIT = 49152
    };
    unwind_context context;
    unsigned char *buffer = NULL;
    uint64_t table_size;
    uint32_t entries_per_chunk;
    uint32_t entry_index = 0;
    uint32_t previous_rva = 0;
    int have_previous = 0;
    int64_t table_offset;
    int64_t file_size;
    int completed = 0;

    if (!result) return 0;
    xx_rt_memset(result, 0, sizeof(*result));
    if (!pe || !device || !entry_count || entry_size < 4 || entry_size > 19 || pe->nSectionCount < 0 || (pe->nSectionCount && !pe->pSections) ||
        (pe->map.record_count && !pe->map.records))
        return 0;
    /* The script performs these operations as exact JavaScript Numbers. Let
     * it retain control for theoretical images whose VAs cannot be represented
     * exactly instead of changing the verdict with native integer arithmetic. */
    if (pe->map.module_address > UINT64_C(9007199254740991) - UINT32_MAX) return 0;
    file_size = xx_io_total_size(device);
    if (file_size < 0 || (uint64_t)file_size > UINT64_C(9007199254740991)) return 0;
    table_size = (uint64_t)entry_count * entry_size;
    context.pe = pe;
    context.device = device;
    context.file_size = (uint64_t)file_size;
    if (!unwind_file_backed(&context, table_rva, table_size, &table_offset)) return 0;

    entries_per_chunk = GUARD_CF_BUFFER_LIMIT / entry_size;
    if (!entries_per_chunk) return 0;
    buffer = (unsigned char *)xx_rt_malloc((size_t)entries_per_chunk * entry_size);
    if (!buffer) return 0;

    while (entry_index < entry_count) {
        uint32_t chunk_entries = entry_count - entry_index;
        uint32_t local_index;
        size_t chunk_size;
        if (chunk_entries > entries_per_chunk) chunk_entries = entries_per_chunk;
        chunk_size = (size_t)chunk_entries * entry_size;
        if (!xx_io_read_at(device, table_offset + (int64_t)((uint64_t)entry_index * entry_size), buffer, chunk_size)) goto cleanup;
        for (local_index = 0; local_index < chunk_entries; local_index++) {
            uint32_t function_rva = unwind_u32(buffer + (size_t)local_index * entry_size);
            uint32_t absolute_index = entry_index + local_index;
            if (have_previous && function_rva < previous_rva) {
                result->status = XX_PE_GUARD_CF_OUT_OF_ORDER;
                result->entry_index = absolute_index;
                result->function_rva = function_rva;
                completed = 1;
                goto cleanup;
            }
            if (!guard_cf_function_file_backed(&context, function_rva)) {
                result->status = XX_PE_GUARD_CF_NOT_EXECUTABLE;
                result->entry_index = absolute_index;
                result->function_rva = function_rva;
                completed = 1;
                goto cleanup;
            }
            previous_rva = function_rva;
            have_previous = 1;
        }
        entry_index += chunk_entries;
    }
    completed = 1;

cleanup:
    xx_rt_free(buffer);
    return completed;
}
