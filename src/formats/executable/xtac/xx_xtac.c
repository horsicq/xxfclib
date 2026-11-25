/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/xtac/xx_xtac.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#define XTAC_MAGIC UINT32_C(0x43415458)
#define XTAC_BLCK_MAGIC UINT32_C(0x4b434c42)
#define XTAC_MAX_LINKED_ENTRIES UINT32_C(0x00100000)

static bool xtac_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool xtac_fail(xx_pd_struct *pd, int code, const char *message)
{
    xx_pd_set_error(pd, code, message);
    return false;
}

static bool xtac_span(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static bool xtac_read(const Abstractformat *format, uint64_t relative, void *output, size_t size)
{
    return format && format->device && output && format->base_address >= 0 && relative <= (uint64_t)(INT64_MAX - format->base_address) &&
           size <= (uint64_t)INT64_MAX - relative && xx_io_read_at(format->device, format->base_address + (int64_t)relative, output, size);
}

static bool xtac_reserve(void **items, uint32_t *capacity, uint32_t wanted, size_t item_size)
{
    uint32_t next;
    void *grown;
    if (!items || !capacity || !item_size || wanted > XTAC_MAX_LINKED_ENTRIES || wanted > SIZE_MAX / item_size) return false;
    if (wanted <= *capacity) return true;
    next = *capacity ? *capacity : 16U;
    while (next < wanted) {
        if (next > XTAC_MAX_LINKED_ENTRIES / 2U) {
            next = XTAC_MAX_LINKED_ENTRIES;
            break;
        }
        next *= 2U;
    }
    if (next < wanted || next > SIZE_MAX / item_size) return false;
    grown = xx_mem_realloc(*items, (size_t)next * item_size);
    if (!grown) return false;
    *items = grown;
    *capacity = next;
    return true;
}

static void xtac_clear_parsed(xx_xtac *reader)
{
    if (!reader) return;
    xx_mem_free(reader->module_name);
    xx_mem_free(reader->nt_path);
    xx_mem_free(reader->address_pairs);
    xx_mem_free(reader->block_stubs);
    xx_mem_free(reader->linked_entries);
    xx_mem_free(reader->sections);
    xx_mem_free(reader->symbols);
    reader->module_name = NULL;
    reader->nt_path = NULL;
    reader->address_pairs = NULL;
    reader->block_stubs = NULL;
    reader->linked_entries = NULL;
    reader->sections = NULL;
    reader->symbols = NULL;
    reader->address_pair_count = 0U;
    reader->block_stub_count = 0U;
    reader->linked_entry_count = 0U;
    reader->section_count = 0U;
    reader->symbol_count = 0U;
    reader->has_entry = false;
    reader->entry.offset = -1;
    reader->entry.address = XX_INVALID_ADDRESS;
    xx_mem_zero(&reader->header, sizeof(reader->header));
}

static bool xtac_read_header(const Abstractformat *format, uint64_t available, xx_xtac_header *header)
{
    uint8_t bytes[XX_XTAC_HEADER_SIZE];
    uint32_t *fields;
    size_t index;
    if (!format || !header || available <= XX_XTAC_HEADER_SIZE || !xtac_read(format, 0U, bytes, sizeof(bytes))) return false;
    fields = &header->magic;
    for (index = 0U; index < 14U; ++index) fields[index] = xx_data_get_u32(bytes, sizeof(bytes), index * 4U, false);
    return header->magic == XTAC_MAGIC;
}

static bool xtac_validate_header(const xx_xtac_header *header, uint64_t available)
{
    uint64_t pairs_size;
    if (!header || header->magic != XTAC_MAGIC || header->address_pair_count > XX_XTAC_MAX_ADDRESS_PAIRS || header->module_name_size > XX_XTAC_MAX_NAME_BYTES ||
        header->nt_path_size > XX_XTAC_MAX_NAME_BYTES || (header->module_name_size & 1U) != 0U || (header->nt_path_size & 1U) != 0U)
        return false;
    pairs_size = (uint64_t)header->address_pair_count * 8U;
    if (!xtac_span(header->address_pairs_offset, pairs_size, available) || !xtac_span(header->module_name_offset, header->module_name_size, available) ||
        !xtac_span(header->nt_path_offset, header->nt_path_size, available) || header->head_block_stub_offset > available || header->tail_block_stub_offset > available ||
        header->linked_list_head_offset > available || header->linked_list_tail_offset > available || header->block_stub_code_size > UINT32_MAX - 8U)
        return false;
    if (header->head_block_stub_offset && !xtac_span(header->head_block_stub_offset, 16U, available)) return false;
    if (header->linked_list_head_offset && !xtac_span(header->linked_list_head_offset, 4U, available)) return false;
    return true;
}

static bool xtac_utf8_append(char *output, size_t capacity, size_t *length, uint32_t codepoint)
{
    if (codepoint <= 0x7fU) {
        if (*length + 1U >= capacity) return false;
        output[(*length)++] = (char)codepoint;
    } else if (codepoint <= 0x7ffU) {
        if (*length + 2U >= capacity) return false;
        output[(*length)++] = (char)(0xc0U | (codepoint >> 6U));
        output[(*length)++] = (char)(0x80U | (codepoint & 0x3fU));
    } else if (codepoint <= 0xffffU) {
        if (*length + 3U >= capacity) return false;
        output[(*length)++] = (char)(0xe0U | (codepoint >> 12U));
        output[(*length)++] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[(*length)++] = (char)(0x80U | (codepoint & 0x3fU));
    } else if (codepoint <= 0x10ffffU) {
        if (*length + 4U >= capacity) return false;
        output[(*length)++] = (char)(0xf0U | (codepoint >> 18U));
        output[(*length)++] = (char)(0x80U | ((codepoint >> 12U) & 0x3fU));
        output[(*length)++] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[(*length)++] = (char)(0x80U | (codepoint & 0x3fU));
    } else {
        return false;
    }
    return true;
}

static char *xtac_read_utf16le(const Abstractformat *format, uint32_t offset, uint32_t byte_size)
{
    uint8_t *bytes = NULL;
    char *output;
    size_t units = byte_size / 2U;
    size_t capacity;
    size_t length = 0U;
    size_t index;
    if (!byte_size) return xx_str_dup("");
    if ((byte_size & 1U) != 0U || units > (SIZE_MAX - 1U) / 3U) return NULL;
    capacity = units * 3U + 1U;
    bytes = (uint8_t *)xx_mem_alloc(byte_size);
    output = (char *)xx_mem_alloc(capacity);
    if (!bytes || !output || !xtac_read(format, offset, bytes, byte_size)) {
        xx_mem_free(bytes);
        xx_mem_free(output);
        return NULL;
    }
    for (index = 0U; index < units; ++index) {
        uint32_t codepoint = xx_data_get_u16(bytes, byte_size, index * 2U, false);
        if (codepoint == 0U) {
            size_t rest;
            for (rest = index + 1U; rest < units; ++rest) {
                if (xx_data_get_u16(bytes, byte_size, rest * 2U, false) != 0U) goto fail;
            }
            break;
        }
        if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
            uint32_t low;
            if (++index >= units) goto fail;
            low = xx_data_get_u16(bytes, byte_size, index * 2U, false);
            if (low < 0xdc00U || low > 0xdfffU) goto fail;
            codepoint = UINT32_C(0x10000) + ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
        } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
            goto fail;
        }
        if (!xtac_utf8_append(output, capacity, &length, codepoint)) goto fail;
    }
    output[length] = '\0';
    xx_mem_free(bytes);
    return output;
