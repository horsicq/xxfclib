/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/pe/xx_pe.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

#define PE_MACHINE_AMD64 UINT16_C(0x8664)
#define PE_DIR_EXCEPTION 3U
#define PE_UNW_FLAG_EHANDLER 1U
#define PE_UNW_FLAG_UHANDLER 2U
#define PE_UNW_FLAG_CHAININFO 4U
#define PE_UNWIND_CHAIN_LIMIT 32U
#define PE_SCOPE_LIMIT UINT32_C(65536)
#define PE_TRY_LIMIT UINT32_C(4096)
#define PE_CATCH_LIMIT UINT32_C(256)
#define PE_IP_LIMIT UINT32_C(65536)
#define PE_REGION_LIMIT UINT32_C(262144)
#define PE_CXX_MAGIC_MIN UINT32_C(0x19930520)
#define PE_CXX_MAGIC_MAX UINT32_C(0x19930522)

typedef struct pe_exception_stream_s {
    xx_pe_exception_region *regions;
    size_t count;
    size_t capacity;
} pe_exception_stream;

typedef struct pe_runtime_function_s {
    uint32_t begin;
    uint32_t end;
    uint32_t unwind;
} pe_runtime_function;

typedef struct pe_unwind_info_s {
    uint8_t version;
    uint8_t flags;
    uint8_t code_count;
} pe_unwind_info;

static bool pe_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static uint64_t pe_module_address(const xx_pe *pe)
{
    if (!pe) return 0U;
    return pe->format.module_address != XX_INVALID_ADDRESS ? pe->format.module_address : pe->image_base;
}

static bool pe_exception_rva_offset(const xx_pe *pe, const xx_memory_map *map, uint64_t rva, uint64_t size, int64_t *offset)
{
    uint64_t address;
    int64_t current;
    int64_t total;
    if (!pe || !map || !pe->format.device || !size || rva > INT64_MAX || size > INT64_MAX) return false;
    address = xx_memory_map_relative_address_to_address(map, (int64_t)rva);
    if (address == XX_INVALID_ADDRESS || !xx_memory_map_is_physical_address_range(map, address, (int64_t)size)) return false;
    current = xx_memory_map_relative_address_to_offset(map, (int64_t)rva);
    total = xx_io_total_size(pe->format.device);
    if (current < 0 || total < current || (uint64_t)(total - current) < size) return false;
    if (offset) *offset = current;
    return true;
}

static bool pe_read_u32(const xx_pe *pe, const xx_memory_map *map, uint64_t rva, uint32_t *value)
{
    int64_t offset;
    if (!value || !pe_exception_rva_offset(pe, map, rva, 4U, &offset)) return false;
    *value = xx_io_get_u32(pe->format.device, offset, false);
    return true;
}

static bool pe_read_runtime(const xx_pe *pe, const xx_memory_map *map, uint64_t rva, pe_runtime_function *value)
{
    int64_t offset;
    if (!value || !pe_exception_rva_offset(pe, map, rva, 12U, &offset)) return false;
    value->begin = xx_io_get_u32(pe->format.device, offset, false);
    value->end = xx_io_get_u32(pe->format.device, offset + 4, false);
    value->unwind = xx_io_get_u32(pe->format.device, offset + 8, false);
    return true;
}

static bool pe_read_unwind(const xx_pe *pe, const xx_memory_map *map, uint64_t rva, pe_unwind_info *value)
{
    int64_t offset;
    uint8_t version_flags;
    if (!value || !pe_exception_rva_offset(pe, map, rva, 4U, &offset)) return false;
    version_flags = xx_io_get_u8(pe->format.device, offset);
    value->version = version_flags & 7U;
    value->flags = version_flags >> 3U;
    value->code_count = xx_io_get_u8(pe->format.device, offset + 2);
    return true;
}

