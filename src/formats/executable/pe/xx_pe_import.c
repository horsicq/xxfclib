/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_pe_import_export_internal.h"

#define PE_IMPORT_DESCRIPTOR_LIMIT UINT32_C(65536)
#define PE_SYMBOL_LIMIT UINT32_C(1048576)

typedef struct pe_import_stream {
    pe_symbol_input input;
    unsigned int phase;
    uint32_t descriptor_index;
    uint32_t thunk_index;
    uint32_t lookup_rva;
    uint32_t iat_rva;
    char *library_name;
    bool delay_uses_va;
    bool have_descriptor;
    bool done;
    bool failed;
} pe_import_stream;

static void pe_import_clear(xx_import_state *state)
{
    xx_mem_free(state->current_record.library_name);
    xx_mem_free(state->current_record.name);
    xx_mem_zero(&state->current_record, sizeof(state->current_record));
    state->current_record.offset = -1;
    state->current_record.address = XX_INVALID_ADDRESS;
    state->has_record = false;
}

static void pe_import_free_internal(void *opaque)
{
    pe_import_stream *stream = (pe_import_stream *)opaque;
    if (!stream) return;
    xx_mem_free(stream->library_name);
    pe_symbol_input_cleanup(&stream->input);
    xx_mem_free(stream);
}

static bool pe_import_rva(pe_import_stream *stream, uint64_t value, uint32_t *rva, xx_pd_struct *pd)
{
    if (stream->delay_uses_va) {
        uint64_t image_base = stream->input.map.is_image ? stream->input.map.module_address : stream->input.image_base;
        if (image_base == XX_INVALID_ADDRESS || value < image_base) return pe_symbol_error(pd, "Invalid legacy PE delay-import address");
        value -= image_base;
    }
    if (!value || value > UINT32_MAX) return pe_symbol_error(pd, "Invalid PE import RVA");
    *rva = (uint32_t)value;
    return true;
}

static bool pe_import_next_descriptor(pe_import_stream *stream, xx_pd_struct *pd)
{
    while (stream->phase < 2U && !xx_pd_is_stopped(pd)) {
        unsigned char bytes[32];
        unsigned int directory = stream->phase ? 13U : 1U;
        uint32_t rva = stream->input.directory_rva[directory];
        uint32_t size = stream->input.directory_size[directory];
        unsigned int descriptor_size = stream->phase ? 32U : 20U;
        uint32_t name;
        uint32_t lookup;
        uint32_t iat;
        unsigned int i;
        bool all_zero = true;
        if (!rva && !size) {
            ++stream->phase;
            stream->descriptor_index = 0U;
            continue;
        }
        if (!rva || size < descriptor_size || stream->descriptor_index >= PE_IMPORT_DESCRIPTOR_LIMIT || stream->descriptor_index >= size / descriptor_size)
            return pe_symbol_error(pd, "Unterminated or invalid PE import descriptors");
        if (!pe_symbol_read(&stream->input, (uint64_t)rva + (uint64_t)stream->descriptor_index * descriptor_size, bytes, descriptor_size, pd)) return false;
        ++stream->descriptor_index;
        for (i = 0U; i < descriptor_size; ++i)
            if (bytes[i]) {
                all_zero = false;
                break;
            }
        if (all_zero) {
            ++stream->phase;
            stream->descriptor_index = 0U;
            continue;
        }
        stream->delay_uses_va = false;
        if (stream->phase) {
            uint32_t attributes = pe_symbol_u32(bytes);
            if (attributes & ~UINT32_C(1)) return pe_symbol_error(pd, "Invalid PE delay-import attributes");
            stream->delay_uses_va = !(attributes & 1U);
            name = pe_symbol_u32(bytes + 4U);
            iat = pe_symbol_u32(bytes + 12U);
            lookup = pe_symbol_u32(bytes + 16U);
        } else {
            lookup = pe_symbol_u32(bytes);
            name = pe_symbol_u32(bytes + 12U);
            iat = pe_symbol_u32(bytes + 16U);
        }
        if (!lookup) lookup = iat;
        if (!pe_import_rva(stream, name, &name, pd) || !pe_import_rva(stream, lookup, &stream->lookup_rva, pd) || !pe_import_rva(stream, iat, &stream->iat_rva, pd))
            return false;
        xx_mem_free(stream->library_name);
        stream->library_name = pe_symbol_string(&stream->input, name, PE_SYMBOL_STRING_LIMIT, pd);
        if (!stream->library_name) return false;
        stream->thunk_index = 0U;
        stream->have_descriptor = true;
        return true;
    }
    if (xx_pd_is_stopped(pd)) return false;
    stream->done = true;
    return false;
}

