/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_pe_stream_internal.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>

#define PE_COFF_SYMBOL_SIZE 18U
#define PE_COFF_SYMBOL_LIMIT UINT32_C(1048576)
#define PE_COFF_NAME_LIMIT UINT32_C(65536)

typedef struct pe_coff_cursor {
    Abstractformat *format;
    int64_t table_offset;
    int64_t strings_offset;
    uint32_t table_count;
    uint32_t strings_size;
    uint32_t next_index;
    bool finished;
} pe_coff_cursor;

static bool pe_coff_error(xx_pd_struct *pd, const char *message) {
    xx_pd_set_error(pd, XXFC_ERR_IO, message);
    return false;
}

static bool pe_coff_out_of_memory(xx_pd_struct *pd, const char *message) {
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, message);
    return false;
}

static bool pe_coff_cancelled(xx_pd_struct *pd) {
    if (!xx_pd_is_stopped(pd)) return false;
    xx_pd_set_error(pd, XXFC_ERR_GENERIC, "PE COFF symbol stream cancelled");
    return true;
}

static uint16_t pe_coff_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t pe_coff_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool pe_coff_range(Abstractformat *format, uint64_t relative,
                           uint64_t size, int64_t *offset, xx_pd_struct *pd) {
    int64_t total = xx_io_size(format->device);
    if (format->base_address < 0 || total < format->base_address ||
        relative > (uint64_t)(total - format->base_address) ||
        size > (uint64_t)(total - format->base_address) - relative)
        return pe_coff_error(pd, "PE COFF table range outside input");
    *offset = format->base_address + (int64_t)relative;
    return true;
}

static char *pe_coff_short_name(const uint8_t *bytes, size_t available,
                                xx_pd_struct *pd) {
    size_t length = 0U;
    char *result;
    while (length < available && bytes[length]) ++length;
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) {
        pe_coff_out_of_memory(pd, "Cannot allocate PE COFF symbol name");
        return NULL;
    }
    xx_mem_copy(result, bytes, length);
    result[length] = '\0';
    return result;
}

static char *pe_coff_long_name(pe_coff_cursor *cursor, uint32_t index,
                               xx_pd_struct *pd) {
    char *result = NULL;
    uint32_t used = 0U;
    uint32_t maximum;
    if (index < 4U || index >= cursor->strings_size) {
        pe_coff_error(pd, "Invalid PE COFF string-table offset");
        return NULL;
    }
    maximum = cursor->strings_size - index;
    if (maximum > PE_COFF_NAME_LIMIT) maximum = PE_COFF_NAME_LIMIT;
    while (used < maximum && !pe_coff_cancelled(pd)) {
        uint8_t bytes[256];
        uint32_t amount = maximum - used;
        uint32_t length;
        char *replacement;
        if (amount > sizeof(bytes)) amount = sizeof(bytes);
        if (!xx_pe_stream_read(cursor->format,
                cursor->strings_offset + index + used, bytes, amount, pd)) break;
        for (length = 0U; length < amount && bytes[length]; ++length) {}
        replacement = (char *)xx_mem_realloc(result, (size_t)used + length + 1U);
        if (!replacement) {
            pe_coff_out_of_memory(pd, "Cannot allocate PE COFF symbol name");
            xx_mem_free(result);
            return NULL;
        }
        result = replacement;
        xx_mem_copy(result + used, bytes, length);
        used += length;
        result[used] = '\0';
        if (length < amount) return result;
    }
    xx_mem_free(result);
    if (!pe_coff_cancelled(pd))
        pe_coff_error(pd, "Unterminated or oversized PE COFF symbol name");
    return NULL;
}

static xx_symbol_binding_t pe_coff_binding(uint32_t storage) {
    switch (storage) {
    case 2U: case 5U: return XX_SYMBOL_BINDING_GLOBAL;
    case 105U: return XX_SYMBOL_BINDING_WEAK;
    case 1U: case 3U: case 4U: case 6U: case 7U: case 9U: case 103U:
        return XX_SYMBOL_BINDING_LOCAL;
    default: return XX_SYMBOL_BINDING_UNKNOWN;
    }
}

static xx_symbol_kind_t pe_coff_kind(const xx_symbol_record *record) {
    if (record->native_storage_class == 103U) return XX_SYMBOL_KIND_FILE;
    /* .bb/.eb and .bf/.ef describe lexical/function debug ranges; their
     * storage-class spelling and inherited type do not define a function. */
    if (record->native_storage_class == 100U || record->native_storage_class == 101U)
        return XX_SYMBOL_KIND_UNKNOWN;
    if (record->native_storage_class == 6U || record->native_storage_class == 7U)
        return XX_SYMBOL_KIND_LABEL;
    if ((record->native_type & UINT32_C(0x30)) == UINT32_C(0x20))
        return XX_SYMBOL_KIND_FUNCTION;
    if (record->native_storage_class == 3U && record->section_number > 0 &&
        !record->value && !record->native_type && record->auxiliary_count)
        return XX_SYMBOL_KIND_SECTION;
    if (record->native_storage_class == 2U || record->native_storage_class == 3U ||
        record->native_storage_class == 5U || record->native_storage_class == 105U)
        return XX_SYMBOL_KIND_DATA;
    return XX_SYMBOL_KIND_UNKNOWN;
}