static bool pe_append_region(pe_exception_stream *stream, const xx_pe_exception_region *region)
{
    xx_pe_exception_region *grown;
    size_t capacity;
    if (!stream || !region || stream->count >= PE_REGION_LIMIT) return false;
    if (stream->count == stream->capacity) {
        capacity = stream->capacity ? stream->capacity * 2U : 16U;
        if (capacity > PE_REGION_LIMIT) capacity = PE_REGION_LIMIT;
        grown = (xx_pe_exception_region *)xx_mem_realloc(stream->regions, capacity * sizeof(*grown));
        if (!grown) return false;
        stream->regions = grown;
        stream->capacity = capacity;
    }
    stream->regions[stream->count++] = *region;
    return true;
}

static void pe_exception_stream_free(void *pointer)
{
    pe_exception_stream *stream = (pe_exception_stream *)pointer;
    if (!stream) return;
    xx_mem_free(stream->regions);
    xx_mem_free(stream);
}

/* Convert the common MSVC RTTI spelling .?AVInner@Outer@@ to Outer::Inner.
 * This deliberately stays small: it is format metadata normalization, rather
 * than a second general-purpose demangler inside xxfclib. */
static void pe_type_name(const xx_pe *pe, const xx_memory_map *map, uint32_t type_rva, char output[256])
{
    char encoded[256];
    char *parts[64];
    size_t length = 0U, count = 0U, used = 0U;
    uint64_t name_rva;
    int64_t offset;
    output[0] = '\0';
    if (!type_rva || !pe_add_u64(type_rva, 16U, &name_rva) || !pe_exception_rva_offset(pe, map, name_rva, 1U, &offset)) return;
    while (length + 1U < sizeof(encoded)) {
        int64_t current;
        uint8_t ch;
        if (!pe_exception_rva_offset(pe, map, name_rva + length, 1U, &current)) return;
        ch = xx_io_get_u8(pe->format.device, current);
        if (!ch) break;
        encoded[length++] = (char)ch;
    }
    encoded[length] = '\0';
    if (length >= 6U && encoded[0] == '.' && encoded[1] == '?' && encoded[2] == 'A' && (encoded[3] == 'V' || encoded[3] == 'U' || encoded[3] == 'T')) {
        char *cursor = encoded + 4;
        while (*cursor && count < sizeof(parts) / sizeof(parts[0])) {
            char *at = strchr(cursor, '@');
            if (!at) break;
            *at = '\0';
            if (!*cursor) break;
            parts[count++] = cursor;
            cursor = at + 1;
        }
        while (count) {
            const char *part = parts[--count];
            size_t part_size = strlen(part);
            if (used && used + 2U < 256U) {
                output[used++] = ':';
                output[used++] = ':';
            }
            if (part_size > 255U - used) part_size = 255U - used;
            memcpy(output + used, part, part_size);
            used += part_size;
            output[used] = '\0';
        }
    } else {
        if (length > 255U) length = 255U;
        memcpy(output, encoded, length);
        output[length] = '\0';
    }
}

static bool pe_cxx_state_range(const uint32_t *ipmap, uint32_t count, uint64_t module, int32_t low, int32_t high, const pe_runtime_function *function, uint64_t *from,
                               uint64_t *to)
{
    uint32_t index;
    bool found = false;
    if (!ipmap || !function || !from || !to) return false;
    for (index = 0U; index < count; ++index) {
        int32_t state = (int32_t)ipmap[index * 2U + 1U];
        if (state >= low && state <= high) {
            if (!found) {
                if (!pe_add_u64(module, ipmap[index * 2U], from)) return false;
                found = true;
            }
        } else if (found) {
            return pe_add_u64(module, ipmap[index * 2U], to) && *to > *from;
        }
    }
    return found && pe_add_u64(module, function->end, to) && *to > *from;
}

