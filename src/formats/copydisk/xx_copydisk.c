/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * COPYDISK disk image.
 *
 *   +0x00  "COPYDISK"
 *   +0x08  u16  bytes per sector, always 0x0200
 *   +0x0a  22   geometry hints the flat image does not need
 *   +0x20  the raw sector image, to end of file
 *
 * Recovered from U3's recognition predicate FUN_004a3b40 (archive/494,
 * class mga, VMT 0x0049e008, slot 0 at 0x004a3b70), which tests exactly the
 * signature and the 0x0200 word, and from its open handler FUN_004a3b90,
 * which hands the rest of the file to the generic raw-image reader after
 * setting a 0x20-byte header skip.  That 0x20 is the whole format.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/copydisk/xx_copydisk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef COPYDISK
#define XX_COPYDISK_FILE_TYPE XX_FILE_TYPE_COPYDISK
#else
#define XX_COPYDISK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define COPYDISK_MAX_MEMBERS 16U

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct copydisk_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint64_t timestamp;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
    uint32_t method;
    uint32_t crc32;
    uint32_t attributes;
    uint32_t flags;
    bool has_crc;
    bool encrypted;
    bool folder;
} copydisk_member;

typedef struct copydisk_stream_s {
    copydisk_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} copydisk_stream;

static uint16_t copydisk_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t copydisk_le32(const uint8_t *b) {
    return (uint32_t)copydisk_le16(b) | ((uint32_t)copydisk_le16(b + 2U) << 16U);
}

static uint64_t copydisk_le64(const uint8_t *b) {
    return (uint64_t)copydisk_le32(b) | ((uint64_t)copydisk_le32(b + 4U) << 32U);
}

static uint32_t copydisk_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t copydisk_be64(const uint8_t *b) {
    return ((uint64_t)copydisk_be32(b) << 32U) | (uint64_t)copydisk_be32(b + 4U);
}

