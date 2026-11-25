/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/xcoff/xx_xcoff.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool xcoff_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool xcoff_magic_is_supported(uint16_t magic)
{
    return magic == UINT16_C(0x0198) || magic == UINT16_C(0x019d) || magic == UINT16_C(0x019f) || magic == UINT16_C(0x01d8) || magic == UINT16_C(0x01dd) ||
           magic == UINT16_C(0x01df);
}

static bool xcoff_read_relative(const Abstractformat *format, uint64_t available, uint64_t offset, void *buffer, size_t size)
{
    return format && format->device && format->base_address >= 0 && offset <= available && (uint64_t)size <= available - offset && offset <= (uint64_t)INT64_MAX &&
           format->base_address <= INT64_MAX - (int64_t)offset && xx_io_read_at(format->device, format->base_address + (int64_t)offset, buffer, size);
}

static void xcoff_copy_fixed_name(char *destination, size_t capacity, const uint8_t *source, size_t source_size)
{
    size_t length = 0U;
    if (!destination || !capacity) return;
    while (length < source_size && source[length] && length + 1U < capacity) {
        destination[length] = (char)source[length];
        ++length;
    }
    destination[length] = '\0';
}

static bool xcoff_load_long_name(const Abstractformat *format, uint64_t available, uint64_t loader_offset, uint32_t loader_size, const xx_xcoff_loader_header *header,
                                 uint32_t string_offset, char destination[256])
{
    uint64_t relative;
    uint32_t remaining;
    uint8_t byte;
    size_t length = 0U;
    if (!header || string_offset >= header->string_table_length || header->string_table_offset > loader_size ||
        header->string_table_length > loader_size - header->string_table_offset)
        return false;
    remaining = header->string_table_length - string_offset;
    relative = loader_offset + header->string_table_offset + string_offset;
    while (remaining--) {
        if (!xcoff_read_relative(format, available, relative++, &byte, 1U)) return false;
        if (!byte) {
            destination[length] = '\0';
            return length != 0U;
        }
        if (length + 1U < 256U) destination[length++] = (char)byte;
    }
    destination[length] = '\0';
    return false;
}

static bool xcoff_parse_loader_symbol(const Abstractformat *format, uint64_t available, uint64_t loader_offset, uint32_t loader_size,
                                      const xx_xcoff_loader_header *loader_header, uint32_t index, xx_xcoff_loader_symbol *symbol)
{
    uint8_t bytes[XX_XCOFF_LOADER_SYMBOL_SIZE];
    uint64_t offset = loader_offset + XX_XCOFF_LOADER_HEADER_SIZE + (uint64_t)index * XX_XCOFF_LOADER_SYMBOL_SIZE;
    bool external_name;
    if (!symbol || !xcoff_read_relative(format, available, offset, bytes, sizeof(bytes))) return false;
    external_name = bytes[0] == 0U && bytes[1] == 0U && bytes[2] == 0U && bytes[3] == 0U;
    symbol->has_inline_name = !external_name;
    if (external_name) {
        uint32_t string_offset = xx_data_get_u32(bytes, sizeof(bytes), 4U, true);
        if (string_offset && !xcoff_load_long_name(format, available, loader_offset, loader_size, loader_header, string_offset, symbol->name)) return false;
    } else {
        xcoff_copy_fixed_name(symbol->name, sizeof(symbol->name), bytes, 8U);
    }
    symbol->value = xx_data_get_u32(bytes, sizeof(bytes), 8U, true);
    symbol->section_number = xx_data_get_u16(bytes, sizeof(bytes), 12U, true);
    symbol->type_flags = bytes[14];
    symbol->storage_class = bytes[15];
    symbol->import_file_index = xx_data_get_u32(bytes, sizeof(bytes), 16U, true);
    symbol->parameter_hash = xx_data_get_u32(bytes, sizeof(bytes), 20U, true);
    return true;
}

