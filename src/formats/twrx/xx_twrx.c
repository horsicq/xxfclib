/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TWRX installer container (*.TZF): a chain of self-describing "TWRX" blocks
 * that tiles the file.  xx_twrx.h carries the field table and the evidence.
 *
 * Method 0 is stored and is decoded; its anchor is the container's own claim
 * that packed and unpacked sizes agree, which holds for every stored member
 * of the reference corpus.  Methods 6 and 8 are the producer's own codecs and
 * are not identified, so those members are listed and unpack refuses.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/twrx/xx_twrx.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as TWRX is registered there. */
#ifdef TWRX
#define XX_TWRX_FILE_TYPE XX_FILE_TYPE_TWRX
#else
#define XX_TWRX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define TWRX_BLOCK_HEADER_SIZE 0x1e
#define TWRX_VERSION 0x0100U
#define TWRX_METHOD_STORED 0U
#define TWRX_MAX_NAME 255U
#define TWRX_MAX_ENTRIES 65536U
#define TWRX_MAX_MEMBER ((int64_t)512 * 1024 * 1024)

typedef struct twrx_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    int64_t unpacked_size;
    uint32_t stamp_low;
    uint32_t stamp_high;
    uint16_t method;
} twrx_member;

typedef struct twrx_stream_s {
    twrx_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} twrx_stream;

static uint16_t twrx_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t twrx_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool twrx_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Member names are DOS 8.3 ANSI and carry national high bytes, which are
 * kept verbatim for the caller's code page.  Only separators, traversal
 * components and characters a path may not hold are made harmless. */