static bool copydisk_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool copydisk_write_all(xx_io_device *device, const void *data, size_t size,
                          xx_pd_struct *pd) {
    size_t done = 0U;
    if (!data && size != 0U) return false;
    if (!device) return true; /* verify-only pass: nothing is materialized */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Copy a run of source bytes straight through to the destination. */
static bool copydisk_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!source || offset < 0) return false;
    if (!destination) return true;
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer + done, want - done);
            if (amount <= 0 || (size_t)amount > want - done) return false;
            done += (size_t)amount;
        }
        if (!copydisk_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool copydisk_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_mem_zero(buffer, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!copydisk_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *copydisk_make_name(const char *prefix, int64_t index,
                           const char *suffix) {
    char buffer[96];
    size_t used = 0U;
    size_t at;
    char *result;
    for (at = 0U; prefix && prefix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[at];
    }
    if (index >= 0) {
        char digits[24];
        size_t count = 0U;
        int64_t value = index;
        do {
            digits[count++] = (char)('0' + (int)(value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 4U && count < sizeof(digits)) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    for (at = 0U; suffix && suffix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[at];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

/* Names that DO come from the container are normalized here: separators are
 * unified, traversal components are removed and anything a filesystem would
 * choke on becomes '_'. */
static char *copydisk_clean_name(const uint8_t *bytes, size_t size) {
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
                c == '>' || c == '?' || c == '|' || c == 0U)
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

static bool copydisk_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
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

static void copydisk_stream_free(void *opaque) {
    copydisk_stream *stream = (copydisk_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool copydisk_add_member(copydisk_stream *stream, const copydisk_member *member) {
    copydisk_member *grown;
    if (!stream || !member || stream->count >= COPYDISK_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (copydisk_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define COPYDISK_HEADER_SIZE 0x20
#define COPYDISK_MAX_IMAGE ((uint64_t)4U * 1024U * 1024U * 1024U)

/* "COPYDISK", a 0x20-byte header and then the raw sector image.  Only the
 * signature and the 512-byte sector size are field-checked, because those are
 * the only two fields the reference implementation itself validates; the
 * remaining header words are geometry hints the flat image does not need. */
static bool copydisk_parse(Abstractformat *format, copydisk_stream **result) {
    uint8_t header[COPYDISK_HEADER_SIZE];
    copydisk_stream *stream = NULL;
    copydisk_member member;
    int64_t total, size;
    uint32_t sector_size;
    uint64_t image_size;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= COPYDISK_HEADER_SIZE ||
        !copydisk_read_at(format->device, format->base_address, header,
                          sizeof(header)) ||
        xx_rt_memcmp(header, "COPYDISK", 8U) != 0)
        return false;

    sector_size = copydisk_le16(header + 8U);
    if (sector_size != 0x200U) return false;

    image_size = (uint64_t)(size - COPYDISK_HEADER_SIZE);
    /* The payload is whole sectors; anything else is not this container. */
    if (image_size == 0U || (image_size % sector_size) != 0U ||
        image_size > COPYDISK_MAX_IMAGE)
        return false;

    stream = (copydisk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->aux0 = sector_size;

    xx_mem_zero(&member, sizeof(member));
    member.name = copydisk_make_name("disk", -1, ".img");
    if (!member.name) goto fail;
    member.header_offset = format->base_address;
    member.header_size = COPYDISK_HEADER_SIZE;
    member.data_offset = format->base_address + COPYDISK_HEADER_SIZE;
    member.packed_size = (int64_t)image_size;
    member.unpacked_size = image_size;
    member.method = 0U; /* stored */
    if (!copydisk_add_member(stream, &member)) {
        xx_mem_free(member.name);
        goto fail;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    copydisk_stream_free(stream);
    return false;
}

static bool copydisk_write_member(Abstractformat *format,
                                  copydisk_stream *stream,
                                  const copydisk_member *member,
                                  xx_io_device *destination,
                                  xx_pd_struct *pd) {
    (void)stream;
    if (!format || !member) return false;
    return copydisk_copy_range(format->device, member->data_offset,
                               member->unpacked_size, destination, pd);
}

static bool copydisk_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *copydisk_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool copydisk_set_record(xx_archive_record *record,
                           const copydisk_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

void xx_copydisk_init(xx_copydisk *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_COPYDISK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-copydisk-image");
    xx_format_set_extension(&archive->format, "dsk");
    archive->format.check_is_valid = xx_copydisk_check_is_valid;
    archive->format.handle_base_info = xx_copydisk_handle_base_info;
    archive->format.get_format_size = xx_copydisk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_copydisk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_copydisk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_copydisk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_copydisk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_copydisk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_copydisk_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_copydisk *xx_copydisk_create(xx_io_device *device, int64_t base_address) {
    xx_copydisk *archive = (xx_copydisk *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_copydisk_init(archive, device, base_address);
    return archive;
}

void xx_copydisk_destroy(xx_copydisk *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_copydisk_free(xx_copydisk *archive) {
    if (!archive) return;
    xx_copydisk_destroy(archive);
    xx_mem_free(archive);
}

bool xx_copydisk_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    copydisk_stream *stream;
    (void)pd;
    if (!copydisk_parse(format, &stream)) return false;
    copydisk_stream_free(stream);
    return true;
}

bool xx_copydisk_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    copydisk_stream *stream;
    xx_copydisk *archive;
    (void)pd;
    if (!format || !copydisk_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_copydisk *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_COPYDISK_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    copydisk_stream_free(stream);
    return true;
}

int64_t xx_copydisk_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_copydisk_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_copydisk_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_copydisk_handle_base_info(format, pd))
               ? ((xx_copydisk *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_copydisk_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    copydisk_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!copydisk_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        copydisk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = copydisk_stream_free;
    state->total_records = stream->count;
    if (!copydisk_copy_options(&state->options, options) ||
        !copydisk_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_copydisk_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_copydisk_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    copydisk_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (copydisk_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        copydisk_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_copydisk_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    copydisk_stream *stream;
    copydisk_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (copydisk_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!copydisk_safe_output_name(member->name)) return false;
    path_option = copydisk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return copydisk_write_member(format, stream, member, NULL, pd);
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
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    if (!destination) goto done;
    result = copydisk_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_copydisk_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
