/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/macho/xx_macho.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

#define MACHO_EXCEPTION_SECTION_LIMIT UINT32_C(65536)
#define MACHO_EXCEPTION_INDEX_LIMIT UINT32_C(65536)
#define MACHO_EXCEPTION_LSDA_LIMIT UINT32_C(262144)
#define MACHO_EXCEPTION_REGION_LIMIT UINT32_C(262144)
#define MACHO_EXCEPTION_ACTION_DEPTH 64U

#define MACHO_DW_EH_PE_OMIT UINT8_C(0xff)
#define MACHO_DW_EH_PE_ABSPTR UINT8_C(0x00)
#define MACHO_DW_EH_PE_ULEB128 UINT8_C(0x01)
#define MACHO_DW_EH_PE_UDATA2 UINT8_C(0x02)
#define MACHO_DW_EH_PE_UDATA4 UINT8_C(0x03)
#define MACHO_DW_EH_PE_UDATA8 UINT8_C(0x04)
#define MACHO_DW_EH_PE_SLEB128 UINT8_C(0x09)
#define MACHO_DW_EH_PE_SDATA2 UINT8_C(0x0a)
#define MACHO_DW_EH_PE_SDATA4 UINT8_C(0x0b)
#define MACHO_DW_EH_PE_SDATA8 UINT8_C(0x0c)
#define MACHO_DW_EH_PE_PCREL UINT8_C(0x10)
#define MACHO_DW_EH_PE_TEXTREL UINT8_C(0x20)
#define MACHO_DW_EH_PE_DATAREL UINT8_C(0x30)
#define MACHO_DW_EH_PE_FUNCREL UINT8_C(0x40)

typedef struct macho_exception_stream_s {
    xx_macho_exception_region *regions;
    size_t count;
    size_t capacity;
} macho_exception_stream;

typedef struct macho_exception_section_s {
    char name[17];
    uint64_t address;
    uint64_t size;
    uint64_t offset;
} macho_exception_section;

typedef struct macho_exception_layout_s {
    Abstractformat *format;
    macho_exception_section *sections;
    size_t section_count;
    size_t section_capacity;
    size_t unwind_index;
    uint64_t preferred_base;
    uint64_t module_base;
    bool is_64;
    bool big_endian;
} macho_exception_layout;

typedef struct macho_eh_reader_s {
    const macho_exception_layout *layout;
    const macho_exception_section *section;
    uint64_t position;
    uint64_t end;
    uint64_t function_address;
    unsigned int bits;
} macho_eh_reader;