static bool pe_cxx_trycatch(const xx_pe *pe, const xx_memory_map *map, pe_exception_stream *stream, const pe_runtime_function *function, uint32_t funcinfo,
                            xx_pd_struct *pd)
{
    uint32_t try_count, try_map, ip_count, ip_map;
    uint32_t *ipmap;
    uint32_t index;
    uint64_t module = pe_module_address(pe);
    if (!pe_read_u32(pe, map, (uint64_t)funcinfo + 12U, &try_count) || !pe_read_u32(pe, map, (uint64_t)funcinfo + 16U, &try_map) ||
        !pe_read_u32(pe, map, (uint64_t)funcinfo + 20U, &ip_count) || !pe_read_u32(pe, map, (uint64_t)funcinfo + 24U, &ip_map))
        return true;
    if (!try_count || !try_map || !ip_count || !ip_map || try_count > PE_TRY_LIMIT || ip_count > PE_IP_LIMIT) return true;
    ipmap = (uint32_t *)xx_mem_alloc((size_t)ip_count * 2U * sizeof(*ipmap));
    if (!ipmap) return false;
    for (index = 0U; index < ip_count; ++index) {
        if (!pe_read_u32(pe, map, (uint64_t)ip_map + (uint64_t)index * 8U, &ipmap[index * 2U]) ||
            !pe_read_u32(pe, map, (uint64_t)ip_map + (uint64_t)index * 8U + 4U, &ipmap[index * 2U + 1U])) {
            xx_mem_free(ipmap);
            return true;
        }
    }
    for (index = 0U; index < try_count && !xx_pd_is_stopped(pd); ++index) {
        uint64_t entry = (uint64_t)try_map + (uint64_t)index * 20U;
        uint32_t low, high, catch_count, handlers, catch_index;
        uint64_t from = 0U, to = 0U;
        if (!pe_read_u32(pe, map, entry, &low) || !pe_read_u32(pe, map, entry + 4U, &high) || !pe_read_u32(pe, map, entry + 12U, &catch_count) ||
            !pe_read_u32(pe, map, entry + 16U, &handlers))
            break;
        if (!catch_count || !handlers || catch_count > PE_CATCH_LIMIT || (int32_t)low > (int32_t)high ||
            !pe_cxx_state_range(ipmap, ip_count, module, (int32_t)low, (int32_t)high, function, &from, &to))
            continue;
        for (catch_index = 0U; catch_index < catch_count; ++catch_index) {
            uint64_t handler_entry = (uint64_t)handlers + (uint64_t)catch_index * 20U;
            uint32_t type_rva, handler_rva;
            xx_pe_exception_region region;
            if (!pe_read_u32(pe, map, handler_entry + 4U, &type_rva) || !pe_read_u32(pe, map, handler_entry + 12U, &handler_rva)) break;
            if (!handler_rva) continue;
            memset(&region, 0, sizeof(region));
            if (!pe_add_u64(module, function->begin, &region.source) || !pe_add_u64(module, handler_rva, &region.handler)) continue;
            region.from = from;
            region.to = to;
            region.kind = XX_PE_EXCEPTION_CATCH;
            region.catch_all = type_rva == 0U;
            if (type_rva) pe_type_name(pe, map, type_rva, region.type);
            if (!pe_append_region(stream, &region)) {
                xx_mem_free(ipmap);
                return false;
            }
        }
    }
    xx_mem_free(ipmap);
    return !xx_pd_is_stopped(pd);
}

