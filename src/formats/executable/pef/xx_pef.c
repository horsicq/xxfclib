/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Bounded reader for the PEF container and loader import/relocation tables.
 * Relocation bytecode is range checked but never interpreted or executed.
 */

#include "xxfclib/formats/executable/pef/xx_pef.h"
#include "../../common/xx_memory_blob.h"
#include "xx_pef_stream_internal.h"

#define PEF_HEADER_SIZE 40U
#define PEF_SECTION_HEADER_SIZE 28U
#define PEF_LOADER_HEADER_SIZE 56U
#define PEF_LOADER_LIBRARY_SIZE 24U
#define PEF_RELOCATION_HEADER_SIZE 12U
#define PEF_SECTION_LIMIT 4096U
#define PEF_LIBRARY_LIMIT 4096U
#define PEF_IMPORT_LIMIT 1048576U

typedef struct pef_loader_info {
    uint32_t offset;
    uint32_t size;
    uint32_t strings_offset;
    uint32_t library_count;
    uint32_t import_count;
    int32_t main_section;
    uint32_t main_offset;
    int32_t init_section;
    uint32_t init_offset;
    int32_t term_section;
    uint32_t term_offset;
} pef_loader_info;

static bool pef_entry_is_bounded(int32_t section, uint32_t offset, uint16_t section_count, const uint64_t *section_sizes)
{
    if (section == -1) return offset == 0U;
    return section >= 0 && (uint32_t)section < section_count && offset < section_sizes[(uint32_t)section];
}

