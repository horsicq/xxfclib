/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/hp_som/xx_hp_som.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <limits.h>

#define SOM_MAX_STRING_TABLE (UINT32_C(1) << 24)
#define SOM_MAX_SPACES UINT32_C(1024)
#define SOM_MAX_SECTIONS UINT32_C(4096)
#define SOM_MAX_SYMBOLS UINT32_C(65536)
#define SOM_MAX_LIBRARIES UINT32_C(1024)
#define SOM_MAX_IMPORTS UINT32_C(65536)
#define SOM_MAX_INTERPRETER_SCAN (UINT32_C(1) << 26)

static bool som_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool som_magic_is_valid(uint16_t magic)
{
    switch (magic) {
        case XX_HP_SOM_MAGIC_RELOC:
        case XX_HP_SOM_MAGIC_EXEC:
        case XX_HP_SOM_MAGIC_SHARE:
        case XX_HP_SOM_MAGIC_SHMEM:
        case XX_HP_SOM_MAGIC_DEMAND:
        case XX_HP_SOM_MAGIC_DL:
        case XX_HP_SOM_MAGIC_SHL: return true;
        default: return false;
    }
}

static bool som_span_is_valid(uint32_t location, uint32_t count, uint32_t stride, uint32_t limit)
{
    return location <= limit && (count == 0U || (stride != 0U && count <= (limit - location) / stride));
}

static bool som_range_is_valid(uint32_t location, uint32_t size, uint32_t limit)
{
    return location <= limit && size <= limit - location;
}

static bool som_read_relative(const xx_hp_som *reader, uint32_t offset, void *data, size_t size)
{
    int64_t absolute;
    if (!reader || !data || reader->format.base_address < 0 || size > UINT32_MAX || !som_range_is_valid(offset, (uint32_t)size, reader->header.som_length) ||
        (uint64_t)offset > (uint64_t)(INT64_MAX - reader->format.base_address))
        return false;
    absolute = reader->format.base_address + (int64_t)offset;
    return xx_io_read_at(reader->format.device, absolute, data, size);
}

static bool som_duplicate_string(const char *table, uint32_t table_size, uint32_t offset, char **result)
{
    uint32_t length = 0U;
    char *copy;
    if (!result) return false;
    *result = NULL;
    if (!table || offset >= table_size) return false;
    while (length < table_size - offset && table[offset + length] != '\0') ++length;
    if (length == table_size - offset) return false;
    copy = (char *)xx_mem_alloc((size_t)length + 1U);
    if (!copy) return false;
    if (length != 0U) xx_mem_copy(copy, table + offset, length);
    copy[length] = '\0';
    *result = copy;
    return true;
}

static bool som_read_string_table(const xx_hp_som *reader, uint32_t location, uint32_t size, char **table)
{
    char *value;
    if (!table) return false;
    *table = NULL;
    if (size == 0U) return true;
    if (size > SOM_MAX_STRING_TABLE || !som_range_is_valid(location, size, reader->header.som_length)) return false;
    value = (char *)xx_mem_alloc((size_t)size + 1U);
    if (!value) return false;
    if (!som_read_relative(reader, location, value, size)) {
        xx_mem_free(value);
        return false;
    }
    value[size] = '\0';
    *table = value;
    return true;
}

