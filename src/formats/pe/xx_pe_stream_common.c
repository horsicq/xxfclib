/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_pe_stream_internal.h"
#include <limits.h>

bool xx_pe_stream_prepare(Abstractformat *format, xx_pd_struct *pd)
{
    int64_t saved;
    bool ok;
    if (!format || !format->device || xx_pd_is_stopped(pd)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid or cancelled PE stream");
        return false;
    }
    saved = xx_io_tell(format->device);
    if (saved < 0) return false;
    ok = xx_format_handle_base_info(format, pd);
    if (xx_io_seek64(format->device, saved, 0) != 0) ok = false;
    if (!ok) return false;
    if (format->file_type != XX_FILE_TYPE_PE32 && format->file_type != XX_FILE_TYPE_PE64 && format->file_type != XX_FILE_TYPE_DOTNET) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Expected PE format");
        return false;
    }
    return true;
}

bool xx_pe_stream_read(Abstractformat *format, int64_t offset, void *buffer, size_t size, xx_pd_struct *pd)
{
    int64_t total;
    bool ok;
    if (!format || !format->device || (!buffer && size) || xx_pd_is_stopped(pd)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid or cancelled PE read");
        return false;
    }
    total = xx_io_size(format->device);
    if (offset < format->base_address || offset < 0 || offset > total || size > (size_t)PTRDIFF_MAX || (uint64_t)size > (uint64_t)(total - offset)) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "PE stream range outside input");
        return false;
    }
    ok = xx_io_read_at(format->device, offset, buffer, size);
    if (!ok) xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot read PE stream range");
    return ok;
}

bool xx_pe_stream_rva(Abstractformat *format, uint64_t rva, uint64_t size, int64_t *offset, xx_pd_struct *pd)
{
    const xx_memory_map *map;
    uint64_t address;
    int64_t first, last, total;
    if (!format || !offset || !size || rva > UINT32_MAX || size - 1 > UINT32_MAX - rva || xx_pd_is_stopped(pd)) goto invalid;
    map = xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    if (!map || map->module_address == UINT64_MAX || rva > UINT64_MAX - map->module_address || size - 1 > UINT64_MAX - map->module_address - rva) goto invalid;
    address = map->module_address + rva;
    if (!xx_memory_map_is_physical_address_range(map, address, (int64_t)size)) goto invalid;
    first = xx_memory_map_address_to_offset(map, address);
    last = xx_memory_map_address_to_offset(map, address + size - 1);
    total = xx_io_size(format->device);
    if (first < format->base_address || first < 0 || first > total || size > (uint64_t)(total - first) || last < first || (uint64_t)(last - first) != size - 1)
        goto invalid;
    *offset = first;
    return true;
invalid:
    xx_pd_set_error(pd, XXFC_ERR_IO, "Unmapped or invalid PE stream RVA");
    return false;
}

uint64_t xx_pe_get_number_of_metadata(Abstractformat *format, xx_pd_struct *pd)
{
    xx_metadata_state *state = xx_format_create_metadata_reading(format, pd);
    uint64_t count = 0;
    bool failed;
    if (!state) return 0;
    while (xx_format_get_current_metadata(format, state)) {
        ++count;
        if (!xx_format_metadata_move_to_next(format, state, pd)) break;
    }
    failed = state->failed;
    xx_format_free_metadata_reading(format, state);
    if (failed) return 0;
    format->number_of_metadata = count;
    return count;
}
