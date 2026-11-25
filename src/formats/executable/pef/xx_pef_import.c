/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_pef_stream_internal.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>

#define PEF_LOADER_HEADER_SIZE 56U
#define PEF_LOADER_LIBRARY_SIZE 24U

typedef struct pef_import_stream {
    uint8_t *loader;
    uint32_t loader_size;
    uint32_t strings_offset;
    uint32_t library_count;
    uint32_t import_count;
    uint32_t next_import;
    uint32_t library_index;
    uint32_t library_first;
    uint32_t library_end;
} pef_import_stream;

static bool pef_error(xx_pd_struct *pd, const char *message)
{
    xx_pd_set_error(pd, XXFC_ERR_IO, message);
    return false;
}

static int64_t pm_available(Abstractformat *f)
{
    int64_t n;
    if (!f || !f->device || f->base_address < 0 || (n = xx_io_size(f->device)) < f->base_address) return -1;
    return n - f->base_address;
}

static XXFC_MAYBE_UNUSED bool pm_read(Abstractformat *f, int64_t at, void *p, size_t n)
{
    int64_t left = pm_available(f);
    size_t capacity = xx_get_file_buffer_size(), done = 0;
    if (at < 0 || left < at || n > (uint64_t)(left - at) || (!p && n) || xx_io_seek64(f->device, f->base_address + at, SEEK_SET) != 0) return false;
    while (done < n) {
        size_t request = n - done < capacity ? n - done : capacity;
        ssize_t got = xx_io_read(f->device, (uint8_t *)p + done, request);
        if (got <= 0 || (size_t)got > request) return false;
        done += (size_t)got;
    }
    return true;
}

static char *xx_pef_stream_string_dup(const uint8_t *loader, uint32_t loader_size, uint32_t strings_offset, uint32_t name_offset)
{
    uint64_t start;
    size_t length;
    char *result;
    if (!pef_string_span(loader, loader_size, strings_offset, name_offset, &length)) return NULL;
    start = (uint64_t)strings_offset + name_offset;
    result = xx_str_create_len(length);
    if (result) xx_rt_memcpy(result, loader + (size_t)start, length);
    return result;
}

static uint8_t *xx_pef_stream_read_loader(xx_pef *reader, xx_pd_struct *pd)
{
    uint8_t *loader;
    int64_t saved;
    bool read_ok;
    if (!reader || (!reader->format.base_info_handled && !xx_pef_handle_base_info(&reader->format, pd)) || !reader->format.is_valid ||
        reader->loader_size < PEF_LOADER_HEADER_SIZE)
        return NULL;
    saved = xx_io_tell(reader->format.device);
    if (saved < 0) {
        pef_error(pd, "PEF loader requires a seekable device");
        return NULL;
    }
    loader = (uint8_t *)xx_mem_alloc(reader->loader_size);
    if (!loader) {
        pef_error(pd, "Could not allocate PEF loader metadata");
        return NULL;
    }
    read_ok = pm_read(&reader->format, reader->loader_offset, loader, reader->loader_size);
    if (xx_io_seek64(reader->format.device, saved, SEEK_SET) != 0) read_ok = false;
    if (!read_ok) {
        xx_mem_free(loader);
        pef_error(pd, "Could not read bounded PEF loader metadata");
        return NULL;
    }
    return loader;
}

uint64_t xx_pef_get_number_of_imports(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_pef_handle_base_info(self, pd)) ? self->number_of_imports : 0U;
}

uint32_t xx_pef_get_number_of_imported_libraries(xx_pef *reader, xx_pd_struct *pd)
{
    return reader && (reader->format.base_info_handled || xx_pef_handle_base_info(&reader->format, pd)) ? reader->imported_library_count : 0U;
}

char *xx_pef_get_imported_library_name(xx_pef *reader, uint32_t index, xx_pd_struct *pd)
{
    uint8_t *loader;
    uint32_t name_offset;
    char *result;
    if (!reader || index >= xx_pef_get_number_of_imported_libraries(reader, pd)) return NULL;
    loader = xx_pef_stream_read_loader(reader, pd);
    if (!loader) return NULL;
    name_offset = xx_data_get_u32(loader + PEF_LOADER_HEADER_SIZE + (size_t)index * PEF_LOADER_LIBRARY_SIZE, PEF_LOADER_LIBRARY_SIZE, 0, true);
    result = xx_pef_stream_string_dup(loader, reader->loader_size, reader->loader_strings_offset, name_offset);
    xx_mem_free(loader);
    if (!result) pef_error(pd, "Invalid PEF imported-library name");
    return result;
}

