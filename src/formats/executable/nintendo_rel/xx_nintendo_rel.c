/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/nintendo_rel/xx_nintendo_rel.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool rel_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool rel_span(uint64_t offset, uint64_t size, uint64_t available)
{
    return offset <= available && size <= available - offset;
}

static bool rel_read(const Abstractformat *format, uint64_t available, uint64_t offset, void *data, size_t size)
{
    if (!format || !format->device || !data || format->base_address < 0 || !rel_span(offset, (uint64_t)size, available) || offset > (uint64_t)INT64_MAX ||
        format->base_address > INT64_MAX - (int64_t)offset)
        return false;
    return xx_io_read_at(format->device, format->base_address + (int64_t)offset, data, size);
}

static void rel_clear_arrays(xx_nintendo_rel *reader)
{
    if (!reader) return;
    xx_mem_free(reader->relocations);
    xx_mem_free(reader->imports);
    xx_mem_free(reader->sections);
    reader->relocations = NULL;
    reader->imports = NULL;
    reader->sections = NULL;
    reader->relocation_count = 0U;
    reader->import_count = 0U;
    reader->section_count = 0U;
}

static void rel_decode_header(xx_nintendo_rel_header *header, const uint8_t bytes[XX_NINTENDO_REL_HEADER_SIZE])
{
    header->module_id = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x00U, true);
    header->next_module_address = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x04U, true);
    header->previous_module_address = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x08U, true);
    header->section_count = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x0cU, true);
    header->section_table_offset = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x10U, true);
    header->module_name_offset = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x14U, true);
    header->module_name_size = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x18U, true);
    header->version = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x1cU, true);
    header->bss_size = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x20U, true);
    header->relocation_table_offset = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x24U, true);
    header->import_table_offset = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x28U, true);
    header->import_table_size = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x2cU, true);
    header->prolog_section = bytes[0x30U];
    header->epilog_section = bytes[0x31U];
    header->unresolved_section = bytes[0x32U];
    header->bss_section = bytes[0x33U];
    header->prolog_offset = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x34U, true);
    header->epilog_offset = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x38U, true);
    header->unresolved_offset = xx_data_get_u32(bytes, XX_NINTENDO_REL_HEADER_SIZE, 0x3cU, true);
}

static bool rel_scan_relocation_table(const Abstractformat *format, uint64_t available, uint32_t table_offset, uint32_t section_count, uint32_t *count, uint64_t *end,
                                      xx_pd_struct *pd)
{
    uint64_t offset = table_offset;
    uint32_t result = 0U;
    uint8_t bytes[8];
    if (table_offset == 0U || (table_offset & 3U) != 0U) return false;
    for (;;) {
        uint8_t type;
        if (result >= XX_NINTENDO_REL_MAX_RELOCATIONS || rel_stopped(pd) || !rel_read(format, available, offset, bytes, sizeof(bytes))) return false;
        type = bytes[2];
        offset += sizeof(bytes);
        if (type == XX_NINTENDO_REL_R_RVL_STOP) {
            *count = result;
            *end = offset;
            return true;
        }
        if (type == XX_NINTENDO_REL_R_RVL_SECT && bytes[3] >= section_count) return false;
        ++result;
    }
}

static bool rel_assign_section_addresses(xx_nintendo_rel *reader, uint32_t format_size)
{
    uint32_t index;
    uint64_t bss_address = (uint64_t)reader->load_address + format_size + XX_NINTENDO_REL_BSS_ADDRESS_GAP;
    if (bss_address > UINT32_MAX) return false;
    for (index = 0U; index < reader->section_count; ++index) {
        xx_nintendo_rel_section *section = &reader->sections[index];
        if (section->size == 0U) {
            section->virtual_address = 0U;
        } else if (section->bss) {
            section->virtual_address = (uint32_t)bss_address;
        } else {
            uint64_t address = (uint64_t)reader->load_address + section->file_offset;
            if (address > UINT32_MAX) return false;
            section->virtual_address = (uint32_t)address;
        }
    }
    return true;
}

