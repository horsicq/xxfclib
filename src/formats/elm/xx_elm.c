/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ELM theme bundles begin with a dotted version, a decimal file count, then
 * "name,size" lines.  Each stored payload has a 14-byte <==MS-Theme==>
 * marker immediately before it.  Some producers append unlisted CSS after
 * the last declared payload; it is bounded printable CSS containing
 * .mstheme near its start and is never emitted as a guessed extra member.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/elm/xx_elm.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#define XX_ELM_MAX_IMAGE INT64_C(0x40000000)
#define XX_ELM_MAX_HEADER INT64_C(0x100000)
#define XX_ELM_MAX_RECORDS 4096U
#define XX_ELM_MAX_MEMBER INT64_C(0x10000000)
#define XX_ELM_MAX_TRAILER INT64_C(0x10000)
#define XX_ELM_MARKER_SIZE 14U

typedef struct xx_elm_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;
} xx_elm_member;

typedef struct xx_elm_stream_s {
    xx_elm_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    bool has_css_trailer;
} xx_elm_stream;

static void xx_elm_vtable_destroy(Abstractformat *self);

static bool xx_elm_read_at(Abstractformat *self, int64_t offset,
                           void *buffer, size_t size) {
    size_t done = 0U;
    if (!self || !self->device || (!buffer && size) || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t got = xx_io_read(self->device, (uint8_t *)buffer + done,
                                 size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_elm_line(Abstractformat *self, int64_t span,
                         int64_t *cursor, char *line, size_t capacity) {
    size_t length = 0U;
    uint8_t byte;
    if (!cursor || !line || capacity < 2U) return false;
    while (*cursor < span && *cursor < XX_ELM_MAX_HEADER &&
           length + 1U < capacity) {
        if (!xx_elm_read_at(self, self->base_address + *cursor, &byte, 1U))
            return false;
        ++*cursor;
        if (byte == '\n') {
            if (length && line[length - 1U] == '\r') --length;
            line[length] = 0;
            return true;
        }
        if (byte < 0x20U || byte > 0x7eU) return false;
        line[length++] = (char)byte;
    }
    return false;
}

static bool xx_elm_decimal(const char *text, int64_t ceiling,
                            int64_t *value) {
    int64_t result = 0;
    if (!text || !text[0] || !value) return false;
    while (*text) {
        uint8_t digit = (uint8_t)*text++;
        if (digit < '0' || digit > '9' ||
            result > (ceiling - (digit - '0')) / 10)
            return false;
        result = result * 10 + (digit - '0');
    }
    *value = result;
    return true;
}

static bool xx_elm_version(const char *line) {
    size_t index = 0U;
    unsigned dots = 0U;
    bool previous_digit = false;
    if (!line || !line[0]) return false;
    while (line[index] && index < 32U) {
        char c = line[index++];
        if (c >= '0' && c <= '9') {
            previous_digit = true;
        } else if (c == '.' && previous_digit) {
            ++dots;
            previous_digit = false;
        } else {
            return false;
        }
    }
    return !line[index] && previous_digit && dots >= 2U;
}

static bool xx_elm_name_safe(const char *name) {
    size_t length = 0U;
    if (!name) return false;
    while (name[length]) {
        char c = name[length++];
        if (length > 240U || (uint8_t)c <= 0x20U || c == '/' || c == '\\' ||
            c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|' || c == '%')
            return false;
    }
    return length > 0U && !(length == 1U && name[0] == '.') &&
           !(length == 2U && name[0] == '.' && name[1] == '.');
}

static void xx_elm_stream_free(void *pointer) {
    xx_elm_stream *stream = (xx_elm_stream *)pointer;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        xx_str_free(stream->items[index].name);
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static xx_elm_stream *xx_elm_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t marker[] = "<==MS-Theme==>";
    xx_elm_stream *stream = NULL;
    char line[512];
    int64_t span, total, cursor = 0, declared;
    size_t index;
    uint8_t observed[XX_ELM_MARKER_SIZE];
    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < 32 || span > XX_ELM_MAX_IMAGE ||
        !xx_elm_line(self, span, &cursor, line, sizeof(line)) ||
        !xx_elm_version(line) ||
        !xx_elm_line(self, span, &cursor, line, sizeof(line)) ||
        !xx_elm_decimal(line, XX_ELM_MAX_RECORDS, &declared) ||
        declared < 1)
        return NULL;
    stream = (xx_elm_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->items = (xx_elm_member *)xx_mem_calloc(
        (size_t)declared, sizeof(*stream->items));
    if (!stream->items) goto fail;
    stream->count = (size_t)declared;
    for (index = 0U; index < stream->count; ++index) {
        char *comma = NULL;
        char *scan;
        int64_t line_start = cursor;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_elm_line(self, span, &cursor, line, sizeof(line))) goto fail;
        for (scan = line; *scan; ++scan)
            if (*scan == ',') comma = scan;
        if (!comma) goto fail;
        *comma++ = 0;
        if (!xx_elm_name_safe(line) ||
            !xx_elm_decimal(comma, XX_ELM_MAX_MEMBER,
                            &stream->items[index].size))
            goto fail;
        stream->items[index].name = xx_str_dup(line);
        if (!stream->items[index].name) goto fail;
        stream->items[index].header_offset = self->base_address + line_start;
        stream->items[index].header_size = cursor - line_start;
    }
    for (index = 0U; index < stream->count; ++index) {
        xx_elm_member *member = &stream->items[index];
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (cursor > span || span - cursor < (int64_t)XX_ELM_MARKER_SIZE ||
            !xx_elm_read_at(self, self->base_address + cursor, observed,
                            sizeof(observed)) ||
            xx_rt_memcmp(observed, marker, XX_ELM_MARKER_SIZE) != 0)
            goto fail;
        cursor += (int64_t)XX_ELM_MARKER_SIZE;
        if (member->size > span - cursor) goto fail;
        member->data_offset = self->base_address + cursor;
        cursor += member->size;
    }
    if (span - cursor > XX_ELM_MAX_TRAILER) goto fail;
    if (span != cursor) {
        uint8_t prefix[64];
        uint8_t bytes[4096];
        int64_t at = cursor;
        size_t inspect = (size_t)((span - cursor) < (int64_t)sizeof(prefix)
                                      ? (span - cursor) : (int64_t)sizeof(prefix));
        size_t offset;
        bool found = false;
        if (inspect < 8U ||
            !xx_elm_read_at(self, self->base_address + cursor, prefix,
                            inspect)) goto fail;
        for (offset = 0U; offset + 8U <= inspect; ++offset)
            if (xx_rt_memcmp(prefix + offset, ".mstheme", 8U) == 0)
                found = true;
        if (!found) goto fail;
        while (at < span) {
            size_t amount = (size_t)((span - at) < (int64_t)sizeof(bytes)
                                         ? (span - at) : (int64_t)sizeof(bytes));
            if (!xx_elm_read_at(self, self->base_address + at, bytes, amount))
                goto fail;
            for (offset = 0U; offset < amount; ++offset) {
                uint8_t c = bytes[offset];
                if (c != '\t' && c != '\r' && c != '\n' &&
                    (c < 0x20U || c > 0x7eU)) goto fail;
            }
            at += (int64_t)amount;
        }
    }
    stream->archive_size = span;
    stream->has_css_trailer = span != cursor;
    return stream;
fail:
    xx_elm_stream_free(stream);
    return NULL;
}

void xx_elm_init(xx_elm *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ELM;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-elm-theme");
    xx_format_set_extension(&archive->format, "elm");
    archive->format.check_is_valid = xx_elm_check_is_valid;
    archive->format.handle_base_info = xx_elm_handle_base_info;
    archive->format.get_format_size = xx_elm_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_elm_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_elm_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_elm_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_elm_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_elm_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_elm_free_archive_records_reading;
    archive->format.destroy = xx_elm_vtable_destroy;
}

xx_elm *xx_elm_create(xx_io_device *device, int64_t base_address) {
    xx_elm *archive = (xx_elm *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_elm_init(archive, device, base_address);
    return archive;
}

void xx_elm_destroy(xx_elm *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_elm_free(xx_elm *archive) {
    if (!archive) return;
    xx_elm_destroy(archive);
    xx_mem_free(archive);
}

static void xx_elm_vtable_destroy(Abstractformat *self) {
    xx_elm_destroy((xx_elm *)self);
}

bool xx_elm_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_elm_stream *stream = xx_elm_parse(self, pd);
    if (!stream) return false;
    xx_elm_stream_free(stream);
    return true;
}

bool xx_elm_is_css_trailer_variant(Abstractformat *self, xx_pd_struct *pd) {
    xx_elm_stream *stream = xx_elm_parse(self, pd);
    bool result;
    if (!stream) return false;
    result = stream->has_css_trailer;
    xx_elm_stream_free(stream);
    return result;
}

bool xx_elm_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_elm_stream *stream;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_elm_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    ((xx_elm *)self)->number_of_records = stream->count;
    xx_elm_stream_free(stream);
    return true;
}

int64_t xx_elm_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_elm_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return self->is_valid ? ((xx_elm *)self)->number_of_records : 0U;
}

static bool xx_elm_set_record(xx_archive_record *record,
                               const xx_elm_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_elm_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;
    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_elm_get_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_elm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_elm_stream *stream = xx_elm_parse(self, pd);
    xx_archive_record_state *state;
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_elm_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_elm_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_elm_copy_options(&state->options, options) ||
        !xx_elm_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_elm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_elm_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_elm_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_elm_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_elm_set_record(&state->current_record,
                                          &stream->items[stream->index]);
    return state->has_record;
}

bool xx_elm_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_elm_stream *stream;
    const xx_elm_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    xx_io_device *output = NULL;
    uint8_t buffer[64 * 1024];
    int64_t remaining;
    int64_t cursor;
    bool ok = true;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_elm_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    path_option = xx_elm_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base_path = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) goto done;
    if (base_path[0] && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\')
        target_path = xx_str_concat3(base_path, "/", member->name);
    else
        target_path = xx_str_concat(base_path, member->name);
    if (!target_path || !xx_store_create_dirs_a(target_path, false)) goto done;
    output = xx_io_file_open(target_path, "wb");
    if (!output) goto done;
    remaining = member->size;
    cursor = member->data_offset;
    while (remaining > 0) {
        size_t amount = remaining < (int64_t)sizeof(buffer)
                            ? (size_t)remaining : sizeof(buffer);
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_elm_read_at(self, cursor, buffer, amount)) {
            ok = false;
            break;
        }
        while (written < amount) {
            ssize_t sent = xx_io_write(output, buffer + written,
                                       amount - written);
            if (sent <= 0 || (size_t)sent > amount - written) {
                ok = false;
                break;
            }
            written += (size_t)sent;
        }
        if (!ok) break;
        cursor += (int64_t)amount;
        remaining -= (int64_t)amount;
    }
    if (xx_io_close(output) != 0) ok = false;
    output = NULL;
    if (!ok) xx_rt_remove(target_path);
    xx_str_free(converted_path);
    xx_str_free(target_path);
    return ok;
done:
    if (output) xx_io_close(output);
    xx_str_free(converted_path);
    xx_str_free(target_path);
    return false;
}

void xx_elm_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