static bool xcoff_resolve_entry(xx_xcoff *reader, uint64_t available)
{
    uint32_t index;
    for (index = 0U; index < reader->loader_symbol_count; ++index) {
        const xx_xcoff_loader_symbol *symbol = &reader->loader_symbols[index];
        const xx_xcoff_section *pointer_section;
        const xx_xcoff_section *text_section;
        uint64_t delta;
        uint64_t file_offset;
        uint8_t entry_bytes[4];
        uint32_t entry;
        if (strcmp(symbol->name, "__start") != 0) continue;
        if (!symbol->section_number || symbol->section_number > reader->section_count || !reader->header.text_section ||
            reader->header.text_section > reader->section_count)
            return false;
        pointer_section = &reader->sections[symbol->section_number - 1U];
        text_section = &reader->sections[reader->header.text_section - 1U];
        if (symbol->value < pointer_section->virtual_address) return false;
        delta = (uint64_t)symbol->value - pointer_section->virtual_address;
        if (delta > pointer_section->size || pointer_section->size - delta < sizeof(entry_bytes)) return false;
        file_offset = (uint64_t)pointer_section->file_offset + delta;
        if (!xcoff_read_relative(&reader->format, available, file_offset, entry_bytes, sizeof(entry_bytes))) return false;
        entry = xx_data_get_u32(entry_bytes, sizeof(entry_bytes), 0U, true);
        if (entry < text_section->virtual_address || (uint64_t)entry - text_section->virtual_address >= text_section->size) return false;
        reader->entry_point = entry;
        reader->has_entry_point = true;
        return true;
    }
    return false;
}

void xx_xcoff_init(xx_xcoff *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_XCOFF;
    reader->format.check_is_valid = xx_xcoff_check_is_valid;
    reader->format.handle_base_info = xx_xcoff_handle_base_info;
    reader->format.get_format_size = xx_xcoff_get_format_size;
    reader->format.get_memory_map = xx_xcoff_get_memory_map;
    reader->format.endian = XX_ENDIAN_BIG;
    reader->format.arch = XX_ARCH_PPC;
    reader->format.os = XX_OS_AIX;
    reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    reader->format.is_executable = true;
    xx_format_set_extension(&reader->format, "xcoff");
    xx_format_set_mime_type(&reader->format, "application/x-aix-xcoff32");
}

