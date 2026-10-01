/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Mozilla MAR index and member extraction. Format reference:
 * https://github.com/mozilla/gecko-dev/blob/master/modules/libmar/src/mar_read.c
 * https://firefox-source-docs.mozilla.org/toolkit/mozapps/update/docs/MarFiles.html
 *
 * MAR stores byte ranges. The bytes in a range can themselves be bzip2/xz
 * compressed update payloads or binary patches; this reader publishes the
 * exact indexed bytes and does not apply a Firefox update manifest.
 */
#include "xxfclib/formats/mozilla_mar/xx_mozilla_mar.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define MAR_MAX_INDEX (32U * 1024U * 1024U)
#define MAR_MAX_MEMBERS 1000000U
#define MAR_MAX_NAME 4096U
#define MAR_COPY_BLOCK 16384U

typedef struct mar_member_s {
    uint32_t offset;
    uint32_t length;
    uint32_t flags;
    uint32_t entry_at;
    char *name; /* points into mar_stream.index */
} mar_member;

typedef struct mar_stream_s {
    uint8_t *index;
    mar_member *members;
    size_t count;
    size_t current;
    int64_t index_offset;
    int64_t size;
} mar_stream;

static uint32_t mar_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static uint64_t mar_be64(const uint8_t *p) {
    return ((uint64_t)mar_be32(p) << 32U) | mar_be32(p + 4U);
}

static bool mar_read_at(Abstractformat *self, int64_t relative,
                        void *buffer, size_t length) {
    size_t done = 0U;
    int64_t total;
    if (!self || !self->device || self->base_address < 0 || relative < 0 ||
        relative > INT64_MAX - self->base_address ||
        (total = xx_io_size(self->device)) < self->base_address + relative ||
        (uint64_t)length > (uint64_t)(total - self->base_address - relative) ||
        (!buffer && length != 0U) ||
        xx_io_seek64(self->device, self->base_address + relative, SEEK_SET) != 0)
        return false;
    while (done < length) {
        size_t amount = length - done;
        ssize_t received;
        if (amount > MAR_COPY_BLOCK) amount = MAR_COPY_BLOCK;
        received = xx_io_read(self->device, (uint8_t *)buffer + done, amount);
        if (received <= 0 || (size_t)received > amount) return false;
        done += (size_t)received;
    }
    return true;
}

static void mar_stream_free(void *opaque) {
    mar_stream *stream = (mar_stream *)opaque;
    if (!stream) return;
    xx_mem_free(stream->index);
    xx_mem_free(stream->members);
    xx_mem_free(stream);
}

/* Newer MARs put signed metadata between the 8-byte legacy header and data.
 * The complete signature/section structure identifies the new header even
 * when its 64-bit file-size field is damaged, so a size mismatch cannot make
 * signed metadata appear to be legacy member bytes. */
static bool mar_data_start(Abstractformat *self, int64_t index_offset,
                           int64_t archive_size, int64_t *start) {
    uint8_t header[20];
    uint32_t count, i;
    int64_t at = 20;
    bool declared_matches;
    *start = 8;
    if (index_offset < 24) return true;
    if (!mar_read_at(self, 0, header, sizeof(header))) return false;
    declared_matches = mar_be64(header + 8U) == (uint64_t)archive_size;
    count = mar_be32(header + 16U);
    if (count > 8U) return !declared_matches;
    for (i = 0U; i < count; ++i) {
        uint8_t signature[8];
        uint32_t length;
        if (at > index_offset - 8 ||
            !mar_read_at(self, at, signature, sizeof(signature)))
            return !declared_matches;
        length = mar_be32(signature + 4U);
        if (length > 2048U || (int64_t)length > index_offset - at - 8)
            return !declared_matches;
        at += 8 + (int64_t)length;
    }
    if (at > index_offset - 4 || !mar_read_at(self, at, header, 4U))
        return !declared_matches;
    count = mar_be32(header);
    at += 4;
    if (count > 1024U) return !declared_matches;
    for (i = 0U; i < count; ++i) {
        uint32_t length;
        if (at > index_offset - 8 ||
            !mar_read_at(self, at, header, 8U)) return !declared_matches;
        length = mar_be32(header);
        if (length < 8U || (int64_t)length > index_offset - at)
            return !declared_matches;
        at += length;
    }
    if (!declared_matches) return false;
    *start = at;
    return true;
}