static bool macho_add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (!result || right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool macho_add_s64(uint64_t base, int64_t relative, uint64_t *result)
{
    if (!result) return false;
    if (relative < 0) {
        uint64_t magnitude = (uint64_t)(-(relative + 1)) + 1U;
        if (magnitude > base) return false;
        *result = base - magnitude;
        return true;
    }
    return macho_add_u64(base, (uint64_t)relative, result);
}

static bool macho_relative_range(const Abstractformat *format, uint64_t offset, uint64_t size)
{
    int64_t total;
    uint64_t available;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    if (format->base_info_handled && format->format_size >= 0 && (uint64_t)format->format_size < available) {
        available = (uint64_t)format->format_size;
    }
    return offset <= available && size <= available - offset;
}

static bool macho_absolute_offset(const Abstractformat *format, uint64_t relative, int64_t *absolute)
{
    if (!format || !absolute || format->base_address < 0 || relative > (uint64_t)(INT64_MAX - format->base_address)) return false;
    *absolute = format->base_address + (int64_t)relative;
    return true;
}

static bool macho_read_u8(const Abstractformat *format, uint64_t offset, uint8_t *value)
{
    int64_t absolute;
    if (!value || !macho_relative_range(format, offset, 1U) || !macho_absolute_offset(format, offset, &absolute)) return false;
    *value = xx_io_get_u8(format->device, absolute);
    return true;
}

static bool macho_read_u32(const Abstractformat *format, uint64_t offset, bool big_endian, uint32_t *value)
{
    int64_t absolute;
    if (!value || !macho_relative_range(format, offset, 4U) || !macho_absolute_offset(format, offset, &absolute)) return false;
    *value = xx_io_get_u32(format->device, absolute, big_endian);
    return true;
}

static bool macho_read_u64(const Abstractformat *format, uint64_t offset, bool big_endian, uint64_t *value)
{
    int64_t absolute;
    if (!value || !macho_relative_range(format, offset, 8U) || !macho_absolute_offset(format, offset, &absolute)) return false;
    *value = xx_io_get_u64(format->device, absolute, big_endian);
    return true;
}

static bool macho_read_name(const Abstractformat *format, uint64_t offset, char value[17])
{
    size_t index;
    if (!value || !macho_relative_range(format, offset, 16U)) return false;
    for (index = 0U; index < 16U; ++index) {
        if (!macho_read_u8(format, offset + index, (uint8_t *)&value[index])) return false;
    }
    value[16] = '\0';
    return true;
}

static void macho_layout_free(macho_exception_layout *layout)
{
    if (!layout) return;
    xx_mem_free(layout->sections);
    memset(layout, 0, sizeof(*layout));
}

static bool macho_layout_append_section(macho_exception_layout *layout, const macho_exception_section *section)
{
    macho_exception_section *grown;
    size_t capacity;
    if (!layout || !section || layout->section_count >= MACHO_EXCEPTION_SECTION_LIMIT) return false;
    if (layout->section_count == layout->section_capacity) {
        capacity = layout->section_capacity ? layout->section_capacity * 2U : 32U;
        if (capacity > MACHO_EXCEPTION_SECTION_LIMIT) capacity = MACHO_EXCEPTION_SECTION_LIMIT;
        grown = (macho_exception_section *)xx_mem_realloc(layout->sections, capacity * sizeof(*grown));
        if (!grown) return false;
        layout->sections = grown;
        layout->section_capacity = capacity;
    }
    layout->sections[layout->section_count] = *section;
    if (strcmp(section->name, "__unwind_info") == 0) layout->unwind_index = layout->section_count;
    ++layout->section_count;
    return true;
}

static bool macho_layout_parse(xx_macho *macho, macho_exception_layout *layout)
{
    Abstractformat *format;
    uint32_t magic, command_count, commands_size;
    uint64_t header_size, cursor, commands_end;
    uint64_t preferred = UINT64_MAX;
    uint32_t command_index;
    if (!macho || !layout) return false;
    memset(layout, 0, sizeof(*layout));
    layout->unwind_index = SIZE_MAX;
    layout->format = format = &macho->format;
    if (!macho_read_u32(format, 0U, false, &magic)) return false;
    if (magic == XX_MACHO_MAGIC_32 || magic == XX_MACHO_MAGIC_64) {
        layout->big_endian = false;
    } else if (magic == XX_MACHO_CIGAM_32 || magic == XX_MACHO_CIGAM_64) {
        layout->big_endian = true;
    } else {
        return false;
    }
    layout->is_64 = magic == XX_MACHO_MAGIC_64 || magic == XX_MACHO_CIGAM_64;
    header_size = layout->is_64 ? 32U : 28U;
    if (!macho_read_u32(format, 16U, layout->big_endian, &command_count) || !macho_read_u32(format, 20U, layout->big_endian, &commands_size) ||
        command_count > UINT32_C(65536) || commands_size < (uint64_t)command_count * 8U || !macho_relative_range(format, header_size, commands_size))
        return false;
    cursor = header_size;
    commands_end = header_size + commands_size;
    for (command_index = 0U; command_index < command_count; ++command_index) {
        uint32_t command, command_size;
        if (!macho_read_u32(format, cursor, layout->big_endian, &command) || !macho_read_u32(format, cursor + 4U, layout->big_endian, &command_size) ||
            command_size < 8U || command_size > commands_end - cursor) {
            macho_layout_free(layout);
            return false;
        }
        if ((!layout->is_64 && command == XX_MACHO_LOAD_SEGMENT) || (layout->is_64 && command == XX_MACHO_LOAD_SEGMENT_64)) {
            uint64_t segment_header = layout->is_64 ? 72U : 56U;
            uint64_t section_size = layout->is_64 ? 80U : 68U;
            uint64_t virtual_address, virtual_size, file_offset, file_size;
            uint32_t section_count, section_index;
            if ((uint64_t)command_size < segment_header) {
                macho_layout_free(layout);
                return false;
            }
            if (layout->is_64) {
                if (!macho_read_u64(format, cursor + 24U, layout->big_endian, &virtual_address) ||
                    !macho_read_u64(format, cursor + 32U, layout->big_endian, &virtual_size) || !macho_read_u64(format, cursor + 40U, layout->big_endian, &file_offset) ||
                    !macho_read_u64(format, cursor + 48U, layout->big_endian, &file_size) || !macho_read_u32(format, cursor + 64U, layout->big_endian, &section_count)) {
                    macho_layout_free(layout);
                    return false;
                }
            } else {
                uint32_t value;
                if (!macho_read_u32(format, cursor + 24U, layout->big_endian, &value)) {
                    macho_layout_free(layout);
                    return false;
                }
                virtual_address = value;
                if (!macho_read_u32(format, cursor + 28U, layout->big_endian, &value)) {
                    macho_layout_free(layout);
                    return false;
                }
                virtual_size = value;
                if (!macho_read_u32(format, cursor + 32U, layout->big_endian, &value)) {
                    macho_layout_free(layout);
                    return false;
                }
                file_offset = value;
                if (!macho_read_u32(format, cursor + 36U, layout->big_endian, &value) || !macho_read_u32(format, cursor + 48U, layout->big_endian, &section_count)) {
                    macho_layout_free(layout);
                    return false;
                }
                file_size = value;
            }
            if (section_count > MACHO_EXCEPTION_SECTION_LIMIT - layout->section_count || (uint64_t)section_count > (UINT64_MAX - segment_header) / section_size ||
                segment_header + (uint64_t)section_count * section_size > command_size || !macho_relative_range(format, file_offset, file_size) ||
                virtual_size < file_size) {
                macho_layout_free(layout);
                return false;
            }
            if (file_size && virtual_address >= file_offset && virtual_address - file_offset < preferred) {
                preferred = virtual_address - file_offset;
            }
            for (section_index = 0U; section_index < section_count; ++section_index) {
                uint64_t at = cursor + segment_header + (uint64_t)section_index * section_size;
                macho_exception_section section;
                uint32_t value;
                memset(&section, 0, sizeof(section));
                if (!macho_read_name(format, at, section.name)) {
                    macho_layout_free(layout);
                    return false;
                }
                if (layout->is_64) {
                    if (!macho_read_u64(format, at + 32U, layout->big_endian, &section.address) || !macho_read_u64(format, at + 40U, layout->big_endian, &section.size) ||
                        !macho_read_u32(format, at + 48U, layout->big_endian, &value)) {
                        macho_layout_free(layout);
                        return false;
                    }
                    section.offset = value;
                } else {
                    if (!macho_read_u32(format, at + 32U, layout->big_endian, &value)) {
                        macho_layout_free(layout);
                        return false;
                    }
                    section.address = value;
                    if (!macho_read_u32(format, at + 36U, layout->big_endian, &value)) {
                        macho_layout_free(layout);
                        return false;
                    }
                    section.size = value;
                    if (!macho_read_u32(format, at + 40U, layout->big_endian, &value)) {
                        macho_layout_free(layout);
                        return false;
                    }
                    section.offset = value;
                }
                if (section.address > UINT64_MAX - section.size || !macho_layout_append_section(layout, &section)) {
                    macho_layout_free(layout);
                    return false;
                }
            }
        }
        cursor += command_size;
    }
    if (cursor != commands_end) {
        macho_layout_free(layout);
        return false;
    }
    if (preferred == UINT64_MAX) preferred = 0U;
    layout->preferred_base = preferred;
    layout->module_base = format->module_address != XX_INVALID_ADDRESS ? format->module_address : preferred;
    return true;
}

static const macho_exception_section *macho_find_section(const macho_exception_layout *layout, uint64_t address)
{
    size_t index;
    if (!layout) return NULL;
    for (index = 0U; index < layout->section_count; ++index) {
        const macho_exception_section *section = &layout->sections[index];
        if (address >= section->address && address - section->address < section->size) return section;
    }
    return NULL;
}

static bool macho_section_u32(const macho_exception_layout *layout, const macho_exception_section *section, uint64_t offset, uint32_t *value)
{
    if (!layout || !section || !value || offset > section->size || section->size - offset < 4U || section->offset > UINT64_MAX - offset) return false;
    return macho_read_u32(layout->format, section->offset + offset, layout->big_endian, value);
}

static bool macho_eh_u8(macho_eh_reader *reader, uint8_t *value)
{
    if (!reader || !value || reader->position >= reader->end || reader->section->offset > UINT64_MAX - reader->position ||
        !macho_read_u8(reader->layout->format, reader->section->offset + reader->position, value))
        return false;
    ++reader->position;
    return true;
}

static bool macho_eh_uleb(macho_eh_reader *reader, uint64_t *value)
{
    uint64_t result = 0U;
    unsigned int shift = 0U, index;
    if (!reader || !value) return false;
    for (index = 0U; index < 10U; ++index) {
        uint8_t byte;
        uint64_t payload;
        if (!macho_eh_u8(reader, &byte)) return false;
        payload = byte & UINT8_C(0x7f);
        if (shift == 63U && payload > 1U) return false;
        result |= payload << shift;
        if (!(byte & UINT8_C(0x80))) {
            *value = result;
            return true;
        }
        shift += 7U;
    }
    return false;
}

static bool macho_eh_sleb(macho_eh_reader *reader, int64_t *value)
{
    uint64_t result = 0U;
    unsigned int shift = 0U, index;
    if (!reader || !value) return false;
    for (index = 0U; index < 10U; ++index) {
        uint8_t byte;
        uint8_t payload;
        unsigned int next_shift;
        if (!macho_eh_u8(reader, &byte)) return false;
        payload = byte & UINT8_C(0x7f);
        if (shift == 63U && payload != 0U && payload != UINT8_C(0x7f)) return false;
        result |= (uint64_t)(payload & (shift == 63U ? 1U : UINT8_C(0x7f))) << shift;
        next_shift = shift + 7U;
        if (!(byte & UINT8_C(0x80))) {
            if ((byte & UINT8_C(0x40)) && next_shift < 64U) result |= UINT64_MAX << next_shift;
            *value = (int64_t)result;
            return true;
        }
        shift = next_shift;
    }
    return false;
}

static size_t macho_eh_encoding_size(uint8_t encoding, unsigned int bits)
{
    switch (encoding & UINT8_C(0x0f)) {
        case MACHO_DW_EH_PE_ABSPTR: return bits / 8U;
        case MACHO_DW_EH_PE_UDATA2:
        case MACHO_DW_EH_PE_SDATA2: return 2U;
        case MACHO_DW_EH_PE_UDATA4:
        case MACHO_DW_EH_PE_SDATA4: return 4U;
        case MACHO_DW_EH_PE_UDATA8:
        case MACHO_DW_EH_PE_SDATA8: return 8U;
        default: return 0U;
    }
}

static bool macho_eh_fixed(macho_eh_reader *reader, size_t size, uint64_t *value)
{
    uint64_t result = 0U;
    size_t index;
    if (!reader || !value || (size != 2U && size != 4U && size != 8U) || size > reader->end - reader->position) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t byte;
        if (!macho_eh_u8(reader, &byte)) return false;
        if (reader->layout->big_endian) result = (result << 8U) | byte;
        else result |= (uint64_t)byte << (index * 8U);
    }
    *value = result;
    return true;
}