fail:
    xx_mem_free(bytes);
    xx_mem_free(output);
    return NULL;
}

static bool xtac_load_pairs(xx_xtac *reader)
{
    uint8_t bytes[8];
    uint32_t index;
    uint32_t count = reader->header.address_pair_count;
    if (!count) return true;
    reader->address_pairs = (xx_xtac_address_pair *)xx_mem_alloc((size_t)count * sizeof(*reader->address_pairs));
    if (!reader->address_pairs) return false;
    for (index = 0U; index < count; ++index) {
        uint64_t offset = (uint64_t)reader->header.address_pairs_offset + (uint64_t)index * 8U;
        if (!xtac_read(&reader->format, offset, bytes, sizeof(bytes))) return false;
        reader->address_pairs[index].x86_rva = xx_data_get_u32(bytes, sizeof(bytes), 0U, false);
        reader->address_pairs[index].arm64_rva = xx_data_get_u32(bytes, sizeof(bytes), 4U, false);
    }
    reader->address_pair_count = count;
    return true;
}

static bool xtac_load_block_stubs(xx_xtac *reader, uint64_t available)
{
    uint32_t pointer = reader->header.head_block_stub_offset;
    uint32_t count = 0U;
    if (!pointer) return reader->header.tail_block_stub_offset == 0U;
    reader->block_stubs = (xx_xtac_block_stub *)xx_mem_calloc(XX_XTAC_MAX_BLOCK_STUBS, sizeof(*reader->block_stubs));
    if (!reader->block_stubs) return false;
    while (pointer) {
        uint8_t bytes[16];
        xx_xtac_block_stub *stub;
        uint32_t previous;
        if (count >= XX_XTAC_MAX_BLOCK_STUBS || !xtac_span(pointer, sizeof(bytes), available) || !xtac_read(&reader->format, pointer, bytes, sizeof(bytes))) return false;
        for (previous = 0U; previous < count; ++previous) {
            if (reader->block_stubs[previous].entry_offset == pointer) return false;
        }
        stub = &reader->block_stubs[count++];
        stub->magic = xx_data_get_u32(bytes, sizeof(bytes), 0U, false);
        stub->offset_to_next_entry = xx_data_get_u32(bytes, sizeof(bytes), 4U, false);
        stub->next_entry_offset = xx_data_get_u32(bytes, sizeof(bytes), 8U, false);
        stub->padding = xx_data_get_u32(bytes, sizeof(bytes), 12U, false);
        stub->entry_offset = pointer;
        if (stub->magic != XTAC_BLCK_MAGIC || stub->padding != 0U || (stub->next_entry_offset && !xtac_span(stub->next_entry_offset, 16U, available))) return false;
        pointer = stub->next_entry_offset;
    }
    reader->block_stub_count = count;
    return !reader->header.tail_block_stub_offset || reader->block_stubs[count - 1U].entry_offset == reader->header.tail_block_stub_offset;
}