static mar_stream *mar_parse(Abstractformat *self, xx_pd_struct *pd) {
    mar_stream *stream = NULL;
    uint8_t header[8], index_header[4];
    uint32_t index_size, index_offset;
    int64_t available, archive_size, data_start;
    size_t at, count = 0U, i;
    int64_t original_cursor;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return NULL;
    original_cursor = xx_io_tell(self->device);
    if (original_cursor < 0) return NULL;
    available = xx_io_size(self->device) - self->base_address;
    if (available < 12 || !mar_read_at(self, 0, header, sizeof(header)) ||
        xx_rt_memcmp(header, "MAR1", 4U) != 0) goto done;
    index_offset = mar_be32(header + 4U);
    if (index_offset < 8U || (int64_t)index_offset > available - 4 ||
        !mar_read_at(self, index_offset, index_header, sizeof(index_header)))
        goto done;
    index_size = mar_be32(index_header);
    if (index_size > MAR_MAX_INDEX ||
        (int64_t)index_size > available - (int64_t)index_offset - 4)
        goto done;
    archive_size = (int64_t)index_offset + 4 + (int64_t)index_size;
    if (!mar_data_start(self, index_offset, archive_size, &data_start))
        goto done;
    stream = (mar_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto done;
    xx_mem_zero(stream, sizeof(*stream));
    stream->index = (uint8_t *)xx_mem_alloc(index_size ? index_size : 1U);
    if (!stream->index ||
        !mar_read_at(self, (int64_t)index_offset + 4, stream->index,
                     index_size)) goto fail;
    /* First pass checks every name and range before exposing any record. */
    for (at = 0U; at < index_size;) {
        uint32_t offset, length;
        size_t name_at, end;
        if ((count & 0x3FFU) == 0U && pd && xx_pd_is_stopped(pd)) goto fail;
        if (index_size - at < 14U) goto fail;
        offset = mar_be32(stream->index + at);
        length = mar_be32(stream->index + at + 4U);
        if ((int64_t)offset > index_offset ||
            (int64_t)length > (int64_t)index_offset - offset ||
            (length != 0U && (int64_t)offset < data_start)) goto fail;
        name_at = at + 12U;
        end = name_at;
        while (end < index_size && stream->index[end] != 0U &&
               end - name_at <= MAR_MAX_NAME) ++end;
        if (end == name_at || end == index_size ||
            end - name_at > MAR_MAX_NAME) goto fail;
        at = end + 1U;
        if (++count > MAR_MAX_MEMBERS) goto fail;
    }
    if (count) {
        stream->members = (mar_member *)xx_mem_alloc(count * sizeof(*stream->members));
        if (!stream->members) goto fail;
    }
    stream->count = count;
    stream->index_offset = index_offset;
    stream->size = archive_size;
    at = 0U;
    for (i = 0U; i < count; ++i) {
        mar_member *member = &stream->members[i];
        member->entry_at = (uint32_t)at;
        member->offset = mar_be32(stream->index + at);
        member->length = mar_be32(stream->index + at + 4U);
        member->flags = mar_be32(stream->index + at + 8U);
        member->name = (char *)(stream->index + at + 12U);
        at += 12U + xx_rt_strlen(member->name) + 1U;
    }
    goto done;
fail:
    mar_stream_free(stream);
    stream = NULL;
done:
    if (xx_io_seek64(self->device, original_cursor, SEEK_SET) != 0) {
        mar_stream_free(stream);
        return NULL;
    }
    return stream;
}

/* Archive paths remain visible in listings, but only relative, portable
 * paths can be materialized on disk. */
static bool mar_component_safe(const char *part, size_t length) {
    static const char *const devices[] = {
        "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"
    };
    size_t i, stem = 0U;
    char upper[16];
    if (!length || length == 1U && part[0] == '.' ||
        length == 2U && part[0] == '.' && part[1] == '.' ||
        part[length - 1U] == '.' || part[length - 1U] == ' ') return false;
    for (i = 0U; i < length; ++i) {
        unsigned char c = (unsigned char)part[i];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*') return false;
    }
    while (stem < length && part[stem] != '.') ++stem;
    while (stem && part[stem - 1U] == ' ') --stem;
    if (stem >= sizeof(upper)) return true;
    for (i = 0U; i < stem; ++i) {
        char c = part[i];
        upper[i] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    upper[stem] = 0;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (xx_str_cmp(upper, devices[i]) == 0) return false;
    if (stem == 4U && upper[3] >= '0' && upper[3] <= '9' &&
        ((upper[0] == 'C' && upper[1] == 'O' && upper[2] == 'M') ||
         (upper[0] == 'L' && upper[1] == 'P' && upper[2] == 'T')))
        return false;
    return true;
}

static bool mar_path_safe(const char *name) {
    size_t i = 0U, start = 0U;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    for (;;) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == 0) {
            if (!mar_component_safe(name + start, i - start)) return false;
            if (name[i] == 0) return true;
            start = i + 1U;
        }
        ++i;
    }
}