static void som_clear_payload(xx_hp_som *reader)
{
    uint32_t index;
    if (!reader) return;
    for (index = 0U; index < reader->space_count; ++index) xx_mem_free(reader->spaces[index].name);
    for (index = 0U; index < reader->section_count; ++index) xx_mem_free(reader->sections[index].name);
    for (index = 0U; index < reader->library_count; ++index) xx_mem_free(reader->libraries[index].name);
    for (index = 0U; index < reader->import_count; ++index) xx_mem_free(reader->imports[index].name);
    for (index = 0U; index < reader->symbol_count; ++index) {
        xx_mem_free(reader->symbols[index].name);
        xx_mem_free(reader->symbols[index].qualifier_name);
    }
    xx_mem_free(reader->spaces);
    xx_mem_free(reader->sections);
    xx_mem_free(reader->libraries);
    xx_mem_free(reader->imports);
    xx_mem_free(reader->symbols);
    xx_mem_free(reader->interpreter);
    reader->spaces = NULL;
    reader->sections = NULL;
    reader->libraries = NULL;
    reader->imports = NULL;
    reader->symbols = NULL;
    reader->interpreter = NULL;
    reader->space_count = 0U;
    reader->section_count = 0U;
    reader->library_count = 0U;
    reader->import_count = 0U;
    reader->symbol_count = 0U;
    reader->entry_address = XX_INVALID_ADDRESS;
    reader->entry_file_offset = -1;
    reader->has_dynamic_loader_header = false;
    xx_mem_zero(&reader->header, sizeof(reader->header));
    xx_mem_zero(&reader->dynamic_loader_header, sizeof(reader->dynamic_loader_header));
}

static bool som_parse_header(xx_hp_som *reader)
{
    uint8_t data[XX_HP_SOM_HEADER_SIZE];
    xx_hp_som_header *header;
    int64_t total;
    uint32_t word;
    if (!reader || !reader->format.device || reader->format.base_address < 0) return false;
    total = xx_io_total_size(reader->format.device);
    if (total < reader->format.base_address || total - reader->format.base_address < XX_HP_SOM_HEADER_SIZE ||
        !xx_io_read_at(reader->format.device, reader->format.base_address, data, sizeof(data)))
        return false;
    header = &reader->header;
    header->system_id = xx_data_get_u16(data, sizeof(data), 0U, true);
    header->magic = xx_data_get_u16(data, sizeof(data), 2U, true);
    if (!som_magic_is_valid(header->magic)) return false;
    word = xx_data_get_u32(data, sizeof(data), 4U, true);
    header->version_id = word;
    header->file_time_seconds = xx_data_get_u32(data, sizeof(data), 8U, true);
    header->file_time_nanoseconds = xx_data_get_u32(data, sizeof(data), 12U, true);
    header->entry_space = xx_data_get_u32(data, sizeof(data), 16U, true);
    header->entry_subspace = xx_data_get_u32(data, sizeof(data), 20U, true);
    header->entry_offset = xx_data_get_u32(data, sizeof(data), 24U, true);
    header->auxiliary_header_location = xx_data_get_u32(data, sizeof(data), 28U, true);
    header->auxiliary_header_size = xx_data_get_u32(data, sizeof(data), 32U, true);
    header->som_length = xx_data_get_u32(data, sizeof(data), 36U, true);
    header->presumed_dp = xx_data_get_u32(data, sizeof(data), 40U, true);
    header->space_location = xx_data_get_u32(data, sizeof(data), 44U, true);
    header->space_total = xx_data_get_u32(data, sizeof(data), 48U, true);
    header->subspace_location = xx_data_get_u32(data, sizeof(data), 52U, true);
    header->subspace_total = xx_data_get_u32(data, sizeof(data), 56U, true);
    header->loader_fixup_location = xx_data_get_u32(data, sizeof(data), 60U, true);
    header->loader_fixup_total = xx_data_get_u32(data, sizeof(data), 64U, true);
    header->space_strings_location = xx_data_get_u32(data, sizeof(data), 68U, true);
    header->space_strings_size = xx_data_get_u32(data, sizeof(data), 72U, true);
    header->initialization_array_location = xx_data_get_u32(data, sizeof(data), 76U, true);
    header->initialization_array_total = xx_data_get_u32(data, sizeof(data), 80U, true);
    header->compiler_location = xx_data_get_u32(data, sizeof(data), 84U, true);
    header->compiler_total = xx_data_get_u32(data, sizeof(data), 88U, true);
    header->symbol_location = xx_data_get_u32(data, sizeof(data), 92U, true);
    header->symbol_total = xx_data_get_u32(data, sizeof(data), 96U, true);
    header->fixup_request_location = xx_data_get_u32(data, sizeof(data), 100U, true);
    header->fixup_request_total = xx_data_get_u32(data, sizeof(data), 104U, true);
    header->symbol_strings_location = xx_data_get_u32(data, sizeof(data), 108U, true);
    header->symbol_strings_size = xx_data_get_u32(data, sizeof(data), 112U, true);
    header->unloadable_space_location = xx_data_get_u32(data, sizeof(data), 116U, true);
    header->unloadable_space_size = xx_data_get_u32(data, sizeof(data), 120U, true);
    header->checksum = xx_data_get_u32(data, sizeof(data), 124U, true);
    if (header->som_length < XX_HP_SOM_HEADER_SIZE || header->som_length > (uint64_t)(total - reader->format.base_address) || header->space_total > SOM_MAX_SPACES ||
        header->subspace_total > SOM_MAX_SECTIONS || header->symbol_total > SOM_MAX_SYMBOLS ||
        !som_range_is_valid(header->auxiliary_header_location, header->auxiliary_header_size, header->som_length) ||
        !som_span_is_valid(header->space_location, header->space_total, XX_HP_SOM_SPACE_RECORD_SIZE, header->som_length) ||
        !som_span_is_valid(header->subspace_location, header->subspace_total, XX_HP_SOM_SECTION_RECORD_SIZE, header->som_length) ||
        !som_range_is_valid(header->space_strings_location, header->space_strings_size, header->som_length) ||
        !som_span_is_valid(header->symbol_location, header->symbol_total, XX_HP_SOM_SYMBOL_RECORD_SIZE, header->som_length) ||
        !som_range_is_valid(header->symbol_strings_location, header->symbol_strings_size, header->som_length) ||
        !som_range_is_valid(header->unloadable_space_location, header->unloadable_space_size, header->som_length))
        return false;
    return true;
}

