/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_pe_import_export_internal.h"

#define PE_SYMBOL_LIMIT UINT32_C(1048576)
#define PE_EXPORT_NAME_WINDOW 1024U

typedef struct pe_export_stream {
    pe_symbol_input input;
    uint32_t directory_rva;
    uint32_t directory_size;
    uint32_t ordinal_base;
    uint32_t function_count;
    uint32_t name_count;
    uint32_t address_table_rva;
    uint32_t name_table_rva;
    uint32_t ordinal_table_rva;
    uint32_t next_slot;
    uint32_t name_window_base;
    uint32_t name_indices[PE_EXPORT_NAME_WINDOW];
    bool name_window_valid;
    bool done;
    bool failed;
} pe_export_stream;

static void pe_export_clear(xx_export_state *state)
{
    xx_mem_free(state->current_record.name);
    xx_mem_free(state->current_record.forwarder);
    xx_mem_zero(&state->current_record, sizeof(state->current_record));
    state->current_record.offset = -1;
    state->current_record.address = XX_INVALID_ADDRESS;
    state->has_record = false;
}

static void pe_export_free_internal(void *opaque)
{
    pe_export_stream *stream = (pe_export_stream *)opaque;
    if (!stream) return;
    pe_symbol_input_cleanup(&stream->input);
    xx_mem_free(stream);
}

static bool pe_export_initialize(pe_export_stream *stream, xx_pd_struct *pd)
{
    unsigned char bytes[40];
    stream->directory_rva = stream->input.directory_rva[0];
    stream->directory_size = stream->input.directory_size[0];
    if (!stream->directory_rva && !stream->directory_size) {
        stream->done = true;
        return true;
    }
    if (!stream->directory_rva || stream->directory_size < sizeof(bytes) || (uint64_t)stream->directory_rva + stream->directory_size > UINT64_C(0x100000000) ||
        !pe_symbol_read(&stream->input, stream->directory_rva, bytes, sizeof(bytes), pd))
        return pe_symbol_error(pd, "Invalid PE export directory");
    stream->ordinal_base = pe_symbol_u32(bytes + 16U);
    stream->function_count = pe_symbol_u32(bytes + 20U);
    stream->name_count = pe_symbol_u32(bytes + 24U);
    stream->address_table_rva = pe_symbol_u32(bytes + 28U);
    stream->name_table_rva = pe_symbol_u32(bytes + 32U);
    stream->ordinal_table_rva = pe_symbol_u32(bytes + 36U);
    if (stream->function_count > PE_SYMBOL_LIMIT || stream->name_count > PE_SYMBOL_LIMIT ||
        (stream->function_count &&
         (!stream->address_table_rva || pe_symbol_offset(&stream->input, stream->address_table_rva, (uint64_t)stream->function_count * 4U) < 0)) ||
        (stream->name_count && (!stream->function_count || !stream->name_table_rva || !stream->ordinal_table_rva ||
                                pe_symbol_offset(&stream->input, stream->name_table_rva, (uint64_t)stream->name_count * 4U) < 0 ||
                                pe_symbol_offset(&stream->input, stream->ordinal_table_rva, (uint64_t)stream->name_count * 2U) < 0)))
        return pe_symbol_error(pd, "Invalid PE export tables");
    if (!stream->function_count) stream->done = true;
    return true;
}

/* Ordinal entries are ordered by name, not EAT slot. A fixed-size cache keeps
 * memory bounded even for a very large export directory. Aliases retain the
 * first name, and zero EAT slots do not produce exports. */
static bool pe_export_names_for_window(pe_export_stream *stream, uint32_t slot, xx_pd_struct *pd)
{
    uint32_t window = slot - slot % PE_EXPORT_NAME_WINDOW;
    uint32_t i;
    if (stream->name_window_valid && stream->name_window_base == window) return true;
    for (i = 0U; i < PE_EXPORT_NAME_WINDOW; ++i) stream->name_indices[i] = UINT32_MAX;
    for (i = 0U; i < stream->name_count;) {
        unsigned char bytes[512];
        uint32_t amount = stream->name_count - i;
        uint32_t j;
        if (amount > sizeof(bytes) / 2U) amount = sizeof(bytes) / 2U;
        if (!pe_symbol_read(&stream->input, (uint64_t)stream->ordinal_table_rva + (uint64_t)i * 2U, bytes, (size_t)amount * 2U, pd)) return false;
        for (j = 0U; j < amount; ++j) {
            uint32_t ordinal = pe_symbol_u16(bytes + j * 2U);
            if (ordinal >= stream->function_count) return pe_symbol_error(pd, "Invalid PE export name ordinal");
            if (ordinal >= window && ordinal - window < PE_EXPORT_NAME_WINDOW && stream->name_indices[ordinal - window] == UINT32_MAX)
                stream->name_indices[ordinal - window] = i + j;
        }
        i += amount;
    }
    stream->name_window_base = window;
    stream->name_window_valid = true;
    return !xx_pd_is_stopped(pd);
}

