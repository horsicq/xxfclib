/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/omf/xx_omf.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

typedef struct omf_symbol_cursor_s {
    xx_omf *reader;
    uint32_t next_index;
    bool finished;
} omf_symbol_cursor;

static bool omf_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static bool omf_fail(xx_pd_struct *pd, int code, const char *message)
{
    xx_pd_set_error(pd, code, message);
    return false;
}

uint32_t xx_omf_get_number_of_public_symbols(xx_omf *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_omf_handle_base_info(&reader->format, pd)) ? reader->symbol_count : 0U;
}

const xx_omf_public_symbol *xx_omf_get_public_symbol(xx_omf *reader, uint32_t index, xx_pd_struct *pd)
{
    return reader && index < xx_omf_get_number_of_public_symbols(reader, pd) ? &reader->symbols[index] : NULL;
}

uint64_t xx_omf_get_number_of_symbols(Abstractformat *format, xx_pd_struct *pd)
{
    xx_omf *reader = (xx_omf *)format;
    return reader && (format->base_info_handled || xx_omf_handle_base_info(format, pd)) ? reader->symbol_count : 0U;
}

static void omf_symbol_cursor_free(void *pointer)
{
    xx_mem_free(pointer);
}

bool xx_omf_symbol_move_to_next(Abstractformat *format, xx_symbol_state *state, xx_pd_struct *pd)
{
    omf_symbol_cursor *cursor;
    const xx_omf_public_symbol *symbol;
    xx_symbol_record *record;
    if (!format || !state || state->format != format || !state->internal_state) return false;
    cursor = (omf_symbol_cursor *)state->internal_state;
    record = &state->current_record;
    xx_symbol_record_cleanup(record);
    state->has_record = false;
    if (state->failed || cursor->finished) return false;
    if (omf_stopped(pd)) {
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
        omf_fail(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot duplicate OMF symbol name");
        return false;
    }
    record->kind = XX_SYMBOL_KIND_UNKNOWN;
    record->binding = XX_SYMBOL_BINDING_GLOBAL;
    record->value = symbol->value;
    record->address = symbol->address;
    record->table_index = symbol->ordinal;
    record->offset = symbol->offset;
    record->section_number = symbol->segment_index;
    record->native_type = symbol->bits;
    ++state->current_index;
    state->has_record = true;
    return true;
}

xx_symbol_state *xx_omf_create_symbols_reading(Abstractformat *format, xx_pd_struct *pd)
{
    xx_symbol_state *state;
    omf_symbol_cursor *cursor;
    xx_omf *reader = (xx_omf *)format;
    if (!reader || (!format->base_info_handled && !xx_omf_handle_base_info(format, pd))) return NULL;
    state = (xx_symbol_state *)xx_mem_alloc(sizeof(*state));
    cursor = (omf_symbol_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!state || !cursor) {
        xx_mem_free(state);
        xx_mem_free(cursor);
        omf_fail(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate OMF symbol cursor");
        return NULL;
    }
    xx_symbol_state_init(state, format);
    state->internal_state = cursor;
    state->free_internal = omf_symbol_cursor_free;
    state->total_records = reader->symbol_count;
    cursor->reader = reader;
    if (!reader->symbol_count) {
        cursor->finished = true;
    } else if (!xx_omf_symbol_move_to_next(format, state, pd)) {
        xx_symbol_state_free(state);
        return NULL;
    }
    return state;
}

const xx_symbol_record *xx_omf_get_current_symbol(Abstractformat *format, xx_symbol_state *state)
{
    return state && state->format == format && state->has_record && !state->failed ? &state->current_record : NULL;
}

void xx_omf_free_symbols_reading(Abstractformat *format, xx_symbol_state *state)
{
    (void)format;
    xx_symbol_state_free(state);
}

const StreamSymbols *xx_omf_get_stream_symbols(void)
{
    static const StreamSymbols stream = {.get_number_of_symbols = xx_omf_get_number_of_symbols,
                                         .create_symbols_reading = xx_omf_create_symbols_reading,
                                         .get_current_symbol = xx_omf_get_current_symbol,
                                         .symbol_move_to_next = xx_omf_symbol_move_to_next,
                                         .free_symbols_reading = xx_omf_free_symbols_reading};
    return &stream;
}