static bool som_parse_spaces(xx_hp_som *reader, const char *strings)
{
    uint32_t index;
    uint8_t data[XX_HP_SOM_SPACE_RECORD_SIZE];
    reader->space_count = reader->header.space_total;
    if (reader->space_count == 0U) return true;
    reader->spaces = (xx_hp_som_space *)xx_mem_calloc(reader->space_count, sizeof(*reader->spaces));
    if (!reader->spaces) return false;
    for (index = 0U; index < reader->space_count; ++index) {
        xx_hp_som_space *space = &reader->spaces[index];
        uint32_t control;
        if (!som_read_relative(reader, reader->header.space_location + index * XX_HP_SOM_SPACE_RECORD_SIZE, data, sizeof(data))) return false;
        space->name_offset = xx_data_get_u32(data, sizeof(data), 0U, true);
        space->flags = xx_data_get_u32(data, sizeof(data), 4U, true);
        space->space_number = xx_data_get_u32(data, sizeof(data), 8U, true);
        space->subspace_index = xx_data_get_u32(data, sizeof(data), 12U, true);
        space->subspace_quantity = xx_data_get_u32(data, sizeof(data), 16U, true);
        for (control = 0U; control < 4U; ++control) space->control[control] = xx_data_get_u32(data, sizeof(data), 20U + control * 4U, true);
        if (!som_duplicate_string(strings, reader->header.space_strings_size, space->name_offset, &space->name)) return false;
    }
    return true;
}

