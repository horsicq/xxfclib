/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/le/xx_le.h"
#include "xx_linear_internal.h"
#include "xx_le_defs.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct xx_linear_data_stream_s {
    xx_data_struct *items;
    size_t count;
} xx_linear_data_stream;

typedef struct xx_linear_record_stream_s {
    const xx_data_struct_field_desc *fields;
    size_t count;
} xx_linear_record_stream;

static bool xx_linear_range(const Abstractformat *format, uint64_t relative,
                            uint64_t size, int64_t *absolute) {
    int64_t total;
    uint64_t available;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    if (relative > available || size > available - relative ||
        relative > (uint64_t)(INT64_MAX - format->base_address)) return false;
    if (absolute) *absolute = format->base_address + (int64_t)relative;
    return true;
}

static bool xx_linear_add_u64(uint64_t left, uint64_t right,
                              uint64_t *result) {
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool xx_linear_mul_u64(uint64_t left, uint64_t right,
                              uint64_t *result) {
    if (!result || (left != 0U && right > UINT64_MAX / left)) return false;
    *result = left * right;
    return true;
}

static void xx_linear_parsed_cleanup(xx_linear_executable *linear) {
    if (!linear) return;
    if (linear->objects) {
        xx_mem_free(linear->objects);
        linear->objects = NULL;
    }
    if (linear->pages) {
        xx_mem_free(linear->pages);
        linear->pages = NULL;
    }
}

static xx_arch_t xx_linear_arch(uint16_t cpu) {
    switch (cpu) {
        case 1: return XX_ARCH_X86_16;
        case 2:
        case 3:
        case 4: return XX_ARCH_X86;
        case 0x40:
        case 0x41:
        case 0x42: return XX_ARCH_MIPS;
        default: return XX_ARCH_UNKNOWN;
    }
}

static xx_os_t xx_linear_os(uint16_t os) {
    switch (os) {
        case 1: return XX_OS_OS2;
        case 2:
        case 4: return XX_OS_WINDOWS;
        case 3: return XX_OS_DOS;
        default: return XX_OS_UNKNOWN;
    }
}

static xx_format_type_t xx_linear_type(uint32_t flags) {
    switch (flags & XX_LINEAR_MODULE_TYPE_MASK) {
        case XX_LINEAR_MODULE_DLL:
        case XX_LINEAR_MODULE_PROTECTED_DLL: return XX_TYPE_LIBRARY;
        case XX_LINEAR_MODULE_PHYSICAL_DRIVER:
        case XX_LINEAR_MODULE_VIRTUAL_DRIVER: return XX_TYPE_DRIVER;
        default: return XX_TYPE_CONSOLE_APPLICATION;
    }
}

static uint64_t xx_linear_count_exports(Abstractformat *format,
                                        uint64_t table_relative) {
    int64_t absolute;
    int64_t total;
    uint64_t available;
    uint64_t current = 0U;
    uint64_t count_total = 0U;
    unsigned bundles = 0U;
    if (!xx_linear_range(format, table_relative, 1U, &absolute)) return 0U;
    total = xx_io_total_size(format->device);
    available = (uint64_t)(total - format->base_address) - table_relative;
    while (current < available && bundles++ < 65536U) {
        uint8_t count = xx_io_get_u8(format->device,
                                     absolute + (int64_t)current++);
        uint8_t type;
        uint64_t entry_size;
        uint64_t needed;
        if (count == 0U) return count_total;
        if (current >= available) return 0U;
        type = xx_io_get_u8(format->device,
                            absolute + (int64_t)current++);
        if (type == 0U) continue;
        switch (type & 0x7fU) {
            case 1: entry_size = 3U; break;
            case 2: entry_size = 5U; break;
            case 3: entry_size = 5U; break;
            case 4: entry_size = 7U; break;
            default: return 0U;
        }
        if (!xx_linear_mul_u64(count, entry_size, &needed) ||
            !xx_linear_add_u64(needed, 2U, &needed) ||
            needed > available - current) return 0U;
        current += needed;
        count_total += count;
    }
    return 0U;
}

static bool xx_linear_page_layout(const xx_linear_executable *linear,
                                  uint32_t page_index,
                                  uint64_t *relative,
                                  uint64_t *physical_size,
                                  bool *is_physical) {
    const xx_linear_page *page;
    uint64_t displacement;
    uint64_t size;
    uint16_t type;
    if (!linear || !linear->pages ||
        page_index >= linear->module_page_count ||
        !relative || !physical_size || !is_physical) return false;
    page = &linear->pages[page_index];
    type = (uint16_t)(page->flags & 7U);
    *is_physical = type == XX_LINEAR_PAGE_VALID;
    *relative = 0U;
    *physical_size = 0U;
    if (!*is_physical) return true;
    if (linear->signature == XX_LE_SIGNATURE) {
        if (page->data_offset == 0U || linear->page_size == 0U) return false;
        if (!xx_linear_mul_u64((uint64_t)page->data_offset - 1U,
                               linear->page_size, &displacement) ||
            !xx_linear_add_u64(linear->data_pages_offset, displacement,
                               relative)) return false;
        size = linear->page_size;
        if (page->data_offset == linear->module_page_count &&
            linear->last_page_size_or_shift != 0U &&
            linear->last_page_size_or_shift <= linear->page_size)
            size = linear->last_page_size_or_shift;
    } else {
        if (linear->last_page_size_or_shift >= 63U ||
            (uint64_t)page->data_offset >
                (UINT64_MAX >> linear->last_page_size_or_shift)) return false;
        displacement = (uint64_t)page->data_offset
                       << linear->last_page_size_or_shift;
        if (!xx_linear_add_u64(linear->data_pages_offset, displacement,
                               relative)) return false;
        size = page->data_size ? page->data_size : linear->page_size;
    }
    *physical_size = size;
    return true;
}

static bool xx_linear_parse(Abstractformat *format,
                            xx_linear_executable *parsed,
                            xx_pd_struct *pd, uint16_t signature) {
    int64_t total;
    uint64_t available;
    uint64_t linear_offset;
    uint64_t relative;
    uint64_t size;
    uint64_t format_end;
    uint32_t index;
    int64_t absolute;
    bool has_full_header;
    if (!parsed) return false;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->expected_signature = signature;
    if (!format || !format->device || format->base_address < 0 ||
        xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    if (available < XX_LINEAR_DOS_HEADER_SIZE ||
        xx_io_get_u16(format->device, format->base_address, false) !=
            UINT16_C(0x5a4d)) return false;
    linear_offset = xx_io_get_u32(format->device,
                                  format->base_address + 0x3c, false);
    if (!xx_linear_range(format, linear_offset,
                         XX_LINEAR_HEADER_CORE_SIZE, &absolute) ||
        xx_io_get_u16(format->device, absolute, false) != signature)
        return false;
    parsed->signature = signature;
    parsed->linear_offset = (uint32_t)linear_offset;
    parsed->byte_order = xx_io_get_u8(format->device, absolute + 2);
    parsed->word_order = xx_io_get_u8(format->device, absolute + 3);
    if (parsed->byte_order != 0U || parsed->word_order != 0U) return false;
    parsed->cpu_type = xx_io_get_u16(format->device, absolute + 8, false);
    parsed->target_os = xx_io_get_u16(format->device, absolute + 0x0a, false);
    parsed->module_version = xx_io_get_u32(format->device, absolute + 0x0c, false);
    parsed->module_flags = xx_io_get_u32(format->device, absolute + 0x10, false);
    parsed->module_page_count = xx_io_get_u32(format->device, absolute + 0x14, false);
    parsed->start_object = xx_io_get_u32(format->device, absolute + 0x18, false);
    parsed->entry_offset = xx_io_get_u32(format->device, absolute + 0x1c, false);
    parsed->stack_object = xx_io_get_u32(format->device, absolute + 0x20, false);
    parsed->stack_offset = xx_io_get_u32(format->device, absolute + 0x24, false);
    parsed->page_size = xx_io_get_u32(format->device, absolute + 0x28, false);
    parsed->last_page_size_or_shift = xx_io_get_u32(format->device, absolute + 0x2c, false);
    parsed->fixup_size = xx_io_get_u32(format->device, absolute + 0x30, false);
    parsed->loader_size = xx_io_get_u32(format->device, absolute + 0x38, false);
    parsed->object_table_offset = xx_io_get_u32(format->device, absolute + 0x40, false);
    parsed->object_count = xx_io_get_u32(format->device, absolute + 0x44, false);
    parsed->object_page_map_offset = xx_io_get_u32(format->device, absolute + 0x48, false);
    parsed->resource_table_offset = xx_io_get_u32(format->device, absolute + 0x50, false);
    parsed->resource_count = xx_io_get_u32(format->device, absolute + 0x54, false);
    parsed->entry_table_offset = xx_io_get_u32(format->device, absolute + 0x5c, false);
    parsed->fixup_page_table_offset = xx_io_get_u32(format->device, absolute + 0x68, false);
    parsed->fixup_record_table_offset = xx_io_get_u32(format->device, absolute + 0x6c, false);
    parsed->import_module_table_offset = xx_io_get_u32(format->device, absolute + 0x70, false);
    parsed->import_module_count = xx_io_get_u32(format->device, absolute + 0x74, false);
    parsed->import_procedure_table_offset = xx_io_get_u32(format->device, absolute + 0x78, false);
    parsed->data_pages_offset = xx_io_get_u32(format->device, absolute + 0x80, false);
    parsed->nonresident_name_table_offset = xx_io_get_u32(format->device, absolute + 0x88, false);
    parsed->nonresident_name_table_size = xx_io_get_u32(format->device, absolute + 0x8c, false);
    parsed->debug_info_offset = xx_io_get_u32(format->device, absolute + 0x98, false);
    parsed->debug_info_size = xx_io_get_u32(format->device, absolute + 0x9c, false);
    has_full_header = xx_linear_range(format, linear_offset,
                                      XX_LINEAR_HEADER_FULL_SIZE, NULL);
    if (has_full_header) {
        parsed->windows_resource_offset = xx_io_get_u32(format->device,
                                                         absolute + 0xb8, false);
        parsed->windows_resource_size = xx_io_get_u32(format->device,
                                                       absolute + 0xbc, false);
    }
    if (parsed->page_size == 0U ||
        (signature == XX_LX_SIGNATURE &&
         parsed->last_page_size_or_shift >= 32U)) return false;

    if (!xx_linear_mul_u64(parsed->object_count, XX_LINEAR_OBJECT_SIZE,
                           &size) || size > SIZE_MAX ||
        !xx_linear_add_u64(linear_offset, parsed->object_table_offset,
                           &relative) ||
        !xx_linear_range(format, relative, size, &absolute))
        return false;
    if (parsed->object_count != 0U) {
        uint64_t allocation_size;
        if (!xx_linear_mul_u64(parsed->object_count,
                               sizeof(*parsed->objects),
                               &allocation_size) || allocation_size > SIZE_MAX)
            return false;
        parsed->objects = (xx_linear_object *)xx_mem_alloc(
            (size_t)allocation_size);
        if (!parsed->objects) return false;
        xx_mem_zero(parsed->objects,
                    (size_t)parsed->object_count * sizeof(*parsed->objects));
    }
    for (index = 0U; index < parsed->object_count; ++index) {
        xx_linear_object *object = &parsed->objects[index];
        int64_t item = absolute + (int64_t)index * XX_LINEAR_OBJECT_SIZE;
        uint64_t first;
        object->virtual_size = xx_io_get_u32(format->device, item, false);
        object->base_address = xx_io_get_u32(format->device, item + 4, false);
        object->flags = xx_io_get_u32(format->device, item + 8, false);
        object->first_page = xx_io_get_u32(format->device, item + 12, false);
        object->page_count = xx_io_get_u32(format->device, item + 16, false);
        object->reserved = xx_io_get_u32(format->device, item + 20, false);
        if (object->page_count == 0U) continue;
        if (object->first_page == 0U) return false;
        first = (uint64_t)object->first_page - 1U;
        if (first > parsed->module_page_count ||
            object->page_count > parsed->module_page_count - first)
            return false;
    }

    size = signature == XX_LE_SIGNATURE
               ? XX_LE_PAGE_RECORD_SIZE : XX_LX_PAGE_RECORD_SIZE;
    if (!xx_linear_mul_u64(parsed->module_page_count, size, &size) ||
        size > SIZE_MAX ||
        !xx_linear_add_u64(linear_offset,
                           parsed->object_page_map_offset, &relative) ||
        !xx_linear_range(format, relative, size, &absolute))
        return false;
    if (parsed->module_page_count != 0U) {
        uint64_t allocation_size;
        if (!xx_linear_mul_u64(parsed->module_page_count,
                               sizeof(*parsed->pages),
                               &allocation_size) || allocation_size > SIZE_MAX)
            return false;
        parsed->pages = (xx_linear_page *)xx_mem_alloc(
            (size_t)allocation_size);
        if (!parsed->pages) return false;
        xx_mem_zero(parsed->pages,
                    (size_t)parsed->module_page_count * sizeof(*parsed->pages));
    }
    for (index = 0U; index < parsed->module_page_count; ++index) {
        xx_linear_page *page = &parsed->pages[index];
        int64_t item = absolute + (int64_t)index *
            (signature == XX_LE_SIGNATURE ? 4 : 8);
        if (signature == XX_LE_SIGNATURE) {
            page->data_offset = ((uint32_t)xx_io_get_u8(format->device, item) << 16U) |
                                ((uint32_t)xx_io_get_u8(format->device, item + 1) << 8U) |
                                xx_io_get_u8(format->device, item + 2);
            page->flags = xx_io_get_u8(format->device, item + 3);
        } else {
            page->data_offset = xx_io_get_u32(format->device, item, false);
            page->data_size = xx_io_get_u16(format->device, item + 4, false);
            page->flags = xx_io_get_u16(format->device, item + 6, false);
        }
    }

    format_end = linear_offset + (has_full_header
        ? XX_LINEAR_HEADER_FULL_SIZE : XX_LINEAR_HEADER_CORE_SIZE);
    if (parsed->data_pages_offset >= linear_offset &&
        parsed->data_pages_offset <= available)
        format_end = parsed->data_pages_offset;
    if (xx_linear_add_u64(linear_offset, parsed->object_table_offset,
                          &relative) &&
        xx_linear_mul_u64(parsed->object_count, XX_LINEAR_OBJECT_SIZE,
                          &size) &&
        xx_linear_add_u64(relative, size, &relative) && relative > format_end)
        format_end = relative;
    if (xx_linear_add_u64(linear_offset, parsed->object_page_map_offset,
                          &relative) &&
        xx_linear_mul_u64(parsed->module_page_count,
                          signature == XX_LE_SIGNATURE ? 4U : 8U, &size) &&
        xx_linear_add_u64(relative, size, &relative) && relative > format_end)
        format_end = relative;
    if (parsed->resource_count != 0U) {
        if (!xx_linear_mul_u64(parsed->resource_count,
                               XX_LINEAR_RESOURCE_RECORD_SIZE, &size) ||
            !xx_linear_add_u64(linear_offset,
                               parsed->resource_table_offset, &relative) ||
            !xx_linear_range(format, relative, size, NULL)) return false;
        if (relative + size > format_end) format_end = relative + size;
    }
    for (index = 0U; index < parsed->module_page_count; ++index) {
        uint64_t page_relative;
        uint64_t page_size;
        uint64_t end;
        bool physical;
        if (!xx_linear_page_layout(parsed, index, &page_relative,
                                   &page_size, &physical)) return false;
        if (!physical) continue;
        if (!xx_linear_range(format, page_relative, page_size, NULL) ||
            !xx_linear_add_u64(page_relative, page_size, &end)) return false;
        if (end > format_end) format_end = end;
    }
    if (parsed->nonresident_name_table_size != 0U) {
        if (!xx_linear_range(format, parsed->nonresident_name_table_offset,
                             parsed->nonresident_name_table_size, NULL))
            return false;
        relative = (uint64_t)parsed->nonresident_name_table_offset +
                   parsed->nonresident_name_table_size;
        if (relative > format_end) format_end = relative;
    }
    if (parsed->debug_info_size != 0U) {
        if (!xx_linear_range(format, parsed->debug_info_offset,
                             parsed->debug_info_size, NULL)) return false;
        relative = (uint64_t)parsed->debug_info_offset +
                   parsed->debug_info_size;
        if (relative > format_end) format_end = relative;
    }
    if (parsed->windows_resource_size != 0U) {
        if (!xx_linear_range(format, parsed->windows_resource_offset,
                             parsed->windows_resource_size, NULL)) return false;
        relative = (uint64_t)parsed->windows_resource_offset +
                   parsed->windows_resource_size;
        if (relative > format_end) format_end = relative;
    }
    if (format_end > available) return false;
    parsed->format.format_size = (int64_t)format_end;
    parsed->format.overlay_offset = format_end < available
        ? format->base_address + (int64_t)format_end : -1;
    parsed->format.overlay_size = format_end < available
        ? (int64_t)(available - format_end) : 0;
    parsed->format.number_of_imports = parsed->import_module_count;
    parsed->format.number_of_resources = parsed->resource_count;
    if (parsed->entry_table_offset != 0U &&
        xx_linear_add_u64(linear_offset, parsed->entry_table_offset,
                          &relative))
        parsed->format.number_of_exports =
            xx_linear_count_exports(format, relative);
    return !xx_pd_is_stopped(pd);
}

static const char *xx_linear_data_struct_id_to_string(Abstractformat *format,
                                                       uint32_t id) {
    (void)format;
    switch (id) {
        case XX_LINEAR_DATA_STRUCT_DOS_HEADER: return "IMAGE_DOS_HEADER";
        case XX_LINEAR_DATA_STRUCT_HEADER: return "LINEAR_EXECUTABLE_HEADER";
        case XX_LINEAR_DATA_STRUCT_OBJECT_TABLE: return "LINEAR_OBJECT_TABLE";
        case XX_LINEAR_DATA_STRUCT_PAGE_MAP: return "LINEAR_PAGE_MAP";
        case XX_LINEAR_DATA_STRUCT_RESOURCE_TABLE: return "LINEAR_RESOURCE_TABLE";
        case XX_LINEAR_DATA_STRUCT_NONRESIDENT_NAME_TABLE:
            return "NONRESIDENT_NAME_TABLE";
        case XX_LINEAR_DATA_STRUCT_DEBUG_DATA: return "DEBUG_DATA";
        case XX_DATA_STRUCT_ID_RAW_DATA: return "RAW_DATA";
        default: return "UNKNOWN";
    }
}

static uint32_t xx_linear_data_struct_string_to_id(Abstractformat *format,
                                                    const char *name) {
    uint32_t id;
    if (!name) return XX_LINEAR_DATA_STRUCT_UNKNOWN;
    for (id = XX_LINEAR_DATA_STRUCT_DOS_HEADER;
         id <= XX_LINEAR_DATA_STRUCT_DEBUG_DATA; ++id)
        if (xx_rt_strcmp(name, xx_linear_data_struct_id_to_string(format, id)) == 0)
            return id;
    return xx_rt_strcmp(name, "RAW_DATA") == 0
               ? XX_DATA_STRUCT_ID_RAW_DATA : XX_LINEAR_DATA_STRUCT_UNKNOWN;
}

static void xx_linear_data_stream_free(void *pointer) {
    xx_linear_data_stream *stream = (xx_linear_data_stream *)pointer;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_linear_data_stream_set(xx_data_struct_state *state,
                                      size_t index) {
    xx_linear_data_stream *stream;
    if (!state || !(stream = (xx_linear_data_stream *)state->internal_state) ||
        index >= stream->count) return false;
    state->current_struct = stream->items[index];
    state->current_index = (int64_t)index;
    state->has_struct = true;
    return true;
}

static bool xx_linear_append_data_struct(xx_linear_data_stream *stream,
                                         uint32_t id, int64_t offset,
                                         int64_t entry_size, uint64_t count,
                                         xx_data_struct_type_t type) {
    xx_data_struct *item;
    if (!stream || !stream->items || stream->count >= 9U || entry_size < 0 ||
        count > (uint64_t)INT64_MAX ||
        (count != 0U && (uint64_t)entry_size >
         (uint64_t)INT64_MAX / count)) return false;
    item = &stream->items[stream->count++];
    xx_mem_zero(item, sizeof(*item));
    item->id = id;
    item->offset = offset;
    item->address = -1;
    item->entry_size = entry_size;
    item->count = count;
    item->total_size = entry_size * (int64_t)count;
    item->type = type;
    return true;
}

static xx_data_struct_state *xx_linear_create_data_structs_reading(
    Abstractformat *format, xx_pd_struct *pd) {
    xx_linear_executable *linear = (xx_linear_executable *)format;
    xx_data_struct_state *state;
    xx_linear_data_stream *stream;
    int64_t header;
    int64_t page_record_size;
    if (!format || (!format->base_info_handled &&
                    !xx_linear_handle_impl(format, pd,
                                           linear->expected_signature)))
        return NULL;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_linear_data_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_data_struct *)xx_mem_alloc(9U * sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        xx_mem_free(state);
        return NULL;
    }
    header = format->base_address + linear->linear_offset;
    page_record_size = linear->signature == XX_LE_SIGNATURE ? 4 : 8;
    (void)xx_linear_append_data_struct(stream,
        XX_LINEAR_DATA_STRUCT_DOS_HEADER, format->base_address, 64, 1,
        XX_DATA_STRUCT_TYPE_STRUCT);
    (void)xx_linear_append_data_struct(stream,
        XX_LINEAR_DATA_STRUCT_HEADER, header, XX_LINEAR_HEADER_CORE_SIZE, 1,
        XX_DATA_STRUCT_TYPE_STRUCT);
    if (linear->object_count != 0U)
        (void)xx_linear_append_data_struct(stream,
            XX_LINEAR_DATA_STRUCT_OBJECT_TABLE,
            header + linear->object_table_offset, XX_LINEAR_OBJECT_SIZE,
            linear->object_count, XX_DATA_STRUCT_TYPE_ENTRY);
    if (linear->module_page_count != 0U)
        (void)xx_linear_append_data_struct(stream,
            XX_LINEAR_DATA_STRUCT_PAGE_MAP,
            header + linear->object_page_map_offset, page_record_size,
            linear->module_page_count, XX_DATA_STRUCT_TYPE_ENTRY);
    if (linear->resource_count != 0U)
        (void)xx_linear_append_data_struct(stream,
            XX_LINEAR_DATA_STRUCT_RESOURCE_TABLE,
            header + linear->resource_table_offset,
            XX_LINEAR_RESOURCE_RECORD_SIZE, linear->resource_count,
            XX_DATA_STRUCT_TYPE_ENTRY);
    if (linear->nonresident_name_table_size != 0U)
        (void)xx_linear_append_data_struct(stream,
            XX_LINEAR_DATA_STRUCT_NONRESIDENT_NAME_TABLE,
            format->base_address + linear->nonresident_name_table_offset,
            linear->nonresident_name_table_size, 1,
            XX_DATA_STRUCT_TYPE_RAW_DATA);
    if (linear->debug_info_size != 0U)
        (void)xx_linear_append_data_struct(stream,
            XX_LINEAR_DATA_STRUCT_DEBUG_DATA,
            format->base_address + linear->debug_info_offset,
            linear->debug_info_size, 1, XX_DATA_STRUCT_TYPE_RAW_DATA);
    if (format->overlay_size > 0)
        (void)xx_linear_append_data_struct(stream, XX_DATA_STRUCT_ID_RAW_DATA,
            format->overlay_offset, format->overlay_size, 1,
            XX_DATA_STRUCT_TYPE_RAW_DATA);
    xx_data_struct_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xx_linear_data_stream_free;
    state->total_structs = (int64_t)stream->count;
    if (!xx_linear_data_stream_set(state, 0U)) {
        xx_data_struct_state_free(state);
        return NULL;
    }
    return state;
}

static const xx_data_struct *xx_linear_get_current_data_struct(
    Abstractformat *format, xx_data_struct_state *state) {
    return format && state && state->format == format && state->has_struct
               ? &state->current_struct : NULL;
}

static bool xx_linear_data_struct_move_to_next(Abstractformat *format,
                                                xx_data_struct_state *state,
                                                xx_pd_struct *pd) {
    (void)pd;
    if (!format || !state || state->format != format ||
        state->current_index < 0) return false;
    if (!xx_linear_data_stream_set(state,
                                   (size_t)state->current_index + 1U)) {
        state->has_struct = false;
        return false;
    }
    return true;
}

static void xx_linear_free_data_structs_reading(
    Abstractformat *format, xx_data_struct_state *state) {
    (void)format;
    xx_data_struct_state_free(state);
}

static const xx_data_struct_field_desc xx_linear_dos_fields[] = {
    {L"e_magic", L"uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"e_cblp", L"uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"e_cp", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"e_cparhdr", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"e_lfarlc", L"uint16", 24, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e_lfanew", L"uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER}
};

static const xx_data_struct_field_desc xx_linear_header_fields[] = {
    {L"e32_magic", L"uint16", 0x00, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"e32_border", L"uint8", 0x02, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"e32_worder", L"uint8", 0x03, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"e32_cpu", L"uint16", 0x08, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"e32_os", L"uint16", 0x0a, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"e32_ver", L"uint32", 0x0c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"e32_mflags", L"uint32", 0x10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"e32_mpages", L"uint32", 0x14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"e32_startobj", L"uint32", 0x18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"e32_eip", L"uint32", 0x1c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS},
    {L"e32_stackobj", L"uint32", 0x20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"e32_esp", L"uint32", 0x24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS},
    {L"e32_pagesize", L"uint32", 0x28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"e32_lastpagesize", L"uint32", 0x2c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"e32_fixupsize", L"uint32", 0x30, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"e32_ldrsize", L"uint32", 0x38, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"e32_objtab", L"uint32", 0x40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e32_objcnt", L"uint32", 0x44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"e32_objmap", L"uint32", 0x48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e32_rsrctab", L"uint32", 0x50, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e32_rsrccnt", L"uint32", 0x54, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"e32_enttab", L"uint32", 0x5c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e32_impmod", L"uint32", 0x70, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e32_impmodcnt", L"uint32", 0x74, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"e32_datapage", L"uint32", 0x80, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e32_nrestab", L"uint32", 0x88, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e32_cbnrestab", L"uint32", 0x8c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"e32_debuginfo", L"uint32", 0x98, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"e32_debuglen", L"uint32", 0x9c, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE}
};

static const xx_data_struct_field_desc xx_linear_object_fields[] = {
    {L"o32_size", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"o32_base", L"uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS},
    {L"o32_flags", L"uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"o32_pagemap", L"uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"o32_mapsize", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"o32_reserved", L"uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED}
};

static const xx_data_struct_field_desc xx_le_page_fields[] = {
    {L"o16_pagenum_hi", L"uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"o16_pagenum_mid", L"uint8", 1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"o16_pagenum_lo", L"uint8", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"o16_pageflags", L"uint8", 3, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS}
};

static const xx_data_struct_field_desc xx_lx_page_fields[] = {
    {L"o32_pagedataoffset", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET},
    {L"o32_pagesize", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"o32_pageflags", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS}
};

static const xx_data_struct_field_desc xx_linear_resource_fields[] = {
    {L"type", L"uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"name", L"uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"size", L"uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"object", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"offset", L"uint32", 10, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET}
};

static void xx_linear_record_stream_free(void *pointer) {
    if (pointer) xx_mem_free(pointer);
}

static bool xx_linear_populate_record(Abstractformat *format,
                                      xx_data_struct_record_state *state,
                                      size_t index) {
    xx_linear_record_stream *stream;
    if (!format || !format->device || !state ||
        !(stream = (xx_linear_record_stream *)state->internal_state) ||
        index >= stream->count) return false;
    return xx_data_struct_record_populate(&state->current_record,
        format->device, state->parent_struct.offset,
        &stream->fields[index], false);
}

static xx_data_struct_record_state *
xx_linear_create_data_struct_records_reading(
    Abstractformat *format, const xx_data_struct *data_struct,
    xx_pd_struct *pd) {
    const xx_data_struct_field_desc *fields = NULL;
    size_t count = 0U;
    xx_data_struct_record_state *state;
    xx_linear_record_stream *stream;
    xx_linear_executable *linear = (xx_linear_executable *)format;
    (void)pd;
    if (!format || !data_struct ||
        data_struct->type == XX_DATA_STRUCT_TYPE_RAW_DATA) return NULL;
    switch (data_struct->id) {
        case XX_LINEAR_DATA_STRUCT_DOS_HEADER:
            fields = xx_linear_dos_fields;
            count = sizeof(xx_linear_dos_fields) / sizeof(xx_linear_dos_fields[0]);
            break;
        case XX_LINEAR_DATA_STRUCT_HEADER:
            fields = xx_linear_header_fields;
            count = sizeof(xx_linear_header_fields) / sizeof(xx_linear_header_fields[0]);
            break;
        case XX_LINEAR_DATA_STRUCT_OBJECT_TABLE:
            fields = xx_linear_object_fields;
            count = sizeof(xx_linear_object_fields) / sizeof(xx_linear_object_fields[0]);
            break;
        case XX_LINEAR_DATA_STRUCT_PAGE_MAP:
            if (linear->signature == XX_LE_SIGNATURE) {
                fields = xx_le_page_fields;
                count = sizeof(xx_le_page_fields) / sizeof(xx_le_page_fields[0]);
            } else {
                fields = xx_lx_page_fields;
                count = sizeof(xx_lx_page_fields) / sizeof(xx_lx_page_fields[0]);
            }
            break;
        case XX_LINEAR_DATA_STRUCT_RESOURCE_TABLE:
            fields = xx_linear_resource_fields;
            count = sizeof(xx_linear_resource_fields) / sizeof(xx_linear_resource_fields[0]);
            break;
        default: return NULL;
    }
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_linear_record_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_record_state_init(state, format, data_struct);
    stream->fields = fields;
    stream->count = count;
    state->internal_state = stream;
    state->free_internal = xx_linear_record_stream_free;
    state->total_records = (int64_t)count;
    if (!xx_linear_populate_record(format, state, 0U)) {
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

static const xx_data_struct_record *xx_linear_get_current_data_struct_record(
    Abstractformat *format, xx_data_struct_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

static bool xx_linear_data_struct_record_move_to_next(
    Abstractformat *format, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    size_t next;
    (void)pd;
    if (!format || !state || state->format != format ||
        state->current_index < 0) return false;
    next = (size_t)state->current_index + 1U;
    if (next >= (size_t)state->total_records) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_linear_populate_record(format, state, next)) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)next;
    state->has_record = true;
    return true;
}

static void xx_linear_free_data_struct_records_reading(
    Abstractformat *format, xx_data_struct_record_state *state) {
    (void)format;
    xx_data_struct_record_state_free(state);
}

void xx_linear_init_impl(xx_linear_executable *linear,
                         xx_io_device *device, int64_t base_address,
                         uint16_t signature) {
    if (!linear) return;
    xx_mem_zero(linear, sizeof(*linear));
    xx_format_init(&linear->format, device, base_address);
    linear->expected_signature = signature;
    linear->format.endian = XX_ENDIAN_LITTLE;
    linear->format.os = XX_OS_UNKNOWN;
    linear->format.arch = XX_ARCH_UNKNOWN;
    linear->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    linear->format.is_executable = true;
    linear->format.data_struct_id_to_string =
        xx_linear_data_struct_id_to_string;
    linear->format.data_struct_string_to_id =
        xx_linear_data_struct_string_to_id;
    linear->format.create_data_structs_reading =
        xx_linear_create_data_structs_reading;
    linear->format.get_current_data_struct = xx_linear_get_current_data_struct;
    linear->format.data_struct_move_to_next =
        xx_linear_data_struct_move_to_next;
    linear->format.free_data_structs_reading =
        xx_linear_free_data_structs_reading;
    linear->format.create_data_struct_records_reading =
        xx_linear_create_data_struct_records_reading;
    linear->format.get_current_data_struct_record =
        xx_linear_get_current_data_struct_record;
    linear->format.data_struct_record_move_to_next =
        xx_linear_data_struct_record_move_to_next;
    linear->format.free_data_struct_records_reading =
        xx_linear_free_data_struct_records_reading;
}

void xx_linear_destroy_impl(xx_linear_executable *linear) {
    if (!linear) return;
    xx_linear_parsed_cleanup(linear);
    xx_format_cleanup_extra_parameters(&linear->format);
}

bool xx_linear_check_impl(Abstractformat *format, xx_pd_struct *pd,
                          uint16_t signature) {
    xx_linear_executable parsed;
    bool result = xx_linear_parse(format, &parsed, pd, signature);
    xx_linear_parsed_cleanup(&parsed);
    return result;
}

bool xx_linear_handle_impl(Abstractformat *format, xx_pd_struct *pd,
                           uint16_t signature) {
    xx_linear_executable parsed;
    xx_linear_executable *linear;
    Abstractformat saved;
    bool result;
    if (!format) return false;
    result = xx_linear_parse(format, &parsed, pd, signature);
    if (!result) {
        xx_linear_parsed_cleanup(&parsed);
        format->is_valid = false;
        format->base_info_handled = false;
        xx_format_invalidate_memory_map(format);
        return false;
    }
    linear = (xx_linear_executable *)format;
    saved = linear->format;
    xx_linear_parsed_cleanup(linear);
    *linear = parsed;
    linear->format = saved;
    parsed.objects = NULL;
    parsed.pages = NULL;
    linear->format.file_type = signature == XX_LE_SIGNATURE
                                   ? XX_FILE_TYPE_LE : XX_FILE_TYPE_LX;
    linear->format.os = xx_linear_os(linear->target_os);
    linear->format.arch = xx_linear_arch(linear->cpu_type);
    linear->format.format_type = xx_linear_type(linear->module_flags);
    if (linear->format.format_type == XX_TYPE_LIBRARY)
        xx_format_set_extension(&linear->format, "dll");
    else if (linear->format.format_type == XX_TYPE_DRIVER)
        xx_format_set_extension(&linear->format, "vxd");
    else
        xx_format_set_extension(&linear->format, "exe");
    linear->format.format_size = parsed.format.format_size;
    linear->format.overlay_offset = parsed.format.overlay_offset;
    linear->format.overlay_size = parsed.format.overlay_size;
    linear->format.number_of_imports = parsed.format.number_of_imports;
    linear->format.number_of_exports = parsed.format.number_of_exports;
    linear->format.number_of_resources = parsed.format.number_of_resources;
    linear->format.is_valid = true;
    linear->format.base_info_handled = true;
    xx_format_invalidate_memory_map(&linear->format);
    return true;
}

int64_t xx_linear_get_format_size_impl(Abstractformat *format,
                                       xx_pd_struct *pd,
                                       uint16_t signature) {
    return format && (format->base_info_handled ||
                      xx_linear_handle_impl(format, pd, signature))
               ? format->format_size : -1;
}

uint64_t xx_linear_get_import_count_impl(Abstractformat *format,
                                         xx_pd_struct *pd,
                                         uint16_t signature) {
    return format && (format->base_info_handled ||
                      xx_linear_handle_impl(format, pd, signature))
               ? format->number_of_imports : 0U;
}

uint64_t xx_linear_get_export_count_impl(Abstractformat *format,
                                         xx_pd_struct *pd,
                                         uint16_t signature) {
    return format && (format->base_info_handled ||
                      xx_linear_handle_impl(format, pd, signature))
               ? format->number_of_exports : 0U;
}

uint64_t xx_linear_get_resource_count_impl(Abstractformat *format,
                                           xx_pd_struct *pd,
                                           uint16_t signature) {
    return format && (format->base_info_handled ||
                      xx_linear_handle_impl(format, pd, signature))
               ? format->number_of_resources : 0U;
}

bool xx_linear_get_memory_map_impl(Abstractformat *format,
                                   xx_memory_map_mode_t mode,
                                   xx_memory_map *output,
                                   xx_pd_struct *pd,
                                   uint16_t signature) {
    xx_linear_executable *linear = (xx_linear_executable *)format;
    int64_t total;
    int64_t binary_size;
    int64_t header_size;
    int64_t first_load = -1;
    uint64_t module;
    uint32_t object_index;
    if (!format || !output || !format->device ||
        !format->base_info_handled || linear->signature != signature ||
        format->base_address < 0 || xx_pd_is_stopped(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_OBJECTS;
    if (mode != XX_MEMORY_MAP_MODE_OBJECTS &&
        mode != XX_MEMORY_MAP_MODE_MAPS) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    binary_size = total - format->base_address;
    header_size = linear->data_pages_offset != 0U &&
                  linear->data_pages_offset <= (uint64_t)binary_size
                      ? (int64_t)linear->data_pages_offset
                      : (int64_t)linear->linear_offset +
                        XX_LINEAR_HEADER_CORE_SIZE;
    module = format->module_address != XX_INVALID_ADDRESS
                 ? format->module_address : 0U;
    output->binary_offset = format->base_address;
    output->binary_size = binary_size;
    output->module_address = module;
    output->is_image = format->is_mapped;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = mode;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = -1;
    output->start_load_offset = format->base_address + header_size;
    if (!xx_memory_map_add_part(output, format->base_address, header_size,
                                module, header_size,
                                XX_FILE_PART_HEADER, 0,
                                signature == XX_LE_SIGNATURE
                                    ? "LE header and loader"
                                    : "LX header and loader", false))
        return false;
    for (object_index = 0U; object_index < linear->object_count;
         ++object_index) {
        const xx_linear_object *object = &linear->objects[object_index];
        uint64_t object_address;
        uint64_t covered_virtual = 0U;
        uint32_t object_page;
        if (!xx_linear_add_u64(module, object->base_address,
                               &object_address) ||
            object_address == XX_INVALID_ADDRESS) return false;
        if ((object->flags & XX_LINEAR_OBJECT_EXECUTABLE) != 0U &&
            output->code_base < 0 && object_address <= (uint64_t)INT64_MAX)
            output->code_base = (int64_t)object_address;
        for (object_page = 0U; object_page < object->page_count;
             ++object_page) {
            uint32_t page_index = object->first_page - 1U + object_page;
            uint64_t page_relative;
            uint64_t physical_size;
            uint64_t address;
            uint64_t remaining;
            uint64_t virtual_size;
            bool physical;
            int64_t offset = -1;
            char name[XX_MEMORY_RECORD_NAME_SIZE];
            if (!xx_linear_mul_u64(object_page, linear->page_size,
                                   &covered_virtual) ||
                covered_virtual >= object->virtual_size) break;
            remaining = (uint64_t)object->virtual_size - covered_virtual;
            virtual_size = remaining < linear->page_size
                               ? remaining : linear->page_size;
            if (!xx_linear_add_u64(object_address, covered_virtual,
                                   &address) ||
                !xx_linear_page_layout(linear, page_index, &page_relative,
                                       &physical_size, &physical)) return false;
            if (physical) {
                if (physical_size > virtual_size) physical_size = virtual_size;
                if (page_relative > (uint64_t)(INT64_MAX -
                                               format->base_address))
                    return false;
                offset = format->base_address + (int64_t)page_relative;
                if (first_load < 0 || offset < first_load) first_load = offset;
            } else {
                physical_size = 0U;
            }
            (void)xx_rt_snprintf(name, sizeof(name), "Object %u page %u",
                           (unsigned)object_index + 1U,
                           (unsigned)object_page + 1U);
            if (!xx_memory_map_add_part(output, offset,
                    (int64_t)physical_size, address, (int64_t)virtual_size,
                    XX_FILE_PART_OBJECT, (int32_t)object_index + 1,
                    name, false)) return false;
        }
        if (object->page_count == 0U ||
            (uint64_t)object->page_count * linear->page_size <
                object->virtual_size) {
            uint64_t tail_start = (uint64_t)object->page_count *
                                  linear->page_size;
            uint64_t address;
            uint64_t tail_size;
            char name[XX_MEMORY_RECORD_NAME_SIZE];
            if (tail_start > object->virtual_size) tail_start = object->virtual_size;
            tail_size = (uint64_t)object->virtual_size - tail_start;
            if (tail_size != 0U) {
                if (!xx_linear_add_u64(object_address, tail_start, &address))
                    return false;
                (void)xx_rt_snprintf(name, sizeof(name), "Object %u zero-fill",
                               (unsigned)object_index + 1U);
                if (!xx_memory_map_add_part(output, -1, 0, address,
                        (int64_t)tail_size, XX_FILE_PART_OBJECT,
                        (int32_t)object_index + 1, name, false)) return false;
            }
        }
    }
    if (first_load >= 0) output->start_load_offset = first_load;
    if (linear->start_object != 0U &&
        linear->start_object <= linear->object_count) {
        const xx_linear_object *object =
            &linear->objects[linear->start_object - 1U];
        uint64_t object_address;
        if (linear->entry_offset < object->virtual_size &&
            xx_linear_add_u64(module, object->base_address,
                              &object_address))
            (void)xx_linear_add_u64(object_address, linear->entry_offset,
                                    &output->entry_point_address);
    }
    if (format->overlay_size > 0 &&
        !xx_memory_map_add_part(output, format->overlay_offset,
                                format->overlay_size,
                                XX_INVALID_ADDRESS, 0,
                                XX_FILE_PART_OVERLAY,
                                (int32_t)linear->object_count + 1,
                                "Overlay", false)) return false;
    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

static void xx_le_vtable_destroy(Abstractformat *format) {
    xx_le_destroy((xx_le *)format);
}

void xx_le_init(xx_le *le, xx_io_device *device, int64_t base_address) {
    if (!le) return;
    xx_linear_init_impl(le, device, base_address, XX_LE_SIGNATURE);
    le->format.file_type = XX_FILE_TYPE_LE;
    xx_format_set_mime_type(&le->format, "application/x-linear-executable");
    xx_format_set_extension(&le->format, "exe");
    le->format.check_is_valid = xx_le_check_is_valid;
    le->format.handle_base_info = xx_le_handle_base_info;
    le->format.get_format_size = xx_le_get_format_size;
    le->format.get_number_of_imports = xx_le_get_number_of_imports;
    le->format.get_number_of_exports = xx_le_get_number_of_exports;
    le->format.get_number_of_resources = xx_le_get_number_of_resources;
    le->format.get_memory_map = xx_le_get_memory_map;
    le->format.destroy = xx_le_vtable_destroy;
}

xx_le *xx_le_create(xx_io_device *device, int64_t base_address) {
    xx_le *le = (xx_le *)xx_mem_alloc(sizeof(*le));
    if (le) xx_le_init(le, device, base_address);
    return le;
}

void xx_le_destroy(xx_le *le) { xx_linear_destroy_impl(le); }

void xx_le_free(xx_le *le) {
    if (!le) return;
    xx_le_destroy(le);
    xx_mem_free(le);
}

bool xx_le_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return xx_linear_check_impl(format, pd, XX_LE_SIGNATURE);
}

bool xx_le_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    return xx_linear_handle_impl(format, pd, XX_LE_SIGNATURE);
}

int64_t xx_le_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return xx_linear_get_format_size_impl(format, pd, XX_LE_SIGNATURE);
}

uint64_t xx_le_get_number_of_imports(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return xx_linear_get_import_count_impl(format, pd, XX_LE_SIGNATURE);
}

uint64_t xx_le_get_number_of_exports(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return xx_linear_get_export_count_impl(format, pd, XX_LE_SIGNATURE);
}

uint64_t xx_le_get_number_of_resources(Abstractformat *format,
                                       xx_pd_struct *pd) {
    return xx_linear_get_resource_count_impl(format, pd, XX_LE_SIGNATURE);
}

bool xx_le_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode,
                          xx_memory_map *output, xx_pd_struct *pd) {
    return xx_linear_get_memory_map_impl(format, mode, output, pd,
                                         XX_LE_SIGNATURE);
}

uint32_t xx_le_get_number_of_objects(const xx_le *le) {
    return le ? le->object_count : 0U;
}

const xx_linear_object *xx_le_get_object(const xx_le *le, uint32_t index) {
    return le && le->objects && index < le->object_count
               ? &le->objects[index] : NULL;
}