static bool pef_loader_is_bounded(memory_blob *blob, uint64_t at, uint64_t size, uint16_t section_count, const uint64_t *section_sizes, pef_loader_info *info)
{
    const uint8_t *loader;
    uint64_t libraries, imported_symbols, relocations, relocation_offset;
    uint64_t strings_offset, export_offset, symbol_table, headers_end, expected_import, i;
    pef_loader_info parsed;

    if (!info || size < PEF_LOADER_HEADER_SIZE || size > UINT32_MAX || !blob_span(blob, at, size)) return false;
    loader = blob->p + (size_t)at;
    xx_mem_zero(&parsed, sizeof(parsed));
    parsed.offset = (uint32_t)at;
    parsed.size = (uint32_t)size;
    parsed.main_section = xx_data_get_i32(loader, (size_t)size, 0, true);
    parsed.main_offset = xx_data_get_u32(loader, (size_t)size, 4, true);
    parsed.init_section = xx_data_get_i32(loader, (size_t)size, 8, true);
    parsed.init_offset = xx_data_get_u32(loader, (size_t)size, 12, true);
    parsed.term_section = xx_data_get_i32(loader, (size_t)size, 16, true);
    parsed.term_offset = xx_data_get_u32(loader, (size_t)size, 20, true);
    libraries = xx_data_get_u32(loader, (size_t)size, 24, true);
    imported_symbols = xx_data_get_u32(loader, (size_t)size, 28, true);
    relocations = xx_data_get_u32(loader, (size_t)size, 32, true);
    relocation_offset = xx_data_get_u32(loader, (size_t)size, 36, true);
    strings_offset = xx_data_get_u32(loader, (size_t)size, 40, true);
    export_offset = xx_data_get_u32(loader, (size_t)size, 44, true);

    if (libraries > PEF_LIBRARY_LIMIT || imported_symbols > PEF_IMPORT_LIMIT || relocations > section_count || relocations > PEF_SECTION_LIMIT) return false;
    if (!pef_entry_is_bounded(parsed.main_section, parsed.main_offset, section_count, section_sizes) ||
        !pef_entry_is_bounded(parsed.init_section, parsed.init_offset, section_count, section_sizes) ||
        !pef_entry_is_bounded(parsed.term_section, parsed.term_offset, section_count, section_sizes))
        return false;

    symbol_table = PEF_LOADER_HEADER_SIZE;
    if (libraries > (UINT64_MAX - symbol_table) / PEF_LOADER_LIBRARY_SIZE) return false;
    symbol_table += libraries * PEF_LOADER_LIBRARY_SIZE;
    headers_end = symbol_table;
    if (imported_symbols > (UINT64_MAX - headers_end) / 4U) return false;
    headers_end += imported_symbols * 4U;
    if (relocations > (UINT64_MAX - headers_end) / PEF_RELOCATION_HEADER_SIZE) return false;
    headers_end += relocations * PEF_RELOCATION_HEADER_SIZE;
    if (headers_end > size || relocation_offset > size || strings_offset > size || export_offset > size) return false;
    if ((libraries || imported_symbols) && strings_offset < headers_end) return false;
    if (relocations && relocation_offset < headers_end) return false;

    expected_import = 0U;
    for (i = 0U; i < libraries; ++i) {
        const uint8_t *library = loader + PEF_LOADER_HEADER_SIZE + (size_t)(i * PEF_LOADER_LIBRARY_SIZE);
        uint32_t name_offset = xx_data_get_u32(library, PEF_LOADER_LIBRARY_SIZE, 0, true);
        uint32_t count = xx_data_get_u32(library, PEF_LOADER_LIBRARY_SIZE, 12, true);
        uint32_t first = xx_data_get_u32(library, PEF_LOADER_LIBRARY_SIZE, 16, true);
        size_t name_length;
        if (library[21] || library[22] || library[23] || first != expected_import || count > imported_symbols - first ||
            !pef_string_span(loader, (uint32_t)size, (uint32_t)strings_offset, name_offset, &name_length))
            return false;
        expected_import += count;
    }
    if (expected_import != imported_symbols) return false;

    for (i = 0U; i < imported_symbols; ++i) {
        uint32_t word = xx_data_get_u32(loader + (size_t)(symbol_table + i * 4U), 4, 0, true);
        uint32_t name_offset = word & UINT32_C(0x00ffffff);
        size_t name_length;
        if (strings_offset > size || name_offset >= size - strings_offset ||
            !pef_string_span(loader, (uint32_t)size, (uint32_t)strings_offset, name_offset, &name_length))
            return false;
    }

    for (i = 0U; i < relocations; ++i) {
        uint64_t header = symbol_table + imported_symbols * 4U + i * PEF_RELOCATION_HEADER_SIZE;
        uint64_t instruction_count = xx_data_get_u32(loader + (size_t)(header + 4U), 4, 0, true);
        uint64_t first = xx_data_get_u32(loader + (size_t)(header + 8U), 4, 0, true);
        uint64_t available;

        /* The target section and relocation bytecode targets are not
         * interpreted here. Invalid targets are ignored by consumers, as in
         * the upstream regression, while the bytecode span remains bounded. */
        if (first > size - relocation_offset) return false;
        available = size - relocation_offset - first;
        if (instruction_count > available / 2U) return false;
    }

    parsed.strings_offset = (uint32_t)strings_offset;
    parsed.library_count = (uint32_t)libraries;
    parsed.import_count = (uint32_t)imported_symbols;
    *info = parsed;
    return true;
}

static bool pef_outer_header_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    uint8_t header[PEF_HEADER_SIZE];
    uint8_t *sections = NULL;
    uint64_t table_size;
    uint32_t architecture;
    uint16_t section_count, instantiated_count;
    int64_t saved;
    size_t i;
    bool result = false;

    if (!format || !format->device || format->base_address < 0 || xx_pd_is_stopped(pd)) return false;
    saved = xx_io_tell(format->device);
    if (saved < 0 || !pm_read(format, 0, header, sizeof(header)) || xx_rt_memcmp(header, "Joy!peff", 8)) goto done;
    architecture = xx_data_get_u32(header + 8, 4, 0, true);
    section_count = xx_data_get_u16(header + 32, 2, 0, true);
    instantiated_count = xx_data_get_u16(header + 34, 2, 0, true);
    if ((architecture != UINT32_C(0x70777063) && architecture != UINT32_C(0x6d36386b)) || xx_data_get_u32(header + 12, 4, 0, true) > 1U || !section_count ||
        section_count > PEF_SECTION_LIMIT || instantiated_count > section_count || xx_data_get_u32(header + 36, 4, 0, true))
        goto done;
    table_size = (uint64_t)section_count * PEF_SECTION_HEADER_SIZE;
    if (pm_available(format) < (int64_t)(PEF_HEADER_SIZE + table_size)) goto done;
    sections = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!sections || !pm_read(format, PEF_HEADER_SIZE, sections, (size_t)table_size)) goto done;
    for (i = 0U; i < section_count; ++i) {
        const uint8_t *section = sections + i * PEF_SECTION_HEADER_SIZE;
        if (section[24] > 8U || section[26] > 31U || section[27]) goto done;
    }
    result = true;
