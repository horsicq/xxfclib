/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * NextStep TAR uses standard 512-byte TAR blocks, but its 225-byte pathname
 * field shifts every remaining field.  This implementation deliberately reads
 * wire offsets rather than using a packed C struct, so ordinary TAR and the
 * shifted layout cannot be confused by compiler padding.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/tar_nextstep/xx_tar_nextstep.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define XX_TAR_NEXTSTEP_BLOCK_SIZE 512U
#define XX_TAR_NEXTSTEP_NAME_SIZE 225U
#define XX_TAR_NEXTSTEP_MODE_OFFSET 0x0e1U
#define XX_TAR_NEXTSTEP_UID_OFFSET 0x0e9U
#define XX_TAR_NEXTSTEP_GID_OFFSET 0x0f1U
#define XX_TAR_NEXTSTEP_SIZE_OFFSET 0x0f9U
#define XX_TAR_NEXTSTEP_MTIME_OFFSET 0x105U
#define XX_TAR_NEXTSTEP_CHECKSUM_OFFSET 0x111U
#define XX_TAR_NEXTSTEP_CHECKSUM_SIZE 8U
#define XX_TAR_NEXTSTEP_TYPE_OFFSET 0x119U
#define XX_TAR_NEXTSTEP_MAX_RECORDS 100000U

typedef struct tar_nextstep_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t data_size;
    uint64_t mtime;
    uint32_t mode;
} tar_nextstep_member;

typedef struct tar_nextstep_private_s {
    tar_nextstep_member *members;
    size_t count;
    size_t capacity;
    int64_t archive_end;
} tar_nextstep_private;

typedef struct tar_nextstep_stream_s {
    size_t index;
} tar_nextstep_stream;

static void tar_nextstep_private_free(void *pointer);
static void tar_nextstep_stream_free(void *pointer);
static void tar_nextstep_vtable_destroy(Abstractformat *self);

static bool tar_nextstep_add_i64(int64_t value, int64_t increment,
                                 int64_t *result) {
    if (!result || value < 0 || increment < 0 || value > INT64_MAX - increment) {
        return false;
    }
    *result = value + increment;
    return true;
}