static bool macho_eh_encoded(macho_eh_reader *reader, uint8_t encoding, uint64_t *value, bool apply_base)
{
    uint64_t field_address, raw = 0U, base = 0U;
    int64_t signed_raw = 0;
    size_t size = 0U;
    bool is_signed = false;
    if (!reader || !value || encoding == MACHO_DW_EH_PE_OMIT || reader->position >= reader->end ||
        !macho_add_u64(reader->section->address, reader->position, &field_address))
        return false;
    switch (encoding & UINT8_C(0x0f)) {
        case MACHO_DW_EH_PE_ABSPTR:
            size = reader->bits / 8U;
            if (size != 4U && size != 8U) return false;
            break;
        case MACHO_DW_EH_PE_ULEB128:
            if (!macho_eh_uleb(reader, &raw)) return false;
            goto decoded;
        case MACHO_DW_EH_PE_UDATA2: size = 2U; break;
        case MACHO_DW_EH_PE_UDATA4: size = 4U; break;
        case MACHO_DW_EH_PE_UDATA8: size = 8U; break;
        case MACHO_DW_EH_PE_SLEB128:
            if (!macho_eh_sleb(reader, &signed_raw)) return false;
            raw = (uint64_t)signed_raw;
            is_signed = true;
            goto decoded;
        case MACHO_DW_EH_PE_SDATA2:
            size = 2U;
            is_signed = true;
            break;
        case MACHO_DW_EH_PE_SDATA4:
            size = 4U;
            is_signed = true;
            break;
        case MACHO_DW_EH_PE_SDATA8:
            size = 8U;
            is_signed = true;
            break;
        default: return false;
    }
    if (!macho_eh_fixed(reader, size, &raw)) return false;
    if (is_signed) {
        if (size == 2U) signed_raw = (int16_t)raw;
        else if (size == 4U) signed_raw = (int32_t)raw;
        else signed_raw = (int64_t)raw;
        raw = (uint64_t)signed_raw;
    }

decoded:
    if (apply_base && raw) {
        switch (encoding & UINT8_C(0x70)) {
            case MACHO_DW_EH_PE_PCREL: base = field_address; break;
            case MACHO_DW_EH_PE_TEXTREL:
            case MACHO_DW_EH_PE_DATAREL: base = reader->layout->preferred_base; break;
            case MACHO_DW_EH_PE_FUNCREL: base = reader->function_address; break;
            default: base = 0U; break;
        }
        if (base && !(is_signed ? macho_add_s64(base, signed_raw, &raw) : macho_add_u64(base, raw, &raw))) return false;
    }
    *value = raw;
    return true;
}