bool xx_pe_import_move_to_next(Abstractformat *format, xx_import_state *state, xx_pd_struct *pd)
{
    pe_import_stream *stream;
    if (!format || !state || state->format != format || !state->internal_state) return false;
    stream = (pe_import_stream *)state->internal_state;
    pe_import_clear(state);
    if (stream->done || stream->failed) return false;
    while (!xx_pd_is_stopped(pd)) {
        unsigned char bytes[8];
        uint64_t lookup;
        uint64_t thunk_rva;
        uint64_t iat_rva;
        uint64_t ordinal_flag;
        uint32_t name_rva;
        if (!stream->have_descriptor && !pe_import_next_descriptor(stream, pd)) {
            if (stream->done) {
                state->total_records = state->current_index + 1;
                return false;
            }
            break;
        }
        if (stream->thunk_index > PE_SYMBOL_LIMIT) {
            pe_symbol_error(pd, "PE import stream exceeds the symbol limit");
            break;
        }
        thunk_rva = (uint64_t)stream->lookup_rva + (uint64_t)stream->thunk_index * stream->input.thunk_size;
        iat_rva = (uint64_t)stream->iat_rva + (uint64_t)stream->thunk_index * stream->input.thunk_size;
        if (!pe_symbol_read(&stream->input, thunk_rva, bytes, stream->input.thunk_size, pd)) break;
        lookup = stream->input.thunk_size == 8U ? pe_symbol_u64(bytes) : pe_symbol_u32(bytes);
        ++stream->thunk_index;
        if (!lookup) {
            stream->have_descriptor = false;
            continue;
        }
        if (state->current_index >= (int64_t)PE_SYMBOL_LIMIT - 1) {
            pe_symbol_error(pd, "PE import stream exceeds the symbol limit");
            break;
        }
        state->current_record.offset = pe_symbol_offset(&stream->input, iat_rva, stream->input.thunk_size);
        if (state->current_record.offset < 0) {
            pe_symbol_error(pd, "Invalid PE import address table");
            break;
        }
        state->current_record.address = xx_memory_map_relative_address_to_address(&stream->input.map, (int64_t)iat_rva);
        ordinal_flag = stream->input.thunk_size == 8U ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000);
        state->current_record.by_ordinal = (lookup & ordinal_flag) != 0U;
        state->current_record.is_delay = stream->phase != 0U;
        if (state->current_record.by_ordinal) {
            if (lookup & ~(ordinal_flag | UINT64_C(0xffff))) {
                pe_symbol_error(pd, "Invalid PE import ordinal thunk");
                break;
            }
            state->current_record.ordinal = lookup & UINT64_C(0xffff);
        } else {
            unsigned char hint[2];
            if (!pe_import_rva(stream, lookup, &name_rva, pd) || !pe_symbol_read(&stream->input, name_rva, hint, sizeof(hint), pd)) break;
            state->current_record.hint = pe_symbol_u16(hint);
            state->current_record.name = pe_symbol_string(&stream->input, (uint64_t)name_rva + 2U, PE_SYMBOL_STRING_LIMIT, pd);
            if (!state->current_record.name) break;
        }
        state->current_record.library_name = pe_symbol_duplicate(stream->library_name, pd);
        if (!state->current_record.library_name || xx_pd_is_stopped(pd)) break;
        ++state->current_index;
        state->has_record = true;
        return true;
    }
    pe_import_clear(state);
    stream->failed = true;
    state->failed = true;
    if (xx_pd_is_stopped(pd)) pe_symbol_cancelled_error(pd);
    return false;
}

xx_import_state *xx_pe_create_imports_reading(Abstractformat *format, xx_pd_struct *pd)
{
    xx_import_state *state;
    pe_import_stream *stream;
    if (!format) return NULL;
    if (xx_pd_is_stopped(pd)) {
        pe_symbol_cancelled_error(pd);
        return NULL;
    }
    state = (xx_import_state *)xx_mem_calloc(1U, sizeof(*state));
    stream = (pe_import_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        xx_mem_free(state);
        xx_mem_free(stream);
        pe_symbol_error(pd, "Could not allocate a PE import stream");
        return NULL;
    }
    xx_import_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pe_import_free_internal;
    if (!pe_symbol_input_init(&stream->input, format, pd)) {
        xx_pe_free_imports_reading(format, state);
        return NULL;
    }
    (void)xx_pe_import_move_to_next(format, state, pd);
    if (stream->failed) {
        xx_pe_free_imports_reading(format, state);
        return NULL;
    }
    return state;
}

const xx_import_record *xx_pe_get_current_import(Abstractformat *format, xx_import_state *state)
{
    return state && state->format == format && state->has_record ? &state->current_record : NULL;
}

void xx_pe_free_imports_reading(Abstractformat *format, xx_import_state *state)
{
    (void)format;
    xx_import_state_free(state);
}

uint64_t xx_pe_get_number_of_imports(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_pe_handle_base_info(format, pd)) ? format->number_of_imports : 0U;
}

const StreamImport *xx_pe_get_stream_import(void)
{
    static const StreamImport stream = {.get_number_of_imports = xx_pe_get_number_of_imports,
                                        .create_imports_reading = xx_pe_create_imports_reading,
                                        .get_current_import = xx_pe_get_current_import,
                                        .import_move_to_next = xx_pe_import_move_to_next,
                                        .free_imports_reading = xx_pe_free_imports_reading};
    return &stream;
}
