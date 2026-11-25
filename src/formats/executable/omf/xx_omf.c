/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/omf/xx_omf.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#define OMF_MAX_ITEMS UINT32_C(0x00100000)

static bool omf_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool omf_fail(xx_pd_struct *pd, int code, const char *message)
{
    xx_pd_set_error(pd, code, message);
    return false;
}

static bool omf_span(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static bool omf_type_valid(uint8_t type)
{
    static const uint8_t types[] = {0x80U, 0x82U, 0x88U, 0x8aU, 0x8bU, 0x8cU, 0x90U, 0x91U, 0x94U, 0x95U, 0x96U, 0x98U, 0x99U, 0x9aU, 0x9cU, 0x9dU, 0xa0U, 0xa1U, 0xa2U,
                                    0xa3U, 0xb0U, 0xb2U, 0xb3U, 0xb4U, 0xb6U, 0xb7U, 0xb8U, 0xbcU, 0xc2U, 0xc3U, 0xc4U, 0xc5U, 0xc6U, 0xc8U, 0xc9U, 0xcaU, 0xccU, 0xceU};
    size_t index;
    for (index = 0U; index < sizeof(types); ++index) {
        if (types[index] == type) return true;
    }
    return false;
}

static bool omf_reserve(void **items, size_t *capacity, size_t wanted, size_t item_size)
{
    size_t next;
    void *grown;
    if (!items || !capacity || !item_size || wanted > OMF_MAX_ITEMS || wanted > SIZE_MAX / item_size) return false;
    if (wanted <= *capacity) return true;
    next = *capacity ? *capacity : 8U;
    while (next < wanted) {
        if (next > OMF_MAX_ITEMS / 2U) {
            next = OMF_MAX_ITEMS;
            break;
        }
        next *= 2U;
    }
    if (next < wanted || next > SIZE_MAX / item_size) return false;
    grown = xx_mem_realloc(*items, next * item_size);
    if (!grown) return false;
    *items = grown;
    *capacity = next;
    return true;
}

static void omf_copy_name(char *output, size_t output_size, const uint8_t *input, size_t input_size)
{
    size_t copied;
    if (!output || !output_size) return;
    copied = input_size < output_size - 1U ? input_size : output_size - 1U;
    if (copied) xx_mem_copy(output, input, copied);
    output[copied] = '\0';
}

static bool omf_index(const uint8_t *bytes, size_t size, size_t *cursor, uint16_t *value)
{
    uint8_t first;
    if (!bytes || !cursor || !value || *cursor >= size) return false;
    first = bytes[(*cursor)++];
    if ((first & 0x80U) != 0U) {
        if (*cursor >= size) return false;
        *value = (uint16_t)(((uint16_t)(first & 0x7fU) << 8U) | bytes[(*cursor)++]);
    } else {
        *value = first;
    }
    return true;
}

static void omf_clear_parsed(xx_omf *reader)
{
    uint32_t index;
    if (!reader) return;
    for (index = 0U; index < reader->logical_name_count; ++index) xx_mem_free(reader->logical_names[index]);
    xx_mem_free(reader->logical_names);
    xx_mem_free(reader->segments);
    xx_mem_free(reader->sections);
    xx_mem_free(reader->symbols);
    reader->logical_names = NULL;
    reader->segments = NULL;
    reader->sections = NULL;
    reader->symbols = NULL;
    reader->logical_name_count = 0U;
    reader->segment_count = 0U;
    reader->section_count = 0U;
    reader->symbol_count = 0U;
    reader->bits = 0U;
    reader->has_entry = false;
    xx_mem_zero(&reader->header, sizeof(reader->header));
    reader->entry.offset = -1;
    reader->entry.address = XX_INVALID_ADDRESS;
}

static bool omf_read_record(const Abstractformat *format, uint64_t available, uint64_t relative, uint8_t **record, uint16_t *record_size, xx_pd_struct *pd)
{
    uint8_t prefix[3];
    uint8_t *bytes;
    uint16_t size;
    uint64_t total;
    uint8_t checksum = 0U;
    size_t index;
    if (!format || !format->device || !record || !record_size || !omf_span(relative, sizeof(prefix), available) ||
        relative > (uint64_t)(INT64_MAX - format->base_address) || !xx_io_read_at(format->device, format->base_address + (int64_t)relative, prefix, sizeof(prefix))) {
        return omf_fail(pd, XXFC_ERR_OUT_OF_BOUNDS, "Truncated OMF record header");
    }
    size = xx_data_get_u16(prefix, sizeof(prefix), 1U, false);
    total = (uint64_t)size + 3U;
    if (!omf_type_valid(prefix[0]) || size == 0U || !omf_span(relative, total, available) || total > SIZE_MAX) {
        return omf_fail(pd, XXFC_ERR_OUT_OF_BOUNDS, "Invalid or truncated OMF record");
    }
    bytes = (uint8_t *)xx_mem_alloc((size_t)total);
    if (!bytes) return omf_fail(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate bounded OMF record");
    if (!xx_io_read_at(format->device, format->base_address + (int64_t)relative, bytes, (size_t)total)) {
        xx_mem_free(bytes);
        return omf_fail(pd, XXFC_ERR_IO, "Cannot read OMF record");
    }
    if (bytes[total - 1U] != 0U) {
        for (index = 0U; index < (size_t)total; ++index) checksum = (uint8_t)(checksum + bytes[index]);
        if (checksum != 0U) {
            xx_mem_free(bytes);
            return omf_fail(pd, XXFC_ERR_INVALID_ARG, "Invalid OMF record checksum");
        }
    }
    *record = bytes;
    *record_size = size;
    return true;
}

static bool omf_first_header(const Abstractformat *format, uint64_t available, xx_omf_header *header, xx_pd_struct *pd)
{
    uint8_t *record = NULL;
    uint16_t record_size = 0U;
    uint8_t name_size;
    size_t index;
    bool okay = false;
    if (!omf_read_record(format, available, 0U, &record, &record_size, pd)) return false;
    if ((record[0] != 0x80U && record[0] != 0x82U) || record_size < 2U) goto cleanup;
    name_size = record[3];
    if ((uint16_t)name_size + 2U != record_size) goto cleanup;
    for (index = 0U; index < name_size; ++index) {
        if (record[4U + index] > 0x7fU) goto cleanup;
    }
    if (header) {
        header->record_type = record[0];
        omf_copy_name(header->module_name, sizeof(header->module_name), record + 4U, name_size);
    }
    okay = true;
cleanup:
    xx_mem_free(record);
    if (!okay) omf_fail(pd, XXFC_ERR_INVALID_ARG, "Invalid OMF module header");
    return okay;
}

static bool omf_append_lname(xx_omf *reader, size_t *capacity, const uint8_t *bytes, size_t size)
{
    char *name = NULL;
    if (!reader || !capacity || reader->logical_name_count >= OMF_MAX_ITEMS ||
        !omf_reserve((void **)&reader->logical_names, capacity, (size_t)reader->logical_name_count + 1U, sizeof(*reader->logical_names)))
        return false;
    if (size) {
        name = (char *)xx_mem_alloc(size + 1U);
        if (!name) return false;
        xx_mem_copy(name, bytes, size);
        name[size] = '\0';
    }
    reader->logical_names[reader->logical_name_count++] = name;
    return true;
}

static bool omf_parse_lnames(xx_omf *reader, size_t *capacity, const uint8_t *payload, size_t payload_size)
{
    size_t cursor = 0U;
    while (cursor < payload_size) {
        uint8_t length = payload[cursor++];
        if ((size_t)length > payload_size - cursor || !omf_append_lname(reader, capacity, payload + cursor, length)) return false;
        cursor += length;
    }
    return true;
}

static bool omf_parse_segdef(xx_omf *reader, size_t *capacity, uint8_t type, const uint8_t *payload, size_t payload_size)
{
    xx_omf_segment *segment;
    size_t cursor;
    uint16_t name_index;
    uint8_t bits;
    uint64_t address = XX_OMF_BASE_ADDRESS;
    if (!reader || !payload || payload_size < 1U || reader->segment_count >= OMF_MAX_ITEMS) return false;
    cursor = 1U + (((payload[0] & 0x0eU) != 0U) ? 0U : 3U);
    bits = (payload[0] & 1U) != 0U || type == 0x99U ? 32U : 16U;
    if (bits == 32U) {
        if (cursor > payload_size || payload_size - cursor < 4U) return false;
        cursor += 4U;
    } else {
        if (cursor > payload_size || payload_size - cursor < 2U) return false;
        cursor += 2U;
    }
    if (!omf_index(payload, payload_size, &cursor, &name_index) ||
        !omf_reserve((void **)&reader->segments, capacity, (size_t)reader->segment_count + 1U, sizeof(*reader->segments)))
        return false;
    if (reader->segment_count) {
        const xx_omf_segment *previous = &reader->segments[reader->segment_count - 1U];
        if (previous->address > UINT64_MAX - previous->declared_size) return false;
        address = previous->address + previous->declared_size;
    }
    segment = &reader->segments[reader->segment_count++];
    xx_mem_zero(segment, sizeof(*segment));
    segment->name_index = name_index;
    segment->bits = bits;
    segment->address = address;
    if ((payload[0] & 2U) != 0U) {
        segment->declared_size = bits == 32U ? UINT32_MAX : UINT16_MAX;
    } else if (bits == 32U) {
        /* The variable index width must not affect the fixed length field. */
        segment->declared_size = xx_data_get_u32(payload, payload_size, 1U + (((payload[0] & 0x0eU) != 0U) ? 0U : 3U), false);
    } else {
        segment->declared_size = xx_data_get_u16(payload, payload_size, 1U + (((payload[0] & 0x0eU) != 0U) ? 0U : 3U), false);
    }
    if (bits == 32U) reader->bits = 32U;
    return true;
}

static bool omf_parse_pubdef(xx_omf *reader, size_t *capacity, uint8_t type, const uint8_t *payload, size_t payload_size)
{
    size_t cursor = 0U;
    uint16_t group_index;
    uint16_t segment_index;
    uint8_t bits = (type & 1U) != 0U ? 32U : 16U;
    if (!omf_index(payload, payload_size, &cursor, &group_index) || !omf_index(payload, payload_size, &cursor, &segment_index)) return false;
    if (group_index == 0U && segment_index == 0U) {
        if (payload_size - cursor < 2U) return false;
        cursor += 2U;
    }
    while (cursor < payload_size) {
        xx_omf_public_symbol *symbol;
        uint8_t name_size = payload[cursor++];
        uint16_t type_index;
        uint32_t value;
        if ((size_t)name_size > payload_size - cursor) return false;
        if ((bits == 32U && payload_size - cursor - name_size < 4U) || (bits == 16U && payload_size - cursor - name_size < 2U)) return false;
        value = bits == 32U ? xx_data_get_u32(payload, payload_size, cursor + name_size, false) : xx_data_get_u16(payload, payload_size, cursor + name_size, false);
        if (reader->symbol_count >= OMF_MAX_ITEMS || !omf_reserve((void **)&reader->symbols, capacity, (size_t)reader->symbol_count + 1U, sizeof(*reader->symbols)))
            return false;
        symbol = &reader->symbols[reader->symbol_count];
        xx_mem_zero(symbol, sizeof(*symbol));
        omf_copy_name(symbol->name, sizeof(symbol->name), payload + cursor, name_size);
        symbol->offset = -1;
        symbol->address = XX_INVALID_ADDRESS;
        symbol->value = value;
        symbol->ordinal = reader->symbol_count + 1U;
        symbol->segment_index = segment_index;
        symbol->bits = bits;
        cursor += name_size + (bits == 32U ? 4U : 2U);
        if (!omf_index(payload, payload_size, &cursor, &type_index)) return false;
        (void)type_index;
        ++reader->symbol_count;
    }
    return true;
}

static bool omf_parse_ledata(xx_omf *reader, size_t *capacity, uint8_t type, uint64_t record_relative, const uint8_t *payload, size_t payload_size)
{
    xx_omf_section *section;
    size_t cursor = 0U;
    uint16_t segment_index;
    uint8_t bits = (type & 1U) != 0U ? 32U : 16U;
    uint32_t segment_offset;
    uint64_t absolute;
    if (!omf_index(payload, payload_size, &cursor, &segment_index)) return false;
    if ((bits == 32U && payload_size - cursor < 4U) || (bits == 16U && payload_size - cursor < 2U)) return false;
    segment_offset = bits == 32U ? xx_data_get_u32(payload, payload_size, cursor, false) : xx_data_get_u16(payload, payload_size, cursor, false);
    cursor += bits == 32U ? 4U : 2U;
    if (payload_size - cursor > UINT32_MAX || reader->section_count >= OMF_MAX_ITEMS || record_relative > (uint64_t)(INT64_MAX - reader->format.base_address) ||
        record_relative > (uint64_t)INT64_MAX - 3U || cursor > (uint64_t)INT64_MAX - record_relative - 3U ||
        !omf_reserve((void **)&reader->sections, capacity, (size_t)reader->section_count + 1U, sizeof(*reader->sections)))
        return false;
    absolute = (uint64_t)reader->format.base_address + record_relative + 3U + cursor;
    if (absolute > INT64_MAX) return false;
    section = &reader->sections[reader->section_count];
    xx_mem_zero(section, sizeof(*section));
    section->offset = (int64_t)absolute;
    section->size = (uint32_t)(payload_size - cursor);
    section->virtual_size = section->size;
    section->segment_offset = segment_offset;
    section->permissions = XX_OMF_PERMISSION_READ | XX_OMF_PERMISSION_WRITE | XX_OMF_PERMISSION_EXECUTE;
    section->index = reader->section_count;
    section->segment_index = segment_index;
    section->bits = bits;
    ++reader->section_count;
    return true;
}

static bool omf_resolve(xx_omf *reader, xx_pd_struct *pd)
{
    uint32_t segment_index;
    uint32_t section_index;
    uint32_t symbol_index;
    uint32_t *section_ordinals;
    if (reader->bits != 32U) reader->bits = 16U;
    for (segment_index = 0U; segment_index < reader->segment_count; ++segment_index) {
        const xx_omf_segment *segment = &reader->segments[segment_index];
        if (segment->name_index > reader->logical_name_count) return omf_fail(pd, XXFC_ERR_INVALID_ARG, "OMF SEGDEF name index is out of range");
    }
    section_ordinals = reader->segment_count ? (uint32_t *)xx_mem_calloc(reader->segment_count, sizeof(*section_ordinals)) : NULL;
    if (reader->segment_count && !section_ordinals) return omf_fail(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate OMF section counters");
    for (section_index = 0U; section_index < reader->section_count; ++section_index) {
        xx_omf_section *section = &reader->sections[section_index];
        const xx_omf_segment *segment;
        const char *base_name = NULL;
        uint64_t end;
        uint32_t ordinal;
        if (section->segment_index == 0U || section->segment_index > reader->segment_count) {
            xx_mem_free(section_ordinals);
            return omf_fail(pd, XXFC_ERR_INVALID_ARG, "OMF LEDATA segment index is out of range");
        }
        segment = &reader->segments[section->segment_index - 1U];
        end = (uint64_t)section->segment_offset + section->size;
        if (end > segment->declared_size) {
            xx_mem_free(section_ordinals);
            return omf_fail(pd, XXFC_ERR_INVALID_ARG, "OMF LEDATA exceeds its segment");
        }
        section->address = segment->address + section->segment_offset;
        section->bits = segment->bits;
        ordinal = ++section_ordinals[section->segment_index - 1U];
        if (segment->name_index) base_name = reader->logical_names[segment->name_index - 1U];
        if (base_name && base_name[0]) {
            (void)xx_rt_snprintf(section->name, sizeof(section->name), "%s_%u", base_name, ordinal);
        } else {
            (void)xx_rt_snprintf(section->name, sizeof(section->name), "no_name_%u", ordinal);
        }
    }
    xx_mem_free(section_ordinals);
    for (symbol_index = 0U; symbol_index < reader->symbol_count; ++symbol_index) {
        xx_omf_public_symbol *symbol = &reader->symbols[symbol_index];
        const xx_omf_segment *segment;
        if (symbol->segment_index == 0U) continue;
        if (symbol->segment_index > reader->segment_count) return omf_fail(pd, XXFC_ERR_INVALID_ARG, "OMF PUBDEF segment index is out of range");
        segment = &reader->segments[symbol->segment_index - 1U];
        if (symbol->value > segment->declared_size || segment->address > UINT64_MAX - symbol->value)
            return omf_fail(pd, XXFC_ERR_INVALID_ARG, "OMF PUBDEF value is outside its segment");
        symbol->address = segment->address + symbol->value;
        for (section_index = 0U; section_index < reader->section_count; ++section_index) {
            const xx_omf_section *section = &reader->sections[section_index];
            uint64_t end = (uint64_t)section->segment_offset + section->size;
            if (section->segment_index == symbol->segment_index && symbol->value >= section->segment_offset && symbol->value < end) {
                symbol->offset = section->offset + (int64_t)(symbol->value - section->segment_offset);
                break;
            }
        }
        if (!reader->has_entry && xx_rt_strcmp(symbol->name, "_start") == 0 && symbol->offset >= 0) {
            reader->entry.offset = symbol->offset;
            reader->entry.address = symbol->address;
            reader->entry.symbol_index = symbol_index;
            reader->has_entry = true;
        }
    }
    return true;
}

static bool omf_parse(xx_omf *reader, uint64_t available, xx_pd_struct *pd)
{
    uint64_t relative = 0U;
    size_t name_capacity = 0U;
    size_t segment_capacity = 0U;
    size_t section_capacity = 0U;
    size_t symbol_capacity = 0U;
    uint32_t record_count = 0U;
    if (!omf_first_header(&reader->format, available, &reader->header, pd)) return false;
    while (relative < available) {
        uint8_t *record = NULL;
        uint16_t record_size = 0U;
        uint8_t type;
        const uint8_t *payload;
        size_t payload_size;
        bool okay = true;
        if (record_count++ >= OMF_MAX_ITEMS || omf_stopped(pd)) return omf_fail(pd, XXFC_ERR_INVALID_ARG, "OMF record limit or cancellation reached");
        if (!omf_read_record(&reader->format, available, relative, &record, &record_size, pd)) return false;
        type = record[0];
        payload = record + 3U;
        payload_size = (size_t)record_size - 1U;
        if (type == 0x96U) {
            okay = omf_parse_lnames(reader, &name_capacity, payload, payload_size);
        } else if (type == 0x98U || type == 0x99U) {
            okay = omf_parse_segdef(reader, &segment_capacity, type, payload, payload_size);
        } else if (type == 0x90U || type == 0x91U || type == 0xb6U || type == 0xb7U) {
            okay = omf_parse_pubdef(reader, &symbol_capacity, type, payload, payload_size);
        } else if (type == 0xa0U || type == 0xa1U) {
            okay = omf_parse_ledata(reader, &section_capacity, type, relative, payload, payload_size);
        }
        relative += (uint64_t)record_size + 3U;
        xx_mem_free(record);
        if (!okay) return omf_fail(pd, XXFC_ERR_INVALID_ARG, "Malformed or oversized OMF record payload");
    }
    if (relative != available) return omf_fail(pd, XXFC_ERR_INVALID_ARG, "OMF records do not cover the complete input");
    return omf_resolve(reader, pd);
}

static void omf_vtable_destroy(Abstractformat *format)
{
    xx_omf_destroy((xx_omf *)format);
}

void xx_omf_init(xx_omf *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_OMF;
    reader->format.check_is_valid = xx_omf_check_is_valid;
    reader->format.handle_base_info = xx_omf_handle_base_info;
    reader->format.get_format_size = xx_omf_get_format_size;
    reader->format.get_memory_map = xx_omf_get_memory_map;
    reader->format.destroy = omf_vtable_destroy;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.arch = XX_ARCH_X86;
    reader->format.os = XX_OS_GENERIC;
    reader->format.format_type = XX_TYPE_OBJECT;
    reader->format.is_executable = true;
    reader->format.module_address = XX_OMF_BASE_ADDRESS;
    reader->entry.offset = -1;
    reader->entry.address = XX_INVALID_ADDRESS;
    xx_format_set_extension(&reader->format, "obj;omf");
    xx_format_set_mime_type(&reader->format, "application/x-omf");
}

xx_omf *xx_omf_create(xx_io_device *device, int64_t base_address)
{
    xx_omf *reader = (xx_omf *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_omf_init(reader, device, base_address);
    return reader;
}

void xx_omf_destroy(xx_omf *reader)
{
    if (!reader) return;
    omf_clear_parsed(reader);
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_omf_free(xx_omf *reader)
{
    if (!reader) return;
    xx_omf_destroy(reader);
    xx_mem_free(reader);
}

bool xx_omf_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    int64_t total;
    if (!format || !format->device || format->base_address < 0 || omf_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    return total >= format->base_address && omf_first_header(format, (uint64_t)(total - format->base_address), NULL, pd);
}

bool xx_omf_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    xx_omf *reader = (xx_omf *)format;
    int64_t total;
    uint64_t available;
    if (!reader || !format->device || format->base_address < 0 || omf_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = (uint64_t)(total - format->base_address);
    omf_clear_parsed(reader);
    if (!omf_parse(reader, available, pd)) {
        omf_clear_parsed(reader);
        return false;
    }
    format->format_size = (int64_t)available;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_FILE_TYPE_OMF;
    format->endian = XX_ENDIAN_LITTLE;
    format->arch = XX_ARCH_X86;
    format->os = XX_OS_GENERIC;
    format->format_type = XX_TYPE_OBJECT;
    format->is_executable = true;
    format->module_address = XX_OMF_BASE_ADDRESS;
    format->number_of_symbols = reader->symbol_count;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_omf_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_omf_handle_base_info(format, pd)) ? format->format_size : -1;
}

bool xx_omf_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    xx_omf *reader = (xx_omf *)format;
    uint32_t index;
    if (!reader || !output || omf_stopped(pd) || (!format->base_info_handled && !xx_omf_handle_base_info(format, pd))) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_SECTIONS;
    if (mode != XX_MEMORY_MAP_MODE_SECTIONS && mode != XX_MEMORY_MAP_MODE_SEGMENTS) return false;
    output->binary_offset = format->base_address;
    output->binary_size = format->format_size;
    output->module_address = XX_OMF_BASE_ADDRESS;
    output->entry_point_address = reader->has_entry ? reader->entry.address : XX_INVALID_ADDRESS;
    output->code_base = reader->has_entry ? (int64_t)reader->entry.address : -1;
    output->start_load_offset = reader->has_entry ? reader->entry.offset : format->base_address;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = XX_MEMORY_MAP_MODE_SECTIONS;
    output->is_image = true;
    for (index = 0U; index < reader->section_count; ++index) {
        const xx_omf_section *section = &reader->sections[index];
        if (!xx_memory_map_add_part(output, section->offset, section->size, section->address, section->virtual_size, XX_FILE_PART_SECTION, (int32_t)index, section->name,
                                    false))
            return false;
    }
    if (!reader->section_count && format->format_size > 0 &&
        !xx_memory_map_add_part(output, format->base_address, format->format_size, XX_INVALID_ADDRESS, format->format_size, XX_FILE_PART_REGION, 0, "OMF records", false))
        return false;
    return xx_memory_map_finalize(output);
}

const xx_omf_header *xx_omf_get_header(xx_omf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_omf_handle_base_info(&reader->format, pd)) ? &reader->header : NULL;
}

uint8_t xx_omf_get_bits(xx_omf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_omf_handle_base_info(&reader->format, pd)) ? reader->bits : 0U;
}

uint32_t xx_omf_get_number_of_segments(xx_omf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_omf_handle_base_info(&reader->format, pd)) ? reader->segment_count : 0U;
}

const xx_omf_segment *xx_omf_get_segment(xx_omf *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_omf_get_number_of_segments(reader, pd) ? &reader->segments[index] : NULL;
}

uint32_t xx_omf_get_number_of_sections(xx_omf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_omf_handle_base_info(&reader->format, pd)) ? reader->section_count : 0U;
}

const xx_omf_section *xx_omf_get_section(xx_omf *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_omf_get_number_of_sections(reader, pd) ? &reader->sections[index] : NULL;
}

const xx_omf_entry *xx_omf_get_entry(xx_omf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_omf_handle_base_info(&reader->format, pd)) && reader->has_entry ? &reader->entry : NULL;
}