static bool macho_append_region(macho_exception_stream *stream, const xx_macho_exception_region *region)
{
    xx_macho_exception_region *grown;
    size_t capacity;
    if (!stream || !region || stream->count >= MACHO_EXCEPTION_REGION_LIMIT) return false;
    if (stream->count == stream->capacity) {
        capacity = stream->capacity ? stream->capacity * 2U : 16U;
        if (capacity > MACHO_EXCEPTION_REGION_LIMIT) capacity = MACHO_EXCEPTION_REGION_LIMIT;
        grown = (xx_macho_exception_region *)xx_mem_realloc(stream->regions, capacity * sizeof(*grown));
        if (!grown) return false;
        stream->regions = grown;
        stream->capacity = capacity;
    }
    stream->regions[stream->count++] = *region;
    return true;
}

static bool macho_type_is_catch_all(const macho_eh_reader *lsda, uint64_t type_table, uint8_t encoding, int64_t type_filter, bool *catch_all)
{
    size_t entry_size;
    uint64_t distance, entry_position, type_address;
    macho_eh_reader entry;
    if (!catch_all) return false;
    *catch_all = false;
    if (type_filter <= 0) return true;
    entry_size = macho_eh_encoding_size(encoding, lsda->bits);
    if (!entry_size || (uint64_t)type_filter > UINT64_MAX / entry_size) return true;
    distance = (uint64_t)type_filter * entry_size;
    if (distance > type_table) return true;
    entry_position = type_table - distance;
    entry = *lsda;
    entry.position = entry_position;
    if (macho_eh_encoded(&entry, encoding, &type_address, true) && type_address == 0U) *catch_all = true;
    return true;
}