static bool xtac_load_linked_entries(xx_xtac *reader, uint64_t available)
{
    uint32_t pointer = reader->header.linked_list_head_offset;
    uint32_t capacity = 0U;
    bool terminated = false;
    if (!pointer) return reader->header.linked_list_tail_offset == 0U;
    while (pointer) {
        uint8_t bytes[12];
        uint32_t value;
        uint32_t step;
        uint32_t previous;
        uint32_t read_size = 4U;
        xx_xtac_linked_entry *entry;
        if (reader->linked_entry_count >= XTAC_MAX_LINKED_ENTRIES || !xtac_span(pointer, 4U, available)) return false;
        for (previous = 0U; previous < reader->linked_entry_count; ++previous) {
            if (reader->linked_entries[previous].entry_offset == pointer) return false;
        }
        if (!xtac_read(&reader->format, pointer, bytes, 4U)) return false;
        value = xx_data_get_u32(bytes, sizeof(bytes), 0U, false);
        if (((value >> 28U) & 1U) != 0U) read_size += 4U;
        if (((value >> 28U) & 2U) != 0U) read_size += 4U;
        if (!xtac_span(pointer, read_size, available) || !xtac_read(&reader->format, pointer, bytes, read_size) ||
            !xtac_reserve((void **)&reader->linked_entries, &capacity, reader->linked_entry_count + 1U, sizeof(*reader->linked_entries)))
            return false;
        entry = &reader->linked_entries[reader->linked_entry_count++];
        xx_mem_zero(entry, sizeof(*entry));
        entry->meta_and_offset = value;
        entry->metadata = (uint8_t)(value >> 28U);
        entry->entry_offset = pointer;
        read_size = 4U;
        if ((entry->metadata & 1U) != 0U) {
            entry->has_forward_edge = true;
            entry->forward_edge_address = xx_data_get_u32(bytes, sizeof(bytes), read_size, false);
            read_size += 4U;
        }
        if ((entry->metadata & 2U) != 0U) {
            entry->has_backward_edge = true;
            entry->backward_edge_address = xx_data_get_u32(bytes, sizeof(bytes), read_size, false);
        }
        step = value & UINT32_C(0x0fffffff);
        if (step == UINT32_C(0x0fffffff)) {
            terminated = true;
            break;
        }
        if (!step || step > (UINT32_MAX - pointer) / 4U) return false;
        pointer += step * 4U;
        if (pointer >= available || (reader->header.address_pairs_offset && pointer >= reader->header.address_pairs_offset)) return false;
    }
    return terminated &&
           (!reader->header.linked_list_tail_offset || reader->linked_entries[reader->linked_entry_count - 1U].entry_offset == reader->header.linked_list_tail_offset);
}

