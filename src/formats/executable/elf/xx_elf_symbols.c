/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/elf/xx_elf.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>

#define ELF_SYMBOL32_SIZE UINT64_C(16)
#define ELF_SYMBOL64_SIZE UINT64_C(24)
#define ELF_SYMBOL_LIMIT UINT64_C(1048576)
#define ELF_SYMBOL_NAME_LIMIT UINT64_C(65536)
#define ELF_SYMBOL_NAMES_TOTAL_LIMIT UINT64_C(67108864)
#define ELF_SHN_UNDEF UINT32_C(0)
#define ELF_SHN_ABS UINT32_C(0xfff1)
#define ELF_SHN_COMMON UINT32_C(0xfff2)
#define ELF_SHN_XINDEX UINT32_C(0xffff)

typedef struct elf_symbol_cursor {
    xx_elf *elf;
    uint64_t section_index;
    uint64_t symbol_index;
    uint64_t total_count;
    uint64_t name_bytes;
    uint64_t preferred_address;
    bool finished;
} elf_symbol_cursor;

static bool elf_symbol_error(xx_pd_struct *pd, int code, const char *message)
{
    xx_pd_set_error(pd, code, message);
    return false;
}

static bool elf_symbol_cancelled(xx_pd_struct *pd)
{
    if (!xx_pd_is_stopped(pd)) return false;
    xx_pd_set_error(pd, XXFC_ERR_GENERIC, "ELF symbol stream cancelled");
    return true;
}