static bool macho_add_action(macho_exception_stream *stream, const macho_eh_reader *lsda, uint64_t action_table, uint64_t type_table, uint8_t type_encoding,
                             uint64_t action, uint64_t source, uint64_t from, uint64_t to, uint64_t handler)
{
    uint64_t record;
    unsigned int depth;
    if (!stream || !lsda) return false;
    if (!action) {
        xx_macho_exception_region region;
        memset(&region, 0, sizeof(region));
        region.source = source;
        region.from = from;
        region.to = to;
        region.handler = handler;
        region.kind = XX_MACHO_EXCEPTION_CLEANUP;
        return macho_append_region(stream, &region);
    }
    if (action_table > lsda->end || action - 1U >= lsda->end - action_table) return false;
    record = action_table + action - 1U;
    for (depth = 0U; depth < MACHO_EXCEPTION_ACTION_DEPTH; ++depth) {
        macho_eh_reader action_reader = *lsda;
        int64_t type_filter, next;
        uint64_t next_field, target;
        xx_macho_exception_region region;
        action_reader.position = record;
        if (!macho_eh_sleb(&action_reader, &type_filter)) return false;
        next_field = action_reader.position;
        if (!macho_eh_sleb(&action_reader, &next)) return false;
        memset(&region, 0, sizeof(region));
        region.source = source;
        region.from = from;
        region.to = to;
        region.handler = handler;
        region.type_filter = type_filter;
        region.kind = type_filter < 0 ? XX_MACHO_EXCEPTION_FILTER : XX_MACHO_EXCEPTION_CATCH;
        if (!macho_type_is_catch_all(lsda, type_table, type_encoding, type_filter, &region.catch_all) || !macho_append_region(stream, &region)) return false;
        if (!next) return true;
        if (!macho_add_s64(next_field, next, &target) || target < action_table || target >= lsda->end) return false;
        record = target;
    }
    return false;
}