static bool rel_fill_relocations(xx_nintendo_rel *reader, uint64_t available, xx_pd_struct *pd)
{
    uint32_t import_index;
    uint32_t output_index = 0U;
    uint8_t bytes[8];
    for (import_index = 0U; import_index < reader->import_count; ++import_index) {
        xx_nintendo_rel_import *import_entry = &reader->imports[import_index];
        uint64_t table_offset = import_entry->relocation_table_offset;
        uint32_t patch_address = 0U;
        uint8_t patch_section = UINT8_MAX;
        uint32_t local_index;
        import_entry->first_relocation = output_index;
        import_entry->resolvable = import_entry->module_id == XX_NINTENDO_REL_MAIN_DOL_MODULE || import_entry->module_id == reader->header.module_id;
        for (local_index = 0U; local_index < import_entry->relocation_count; ++local_index) {
            xx_nintendo_rel_relocation *relocation;
            uint16_t delta;
            uint8_t type;
            uint8_t section;
            if (output_index >= reader->relocation_count || rel_stopped(pd) || !rel_read(&reader->format, available, table_offset, bytes, sizeof(bytes))) return false;
            relocation = &reader->relocations[output_index++];
            delta = xx_data_get_u16(bytes, sizeof(bytes), 0U, true);
            type = bytes[2];
            section = bytes[3];
            relocation->table_offset = (uint32_t)table_offset;
            relocation->import_index = import_index;
            relocation->module_id = import_entry->module_id;
            relocation->offset_delta = delta;
            relocation->type = type;
            relocation->target_section = section;
            relocation->addend = xx_data_get_u32(bytes, sizeof(bytes), 4U, true);
            relocation->patch_file_offset = XX_NINTENDO_REL_INVALID_FILE_OFFSET;
            relocation->patch_section = patch_section;
            if (type == XX_NINTENDO_REL_R_RVL_SECT) {
                if (section >= reader->section_count || reader->sections[section].virtual_address == 0U) return false;
                patch_section = section;
                patch_address = reader->sections[section].virtual_address;
                relocation->patch_section = patch_section;
                relocation->patch_address = patch_address;
            } else if (patch_section != UINT8_MAX) {
                const xx_nintendo_rel_section *source = &reader->sections[patch_section];
                uint64_t next = (uint64_t)patch_address + delta;
                uint64_t relative;
                if (next > UINT32_MAX || next < source->virtual_address) return false;
                patch_address = (uint32_t)next;
                relative = (uint64_t)patch_address - source->virtual_address;
                relocation->patch_section = patch_section;
                relocation->patch_address = patch_address;
                if (relative < source->size) {
                    relocation->patch_address_valid = true;
                    if (!source->bss && relative <= UINT32_MAX - source->file_offset) relocation->patch_file_offset = source->file_offset + (uint32_t)relative;
                }
            }
            table_offset += sizeof(bytes);
        }
    }
    return output_index == reader->relocation_count;
}

void xx_nintendo_rel_init(xx_nintendo_rel *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_NINTENDO_REL;
    reader->format.check_is_valid = xx_nintendo_rel_check_is_valid;
    reader->format.handle_base_info = xx_nintendo_rel_handle_base_info;
    reader->format.get_format_size = xx_nintendo_rel_get_format_size;
    reader->format.get_memory_map = xx_nintendo_rel_get_memory_map;
    reader->format.endian = XX_ENDIAN_BIG;
    reader->format.arch = XX_ARCH_PPC;
    reader->format.os = XX_OS_WII;
    reader->format.format_type = XX_TYPE_OBJECT;
    reader->format.is_executable = true;
    xx_format_set_extension(&reader->format, "rel");
    xx_format_set_mime_type(&reader->format, "application/x-nintendo-wii-rel");
}