xx_xcoff *xx_xcoff_create(xx_io_device *device, int64_t base_address)
{
    xx_xcoff *reader = (xx_xcoff *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_xcoff_init(reader, device, base_address);
    return reader;
}

void xx_xcoff_destroy(xx_xcoff *reader)
{
    if (!reader) return;
    xx_mem_free(reader->imports);
    xx_mem_free(reader->loader_symbols);
    xx_mem_free(reader->sections);
    reader->imports = NULL;
    reader->loader_symbols = NULL;
    reader->sections = NULL;
    reader->import_count = 0U;
    reader->loader_symbol_count = 0U;
    reader->section_count = 0U;
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_xcoff_free(xx_xcoff *reader)
{
    if (!reader) return;
    xx_xcoff_destroy(reader);
    xx_mem_free(reader);
}

bool xx_xcoff_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t bytes[XX_XCOFF_FILE_HEADER_SIZE];
    int64_t total;
    uint64_t available;
    uint16_t sections;
    (void)pd;
    if (!self || !self->device || self->base_address < 0 || (total = xx_io_total_size(self->device)) < self->base_address) return false;
    available = (uint64_t)(total - self->base_address);
    if (!xcoff_read_relative(self, available, 0U, bytes, sizeof(bytes))) return false;
    sections = xx_data_get_u16(bytes, sizeof(bytes), 2U, true);
    return xcoff_magic_is_supported(xx_data_get_u16(bytes, sizeof(bytes), 0U, true)) && sections && sections <= XX_XCOFF_MAX_SECTIONS &&
           xx_data_get_u16(bytes, sizeof(bytes), 16U, true) == XX_XCOFF_OPTIONAL_HEADER_SIZE;
}

bool xx_xcoff_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_xcoff *reader = (xx_xcoff *)self;
    uint8_t fixed[XX_XCOFF_FILE_HEADER_SIZE + XX_XCOFF_OPTIONAL_HEADER_SIZE];
    xx_xcoff_section *sections = NULL;
    xx_xcoff_loader_symbol *loader_symbols = NULL;
    xx_xcoff_import *imports = NULL;
    uint8_t section_bytes[XX_XCOFF_SECTION_HEADER_SIZE];
    uint8_t loader_bytes[XX_XCOFF_LOADER_HEADER_SIZE];
    int64_t total;
    uint64_t available;
    uint64_t section_table_size;
    uint64_t section_table_offset = sizeof(fixed);
    uint64_t loader_offset = 0U;
    uint32_t index;
    uint32_t import_count = 0U;
    bool result = false;

    if (!reader || !self->device || self->base_address < 0 || xcoff_stopped(pd)) return false;
    if (self->base_info_handled) return self->is_valid;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    available = (uint64_t)(total - self->base_address);
    if (!xcoff_read_relative(self, available, 0U, fixed, sizeof(fixed))) return false;

    reader->header.magic = xx_data_get_u16(fixed, sizeof(fixed), 0U, true);
    reader->header.section_count = xx_data_get_u16(fixed, sizeof(fixed), 2U, true);
    reader->header.timestamp = xx_data_get_u32(fixed, sizeof(fixed), 4U, true);
    reader->header.symbol_table_offset = xx_data_get_u32(fixed, sizeof(fixed), 8U, true);
    reader->header.symbol_count = xx_data_get_u32(fixed, sizeof(fixed), 12U, true);
    reader->header.optional_header_size = xx_data_get_u16(fixed, sizeof(fixed), 16U, true);
    reader->header.flags = xx_data_get_u16(fixed, sizeof(fixed), 18U, true);
    if (!xcoff_magic_is_supported(reader->header.magic) || !reader->header.section_count || reader->header.section_count > XX_XCOFF_MAX_SECTIONS ||
        reader->header.optional_header_size != XX_XCOFF_OPTIONAL_HEADER_SIZE)
        return false;
    reader->header.optional_magic = xx_data_get_u16(fixed, sizeof(fixed), 20U, true);
    reader->header.version_stamp = xx_data_get_u16(fixed, sizeof(fixed), 22U, true);
    reader->header.text_size = xx_data_get_u32(fixed, sizeof(fixed), 24U, true);
    reader->header.data_size = xx_data_get_u32(fixed, sizeof(fixed), 28U, true);
    reader->header.bss_size = xx_data_get_u32(fixed, sizeof(fixed), 32U, true);
    reader->header.entry_descriptor = xx_data_get_u32(fixed, sizeof(fixed), 36U, true);
    reader->header.text_start = xx_data_get_u32(fixed, sizeof(fixed), 40U, true);
    reader->header.data_start = xx_data_get_u32(fixed, sizeof(fixed), 44U, true);
    reader->header.toc = xx_data_get_u32(fixed, sizeof(fixed), 48U, true);
    reader->header.entry_section = xx_data_get_u16(fixed, sizeof(fixed), 52U, true);
    reader->header.text_section = xx_data_get_u16(fixed, sizeof(fixed), 54U, true);
    reader->header.data_section = xx_data_get_u16(fixed, sizeof(fixed), 56U, true);
    reader->header.toc_section = xx_data_get_u16(fixed, sizeof(fixed), 58U, true);
    reader->header.loader_section = xx_data_get_u16(fixed, sizeof(fixed), 60U, true);
    reader->header.bss_section = xx_data_get_u16(fixed, sizeof(fixed), 62U, true);

    section_table_size = (uint64_t)reader->header.section_count * XX_XCOFF_SECTION_HEADER_SIZE;
    if (section_table_offset > available || section_table_size > available - section_table_offset) return false;
    sections = (xx_xcoff_section *)xx_mem_alloc((size_t)reader->header.section_count * sizeof(*sections));
    if (!sections) return false;
    xx_mem_zero(sections, (size_t)reader->header.section_count * sizeof(*sections));
    for (index = 0U; index < reader->header.section_count; ++index) {
        xx_xcoff_section *section = &sections[index];
        uint64_t offset = section_table_offset + (uint64_t)index * XX_XCOFF_SECTION_HEADER_SIZE;
        if (xcoff_stopped(pd) || !xcoff_read_relative(self, available, offset, section_bytes, sizeof(section_bytes))) goto cleanup;
        xcoff_copy_fixed_name(section->name, sizeof(section->name), section_bytes, 8U);
        section->physical_address = xx_data_get_u32(section_bytes, sizeof(section_bytes), 8U, true);
        section->virtual_address = xx_data_get_u32(section_bytes, sizeof(section_bytes), 12U, true);
        section->size = xx_data_get_u32(section_bytes, sizeof(section_bytes), 16U, true);
        section->file_offset = xx_data_get_u32(section_bytes, sizeof(section_bytes), 20U, true);
        section->relocation_offset = xx_data_get_u32(section_bytes, sizeof(section_bytes), 24U, true);
        section->line_number_offset = xx_data_get_u32(section_bytes, sizeof(section_bytes), 28U, true);
        section->relocation_count = xx_data_get_u16(section_bytes, sizeof(section_bytes), 32U, true);
        section->line_number_count = xx_data_get_u16(section_bytes, sizeof(section_bytes), 34U, true);
        section->flags = xx_data_get_u32(section_bytes, sizeof(section_bytes), 36U, true);
        section->type = (xx_xcoff_section_type)(section->flags & UINT32_C(0xffff));
        if (section->type == XX_XCOFF_SECTION_TEXT) section->permissions = XX_XCOFF_PERMISSION_READ | XX_XCOFF_PERMISSION_EXECUTE;
        else if (section->type == XX_XCOFF_SECTION_DATA || section->type == XX_XCOFF_SECTION_BSS || section->type == XX_XCOFF_SECTION_THREAD_DATA ||
                 section->type == XX_XCOFF_SECTION_THREAD_BSS)
            section->permissions = XX_XCOFF_PERMISSION_READ | XX_XCOFF_PERMISSION_WRITE;
        if (section->type != XX_XCOFF_SECTION_BSS && section->type != XX_XCOFF_SECTION_THREAD_BSS &&
            ((uint64_t)section->file_offset > available || (uint64_t)section->size > available - section->file_offset))
            goto cleanup;
    }

    if (!reader->header.loader_section || reader->header.loader_section > reader->header.section_count) goto cleanup;
    loader_offset = sections[reader->header.loader_section - 1U].file_offset;
    if (sections[reader->header.loader_section - 1U].type != XX_XCOFF_SECTION_LOADER || sections[reader->header.loader_section - 1U].size < XX_XCOFF_LOADER_HEADER_SIZE ||
        !xcoff_read_relative(self, available, loader_offset, loader_bytes, sizeof(loader_bytes)))
        goto cleanup;
    reader->loader_header.version = xx_data_get_u32(loader_bytes, sizeof(loader_bytes), 0U, true);
    reader->loader_header.symbol_count = xx_data_get_u32(loader_bytes, sizeof(loader_bytes), 4U, true);
    reader->loader_header.relocation_count = xx_data_get_u32(loader_bytes, sizeof(loader_bytes), 8U, true);
    reader->loader_header.import_string_length = xx_data_get_u32(loader_bytes, sizeof(loader_bytes), 12U, true);
    reader->loader_header.import_file_count = xx_data_get_u32(loader_bytes, sizeof(loader_bytes), 16U, true);
    reader->loader_header.import_file_offset = xx_data_get_u32(loader_bytes, sizeof(loader_bytes), 20U, true);
    reader->loader_header.string_table_length = xx_data_get_u32(loader_bytes, sizeof(loader_bytes), 24U, true);
    reader->loader_header.string_table_offset = xx_data_get_u32(loader_bytes, sizeof(loader_bytes), 28U, true);
    if (reader->loader_header.version != 1U || reader->loader_header.symbol_count > XX_XCOFF_MAX_LOADER_SYMBOLS ||
        (uint64_t)reader->loader_header.symbol_count * XX_XCOFF_LOADER_SYMBOL_SIZE > sections[reader->header.loader_section - 1U].size - XX_XCOFF_LOADER_HEADER_SIZE ||
        reader->loader_header.import_file_offset > sections[reader->header.loader_section - 1U].size ||
        reader->loader_header.import_string_length > sections[reader->header.loader_section - 1U].size - reader->loader_header.import_file_offset ||
        reader->loader_header.string_table_offset > sections[reader->header.loader_section - 1U].size ||
        reader->loader_header.string_table_length > sections[reader->header.loader_section - 1U].size - reader->loader_header.string_table_offset)
        goto cleanup;

    if (reader->loader_header.symbol_count) {
        loader_symbols = (xx_xcoff_loader_symbol *)xx_mem_alloc((size_t)reader->loader_header.symbol_count * sizeof(*loader_symbols));
        imports = (xx_xcoff_import *)xx_mem_alloc((size_t)reader->loader_header.symbol_count * sizeof(*imports));
        if (!loader_symbols || !imports) goto cleanup;
        xx_mem_zero(loader_symbols, (size_t)reader->loader_header.symbol_count * sizeof(*loader_symbols));
        xx_mem_zero(imports, (size_t)reader->loader_header.symbol_count * sizeof(*imports));
    }
    for (index = 0U; index < reader->loader_header.symbol_count; ++index) {
        xx_xcoff_loader_symbol *symbol = &loader_symbols[index];
        if (xcoff_stopped(pd) ||
            !xcoff_parse_loader_symbol(self, available, loader_offset, sections[reader->header.loader_section - 1U].size, &reader->loader_header, index, symbol))
            goto cleanup;
        if ((symbol->type_flags & UINT8_C(0xf8)) == UINT8_C(0x40) && symbol->has_inline_name && symbol->name[0]) {
            xx_xcoff_import *import_record = &imports[import_count++];
            xcoff_copy_fixed_name(import_record->name, sizeof(import_record->name), (const uint8_t *)symbol->name, 8U);
            import_record->loader_symbol_index = index;
            import_record->import_file_index = symbol->import_file_index;
            import_record->is_function = symbol->storage_class == UINT8_C(0x0a);
        }
    }

    reader->sections = sections;
    reader->loader_symbols = loader_symbols;
    reader->imports = imports;
    reader->section_count = reader->header.section_count;
    reader->loader_symbol_count = reader->loader_header.symbol_count;
    reader->import_count = import_count;
    reader->has_loader = true;
    sections = NULL;
    loader_symbols = NULL;
    imports = NULL;
    if (!xcoff_resolve_entry(reader, available)) goto committed_cleanup;
    self->format_size = (int64_t)available;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->is_valid = true;
    self->base_info_handled = true;
    result = true;
    goto cleanup;

committed_cleanup:
    xx_mem_free(reader->imports);
    xx_mem_free(reader->loader_symbols);
    xx_mem_free(reader->sections);
    reader->imports = NULL;
    reader->loader_symbols = NULL;
    reader->sections = NULL;
    reader->section_count = 0U;
    reader->loader_symbol_count = 0U;
    reader->import_count = 0U;
cleanup:
    xx_mem_free(imports);
    xx_mem_free(loader_symbols);
    xx_mem_free(sections);
    return result && !xcoff_stopped(pd);
}

