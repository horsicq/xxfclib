/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arx/xx_arx.h"

#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_ARX_MAX_MEMBERS 100000U

typedef struct xx_arx_member_s {
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    uint32_t packed_size;
    uint32_t original_size;
    uint32_t dos_time;
    uint8_t method;
    bool stored;
    char *name;
} xx_arx_member;

typedef struct xx_arx_stream_s {
    xx_arx_member *items;
    size_t count;
    size_t index;
    int64_t end;
} xx_arx_stream;

static uint16_t xx_arx_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t xx_arx_u32(const uint8_t *p) {
    return (uint32_t)xx_arx_u16(p) |
           ((uint32_t)xx_arx_u16(p + 2U) << 16U);
}

static bool xx_arx_read(xx_io_device *device, int64_t offset, void *data,
                        size_t size) {
    size_t done = 0U;
    if (!device || offset < 0 || offset > LONG_MAX ||
        xx_io_seek(device, (long)offset, SEEK_SET)) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static void xx_arx_stream_free(void *pointer) {
    xx_arx_stream *stream = (xx_arx_stream *)pointer;
    size_t i;
    if (!stream) return;
    for (i = 0U; i < stream->count; ++i)
        if (stream->items[i].name) xx_str_free(stream->items[i].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static uint8_t xx_arx_method(const uint8_t *header) {
    if (header[2] != '-' || header[3] != 'l' || header[6] != '-') return 255U;
    if ((header[4] != 'h' && header[4] != 'z') ||
        header[5] < '0' || header[5] > '9') return 255U;
    return (uint8_t)(header[5] - '0');
}

static bool xx_arx_parse(Abstractformat *format, xx_arx_stream **result) {
    xx_arx_stream *stream;
    int64_t total;
    int64_t offset;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total <= format->base_address) return false;
    stream = (xx_arx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    offset = format->base_address;
    while (stream->count < XX_ARX_MAX_MEMBERS) {
        uint8_t lead;
        uint8_t *header;
        size_t header_size;
        size_t name_size;
        uint32_t packed;
        uint32_t original;
        uint8_t method;
        uint8_t checksum = 0U;
        size_t i;
        xx_arx_member *grown;
        xx_arx_member *member;
        if (offset >= total || !xx_arx_read(format->device, offset, &lead, 1U))
            goto fail;
        if (lead == 0U) {
            if (stream->count == 0U) goto fail;
            stream->end = offset + 1;
            *result = stream;
            return true;
        }
        header_size = (size_t)lead + 2U;
        if (header_size < 24U || header_size > (size_t)(total - offset))
            goto fail;
        header = (uint8_t *)xx_mem_alloc(header_size);
        if (!header || !xx_arx_read(format->device, offset, header,
                                    header_size)) {
            if (header) xx_mem_free(header);
            goto fail;
        }
        for (i = 2U; i < header_size; ++i) checksum += header[i];
        method = xx_arx_method(header);
        name_size = header[22];
        if (checksum != header[1] || method == 255U || header[7] != 0U ||
            23U + name_size > header_size) {
            xx_mem_free(header);
            goto fail;
        }
        packed = xx_arx_u32(header + 8U);
        original = xx_arx_u32(header + 12U);
        if (packed == 0U) packed = original;
        if ((uint64_t)packed > (uint64_t)(total - offset - (int64_t)header_size)) {
            xx_mem_free(header);
            goto fail;
        }
        grown = (xx_arx_member *)xx_mem_realloc(
            stream->items, (stream->count + 1U) * sizeof(*grown));
        if (!grown) {
            xx_mem_free(header);
            goto fail;
        }
        stream->items = grown;
        member = &stream->items[stream->count];
        xx_mem_zero(member, sizeof(*member));
        member->name = (char *)xx_mem_alloc(name_size + 1U);
        if (!member->name) {
            xx_mem_free(header);
            goto fail;
        }
        for (i = 0U; i < name_size; ++i) {
            uint8_t c = header[23U + i];
            if (c < 0x20U || c == 0x7fU) {
                xx_mem_free(header);
                goto fail;
            }
            member->name[i] = c == '\\' ? '/' : (char)c;
        }
        member->name[name_size] = '\0';
        member->header_offset = offset;
        member->header_size = (int64_t)header_size;
        member->data_offset = offset + (int64_t)header_size;
        member->packed_size = packed;
        member->original_size = original;
        member->dos_time = ((uint32_t)xx_arx_u16(header + 18U) << 16U) |
                           xx_arx_u16(header + 16U);
        member->stored = xx_arx_u32(header + 8U) == 0U || method == 0U;
        member->method = member->stored ? 0U : method;
        ++stream->count;
        xx_mem_free(header);
        offset = member->data_offset + member->packed_size;
    }
fail:
    xx_arx_stream_free(stream);
    return false;
}

static bool xx_arx_options(xx_list_s *dst, const xx_list_s *src) {
    size_t i;
    if (!src) return true;
    for (i = 0U; i < src->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)src, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(dst, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_arx_find(const xx_list_s *list, uint32_t id) {
    size_t i;
    if (!list) return NULL;
    for (i = 0U; i < list->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)list, i);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool xx_arx_safe(const char *name) {
    const char *part = name;
    const char *p;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    if (name[1] == ':') return false;
    for (p = name;; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c && c < 32U)) return false;
        if (c == '/' || c == '\\' || !c) {
            size_t n = (size_t)(p - part);
            if (!n || (n == 1U && part[0] == '.') ||
                (n == 2U && part[0] == '.' && part[1] == '.')) return false;
            if (!c) return true;
            part = p + 1;
        }
    }
}

static bool xx_arx_record(xx_archive_record *record,
                          const xx_arx_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_arx_decode(Abstractformat *format, const xx_arx_member *member,
                          uint8_t **plain, size_t *plain_size,
                          xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool ok = false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size
                                         ? member->packed_size
                                         : 1U);
    output = (uint8_t *)xx_mem_alloc(member->original_size
                                         ? member->original_size
                                         : 1U);
    if (!packed || !output ||
        (member->packed_size &&
         !xx_arx_read(format->device, member->data_offset, packed,
                      member->packed_size))) goto done;
    if (member->stored) {
        if (member->packed_size != member->original_size) goto done;
        if (member->original_size)
            xx_mem_copy(output, packed, member->original_size);
        written = member->original_size;
    } else if (member->method == 1U) {
        if (!xx_lzh1_decode_memory(packed, member->packed_size, output,
                                   member->original_size, &written)) goto done;
    } else {
        goto done;
    }
    if (written != member->original_size) goto done;
    *plain = output;
    *plain_size = written;
    output = NULL;
    ok = true;
done:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return ok;
}

void xx_arx_init(xx_arx *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.file_type = XX_FILE_TYPE_ARX;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_extension(&archive->format, "arx");
    archive->format.check_is_valid = xx_arx_check_is_valid;
    archive->format.handle_base_info = xx_arx_handle_base_info;
    archive->format.get_format_size = xx_arx_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_arx_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_arx_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_arx_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_arx_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_arx_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_arx_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_arx *xx_arx_create(xx_io_device *device, int64_t base_address) {
    xx_arx *archive = (xx_arx *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_arx_init(archive, device, base_address);
    return archive;
}

void xx_arx_destroy(xx_arx *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_arx_free(xx_arx *archive) {
    if (!archive) return;
    xx_arx_destroy(archive);
    xx_mem_free(archive);
}

bool xx_arx_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    xx_arx_stream *stream;
    (void)pd;
    if (!xx_arx_parse(format, &stream)) return false;
    xx_arx_stream_free(stream);
    return true;
}

bool xx_arx_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_arx_stream *stream;
    xx_arx *archive;
    (void)pd;
    if (!format || !xx_arx_parse(format, &stream)) return false;
    archive = (xx_arx *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = stream->end;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->end - format->base_address;
    format->is_valid = true;
    format->base_info_handled = true;
    xx_arx_stream_free(stream);
    return true;
}

int64_t xx_arx_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled || xx_arx_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_arx_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled || xx_arx_handle_base_info(format, pd))
               ? ((xx_arx *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_arx_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_arx_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!xx_arx_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_arx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xx_arx_stream_free;
    state->total_records = stream->count;
    if (!xx_arx_options(&state->options, options) ||
        (stream->count && !xx_arx_record(&state->current_record,
                                         &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    return state;
}

const xx_archive_record *xx_arx_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_arx_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_arx_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (xx_arx_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = xx_arx_record(&state->current_record,
                                      &stream->items[stream->index]);
    return state->has_record;
}

bool xx_arx_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_arx_stream *stream;
    const xx_arx_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t size = 0U;
    bool ok = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (xx_arx_stream *)state->internal_state) ||
        stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_arx_safe(member->name)) return false;
    option = xx_arx_find(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        ok = xx_arx_decode(format, member, &plain, &size, pd);
        if (plain) xx_mem_free(plain);
        return ok;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING ||
             option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_arx_decode(format, member, &plain, &size, pd) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *output = xx_io_file_open(path, "wb");
        created = output != NULL;
        size_t at = 0U;
        if (!output) goto done;
        ok = true;
        while (at < size) {
            ssize_t amount = xx_io_write(output, plain + at, size - at);
            if (amount <= 0 || (size_t)amount > size - at) {
                ok = false;
                break;
            }
            at += (size_t)amount;
        }
        xx_io_close(output);
    }
done:
    if (!ok && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned) xx_str_free(owned);
    return ok;
}

void xx_arx_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