xx_nintendo_rel *xx_nintendo_rel_create(xx_io_device *device, int64_t base_address)
{
    xx_nintendo_rel *reader = (xx_nintendo_rel *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_nintendo_rel_init(reader, device, base_address);
    return reader;
}

void xx_nintendo_rel_destroy(xx_nintendo_rel *reader)
{
    if (!reader) return;
    rel_clear_arrays(reader);
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_nintendo_rel_free(xx_nintendo_rel *reader)
{
    if (!reader) return;
    xx_nintendo_rel_destroy(reader);
    xx_mem_free(reader);
}

bool xx_nintendo_rel_set_load_address(xx_nintendo_rel *reader, uint32_t load_address)
{
    if (!reader || reader->format.base_info_handled) return false;
    reader->load_address = load_address;
    return true;
}

bool xx_nintendo_rel_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    return xx_nintendo_rel_handle_base_info(self, pd);
}

bool xx_nintendo_rel_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_nintendo_rel *reader = (xx_nintendo_rel *)self;
    uint8_t header_bytes[XX_NINTENDO_REL_HEADER_SIZE];
    uint8_t extension_bytes[12];
    uint8_t entry_bytes[8];
    uint32_t relocation_counts[XX_NINTENDO_REL_MAX_IMPORTS];
    uint64_t available;
    uint64_t extent;
    uint64_t table_end;
    uint32_t index;
    uint32_t bss_count = 0U;
    uint32_t total_relocations = 0U;
    bool has_stored_executable = false;
    int64_t total;

    if (!reader || !self->device || self->base_address < 0 || rel_stopped(pd)) return false;
    if (self->base_info_handled) return self->is_valid;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    available = (uint64_t)(total - self->base_address);
    if (!rel_read(self, available, 0U, header_bytes, sizeof(header_bytes))) return false;
    rel_decode_header(&reader->header, header_bytes);
    if (reader->header.version > 3U || reader->header.section_count == 0U || reader->header.section_count > XX_NINTENDO_REL_MAX_SECTIONS ||
        (reader->header.import_table_size & 7U) != 0U || reader->header.import_table_size / 8U > XX_NINTENDO_REL_MAX_IMPORTS)
        return false;

    extent = reader->header.version == 3U ? XX_NINTENDO_REL_V3_HEADER_SIZE : XX_NINTENDO_REL_HEADER_SIZE;
    if (reader->header.version == 3U) {
        if (!rel_read(self, available, XX_NINTENDO_REL_HEADER_SIZE, extension_bytes, sizeof(extension_bytes))) return false;
        reader->v3.section_alignment = xx_data_get_u32(extension_bytes, sizeof(extension_bytes), 0U, true);
        reader->v3.bss_alignment = xx_data_get_u32(extension_bytes, sizeof(extension_bytes), 4U, true);
        reader->v3.fix_size = xx_data_get_u32(extension_bytes, sizeof(extension_bytes), 8U, true);
        if (reader->v3.section_alignment == 0U || reader->v3.bss_alignment == 0U) return false;
    }

    table_end = (uint64_t)reader->header.section_table_offset + (uint64_t)reader->header.section_count * 8U;
    if (!rel_span(reader->header.section_table_offset, (uint64_t)reader->header.section_count * 8U, available)) return false;
    if (table_end > extent) extent = table_end;
    reader->section_count = reader->header.section_count;
    reader->sections = (xx_nintendo_rel_section *)xx_mem_calloc(reader->section_count, sizeof(*reader->sections));
    if (!reader->sections) goto fail;
    for (index = 0U; index < reader->section_count; ++index) {
        xx_nintendo_rel_section *section = &reader->sections[index];
        uint64_t offset = (uint64_t)reader->header.section_table_offset + (uint64_t)index * 8U;
        uint64_t section_end;
        if (rel_stopped(pd) || !rel_read(self, available, offset, entry_bytes, sizeof(entry_bytes))) goto fail;
        section->packed_offset = xx_data_get_u32(entry_bytes, sizeof(entry_bytes), 0U, true);
        section->file_offset = section->packed_offset & UINT32_C(0xfffffffc);
        section->size = xx_data_get_u32(entry_bytes, sizeof(entry_bytes), 4U, true);
        section->index = index;
        section->executable = (section->packed_offset & 1U) != 0U;
        if ((section->packed_offset & 2U) != 0U) goto fail;
        if (section->size == 0U) continue;
        if (section->file_offset == 0U) {
            if (++bss_count > 1U) goto fail;
            section->bss = true;
        } else {
            if (!rel_span(section->file_offset, section->size, available)) goto fail;
            section_end = (uint64_t)section->file_offset + section->size;
            if (section_end > extent) extent = section_end;
            if (section->executable) has_stored_executable = true;
        }
    }
    if (!has_stored_executable || (bss_count == 0U) != (reader->header.bss_size == 0U)) goto fail;

    reader->import_count = reader->header.import_table_size / 8U;
    if (!rel_span(reader->header.import_table_offset, reader->header.import_table_size, available)) goto fail;
    table_end = (uint64_t)reader->header.import_table_offset + reader->header.import_table_size;
    if (table_end > extent) extent = table_end;
    if (reader->import_count > 0U) {
        reader->imports = (xx_nintendo_rel_import *)xx_mem_calloc(reader->import_count, sizeof(*reader->imports));
        if (!reader->imports) goto fail;
    }
    for (index = 0U; index < reader->import_count; ++index) {
        xx_nintendo_rel_import *import_entry = &reader->imports[index];
        uint64_t offset = (uint64_t)reader->header.import_table_offset + (uint64_t)index * 8U;
        uint64_t relocation_end;
        if (rel_stopped(pd) || !rel_read(self, available, offset, entry_bytes, sizeof(entry_bytes))) goto fail;
        import_entry->module_id = xx_data_get_u32(entry_bytes, sizeof(entry_bytes), 0U, true);
        import_entry->relocation_table_offset = xx_data_get_u32(entry_bytes, sizeof(entry_bytes), 4U, true);
        if (!rel_scan_relocation_table(self, available, import_entry->relocation_table_offset, reader->section_count, &relocation_counts[index], &relocation_end, pd) ||
            relocation_counts[index] > XX_NINTENDO_REL_MAX_RELOCATIONS - total_relocations)
            goto fail;
        import_entry->relocation_count = relocation_counts[index];
        total_relocations += relocation_counts[index];
        if (relocation_end > extent) extent = relocation_end;
    }
    if (reader->header.module_name_offset != 0U && reader->header.module_name_size != 0U) {
        uint64_t name_end = (uint64_t)reader->header.module_name_offset + reader->header.module_name_size;
        if (!rel_span(reader->header.module_name_offset, reader->header.module_name_size, available)) goto fail;
        if (name_end > extent) extent = name_end;
    }
    if (extent > UINT32_MAX || extent > (uint64_t)INT64_MAX) goto fail;
    if (!rel_assign_section_addresses(reader, (uint32_t)extent)) goto fail;
    reader->relocation_count = total_relocations;
    if (reader->relocation_count > 0U) {
        reader->relocations = (xx_nintendo_rel_relocation *)xx_mem_calloc(reader->relocation_count, sizeof(*reader->relocations));
        if (!reader->relocations || !rel_fill_relocations(reader, available, pd)) goto fail;
    }

    self->format_size = (int64_t)extent;
    self->overlay_offset = available > extent ? (int64_t)extent : -1;
    self->overlay_size = available > extent ? (int64_t)(available - extent) : 0;
    self->is_valid = true;
    self->base_info_handled = true;
    return !rel_stopped(pd);

fail:
    rel_clear_arrays(reader);
    xx_mem_zero(&reader->header, sizeof(reader->header));
    xx_mem_zero(&reader->v3, sizeof(reader->v3));
    return false;
}

