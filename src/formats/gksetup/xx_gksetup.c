/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GkSetup installer data file (SETUP.DAT / SETUP.DA_), the payload container
 * of the freeware "GkSetup" Win16/Win32 setup builder.  Layout taken from the
 * XArchive reference module installers/xgksetup.cpp.
 *
 * Fixed header:
 *   0x0000  "This is a binary data file. Keep out !" 0x1A   (39 bytes)
 *   0x0050  "GK"
 *   0x0128  uint32 data_offset -- 0x600 or 0x700, where the chain starts.
 *           Everything between the banner and it is the builder's script/UI
 *           blob.  UNINSTAL.DAT carries the same banner and signature but a
 *           nonsense value here, which is what separates the two.
 *
 * From data_offset the file is a flat chain with no index and no member
 * count; it simply runs until a record stops parsing.  One record is a
 * 0x124-byte WIN32_FIND_DATA-shaped descriptor followed by the payload:
 *   0x0000  char     name[0x104]   NUL padded, may carry '\' path segments
 *   0x0104  uint32   attributes    FILE_ATTRIBUTE_* (0x10 == directory)
 *   0x0108  FILETIME creation
 *   0x0110  FILETIME last access
 *   0x0118  FILETIME last write
 *   0x0120  int32    size          payload length, 0 for a directory
 *
 * Some writers emit an extra all-zero uint32 between the descriptor and the
 * payload.  Nothing announces it, so the first two records are probed under
 * the padded reading and the answer is then held for the whole chain.
 *
 * Directories are not members: a directory record pushes its name onto the
 * current path and the literal name ".." pops one level.  Everything else is
 * a file stored verbatim, so unpacking is a byte copy.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gksetup/xx_gksetup.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef GKSETUP
#define XX_GKSETUP_FILE_TYPE XX_FILE_TYPE_GKSETUP
#else
#define XX_GKSETUP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define GK_BANNER_SIZE 39U
#define GK_HEADER_SIZE 0x600
#define GK_SIGNATURE_OFFSET 0x50
#define GK_DATAOFFSET_OFFSET 0x128
#define GK_RECORD_SIZE 0x124
#define GK_NAME_FIELD_SIZE 0x104
#define GK_ATTRIBUTES_OFFSET 0x104
#define GK_SIZE_OFFSET 0x120
#define GK_ATTRIBUTE_DIRECTORY 0x10U
#define GK_DATAOFFSET_A 0x600
#define GK_DATAOFFSET_B 0x700
/* The chain carries no count, so the walk is bounded only by the file.  One
 * record costs 0x124 bytes plus its payload, so this ceiling is far above
 * anything a real producer emits and merely stops a pathological loop. */
#define GK_MAX_RECORDS 1000000U
/* Longest path the accumulated directory prefix may reach.  A cycle of
 * directory records could otherwise grow it without bound. */
#define GK_MAX_PATH 4096U

static const char g_gk_banner[GK_BANNER_SIZE] = {
    'T', 'h', 'i', 's', ' ', 'i', 's', ' ', 'a', ' ', 'b', 'i', 'n',
    'a', 'r', 'y', ' ', 'd', 'a', 't', 'a', ' ', 'f', 'i', 'l', 'e',
    '.', ' ', 'K', 'e', 'e', 'p', ' ', 'o', 'u', 't', ' ', '!', '\x1a'};

typedef struct gk_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;
    uint32_t attributes;
} gk_member;

typedef struct gk_stream_s {
    gk_member *items;
    size_t count;
    size_t index;
    int64_t data_offset;
    int64_t archive_size;
    bool has_padding;
} gk_stream;

static uint16_t gk_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t gk_le32(const uint8_t *bytes) {
    return (uint32_t)gk_le16(bytes) | ((uint32_t)gk_le16(bytes + 2U) << 16U);
}

static bool gk_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Every offset/size pair that comes out of the file goes through this before
 * it is used to read, allocate, or advance the walk. */
static bool gk_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Append one path component, mapping '\' to '/' and replacing anything the
 * host filesystem cannot carry.  Returns a freshly allocated string; the
 * caller owns it and the old prefix is left untouched. */