void xx_mozilla_mar_init(xx_mozilla_mar *archive, xx_io_device *device,
                         int64_t base_address) {
    Abstractformat *self;
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    self = &archive->format;
    xx_format_init(self, device, base_address);
    self->endian = XX_ENDIAN_BIG;
    self->file_type = XX_FILE_TYPE_MOZILLA_MAR;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    xx_format_set_mime_type(self, "application/x-mozilla-mar");
    xx_format_set_extension(self, "mar");
    self->check_is_valid = xx_mozilla_mar_check_is_valid;
    self->handle_base_info = xx_mozilla_mar_handle_base_info;
    self->get_format_size = xx_mozilla_mar_get_format_size;
    self->get_number_of_archive_records =
        xx_mozilla_mar_get_number_of_archive_records;
    self->create_archive_records_reading =
        xx_mozilla_mar_create_archive_records_reading;
    self->get_current_archive_record =
        xx_mozilla_mar_get_current_archive_record;
    self->archive_record_move_to_next =
        xx_mozilla_mar_archive_record_move_to_next;
    self->unpack_current_archive_record =
        xx_mozilla_mar_unpack_current_archive_record;
    self->free_archive_records_reading =
        xx_mozilla_mar_free_archive_records_reading;
}

xx_mozilla_mar *xx_mozilla_mar_create(xx_io_device *device,
                                      int64_t base_address) {
    xx_mozilla_mar *archive = (xx_mozilla_mar *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_mozilla_mar_init(archive, device, base_address);
    return archive;
}

void xx_mozilla_mar_destroy(xx_mozilla_mar *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_mozilla_mar_free(xx_mozilla_mar *archive) {
    if (!archive) return;
    xx_mozilla_mar_destroy(archive);
    xx_mem_free(archive);
}

bool xx_mozilla_mar_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    mar_stream *stream = mar_parse(self, pd);
    if (!stream) return false;
    mar_stream_free(stream);
    return true;
}

bool xx_mozilla_mar_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    mar_stream *stream = mar_parse(self, pd);
    if (!stream) {
        if (self) {
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    self->format_size = stream->size;
    self->number_of_archive_records = stream->count;
    self->overlay_offset = self->base_address + stream->size;
    self->overlay_size = xx_io_size(self->device) - self->overlay_offset;
    if (self->overlay_size <= 0) self->overlay_offset = -1;
    self->is_valid = true;
    self->base_info_handled = true;
    mar_stream_free(stream);
    return true;
}

int64_t xx_mozilla_mar_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_mozilla_mar_handle_base_info(self, pd))
               ? self->format_size : -1;
}

uint64_t xx_mozilla_mar_get_number_of_archive_records(Abstractformat *self,
                                                      xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_mozilla_mar_handle_base_info(self, pd))
               ? self->number_of_archive_records : 0U;
}

static bool mar_set_record(xx_archive_record *record, Abstractformat *self,
                           const mar_stream *stream, const mar_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address + stream->index_offset + 4 +
                            member->entry_at;
    record->header_size = 12U + xx_rt_strlen(member->name) + 1U;
    record->data_offset = self->base_address + member->offset;
    record->compressed_size = member->length;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                       member->length) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                       member->length) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                       0U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

xx_archive_record_state *xx_mozilla_mar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    mar_stream *stream = mar_parse(self, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mar_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = mar_stream_free;
    state->total_records = stream->count;
    for (i = 0U; options && i < options->count; ++i) {
        const xx_meta *source = (const xx_meta *)xx_list_at(options, i);
        xx_meta copy;
        if (!source) continue;
        xx_meta_init(&copy, source->meta_id);
        if (!xx_var_copy(&copy.var, &source->var) ||
            !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy);
            xx_archive_record_state_free(state);
            return NULL;
        }
    }
    state->has_record = stream->count != 0U;
    if (state->has_record &&
        !mar_set_record(&state->current_record, self, stream,
                        &stream->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}

const xx_archive_record *xx_mozilla_mar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
        ? &state->current_record : NULL;
}