int64_t xx_nintendo_rel_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_nintendo_rel_handle_base_info(self, pd)) ? self->format_size : -1;
}

const xx_nintendo_rel_header *xx_nintendo_rel_get_header(xx_nintendo_rel *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_nintendo_rel_handle_base_info(&reader->format, pd)) ? &reader->header : NULL;
}

const xx_nintendo_rel_section *xx_nintendo_rel_get_section(xx_nintendo_rel *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_nintendo_rel_handle_base_info(&reader->format, pd)) && index < reader->section_count
               ? &reader->sections[index]
               : NULL;
}

const xx_nintendo_rel_import *xx_nintendo_rel_get_import(xx_nintendo_rel *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_nintendo_rel_handle_base_info(&reader->format, pd)) && index < reader->import_count ? &reader->imports[index]
                                                                                                                                                 : NULL;
}

const xx_nintendo_rel_relocation *xx_nintendo_rel_get_relocation(xx_nintendo_rel *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_nintendo_rel_handle_base_info(&reader->format, pd)) && index < reader->relocation_count
               ? &reader->relocations[index]
               : NULL;
}

bool xx_nintendo_rel_get_special_symbol_address(xx_nintendo_rel *reader, xx_nintendo_rel_special_symbol symbol, uint32_t *address, xx_pd_struct *pd)
{
    uint8_t section_index;
    uint32_t offset;
    const xx_nintendo_rel_section *section;
    if (!reader || !address || (!reader->format.base_info_handled && !xx_nintendo_rel_handle_base_info(&reader->format, pd))) return false;
    switch (symbol) {
        case XX_NINTENDO_REL_SYMBOL_PROLOG:
            section_index = reader->header.prolog_section;
            offset = reader->header.prolog_offset;
            break;
        case XX_NINTENDO_REL_SYMBOL_EPILOG:
            section_index = reader->header.epilog_section;
            offset = reader->header.epilog_offset;
            break;
        case XX_NINTENDO_REL_SYMBOL_UNRESOLVED:
            section_index = reader->header.unresolved_section;
            offset = reader->header.unresolved_offset;
            break;
        default: return false;
    }
    if (section_index == 0U || section_index >= reader->section_count) return false;
    section = &reader->sections[section_index];
    if (section->virtual_address == 0U || offset >= section->size || section->virtual_address > UINT32_MAX - offset) return false;
    *address = section->virtual_address + offset;
    return true;
}