done:
    xx_mem_free(sections);
    if (saved >= 0 && xx_io_seek64(format->device, saved, SEEK_SET) != 0) result = false;
    return result;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    memory_blob blob;
    xx_pef *reader = (xx_pef *)format;
    pef_loader_info loader_info;
    uint64_t *section_sizes = NULL;
    uint64_t table_size, table_end, format_end, loader_at = 0U, loader_size = 0U, i;
    uint32_t architecture;
    uint16_t section_count, instantiated_count;
    unsigned int loader_count = 0U;
    bool ok = false;

    xx_mem_zero(&loader_info, sizeof(loader_info));
    if (!blob_load(format, &blob, pd)) return false;
    BLOB_NEED(blob_span(&blob, 0, PEF_HEADER_SIZE));
    BLOB_NEED(!xx_rt_memcmp(blob.p, "Joy!peff", 8));
    architecture = xx_data_get_u32(blob.p + 8, 4, 0, true);
    BLOB_NEED(architecture == UINT32_C(0x70777063) || architecture == UINT32_C(0x6d36386b)); /* pwpc / m68k */
    BLOB_NEED(xx_data_get_u32(blob.p + 12, 4, 0, true) <= 1U);
    section_count = xx_data_get_u16(blob.p + 32, 2, 0, true);
    instantiated_count = xx_data_get_u16(blob.p + 34, 2, 0, true);
    BLOB_NEED(section_count && section_count <= PEF_SECTION_LIMIT && instantiated_count <= section_count && !xx_data_get_u32(blob.p + 36, 4, 0, true));
    table_size = (uint64_t)section_count * PEF_SECTION_HEADER_SIZE;
    table_end = PEF_HEADER_SIZE + table_size;
    format_end = table_end;
    BLOB_NEED(blob_span(&blob, PEF_HEADER_SIZE, table_size));
    BLOB_NEED(blob_add(format, stream, &blob, "container-header", 0, table_end));
    section_sizes = (uint64_t *)xx_mem_calloc(section_count, sizeof(*section_sizes));
    BLOB_NEED(section_sizes != NULL);

    for (i = 0U; i < section_count; ++i) {
        const uint8_t *section = blob.p + PEF_HEADER_SIZE + (size_t)(i * PEF_SECTION_HEADER_SIZE);
        uint64_t total_size = xx_data_get_u32(section + 8, 4, 0, true);
        uint64_t unpacked_size = xx_data_get_u32(section + 12, 4, 0, true);
        uint64_t packed_size = xx_data_get_u32(section + 16, 4, 0, true);
        uint64_t offset = xx_data_get_u32(section + 20, 4, 0, true);
        uint8_t kind = section[24], alignment = section[26];
        char name[48];

        BLOB_NEED(kind <= 8U && alignment <= 31U && section[27] == 0U);
        section_sizes[i] = total_size;
        if (kind == 4U) {
            BLOB_NEED(++loader_count == 1U && packed_size >= PEF_LOADER_HEADER_SIZE);
            loader_at = offset;
            loader_size = packed_size;
        } else {
            BLOB_NEED(total_size >= unpacked_size);
            if (kind != 2U) BLOB_NEED(packed_size <= unpacked_size);
        }
        if (!packed_size) continue;
        BLOB_NEED(offset >= table_end && blob_span(&blob, offset, packed_size));
        if (offset + packed_size > format_end) format_end = offset + packed_size;
        xx_rt_snprintf(name, sizeof(name), "section-%04u-kind-%u", (unsigned)i, (unsigned)kind);
        BLOB_NEED(blob_add(format, stream, &blob, name, offset, packed_size));
    }

    BLOB_NEED(loader_count == 1U && pef_loader_is_bounded(&blob, loader_at, loader_size, section_count, section_sizes, &loader_info));
    reader->architecture = architecture;
    reader->section_count = section_count;
    reader->instantiated_section_count = instantiated_count;
    reader->loader_offset = loader_info.offset;
    reader->loader_size = loader_info.size;
    reader->loader_strings_offset = loader_info.strings_offset;
    reader->imported_library_count = loader_info.library_count;
    reader->imported_symbol_count = loader_info.import_count;
    reader->main_section = loader_info.main_section;
    reader->main_offset = loader_info.main_offset;
    reader->init_section = loader_info.init_section;
    reader->init_offset = loader_info.init_offset;
    reader->term_section = loader_info.term_section;
    reader->term_offset = loader_info.term_offset;
    stream->size = (int64_t)format_end;
    ok = true;