static bool tar_nextstep_read_at(xx_io_device *device, int64_t offset,
                                 void *buffer, size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool tar_nextstep_is_space(uint8_t value) {
    return value == 0x20U || (value >= 0x09U && value <= 0x0dU);
}

/* Classic TAR octal fields are NUL terminated and may have surrounding ASCII
 * whitespace.  Empty fields mean zero, while any non-octal digit rejects the
 * header instead of silently turning malformed values into zero. */
static bool tar_nextstep_parse_octal(const uint8_t *field, size_t size,
                                     uint64_t *result) {
    size_t start = 0U;
    size_t end = 0U;
    uint64_t value = 0U;
    if (!field || !result || size == 0U) return false;
    while (end < size && field[end] != 0U) ++end;
    while (start < end && tar_nextstep_is_space(field[start])) ++start;
    while (end > start && tar_nextstep_is_space(field[end - 1U])) --end;
    if (start == end) {
        *result = 0U;
        return true;
    }
    for (; start < end; ++start) {
        uint64_t digit;
        if (field[start] < (uint8_t)'0' || field[start] > (uint8_t)'7') {
            return false;
        }
        digit = (uint64_t)(field[start] - (uint8_t)'0');
        if (value > (UINT64_MAX - digit) / 8U) return false;
        value = value * 8U + digit;
    }
    *result = value;
    return true;
}

static uint32_t tar_nextstep_checksum(const uint8_t *header) {
    uint32_t sum = (uint32_t)XX_TAR_NEXTSTEP_CHECKSUM_SIZE * 0x20U;
    size_t index;
    for (index = 0U; index < XX_TAR_NEXTSTEP_CHECKSUM_OFFSET; ++index) {
        sum += header[index];
    }
    for (index = XX_TAR_NEXTSTEP_CHECKSUM_OFFSET +
                 XX_TAR_NEXTSTEP_CHECKSUM_SIZE;
         index < XX_TAR_NEXTSTEP_BLOCK_SIZE; ++index) {
        sum += header[index];
    }
    return sum;
}

static bool tar_nextstep_is_zero_block(const uint8_t *header) {
    size_t index;
    if (!header) return false;
    for (index = 0U; index < XX_TAR_NEXTSTEP_BLOCK_SIZE; ++index) {
        if (header[index] != 0U) return false;
    }
    return true;
}

static char *tar_nextstep_copy_name(const uint8_t *field, size_t size) {
    size_t length = 0U;
    char *name;
    if (!field || size == 0U) return NULL;
    while (length < size && field[length] != 0U) ++length;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    if (length != 0U) xx_mem_copy(name, field, length);
    name[length] = '\0';
    return name;
}

static bool tar_nextstep_add_member(tar_nextstep_private *private_state,
                                    tar_nextstep_member *member) {
    tar_nextstep_member *members;
    size_t capacity;
    if (!private_state || !member || !member->name) return false;
    if (private_state->count >= XX_TAR_NEXTSTEP_MAX_RECORDS) return false;
    if (private_state->count == private_state->capacity) {
        capacity = private_state->capacity ? private_state->capacity * 2U : 16U;
        if (capacity < private_state->capacity ||
            capacity > SIZE_MAX / sizeof(*members)) {
            return false;
        }
        members = (tar_nextstep_member *)xx_mem_realloc(
            private_state->members, capacity * sizeof(*members));
        if (!members) return false;
        private_state->members = members;
        private_state->capacity = capacity;
    }
    private_state->members[private_state->count++] = *member;
    member->name = NULL;
    return true;
}

static void tar_nextstep_private_free(void *pointer) {
    tar_nextstep_private *private_state = (tar_nextstep_private *)pointer;
    size_t index;
    if (!private_state) return;
    for (index = 0U; index < private_state->count; ++index) {
        xx_mem_free(private_state->members[index].name);
    }
    xx_mem_free(private_state->members);
    xx_mem_free(private_state);
}

static void tar_nextstep_stream_free(void *pointer) {
    xx_mem_free(pointer);
}

/* A NextStep archive is identified only by the shifted checksum.  The first
 * header must verify.  Once at least one valid header was read, malformed
 * trailing data ends the archive just as historical tools do. */
static bool tar_nextstep_parse(Abstractformat *format,
                               tar_nextstep_private **result,
                               xx_pd_struct *pd) {
    tar_nextstep_private *private_state;
    int64_t total_size;
    int64_t archive_size;
    int64_t relative_offset = 0;
    bool first_header = true;
    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    *result = NULL;
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address) return false;
    archive_size = total_size - format->base_address;
    if (archive_size < (int64_t)XX_TAR_NEXTSTEP_BLOCK_SIZE) return false;
    private_state = (tar_nextstep_private *)xx_mem_calloc(1U,
                                                          sizeof(*private_state));
    if (!private_state) return false;
    private_state->archive_end = total_size;

    while (relative_offset <= archive_size - (int64_t)XX_TAR_NEXTSTEP_BLOCK_SIZE) {
        uint8_t header[XX_TAR_NEXTSTEP_BLOCK_SIZE];
        uint64_t stored_checksum;
        uint64_t data_size_u64;
        uint64_t mode_u64 = 0U;
        uint64_t mtime_u64 = 0U;
        int64_t header_offset;
        int64_t data_offset;
        int64_t data_size;
        int64_t padded_size;
        char type;
        bool regular;

        if ((pd && xx_pd_is_stopped(pd)) ||
            !tar_nextstep_add_i64(format->base_address, relative_offset,
                                  &header_offset) ||
            !tar_nextstep_read_at(format->device, header_offset, header,
                                  sizeof(header))) {
            goto fail;
        }
        if (tar_nextstep_is_zero_block(header)) {
            private_state->archive_end = header_offset;
            break;
        }
        if (!tar_nextstep_parse_octal(
                header + XX_TAR_NEXTSTEP_CHECKSUM_OFFSET,
                XX_TAR_NEXTSTEP_CHECKSUM_SIZE, &stored_checksum) ||
            stored_checksum == 0U || stored_checksum > UINT32_MAX ||
            (uint32_t)stored_checksum != tar_nextstep_checksum(header)) {
            if (first_header) goto fail;
            private_state->archive_end = header_offset;
            break;
        }
        first_header = false;
        if (!tar_nextstep_parse_octal(header + XX_TAR_NEXTSTEP_SIZE_OFFSET,
                                      12U, &data_size_u64) ||
            data_size_u64 > (uint64_t)INT64_MAX) {
            private_state->archive_end = header_offset;
            break;
        }
        /* Mode and mtime are metadata only.  Keep an absent or malformed
         * value at zero without weakening the checksum-based detector. */
        (void)tar_nextstep_parse_octal(header + XX_TAR_NEXTSTEP_MODE_OFFSET,
                                       8U, &mode_u64);
        (void)tar_nextstep_parse_octal(header + XX_TAR_NEXTSTEP_MTIME_OFFSET,
                                       12U, &mtime_u64);
        data_size = (int64_t)data_size_u64;
        type = (char)header[XX_TAR_NEXTSTEP_TYPE_OFFSET];
        if (type == '1' || type == '2') data_size = 0;
        if (!tar_nextstep_add_i64(relative_offset,
                                  (int64_t)XX_TAR_NEXTSTEP_BLOCK_SIZE,
                                  &data_offset) ||
            data_size > INT64_MAX - ((int64_t)XX_TAR_NEXTSTEP_BLOCK_SIZE - 1)) {
            private_state->archive_end = header_offset;
            break;
        }
        padded_size = ((data_size + (int64_t)XX_TAR_NEXTSTEP_BLOCK_SIZE - 1) /
                       (int64_t)XX_TAR_NEXTSTEP_BLOCK_SIZE) *
                      (int64_t)XX_TAR_NEXTSTEP_BLOCK_SIZE;
        if (padded_size > archive_size - data_offset) {
            private_state->archive_end = total_size;
            break;
        }

        regular = type == '\0' || type == '0';
        if (regular && header[0] != 0U) {
            tar_nextstep_member member;
            xx_mem_zero(&member, sizeof(member));
            member.name = tar_nextstep_copy_name(header, XX_TAR_NEXTSTEP_NAME_SIZE);
            if (!member.name) goto fail;
            member.header_offset = header_offset;
            if (!tar_nextstep_add_i64(format->base_address, data_offset,
                                      &member.data_offset)) {
                xx_mem_free(member.name);
                goto fail;
            }
            member.data_size = data_size;
            member.mtime = mtime_u64;
            member.mode = mode_u64 > UINT32_MAX ? UINT32_MAX :
                                                  (uint32_t)mode_u64;
            if (!tar_nextstep_add_member(private_state, &member)) {
                xx_mem_free(member.name);
                goto fail;
            }
        }
        if (!tar_nextstep_add_i64(data_offset, padded_size, &relative_offset)) {
            goto fail;
        }
        if (!tar_nextstep_add_i64(format->base_address, relative_offset,
                                  &private_state->archive_end)) {
            goto fail;
        }
    }
    if (private_state->count == 0U) goto fail;
    *result = private_state;
    return true;
fail:
    tar_nextstep_private_free(private_state);
    return false;
}