int64_t xx_xcoff_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_xcoff_handle_base_info(self, pd)) ? self->format_size : -1;
}

const xx_xcoff_header *xx_xcoff_get_header(xx_xcoff *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_xcoff_handle_base_info(&reader->format, pd)) ? &reader->header : NULL;
}

uint32_t xx_xcoff_get_number_of_sections(xx_xcoff *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_xcoff_handle_base_info(&reader->format, pd)) ? reader->section_count : 0U;
}

const xx_xcoff_section *xx_xcoff_get_section(xx_xcoff *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_xcoff_get_number_of_sections(reader, pd) ? &reader->sections[index] : NULL;
}

bool xx_xcoff_read_section(xx_xcoff *reader, uint32_t index, uint32_t offset, void *buffer, size_t size, xx_pd_struct *pd)
{
    const xx_xcoff_section *section = xx_xcoff_get_section(reader, index, pd);
    if (!section || section->type == XX_XCOFF_SECTION_BSS || section->type == XX_XCOFF_SECTION_THREAD_BSS || offset > section->size || size > section->size - offset ||
        xcoff_stopped(pd))
        return false;
    if (!size) return true;
    return buffer && xx_io_read_at(reader->format.device, reader->format.base_address + (int64_t)section->file_offset + (int64_t)offset, buffer, size);
}