done:
    xx_mem_free(section_sizes);
    xx_mem_free(blob.p);
    return ok;
}

static int64_t pef_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_pef_handle_base_info(self, pd)) ? self->format_size : -1;
}

void xx_pef_init(xx_pef *reader, xx_io_device *device, int64_t base_address)
{
    if (reader) {
        xx_mem_zero(reader, sizeof(*reader));
        pm_init(&reader->format, device, base_address, XX_FILE_TYPE_PEF, "pef");
        reader->format.check_is_valid = xx_pef_check_is_valid;
        reader->format.handle_base_info = xx_pef_handle_base_info;
        reader->format.get_format_size = pef_get_format_size;
        reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
        reader->format.is_archive = false;
        reader->format.is_executable = true;
        reader->format.endian = XX_ENDIAN_BIG;
        reader->format.os = XX_OS_MACOS;
        reader->main_section = reader->init_section = reader->term_section = -1;
    }
}

xx_pef *xx_pef_create(xx_io_device *device, int64_t base_address)
{
    xx_pef *reader = (xx_pef *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pef_init(reader, device, base_address);
    return reader;
}

void xx_pef_destroy(xx_pef *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pef_free(xx_pef *reader)
{
    if (reader) {
        xx_pef_destroy(reader);
        xx_mem_free(reader);
    }
}

bool xx_pef_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    return pef_outer_header_is_valid(self, pd);
}

bool xx_pef_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_pef *reader = (xx_pef *)self;
    if (!self) return false;
    if (self->base_info_handled) return self->is_valid;
    if (!pm_handle(self, pd)) return false;
    self->arch = reader->architecture == UINT32_C(0x70777063) ? XX_ARCH_PPC : XX_ARCH_M68K;
    self->endian = XX_ENDIAN_BIG;
    self->os = XX_OS_MACOS;
    self->format_type = XX_TYPE_CONSOLE_APPLICATION;
    self->is_executable = true;
    self->is_archive = false;
    self->number_of_imports = reader->imported_symbol_count;
    return true;
}

static bool pef_get_entry_tuple(const xx_pef *reader, int32_t section, uint32_t offset, uint16_t *section_index, uint32_t *section_offset)
{
    if (!reader || !reader->format.base_info_handled || !reader->format.is_valid || section < 0 || (uint32_t)section >= reader->section_count) return false;
    if (section_index) *section_index = (uint16_t)section;
    if (section_offset) *section_offset = offset;
    return true;
}

bool xx_pef_get_entry_point(const xx_pef *reader, uint16_t *section_index, uint32_t *section_offset)
{
    return reader && pef_get_entry_tuple(reader, reader->main_section, reader->main_offset, section_index, section_offset);
}

bool xx_pef_get_init_point(const xx_pef *reader, uint16_t *section_index, uint32_t *section_offset)
{
    return reader && pef_get_entry_tuple(reader, reader->init_section, reader->init_offset, section_index, section_offset);
}

bool xx_pef_get_term_point(const xx_pef *reader, uint16_t *section_index, uint32_t *section_offset)
{
    return reader && pef_get_entry_tuple(reader, reader->term_section, reader->term_offset, section_index, section_offset);
}
