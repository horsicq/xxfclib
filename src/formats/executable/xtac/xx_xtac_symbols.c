/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/xtac/xx_xtac.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

typedef struct xtac_symbol_cursor_s {
    xx_xtac *reader;
    uint32_t next_index;
    bool finished;
} xtac_symbol_cursor;

static bool xtac_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool xtac_fail(xx_pd_struct *pd, int code, const char *message)
{
    xx_pd_set_error(pd, code, message);
    return false;
}

#define XTAC_READY(reader, pd) ((reader) && ((reader)->format.base_info_handled || xx_xtac_handle_base_info(&(reader)->format, (pd))))

uint32_t xx_xtac_get_number_of_public_symbols(xx_xtac *reader, xx_pd_struct *pd)
{
    return XTAC_READY(reader, pd) ? reader->symbol_count : 0U;
}

const xx_xtac_symbol *xx_xtac_get_public_symbol(xx_xtac *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_xtac_get_number_of_public_symbols(reader, pd) ? &reader->symbols[index] : NULL;
}

#undef XTAC_READY

uint64_t xx_xtac_get_number_of_symbols(Abstractformat *format, xx_pd_struct *pd)
{
    xx_xtac *reader = (xx_xtac *)format;
    return reader && (format->base_info_handled || xx_xtac_handle_base_info(format, pd)) ? reader->symbol_count : 0U;
}

static void xtac_symbol_cursor_free(void *pointer)
{
    xx_mem_free(pointer);
}

bool xx_xtac_symbol_move_to_next(Abstractformat *format, xx_symbol_state *state, xx_pd_struct *pd)
{
    xtac_symbol_cursor *cursor;
    const xx_xtac_symbol *symbol;
    xx_symbol_record *record;
    if (!format || !state || state->format != format || !state->internal_state) return false;
    cursor = (xtac_symbol_cursor *)state->internal_state;
    record = &state->current_record;
    xx_symbol_record_cleanup(record);
    state->has_record = false;
    if (state->failed || cursor->finished) return false;
    if (xtac_stopped(pd)) {
        state->failed = true;
        cursor->finished = true;
        return false;
    }
    if (cursor->next_index >= cursor->reader->symbol_count) {
        cursor->finished = true;
        return false;
    }
    symbol = &cursor->reader->symbols[cursor->next_index++];
    record->name = xx_str_dup(symbol->name);
    if (!record->name) {
        state->failed = true;
        cursor->finished = true;
        xtac_fail(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot duplicate XTAC symbol name");
        return false;
    }
    record->kind = XX_SYMBOL_KIND_FUNCTION;
    record->binding = XX_SYMBOL_BINDING_UNKNOWN;
    record->value = symbol->x86_rva;
    record->address = symbol->arm64_rva;
    record->table_index = symbol->ordinal;
    record->offset = format->base_address + symbol->arm64_rva;
    ++state->current_index;
    state->has_record = true;
    return true;
}

xx_symbol_state *xx_xtac_create_symbols_reading(Abstractformat *format, xx_pd_struct *pd)
{
    xx_symbol_state *state;
    xtac_symbol_cursor *cursor;
    xx_xtac *reader = (xx_xtac *)format;
    if (!reader || (!format->base_info_handled && !xx_xtac_handle_base_info(format, pd))) return NULL;
    state = (xx_symbol_state *)xx_mem_alloc(sizeof(*state));
    cursor = (xtac_symbol_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!state || !cursor) {
        xx_mem_free(state);
        xx_mem_free(cursor);
        xtac_fail(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate XTAC symbol cursor");
        return NULL;
    }
    xx_symbol_state_init(state, format);
    state->internal_state = cursor;
    state->free_internal = xtac_symbol_cursor_free;
    state->total_records = reader->symbol_count;
    cursor->reader = reader;
    if (!reader->symbol_count) {
        cursor->finished = true;
    } else if (!xx_xtac_symbol_move_to_next(format, state, pd)) {
        xx_symbol_state_free(state);
        return NULL;
    }
    return state;
}

const xx_symbol_record *xx_xtac_get_current_symbol(Abstractformat *format, xx_symbol_state *state)
{
    return state && state->format == format && state->has_record && !state->failed ? &state->current_record : NULL;
}

void xx_xtac_free_symbols_reading(Abstractformat *format, xx_symbol_state *state)
{
    (void)format;
    xx_symbol_state_free(state);
}

const StreamSymbols *xx_xtac_get_stream_symbols(void)
{
    static const StreamSymbols stream = {.get_number_of_symbols = xx_xtac_get_number_of_symbols,
                                         .create_symbols_reading = xx_xtac_create_symbols_reading,
                                         .get_current_symbol = xx_xtac_get_current_symbol,
                                         .symbol_move_to_next = xx_xtac_symbol_move_to_next,
                                         .free_symbols_reading = xx_xtac_free_symbols_reading};
    return &stream;
}