static bool macho_relocate_to_module(const macho_exception_layout *layout, uint64_t preferred_address, uint64_t *module_address)
{
    if (!layout || !module_address || preferred_address < layout->preferred_base) return false;
    return macho_add_u64(layout->module_base, preferred_address - layout->preferred_base, module_address);
}

static bool macho_parse_lsda(const macho_exception_layout *layout, const macho_exception_section *section, macho_exception_stream *stream, uint64_t function_offset,
                             uint64_t lsda_address)
{
    macho_eh_reader reader;
    uint64_t source, preferred_function, lpstart, type_table = 0U, callsite_size, callsite_end, action_table;
    uint8_t lp_encoding, type_encoding, callsite_encoding;
    if (!layout || !section || !stream || lsda_address < section->address || !macho_add_u64(layout->module_base, function_offset, &source) ||
        !macho_add_u64(layout->preferred_base, function_offset, &preferred_function))
        return false;
    memset(&reader, 0, sizeof(reader));
    reader.layout = layout;
    reader.section = section;
    reader.position = lsda_address - section->address;
    reader.end = section->size;
    /* Encoded LSDA addresses live in the Mach-O preferred address space.
     * Keep DW_EH_PE_funcrel there and apply the runtime slide exactly once
     * after decoding the LPStart value. */
    reader.function_address = preferred_function;
    reader.bits = layout->is_64 ? 64U : 32U;
    if (!macho_eh_u8(&reader, &lp_encoding)) return false;
    lpstart = source;
    if (lp_encoding != MACHO_DW_EH_PE_OMIT) {
        uint64_t preferred_lp;
        if (!macho_eh_encoded(&reader, lp_encoding, &preferred_lp, true) || !macho_relocate_to_module(layout, preferred_lp, &lpstart)) return false;
    }
    if (!macho_eh_u8(&reader, &type_encoding)) return false;
    if (type_encoding != MACHO_DW_EH_PE_OMIT) {
        uint64_t type_offset;
        if (!macho_eh_uleb(&reader, &type_offset) || type_offset > reader.end - reader.position) return false;
        type_table = reader.position + type_offset;
    }
    if (!macho_eh_u8(&reader, &callsite_encoding) || !macho_eh_uleb(&reader, &callsite_size) || callsite_size > reader.end - reader.position) return false;
    callsite_end = reader.position + callsite_size;
    action_table = callsite_end;
    while (reader.position < callsite_end) {
        uint64_t start, length, landing, action, from, to, handler;
        if (!macho_eh_encoded(&reader, callsite_encoding, &start, false) || !macho_eh_encoded(&reader, callsite_encoding, &length, false) ||
            !macho_eh_encoded(&reader, callsite_encoding, &landing, false) || !macho_eh_uleb(&reader, &action) || reader.position > callsite_end)
            return false;
        if (!landing) continue;
        if (!macho_add_u64(source, start, &from) || !macho_add_u64(from, length, &to) || !macho_add_u64(lpstart, landing, &handler) || to < from ||
            !macho_add_action(stream, &reader, action_table, type_table, type_encoding, action, source, from, to, handler))
            return false;
    }
    return reader.position == callsite_end;
}

