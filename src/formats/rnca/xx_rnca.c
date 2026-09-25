/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Rob Northen "RNCA" multi-member archive.
 *
 * Layout (recovered from the reference unpacker's recognition predicate and
 * confirmed against the RNC corpus; see xx_rnca.h for the field list):
 *
 *   +0   "RNCA"
 *   +4   uint16be  first member offset == header + directory size
 *   +6   uint16be  directory check word
 *   +8   uint16be  copy of the +4 value
 *   +10  uint8     zero
 *   +11  directory: (name, NUL, uint32be member offset) repeated, ended by a
 *        zero byte in the name position
 *
 * Each member is an independent RNC stream.  Methods 1 and 2 are the ordinary
 * ProPack stream, which the RNC reader already decodes, so a nested xx_rnc is
 * constructed over the same device at the member's offset rather than
 * duplicating the decoder.  Method 0 is a stored stream - an 8 byte header
 * whose uint32be length is followed by that many raw bytes - and is copied
 * here.
 *
 * There is no length field for a member, only the offset of the next one, so
 * every member's own header must agree with the span the directory implies:
 * that agreement is the structural check that makes the four-byte magic
 * trustworthy.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rnca/xx_rnca.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/formats/rnc/xx_rnc.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef RNCA
#define XX_RNCA_FILE_TYPE XX_FILE_TYPE_RNCA
#else
#define XX_RNCA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_RNCA_HEADER_SIZE 11U
/* The first-member offset is a uint16, so the header plus the whole directory
 * is bounded at 65535 bytes by the field's own width; the smallest directory
 * that can hold one entry is the fixed header plus its terminator. */
#define XX_RNCA_MIN_DIRECTORY 12U
#define XX_RNCA_MAX_MEMBERS 65536U
#define XX_RNCA_MAX_NAME 255U
/* Stored ("RNC\0") members carry an 8 byte header; packed ones carry 18. */
#define XX_RNCA_STORED_HEADER 8U
#define XX_RNCA_PACKED_HEADER 18U
/* A stored member is copied through a bounded buffer rather than in one
 * allocation the size of the declared length. */
#define XX_RNCA_COPY_CHUNK 65536U

typedef struct rnca_member_s {
    char *name;
    int64_t header_offset; /* absolute offset of the member's RNC header */
    int64_t data_offset;   /* absolute offset of the member's payload */
    int64_t span;          /* bytes up to the next member, or to the end */
    uint64_t packed_size;  /* declared packed length */
    uint64_t unpacked_size;/* declared plain length */
    uint8_t method;        /* 0 stored, 1 and 2 ProPack */
} rnca_member;

typedef struct rnca_stream_s {
    rnca_member *items;
    size_t count;
    size_t index;
    int64_t directory_size;
    int64_t archive_size;
} rnca_stream;

static uint16_t rnca_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t rnca_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool rnca_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Member names are DOS 8.3 byte strings.  Only the filesystem-facing
 * representation is normalized: separators, traversal components and bytes
 * that cannot appear in a path component become '_'. */
static char *rnca_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > XX_RNCA_MAX_NAME) return NULL;
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
        if (end == start ||
            (end - start == 1U && bytes[start] == '.')) continue;
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
            if (c < 0x20U || c > 0x7EU || c == '"' || c == '*' || c == ':' ||
                c == '<' || c == '>' || c == '?' || c == '|')
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

