/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ptero-Engine BIGF/ZBL resource archives.
 *
 * This Ptero-Engine container is unrelated to EA's BIGF/BIG4 format.  Its
 * 64-byte header starts with "BIGF", a version byte and "ZBL".  The offset,
 * length and count fields are little endian.  Version zero has 40-byte index
 * entries followed by NUL-terminated names.  Stored payloads are copied;
 * compressed payloads use independently framed "[..]" Ptero LZW blocks.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ptero_bigf/xx_ptero_bigf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <limits.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

#define XX_PTERO_BIGF_HEADER_SIZE 64U
#define XX_PTERO_BIGF_ENTRY_FIXED 40U
#define XX_PTERO_BIGF_MAX_MEMBERS 100000U
#define XX_PTERO_BIGF_MAX_NAME 65535U
#define XX_PTERO_BIGF_MAX_DIRECTORY (256U * 1024U * 1024U)
#define XX_PTERO_BIGF_MAX_BLOCK_OUTPUT (64U * 1024U * 1024U)
#define XX_PTERO_BIGF_MAX_DICTIONARY (1U << 24)

typedef struct xx_ptero_bigf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool compressed;
    bool is_folder;
} xx_ptero_bigf_member;

typedef struct xx_ptero_bigf_stream_s {
    xx_ptero_bigf_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_ptero_bigf_stream;

static void xx_ptero_bigf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ptero_bigf_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_ptero_bigf_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

static bool xx_ptero_bigf_reserved_component(const char *part,
                                             size_t length) {
    char upper[5] = {0};
    size_t stem = 0U;
    size_t index;

    while (stem < length && part[stem] != '.') ++stem;
    if (stem < 3U || stem > 4U) return false;
    for (index = 0U; index < stem; ++index) {
        unsigned char value = (unsigned char)part[index];
        upper[index] = (char)(value >= 'a' && value <= 'z'
                                  ? value - ('a' - 'A') : value);
    }
    if (stem == 3U) {
        return xx_rt_memcmp(upper, "CON", 3U) == 0 ||
               xx_rt_memcmp(upper, "PRN", 3U) == 0 ||
               xx_rt_memcmp(upper, "AUX", 3U) == 0 ||
               xx_rt_memcmp(upper, "NUL", 3U) == 0;
    }
    return (xx_rt_memcmp(upper, "COM", 3U) == 0 ||
            xx_rt_memcmp(upper, "LPT", 3U) == 0) &&
           upper[3] >= '1' && upper[3] <= '9';
}

/* Refuse traversal, Windows device names, and names that normalize to a
 * different path on Windows before creating an extraction destination. */
static bool xx_ptero_bigf_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') {
            unsigned char value = (unsigned char)*end;
            if (value < 0x20U || value == 0x7fU || *end == ':' ||
                *end == '\\' || *end == '<' || *end == '>' ||
                *end == '"' || *end == '|' || *end == '?' ||
                *end == '*') return false;
            ++end;
        }
        length = (size_t)(end - cursor);
        if (length == 0U || length > 255U ||
            (length == 1U && cursor[0] == '.') ||
            (length == 2U && cursor[0] == '.' && cursor[1] == '.') ||
            cursor[length - 1U] == '.' || cursor[length - 1U] == ' ' ||
            xx_ptero_bigf_reserved_component(cursor, length) ||
            (size_t)(end - name) > 4096U) return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_ptero_bigf_stream_free(void *pointer) {
    xx_ptero_bigf_stream *stream = (xx_ptero_bigf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ptero_bigf_add(xx_ptero_bigf_stream *stream,
                          const xx_ptero_bigf_member *member) {
    if (!stream || !member || stream->count >= XX_PTERO_BIGF_MAX_MEMBERS)
        return false;
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity ? stream->capacity * 2U : 16U;
        xx_ptero_bigf_member *grown;

        if (capacity > XX_PTERO_BIGF_MAX_MEMBERS)
            capacity = XX_PTERO_BIGF_MAX_MEMBERS;
        grown = (xx_ptero_bigf_member *)xx_mem_realloc(
            stream->items, sizeof(*grown) * capacity);
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

static bool xx_ptero_bigf_bits(const uint8_t *source, size_t source_size,
                               size_t *bit_position, unsigned width,
                               uint32_t *code) {
    unsigned i;
    uint32_t value = 0U;
    if (!source || !bit_position || !code || width == 0U || width > 24U ||
        source_size > SIZE_MAX / 8U ||
        *bit_position > source_size * 8U ||
        width > source_size * 8U - *bit_position) return false;
    for (i = 0U; i < width; ++i) {
        const size_t position = *bit_position + i;
        value |= (uint32_t)((source[position / 8U] >> (position & 7U)) & 1U)
                 << i;
    }
    *bit_position += width;
    *code = value;
    return true;
}

/* Ptero LZW uses little-endian codes, a byte-aligned clear code and an early
 * code-width transition.  The output bound also bounds dictionary growth. */
static bool xx_ptero_bigf_lzw(const uint8_t *source, size_t source_size,
                              uint8_t *output, size_t output_size,
                              xx_pd_struct *pd) {
    size_t capacity, dictionary_size = 257U, output_position = 0U;
    size_t bit_position = 0U;
    uint32_t *prefix = NULL;
    uint8_t *suffix = NULL, *phrase = NULL;
    unsigned width = 9U;
    uint32_t previous_code = 0U;
    uint8_t previous_first = 0U;
    bool has_previous = false, result = false;

    if (!source || (!output && output_size) ||
        output_size > XX_PTERO_BIGF_MAX_BLOCK_OUTPUT) return false;
    if (output_size == 0U) return true;
    capacity = output_size > XX_PTERO_BIGF_MAX_DICTIONARY - 257U
                   ? XX_PTERO_BIGF_MAX_DICTIONARY : output_size + 257U;
    prefix = (uint32_t *)xx_mem_alloc(capacity * sizeof(*prefix));
    suffix = (uint8_t *)xx_mem_alloc(capacity);
    phrase = (uint8_t *)xx_mem_alloc(output_size);
    if (!prefix || !suffix || !phrase) goto cleanup;

    while (output_position < output_size) {
        uint32_t code, current;
        uint8_t first;
        size_t phrase_size = 0U, steps = 0U, i;
        bool appended = false;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_ptero_bigf_bits(source, source_size, &bit_position,
                                width, &code)) goto cleanup;
        if (code == 256U) {
            bit_position = (bit_position + 7U) & ~(size_t)7U;
            dictionary_size = 257U;
            width = 9U;
            has_previous = false;
            continue;
        }
        if (code == dictionary_size && has_previous) {
            if (dictionary_size >= capacity) goto cleanup;
            prefix[dictionary_size] = previous_code;
            suffix[dictionary_size] = previous_first;
            ++dictionary_size;
            appended = true;
        } else if (code >= dictionary_size) {
            goto cleanup;
        }

        current = code;
        while (current >= 257U) {
            if (current >= dictionary_size || ++steps > dictionary_size ||
                phrase_size >= output_size - output_position) goto cleanup;
            phrase[phrase_size++] = suffix[current];
            current = prefix[current];
        }
        if (current >= 256U ||
            phrase_size >= output_size - output_position) goto cleanup;
        phrase[phrase_size++] = (uint8_t)current;
        first = phrase[phrase_size - 1U];
        if (has_previous && !appended) {
            if (dictionary_size >= capacity) goto cleanup;
            prefix[dictionary_size] = previous_code;
            suffix[dictionary_size] = first;
            ++dictionary_size;
            appended = true;
        }
        if (appended && (uint64_t)dictionary_size + 1U >=
                            (UINT64_C(1) << width)) {
            if (width >= 24U) goto cleanup;
            ++width;
        }
        for (i = 0U; i < phrase_size; ++i) {
            output[output_position + i] = phrase[phrase_size - 1U - i];
        }
        output_position += phrase_size;
        previous_code = code;
        previous_first = first;
        has_previous = true;
    }
    result = true;
cleanup:
    xx_mem_free(phrase);
    xx_mem_free(suffix);
    xx_mem_free(prefix);
    return result;
}

static bool xx_ptero_bigf_decode(Abstractformat *self,
                                  const xx_ptero_bigf_member *member,
                                  uint8_t **out, size_t *out_size,
                                  xx_pd_struct *pd) {
    uint8_t *buffer = NULL;
    int64_t cursor, end;
    size_t produced = 0U;
    if (!out || !out_size || !member || member->uncompressed_size < 0 ||
        (uint64_t)member->uncompressed_size > SIZE_MAX ||
        (uint64_t)member->uncompressed_size > 256U * 1024U * 1024U) return false;
    *out = NULL;
    *out_size = 0U;
    buffer = (uint8_t *)xx_mem_alloc(member->uncompressed_size
                                    ? (size_t)member->uncompressed_size : 1U);
    if (!buffer) return false;
    if (!member->compressed) {
        if (member->uncompressed_size &&
            !xx_ptero_bigf_read_at(self, member->data_offset, buffer,
                                   (size_t)member->uncompressed_size)) goto fail;
    } else {
        cursor = member->data_offset;
        end = cursor + member->compressed_size;
        while (cursor < end) {
            uint8_t header[12];
            uint32_t packed_size, plain_size;
            uint8_t *packed;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !xx_ptero_bigf_range_within(end, cursor, 12) ||
                !xx_ptero_bigf_read_at(self, cursor, header, sizeof(header)) ||
                memcmp(header, "[..]", 4) != 0) goto fail;
            packed_size = xx_data_get_u32(header + 4, 4, 0, false);
            plain_size = xx_data_get_u32(header + 8, 4, 0, false);
            if (!xx_ptero_bigf_range_within(end, cursor + 12,
                                             packed_size) ||
                plain_size > (size_t)member->uncompressed_size - produced ||
                plain_size > XX_PTERO_BIGF_MAX_BLOCK_OUTPUT) goto fail;
            packed = (uint8_t *)xx_mem_alloc(packed_size ? packed_size : 1U);
            if (!packed) goto fail;
            if ((packed_size && !xx_ptero_bigf_read_at(self, cursor + 12,
                                                       packed, packed_size)) ||
                !xx_ptero_bigf_lzw(packed, packed_size, buffer + produced,
                                    plain_size, pd)) {
                xx_mem_free(packed);
                goto fail;
            }
            xx_mem_free(packed);
            produced += plain_size;
            cursor += 12 + packed_size;
        }
        if (produced != (size_t)member->uncompressed_size) goto fail;
    }
    *out = buffer;
    *out_size = (size_t)member->uncompressed_size;
    return true;
fail:
    xx_mem_free(buffer);
    return false;
}

static bool xx_ptero_bigf_validate_blocks(Abstractformat *self,
                                           const xx_ptero_bigf_member *member,
                                           xx_pd_struct *pd) {
    int64_t cursor = member->data_offset;
    const int64_t end = cursor + member->compressed_size;
    int64_t total_plain = 0;
    while (cursor < end) {
        uint8_t block[12];
        uint32_t packed, plain;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_ptero_bigf_range_within(end, cursor, 12) ||
            !xx_ptero_bigf_read_at(self, cursor, block, sizeof(block)) ||
            memcmp(block, "[..]", 4) != 0) return false;
        packed = xx_data_get_u32(block + 4, 4, 0, false);
        plain = xx_data_get_u32(block + 8, 4, 0, false);
        if (!xx_ptero_bigf_range_within(end, cursor + 12, packed) ||
            packed > XX_PTERO_BIGF_MAX_BLOCK_OUTPUT ||
            plain > XX_PTERO_BIGF_MAX_BLOCK_OUTPUT ||
            plain > member->uncompressed_size - total_plain) return false;
        cursor += 12 + packed;
        total_plain += plain;
    }
    return cursor == end && total_plain == member->uncompressed_size;
}