bool xx_pe_export_move_to_next(Abstractformat *format, xx_export_state *state, xx_pd_struct *pd)
{
    pe_export_stream *stream;
    if (!format || !state || state->format != format || !state->internal_state) return false;
    stream = (pe_export_stream *)state->internal_state;
    pe_export_clear(state);
    if (stream->done || stream->failed) return false;
    while (stream->next_slot < stream->function_count && !xx_pd_is_stopped(pd)) {
        unsigned char bytes[4];
        uint32_t slot = stream->next_slot++;
        uint32_t rva;
        uint32_t name_index;
        if (!pe_symbol_read(&stream->input, (uint64_t)stream->address_table_rva + (uint64_t)slot * 4U, bytes, sizeof(bytes), pd)) goto failed;
        rva = pe_symbol_u32(bytes);
        if (!rva) continue;
        if (!pe_export_names_for_window(stream, slot, pd)) goto failed;
        name_index = stream->name_indices[slot - stream->name_window_base];
        if (name_index != UINT32_MAX) {
            uint32_t name_rva;
            if (!pe_symbol_read(&stream->input, (uint64_t)stream->name_table_rva + (uint64_t)name_index * 4U, bytes, sizeof(bytes), pd)) goto failed;
            name_rva = pe_symbol_u32(bytes);
            if (!name_rva) {
                pe_symbol_error(pd, "Invalid PE export name RVA");
                goto failed;
            }
            state->current_record.name = pe_symbol_string(&stream->input, name_rva, PE_SYMBOL_STRING_LIMIT, pd);
            if (!state->current_record.name) goto failed;
        }
        state->current_record.ordinal = (uint64_t)stream->ordinal_base + slot;
        state->current_record.offset = pe_symbol_offset(&stream->input, rva, 1U);
        state->current_record.address = xx_memory_map_relative_address_to_address(&stream->input.map, rva);
        if (!xx_memory_map_is_address_valid(&stream->input.map, state->current_record.address)) {
            pe_symbol_error(pd, "Invalid PE export target RVA");
            goto failed;
        }
        if (rva >= stream->directory_rva && (uint64_t)rva < (uint64_t)stream->directory_rva + stream->directory_size) {
            uint32_t maximum = stream->directory_size - (rva - stream->directory_rva);
            state->current_record.forwarder = pe_symbol_string(&stream->input, rva, maximum, pd);
            if (!state->current_record.forwarder) goto failed;
        }
        if (xx_pd_is_stopped(pd)) goto failed;
        ++state->current_index;
        state->has_record = true;
        return true;
    }
    if (xx_pd_is_stopped(pd)) goto failed;
    pe_export_clear(state);
    stream->done = true;
    state->total_records = state->current_index + 1;
    return false;
failed:
    pe_export_clear(state);
    stream->failed = true;
    state->failed = true;
    if (xx_pd_is_stopped(pd)) pe_symbol_cancelled_error(pd);
    return false;
}

xx_export_state *xx_pe_create_exports_reading(Abstractformat *format, xx_pd_struct *pd)
{
    xx_export_state *state;
    pe_export_stream *stream;
    if (!format) return NULL;
    if (xx_pd_is_stopped(pd)) {
        pe_symbol_cancelled_error(pd);
        return NULL;
    }
    state = (xx_export_state *)xx_mem_calloc(1U, sizeof(*state));
    stream = (pe_export_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        xx_mem_free(state);
        xx_mem_free(stream);
        pe_symbol_error(pd, "Could not allocate a PE export stream");
        return NULL;
    }
    xx_export_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pe_export_free_internal;
    if (!pe_symbol_input_init(&stream->input, format, pd) || !pe_export_initialize(stream, pd)) {
        xx_pe_free_exports_reading(format, state);
        return NULL;
    }
    if (stream->done) state->total_records = 0;
    else (void)xx_pe_export_move_to_next(format, state, pd);
    if (stream->failed) {
        xx_pe_free_exports_reading(format, state);
        return NULL;
    }
    return state;
}

const xx_export_record *xx_pe_get_current_export(Abstractformat *format, xx_export_state *state)
{
    return state && state->format == format && state->has_record ? &state->current_record : NULL;
}

void xx_pe_free_exports_reading(Abstractformat *format, xx_export_state *state)
{
    (void)format;
    xx_export_state_free(state);
}

uint64_t xx_pe_get_number_of_exports(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_pe_handle_base_info(format, pd)) ? format->number_of_exports : 0U;
}

const StreamExport *xx_pe_get_stream_export(void)
{
    static const StreamExport stream = {.get_number_of_exports = xx_pe_get_number_of_exports,
                                        .create_exports_reading = xx_pe_create_exports_reading,
                                        .get_current_export = xx_pe_get_current_export,
                                        .export_move_to_next = xx_pe_export_move_to_next,
                                        .free_exports_reading = xx_pe_free_exports_reading};
    return &stream;
}