static bool pef_import_span(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static void pef_import_free_internal(void *opaque)
{
    pef_import_stream *stream = (pef_import_stream *)opaque;
    if (!stream) return;
    xx_mem_free(stream->loader);
    xx_mem_free(stream);
}

static bool pef_import_select_library(pef_import_stream *stream, uint32_t import_index)
{
    while (stream->library_index < stream->library_count) {
        const uint8_t *library = stream->loader + PEF_LOADER_HEADER_SIZE + (size_t)stream->library_index * PEF_LOADER_LIBRARY_SIZE;
        uint32_t count = xx_data_get_u32(library, PEF_LOADER_LIBRARY_SIZE, 12, true);
        uint32_t first = xx_data_get_u32(library, PEF_LOADER_LIBRARY_SIZE, 16, true);
        stream->library_first = first;
        stream->library_end = first + count;
        if (import_index < stream->library_end) return import_index >= stream->library_first;
        ++stream->library_index;
    }
    return false;
}

bool xx_pef_import_move_to_next(Abstractformat *self, xx_import_state *state, xx_pd_struct *pd)
{
    pef_import_stream *stream;
    uint64_t symbol_table;
    uint32_t word, name_offset, library_name_offset;
    const uint8_t *library;
    if (!self || !state || state->format != self || !state->internal_state) return false;
    stream = (pef_import_stream *)state->internal_state;
    xx_import_record_cleanup(&state->current_record);
    state->has_record = false;
    if (stream->next_import >= stream->import_count) {
        state->total_records = stream->import_count;
        return false;
    }
    if (xx_pd_is_stopped(pd) || !pef_import_select_library(stream, stream->next_import)) goto failed;
    symbol_table = PEF_LOADER_HEADER_SIZE + (uint64_t)stream->library_count * PEF_LOADER_LIBRARY_SIZE;
    if (!pef_import_span(symbol_table + (uint64_t)stream->next_import * 4U, 4U, stream->loader_size)) goto failed;
    word = xx_data_get_u32(stream->loader + (size_t)(symbol_table + (uint64_t)stream->next_import * 4U), 4, 0, true);
    name_offset = word & UINT32_C(0x00ffffff);
    library = stream->loader + PEF_LOADER_HEADER_SIZE + (size_t)stream->library_index * PEF_LOADER_LIBRARY_SIZE;
    library_name_offset = xx_data_get_u32(library, PEF_LOADER_LIBRARY_SIZE, 0, true);
    state->current_record.library_name = xx_pef_stream_string_dup(stream->loader, stream->loader_size, stream->strings_offset, library_name_offset);
    state->current_record.name = xx_pef_stream_string_dup(stream->loader, stream->loader_size, stream->strings_offset, name_offset);
    if (!state->current_record.library_name || !state->current_record.name) goto failed;
    /* PEF imports are unresolved loader symbols. Radare presents their
     * public virtual address as zero rather than the generic unavailable
     * sentinel installed by xx_import_record_cleanup(). */
    state->current_record.address = 0U;
    state->current_index = stream->next_import;
    ++stream->next_import;
    state->has_record = true;
    return true;
failed:
    xx_import_record_cleanup(&state->current_record);
    state->failed = true;
    pef_error(pd, xx_pd_is_stopped(pd) ? "PEF import stream cancelled" : "Malformed PEF import stream");
    return false;
}

xx_import_state *xx_pef_create_imports_reading(Abstractformat *self, xx_pd_struct *pd)
{
    xx_pef *reader = (xx_pef *)self;
    xx_import_state *state;
    pef_import_stream *stream;
    if (!reader || (!self->base_info_handled && !xx_pef_handle_base_info(self, pd))) return NULL;
    state = (xx_import_state *)xx_mem_calloc(1U, sizeof(*state));
    stream = (pef_import_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        xx_mem_free(state);
        xx_mem_free(stream);
        pef_error(pd, "Could not allocate PEF import stream");
        return NULL;
    }
    xx_import_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = pef_import_free_internal;
    state->total_records = reader->imported_symbol_count;
    stream->loader = xx_pef_stream_read_loader(reader, pd);
    if (!stream->loader) {
        xx_import_state_free(state);
        return NULL;
    }
    stream->loader_size = reader->loader_size;
    stream->strings_offset = reader->loader_strings_offset;
    stream->library_count = reader->imported_library_count;
    stream->import_count = reader->imported_symbol_count;
    (void)xx_pef_import_move_to_next(self, state, pd);
    if (state->failed) {
        xx_import_state_free(state);
        return NULL;
    }
    return state;
}

const xx_import_record *xx_pef_get_current_import(Abstractformat *self, xx_import_state *state)
{
    return self && state && state->format == self && state->has_record && !state->failed ? &state->current_record : NULL;
}

void xx_pef_free_imports_reading(Abstractformat *self, xx_import_state *state)
{
    (void)self;
    xx_import_state_free(state);
}

const StreamImport *xx_pef_get_stream_import(void)
{
    static const StreamImport stream = {.get_number_of_imports = xx_pef_get_number_of_imports,
                                        .create_imports_reading = xx_pef_create_imports_reading,
                                        .get_current_import = xx_pef_get_current_import,
                                        .import_move_to_next = xx_pef_import_move_to_next,
                                        .free_imports_reading = xx_pef_free_imports_reading};
    return &stream;
}