static bool pe_coff_resolve(pe_coff_cursor *cursor, xx_symbol_record *record,
                             xx_pd_struct *pd) {
    xx_pe *pe = (xx_pe *)cursor->format;
    const xx_pe_section *section;
    const xx_memory_map *map;
    uint64_t rva;
    if (record->section_number == 0) {
        record->is_undefined = true;
        if (record->binding == XX_SYMBOL_BINDING_GLOBAL ||
            record->binding == XX_SYMBOL_BINDING_WEAK)
            record->size = record->value; /* A nonzero undefined value is common size. */
        return true;
    }
    if (record->section_number == -1) {
        record->is_absolute = true;
        record->address = record->value;
        return true;
    }
    if (record->section_number == -2) {
        record->is_debug = true;
        return true;
    }
    if (record->section_number < 0 ||
        (uint32_t)record->section_number > pe->number_of_sections)
        return pe_coff_error(pd, "Invalid PE COFF symbol section number");
    section = &pe->sections[(uint32_t)record->section_number - 1U];
    rva = (uint64_t)section->virtual_address + record->value;
    map = xx_format_get_memory_map(cursor->format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    if (!map || rva > UINT32_MAX || map->module_address == XX_INVALID_ADDRESS ||
        rva >= XX_INVALID_ADDRESS - map->module_address)
        return pe_coff_error(pd, "Invalid PE COFF symbol address");
    record->address = map->module_address + rva;
    /* COFF supplies a section index, so physical backing belongs to that
     * section even when another section overlaps its virtual address. */
    if (record->value < section->raw_size &&
        !pe_coff_range(cursor->format,
                       (uint64_t)section->raw_offset + record->value,
                       1U, &record->offset, pd)) return false;
    return true;
}

static bool pe_coff_auxiliary(pe_coff_cursor *cursor, xx_symbol_record *record,
                               xx_pd_struct *pd) {
    uint8_t bytes[PE_COFF_SYMBOL_SIZE];
    int64_t offset = record->record_offset + PE_COFF_SYMBOL_SIZE;
    if (!record->auxiliary_count) return true;
    if (record->kind == XX_SYMBOL_KIND_FILE) {
        uint32_t size = record->auxiliary_count * PE_COFF_SYMBOL_SIZE;
        uint8_t *name = (uint8_t *)xx_mem_alloc(size);
        if (!name) return pe_coff_out_of_memory(pd, "Cannot allocate PE COFF file auxiliary data");
        if (!xx_pe_stream_read(cursor->format, offset, name, size, pd)) {
            xx_mem_free(name);
            return false;
        }
        record->file_name = pe_coff_short_name(name, size, pd);
        xx_mem_free(name);
        return record->file_name != NULL;
    }
    if (record->kind == XX_SYMBOL_KIND_SECTION ||
        (record->kind == XX_SYMBOL_KIND_FUNCTION &&
         record->native_storage_class == 2U && record->section_number > 0)) {
        if (!xx_pe_stream_read(cursor->format, offset, bytes, sizeof(bytes), pd))
            return false;
        record->size = pe_coff_u32(bytes + (record->kind == XX_SYMBOL_KIND_SECTION ? 0U : 4U));
    }
    return true;
}

static void pe_coff_cursor_free(void *pointer) {
    xx_mem_free(pointer);
}

bool xx_pe_symbol_move_to_next(Abstractformat *format, xx_symbol_state *state,
                                xx_pd_struct *pd) {
    pe_coff_cursor *cursor;
    xx_symbol_record *record;
    uint8_t bytes[PE_COFF_SYMBOL_SIZE];
    uint16_t section;
    if (!format || !state || state->format != format || !state->internal_state)
        return false;
    cursor = (pe_coff_cursor *)state->internal_state;
    record = &state->current_record;
    xx_symbol_record_cleanup(record);
    state->has_record = false;
    if (state->failed || cursor->finished) return false;
    if (pe_coff_cancelled(pd)) goto fail;
    if (cursor->next_index >= cursor->table_count) {
        cursor->finished = true;
        state->total_records = state->current_index + 1;
        return false;
    }
    record->table_index = cursor->next_index;
    record->record_offset = cursor->table_offset +
                             (int64_t)cursor->next_index * PE_COFF_SYMBOL_SIZE;
    if (!xx_pe_stream_read(format, record->record_offset, bytes, sizeof(bytes), pd))
        goto fail;
    record->value = pe_coff_u32(bytes + 8U);
    section = pe_coff_u16(bytes + 12U);
    record->section_number = section < UINT16_C(0x8000) ? (int32_t)section
                                                        : (int32_t)section - 65536;
    record->native_type = pe_coff_u16(bytes + 14U);
    record->native_storage_class = bytes[16];
    record->auxiliary_count = bytes[17];
    if (record->auxiliary_count > cursor->table_count - cursor->next_index - 1U) {
        pe_coff_error(pd, "Truncated PE COFF auxiliary records");
        goto fail;
    }
    record->name = pe_coff_u32(bytes) ? pe_coff_short_name(bytes, 8U, pd)
                                     : pe_coff_long_name(cursor, pe_coff_u32(bytes + 4U), pd);
    if (!record->name) goto fail;
    record->binding = pe_coff_binding(record->native_storage_class);
    record->kind = pe_coff_kind(record);
    if (!pe_coff_resolve(cursor, record, pd) ||
        !pe_coff_auxiliary(cursor, record, pd) || pe_coff_cancelled(pd)) goto fail;
    cursor->next_index += 1U + record->auxiliary_count;
    ++state->current_index;
    state->has_record = true;
    return true;
fail:
    xx_symbol_record_cleanup(record);
    state->failed = true;
    cursor->finished = true;
    return false;
}

xx_symbol_state *xx_pe_create_symbols_reading(Abstractformat *format,
                                              xx_pd_struct *pd) {
    xx_symbol_state *state;
    pe_coff_cursor *cursor;
    xx_pe *pe;
    uint8_t header[20];
    uint8_t strings[4];
    uint32_t pointer;
    int64_t offset;
    uint64_t string_relative;
    if (!xx_pe_stream_prepare(format, pd)) return NULL;
    state = (xx_symbol_state *)xx_mem_alloc(sizeof(*state));
    cursor = (pe_coff_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!state || !cursor) {
        xx_mem_free(state);
        xx_mem_free(cursor);
        pe_coff_out_of_memory(pd, "Cannot allocate PE COFF symbol cursor");
        return NULL;
    }
    xx_symbol_state_init(state, format);
    state->internal_state = cursor;
    state->free_internal = pe_coff_cursor_free;
    cursor->format = format;
    /* COFF pointers are file offsets. A mapped image normally omits this
     * file-only table; treating its raw pointer as an RVA invents symbols. */
    if (format->is_mapped) {
        cursor->finished = true;
        state->total_records = 0;
        return state;
    }
    pe = (xx_pe *)format;
    if (!pe_coff_range(format, (uint64_t)pe->pe_offset + 4U, sizeof(header), &offset, pd) ||
        !xx_pe_stream_read(format, offset, header, sizeof(header), pd)) goto fail;
    pointer = pe_coff_u32(header + 8U);
    cursor->table_count = pe_coff_u32(header + 12U);
    if (!cursor->table_count) {
        cursor->finished = true;
        state->total_records = 0;
        return state;
    }
    if (!pointer || cursor->table_count > PE_COFF_SYMBOL_LIMIT) {
        pe_coff_error(pd, "Invalid or oversized PE COFF symbol table");
        goto fail;
    }
    string_relative = (uint64_t)pointer +
                       (uint64_t)cursor->table_count * PE_COFF_SYMBOL_SIZE;
    if (!pe_coff_range(format, pointer,
                        (uint64_t)cursor->table_count * PE_COFF_SYMBOL_SIZE,
                        &cursor->table_offset, pd) ||
        !pe_coff_range(format, string_relative, sizeof(strings), &cursor->strings_offset, pd) ||
        !xx_pe_stream_read(format, cursor->strings_offset, strings, sizeof(strings), pd))
        goto fail;
    cursor->strings_size = pe_coff_u32(strings);
    if (cursor->strings_size < 4U ||
        !pe_coff_range(format, string_relative, cursor->strings_size, &offset, pd)) {
        pe_coff_error(pd, "Invalid or truncated PE COFF string table");
        goto fail;
    }
    if (!xx_pe_symbol_move_to_next(format, state, pd) && state->failed) goto fail;
    return state;
fail:
    xx_symbol_state_free(state);
    return NULL;
}

const xx_symbol_record *xx_pe_get_current_symbol(Abstractformat *format,
                                                 xx_symbol_state *state) {
    return state && state->format == format && state->has_record && !state->failed
               ? &state->current_record : NULL;
}

void xx_pe_free_symbols_reading(Abstractformat *format, xx_symbol_state *state) {
    (void)format;
    xx_symbol_state_free(state);
}

uint64_t xx_pe_get_number_of_symbols(Abstractformat *format, xx_pd_struct *pd) {
    xx_symbol_state *state = xx_format_create_symbols_reading(format, pd);
    uint64_t count = 0U;
    bool failed;
    if (!state) return 0U;
    while (xx_format_get_current_symbol(format, state)) {
        ++count;
        if (!xx_format_symbol_move_to_next(format, state, pd)) break;
    }
    failed = state->failed;
    xx_format_free_symbols_reading(format, state);
    if (failed) return 0U;
    format->number_of_symbols = count;
    return count;
}