static bool pe_seh_trycatch(const xx_pe *pe, const xx_memory_map *map, pe_exception_stream *stream, const pe_runtime_function *function, uint64_t scope_rva,
                            xx_pd_struct *pd)
{
    uint32_t count, index;
    uint64_t module = pe_module_address(pe);
    if (!pe_read_u32(pe, map, scope_rva, &count) || !count || count > PE_SCOPE_LIMIT) return true;
    for (index = 0U; index < count && !xx_pd_is_stopped(pd); ++index) {
        uint64_t at = scope_rva + 4U + (uint64_t)index * 16U;
        uint32_t begin, end, handler, jump;
        xx_pe_exception_region region;
        if (!pe_read_u32(pe, map, at, &begin) || !pe_read_u32(pe, map, at + 4U, &end) || !pe_read_u32(pe, map, at + 8U, &handler) ||
            !pe_read_u32(pe, map, at + 12U, &jump))
            return true;
        if (!begin || !end || begin == UINT32_MAX || end == UINT32_MAX || begin > end || begin < function->begin - (function->begin != 0U) || begin >= function->end ||
            end > function->end + (function->end != UINT32_MAX) || end <= function->begin)
            continue;
        memset(&region, 0, sizeof(region));
        if (!pe_add_u64(module, function->begin, &region.source) || !pe_add_u64(module, begin, &region.from) || !pe_add_u64(module, end, &region.to)) continue;
        if (jump) {
            if (!pe_add_u64(module, jump, &region.handler)) continue;
            if (handler > 1U && !pe_add_u64(module, handler, &region.filter)) continue;
            region.kind = region.filter ? XX_PE_EXCEPTION_FILTER : XX_PE_EXCEPTION_CATCH;
        } else {
            if (!handler || handler == 1U || !pe_add_u64(module, handler, &region.handler)) continue;
            region.kind = XX_PE_EXCEPTION_FINALLY;
        }
        if (!pe_append_region(stream, &region)) return false;
    }
    return !xx_pd_is_stopped(pd);
}