static char *gk_join(const char *prefix, const uint8_t *name, size_t length,
                     bool trailing_slash) {
    size_t prefix_length = prefix ? xx_rt_strlen(prefix) : 0U;
    size_t total;
    char *result;
    size_t at = 0U;
    size_t index;
    if (length > GK_MAX_PATH || prefix_length > GK_MAX_PATH) return NULL;
    total = prefix_length + length + 2U;
    if (total > GK_MAX_PATH) return NULL;
    result = (char *)xx_mem_alloc(total);
    if (!result) return NULL;
    if (prefix_length) {
        xx_rt_memcpy(result, prefix, prefix_length);
        at = prefix_length;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t c = name[index];
        if (c == '\\' || c == '/')
            result[at++] = '/';
        else if (c < 0x20U || c == ':' || c == '*' || c == '?' || c == '"' ||
                 c == '<' || c == '>' || c == '|')
            result[at++] = '_';
        else
            result[at++] = (char)c;
    }
    if (trailing_slash && at != 0U && result[at - 1U] != '/')
        result[at++] = '/';
    result[at] = 0;
    return result;
}

/* A name that leaves the extraction root, or that carries a drive letter or a
 * ".." component, is refused rather than rewritten. */
static bool gk_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void gk_stream_free(void *opaque) {
    gk_stream *stream = (gk_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool gk_add_member(gk_stream *stream, const gk_member *member) {
    gk_member *grown;
    if (!stream || !member || stream->count >= GK_MAX_RECORDS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (gk_member *)xx_mem_realloc(stream->items,
                                        (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Reads one descriptor.  The name is rejected outright when it holds a
 * control byte: that is the cheapest structural rule that keeps payload bytes
 * from parsing as a record and so terminates the chain at the right place. */
static bool gk_read_record(xx_io_device *device, int64_t offset,
                           uint8_t *record, size_t *name_length,
                           uint32_t *attributes, int64_t *size) {
    size_t length = 0U;
    size_t index;
    int32_t declared;
    if (!gk_read_at(device, offset, record, GK_RECORD_SIZE)) return false;
    while (length < GK_NAME_FIELD_SIZE - 1U && record[length] != 0U) ++length;
    for (index = 0U; index < length; ++index)
        if (record[index] < 0x20U) return false;
    *name_length = length;
    *attributes = gk_le32(record + GK_ATTRIBUTES_OFFSET);
    declared = (int32_t)gk_le32(record + GK_SIZE_OFFSET);
    if (declared < 0) return false;
    *size = (int64_t)declared;
    return true;
}

/* Decide whether the extra all-zero uint32 is present by walking the first
 * two records under the padded reading, exactly as the reference does. */
static bool gk_probe_padding(xx_io_device *device, int64_t base,
                             int64_t data_offset, int64_t available) {
    uint8_t record[GK_RECORD_SIZE];
    uint8_t padding[4];
    int64_t cursor = data_offset;
    int index;
    for (index = 0; index < 2; ++index) {
        size_t name_length;
        uint32_t attributes;
        int64_t size;
        int64_t after;
        if (!gk_range_within(available, cursor, GK_RECORD_SIZE) ||
            !gk_read_record(device, base + cursor, record, &name_length,
                            &attributes, &size))
            return false;
        after = cursor + GK_RECORD_SIZE;
        if (!gk_range_within(available, after, size) ||
            !gk_range_within(available, after, 4) ||
            !gk_read_at(device, base + after, padding, sizeof(padding)) ||
            gk_le32(padding) != 0U)
            return false;
        cursor = after + 4 + size;
    }
    return true;
}

static bool gk_parse(Abstractformat *format, gk_stream **result) {
    uint8_t header[GK_HEADER_SIZE];
    uint8_t record[GK_RECORD_SIZE];
    uint8_t padding[4];
    gk_stream *stream = NULL;
    char *path = NULL;
    int64_t total, available, cursor, base;
    int64_t padding_size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    available = total - base;
    if (available < (int64_t)GK_HEADER_SIZE + (int64_t)GK_RECORD_SIZE)
        return false;
    if (!gk_read_at(format->device, base, header, sizeof(header)) ||
        xx_rt_memcmp(header, g_gk_banner, GK_BANNER_SIZE) != 0 ||
        header[GK_SIGNATURE_OFFSET] != 'G' ||
        header[GK_SIGNATURE_OFFSET + 1] != 'K')
        return false;

    stream = (gk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->data_offset = (int64_t)gk_le32(header + GK_DATAOFFSET_OFFSET);
    if (stream->data_offset != GK_DATAOFFSET_A &&
        stream->data_offset != GK_DATAOFFSET_B)
        goto fail;
    if (!gk_range_within(available, stream->data_offset, GK_RECORD_SIZE))
        goto fail;

    stream->has_padding = gk_probe_padding(format->device, base,
                                           stream->data_offset, available);
    padding_size = stream->has_padding ? 4 : 0;

    cursor = stream->data_offset;
    while (gk_range_within(available, cursor, GK_RECORD_SIZE)) {
        size_t name_length;
        uint32_t attributes;
        int64_t size;
        int64_t payload;
        gk_member member;
        if (!gk_read_record(format->device, base + cursor, record,
                            &name_length, &attributes, &size))
            break;
        payload = cursor + GK_RECORD_SIZE;
        if (padding_size) {
            if (!gk_range_within(available, payload, 4) ||
                !gk_read_at(format->device, base + payload, padding,
                            sizeof(padding)) ||
                gk_le32(padding) != 0U)
                break;
            payload += 4;
        }
        if (name_length == 2U && record[0] == '.' && record[1] == '.') {
            /* Pop one level; the record carries no payload of its own. */
            if (path) {
                size_t length = xx_rt_strlen(path);
                if (length && path[length - 1U] == '/') --length;
                while (length && path[length - 1U] != '/') --length;
                path[length] = 0;
            }
            cursor = payload;
            continue;
        }
        if (name_length == 0U) break;
        if (attributes & GK_ATTRIBUTE_DIRECTORY) {
            char *grown;
            if (size != 0) break;
            grown = gk_join(path, record, name_length, true);
            if (!grown) break;
            if (path) xx_mem_free(path);
            path = grown;
            cursor = payload;
            continue;
        }
        if (!gk_range_within(available, payload, size)) break;
        xx_mem_zero(&member, sizeof(member));
        member.name = gk_join(path, record, name_length, false);
        if (!member.name) break;
        member.header_offset = base + cursor;
        member.header_size = payload - cursor;
        member.data_offset = base + payload;
        member.size = size;
        member.attributes = attributes;
        if (!gk_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto fail;
        }
        cursor = payload + size;
        stream->archive_size = cursor;
    }

    if (stream->count == 0U) goto fail;
    if (stream->archive_size <= 0 || stream->archive_size > available)
        stream->archive_size = available;
    if (path) xx_mem_free(path);
    *result = stream;
    return true;
fail:
    if (path) xx_mem_free(path);
    gk_stream_free(stream);
    return false;
}

static bool gk_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *gk_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool gk_set_record(xx_archive_record *record, const gk_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_gksetup_init(xx_gksetup *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GKSETUP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-gksetup");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_gksetup_check_is_valid;
    archive->format.handle_base_info = xx_gksetup_handle_base_info;
    archive->format.get_format_size = xx_gksetup_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gksetup_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gksetup_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gksetup_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gksetup_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gksetup_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gksetup_free_archive_records_reading;
    archive->data_offset = -1;
    archive->archive_end = -1;
}

xx_gksetup *xx_gksetup_create(xx_io_device *device, int64_t base_address) {
    xx_gksetup *archive = (xx_gksetup *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_gksetup_init(archive, device, base_address);
    return archive;
}

void xx_gksetup_destroy(xx_gksetup *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_gksetup_free(xx_gksetup *archive) {
    if (!archive) return;
    xx_gksetup_destroy(archive);
    xx_mem_free(archive);
}

bool xx_gksetup_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    gk_stream *stream;
    (void)pd;
    if (!gk_parse(format, &stream)) return false;
    gk_stream_free(stream);
    return true;
}

bool xx_gksetup_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    gk_stream *stream;
    xx_gksetup *archive;
    (void)pd;
    if (!format) return false;
    if (!gk_parse(format, &stream)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_gksetup *)format;
    archive->number_of_records = stream->count;
    archive->data_offset = format->base_address + stream->data_offset;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->has_padding = stream->has_padding;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_GKSETUP_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    gk_stream_free(stream);
    return true;
}

int64_t xx_gksetup_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gksetup_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_gksetup_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gksetup_handle_base_info(format, pd))
               ? ((xx_gksetup *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_gksetup_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    gk_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!gk_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        gk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = gk_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!gk_copy_options(&state->options, options) ||
        !gk_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_gksetup_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_gksetup_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    gk_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (gk_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = gk_set_record(&state->current_record,
                                      &stream->items[stream->index]);
    return state->has_record;
}

bool xx_gksetup_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    gk_stream *stream;
    gk_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (gk_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!gk_safe_output_name(member->name)) return false;
    path_option = gk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true; /* A dry run: the member is readable. */
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
    result = xx_store_unpack_device_to_file(format->device,
                                            member->data_offset, member->size,
                                            path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_gksetup_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