static bool tar_nextstep_copy_options(xx_list_s *destination,
                                      const xx_list_s *source) {
    size_t index;
    if (!destination) return false;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *tar_nextstep_find_option(const xx_list_s *options,
                                               uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

/* Convert harmless leading ./ components to a relative path and reject names
 * that have ambiguous or unsafe Windows/POSIX filesystem meanings. */
static char *tar_nextstep_safe_output_name(const char *source) {
    const char *name = source;
    size_t length;
    size_t index;
    size_t component_start = 0U;
    char *result;
    if (!source) return NULL;
    while (name[0] == '.' && (name[1] == '/' || name[1] == '\\')) name += 2;
    length = xx_str_len(name);
    if (length == 0U || name[0] == '/' || name[0] == '\\' ||
        (length >= 2U && name[1] == ':')) {
        return NULL;
    }
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    for (index = 0U; index < length; ++index) {
        unsigned char value = (unsigned char)name[index];
        if (value < 0x20U || value == 0x7fU || value == ':' || value == '<' ||
            value == '>' || value == '"' || value == '|' || value == '?' ||
            value == '*') {
            xx_mem_free(result);
            return NULL;
        }
        result[index] = name[index] == '\\' ? '/' : name[index];
    }
    result[length] = '\0';
    for (index = 0U; index <= length; ++index) {
        if (index != length && result[index] != '/') continue;
        if (index == component_start ||
            (index - component_start == 1U && result[component_start] == '.') ||
            (index - component_start == 2U && result[component_start] == '.' &&
             result[component_start + 1U] == '.')) {
            xx_mem_free(result);
            return NULL;
        }
        component_start = index + 1U;
    }
    return result;
}

static bool tar_nextstep_set_record(xx_archive_record *record,
                                    const tar_nextstep_member *member) {
    if (!record || !member || !member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)XX_TAR_NEXTSTEP_BLOCK_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->mtime) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->mode) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_tar_nextstep_init(xx_tar_nextstep *archive, xx_io_device *device,
                          int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_UNKNOWN;
    archive->format.file_type = XX_FILE_TYPE_TAR_NEXTSTEP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-tar");
    xx_format_set_extension(&archive->format, "tar");
    archive->format.check_is_valid = xx_tar_nextstep_check_is_valid;
    archive->format.handle_base_info = xx_tar_nextstep_handle_base_info;
    archive->format.get_format_size = xx_tar_nextstep_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_tar_nextstep_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_tar_nextstep_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_tar_nextstep_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_tar_nextstep_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_tar_nextstep_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_tar_nextstep_free_archive_records_reading;
    archive->format.destroy = tar_nextstep_vtable_destroy;
    archive->archive_end = -1;
}

xx_tar_nextstep *xx_tar_nextstep_create(xx_io_device *device,
                                         int64_t base_address) {
    xx_tar_nextstep *archive =
        (xx_tar_nextstep *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_tar_nextstep_init(archive, device, base_address);
    return archive;
}

void xx_tar_nextstep_destroy(xx_tar_nextstep *archive) {
    if (!archive) return;
    if (archive->internal) {
        tar_nextstep_private_free(archive->internal);
        archive->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void tar_nextstep_vtable_destroy(Abstractformat *self) {
    xx_tar_nextstep_destroy((xx_tar_nextstep *)self);
}

void xx_tar_nextstep_free(xx_tar_nextstep *archive) {
    if (!archive) return;
    xx_tar_nextstep_destroy(archive);
    xx_mem_free(archive);
}

bool xx_tar_nextstep_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    tar_nextstep_private *private_state = NULL;
    bool result = tar_nextstep_parse(self, &private_state, pd);
    tar_nextstep_private_free(private_state);
    return result;
}

bool xx_tar_nextstep_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    tar_nextstep_private *private_state = NULL;
    xx_tar_nextstep *archive;
    int64_t total_size;
    if (!self || !tar_nextstep_parse(self, &private_state, pd)) {
        if (self) self->is_valid = false;
        return false;
    }
    archive = (xx_tar_nextstep *)self;
    if (archive->internal) tar_nextstep_private_free(archive->internal);
    archive->internal = private_state;
    archive->number_of_records = (uint64_t)private_state->count;
    archive->archive_end = private_state->archive_end;
    total_size = xx_io_total_size(self->device);
    self->file_type = XX_FILE_TYPE_TAR_NEXTSTEP;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->number_of_archive_records = (uint64_t)private_state->count;
    self->format_size = private_state->archive_end - self->base_address;
    self->overlay_offset = private_state->archive_end < total_size
                               ? private_state->archive_end
                               : -1;
    self->overlay_size = private_state->archive_end < total_size
                             ? total_size - private_state->archive_end
                             : 0;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_tar_nextstep_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_tar_nextstep_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_tar_nextstep_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_tar_nextstep_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_tar_nextstep *)self)->number_of_records;
}

