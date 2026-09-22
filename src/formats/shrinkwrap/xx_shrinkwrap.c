/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Shrink-Wrap floppy image, which is Apple's "Disk Copy
 * 4.2" container.  XArchive has no module for it (its xpmdiskcopy is the
 * unrelated PC "PM Diskcopy"), so the layout below was derived from the corpus
 * under F:\ARC\ARC\SHRINK-WRAP and confirmed by the format's own checksums.
 *
 * 84-byte big-endian header:
 *
 *   +0    u8    volume name length, 0..63
 *   +1    63    volume name, space or NUL padded
 *   +0x40 u32   data size in bytes, a multiple of 512
 *   +0x44 u32   tag size in bytes - the GCR tag bytes, 12 per sector on the
 *               400K/800K formats and 0 on the 720K/1440K ones
 *   +0x48 u32   data checksum
 *   +0x4C u32   tag checksum - taken over the tag bytes FROM OFFSET 12, the
 *               first sector's tags being excluded by the format
 *   +0x50 u8    disk format: 0 = 400K, 1 = 800K, 2 = 720K, 3 = 1440K
 *   +0x51 u8    format byte (0x02, 0x12, 0x22 or 0x24)
 *   +0x52 u16   0x0100, the private word that makes the header identifiable
 *
 * Then `data size` bytes of sector data and `tag size` bytes of tags.  Some
 * writers pad the file out past that; the payload is what the header declares,
 * not what is left in the file.
 *
 * Both checksums use Disk Copy's own rolling sum: add each big-endian 16-bit
 * word into a 32-bit accumulator, then rotate the accumulator right by one.
 * They are verified on unpack, which is what tells a correct read from a
 * plausible one.
 *
 * The reader publishes the sector data as one member and, when the image
 * carries them, the tag bytes as a second.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/shrinkwrap/xx_shrinkwrap.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef SHRINKWRAP
#define XX_SHRINKWRAP_FILE_TYPE XX_FILE_TYPE_SHRINKWRAP
#else
#define XX_SHRINKWRAP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SW_HEADER_SIZE 84
#define SW_NAME_MAX 63U
#define SW_SECTOR_SIZE 512
#define SW_MAX_DATA INT64_C(0x10000000)
#define SW_MAX_TAG INT64_C(0x1000000)
#define SW_PRIVATE UINT16_C(0x0100)

typedef struct sw_member_s {
    char *name;
    int64_t data_offset;
    int64_t size;
    uint32_t checksum;
    int64_t checksum_skip; /* bytes excluded from the front of the sum */
    bool is_tags;
} sw_member;

typedef struct sw_stream_s {
    sw_member items[2];
    size_t count;
    size_t index;
    int64_t archive_size;
    int64_t data_size;
    int64_t tag_size;
    uint8_t disk_format;
    uint8_t format_byte;
} sw_stream;

static uint16_t sw_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static uint32_t sw_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool sw_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Disk Copy's rolling checksum: add a big-endian word, then rotate right. */
static uint32_t sw_checksum(const uint8_t *data, size_t size) {
    uint32_t sum = 0U;
    size_t index;
    for (index = 0U; index + 1U < size; index += 2U) {
        sum = (sum + (uint32_t)sw_be16(data + index)) & UINT32_C(0xFFFFFFFF);
        sum = (sum >> 1U) | ((sum & 1U) << 31U);
    }
    return sum;
}

/* The Mac volume name is free-form text; only what a host filesystem would
 * object to is neutralized. */
static char *sw_make_name(const uint8_t *header, const char *suffix) {
    char buffer[SW_NAME_MAX + 8U];
    size_t length = header[0];
    size_t input, output = 0U;
    if (length > SW_NAME_MAX) length = SW_NAME_MAX;
    for (input = 0U; input < length; ++input) {
        uint8_t c = header[1U + input];
        if (c == 0U) break;
        if (c < 0x20U || c == '/' || c == '\\' || c == '"' || c == '*' ||
            c == ':' || c == '<' || c == '>' || c == '?' || c == '|')
            buffer[output++] = '_';
        else
            buffer[output++] = (char)c;
    }
    while (output != 0U &&
           (buffer[output - 1U] == ' ' || buffer[output - 1U] == '.'))
        --output;
    buffer[output] = 0;
    return xx_str_concat(output != 0U ? buffer : "disk", suffix);
}