static bool macho_collect_exceptions(xx_macho *macho, macho_exception_stream *stream, xx_pd_struct *pd)
{
    macho_exception_layout layout;
    const macho_exception_section *unwind;
    uint32_t version, index_offset, index_count, index;
    uint64_t lsda_seen = 0U;
    bool result = true;
    if (!macho || !stream || xx_pd_is_stopped(pd)) return false;
    if (!macho->format.base_info_handled && !xx_macho_handle_base_info(&macho->format, pd)) return false;
    if (!macho_layout_parse(macho, &layout)) return false;
    if (layout.unwind_index == SIZE_MAX) goto done;
    unwind = &layout.sections[layout.unwind_index];
    if (unwind->size < 28U || !macho_section_u32(&layout, unwind, 0U, &version) || !macho_section_u32(&layout, unwind, 20U, &index_offset) ||
        !macho_section_u32(&layout, unwind, 24U, &index_count) || version != 1U || index_count < 2U || index_count > MACHO_EXCEPTION_INDEX_LIMIT ||
        (uint64_t)index_count > unwind->size / 12U || index_offset > unwind->size - (uint64_t)index_count * 12U)
        goto done;
    for (index = 0U; index + 1U < index_count && !xx_pd_is_stopped(pd); ++index) {
        uint64_t entry = (uint64_t)index_offset + (uint64_t)index * 12U;
        uint32_t lsda_offset, next_lsda_offset;
        uint64_t at;
        if (!macho_section_u32(&layout, unwind, entry + 8U, &lsda_offset) || !macho_section_u32(&layout, unwind, entry + 20U, &next_lsda_offset) ||
            lsda_offset > next_lsda_offset || next_lsda_offset > unwind->size || (next_lsda_offset - lsda_offset) % 8U != 0U)
            continue;
        for (at = lsda_offset; at < next_lsda_offset && !xx_pd_is_stopped(pd); at += 8U) {
            uint32_t function_offset, lsda_offset_from_base;
            uint64_t lsda_address;
            const macho_exception_section *section;
            size_t before;
            if (++lsda_seen > MACHO_EXCEPTION_LSDA_LIMIT) goto done;
            if (!macho_section_u32(&layout, unwind, at, &function_offset) || !macho_section_u32(&layout, unwind, at + 4U, &lsda_offset_from_base) ||
                !macho_add_u64(layout.preferred_base, lsda_offset_from_base, &lsda_address))
                continue;
            section = macho_find_section(&layout, lsda_address);
            if (!section) continue;
            before = stream->count;
            if (!macho_parse_lsda(&layout, section, stream, function_offset, lsda_address)) {
                stream->count = before;
            }
        }
    }
    if (xx_pd_is_stopped(pd)) result = false;

done:
    macho_layout_free(&layout);
    return result;
}

static void macho_exception_stream_free(void *pointer)
{
    macho_exception_stream *stream = (macho_exception_stream *)pointer;
    if (!stream) return;
    xx_mem_free(stream->regions);
    xx_mem_free(stream);
}

xx_macho_exception_state *xx_macho_create_exception_regions_reading(Abstractformat *format, xx_pd_struct *pd)
{
    xx_macho_exception_state *state;
    macho_exception_stream *stream;
    if (!format || xx_pd_is_stopped(pd)) return NULL;
    state = (xx_macho_exception_state *)xx_mem_calloc(1U, sizeof(*state));
    stream = (macho_exception_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        xx_mem_free(state);
        xx_mem_free(stream);
        return NULL;
    }
    state->format = format;
    state->current_index = -1;
    state->total_regions = -1;
    state->internal_state = stream;
    state->free_internal = macho_exception_stream_free;
    if (!macho_collect_exceptions((xx_macho *)format, stream, pd)) {
        xx_macho_free_exception_regions_reading(format, state);
        return NULL;
    }
    state->total_regions = (int64_t)stream->count;
    (void)xx_macho_exception_region_move_to_next(format, state, pd);
    return state;
}

const xx_macho_exception_region *xx_macho_get_current_exception_region(Abstractformat *format, xx_macho_exception_state *state)
{
    return state && state->format == format && state->has_region ? &state->current_region : NULL;
}

bool xx_macho_exception_region_move_to_next(Abstractformat *format, xx_macho_exception_state *state, xx_pd_struct *pd)
{
    macho_exception_stream *stream;
    int64_t next;
    if (!format || !state || state->format != format || xx_pd_is_stopped(pd) || !(stream = (macho_exception_stream *)state->internal_state)) {
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

void xx_macho_free_exception_regions_reading(Abstractformat *format, xx_macho_exception_state *state)
{
    (void)format;
    if (!state) return;
    if (state->free_internal && state->internal_state) state->free_internal(state->internal_state);
    xx_mem_free(state);
}

uint64_t xx_macho_get_number_of_exception_regions(Abstractformat *format, xx_pd_struct *pd)
{
    xx_macho_exception_state *state = xx_macho_create_exception_regions_reading(format, pd);
    uint64_t result = state && state->total_regions > 0 ? (uint64_t)state->total_regions : 0U;
    xx_macho_free_exception_regions_reading(format, state);
    return result;
}