xx_archive_record_state *xx_tar_nextstep_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_nextstep *archive;
    tar_nextstep_private *private_state;
    tar_nextstep_stream *stream;
    xx_archive_record_state *state;
    if (!self || (!self->base_info_handled &&
                  !xx_tar_nextstep_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    archive = (xx_tar_nextstep *)self;
    private_state = (tar_nextstep_private *)archive->internal;
    if (!private_state || private_state->count == 0U) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (tar_nextstep_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        xx_mem_free(state);
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = tar_nextstep_stream_free;
    state->total_records = (int64_t)private_state->count;
    if (!tar_nextstep_copy_options(&state->options, options) ||
        !tar_nextstep_set_record(&state->current_record,
                                 &private_state->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_tar_nextstep_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_tar_nextstep_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_tar_nextstep *archive;
    tar_nextstep_private *private_state;
    tar_nextstep_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (tar_nextstep_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    archive = (xx_tar_nextstep *)self;
    private_state = (tar_nextstep_private *)archive->internal;
    if (!private_state) return false;
    ++stream->index;
    if (stream->index >= private_state->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!tar_nextstep_set_record(&state->current_record,
                                 &private_state->members[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_tar_nextstep_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_tar_nextstep *archive;
    tar_nextstep_private *private_state;
    tar_nextstep_stream *stream;
    const tar_nextstep_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *safe_name = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (tar_nextstep_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    archive = (xx_tar_nextstep *)self;
    private_state = (tar_nextstep_private *)archive->internal;
    if (!private_state || stream->index >= private_state->count) return false;
    member = &private_state->members[stream->index];
    safe_name = tar_nextstep_safe_output_name(member->name);
    if (!safe_name) goto cleanup;
    path_option = tar_nextstep_find_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = member->data_offset >= 0 && member->data_size >= 0 &&
                 member->data_offset <= xx_io_total_size(self->device) &&
                 member->data_size <=
                     xx_io_total_size(self->device) - member->data_offset;
        goto cleanup;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    destination = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
                   base[xx_str_len(base) - 1U] != '\\')
                      ? xx_str_concat3(base, "/", safe_name)
                      : xx_str_concat(base, safe_name);
    if (!destination) goto cleanup;
    if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                 member->data_offset,
                                                 member->data_size,
                                                 destination, pd);
        if (!result) xx_rt_remove(destination);
    }
cleanup:
    if (destination) xx_str_free(destination);
    if (safe_name) xx_str_free(safe_name);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_tar_nextstep_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_tar_nextstep_get_number_of_records(
    const xx_tar_nextstep *archive) {
    return archive ? archive->number_of_records : 0U;
}

int64_t xx_tar_nextstep_get_archive_end(const xx_tar_nextstep *archive) {
    return archive ? archive->archive_end : -1;
}