static xx_ptero_bigf_stream *xx_ptero_bigf_parse(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    uint8_t header[XX_PTERO_BIGF_HEADER_SIZE];
    uint8_t *directory = NULL;
    xx_ptero_bigf_stream *stream = NULL;
    int64_t total, span, archive_size, directory_offset, data_offset;
    size_t directory_size, position = 0U;
    uint32_t count, index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_PTERO_BIGF_HEADER_SIZE ||
        !xx_ptero_bigf_read_at(self, self->base_address, header,
                               sizeof(header)) ||
        memcmp(header, "BIGF", 4) != 0 || header[4] != 0U ||
        memcmp(header + 5, "ZBL", 3) != 0 ||
        xx_data_get_u64(header + 8, 8, 0, false) > INT64_MAX ||
        xx_data_get_u64(header + 20, 8, 0, false) > INT64_MAX ||
        xx_data_get_u64(header + 28, 8, 0, false) > XX_PTERO_BIGF_MAX_DIRECTORY ||
        xx_data_get_u64(header + 36, 8, 0, false) > INT64_MAX) return NULL;
    archive_size = (int64_t)xx_data_get_u64(header + 8, 8, 0, false);
    directory_offset = (int64_t)xx_data_get_u64(header + 20, 8, 0, false);
    directory_size = (size_t)xx_data_get_u64(header + 28, 8, 0, false);
    data_offset = (int64_t)xx_data_get_u64(header + 36, 8, 0, false);
    count = xx_data_get_u32(header + 16, 4, 0, false);
    if (archive_size < XX_PTERO_BIGF_HEADER_SIZE || archive_size > span ||
        data_offset < XX_PTERO_BIGF_HEADER_SIZE ||
        data_offset > directory_offset ||
        !xx_ptero_bigf_range_within(archive_size, directory_offset,
                                     (int64_t)directory_size) ||
        directory_offset + (int64_t)directory_size != archive_size ||
        count == 0U || count > XX_PTERO_BIGF_MAX_MEMBERS ||
        directory_size < (size_t)count * (XX_PTERO_BIGF_ENTRY_FIXED + 2U))
        return NULL;

    stream = (xx_ptero_bigf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    directory = (uint8_t *)xx_mem_alloc(directory_size);
    if (!directory) goto fail;
    if (!xx_ptero_bigf_read_at(self, self->base_address + directory_offset,
                               directory, directory_size)) goto fail;

    for (index = 0U; index < count; ++index) {
        const uint8_t *record;
        const uint8_t *terminator;
        size_t name_length, header_position = position, name_start;
        uint64_t relative_offset, plain_size;
        uint32_t packed_size, flag;
        xx_ptero_bigf_member member;
        char *name;
        size_t j;
        if ((pd && xx_pd_is_stopped(pd)) ||
            position > directory_size ||
            XX_PTERO_BIGF_ENTRY_FIXED >= directory_size - position)
            goto fail;
        record = directory + position;
        name_start = position + XX_PTERO_BIGF_ENTRY_FIXED;
        terminator = (const uint8_t *)memchr(directory + name_start, 0,
                                             directory_size - name_start);
        if (!terminator) goto fail;
        name_length = (size_t)(terminator - (directory + name_start));
        if (name_length == 0U || name_length > XX_PTERO_BIGF_MAX_NAME)
            goto fail;
        position = name_start + name_length + 1U;
        relative_offset = xx_data_get_u64(record, 8, 0, false);
        plain_size = xx_data_get_u64(record + 20, 8, 0, false);
        packed_size = xx_data_get_u32(record + 28, 4, 0, false);
        flag = xx_data_get_u32(record + 32, 4, 0, false);
        if (xx_data_get_u32(record + 8, 4, 0, false) != 32U || flag > 1U ||
            relative_offset > INT64_MAX || plain_size > INT64_MAX ||
            relative_offset < (uint64_t)data_offset ||
            relative_offset > (uint64_t)directory_offset) goto fail;
        name = (char *)xx_mem_alloc(name_length + 1U);
        if (!name) goto fail;
        for (j = 0U; j < name_length; ++j) {
            const uint8_t byte = directory[name_start + j];
            if (byte < 0x20U || byte > 0x7eU) break;
            name[j] = byte == '\\' ? '/' : (char)byte;
        }
        name[name_length] = '\0';
        if (j != name_length || !xx_ptero_bigf_path_safe(name)) {
            xx_mem_free(name);
            goto fail;
        }
        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + directory_offset +
                               (int64_t)header_position;
        member.header_size = (int64_t)(position - header_position);
        member.data_offset = self->base_address + (int64_t)relative_offset;
        member.uncompressed_size = (int64_t)plain_size;
        member.compressed = flag == 1U;
        member.method = flag;
        member.timestamp = xx_data_get_u64(record + 12, 8, 0, false);
        member.compressed_size = member.compressed ? (int64_t)packed_size
                                                   : (int64_t)plain_size;
        if (!xx_ptero_bigf_range_within(directory_offset,
                                         (int64_t)relative_offset,
                                         member.compressed_size) ||
            (!member.compressed && packed_size != 0U &&
             packed_size != plain_size) ||
            (member.compressed && (packed_size < 12U ||
                                   !xx_ptero_bigf_validate_blocks(self,
                                       &member, pd)))) {
            xx_mem_free(name);
            goto fail;
        }
        if (!xx_ptero_bigf_add(stream, &member)) {
            xx_mem_free(name);
            goto fail;
        }
    }
    if (position != directory_size || stream->count != count) goto fail;
    stream->archive_size = archive_size;
    xx_mem_free(directory);
    return stream;
fail:
    xx_mem_free(directory);
    xx_ptero_bigf_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_ptero_bigf_init(xx_ptero_bigf *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PTERO_BIGF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ptero-cbf");
    xx_format_set_extension(&archive->format, "cbf");
    archive->format.check_is_valid = xx_ptero_bigf_check_is_valid;
    archive->format.handle_base_info = xx_ptero_bigf_handle_base_info;
    archive->format.get_format_size = xx_ptero_bigf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ptero_bigf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ptero_bigf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ptero_bigf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ptero_bigf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ptero_bigf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ptero_bigf_free_archive_records_reading;
    archive->format.destroy = xx_ptero_bigf_vtable_destroy;
}

xx_ptero_bigf *xx_ptero_bigf_create(xx_io_device *device, int64_t base_address) {
    xx_ptero_bigf *archive = (xx_ptero_bigf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ptero_bigf_init(archive, device, base_address);
    return archive;
}

void xx_ptero_bigf_destroy(xx_ptero_bigf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ptero_bigf_free(xx_ptero_bigf *archive) {
    if (!archive) return;
    xx_ptero_bigf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ptero_bigf_vtable_destroy(Abstractformat *self) {
    xx_ptero_bigf_destroy((xx_ptero_bigf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ptero_bigf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ptero_bigf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ptero_bigf_parse(self, pd);
    if (!stream) return false;
    xx_ptero_bigf_stream_free(stream);
    return true;
}

bool xx_ptero_bigf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ptero_bigf *archive = (xx_ptero_bigf *)self;
    xx_ptero_bigf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ptero_bigf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ptero_bigf_stream_free(stream);
    return true;
}

int64_t xx_ptero_bigf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ptero_bigf_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ptero_bigf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ptero_bigf_set_record(xx_archive_record *record,
                                 const xx_ptero_bigf_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_ptero_bigf_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
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

static const xx_var *xx_ptero_bigf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ptero_bigf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ptero_bigf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ptero_bigf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ptero_bigf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ptero_bigf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ptero_bigf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ptero_bigf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ptero_bigf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ptero_bigf_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ptero_bigf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ptero_bigf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ptero_bigf_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

static bool xx_ptero_bigf_same_path(const char *left, const char *right) {
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return *left == *right;
}

static xx_io_device *xx_ptero_bigf_open_stage(const char *target,
                                              char **stage_path) {
    char *parent;
    size_t index, cut = 0U;
    unsigned attempt;

    if (!target || !stage_path) return NULL;
    *stage_path = NULL;
    parent = xx_str_dup(target);
    if (!parent) return NULL;
    for (index = 0U; parent[index]; ++index) {
        if (parent[index] == '/' || parent[index] == '\\') cut = index + 1U;
    }
    parent[cut] = '\0';
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48];
        char *candidate;
        xx_io_device *output;

        (void)xx_rt_snprintf(suffix, sizeof(suffix),
                             ".xx_ptero_bigf.tmp.%u", attempt);
        candidate = xx_str_concat(parent, suffix);
        if (!candidate) break;
        if (xx_ptero_bigf_same_path(candidate, target)) {
            xx_str_free(candidate);
            continue;
        }
        output = xx_io_file_open(candidate, "wbx");
        if (output) {
            *stage_path = candidate;
            xx_str_free(parent);
            return output;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);
    return NULL;
}

bool xx_ptero_bigf_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ptero_bigf_stream *stream;
    const xx_ptero_bigf_member *member;
    const xx_var *path_option;
    const xx_var *limit_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    char *stage_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool overwrite = false;
    xx_io_device *output = NULL;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ptero_bigf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ptero_bigf_path_safe(member->name)) return false;
    limit_option = xx_ptero_bigf_get_option(&state->options,
                                            XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit_option && (uint64_t)member->uncompressed_size >
                            xx_var_get_u64(limit_option)) return false;
    /* Decoding currently materializes a whole member in memory. */
    limit_option = xx_ptero_bigf_get_option(&state->options,
                                            XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit_option && (uint64_t)member->uncompressed_size >
                            xx_var_get_u64(limit_option)) return false;

    path_option = xx_ptero_bigf_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ptero_bigf_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    limit_option = xx_ptero_bigf_get_option(&state->options,
                                            XX_META_ID_OPT_OVERWRITE);
    overwrite = limit_option && xx_var_get_bool(limit_option);
    if (!overwrite && xx_io_file_exists_a(target_path)) {
        xx_str_free(target_path);
        return false;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_ptero_bigf_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        size_t completed = 0U;

        output = xx_ptero_bigf_open_stage(target_path, &stage_path);
        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (result && pd && xx_pd_is_stopped(pd)) result = false;
    if (result) result = xx_io_file_replace_a(stage_path, target_path,
                                               overwrite);
    if (stage_path) {
        if (!result) (void)xx_io_file_remove_a(stage_path);
        xx_str_free(stage_path);
    }
    xx_str_free(target_path);
    return result;
}

void xx_ptero_bigf_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