static char *twrx_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start, end, component_start;
        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && bytes[start] == '.'))
            continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|')
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool twrx_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void twrx_stream_free(void *opaque) {
    twrx_stream *stream = (twrx_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool twrx_add_member(twrx_stream *stream, const twrx_member *member) {
    if (!stream || !member || stream->count >= (size_t)TWRX_MAX_ENTRIES)
        return false;
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity == 0U ? 16U : stream->capacity * 2U;
        twrx_member *grown;
        if (capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (twrx_member *)(stream->items
                                    ? xx_mem_realloc(stream->items,
                                                     capacity * sizeof(*grown))
                                    : xx_mem_alloc(capacity * sizeof(*grown)));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

static bool twrx_parse(Abstractformat *format, twrx_stream **result) {
    uint8_t header[TWRX_BLOCK_HEADER_SIZE];
    uint8_t *raw_name = NULL;
    twrx_stream *stream = NULL;
    int64_t total, size, cursor = 0;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)TWRX_BLOCK_HEADER_SIZE + 1) return false;
    stream = (twrx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    while (cursor < size) {
        twrx_member member;
        int64_t packed, unpacked, name_length, data_offset;
        if (size - cursor < (int64_t)TWRX_BLOCK_HEADER_SIZE ||
            !twrx_read_at(format->device, format->base_address + cursor,
                          header, sizeof(header))) goto fail;
        if (xx_rt_memcmp(header, "TWRX", 4U) != 0) goto fail;
        /* Only one version word exists; anything else is a layout this
         * reader has never been validated against. */
        if (twrx_le16(header + 4U) != TWRX_VERSION) goto fail;
        if (twrx_le16(header + 6U) != 0U) goto fail;
        packed = (int64_t)twrx_le32(header + 0x12U);
        unpacked = (int64_t)twrx_le32(header + 0x16U);
        name_length = (int64_t)twrx_le32(header + 0x1aU);
        if (name_length <= 0 || name_length > (int64_t)TWRX_MAX_NAME)
            goto fail;
        if (packed < 0 || packed > TWRX_MAX_MEMBER) goto fail;
        if (unpacked < 0 || unpacked > TWRX_MAX_MEMBER) goto fail;
        if (name_length > size - cursor - (int64_t)TWRX_BLOCK_HEADER_SIZE)
            goto fail;
        data_offset = cursor + (int64_t)TWRX_BLOCK_HEADER_SIZE + name_length;
        if (packed > size - data_offset) goto fail;
        raw_name = (uint8_t *)xx_mem_alloc((size_t)name_length);
        if (!raw_name ||
            !twrx_read_at(format->device,
                          format->base_address + cursor +
                              TWRX_BLOCK_HEADER_SIZE,
                          raw_name, (size_t)name_length)) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = twrx_normalize_name(raw_name, (size_t)name_length);
        xx_mem_free(raw_name);
        raw_name = NULL;
        if (!member.name) goto fail;
        member.method = twrx_le16(header + 8U);
        member.stamp_low = twrx_le32(header + 0x0aU);
        member.stamp_high = twrx_le32(header + 0x0eU);
        member.header_offset = format->base_address + cursor;
        member.data_offset = format->base_address + data_offset;
        member.packed_size = packed;
        member.unpacked_size = unpacked;
        /* A stored member that disagrees with itself about its own length is
         * not a stored member.  This is the only cross-check the container
         * offers, and it holds for every stored member of the corpus. */
        if (member.method == TWRX_METHOD_STORED && packed != unpacked) {
            xx_str_free(member.name);
            goto fail;
        }
        if (!twrx_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        /* Every block consumes at least its own header, so the walk always
         * advances and the loop always terminates. */
        cursor = data_offset + packed;
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (raw_name) xx_mem_free(raw_name);
    twrx_stream_free(stream);
    return false;
}

static bool twrx_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *twrx_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool twrx_set_record(xx_archive_record *record,
                            const twrx_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->stamp_low) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_twrx_init(xx_twrx *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TWRX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-twrx");
    xx_format_set_extension(&archive->format, "tzf");
    archive->format.check_is_valid = xx_twrx_check_is_valid;
    archive->format.handle_base_info = xx_twrx_handle_base_info;
    archive->format.get_format_size = xx_twrx_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_twrx_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_twrx_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_twrx_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_twrx_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_twrx_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_twrx_free_archive_records_reading;
}

xx_twrx *xx_twrx_create(xx_io_device *device, int64_t base_address) {
    xx_twrx *archive = (xx_twrx *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_twrx_init(archive, device, base_address);
    return archive;
}

void xx_twrx_destroy(xx_twrx *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_twrx_free(xx_twrx *archive) {
    if (!archive) return;
    xx_twrx_destroy(archive);
    xx_mem_free(archive);
}

bool xx_twrx_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    twrx_stream *stream;
    (void)pd;
    if (!twrx_parse(format, &stream)) return false;
    twrx_stream_free(stream);
    return true;
}

bool xx_twrx_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    twrx_stream *stream;
    xx_twrx *archive;
    (void)pd;
    if (!format || !twrx_parse(format, &stream)) return false;
    archive = (xx_twrx *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    twrx_stream_free(stream);
    return true;
}

int64_t xx_twrx_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_twrx_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_twrx_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_twrx_handle_base_info(format, pd))
               ? ((xx_twrx *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_twrx_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    twrx_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!twrx_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        twrx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = twrx_stream_free;
    state->total_records = stream->count;
    if (!twrx_copy_options(&state->options, options) ||
        !twrx_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_twrx_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_twrx_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    twrx_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (twrx_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = twrx_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_twrx_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    twrx_stream *stream;
    twrx_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (twrx_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    /* Methods 6 and 8 carry the producer's own codecs, which have not been
     * identified.  Refusing beats writing plausible-looking garbage. */
    if (member->method != TWRX_METHOD_STORED) return false;
    if (!twrx_safe_output_name(member->name)) return false;
    if (member->unpacked_size != member->packed_size) return false;
    if ((uint64_t)member->packed_size > (uint64_t)SIZE_MAX) return false;
    plain_size = (size_t)member->packed_size;
    plain = (uint8_t *)xx_mem_alloc(plain_size != 0U ? plain_size : 1U);
    if (!plain) return false;
    if (plain_size != 0U &&
        !twrx_read_at(format->device, member->data_offset, plain, plain_size))
        goto done;
    path_option = twrx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_twrx_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