static bool rnca_safe_output_name(const char *name) {
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

static void rnca_stream_free(void *opaque) {
    rnca_stream *stream = (rnca_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool rnca_add_member(rnca_stream *stream, const rnca_member *member) {
    rnca_member *grown;
    if (!stream || !member || stream->count >= XX_RNCA_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (rnca_member *)xx_mem_realloc(stream->items,
                                          (stream->count + 1U) *
                                              sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Read a member's own RNC header and check that it describes exactly the
 * extent the directory left for it.  span is the distance to the next member;
 * a member may be followed by a little alignment padding, so the header must
 * fit inside the span rather than fill it. */
static bool rnca_fill_member(xx_io_device *device, rnca_member *member,
                             int64_t span) {
    uint8_t raw[XX_RNCA_PACKED_HEADER];
    if (span < (int64_t)XX_RNCA_STORED_HEADER ||
        !rnca_read_at(device, member->header_offset, raw,
                      XX_RNCA_STORED_HEADER) ||
        xx_rt_memcmp(raw, "RNC", 3U) != 0)
        return false;
    member->method = raw[3];
    member->span = span;
    if (member->method == 0U) {
        member->unpacked_size = rnca_be32(raw + 4U);
        member->packed_size = member->unpacked_size;
        member->data_offset =
            member->header_offset + (int64_t)XX_RNCA_STORED_HEADER;
        return member->packed_size <=
               (uint64_t)(span - (int64_t)XX_RNCA_STORED_HEADER);
    }
    if (member->method != 1U && member->method != 2U) return false;
    if (span < (int64_t)XX_RNCA_PACKED_HEADER ||
        !rnca_read_at(device, member->header_offset, raw, sizeof(raw)))
        return false;
    member->unpacked_size = rnca_be32(raw + 4U);
    member->packed_size = rnca_be32(raw + 8U);
    member->data_offset =
        member->header_offset + (int64_t)XX_RNCA_PACKED_HEADER;
    return member->packed_size != 0U && member->unpacked_size != 0U &&
           member->packed_size <=
               (uint64_t)(span - (int64_t)XX_RNCA_PACKED_HEADER);
}

static bool rnca_parse(Abstractformat *format, rnca_stream **result) {
    uint8_t header[XX_RNCA_HEADER_SIZE];
    uint8_t *directory = NULL;
    rnca_stream *stream = NULL;
    int64_t total, size;
    uint32_t directory_size, cursor;
    size_t index;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)XX_RNCA_HEADER_SIZE ||
        !rnca_read_at(format->device, format->base_address, header,
                      sizeof(header)) ||
        xx_rt_memcmp(header, "RNCA", 4U) != 0)
        return false;

    directory_size = rnca_be16(header + 4U);
    /* The two copies of the first-member offset must agree, the trailing byte
     * of the fixed header must be zero, and the directory must both hold at
     * least one entry and end before the end of the file. */
    if (directory_size != (uint32_t)rnca_be16(header + 8U) ||
        header[10] != 0U || directory_size < XX_RNCA_MIN_DIRECTORY ||
        (uint64_t)directory_size >= (uint64_t)size)
        return false;

    directory = (uint8_t *)xx_mem_alloc(directory_size);
    if (!directory) return false;
    if (!rnca_read_at(format->device, format->base_address, directory,
                      directory_size))
        goto fail;

    stream = (rnca_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->directory_size = (int64_t)directory_size;

    cursor = XX_RNCA_HEADER_SIZE;
    while (cursor < directory_size && directory[cursor] != 0U) {
        uint32_t name_start = cursor;
        rnca_member member;
        uint32_t offset;
        while (cursor < directory_size && directory[cursor] != 0U) ++cursor;
        /* name, its NUL, and the four offset bytes must all be inside the
         * directory the header declared */
        if (cursor >= directory_size ||
            (uint64_t)cursor + 5U > (uint64_t)directory_size ||
            cursor - name_start > XX_RNCA_MAX_NAME || cursor == name_start)
            goto fail;
        offset = rnca_be32(directory + cursor + 1U);
        cursor += 5U;
        xx_mem_zero(&member, sizeof(member));
        if ((uint64_t)offset < (uint64_t)directory_size ||
            (uint64_t)offset >= (uint64_t)size)
            goto fail;
        member.header_offset = format->base_address + (int64_t)offset;
        member.name = rnca_normalize_name(directory + name_start,
                                          cursor - 5U - name_start);
        if (!member.name) goto fail;
        if (!rnca_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }
    /* The directory ends with a zero byte in the name position, and that byte
     * is the last one before the first member. */
    if (stream->count == 0U || cursor != directory_size - 1U ||
        directory[cursor] != 0U)
        goto fail;

    for (index = 0U; index < stream->count; ++index) {
        rnca_member *member = &stream->items[index];
        int64_t next = index + 1U < stream->count
                           ? stream->items[index + 1U].header_offset
                           : format->base_address + size;
        if (next <= member->header_offset ||
            !rnca_fill_member(format->device, member,
                              next - member->header_offset))
            goto fail;
    }
    /* The first member must start exactly where the directory ends, or the
     * two copies of the offset in the header describe nothing. */
    if (stream->items[0].header_offset !=
        format->base_address + (int64_t)directory_size)
        goto fail;

    stream->archive_size = size;
    xx_mem_free(directory);
    *result = stream;
    return true;
fail:
    if (directory) xx_mem_free(directory);
    rnca_stream_free(stream);
    return false;
}

static bool rnca_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *rnca_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool rnca_set_record(xx_archive_record *record,
                            const rnca_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->method == 0U
                              ? (int64_t)XX_RNCA_STORED_HEADER
                              : (int64_t)XX_RNCA_PACKED_HEADER;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Copy a stored member through a fixed buffer: the declared length is a file
 * controlled 32-bit field and must never size an allocation. */
static bool rnca_copy_stored(xx_io_device *source, const rnca_member *member,
                             xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t buffer[XX_RNCA_COPY_CHUNK];
    uint64_t remaining = member->packed_size;
    int64_t at = member->data_offset;
    while (remaining != 0U) {
        size_t chunk = remaining > (uint64_t)sizeof(buffer)
                           ? sizeof(buffer) : (size_t)remaining;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !rnca_read_at(source, at, buffer, chunk))
            return false;
        while (written < chunk) {
            ssize_t amount = xx_io_write(destination, buffer + written,
                                         chunk - written);
            if (amount <= 0 || (size_t)amount > chunk - written) return false;
            written += (size_t)amount;
        }
        remaining -= chunk;
        at += (int64_t)chunk;
    }
    return true;
}

/* Decode one member into an already opened destination device. */
static bool rnca_write_member(Abstractformat *format,
                              const rnca_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    xx_rnc nested;
    bool result;
    if (member->method == 0U)
        return rnca_copy_stored(format->device, member, destination, pd);
    /* Methods 1 and 2 are the ordinary ProPack stream; reuse its reader on
     * the same device rather than carrying a second copy of the decoder. */
    xx_rnc_init(&nested, format->device, member->header_offset);
    result = xx_rnc_unpack_to_device(&nested, destination, pd);
    xx_rnc_destroy(&nested);
    return result;
}

void xx_rnca_init(xx_rnca *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_RNCA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-rnca");
    xx_format_set_extension(&archive->format, "rnc");
    archive->format.check_is_valid = xx_rnca_check_is_valid;
    archive->format.handle_base_info = xx_rnca_handle_base_info;
    archive->format.get_format_size = xx_rnca_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rnca_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rnca_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rnca_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rnca_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rnca_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rnca_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_rnca *xx_rnca_create(xx_io_device *device, int64_t base_address) {
    xx_rnca *archive = (xx_rnca *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_rnca_init(archive, device, base_address);
    return archive;
}

void xx_rnca_destroy(xx_rnca *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_rnca_free(xx_rnca *archive) {
    if (!archive) return;
    xx_rnca_destroy(archive);
    xx_mem_free(archive);
}

bool xx_rnca_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    rnca_stream *stream;
    (void)pd;
    if (!rnca_parse(format, &stream)) return false;
    rnca_stream_free(stream);
    return true;
}

bool xx_rnca_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    rnca_stream *stream;
    xx_rnca *archive;
    (void)pd;
    if (!format || !rnca_parse(format, &stream)) return false;
    archive = (xx_rnca *)format;
    archive->number_of_records = stream->count;
    archive->directory_size = stream->directory_size;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    rnca_stream_free(stream);
    return true;
}

int64_t xx_rnca_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rnca_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_rnca_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rnca_handle_base_info(format, pd))
               ? ((xx_rnca *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_rnca_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    rnca_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!rnca_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        rnca_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = rnca_stream_free;
    state->total_records = stream->count;
    if (!rnca_copy_options(&state->options, options) ||
        !rnca_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_rnca_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_rnca_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    rnca_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (rnca_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = rnca_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rnca_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    rnca_stream *stream;
    rnca_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (rnca_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!rnca_safe_output_name(member->name)) return false;
    path_option = rnca_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the member decodes without keeping it. */
        if (member->method == 0U) return true;
        {
            xx_rnc nested;
            xx_rnc_init(&nested, format->device, member->header_offset);
            result = xx_rnc_check_is_valid(&nested.format, pd);
            xx_rnc_destroy(&nested);
        }
        return result;
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
        created = destination != NULL;
        if (!destination) goto done;
        result = rnca_write_member(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_rnca_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

uint64_t xx_rnca_get_number_of_records(const xx_rnca *archive) {
    return archive ? archive->number_of_records : 0U;
}

int64_t xx_rnca_get_directory_size(const xx_rnca *archive) {
    return archive ? archive->directory_size : -1;
}