static uint16_t elf_symbol_u16(const uint8_t *bytes, bool big_endian)
{
    if (big_endian) return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t elf_symbol_u32(const uint8_t *bytes, bool big_endian)
{
    if (big_endian) return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) | ((uint32_t)bytes[2] << 8U) | bytes[3];
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint64_t elf_symbol_u64(const uint8_t *bytes, bool big_endian)
{
    uint64_t result = 0U;
    unsigned index;
    if (big_endian) {
        for (index = 0U; index < 8U; ++index) result = (result << 8U) | bytes[index];
    } else {
        for (index = 0U; index < 8U; ++index) result |= (uint64_t)bytes[index] << (index * 8U);
    }
    return result;
}

static bool elf_symbol_absolute(Abstractformat *format, uint64_t relative, int64_t *absolute, xx_pd_struct *pd)
{
    int64_t total;
    if (!format || !format->device || format->base_address < 0 || relative > (uint64_t)(INT64_MAX - format->base_address))
        return elf_symbol_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF symbol offset outside input");
    total = xx_io_total_size(format->device);
    if (total < format->base_address || relative > (uint64_t)(total - format->base_address))
        return elf_symbol_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF symbol offset outside input");
    *absolute = format->base_address + (int64_t)relative;
    return true;
}

static bool elf_symbol_read(Abstractformat *format, uint64_t relative, void *buffer, size_t size, xx_pd_struct *pd)
{
    int64_t absolute;
    int64_t total;
    if (!elf_symbol_absolute(format, relative, &absolute, pd)) return false;
    total = xx_io_total_size(format->device);
    if ((uint64_t)size > (uint64_t)(total - absolute) || !xx_io_read_at(format->device, absolute, buffer, size))
        return elf_symbol_error(pd, XXFC_ERR_IO, "Cannot read ELF symbol data");
    return true;
}

static bool elf_symbol_is_table(const xx_elf_section_header *section)
{
    return section && (section->type == XX_ELF_SECTION_SYMTAB || section->type == XX_ELF_SECTION_DYNSYM);
}

static bool elf_symbol_table_layout(const xx_elf *elf, uint64_t section_index, uint64_t *count, xx_pd_struct *pd)
{
    const xx_elf_section_header *table;
    const xx_elf_section_header *strings;
    uint64_t canonical;
    if (!elf || section_index >= elf->section_header_count || !count) return false;
    table = &elf->section_headers[section_index];
    if (!elf_symbol_is_table(table)) return false;
    canonical = elf->elf_class == XX_ELF_CLASS_64 ? ELF_SYMBOL64_SIZE : ELF_SYMBOL32_SIZE;
    if (table->entry_size < canonical || table->size % table->entry_size != 0U || table->link >= elf->section_header_count)
        return elf_symbol_error(pd, XXFC_ERR_INVALID_ARG, "Invalid ELF symbol-table layout");
    strings = &elf->section_headers[table->link];
    if (strings->type != XX_ELF_SECTION_STRTAB || strings->type == XX_ELF_SECTION_NOBITS)
        return elf_symbol_error(pd, XXFC_ERR_INVALID_ARG, "Invalid ELF symbol string table");
    *count = table->size / table->entry_size;
    return true;
}

static char *elf_symbol_name(elf_symbol_cursor *cursor, const xx_elf_section_header *strings, uint32_t index, xx_pd_struct *pd)
{
    uint64_t remaining;
    uint64_t used = 0U;
    uint64_t maximum;
    char *result = NULL;
    if (index >= strings->size) {
        elf_symbol_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Invalid ELF symbol name offset");
        return NULL;
    }
    remaining = strings->size - index;
    maximum = remaining;
    if (maximum > ELF_SYMBOL_NAME_LIMIT + 1U) maximum = ELF_SYMBOL_NAME_LIMIT + 1U;
    while (used < maximum && !elf_symbol_cancelled(pd)) {
        uint8_t bytes[256];
        size_t amount = (size_t)(maximum - used);
        size_t length;
        char *replacement;
        if (amount > sizeof(bytes)) amount = sizeof(bytes);
        if (!elf_symbol_read(&cursor->elf->format, strings->offset + index + used, bytes, amount, pd)) {
            xx_mem_free(result);
            return NULL;
        }
        for (length = 0U; length < amount && bytes[length] != 0U; ++length) {
        }
        if ((uint64_t)length > ELF_SYMBOL_NAMES_TOTAL_LIMIT - cursor->name_bytes || used + (uint64_t)length > ELF_SYMBOL_NAMES_TOTAL_LIMIT - cursor->name_bytes) {
            elf_symbol_error(pd, XXFC_ERR_OUT_OF_MEMORY, "ELF symbol names exceed bounded budget");
            xx_mem_free(result);
            return NULL;
        }
        replacement = (char *)xx_mem_realloc(result, (size_t)(used + length + 1U));
        if (!replacement) {
            elf_symbol_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate ELF symbol name");
            xx_mem_free(result);
            return NULL;
        }
        result = replacement;
        xx_mem_copy(result + used, bytes, length);
        used += length;
        result[used] = '\0';
        if (length < amount) {
            cursor->name_bytes += used;
            return result;
        }
    }
    xx_mem_free(result);
    if (!elf_symbol_cancelled(pd)) elf_symbol_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Unterminated or oversized ELF symbol name");
    return NULL;
}

static uint32_t elf_symbol_extended_section(elf_symbol_cursor *cursor, uint64_t table_index, uint64_t symbol_index, xx_pd_struct *pd)
{
    uint64_t index;
    for (index = 0U; index < cursor->elf->section_header_count; ++index) {
        const xx_elf_section_header *section = &cursor->elf->section_headers[index];
        uint8_t bytes[4];
        uint64_t stride;
        if (section->type != XX_ELF_SECTION_SYMTAB_SHNDX || section->link != table_index) continue;
        if (section->entry_size != 0U && section->entry_size < 4U) break;
        stride = section->entry_size ? section->entry_size : 4U;
        if (section->size % stride != 0U || symbol_index > UINT64_MAX / stride || symbol_index * stride > section->size || section->size - symbol_index * stride < 4U)
            break;
        if (!elf_symbol_read(&cursor->elf->format, section->offset + symbol_index * stride, bytes, sizeof(bytes), pd)) return UINT32_MAX;
        return elf_symbol_u32(bytes, cursor->elf->data_encoding == XX_ELF_DATA_MSB);
    }
    elf_symbol_error(pd, XXFC_ERR_INVALID_ARG, "Missing ELF extended symbol section index");
    return UINT32_MAX;
}

static xx_symbol_kind_t elf_symbol_kind(uint8_t type)
{
    switch (type) {
        case 1U:
        case 5U:
        case 6U: return XX_SYMBOL_KIND_DATA; /* OBJECT, COMMON, TLS */
        case 2U:
        case 10U: return XX_SYMBOL_KIND_FUNCTION; /* FUNC, GNU_IFUNC */
        case 3U: return XX_SYMBOL_KIND_SECTION;
        case 4U: return XX_SYMBOL_KIND_FILE;
        default: return XX_SYMBOL_KIND_UNKNOWN;
    }
}

static xx_symbol_binding_t elf_symbol_binding(uint8_t binding)
{
    switch (binding) {
        case 0U: return XX_SYMBOL_BINDING_LOCAL;
        case 1U:
        case 10U: return XX_SYMBOL_BINDING_GLOBAL; /* GLOBAL, GNU_UNIQUE */
        case 2U: return XX_SYMBOL_BINDING_WEAK;
        default: return XX_SYMBOL_BINDING_UNKNOWN;
    }
}

static bool elf_symbol_target(elf_symbol_cursor *cursor, xx_symbol_record *record, uint32_t section_index, xx_pd_struct *pd)
{
    Abstractformat *format = &cursor->elf->format;
    uint64_t module = format->module_address != XX_INVALID_ADDRESS ? format->module_address : 0U;
    uint64_t relative;
    int64_t absolute;
    if (section_index == ELF_SHN_UNDEF || section_index == ELF_SHN_COMMON) {
        record->is_undefined = section_index == ELF_SHN_UNDEF;
        return true;
    }
    if (section_index == ELF_SHN_ABS) {
        record->is_absolute = true;
        if (record->value <= INT64_MAX) record->offset = (int64_t)record->value;
        if (cursor->elf->type == XX_ELF_TYPE_REL) {
            if (record->value < XX_INVALID_ADDRESS - module) record->address = module + record->value;
        } else {
            record->address = record->value;
        }
        return true;
    }
    if (section_index >= cursor->elf->section_header_count) return true; /* reserved processor/OS index */
    {
        const xx_elf_section_header *section = &cursor->elf->section_headers[section_index];
        uint64_t target;
        if (cursor->elf->type == XX_ELF_TYPE_REL) {
            if (record->value > section->size || record->size > section->size - record->value || section->offset > UINT64_MAX - record->value) return true;
            relative = section->offset + record->value;
            if (relative < XX_INVALID_ADDRESS - module) record->address = module + relative;
        } else {
            uint64_t delta;
            if (record->value < section->address) return true;
            delta = record->value - section->address;
            if (delta > section->size || record->size > section->size - delta || section->offset > UINT64_MAX - delta) return true;
            relative = section->offset + delta;
            target = record->value;
            if (format->module_address != XX_INVALID_ADDRESS && record->value >= cursor->preferred_address) {
                delta = record->value - cursor->preferred_address;
                if (delta < XX_INVALID_ADDRESS - module) target = module + delta;
                else target = XX_INVALID_ADDRESS;
            }
            record->address = target;
        }
        if (section->type != XX_ELF_SECTION_NOBITS && elf_symbol_absolute(format, relative, &absolute, pd)) record->offset = absolute;
        else if (section->type != XX_ELF_SECTION_NOBITS) return false;
    }
    return true;
}

static void elf_symbol_cursor_free(void *pointer)
{
    xx_mem_free(pointer);
}

bool xx_elf_symbol_move_to_next(Abstractformat *format, xx_symbol_state *state, xx_pd_struct *pd)
{
    elf_symbol_cursor *cursor;
    xx_symbol_record *record;
    const xx_elf_section_header *table = NULL;
    const xx_elf_section_header *strings;
    uint64_t table_count = 0U;
    uint64_t entry_size;
    uint64_t relative;
    uint64_t current_index;
    uint8_t bytes[24];
    uint32_t name_index;
    uint32_t section_index;
    uint8_t info;
    bool big_endian;
    if (!format || !state || state->format != format || !state->internal_state) return false;
    cursor = (elf_symbol_cursor *)state->internal_state;
    record = &state->current_record;
    xx_symbol_record_cleanup(record);
    state->has_record = false;
    if (state->failed || cursor->finished) return false;
    if (elf_symbol_cancelled(pd)) goto fail;
    while (cursor->section_index < cursor->elf->section_header_count) {
        table = &cursor->elf->section_headers[cursor->section_index];
        if (elf_symbol_is_table(table)) {
            if (!elf_symbol_table_layout(cursor->elf, cursor->section_index, &table_count, pd)) goto fail;
            if (cursor->symbol_index < table_count) break;
        }
        ++cursor->section_index;
        cursor->symbol_index = 0U;
    }
    if (cursor->section_index >= cursor->elf->section_header_count) {
        cursor->finished = true;
        state->total_records = state->current_index + 1;
        return false;
    }
    current_index = cursor->symbol_index;
    entry_size = cursor->elf->elf_class == XX_ELF_CLASS_64 ? ELF_SYMBOL64_SIZE : ELF_SYMBOL32_SIZE;
    if (current_index > UINT64_MAX / table->entry_size || table->offset > UINT64_MAX - current_index * table->entry_size) {
        elf_symbol_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF symbol record offset overflow");
        goto fail;
    }
    relative = table->offset + current_index * table->entry_size;
    if (!elf_symbol_read(format, relative, bytes, (size_t)entry_size, pd) || !elf_symbol_absolute(format, relative, &record->record_offset, pd)) goto fail;
    big_endian = cursor->elf->data_encoding == XX_ELF_DATA_MSB;
    name_index = elf_symbol_u32(bytes, big_endian);
    if (cursor->elf->elf_class == XX_ELF_CLASS_64) {
        info = bytes[4];
        section_index = elf_symbol_u16(bytes + 6U, big_endian);
        record->value = elf_symbol_u64(bytes + 8U, big_endian);
        record->size = elf_symbol_u64(bytes + 16U, big_endian);
    } else {
        record->value = elf_symbol_u32(bytes + 4U, big_endian);
        record->size = elf_symbol_u32(bytes + 8U, big_endian);
        info = bytes[12];
        section_index = elf_symbol_u16(bytes + 14U, big_endian);
    }
    if (section_index == ELF_SHN_XINDEX) {
        section_index = elf_symbol_extended_section(cursor, cursor->section_index, current_index, pd);
        if (section_index == UINT32_MAX) goto fail;
    }
    if (section_index > INT32_MAX) {
        elf_symbol_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF symbol section index cannot be represented");
        goto fail;
    }
    strings = &cursor->elf->section_headers[table->link];
    record->name = elf_symbol_name(cursor, strings, name_index, pd);
    if (!record->name) goto fail;
    record->table_index = current_index;
    record->section_number = (int32_t)section_index;
    record->native_type = info & UINT8_C(0x0f);
    record->native_storage_class = info >> 4U;
    record->kind = elf_symbol_kind((uint8_t)record->native_type);
    record->binding = elf_symbol_binding((uint8_t)record->native_storage_class);
    if (!elf_symbol_target(cursor, record, section_index, pd) || elf_symbol_cancelled(pd)) goto fail;
    ++cursor->symbol_index;
    ++state->current_index;
    state->has_record = true;
    return true;
fail:
    xx_symbol_record_cleanup(record);
    state->failed = true;
    cursor->finished = true;
    return false;
}

xx_symbol_state *xx_elf_create_symbols_reading(Abstractformat *format, xx_pd_struct *pd)
{
    xx_symbol_state *state;
    elf_symbol_cursor *cursor;
    xx_elf *elf;
    uint64_t index;
    uint64_t count;
    if (!format || !format->device || !format->base_info_handled || !format->is_valid) return NULL;
    elf = (xx_elf *)format;
    state = (xx_symbol_state *)xx_mem_alloc(sizeof(*state));
    cursor = (elf_symbol_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!state || !cursor) {
        xx_mem_free(state);
        xx_mem_free(cursor);
        elf_symbol_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate ELF symbol cursor");
        return NULL;
    }
    xx_symbol_state_init(state, format);
    state->internal_state = cursor;
    state->free_internal = elf_symbol_cursor_free;
    cursor->elf = elf;
    cursor->preferred_address = UINT64_MAX;
    for (index = 0U; index < elf->section_header_count; ++index) {
        const xx_elf_section_header *section = &elf->section_headers[index];
        if (elf_symbol_cancelled(pd)) goto fail;
        if (elf_symbol_is_table(section)) {
            if (!elf_symbol_table_layout(elf, index, &count, pd)) goto fail;
            if (count > ELF_SYMBOL_LIMIT - cursor->total_count) {
                elf_symbol_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "ELF symbol count exceeds bounded limit");
                goto fail;
            }
            cursor->total_count += count;
        }
        if ((section->flags & XX_ELF_SECTION_FLAG_ALLOC) != 0U && section->size > 0U && section->address < cursor->preferred_address)
            cursor->preferred_address = section->address;
    }
    if (cursor->preferred_address == UINT64_MAX) cursor->preferred_address = 0U;
    state->total_records = (int64_t)cursor->total_count;
    if (cursor->total_count == 0U) {
        cursor->finished = true;
        return state;
    }
    if (!xx_elf_symbol_move_to_next(format, state, pd) && state->failed) goto fail;
    return state;
fail:
    xx_symbol_state_free(state);
    return NULL;
}

const xx_symbol_record *xx_elf_get_current_symbol(Abstractformat *format, xx_symbol_state *state)
{
    return state && state->format == format && state->has_record && !state->failed ? &state->current_record : NULL;
}

void xx_elf_free_symbols_reading(Abstractformat *format, xx_symbol_state *state)
{
    (void)format;
    xx_symbol_state_free(state);
}

uint64_t xx_elf_get_number_of_symbols(Abstractformat *format, xx_pd_struct *pd)
{
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

const StreamSymbols *xx_elf_get_stream_symbols(void)
{
    static const StreamSymbols stream = {.get_number_of_symbols = xx_elf_get_number_of_symbols,
                                         .create_symbols_reading = xx_elf_create_symbols_reading,
                                         .get_current_symbol = xx_elf_get_current_symbol,
                                         .symbol_move_to_next = xx_elf_symbol_move_to_next,
                                         .free_symbols_reading = xx_elf_free_symbols_reading};
    return &stream;
}
