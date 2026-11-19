/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_le_private.h"
#include "xxfclib/formats/executable/le/xx_le.h"
#include "xx_linear_internal.h"
#include "xx_le_defs.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool xx_linear_range(const Abstractformat *format, uint64_t relative, uint64_t size, int64_t *absolute)
{
    int64_t total;
    uint64_t available;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    if (relative > available || size > available - relative || relative > (uint64_t)(INT64_MAX - format->base_address)) return false;
    if (absolute) *absolute = format->base_address + (int64_t)relative;
    return true;
}

static bool xx_linear_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool xx_linear_mul_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (!result || (left != 0U && right > UINT64_MAX / left)) return false;
    *result = left * right;
    return true;
}

static void xx_linear_parsed_cleanup(xx_linear_executable *linear)
{
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

static xx_arch_t xx_linear_arch(uint16_t cpu)
{
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

static xx_os_t xx_linear_os(uint16_t os)
{
    switch (os) {
        case 1: return XX_OS_OS2;
        case 2:
        case 4: return XX_OS_WINDOWS;
        case 3: return XX_OS_DOS;
        default: return XX_OS_UNKNOWN;
    }
}

static xx_format_type_t xx_linear_type(uint32_t flags)
{
    switch (flags & XX_LINEAR_MODULE_TYPE_MASK) {
        case XX_LINEAR_MODULE_DLL:
        case XX_LINEAR_MODULE_PROTECTED_DLL: return XX_TYPE_LIBRARY;
        case XX_LINEAR_MODULE_PHYSICAL_DRIVER:
        case XX_LINEAR_MODULE_VIRTUAL_DRIVER: return XX_TYPE_DRIVER;
        default: return XX_TYPE_CONSOLE_APPLICATION;
    }
}

static uint64_t xx_linear_count_exports(Abstractformat *format, uint64_t table_relative)
{
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
        uint8_t count = xx_io_get_u8(format->device, absolute + (int64_t)current++);
        uint8_t type;
        uint64_t entry_size;
        uint64_t needed;
        if (count == 0U) return count_total;
        if (current >= available) return 0U;
        type = xx_io_get_u8(format->device, absolute + (int64_t)current++);
        if (type == 0U) continue;
        switch (type & 0x7fU) {
            case 1: entry_size = 3U; break;
            case 2: entry_size = 5U; break;
            case 3: entry_size = 5U; break;
            case 4: entry_size = 7U; break;
            default: return 0U;
        }
        if (!xx_linear_mul_u64(count, entry_size, &needed) || !xx_linear_add_u64(needed, 2U, &needed) || needed > available - current) return 0U;
        current += needed;
        count_total += count;
    }
    return 0U;
}

static bool xx_linear_page_layout(const xx_linear_executable *linear, uint32_t page_index, uint64_t *relative, uint64_t *physical_size, bool *is_physical)
{
    const xx_linear_page *page;
    uint64_t displacement;
    uint64_t size;
    uint16_t type;
    if (!linear || !linear->pages || page_index >= linear->module_page_count || !relative || !physical_size || !is_physical) return false;
    page = &linear->pages[page_index];
    type = (uint16_t)(page->flags & 7U);
    *is_physical = type == XX_LINEAR_PAGE_VALID;
    *relative = 0U;
    *physical_size = 0U;
    if (!*is_physical) return true;
    if (linear->signature == XX_LE_SIGNATURE) {
        if (page->data_offset == 0U || linear->page_size == 0U) return false;
        if (!xx_linear_mul_u64((uint64_t)page->data_offset - 1U, linear->page_size, &displacement) ||
            !xx_linear_add_u64(linear->data_pages_offset, displacement, relative))
            return false;
        size = linear->page_size;
        if (page->data_offset == linear->module_page_count && linear->last_page_size_or_shift != 0U && linear->last_page_size_or_shift <= linear->page_size)
            size = linear->last_page_size_or_shift;
    } else {
        if (linear->last_page_size_or_shift >= 63U || (uint64_t)page->data_offset > (UINT64_MAX >> linear->last_page_size_or_shift)) return false;
        displacement = (uint64_t)page->data_offset << linear->last_page_size_or_shift;
        if (!xx_linear_add_u64(linear->data_pages_offset, displacement, relative)) return false;
        size = page->data_size ? page->data_size : linear->page_size;
    }
    *physical_size = size;
    return true;
}

static bool xx_linear_parse(Abstractformat *format, xx_linear_executable *parsed, xx_pd_struct *pd, uint16_t signature)
{
    int64_t total;
    uint64_t available;
    uint64_t linear_offset;
    uint64_t relative;
    uint64_t size;
    uint64_t format_end;
    uint32_t index;
    int64_t absolute;
    bool has_full_header;
    uint16_t initial_signature;
    if (!parsed) return false;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->expected_signature = signature;
    if (!format || !format->device || format->base_address < 0 || xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    if (available < XX_LINEAR_HEADER_CORE_SIZE) return false;
    initial_signature = xx_io_get_u16(format->device, format->base_address, false);
    if (initial_signature == signature) {
        linear_offset = 0U;
    } else {
        if (available < XX_LINEAR_DOS_HEADER_SIZE || initial_signature != UINT16_C(0x5a4d)) return false;
        linear_offset = xx_io_get_u32(format->device, format->base_address + 0x3c, false);
    }
    if (!xx_linear_range(format, linear_offset, XX_LINEAR_HEADER_CORE_SIZE, &absolute) || xx_io_get_u16(format->device, absolute, false) != signature) return false;
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
    has_full_header = xx_linear_range(format, linear_offset, XX_LINEAR_HEADER_FULL_SIZE, NULL);
    if (has_full_header) {
        parsed->windows_resource_offset = xx_io_get_u32(format->device, absolute + 0xb8, false);
        parsed->windows_resource_size = xx_io_get_u32(format->device, absolute + 0xbc, false);
    }
    if (parsed->page_size == 0U || (signature == XX_LX_SIGNATURE && parsed->last_page_size_or_shift >= 32U)) return false;

    if (!xx_linear_mul_u64(parsed->object_count, XX_LINEAR_OBJECT_SIZE, &size) || size > SIZE_MAX ||
        !xx_linear_add_u64(linear_offset, parsed->object_table_offset, &relative) || !xx_linear_range(format, relative, size, &absolute))
        return false;
    if (parsed->object_count != 0U) {
        uint64_t allocation_size;
        if (!xx_linear_mul_u64(parsed->object_count, sizeof(*parsed->objects), &allocation_size) || allocation_size > SIZE_MAX) return false;
        parsed->objects = (xx_linear_object *)xx_mem_alloc((size_t)allocation_size);
        if (!parsed->objects) return false;
        xx_mem_zero(parsed->objects, (size_t)parsed->object_count * sizeof(*parsed->objects));
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
        if (first > parsed->module_page_count || object->page_count > parsed->module_page_count - first) return false;
    }

    size = signature == XX_LE_SIGNATURE ? XX_LE_PAGE_RECORD_SIZE : XX_LX_PAGE_RECORD_SIZE;
    if (!xx_linear_mul_u64(parsed->module_page_count, size, &size) || size > SIZE_MAX || !xx_linear_add_u64(linear_offset, parsed->object_page_map_offset, &relative) ||
        !xx_linear_range(format, relative, size, &absolute))
        return false;
    if (parsed->module_page_count != 0U) {
        uint64_t allocation_size;
        if (!xx_linear_mul_u64(parsed->module_page_count, sizeof(*parsed->pages), &allocation_size) || allocation_size > SIZE_MAX) return false;
        parsed->pages = (xx_linear_page *)xx_mem_alloc((size_t)allocation_size);
        if (!parsed->pages) return false;
        xx_mem_zero(parsed->pages, (size_t)parsed->module_page_count * sizeof(*parsed->pages));
    }
    for (index = 0U; index < parsed->module_page_count; ++index) {
        xx_linear_page *page = &parsed->pages[index];
        int64_t item = absolute + (int64_t)index * (signature == XX_LE_SIGNATURE ? 4 : 8);
        if (signature == XX_LE_SIGNATURE) {
            page->data_offset =
                ((uint32_t)xx_io_get_u8(format->device, item) << 16U) | ((uint32_t)xx_io_get_u8(format->device, item + 1) << 8U) | xx_io_get_u8(format->device, item + 2);
            page->flags = xx_io_get_u8(format->device, item + 3);
        } else {
            page->data_offset = xx_io_get_u32(format->device, item, false);
            page->data_size = xx_io_get_u16(format->device, item + 4, false);
            page->flags = xx_io_get_u16(format->device, item + 6, false);
        }
    }

    format_end = linear_offset + (has_full_header ? XX_LINEAR_HEADER_FULL_SIZE : XX_LINEAR_HEADER_CORE_SIZE);
    if (parsed->data_pages_offset >= linear_offset && parsed->data_pages_offset <= available) format_end = parsed->data_pages_offset;
    if (xx_linear_add_u64(linear_offset, parsed->object_table_offset, &relative) && xx_linear_mul_u64(parsed->object_count, XX_LINEAR_OBJECT_SIZE, &size) &&
        xx_linear_add_u64(relative, size, &relative) && relative > format_end)
        format_end = relative;
    if (xx_linear_add_u64(linear_offset, parsed->object_page_map_offset, &relative) &&
        xx_linear_mul_u64(parsed->module_page_count, signature == XX_LE_SIGNATURE ? 4U : 8U, &size) && xx_linear_add_u64(relative, size, &relative) &&
        relative > format_end)
        format_end = relative;
    if (parsed->resource_count != 0U) {
        if (!xx_linear_mul_u64(parsed->resource_count, XX_LINEAR_RESOURCE_RECORD_SIZE, &size) ||
            !xx_linear_add_u64(linear_offset, parsed->resource_table_offset, &relative) || !xx_linear_range(format, relative, size, NULL))
            return false;
        if (relative + size > format_end) format_end = relative + size;
    }
    for (index = 0U; index < parsed->module_page_count; ++index) {
        uint64_t page_relative;
        uint64_t page_size;
        uint64_t end;
        bool physical;
        if (!xx_linear_page_layout(parsed, index, &page_relative, &page_size, &physical)) return false;
        if (!physical) continue;
        if (!xx_linear_range(format, page_relative, page_size, NULL) || !xx_linear_add_u64(page_relative, page_size, &end)) return false;
        if (end > format_end) format_end = end;
    }
    if (parsed->nonresident_name_table_size != 0U) {
        if (!xx_linear_range(format, parsed->nonresident_name_table_offset, parsed->nonresident_name_table_size, NULL)) return false;
        relative = (uint64_t)parsed->nonresident_name_table_offset + parsed->nonresident_name_table_size;
        if (relative > format_end) format_end = relative;
    }
    if (parsed->debug_info_size != 0U) {
        if (!xx_linear_range(format, parsed->debug_info_offset, parsed->debug_info_size, NULL)) return false;
        relative = (uint64_t)parsed->debug_info_offset + parsed->debug_info_size;
        if (relative > format_end) format_end = relative;
    }
    if (parsed->windows_resource_size != 0U) {
        if (!xx_linear_range(format, parsed->windows_resource_offset, parsed->windows_resource_size, NULL)) return false;
        relative = (uint64_t)parsed->windows_resource_offset + parsed->windows_resource_size;
        if (relative > format_end) format_end = relative;
    }
    if (format_end > available) return false;
    parsed->format.format_size = (int64_t)format_end;
    parsed->format.overlay_offset = format_end < available ? format->base_address + (int64_t)format_end : -1;
    parsed->format.overlay_size = format_end < available ? (int64_t)(available - format_end) : 0;
    parsed->format.number_of_imports = parsed->import_module_count;
    parsed->format.number_of_resources = parsed->resource_count;
    if (parsed->entry_table_offset != 0U && xx_linear_add_u64(linear_offset, parsed->entry_table_offset, &relative))
        parsed->format.number_of_exports = xx_linear_count_exports(format, relative);
    return !xx_pd_is_stopped(pd);
}

void xx_linear_init_impl(xx_linear_executable *linear, xx_io_device *device, int64_t base_address, uint16_t signature)
{
    if (!linear) return;
    xx_mem_zero(linear, sizeof(*linear));
    xx_format_init(&linear->format, device, base_address);
    linear->expected_signature = signature;
    linear->format.endian = XX_ENDIAN_LITTLE;
    linear->format.os = XX_OS_UNKNOWN;
    linear->format.arch = XX_ARCH_UNKNOWN;
    linear->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    linear->format.is_executable = true;
}

void xx_linear_destroy_impl(xx_linear_executable *linear)
{
    if (!linear) return;
    xx_linear_parsed_cleanup(linear);
    xx_format_cleanup_extra_parameters(&linear->format);
}

bool xx_linear_check_impl(Abstractformat *format, xx_pd_struct *pd, uint16_t signature)
{
    xx_linear_executable parsed;
    bool result = xx_linear_parse(format, &parsed, pd, signature);
    xx_linear_parsed_cleanup(&parsed);
    return result;
}

bool xx_linear_handle_impl(Abstractformat *format, xx_pd_struct *pd, uint16_t signature)
{
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
    linear->format.file_type = signature == XX_LE_SIGNATURE ? XX_FILE_TYPE_LE : XX_FILE_TYPE_LX;
    linear->format.os = xx_linear_os(linear->target_os);
    linear->format.arch = xx_linear_arch(linear->cpu_type);
    linear->format.format_type = xx_linear_type(linear->module_flags);
    if (linear->format.format_type == XX_TYPE_LIBRARY) xx_format_set_extension(&linear->format, "dll");
    else if (linear->format.format_type == XX_TYPE_DRIVER) xx_format_set_extension(&linear->format, "vxd");
    else xx_format_set_extension(&linear->format, "exe");
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

int64_t xx_linear_get_format_size_impl(Abstractformat *format, xx_pd_struct *pd, uint16_t signature)
{
    return format && (format->base_info_handled || xx_linear_handle_impl(format, pd, signature)) ? format->format_size : -1;
}

bool xx_linear_get_memory_map_impl(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd, uint16_t signature)
{
    xx_linear_executable *linear = (xx_linear_executable *)format;
    int64_t total;
    int64_t binary_size;
    int64_t header_size;
    int64_t first_load = -1;
    uint64_t module;
    uint32_t object_index;
    if (!format || !output || !format->device || !format->base_info_handled || linear->signature != signature || format->base_address < 0 || xx_pd_is_stopped(pd))
        return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_OBJECTS;
    if (mode != XX_MEMORY_MAP_MODE_OBJECTS && mode != XX_MEMORY_MAP_MODE_MAPS) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    binary_size = total - format->base_address;
    header_size = linear->data_pages_offset != 0U && linear->data_pages_offset <= (uint64_t)binary_size ? (int64_t)linear->data_pages_offset
                                                                                                        : (int64_t)linear->linear_offset + XX_LINEAR_HEADER_CORE_SIZE;
    module = format->module_address != XX_INVALID_ADDRESS ? format->module_address : 0U;
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
    if (!xx_memory_map_add_part(output, format->base_address, header_size, module, header_size, XX_FILE_PART_HEADER, 0,
                                signature == XX_LE_SIGNATURE ? "LE header and loader" : "LX header and loader", false))
        return false;
    for (object_index = 0U; object_index < linear->object_count; ++object_index) {
        const xx_linear_object *object = &linear->objects[object_index];
        uint64_t object_address;
        uint64_t covered_virtual = 0U;
        uint32_t object_page;
        if (!xx_linear_add_u64(module, object->base_address, &object_address) || object_address == XX_INVALID_ADDRESS) return false;
        if ((object->flags & XX_LINEAR_OBJECT_EXECUTABLE) != 0U && output->code_base < 0 && object_address <= (uint64_t)INT64_MAX)
            output->code_base = (int64_t)object_address;
        for (object_page = 0U; object_page < object->page_count; ++object_page) {
            uint32_t page_index = object->first_page - 1U + object_page;
            uint64_t page_relative;
            uint64_t physical_size;
            uint64_t address;
            uint64_t remaining;
            uint64_t virtual_size;
            bool physical;
            int64_t offset = -1;
            char name[XX_MEMORY_RECORD_NAME_SIZE];
            if (!xx_linear_mul_u64(object_page, linear->page_size, &covered_virtual) || covered_virtual >= object->virtual_size) break;
            remaining = (uint64_t)object->virtual_size - covered_virtual;
            virtual_size = remaining < linear->page_size ? remaining : linear->page_size;
            if (!xx_linear_add_u64(object_address, covered_virtual, &address) || !xx_linear_page_layout(linear, page_index, &page_relative, &physical_size, &physical))
                return false;
            if (physical) {
                if (physical_size > virtual_size) physical_size = virtual_size;
                if (page_relative > (uint64_t)(INT64_MAX - format->base_address)) return false;
                offset = format->base_address + (int64_t)page_relative;
                if (first_load < 0 || offset < first_load) first_load = offset;
            } else {
                physical_size = 0U;
            }
            (void)xx_rt_snprintf(name, sizeof(name), "Object %u page %u", (unsigned)object_index + 1U, (unsigned)object_page + 1U);
            if (!xx_memory_map_add_part(output, offset, (int64_t)physical_size, address, (int64_t)virtual_size, XX_FILE_PART_OBJECT, (int32_t)object_index + 1, name,
                                        false))
                return false;
        }
        if (object->page_count == 0U || (uint64_t)object->page_count * linear->page_size < object->virtual_size) {
            uint64_t tail_start = (uint64_t)object->page_count * linear->page_size;
            uint64_t address;
            uint64_t tail_size;
            char name[XX_MEMORY_RECORD_NAME_SIZE];
            if (tail_start > object->virtual_size) tail_start = object->virtual_size;
            tail_size = (uint64_t)object->virtual_size - tail_start;
            if (tail_size != 0U) {
                if (!xx_linear_add_u64(object_address, tail_start, &address)) return false;
                (void)xx_rt_snprintf(name, sizeof(name), "Object %u zero-fill", (unsigned)object_index + 1U);
                if (!xx_memory_map_add_part(output, -1, 0, address, (int64_t)tail_size, XX_FILE_PART_OBJECT, (int32_t)object_index + 1, name, false)) return false;
            }
        }
    }
    if (first_load >= 0) output->start_load_offset = first_load;
    if (linear->start_object != 0U && linear->start_object <= linear->object_count) {
        const xx_linear_object *object = &linear->objects[linear->start_object - 1U];
        uint64_t object_address;
        if (linear->entry_offset < object->virtual_size && xx_linear_add_u64(module, object->base_address, &object_address))
            (void)xx_linear_add_u64(object_address, linear->entry_offset, &output->entry_point_address);
    }
    if (format->overlay_size > 0 && !xx_memory_map_add_part(output, format->overlay_offset, format->overlay_size, XX_INVALID_ADDRESS, 0, XX_FILE_PART_OVERLAY,
                                                            (int32_t)linear->object_count + 1, "Overlay", false))
        return false;
    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

static void xx_le_vtable_destroy(Abstractformat *format)
{
    xx_le_destroy((xx_le *)format);
}

void xx_le_init(xx_le *le, xx_io_device *device, int64_t base_address)
{
    if (!le) return;
    xx_linear_init_impl(le, device, base_address, XX_LE_SIGNATURE);
    le->format.file_type = XX_FILE_TYPE_LE;
    xx_format_set_mime_type(&le->format, "application/x-linear-executable");
    xx_format_set_extension(&le->format, "exe");
    le->format.check_is_valid = xx_le_check_is_valid;
    le->format.handle_base_info = xx_le_handle_base_info;
    le->format.get_format_size = xx_le_get_format_size;
    le->format.get_memory_map = xx_le_get_memory_map;
    le->format.destroy = xx_le_vtable_destroy;
}

xx_le *xx_le_create(xx_io_device *device, int64_t base_address)
{
    xx_le *le = (xx_le *)xx_mem_alloc(sizeof(*le));
    if (le) xx_le_init(le, device, base_address);
    return le;
}

void xx_le_destroy(xx_le *le)
{
    xx_linear_destroy_impl(le);
}

void xx_le_free(xx_le *le)
{
    if (!le) return;
    xx_le_destroy(le);
    xx_mem_free(le);
}

bool xx_le_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return xx_linear_check_impl(format, pd, XX_LE_SIGNATURE);
}

bool xx_le_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    return xx_linear_handle_impl(format, pd, XX_LE_SIGNATURE);
}

int64_t xx_le_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return xx_linear_get_format_size_impl(format, pd, XX_LE_SIGNATURE);
}

bool xx_le_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    return xx_linear_get_memory_map_impl(format, mode, output, pd, XX_LE_SIGNATURE);
}

uint32_t xx_le_get_number_of_objects(const xx_le *le)
{
    return le ? le->object_count : 0U;
}

const xx_linear_object *xx_le_get_object(const xx_le *le, uint32_t index)
{
    return le && le->objects && index < le->object_count ? &le->objects[index] : NULL;
}
