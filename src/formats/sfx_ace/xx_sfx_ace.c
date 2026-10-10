/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded PE carrier parser; embedded ACE bytes are never executed.
 */
#include "xxfclib/formats/sfx_ace/xx_sfx_ace.h"
#include "../common/xx_executable_carrier.h"
#include "../common/xx_pe_resource_locator.h"
#include "xxfclib/formats/ace/xx_ace.h"
#include "xxfclib/io/xx_io.h"

typedef struct sfx_ace_inner_s {
    uint8_t *image;
    xx_io_device *device;
    xx_ace *ace;
    bool resource_member;
} sfx_ace_inner;

static void sfx_inner_free(sfx_ace_inner *inner) {
    if (!inner) return;
    if (inner->ace) xx_ace_free(inner->ace);
    if (inner->device) xx_io_close(inner->device);
    xx_mem_free(inner->image);
    xx_mem_free(inner);
}

static sfx_ace_inner *sfx_inner_open(Abstractformat *format, int64_t absolute,
                                     int64_t size, xx_pd_struct *pd) {
    sfx_ace_inner *inner;
    if (!format || size <= 0 || (uint64_t)size > SIZE_MAX ||
        absolute < format->base_address ||
        !carrier_range(pm_available(format), absolute - format->base_address,
                  (uint64_t)size)) return NULL;
    inner = (sfx_ace_inner *)xx_mem_calloc(1, sizeof(*inner));
    if (!inner) return NULL;
    inner->image = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!inner->image ||
        !pm_read(format, absolute - format->base_address, inner->image,
                 (size_t)size)) goto fail;
    inner->device = xx_io_mem_open_ro(inner->image, (size_t)size);
    if (!inner->device) goto fail;
    inner->ace = xx_ace_create(inner->device, 0);
    if (!inner->ace ||
        !xx_ace_handle_base_info(&inner->ace->format, pd)) goto fail;
    /* The reference reader places PE resource archive members beneath resource ID 1. The
     * resource tree must actually reference this byte range; arbitrary bytes
     * inside .rsrc do not receive that prefix. */
    inner->resource_member = size <= UINT32_MAX &&
        xx_sfx_pe_resource_contains(format,
                                    absolute - format->base_address,
                                    (uint32_t)size);
    return inner;
fail:
    sfx_inner_free(inner);
    return NULL;
}

static bool executable_carrier_at_parse(Abstractformat *format, pm_stream *stream,
                        int64_t at, xx_pd_struct *pd) {
    uint8_t header[65539];
    int64_t cursor = at, limit = pm_available(format);
    unsigned blocks = 0;
    uint64_t header_bytes = 0;
    sfx_ace_inner *inner;
    while (limit - cursor >= 7) {
        unsigned size, width;
        uint16_t flags;
        uint64_t packed = 0;
        uint32_t crc;
        if (carrier_stop(pd) || !pm_read(format, cursor, header, 7)) return false;
        size = xx_data_get_u16(header + 2, 2, 0, false);
        if (size < 3 || !carrier_range(limit, cursor, (uint64_t)size + 4U) ||
            blocks >= 4096U || header_bytes + size > 4194304U) break;
        if (!pm_read(format, cursor, header, size + 4U)) return false;
        crc = executable_carrier_crc(header + 4, size) ^ UINT32_MAX;
        if ((crc & 65535U) != xx_data_get_u16(header, 2, 0, false)) break;
        flags = xx_data_get_u16(header + 5, 2, 0, false);
        if (flags & 1U) {
            width = (flags & 4U) ? 8U : 4U;
            if (size < 3U + width) break;
            packed = width == 8U ? xx_data_get_u64(header + 7, 8, 0, false)
                                 : xx_data_get_u32(header + 7, 4, 0, false);
        }
        if (!carrier_range(limit, cursor + (int64_t)size + 4, packed)) break;
        cursor += (int64_t)size + 4 + (int64_t)packed;
        header_bytes += size;
        ++blocks;
    }
    if (carrier_stop(pd) || blocks < 2U || cursor <= at) return false;
    inner = sfx_inner_open(format, format->base_address + at,
                           cursor - at, pd);
    if (!inner) return false;
    if (!inner->ace->number_of_records ||
        inner->ace->number_of_records > 4096U) {
        sfx_inner_free(inner);
        return false;
    }
    sfx_inner_free(inner);
    return executable_carrier_component(format, stream, at, cursor - at, "payload.ace");
}

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    static const uint8_t signature[] = {'*', '*', 'A', 'C', 'E', '*', '*'};
    return executable_carrier_scan(format, stream, signature, sizeof(signature), -7,
                   true, false, executable_carrier_at_parse, pd) && carrier_members(stream, pd);
}

static bool sfx_prefix_record(xx_archive_record_state *state) {
    const char *old_name;
    char *name;
    bool ok;
    if (!state || !state->has_record) return true;
    old_name = xx_archive_record_get_original_name(&state->current_record);
    if (!old_name) return false;
    name = xx_str_concat("1/", old_name);
    if (!name) return false;
    ok = xx_archive_record_set_original_name(&state->current_record, name);
    xx_str_free(name);
    return ok;
}

static bool sfx_prefix_output_path(xx_archive_record_state *state) {
    size_t i;
    for (i = 0; state && i < state->options.count; ++i) {
        xx_meta *meta = (xx_meta *)xx_list_at(&state->options, i);
        const char *base;
        char *path;
        bool ok;
        if (!meta || meta->meta_id != XX_META_ID_OPT_UNPACK_PATH) continue;
        if (meta->var.type != XX_VAR_TYPE_STRING &&
            meta->var.type != XX_VAR_TYPE_STRING_VIEW) return false;
        base = xx_var_get_str(&meta->var);
        if (!base) return false;
        path = xx_str_concat(base, "/1");
        if (!path) return false;
        ok = xx_var_set_str(&meta->var, path);
        xx_str_free(path);
        return ok;
    }
    return true;
}