static bool som_parse_sections(xx_hp_som *reader, const char *strings)
{
    uint32_t index;
    uint8_t data[XX_HP_SOM_SECTION_RECORD_SIZE];
    reader->section_count = reader->header.subspace_total;
    if (reader->section_count == 0U) return true;
    reader->sections = (xx_hp_som_section *)xx_mem_calloc(reader->section_count, sizeof(*reader->sections));
    if (!reader->sections) return false;
    for (index = 0U; index < reader->section_count; ++index) {
        xx_hp_som_section *section = &reader->sections[index];
        if (!som_read_relative(reader, reader->header.subspace_location + index * XX_HP_SOM_SECTION_RECORD_SIZE, data, sizeof(data))) return false;
        section->space_index = xx_data_get_u32(data, sizeof(data), 0U, true);
        section->flags = xx_data_get_u32(data, sizeof(data), 4U, true);
        section->file_offset = xx_data_get_u32(data, sizeof(data), 8U, true);
        section->file_size = xx_data_get_u32(data, sizeof(data), 12U, true);
        section->subspace_start = xx_data_get_u32(data, sizeof(data), 16U, true);
        section->virtual_size = xx_data_get_u32(data, sizeof(data), 20U, true);
        section->alignment = xx_data_get_u32(data, sizeof(data), 24U, true);
        section->name_offset = xx_data_get_u32(data, sizeof(data), 28U, true);
        section->fixup_request_index = xx_data_get_u32(data, sizeof(data), 32U, true);
        section->fixup_request_quantity = xx_data_get_u32(data, sizeof(data), 36U, true);
        /* file_loc_init_value is an initialization pattern when the length
         * is zero, not a physical location. Keep its original bits. */
        if ((section->file_size != 0U && !som_range_is_valid(section->file_offset, section->file_size, reader->header.som_length)) ||
            !som_duplicate_string(strings, reader->header.space_strings_size, section->name_offset, &section->name))
            return false;
        section->address = XX_HP_SOM_BASE_ADDRESS + section->subspace_start;
    }
    if (reader->header.entry_subspace < reader->section_count && reader->header.entry_space < reader->space_count) {
        const xx_hp_som_section *section = &reader->sections[reader->header.entry_subspace];
        if (reader->header.entry_offset <= section->virtual_size) {
            reader->entry_address = section->address + reader->header.entry_offset;
            if (reader->header.entry_offset < section->file_size) {
                uint64_t relative = (uint64_t)section->file_offset + reader->header.entry_offset;
                if (relative <= (uint64_t)(INT64_MAX - reader->format.base_address)) reader->entry_file_offset = reader->format.base_address + (int64_t)relative;
            }
        }
    }
    return true;
}

static void som_parse_dynamic_header(xx_hp_som_dl_header *header, const uint8_t data[XX_HP_SOM_DL_HEADER_SIZE])
{
    header->header_version = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 0U, true);
    header->ltptr_value = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 4U, true);
    header->library_list_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 8U, true);
    header->library_list_count = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 12U, true);
    header->import_list_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 16U, true);
    header->import_list_count = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 20U, true);
    header->hash_table_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 24U, true);
    header->hash_table_size = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 28U, true);
    header->export_list_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 32U, true);
    header->export_list_count = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 36U, true);
    header->string_table_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 40U, true);
    header->string_table_size = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 44U, true);
    header->dynamic_relocation_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 48U, true);
    header->dynamic_relocation_count = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 52U, true);
    header->dlt_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 56U, true);
    header->plt_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 60U, true);
    header->dlt_count = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 64U, true);
    header->plt_count = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 68U, true);
    header->highwater_mark = xx_data_get_u16(data, XX_HP_SOM_DL_HEADER_SIZE, 72U, true);
    header->flags = xx_data_get_u16(data, XX_HP_SOM_DL_HEADER_SIZE, 74U, true);
    header->export_extension_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 76U, true);
    header->module_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 80U, true);
    header->module_count = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 84U, true);
    header->elaborator = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 88U, true);
    header->initializer = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 92U, true);
    header->embedded_path = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 96U, true);
    header->initializer_count = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 100U, true);
    header->thread_data_size = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 104U, true);
    header->fastbind_list_location = xx_data_get_u32(data, XX_HP_SOM_DL_HEADER_SIZE, 108U, true);
}

