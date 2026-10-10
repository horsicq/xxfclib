/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/pe/xx_pe.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>

#define PE_SYMBOL_LIMIT UINT32_C(1048576)
#define PE_IMPORT_DESCRIPTOR_LIMIT UINT32_C(65536)
#define PE_SYMBOL_STRING_LIMIT UINT32_C(65536)
#define PE_EXPORT_NAME_WINDOW 1024U

/* The cursor owns only its mapping and current strings. It never materializes
 * an inspection object or a complete symbol list. */
typedef struct pe_symbol_input {
    xx_io_device *device;
    xx_memory_map map;
    int64_t size;
    uint64_t image_base;
    uint32_t directory_rva[16];
    uint32_t directory_size[16];
    unsigned int thunk_size;
} pe_symbol_input;

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

static bool pe_symbol_error(xx_pd_struct *pd, const char *message)
{
    xx_pd_set_error(pd, XXFC_ERR_IO, message);
    return false;
}

static bool pe_symbol_cancelled_error(xx_pd_struct *pd)
{
    xx_pd_set_error(pd, XXFC_ERR_GENERIC, "PE symbol stream cancelled");
    return false;
}

static uint16_t pe_symbol_u16(const unsigned char *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t pe_symbol_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint64_t pe_symbol_u64(const unsigned char *bytes)
{
    return (uint64_t)pe_symbol_u32(bytes) | ((uint64_t)pe_symbol_u32(bytes + 4U) << 32U);
}

static void pe_symbol_input_cleanup(pe_symbol_input *input)
{
    if (input) xx_memory_map_cleanup(&input->map);
}

static bool pe_symbol_input_init(pe_symbol_input *input, Abstractformat *format, xx_pd_struct *pd)
{
    const xx_memory_map *source;
    const xx_pe *pe;
    int64_t position;
    size_t i;
    bool success = false;
    if (!input || !format || !format->device) return false;
    if (xx_pd_is_stopped(pd)) return pe_symbol_cancelled_error(pd);
    xx_memory_map_init(&input->map);
    input->device = format->device;
    input->size = xx_io_total_size(input->device);
    position = xx_io_tell(input->device);
    if (position < 0 || input->size < 0) return pe_symbol_error(pd, "PE symbol streams require a seekable, sized device");
    if ((!format->base_info_handled && !xx_format_handle_base_info(format, pd)) || !format->is_valid) goto finished;
    source = xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    if (!source) goto finished;
    input->map = *source;
    input->map.records = NULL;
    input->map.record_count = input->map.record_capacity = 0U;
    for (i = 0U; i < source->record_count; ++i) {
        if (xx_pd_is_stopped(pd) || !xx_memory_map_add_record(&input->map, &source->records[i])) goto finished;
    }
    pe = (const xx_pe *)format;
    input->image_base = pe->image_base;
    input->thunk_size = pe->optional_magic == XX_PE_MAGIC_64 ? 8U : 4U;
    xx_mem_copy(input->directory_rva, pe->data_directory_rva, sizeof(input->directory_rva));
    xx_mem_copy(input->directory_size, pe->data_directory_size, sizeof(input->directory_size));
    success = true;
finished:
    if (xx_io_seek64(input->device, position, 0) != 0) return pe_symbol_error(pd, "Could not restore the PE input cursor");
    if (xx_pd_is_stopped(pd)) return pe_symbol_cancelled_error(pd);
    if (!success) pe_symbol_error(pd, "Could not initialize the PE symbol stream");
    return success;
}

static int64_t pe_symbol_offset(const pe_symbol_input *input, uint64_t rva, uint64_t size)
{
    uint64_t address;
    int64_t offset;
    if (!input || !size || rva > UINT32_MAX || size > UINT64_C(0x100000000) - rva || size > (uint64_t)INT64_MAX) return -1;
    address = xx_memory_map_relative_address_to_address(&input->map, (int64_t)rva);
    if (address == XX_INVALID_ADDRESS || !xx_memory_map_is_physical_address_range(&input->map, address, (int64_t)size)) return -1;
    offset = xx_memory_map_relative_address_to_offset(&input->map, (int64_t)rva);
    if (offset < 0 || offset > input->size || size > (uint64_t)(input->size - offset)) return -1;
    return offset;
}

static bool pe_symbol_read(pe_symbol_input *input, uint64_t rva, void *bytes, size_t size, xx_pd_struct *pd)
{
    int64_t offset;
    if (xx_pd_is_stopped(pd)) return false;
    offset = pe_symbol_offset(input, rva, (uint64_t)size);
    if (offset < 0 || !xx_io_read_at(input->device, offset, bytes, size)) return pe_symbol_error(pd, "Invalid or truncated PE symbol table");
    return true;
}

static char *pe_symbol_duplicate(const char *source, xx_pd_struct *pd)
{
    size_t size = 0U;
    char *result;
    while (source[size]) ++size;
    result = (char *)xx_mem_alloc(size + 1U);
    if (!result) {
        pe_symbol_error(pd, "Could not allocate a PE symbol string");
        return NULL;
    }
    xx_mem_copy(result, source, size + 1U);
    return result;
}

/* Read bounded chunks, shrinking at mapping boundaries. In particular, a NUL
 * beyond a physical section never legitimizes an unterminated string. */
static char *pe_symbol_string(pe_symbol_input *input, uint64_t rva, uint32_t maximum, xx_pd_struct *pd)
{
    char *result = NULL;
    size_t used = 0U;
    size_t capacity = 0U;
    if (maximum > PE_SYMBOL_STRING_LIMIT) maximum = PE_SYMBOL_STRING_LIMIT;
    while (used < maximum && !xx_pd_is_stopped(pd)) {
        unsigned char bytes[128];
        size_t amount = maximum - used;
        size_t length;
        bool terminated = false;
        char *replacement;
        if (amount > sizeof(bytes)) amount = sizeof(bytes);
        while (amount && pe_symbol_offset(input, rva + used, amount) < 0) amount /= 2U;
        if (!amount || !pe_symbol_read(input, rva + used, bytes, amount, pd)) break;
        for (length = 0U; length < amount; ++length) {
            if (!bytes[length]) {
                terminated = true;
                break;
            }
        }
        if (used + length + 1U > capacity) {
            capacity = used + length + 1U;
            replacement = (char *)xx_mem_realloc(result, capacity);
            if (!replacement) {
                xx_mem_free(result);
                pe_symbol_error(pd, "Could not allocate a PE symbol string");
                return NULL;
            }
            result = replacement;
        }
        xx_mem_copy(result + used, bytes, length);
        used += length;
        result[used] = '\0';
        if (terminated) return result;
    }
    xx_mem_free(result);
    if (!xx_pd_is_stopped(pd)) pe_symbol_error(pd, "Invalid or unterminated PE symbol string");
    return NULL;
}

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