static sfx_ace_inner *sfx_inner(Abstractformat *format) {
    return format ? (sfx_ace_inner *)((xx_sfx_ace *)format)->internal : NULL;
}

static int64_t sfx_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_ace_handle_base_info(format, pd))
        ? format->format_size : -1;
}

static uint64_t sfx_count(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_ace_handle_base_info(format, pd))
        ? format->number_of_archive_records : 0U;
}

static xx_archive_record_state *sfx_create(Abstractformat *format,
                                            const xx_list_s *options,
                                            xx_pd_struct *pd) {
    sfx_ace_inner *inner;
    xx_archive_record_state *state;
    if (!format || (!format->base_info_handled &&
                    !xx_sfx_ace_handle_base_info(format, pd))) return NULL;
    inner = sfx_inner(format);
    if (!inner) return NULL;
    state = xx_ace_create_archive_records_reading(&inner->ace->format,
                                                  options, pd);
    if (!state) return NULL;
    if (inner->resource_member &&
        (!sfx_prefix_output_path(state) || !sfx_prefix_record(state))) {
        xx_ace_free_archive_records_reading(&inner->ace->format, state);
        return NULL;
    }
    return state;
}

static const xx_archive_record *sfx_current(Abstractformat *format,
                                             xx_archive_record_state *state) {
    sfx_ace_inner *inner = sfx_inner(format);
    return inner ? xx_ace_get_current_archive_record(&inner->ace->format,
                                                      state) : NULL;
}

static bool sfx_unpack(Abstractformat *format, xx_archive_record_state *state,
                       xx_pd_struct *pd) {
    sfx_ace_inner *inner = sfx_inner(format);
    return inner && xx_ace_unpack_current_archive_record(&inner->ace->format,
                                                          state, pd);
}

static bool sfx_next(Abstractformat *format, xx_archive_record_state *state,
                     xx_pd_struct *pd) {
    sfx_ace_inner *inner = sfx_inner(format);
    if (!inner || !xx_ace_archive_record_move_to_next(&inner->ace->format,
                                                      state, pd)) return false;
    return !inner->resource_member || sfx_prefix_record(state);
}

static void sfx_free_records(Abstractformat *format,
                             xx_archive_record_state *state) {
    sfx_ace_inner *inner = sfx_inner(format);
    xx_ace_free_archive_records_reading(inner ? &inner->ace->format : NULL,
                                        state);
}

static void sfx_vdestroy(Abstractformat *format) {
    xx_sfx_ace_destroy((xx_sfx_ace *)format);
}

void xx_sfx_ace_init(xx_sfx_ace *reader, xx_io_device *device, int64_t base) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base, XX_FILE_TYPE_SFX_ACE, "exe");
    reader->format.check_is_valid = xx_sfx_ace_check_is_valid;
    reader->format.handle_base_info = xx_sfx_ace_handle_base_info;
    reader->format.get_format_size = sfx_size;
    reader->format.get_number_of_archive_records = sfx_count;
    reader->format.create_archive_records_reading = sfx_create;
    reader->format.get_current_archive_record = sfx_current;
    reader->format.unpack_current_archive_record = sfx_unpack;
    reader->format.archive_record_move_to_next = sfx_next;
    reader->format.free_archive_records_reading = sfx_free_records;
    reader->format.destroy = sfx_vdestroy;
}

xx_sfx_ace *xx_sfx_ace_create(xx_io_device *device, int64_t base) {
    xx_sfx_ace *reader = (xx_sfx_ace *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_sfx_ace_init(reader, device, base);
    return reader;
}

void xx_sfx_ace_destroy(xx_sfx_ace *reader) {
    if (!reader) return;
    sfx_inner_free((sfx_ace_inner *)reader->internal);
    reader->internal = NULL;
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_sfx_ace_free(xx_sfx_ace *reader) {
    if (!reader) return;
    xx_sfx_ace_destroy(reader);
    xx_mem_free(reader);
}

bool xx_sfx_ace_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return pm_valid(format, pd);
}

bool xx_sfx_ace_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_sfx_ace *reader = (xx_sfx_ace *)format;
    pm_stream *stream;
    sfx_ace_inner *inner;
    pm_member *member;
    int64_t end;
    if (!reader || !format->device) return false;
    if (format->base_info_handled) return true;
    stream = pm_open(format, pd);
    if (!stream) goto fail;
    if (stream->count != 1U) {
        pm_free_stream(stream);
        goto fail;
    }
    member = &stream->items[0];
    inner = sfx_inner_open(format, member->offset, member->size, pd);
    if (!inner) {
        pm_free_stream(stream);
        goto fail;
    }
    sfx_inner_free((sfx_ace_inner *)reader->internal);
    reader->internal = inner;
    format->format_size = stream->size;
    format->number_of_archive_records = inner->ace->number_of_records;
    end = format->base_address + stream->size;
    format->overlay_size = xx_io_size(format->device) - end;
    format->overlay_offset = format->overlay_size > 0 ? end : -1;
    format->is_valid = true;
    format->base_info_handled = true;
    pm_free_stream(stream);
    return true;
fail:
    format->is_valid = false;
    format->base_info_handled = false;
    return false;
}