static bool som_parse_dynamic_loader(xx_hp_som *reader)
{
    const xx_hp_som_section *section = NULL;
    xx_hp_som_dl_header *header = &reader->dynamic_loader_header;
    char *strings = NULL;
    uint8_t data[XX_HP_SOM_DL_HEADER_SIZE];
    uint32_t index;
    uint32_t output_count;
    for (index = 0U; index < reader->section_count; ++index) {
        if (reader->sections[index].name && xx_rt_strcmp(reader->sections[index].name, "$SHLIB_INFO$") == 0) {
            section = &reader->sections[index];
            break;
        }
    }
    if (!section) return true;
    if (section->file_size < XX_HP_SOM_DL_HEADER_SIZE || !som_read_relative(reader, section->file_offset, data, sizeof(data))) return false;
    som_parse_dynamic_header(header, data);
    reader->has_dynamic_loader_header = true;
    if (header->library_list_count > SOM_MAX_LIBRARIES || header->import_list_count > SOM_MAX_IMPORTS || header->string_table_size > SOM_MAX_STRING_TABLE ||
        !som_range_is_valid(header->string_table_location, header->string_table_size, section->file_size) ||
        !som_span_is_valid(header->library_list_location, header->library_list_count, XX_HP_SOM_DL_LIBRARY_RECORD_SIZE, section->file_size) ||
        !som_span_is_valid(header->import_list_location, header->import_list_count, XX_HP_SOM_DL_IMPORT_RECORD_SIZE, section->file_size))
        return false;
    if (header->string_table_size != 0U) {
        strings = (char *)xx_mem_alloc((size_t)header->string_table_size + 1U);
        if (!strings || !som_read_relative(reader, section->file_offset + header->string_table_location, strings, header->string_table_size)) {
            xx_mem_free(strings);
            return false;
        }
        strings[header->string_table_size] = '\0';
    }
    if ((header->library_list_count > 1U || header->import_list_count > 0U) && !strings) return false;

    if (header->library_list_count > 1U) {
        reader->libraries = (xx_hp_som_library *)xx_mem_calloc(header->library_list_count - 1U, sizeof(*reader->libraries));
        if (!reader->libraries) {
            xx_mem_free(strings);
            return false;
        }
        output_count = 0U;
        for (index = 1U; index < header->library_list_count; ++index) {
            uint8_t record[XX_HP_SOM_DL_LIBRARY_RECORD_SIZE];
            xx_hp_som_library *library = &reader->libraries[output_count];
            if (!som_read_relative(reader, section->file_offset + header->library_list_location + index * XX_HP_SOM_DL_LIBRARY_RECORD_SIZE, record, sizeof(record))) {
                xx_mem_free(strings);
                return false;
            }
            library->name_offset = xx_data_get_u32(record, sizeof(record), 0U, true);
            if (!som_duplicate_string(strings, header->string_table_size, library->name_offset, &library->name)) {
                xx_mem_free(strings);
                return false;
            }
            library->reserved = (uint8_t)((record[4] >> 2U) & 0x3fU);
            library->internal_name = ((record[4] >> 1U) & 1U) != 0U;
            library->dash_l_reference = (record[4] & 1U) != 0U;
            library->bind = record[5];
            library->highwater_mark = xx_data_get_u16(record, sizeof(record), 6U, true);
            ++output_count;
            reader->library_count = output_count;
        }
    }

    if (header->import_list_count > 0U) {
        reader->imports = (xx_hp_som_import *)xx_mem_calloc(header->import_list_count, sizeof(*reader->imports));
        if (!reader->imports) {
            xx_mem_free(strings);
            return false;
        }
        output_count = 0U;
        for (index = 0U; index < header->import_list_count; ++index) {
            uint8_t record[XX_HP_SOM_DL_IMPORT_RECORD_SIZE];
            uint32_t name_offset;
            xx_hp_som_import *import_entry;
            if (!som_read_relative(reader, section->file_offset + header->import_list_location + index * XX_HP_SOM_DL_IMPORT_RECORD_SIZE, record, sizeof(record))) {
                xx_mem_free(strings);
                return false;
            }
            name_offset = xx_data_get_u32(record, sizeof(record), 0U, true);
            if (name_offset == UINT32_MAX) continue;
            import_entry = &reader->imports[output_count];
            import_entry->name_offset = name_offset;
            if (!som_duplicate_string(strings, header->string_table_size, name_offset, &import_entry->name)) {
                xx_mem_free(strings);
                return false;
            }
            import_entry->type = record[4];
            import_entry->qualifier = record[5];
            import_entry->reserved = xx_data_get_u16(record, sizeof(record), 6U, true);
            ++output_count;
            reader->import_count = output_count;
        }
    }
    xx_mem_free(strings);
    return true;
}

