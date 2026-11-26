/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PE_IMPORT_EXPORT_INTERNAL_H
#define XX_PE_IMPORT_EXPORT_INTERNAL_H

#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>

#define PE_SYMBOL_STRING_LIMIT UINT32_C(65536)

typedef struct pe_symbol_input {
    xx_io_device *device;
    xx_memory_map map;
    int64_t size;
    uint64_t image_base;
    uint32_t directory_rva[16];
    uint32_t directory_size[16];
    unsigned int thunk_size;
} pe_symbol_input;

static inline bool pe_symbol_error(xx_pd_struct *pd, const char *message)
{
    xx_pd_set_error(pd, XXFC_ERR_IO, message);
    return false;
}

static inline bool pe_symbol_cancelled_error(xx_pd_struct *pd)
{
    xx_pd_set_error(pd, XXFC_ERR_GENERIC, "PE symbol stream cancelled");
    return false;
}

static inline uint16_t pe_symbol_u16(const unsigned char *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static inline uint32_t pe_symbol_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static inline uint64_t pe_symbol_u64(const unsigned char *bytes)
{
    return (uint64_t)pe_symbol_u32(bytes) | ((uint64_t)pe_symbol_u32(bytes + 4U) << 32U);
}

static inline void pe_symbol_input_cleanup(pe_symbol_input *input)
{
    if (input) xx_memory_map_cleanup(&input->map);
}

static inline bool pe_symbol_input_init(pe_symbol_input *input, Abstractformat *format, xx_pd_struct *pd)
{
    const xx_memory_map *source;
    const xx_pe *pe;
    int64_t position;
    size_t i;
    bool success = false;
    if (!input || !format || !format->device) return false;
    if (xx_pd_is_stopped(pd)) return pe_symbol_cancelled_error(pd);
    xx_memory_map_init(&input->map);
    input->device = format->device;
    input->size = xx_io_total_size(input->device);
    position = xx_io_tell(input->device);
    if (position < 0 || input->size < 0) return pe_symbol_error(pd, "PE symbol streams require a seekable, sized device");
    if ((!format->base_info_handled && !xx_format_handle_base_info(format, pd)) || !format->is_valid) goto finished;
    source = xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    if (!source) goto finished;
    input->map = *source;
    input->map.records = NULL;
    input->map.record_count = input->map.record_capacity = 0U;
    for (i = 0U; i < source->record_count; ++i) {
        if (xx_pd_is_stopped(pd) || !xx_memory_map_add_record(&input->map, &source->records[i])) goto finished;
    }
    pe = (const xx_pe *)format;
    input->image_base = pe->image_base;
    input->thunk_size = xx_pe_is_64(pe) ? 8U : 4U;
    xx_mem_copy(input->directory_rva, pe->data_directory_rva, sizeof(input->directory_rva));
    xx_mem_copy(input->directory_size, pe->data_directory_size, sizeof(input->directory_size));
    success = true;
finished:
    if (xx_io_seek64(input->device, position, 0) != 0) return pe_symbol_error(pd, "Could not restore the PE input cursor");
    if (xx_pd_is_stopped(pd)) return pe_symbol_cancelled_error(pd);
    if (!success) pe_symbol_error(pd, "Could not initialize the PE symbol stream");
    return success;
}

static inline int64_t pe_symbol_offset(const pe_symbol_input *input, uint64_t rva, uint64_t size)
{
    uint64_t address;
    int64_t offset;
    if (!input || !size || rva > UINT32_MAX || size > UINT64_C(0x100000000) - rva || size > (uint64_t)INT64_MAX) return -1;
    address = xx_memory_map_relative_address_to_address(&input->map, (int64_t)rva);
    if (address == XX_INVALID_ADDRESS || !xx_memory_map_is_physical_address_range(&input->map, address, (int64_t)size)) return -1;
    offset = xx_memory_map_relative_address_to_offset(&input->map, (int64_t)rva);
    if (offset < 0 || offset > input->size || size > (uint64_t)(input->size - offset)) return -1;
    return offset;
}

static inline bool pe_symbol_read(pe_symbol_input *input, uint64_t rva, void *bytes, size_t size, xx_pd_struct *pd)
{
    int64_t offset;
    if (xx_pd_is_stopped(pd)) return false;
    offset = pe_symbol_offset(input, rva, (uint64_t)size);
    if (offset < 0 || !xx_io_read_at(input->device, offset, bytes, size)) return pe_symbol_error(pd, "Invalid or truncated PE symbol table");
    return true;
}

static inline char *pe_symbol_duplicate(const char *source, xx_pd_struct *pd)
{
    size_t size = 0U;
    char *result;
    while (source[size]) ++size;
    result = (char *)xx_mem_alloc(size + 1U);
    if (!result) {
        pe_symbol_error(pd, "Could not allocate a PE symbol string");
        return NULL;
    }
    xx_mem_copy(result, source, size + 1U);
    return result;
}

/* Read bounded chunks, shrinking at mapping boundaries. In particular, a NUL
 * beyond a physical section never legitimizes an unterminated string. */
static inline char *pe_symbol_string(pe_symbol_input *input, uint64_t rva, uint32_t maximum, xx_pd_struct *pd)
{
    char *result = NULL;
    size_t used = 0U;
    size_t capacity = 0U;
    if (maximum > PE_SYMBOL_STRING_LIMIT) maximum = PE_SYMBOL_STRING_LIMIT;
    while (used < maximum && !xx_pd_is_stopped(pd)) {
        unsigned char bytes[128];
        size_t amount = maximum - used;
        size_t length;
        bool terminated = false;
        char *replacement;
        if (amount > sizeof(bytes)) amount = sizeof(bytes);
        while (amount && pe_symbol_offset(input, rva + used, amount) < 0) amount /= 2U;
        if (!amount || !pe_symbol_read(input, rva + used, bytes, amount, pd)) break;
        for (length = 0U; length < amount; ++length) {
            if (!bytes[length]) {
                terminated = true;
                break;
            }
        }
        if (used + length + 1U > capacity) {
            capacity = used + length + 1U;
            replacement = (char *)xx_mem_realloc(result, capacity);
            if (!replacement) {
                xx_mem_free(result);
                pe_symbol_error(pd, "Could not allocate a PE symbol string");
                return NULL;
            }
            result = replacement;
        }
        xx_mem_copy(result + used, bytes, length);
        used += length;
        result[used] = '\0';
        if (terminated) return result;
    }
    xx_mem_free(result);
    if (!xx_pd_is_stopped(pd)) pe_symbol_error(pd, "Invalid or unterminated PE symbol string");
    return NULL;
}

#endif