static uint32_t rel_read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) | ((uint32_t)data[2] << 8U) | data[3];
}

static void rel_write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

bool xx_nintendo_rel_apply_relocations(xx_nintendo_rel *reader, uint8_t *module, size_t module_size, xx_nintendo_rel_apply_report *report, xx_pd_struct *pd)
{
    xx_nintendo_rel_apply_report result;
    uint32_t import_index;
    bool success = true;
    xx_mem_zero(&result, sizeof(result));
    if (!reader || !module || (!reader->format.base_info_handled && !xx_nintendo_rel_handle_base_info(&reader->format, pd)) || reader->format.format_size < 0 ||
        (uint64_t)reader->format.format_size > module_size)
        return false;
    for (import_index = 0U; import_index < reader->import_count; ++import_index) {
        const xx_nintendo_rel_import *import_entry = &reader->imports[import_index];
        uint32_t local_index;
        if (!import_entry->resolvable) {
            ++result.external_imports;
            result.external_relocations += import_entry->relocation_count;
            continue;
        }
        for (local_index = 0U; local_index < import_entry->relocation_count; ++local_index) {
            const xx_nintendo_rel_relocation *relocation = &reader->relocations[import_entry->first_relocation + local_index];
            uint32_t old_value;
            uint32_t new_value;
            uint32_t symbol_address;
            uint32_t expression;
            size_t write_size;
            if (rel_stopped(pd)) return false;
            if (relocation->type == XX_NINTENDO_REL_R_RVL_SECT || relocation->type == XX_NINTENDO_REL_R_RVL_NONE) continue;
            if (!relocation->patch_address_valid || relocation->patch_file_offset == XX_NINTENDO_REL_INVALID_FILE_OFFSET || relocation->patch_file_offset > module_size ||
                module_size - relocation->patch_file_offset < 4U) {
                ++result.invalid_relocations;
                success = false;
                continue;
            }
            if (import_entry->module_id == XX_NINTENDO_REL_MAIN_DOL_MODULE) {
                symbol_address = 0U;
            } else if (relocation->target_section < reader->section_count && reader->sections[relocation->target_section].virtual_address != 0U) {
                symbol_address = reader->sections[relocation->target_section].virtual_address;
            } else {
                ++result.invalid_relocations;
                success = false;
                continue;
            }
            expression = symbol_address + relocation->addend;
            old_value = rel_read_be32(module + relocation->patch_file_offset);
            new_value = old_value;
            write_size = 4U;
            switch (relocation->type) {
                case XX_NINTENDO_REL_R_PPC_ADDR32: new_value = expression; break;
                case XX_NINTENDO_REL_R_PPC_ADDR24: new_value = (old_value & UINT32_C(0xfc000003)) | (expression & UINT32_C(0x03fffffc)); break;
                case XX_NINTENDO_REL_R_PPC_REL24:
                    new_value = (old_value & UINT32_C(0xfc000003)) | ((expression - relocation->patch_address) & UINT32_C(0x03fffffc));
                    break;
                case XX_NINTENDO_REL_R_PPC_ADDR16_LO:
                    new_value = (expression << 16U) | (old_value & UINT32_C(0x0000ffff));
                    write_size = 2U;
                    break;
                case XX_NINTENDO_REL_R_PPC_ADDR16_HI:
                    new_value = (expression & UINT32_C(0xffff0000)) | (old_value & UINT32_C(0x0000ffff));
                    write_size = 2U;
                    break;
                case XX_NINTENDO_REL_R_PPC_ADDR16_HA:
                    new_value = ((((expression >> 16U) + ((expression & UINT32_C(0x8000)) ? 1U : 0U)) & UINT32_C(0xffff)) << 16U) | (old_value & UINT32_C(0x0000ffff));
                    write_size = 2U;
                    break;
                default:
                    ++result.unsupported_relocations;
                    success = false;
                    continue;
            }
            if (write_size == 4U) {
                rel_write_be32(module + relocation->patch_file_offset, new_value);
            } else {
                module[relocation->patch_file_offset] = (uint8_t)(new_value >> 24U);
                module[relocation->patch_file_offset + 1U] = (uint8_t)(new_value >> 16U);
            }
            ++result.applied_relocations;
        }
    }
    if (report) *report = result;
    return success && !rel_stopped(pd);
}