static bool pe_collect_exceptions(xx_pe *pe, pe_exception_stream *stream, xx_pd_struct *pd)
{
    const xx_memory_map *map;
    uint32_t directory_rva, directory_size, known_seh_handler = 0U;
    uint64_t cursor, entries;
    if (!pe || !stream || xx_pd_is_stopped(pd)) return false;
    if (!pe->format.base_info_handled && !xx_pe_handle_base_info(&pe->format, pd)) return false;
    if (pe->machine != PE_MACHINE_AMD64 || pe->number_of_data_directories <= PE_DIR_EXCEPTION) return true;
    directory_rva = pe->data_directory_rva[PE_DIR_EXCEPTION];
    directory_size = pe->data_directory_size[PE_DIR_EXCEPTION];
    if (!directory_rva || directory_size < 12U) return true;
    map = xx_format_get_memory_map(&pe->format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    if (!map) return false;
    entries = directory_size / 12U;
    if (entries > PE_REGION_LIMIT) entries = PE_REGION_LIMIT;
    for (cursor = 0U; cursor < entries && !xx_pd_is_stopped(pd); ++cursor) {
        pe_runtime_function function;
        pe_unwind_info info;
        uint32_t saved_begin, saved_end, handler;
        uint64_t unwind_rva, tail_rva;
        unsigned int chain = 0U;
        if (!pe_read_runtime(pe, map, (uint64_t)directory_rva + cursor * 12U, &function)) break;
        if (!function.begin || function.begin == UINT32_MAX) break;
        saved_begin = function.begin;
        saved_end = function.end;
        while (function.unwind & 1U) {
            if (chain++ >= PE_UNWIND_CHAIN_LIMIT || !pe_read_runtime(pe, map, function.unwind & ~UINT32_C(1), &function)) break;
        }
        if (function.unwind & 1U) continue;
        function.begin = saved_begin;
        function.end = saved_end;
        unwind_rva = function.unwind;
        if (!pe_read_unwind(pe, map, unwind_rva, &info) || info.version != 1U ||
            (!(info.flags & (PE_UNW_FLAG_EHANDLER | PE_UNW_FLAG_UHANDLER)) && !(info.flags & PE_UNW_FLAG_CHAININFO)))
            continue;
        tail_rva = unwind_rva + 4U + (uint64_t)((info.code_count + 1U) & ~1U) * 2U;
        if (info.flags & PE_UNW_FLAG_CHAININFO) {
            unsigned int chained = 0U;
            do {
                if (chained++ >= PE_UNWIND_CHAIN_LIMIT || !pe_read_runtime(pe, map, tail_rva, &function)) break;
                chain = 0U;
                while (function.unwind & 1U) {
                    if (chain++ >= PE_UNWIND_CHAIN_LIMIT || !pe_read_runtime(pe, map, function.unwind & ~UINT32_C(1), &function)) break;
                }
                if ((function.unwind & 1U) || !pe_read_unwind(pe, map, function.unwind, &info) || info.version != 1U) break;
                unwind_rva = function.unwind;
                tail_rva = unwind_rva + 4U + (uint64_t)((info.code_count + 1U) & ~1U) * 2U;
            } while (info.flags & PE_UNW_FLAG_CHAININFO);
            if (info.flags & PE_UNW_FLAG_CHAININFO || !(info.flags & (PE_UNW_FLAG_EHANDLER | PE_UNW_FLAG_UHANDLER))) continue;
            function.begin = saved_begin;
            function.end = saved_end;
        }
        if (!pe_read_u32(pe, map, tail_rva, &handler)) continue;
        tail_rva += 4U;
        if (handler != known_seh_handler) {
            uint32_t funcinfo, magic;
            if (pe_read_u32(pe, map, tail_rva, &funcinfo) && pe_read_u32(pe, map, funcinfo, &magic) && magic >= PE_CXX_MAGIC_MIN && magic <= PE_CXX_MAGIC_MAX) {
                if (!pe_cxx_trycatch(pe, map, stream, &function, funcinfo, pd)) return false;
                continue;
            }
        }
        if (known_seh_handler && known_seh_handler != handler) continue;
        {
            size_t before = stream->count;
            if (!pe_seh_trycatch(pe, map, stream, &function, tail_rva, pd)) return false;
            if (stream->count != before) known_seh_handler = handler;
        }
    }
    return !xx_pd_is_stopped(pd);
}

xx_pe_exception_state *xx_pe_create_exception_regions_reading(Abstractformat *format, xx_pd_struct *pd)
{
    xx_pe_exception_state *state;
    pe_exception_stream *stream;
    if (!format || xx_pd_is_stopped(pd)) return NULL;
    state = (xx_pe_exception_state *)xx_mem_calloc(1U, sizeof(*state));
    stream = (pe_exception_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        xx_mem_free(state);
        xx_mem_free(stream);
        return NULL;
    }
    state->format = format;
    state->current_index = -1;
    state->total_regions = -1;
    state->internal_state = stream;
    state->free_internal = pe_exception_stream_free;
    if (!pe_collect_exceptions((xx_pe *)format, stream, pd)) {
        xx_pe_free_exception_regions_reading(format, state);
        return NULL;
    }
    state->total_regions = (int64_t)stream->count;
    (void)xx_pe_exception_region_move_to_next(format, state, pd);
    return state;
}

const xx_pe_exception_region *xx_pe_get_current_exception_region(Abstractformat *format, xx_pe_exception_state *state)
{
    return state && state->format == format && state->has_region ? &state->current_region : NULL;
}

bool xx_pe_exception_region_move_to_next(Abstractformat *format, xx_pe_exception_state *state, xx_pd_struct *pd)
{
    pe_exception_stream *stream;
    int64_t next;
    if (!format || !state || state->format != format || xx_pd_is_stopped(pd) || !(stream = (pe_exception_stream *)state->internal_state)) {
        if (state) state->has_region = false;
        return false;
    }
    next = state->current_index + 1;
    if (next < 0 || (uint64_t)next >= stream->count) {
        state->has_region = false;
        return false;
    }
    state->current_region = stream->regions[next];
    state->current_index = next;
    state->has_region = true;
    return true;
}

void xx_pe_free_exception_regions_reading(Abstractformat *format, xx_pe_exception_state *state)
{
    (void)format;
    if (!state) return;
    if (state->free_internal && state->internal_state) state->free_internal(state->internal_state);
    xx_mem_free(state);
}

uint64_t xx_pe_get_number_of_exception_regions(Abstractformat *format, xx_pd_struct *pd)
{
    xx_pe_exception_state *state = xx_pe_create_exception_regions_reading(format, pd);
    uint64_t result = state && state->total_regions > 0 ? (uint64_t)state->total_regions : 0U;
    xx_pe_free_exception_regions_reading(format, state);
    return result;
}