static void sw_stream_free(void *opaque) {
    sw_stream *stream = (sw_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    xx_mem_free(stream);
}

static bool sw_parse(Abstractformat *format, sw_stream **result) {
    uint8_t header[SW_HEADER_SIZE];
    sw_stream *stream;
    int64_t total, size, data_size, tag_size;
    uint8_t disk_format, format_byte;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < SW_HEADER_SIZE + SW_SECTOR_SIZE) return false;
    if (!sw_read_at(format->device, format->base_address, header,
                    sizeof(header)))
        return false;

    if (sw_be16(header + 0x52) != SW_PRIVATE) return false;
    if (header[0] > SW_NAME_MAX) return false;
    disk_format = header[0x50];
    format_byte = header[0x51];
    if (disk_format > 3U) return false;
    if (format_byte != 0x02U && format_byte != 0x12U && format_byte != 0x22U &&
        format_byte != 0x24U)
        return false;

    data_size = (int64_t)sw_be32(header + 0x40);
    tag_size = (int64_t)sw_be32(header + 0x44);
    /* Bound both declared extents against the file before anything is read. */
    if (data_size <= 0 || data_size > SW_MAX_DATA ||
        (data_size % SW_SECTOR_SIZE) != 0)
        return false;
    if (tag_size < 0 || tag_size > SW_MAX_TAG) return false;
    if (size - SW_HEADER_SIZE < data_size) return false;
    if (size - SW_HEADER_SIZE - data_size < tag_size) return false;

    stream = (sw_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->data_size = data_size;
    stream->tag_size = tag_size;
    stream->disk_format = disk_format;
    stream->format_byte = format_byte;
    stream->archive_size = SW_HEADER_SIZE + data_size + tag_size;

    stream->items[0].name = sw_make_name(header, ".img");
    if (!stream->items[0].name) goto fail;
    stream->items[0].data_offset = format->base_address + SW_HEADER_SIZE;
    stream->items[0].size = data_size;
    stream->items[0].checksum = sw_be32(header + 0x48);
    stream->items[0].checksum_skip = 0;
    stream->items[0].is_tags = false;
    stream->count = 1U;

    if (tag_size > 0) {
        stream->items[1].name = sw_make_name(header, ".tags");
        if (!stream->items[1].name) goto fail;
        stream->items[1].data_offset =
            format->base_address + SW_HEADER_SIZE + data_size;
        stream->items[1].size = tag_size;
        stream->items[1].checksum = sw_be32(header + 0x4C);
        /* The format excludes the first sector's twelve tag bytes. */
        stream->items[1].checksum_skip = (tag_size > 12) ? 12 : tag_size;
        stream->items[1].is_tags = true;
        stream->count = 2U;
    }

    *result = stream;
    return true;
fail:
    sw_stream_free(stream);
    return false;
}

static bool sw_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *sw_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sw_set_record(xx_archive_record *record, const sw_stream *stream,
                          const sw_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->data_offset - SW_HEADER_SIZE;
    record->header_size = SW_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->checksum) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          stream->format_byte) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          stream->disk_format) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool sw_read_member(Abstractformat *format, const sw_member *member,
                           uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    size_t size;
    if (!format || !member || !plain || !plain_size || member->size <= 0 ||
        (uint64_t)member->size > SIZE_MAX)
        return false;
    size = (size_t)member->size;
    output = (uint8_t *)xx_mem_alloc(size);
    if (!output) return false;
    if (!sw_read_at(format->device, member->data_offset, output, size)) {
        xx_mem_free(output);
        return false;
    }
    if (sw_checksum(output + member->checksum_skip,
                    size - (size_t)member->checksum_skip) !=
        member->checksum) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = size;
    return true;
}

void xx_shrinkwrap_init(xx_shrinkwrap *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_SHRINKWRAP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-apple-diskimage");
    xx_format_set_extension(&archive->format, "image");
    archive->format.check_is_valid = xx_shrinkwrap_check_is_valid;
    archive->format.handle_base_info = xx_shrinkwrap_handle_base_info;
    archive->format.get_format_size = xx_shrinkwrap_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_shrinkwrap_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_shrinkwrap_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_shrinkwrap_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_shrinkwrap_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_shrinkwrap_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_shrinkwrap_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_shrinkwrap *xx_shrinkwrap_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_shrinkwrap *archive = (xx_shrinkwrap *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_shrinkwrap_init(archive, device, base_address);
    return archive;
}

void xx_shrinkwrap_destroy(xx_shrinkwrap *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_shrinkwrap_free(xx_shrinkwrap *archive) {
    if (!archive) return;
    xx_shrinkwrap_destroy(archive);
    xx_mem_free(archive);
}

bool xx_shrinkwrap_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    sw_stream *stream;
    (void)pd;
    if (!sw_parse(format, &stream)) return false;
    sw_stream_free(stream);
    return true;
}

bool xx_shrinkwrap_handle_base_info(Abstractformat *format,
                                    xx_pd_struct *pd) {
    sw_stream *stream;
    xx_shrinkwrap *archive;
    (void)pd;
    if (!format || !sw_parse(format, &stream)) return false;
    archive = (xx_shrinkwrap *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->data_size = stream->data_size;
    archive->tag_size = stream->tag_size;
    archive->disk_format = stream->disk_format;
    archive->format_byte = stream->format_byte;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    sw_stream_free(stream);
    return true;
}

int64_t xx_shrinkwrap_get_format_size(Abstractformat *format,
                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_shrinkwrap_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_shrinkwrap_get_number_of_archive_records(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_shrinkwrap_handle_base_info(format, pd))
               ? ((xx_shrinkwrap *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_shrinkwrap_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    sw_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!sw_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sw_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sw_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!sw_copy_options(&state->options, options) ||
        !sw_set_record(&state->current_record, stream, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_shrinkwrap_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_shrinkwrap_archive_record_move_to_next(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    sw_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (sw_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = sw_set_record(&state->current_record, stream,
                                      &stream->items[stream->index]);
    return state->has_record;
}

bool xx_shrinkwrap_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    sw_stream *stream;
    sw_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (sw_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!sw_read_member(format, member, &plain, &plain_size)) goto done;
    path_option = sw_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!path) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
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

void xx_shrinkwrap_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