bool xx_nintendo_rel_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_nintendo_rel *reader = (xx_nintendo_rel *)self;
    uint32_t index;
    uint32_t entry = 0U;
    if (!reader || !output || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_SECTIONS) ||
        (!self->base_info_handled && !xx_nintendo_rel_handle_base_info(self, pd)))
        return false;
    output->binary_offset = self->base_address;
    output->binary_size = self->format_size;
    output->module_address = reader->load_address;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = reader->load_address;
    output->start_load_offset = self->base_address;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = XX_MEMORY_MAP_MODE_SECTIONS;
    output->is_image = true;
    if (xx_nintendo_rel_get_special_symbol_address(reader, XX_NINTENDO_REL_SYMBOL_PROLOG, &entry, pd)) output->entry_point_address = entry;
    for (index = 0U; index < reader->section_count; ++index) {
        const xx_nintendo_rel_section *section = &reader->sections[index];
        char name[32];
        if (section->size == 0U) continue;
        if (section->bss) {
            (void)snprintf(name, sizeof(name), "bss");
            if (!xx_memory_map_add_part(output, -1, 0, section->virtual_address, section->size, XX_FILE_PART_SECTION, (int32_t)index, name, false)) return false;
        } else {
            (void)snprintf(name, sizeof(name), "%s_%u", section->executable ? "text" : "data", index);
            if (!xx_memory_map_add_part(output, self->base_address + section->file_offset, section->size, section->virtual_address, section->size, XX_FILE_PART_SECTION,
                                        (int32_t)index, name, false))
                return false;
        }
    }
    return !rel_stopped(pd) && xx_memory_map_finalize(output);
}