const xx_xcoff_loader_header *xx_xcoff_get_loader_header(xx_xcoff *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_xcoff_handle_base_info(&reader->format, pd)) && reader->has_loader ? &reader->loader_header : NULL;
}

uint32_t xx_xcoff_get_number_of_loader_symbols(xx_xcoff *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_xcoff_handle_base_info(&reader->format, pd)) ? reader->loader_symbol_count : 0U;
}

const xx_xcoff_loader_symbol *xx_xcoff_get_loader_symbol(xx_xcoff *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_xcoff_get_number_of_loader_symbols(reader, pd) ? &reader->loader_symbols[index] : NULL;
}

uint32_t xx_xcoff_get_number_of_imports(xx_xcoff *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_xcoff_handle_base_info(&reader->format, pd)) ? reader->import_count : 0U;
}

const xx_xcoff_import *xx_xcoff_get_import(xx_xcoff *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_xcoff_get_number_of_imports(reader, pd) ? &reader->imports[index] : NULL;
}

bool xx_xcoff_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_xcoff *reader = (xx_xcoff *)self;
    uint32_t index;
    if (!reader || !output || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_SECTIONS) ||
        (!self->base_info_handled && !xx_xcoff_handle_base_info(self, pd)))
        return false;
    output->binary_offset = self->base_address;
    output->binary_size = self->format_size;
    output->module_address = 0U;
    output->entry_point_address = reader->entry_point;
    output->code_base = reader->header.text_start;
    output->start_load_offset = self->base_address;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = XX_MEMORY_MAP_MODE_SECTIONS;
    output->is_image = true;
    if (!xx_memory_map_reserve(output, reader->section_count)) return false;
    for (index = 0U; index < reader->section_count; ++index) {
        const xx_xcoff_section *section = &reader->sections[index];
        char name[32];
        int written = snprintf(name, sizeof(name), "%s-%u", section->name, index);
        if (written < 0 || (size_t)written >= sizeof(name) || xcoff_stopped(pd) ||
            !xx_memory_map_add_part(output, self->base_address + section->file_offset, section->size, section->virtual_address, section->size, XX_FILE_PART_SECTION,
                                    (int32_t)index, name, false))
            return false;
    }
    return !xcoff_stopped(pd) && xx_memory_map_finalize(output);
}