static bool xtac_build_sections(xx_xtac *reader, uint64_t available)
{
    uint32_t wanted = 1U + reader->block_stub_count * 2U;
    uint64_t pointer = (uint64_t)reader->header.head_block_stub_offset + 16U;
    uint64_t code_size = (uint64_t)reader->header.block_stub_code_size + 8U;
    uint32_t index;
    reader->sections = (xx_xtac_section *)xx_mem_calloc(wanted, sizeof(*reader->sections));
    if (!reader->sections) return false;
    (void)xx_rt_snprintf(reader->sections[0].name, sizeof(reader->sections[0].name), "header");
    reader->sections[0].file_offset = 0U;
    reader->sections[0].file_size = 0U;
    reader->sections[0].address = 0U;
    reader->sections[0].virtual_size = reader->header.head_block_stub_offset;
    reader->sections[0].permissions = XX_XTAC_PERMISSION_READ;
    reader->sections[0].index = 0U;
    reader->section_count = 1U;
    for (index = 0U; index < reader->block_stub_count; ++index) {
        const xx_xtac_block_stub *stub = &reader->block_stubs[index];
        xx_xtac_section *section;
        uint64_t trans_size;
        if (stub->offset_to_next_entry < code_size || pointer > UINT32_MAX || code_size > UINT32_MAX || !xtac_span(pointer, code_size, available)) return false;
        section = &reader->sections[reader->section_count];
        (void)xx_rt_snprintf(section->name, sizeof(section->name), "blck_code_%u", index);
        section->file_offset = (uint32_t)pointer;
        section->file_size = 0U;
        section->address = (uint32_t)pointer;
        section->virtual_size = (uint32_t)code_size;
        section->permissions = XX_XTAC_PERMISSION_READ | XX_XTAC_PERMISSION_EXECUTE;
        section->index = reader->section_count++;
        pointer += code_size;
        trans_size = (uint64_t)stub->offset_to_next_entry - code_size;
        if (pointer > UINT32_MAX || trans_size > UINT32_MAX || !xtac_span(pointer, trans_size, available)) return false;
        section = &reader->sections[reader->section_count];
        (void)xx_rt_snprintf(section->name, sizeof(section->name), "trans_code_%u", index);
        section->file_offset = (uint32_t)pointer;
        section->file_size = 0U;
        section->address = (uint32_t)pointer;
        section->virtual_size = (uint32_t)trans_size;
        section->permissions = XX_XTAC_PERMISSION_READ | XX_XTAC_PERMISSION_EXECUTE;
        section->index = reader->section_count++;
        pointer += trans_size;
        if (index + 1U < reader->block_stub_count) {
            if (pointer > UINT64_MAX - 16U) return false;
            pointer += 16U;
        }
    }
    return true;
}