static bool som_parse_symbols(xx_hp_som *reader, const char *strings)
{
    uint32_t index;
    uint32_t output_count = 0U;
    uint8_t data[XX_HP_SOM_SYMBOL_RECORD_SIZE];
    if (reader->header.symbol_total == 0U) return true;
    reader->symbols = (xx_hp_som_symbol *)xx_mem_calloc(reader->header.symbol_total, sizeof(*reader->symbols));
    if (!reader->symbols) return false;
    for (index = 0U; index < reader->header.symbol_total; ++index) {
        xx_hp_som_symbol candidate;
        xx_mem_zero(&candidate, sizeof(candidate));
        if (!som_read_relative(reader, reader->header.symbol_location + index * XX_HP_SOM_SYMBOL_RECORD_SIZE, data, sizeof(data))) return false;
        candidate.flags = xx_data_get_u32(data, sizeof(data), 0U, true);
        candidate.name_offset = xx_data_get_u32(data, sizeof(data), 4U, true);
        candidate.qualifier_name_offset = xx_data_get_u32(data, sizeof(data), 8U, true);
        candidate.information = xx_data_get_u32(data, sizeof(data), 12U, true);
        candidate.value = xx_data_get_u32(data, sizeof(data), 16U, true);
        candidate.type = (uint8_t)((candidate.flags >> 24U) & 0x3fU);
        candidate.scope = (uint8_t)((candidate.flags >> 20U) & 0x0fU);
        candidate.address = XX_HP_SOM_BASE_ADDRESS + candidate.value;
        if (candidate.name_offset == UINT32_MAX || !som_duplicate_string(strings, reader->header.symbol_strings_size, candidate.name_offset, &candidate.name) ||
            candidate.name[0] == '\0') {
            xx_mem_free(candidate.name);
            continue;
        }
        if (candidate.qualifier_name_offset != UINT32_MAX && candidate.qualifier_name_offset < reader->header.symbol_strings_size)
            (void)som_duplicate_string(strings, reader->header.symbol_strings_size, candidate.qualifier_name_offset, &candidate.qualifier_name);
        reader->symbols[output_count++] = candidate;
        reader->symbol_count = output_count;
    }
    return true;
}

static bool som_find_interpreter(xx_hp_som *reader)
{
    static const char pattern[] = "dld.sl";
    uint8_t *data;
    uint32_t size = reader->header.som_length;
    uint32_t index;
    if (size == 0U || size > SOM_MAX_INTERPRETER_SCAN) return true;
    data = (uint8_t *)xx_mem_alloc(size);
    if (!data) return false;
    if (!som_read_relative(reader, 0U, data, size)) {
        xx_mem_free(data);
        return false;
    }
    for (index = 0U; index + sizeof(pattern) - 1U <= size; ++index) {
        uint32_t start;
        uint32_t end;
        uint32_t length;
        if (xx_rt_memcmp(data + index, pattern, sizeof(pattern) - 1U) != 0) continue;
        start = index;
        while (start > 0U && index - start < 4096U && data[start - 1U] != 0U) --start;
        if (data[start] != '/' || (start > 0U && data[start - 1U] != 0U)) continue;
        end = index + (uint32_t)sizeof(pattern) - 1U;
        while (end < size && end - start < 4096U && data[end] != 0U) ++end;
        if (end == size || data[end] != 0U) continue;
        length = end - start;
        reader->interpreter = (char *)xx_mem_alloc((size_t)length + 1U);
        if (!reader->interpreter) {
            xx_mem_free(data);
            return false;
        }
        xx_mem_copy(reader->interpreter, data + start, length);
        reader->interpreter[length] = '\0';
        break;
    }
    xx_mem_free(data);
    return true;
}