bool xx_mozilla_mar_archive_record_move_to_next(Abstractformat *self,
                                                 xx_archive_record_state *state,
                                                 xx_pd_struct *pd) {
    mar_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream = (mar_stream *)state->internal_state;
    if (++stream->current >= stream->count) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = mar_set_record(&state->current_record, self, stream,
                                      &stream->members[stream->current]);
    return state->has_record;
}

bool xx_mozilla_mar_extract_record_to_device(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_io_device *destination,
                                              xx_pd_struct *pd) {
    mar_stream *stream;
    const mar_member *member;
    const xx_var *limit;
    uint8_t buffer[MAR_COPY_BLOCK];
    int64_t original_cursor, position;
    uint32_t left;
    bool success = true;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    stream = (mar_stream *)state->internal_state;
    member = &stream->members[stream->current];
    limit = xx_format_resolve_extra_parameter(
        self, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && (uint64_t)member->length > xx_var_get_u64(limit)) return false;
    if (!destination) return true;
    original_cursor = xx_io_tell(self->device);
    if (original_cursor < 0) return false;
    position = (int64_t)self->base_address + member->offset;
    left = member->length;
    while (left) {
        size_t amount = left < sizeof(buffer) ? left : sizeof(buffer);
        size_t used = 0U;
        if (pd && xx_pd_is_stopped(pd)) { success = false; break; }
        if (!mar_read_at(self, position - self->base_address, buffer, amount)) {
            success = false;
            break;
        }
        while (used < amount) {
            ssize_t written = xx_io_write(destination, buffer + used,
                                          amount - used);
            if (written <= 0 || (size_t)written > amount - used) {
                success = false;
                break;
            }
            used += (size_t)written;
        }
        if (!success) break;
        position += amount;
        left -= (uint32_t)amount;
    }
    if (xx_io_seek64(self->device, original_cursor, SEEK_SET) != 0)
        success = false;
    return success && !(pd && xx_pd_is_stopped(pd));
}

static bool mar_same_path(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return *a == *b;
}

static xx_io_device *mar_open_stage(const char *target, char **stage) {
    char *parent = xx_str_dup(target);
    size_t i, cut = 0U;
    unsigned attempt;
    if (!parent) return NULL;
    for (i = 0U; parent[i]; ++i)
        if (parent[i] == '/' || parent[i] == '\\') cut = i + 1U;
    parent[cut] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40];
        char *candidate;
        xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_mar.tmp.%u", attempt);
        candidate = xx_str_concat(parent, suffix);
        if (!candidate) break;
        if (mar_same_path(candidate, target)) {
            xx_str_free(candidate);
            continue;
        }
        output = xx_io_file_open(candidate, "wbx");
        if (output) {
            *stage = candidate;
            xx_str_free(parent);
            return output;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);
    return NULL;
}

bool xx_mozilla_mar_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    mar_stream *stream;
    const mar_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *converted = NULL, *target = NULL, *stage = NULL;
    xx_io_device *output = NULL;
    bool overwrite = false, success = false;
    if (!self || !state || state->format != self || !state->has_record)
        return false;
    stream = (mar_stream *)state->internal_state;
    member = &stream->members[stream->current];
    if (!mar_path_safe(member->name)) return false;
    option = xx_format_resolve_extra_parameter(
        self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return xx_mozilla_mar_extract_record_to_device(
        self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING ||
             option->type == XX_VAR_TYPE_WSTRING_VIEW)
        base = converted = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
    if (!base) goto done;
    target = base[0] ? xx_str_concat3(base, "/", member->name)
                     : xx_str_dup(member->name);
    if (!target || !xx_store_create_dirs_a(target, false)) goto done;
    option = xx_format_resolve_extra_parameter(
        self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    if ((!overwrite && xx_io_file_exists_a(target)) ||
        (pd && xx_pd_is_stopped(pd))) goto done;
    output = mar_open_stage(target, &stage);
    if (!output) goto done;
    success = xx_mozilla_mar_extract_record_to_device(self, state, output, pd);
done:
    if (output && xx_io_close(output) != 0) success = false;
    if (success && stage)
        success = xx_io_file_replace_a(stage, target, overwrite);
    if (stage && !success) (void)xx_io_file_remove_a(stage);
    xx_str_free(stage);
    xx_str_free(target);
    xx_str_free(converted);
    return success;
}

void xx_mozilla_mar_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