static bool xtac_build_symbols(xx_xtac *reader)
{
    uint32_t index;
    uint32_t count = 0U;
    reader->symbols = reader->address_pair_count ? (xx_xtac_symbol *)xx_mem_calloc(reader->address_pair_count, sizeof(*reader->symbols)) : NULL;
    if (reader->address_pair_count && !reader->symbols) return false;
    for (index = 0U; index < reader->address_pair_count; ++index) {
        const xx_xtac_address_pair *pair = &reader->address_pairs[index];
        xx_xtac_symbol *symbol;
        if (pair->x86_rva == UINT32_MAX || pair->arm64_rva == UINT32_MAX) continue;
        symbol = &reader->symbols[count++];
        /* Radare2's XTAC plugin uses its default UINT64_MAX x86 base, which
         * wraps each 32-bit RVA down by one in the public symbol name. Keep
         * the raw pair alongside that compatibility spelling. */
        (void)xx_rt_snprintf(symbol->name, sizeof(symbol->name), "x86.%08x", pair->x86_rva - 1U);
        symbol->x86_rva = pair->x86_rva;
        symbol->arm64_rva = pair->arm64_rva;
        symbol->ordinal = index;
    }
    reader->symbol_count = count;
    return true;
}

static bool xtac_parse(xx_xtac *reader, uint64_t available, xx_pd_struct *pd)
{
    if (!xtac_read_header(&reader->format, available, &reader->header) || !xtac_validate_header(&reader->header, available))
        return xtac_fail(pd, XXFC_ERR_INVALID_ARG, "Invalid XTAC header or bounded region");
    if (xtac_stopped(pd) || !xtac_load_pairs(reader)) return xtac_fail(pd, XXFC_ERR_INVALID_ARG, "Cannot load bounded XTAC address pairs");
    reader->module_name = xtac_read_utf16le(&reader->format, reader->header.module_name_offset, reader->header.module_name_size);
    reader->nt_path = xtac_read_utf16le(&reader->format, reader->header.nt_path_offset, reader->header.nt_path_size);
    if (!reader->module_name || !reader->nt_path) return xtac_fail(pd, XXFC_ERR_INVALID_ARG, "Invalid XTAC UTF-16 metadata");
    if (xtac_stopped(pd) || !xtac_load_block_stubs(reader, available) || !xtac_load_linked_entries(reader, available) || !xtac_build_sections(reader, available) ||
        !xtac_build_symbols(reader))
        return xtac_fail(pd, XXFC_ERR_INVALID_ARG, "Invalid or oversized XTAC structural table");
    return true;
}

static void xtac_vtable_destroy(Abstractformat *format)
{
    xx_xtac_destroy((xx_xtac *)format);
}

void xx_xtac_init(xx_xtac *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_XTAC;
    reader->format.check_is_valid = xx_xtac_check_is_valid;
    reader->format.handle_base_info = xx_xtac_handle_base_info;
    reader->format.get_format_size = xx_xtac_get_format_size;
    reader->format.get_memory_map = xx_xtac_get_memory_map;
    reader->format.destroy = xtac_vtable_destroy;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.arch = XX_ARCH_ARM64;
    reader->format.os = XX_OS_WINDOWS;
    reader->format.format_type = XX_TYPE_CUSTOM;
    reader->format.is_executable = true;
    reader->format.module_address = 0U;
    reader->entry.offset = -1;
    reader->entry.address = XX_INVALID_ADDRESS;
    xx_format_set_extension(&reader->format, "xta;xtac;jc");
    xx_format_set_mime_type(&reader->format, "application/x-xtac");
}

xx_xtac *xx_xtac_create(xx_io_device *device, int64_t base_address)
{
    xx_xtac *reader = (xx_xtac *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_xtac_init(reader, device, base_address);
    return reader;
}

void xx_xtac_destroy(xx_xtac *reader)
{
    if (!reader) return;
    xtac_clear_parsed(reader);
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_xtac_free(xx_xtac *reader)
{
    if (!reader) return;
    xx_xtac_destroy(reader);
    xx_mem_free(reader);
}

bool xx_xtac_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    uint8_t magic[4];
    (void)pd;
    return format && format->device && format->base_address >= 0 && xtac_read(format, 0U, magic, sizeof(magic)) &&
           xx_data_get_u32(magic, sizeof(magic), 0U, false) == XTAC_MAGIC;
}

bool xx_xtac_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    xx_xtac *reader = (xx_xtac *)format;
    int64_t total;
    uint64_t available;
    if (!reader || !format->device || format->base_address < 0 || xtac_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    xtac_clear_parsed(reader);
    if (!xtac_parse(reader, available, pd)) {
        xtac_clear_parsed(reader);
        return false;
    }
    format->format_size = (int64_t)available;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_FILE_TYPE_XTAC;
    format->endian = XX_ENDIAN_LITTLE;
    format->arch = XX_ARCH_ARM64;
    format->os = XX_OS_WINDOWS;
    format->format_type = XX_TYPE_CUSTOM;
    format->is_executable = true;
    format->module_address = 0U;
    format->number_of_symbols = reader->symbol_count;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_xtac_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_xtac_handle_base_info(format, pd)) ? format->format_size : -1;
}