static bool som_parse(xx_hp_som *reader, xx_pd_struct *pd)
{
    char *space_strings = NULL;
    char *symbol_strings = NULL;
    bool result = false;
    if (!reader || som_stopped(pd) || !som_parse_header(reader)) goto done;
    if (!som_read_string_table(reader, reader->header.space_strings_location, reader->header.space_strings_size, &space_strings) ||
        !som_read_string_table(reader, reader->header.symbol_strings_location, reader->header.symbol_strings_size, &symbol_strings) || som_stopped(pd) ||
        !som_parse_spaces(reader, space_strings) || !som_parse_sections(reader, space_strings) || som_stopped(pd) || !som_parse_dynamic_loader(reader) ||
        !som_parse_symbols(reader, symbol_strings) || som_stopped(pd) || !som_find_interpreter(reader))
        goto done;
    result = true;
done:
    xx_mem_free(space_strings);
    xx_mem_free(symbol_strings);
    return result;
}

void xx_hp_som_init(xx_hp_som *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_HP_SOM;
    reader->format.check_is_valid = xx_hp_som_check_is_valid;
    reader->format.handle_base_info = xx_hp_som_handle_base_info;
    reader->format.get_format_size = xx_hp_som_get_format_size;
    reader->format.get_memory_map = xx_hp_som_get_memory_map;
    reader->format.endian = XX_ENDIAN_BIG;
    reader->format.arch = XX_ARCH_HPPA;
    reader->format.os = XX_OS_HPUX;
    reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    reader->format.is_executable = true;
    reader->entry_address = XX_INVALID_ADDRESS;
    reader->entry_file_offset = -1;
    xx_format_set_extension(&reader->format, "som");
    xx_format_set_mime_type(&reader->format, "application/x-hp-som");
}

xx_hp_som *xx_hp_som_create(xx_io_device *device, int64_t base_address)
{
    xx_hp_som *reader = (xx_hp_som *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_hp_som_init(reader, device, base_address);
    return reader;
}

void xx_hp_som_destroy(xx_hp_som *reader)
{
    if (!reader) return;
    som_clear_payload(reader);
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_hp_som_free(xx_hp_som *reader)
{
    if (!reader) return;
    xx_hp_som_destroy(reader);
    xx_mem_free(reader);
}

bool xx_hp_som_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    xx_hp_som temporary;
    bool result;
    if (!format || !format->device || som_stopped(pd)) return false;
    xx_hp_som_init(&temporary, format->device, format->base_address);
    result = som_parse(&temporary, pd);
    xx_hp_som_destroy(&temporary);
    return result;
}

bool xx_hp_som_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    xx_hp_som *reader = (xx_hp_som *)format;
    int64_t available;
    if (!reader || !format->device || som_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    som_clear_payload(reader);
    if (!som_parse(reader, pd)) {
        som_clear_payload(reader);
        return false;
    }
    available = xx_io_total_size(format->device) - format->base_address;
    format->format_size = reader->header.som_length;
    format->overlay_size = available - format->format_size;
    format->overlay_offset = format->overlay_size > 0 ? format->base_address + format->format_size : -1;
    if (reader->header.magic == XX_HP_SOM_MAGIC_RELOC) {
        format->format_type = XX_TYPE_OBJECT;
        format->is_executable = false;
    } else if (reader->header.magic == XX_HP_SOM_MAGIC_EXEC) {
        format->format_type = XX_TYPE_CONSOLE_APPLICATION;
    } else {
        /* SHARE, SHMEM, DEMAND, DL and SHL are reported as dynamic SOM
         * images. XX_TYPE_LIBRARY is the public type that carries that
         * distinction. */
        format->format_type = XX_TYPE_LIBRARY;
    }
    format->is_valid = true;
    format->base_info_handled = true;
    return !som_stopped(pd);
}

int64_t xx_hp_som_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_hp_som_handle_base_info(format, pd)) ? format->format_size : -1;
}