bool xx_xtac_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_xtac *reader = (xx_xtac *)format;
    uint32_t index;
    if (!reader || !output || xtac_stopped(pd) || (!format->base_info_handled && !xx_xtac_handle_base_info(format, pd))) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_SECTIONS;
    if (mode != XX_MEMORY_MAP_MODE_SECTIONS) return false;
    output->binary_offset = format->base_address;
    output->binary_size = format->format_size;
    output->module_address = 0U;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = reader->section_count > 1U ? (int64_t)reader->sections[1].address : -1;
    output->start_load_offset = format->base_address;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = XX_MEMORY_MAP_MODE_SECTIONS;
    output->is_image = true;
    for (index = 0U; index < reader->section_count; ++index) {
        const xx_xtac_section *section = &reader->sections[index];
        if (!section->virtual_size) continue;
        if (!xx_memory_map_add_part(output, format->base_address + section->file_offset, section->virtual_size, section->address, section->virtual_size,
                                    XX_FILE_PART_SECTION, (int32_t)index, section->name, false))
            return false;
    }
    return xx_memory_map_finalize(output);
}

#define XTAC_READY(reader, pd) ((reader) && ((reader)->format.base_info_handled || xx_xtac_handle_base_info(&(reader)->format, (pd))))

const xx_xtac_header *xx_xtac_get_header(xx_xtac *reader, xx_pd_struct *pd)
{
    return XTAC_READY(reader, pd) ? &reader->header : NULL;
}

const char *xx_xtac_get_module_name(xx_xtac *reader, xx_pd_struct *pd)
{
    return XTAC_READY(reader, pd) ? reader->module_name : NULL;
}

const char *xx_xtac_get_nt_path(xx_xtac *reader, xx_pd_struct *pd)
{
    return XTAC_READY(reader, pd) ? reader->nt_path : NULL;
}

uint32_t xx_xtac_get_number_of_address_pairs(xx_xtac *reader, xx_pd_struct *pd)
{
    return XTAC_READY(reader, pd) ? reader->address_pair_count : 0U;
}

const xx_xtac_address_pair *xx_xtac_get_address_pair(xx_xtac *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_xtac_get_number_of_address_pairs(reader, pd) ? &reader->address_pairs[index] : NULL;
}

uint32_t xx_xtac_get_number_of_block_stubs(xx_xtac *reader, xx_pd_struct *pd)
{
    return XTAC_READY(reader, pd) ? reader->block_stub_count : 0U;
}

const xx_xtac_block_stub *xx_xtac_get_block_stub(xx_xtac *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_xtac_get_number_of_block_stubs(reader, pd) ? &reader->block_stubs[index] : NULL;
}

uint32_t xx_xtac_get_number_of_linked_entries(xx_xtac *reader, xx_pd_struct *pd)
{
    return XTAC_READY(reader, pd) ? reader->linked_entry_count : 0U;
}

const xx_xtac_linked_entry *xx_xtac_get_linked_entry(xx_xtac *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_xtac_get_number_of_linked_entries(reader, pd) ? &reader->linked_entries[index] : NULL;
}

uint32_t xx_xtac_get_number_of_sections(xx_xtac *reader, xx_pd_struct *pd)
{
    return XTAC_READY(reader, pd) ? reader->section_count : 0U;
}

const xx_xtac_section *xx_xtac_get_section(xx_xtac *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_xtac_get_number_of_sections(reader, pd) ? &reader->sections[index] : NULL;
}

const xx_xtac_entry *xx_xtac_get_entry(xx_xtac *reader, xx_pd_struct *pd)
{
    return XTAC_READY(reader, pd) && reader->has_entry ? &reader->entry : NULL;
}

#undef XTAC_READY