bool xx_hp_som_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_hp_som *reader = (xx_hp_som *)format;
    uint32_t index;
    if (!reader || !output || som_stopped(pd) || (mode != XX_MEMORY_MAP_MODE_UNKNOWN && mode != XX_MEMORY_MAP_MODE_SECTIONS && mode != XX_MEMORY_MAP_MODE_MAPS) ||
        (!format->base_info_handled && !xx_hp_som_handle_base_info(format, pd)))
        return false;
    output->binary_offset = format->base_address;
    output->binary_size = format->format_size;
    output->module_address = XX_HP_SOM_BASE_ADDRESS;
    output->entry_point_address = reader->entry_address;
    output->code_base = reader->entry_file_offset >= format->base_address ? reader->entry_file_offset - format->base_address : -1;
    output->start_load_offset = format->base_address;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = XX_MEMORY_MAP_MODE_SECTIONS;
    output->is_image = true;
    if (!xx_memory_map_add_part(output, format->base_address, XX_HP_SOM_HEADER_SIZE, XX_HP_SOM_BASE_ADDRESS, XX_HP_SOM_HEADER_SIZE, XX_FILE_PART_HEADER, 0, "SOM header",
                                false))
        return false;
    for (index = 0U; index < reader->section_count; ++index) {
        const xx_hp_som_section *section = &reader->sections[index];
        int64_t offset;
        if (section->file_size == 0U && section->virtual_size == 0U) continue;
        offset = section->file_size > 0U ? format->base_address + section->file_offset : -1;
        if (!xx_memory_map_add_part(output, offset, section->file_size, section->address, section->virtual_size, XX_FILE_PART_SECTION,
                                    index <= INT32_MAX ? (int32_t)index : -1, section->name ? section->name : "SOM subspace", false))
            return false;
    }
    return !som_stopped(pd) && xx_memory_map_finalize(output);
}

static bool som_ensure_handled(xx_hp_som *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_hp_som_handle_base_info(&reader->format, pd));
}

const xx_hp_som_header *xx_hp_som_get_header(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) ? &reader->header : NULL;
}

const xx_hp_som_dl_header *xx_hp_som_get_dynamic_loader_header(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) && reader->has_dynamic_loader_header ? &reader->dynamic_loader_header : NULL;
}

uint32_t xx_hp_som_get_space_count(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) ? reader->space_count : 0U;
}

const xx_hp_som_space *xx_hp_som_get_space(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) && index < reader->space_count ? &reader->spaces[index] : NULL;
}

uint32_t xx_hp_som_get_section_count(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) ? reader->section_count : 0U;
}

const xx_hp_som_section *xx_hp_som_get_section(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) && index < reader->section_count ? &reader->sections[index] : NULL;
}

uint32_t xx_hp_som_get_library_count(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) ? reader->library_count : 0U;
}

const xx_hp_som_library *xx_hp_som_get_library(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) && index < reader->library_count ? &reader->libraries[index] : NULL;
}

uint32_t xx_hp_som_get_import_count(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) ? reader->import_count : 0U;
}

const xx_hp_som_import *xx_hp_som_get_import(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) && index < reader->import_count ? &reader->imports[index] : NULL;
}

uint32_t xx_hp_som_get_symbol_count(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) ? reader->symbol_count : 0U;
}

const xx_hp_som_symbol *xx_hp_som_get_symbol(xx_hp_som *reader, uint32_t index, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) && index < reader->symbol_count ? &reader->symbols[index] : NULL;
}

const char *xx_hp_som_get_interpreter(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) ? reader->interpreter : NULL;
}

uint64_t xx_hp_som_get_entry_address(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) ? reader->entry_address : XX_INVALID_ADDRESS;
}

int64_t xx_hp_som_get_entry_file_offset(xx_hp_som *reader, xx_pd_struct *pd)
{
    return som_ensure_handled(reader, pd) ? reader->entry_file_offset : -1;
}
