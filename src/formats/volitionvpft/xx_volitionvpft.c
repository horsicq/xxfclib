/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Volition "VP" package (Descent: FreeSpace, FreeSpace 2).  A 16-byte header
 * points at a flat index of 44-byte entries that is really a pre-order walk
 * of a directory tree; members are stored, never compressed.  The header test
 * is U3's own recognition predicate (FUN_006444a0).  xx_volitionvpft.h has
 * the field table and the corpus evidence.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/volitionvpft/xx_volitionvpft.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as VOLITIONVPFT is registered. */
#ifdef VOLITIONVPFT
#define XX_VOLITIONVPFT_FILE_TYPE XX_FILE_TYPE_VOLITIONVPFT
#else
#define XX_VOLITIONVPFT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct vp_member_s {
    char *path;
    int64_t entry_offset;
    int64_t data_offset;
    uint32_t data_size;
    uint32_t timestamp;
} vp_member;

typedef struct vp_stream_s {
    vp_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t entry_count;
    int64_t index_offset;
} vp_stream;

static uint32_t vp_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool vp_read_at(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Names come from untrusted content and become output file names, so every
 * separator and every traversal shape has to die here. */
static char *vp_component(const uint8_t *raw, size_t size) {
    char *result;
    size_t index, length = 0U;
    while (length < size && raw[length] != 0U) ++length;
    if (length == 0U) return NULL;
    result = (char *)xx_mem_alloc(length + 2U);
    if (!result) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t value = raw[index];
        result[index] = (value < 0x20U || value >= 0x7fU || value == '/' ||
                         value == '\\' || value == ':' || value == '<' ||
                         value == '>' || value == '"' || value == '|' ||
                         value == '?' || value == '*')
                            ? '_'
                            : (char)value;
    }
    while (length != 0U &&
           (result[length - 1U] == ' ' || result[length - 1U] == '.'))
        --length;
    if ((length == 1U && result[0] == '.') ||
        (length == 2U && result[0] == '.' && result[1] == '.'))
        length = 0U;
    if (length == 0U) result[length++] = '_';
    result[length] = 0;
    return result;
}

static char *vp_join_path(char *const *directories, size_t depth,
                          const char *component) {
    char *result;
    size_t index, length = 0U, at = 0U;
    if (!component || !component[0]) return NULL;
    for (index = 0U; index < depth; ++index) {
        if (!directories[index]) return NULL;
        length += xx_str_len(directories[index]) + 1U;
    }
    length += xx_str_len(component);
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    for (index = 0U; index < depth; ++index) {
        size_t part = xx_str_len(directories[index]);
        xx_rt_memcpy(result + at, directories[index], part);
        at += part;
        result[at++] = '/';
    }
    xx_rt_memcpy(result + at, component, xx_str_len(component));
    at += xx_str_len(component);
    result[at] = 0;
    return result;
}

static void vp_stream_free(void *opaque) {
    vp_stream *stream = (vp_stream *)opaque;
    size_t index;
    if (!stream) return;
    if (stream->items) {
        for (index = 0U; index < stream->count; ++index) {
            if (stream->items[index].path) xx_mem_free(stream->items[index].path);
        }
        xx_mem_free(stream->items);
    }
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

static bool vp_parse(Abstractformat *format, vp_stream **result,
                     xx_pd_struct *pd) {
    uint8_t header[XX_VOLITIONVPFT_HEADER_SIZE];
    char *directories[XX_VOLITIONVPFT_MAX_DEPTH];
    vp_stream *stream = NULL;
    int64_t total, span, index_offset, table_size;
    uint32_t version, count, entry;
    size_t depth = 0U, produced = 0U, cleanup;
    bool valid = false;

    for (cleanup = 0U; cleanup < XX_VOLITIONVPFT_MAX_DEPTH; ++cleanup)
        directories[cleanup] = NULL;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    if (span < (int64_t)(XX_VOLITIONVPFT_HEADER_SIZE +
                         XX_VOLITIONVPFT_ENTRY_SIZE))
        return false;
    if (!vp_read_at(format->device, format->base_address, header,
                    sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, XX_VOLITIONVPFT_SIGNATURE,
                     XX_VOLITIONVPFT_SIGNATURE_SIZE) != 0)
        return false;

    version = vp_le32(header + 4);
    index_offset = (int64_t)(int32_t)vp_le32(header + 8);
    count = vp_le32(header + 12);
    if (version != XX_VOLITIONVPFT_VERSION) return false;
    if (index_offset <= (int64_t)XX_VOLITIONVPFT_HEADER_SIZE) return false;
    if (count == 0U || count > XX_VOLITIONVPFT_MAX_ENTRIES) return false;

    table_size = (int64_t)count * (int64_t)XX_VOLITIONVPFT_ENTRY_SIZE;
    /* The declared count must fit in the real file before it is used to
     * allocate or loop. */
    if (index_offset > span || table_size > span - index_offset) return false;

    stream = (vp_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->items = (vp_member *)xx_mem_calloc((size_t)count,
                                               sizeof(*stream->items));
    if (!stream->items) goto done;
    stream->entry_count = count;
    stream->index_offset = format->base_address + index_offset;

    for (entry = 0U; entry < count; ++entry) {
        uint8_t record[XX_VOLITIONVPFT_ENTRY_SIZE];
        int64_t entry_offset =
            index_offset + (int64_t)entry * (int64_t)XX_VOLITIONVPFT_ENTRY_SIZE;
        uint32_t data_offset, data_size, timestamp;
        char *component;

        if ((pd && xx_pd_is_stopped(pd)) ||
            !vp_read_at(format->device, format->base_address + entry_offset,
                        record, sizeof(record)))
            goto done;
        data_offset = vp_le32(record);
        data_size = vp_le32(record + 4);
        timestamp = vp_le32(record + 40);

        if (data_size == 0U) {
            /* Directory entry: ".." pops, anything else pushes. */
            if (record[0 + 8] == '.' && record[9] == '.' && record[10] == 0U) {
                if (depth == 0U) goto done;
                --depth;
                xx_mem_free(directories[depth]);
                directories[depth] = NULL;
                continue;
            }
            if (depth >= XX_VOLITIONVPFT_MAX_DEPTH) goto done;
            component = vp_component(record + 8, XX_VOLITIONVPFT_NAME_SIZE);
            if (!component) goto done;
            directories[depth++] = component;
            continue;
        }

        /* Both halves of the extent must lie inside the real file, and a
         * member may not reach into the index. */
        if ((int64_t)data_offset < (int64_t)XX_VOLITIONVPFT_HEADER_SIZE ||
            (int64_t)data_offset > index_offset ||
            (int64_t)data_size > index_offset - (int64_t)data_offset)
            goto done;

        component = vp_component(record + 8, XX_VOLITIONVPFT_NAME_SIZE);
        if (!component) goto done;
        stream->items[produced].path =
            vp_join_path(directories, depth, component);
        xx_mem_free(component);
        if (!stream->items[produced].path) goto done;
        stream->items[produced].entry_offset =
            format->base_address + entry_offset;
        stream->items[produced].data_offset =
            format->base_address + (int64_t)data_offset;
        stream->items[produced].data_size = data_size;
        stream->items[produced].timestamp = timestamp;
        ++produced;
        stream->count = produced;
    }

    /* A package with no file at all is not a package. */
    if (produced == 0U) goto done;
    stream->count = produced;
    stream->archive_size = index_offset + table_size;
    valid = true;
done:
    for (cleanup = 0U; cleanup < XX_VOLITIONVPFT_MAX_DEPTH; ++cleanup) {
        if (directories[cleanup]) xx_mem_free(directories[cleanup]);
    }
    if (!valid) {
        vp_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

/* -------------------------------------------------------------- record -- */

static bool vp_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
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

static const xx_var *vp_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool vp_set_record(xx_archive_record *record, const vp_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->entry_offset;
    record->header_size = XX_VOLITIONVPFT_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->path) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_volitionvpft_init(xx_volitionvpft *archive, xx_io_device *device,
                          int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VOLITIONVPFT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-volition-vp");
    xx_format_set_extension(&archive->format, "vp");
    xx_format_set_version(&archive->format, "2");
    archive->format.check_is_valid = xx_volitionvpft_check_is_valid;
    archive->format.handle_base_info = xx_volitionvpft_handle_base_info;
    archive->format.get_format_size = xx_volitionvpft_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_volitionvpft_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_volitionvpft_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_volitionvpft_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_volitionvpft_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_volitionvpft_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_volitionvpft_free_archive_records_reading;
    archive->index_offset = -1;
}

xx_volitionvpft *xx_volitionvpft_create(xx_io_device *device,
                                        int64_t base_address) {
    xx_volitionvpft *archive =
        (xx_volitionvpft *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_volitionvpft_init(archive, device, base_address);
    return archive;
}

void xx_volitionvpft_destroy(xx_volitionvpft *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_volitionvpft_free(xx_volitionvpft *archive) {
    if (!archive) return;
    xx_volitionvpft_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_volitionvpft_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    vp_stream *stream;
    if (!vp_parse(format, &stream, pd)) return false;
    vp_stream_free(stream);
    return true;
}

bool xx_volitionvpft_handle_base_info(Abstractformat *format,
                                      xx_pd_struct *pd) {
    vp_stream *stream;
    xx_volitionvpft *archive;
    int64_t total;

    if (!format || !vp_parse(format, &stream, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_volitionvpft *)format;
    archive->number_of_records = stream->count;
    archive->number_of_entries = stream->entry_count;
    archive->index_offset = stream->index_offset;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    total = xx_io_total_size(format->device);
    if (total > format->base_address + stream->archive_size) {
        format->overlay_offset = format->base_address + stream->archive_size;
        format->overlay_size = total - format->overlay_offset;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    format->is_valid = true;
    format->base_info_handled = true;
    vp_stream_free(stream);
    return true;
}

int64_t xx_volitionvpft_get_format_size(Abstractformat *format,
                                        xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_volitionvpft_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_volitionvpft_get_number_of_archive_records(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_volitionvpft_handle_base_info(format, pd))
               ? ((xx_volitionvpft *)format)->number_of_records
               : 0U;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_volitionvpft_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    vp_stream *stream;
    xx_archive_record_state *state;

    if (!vp_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        vp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = vp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!vp_copy_options(&state->options, options) ||
        !vp_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_volitionvpft_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_volitionvpft_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    vp_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (vp_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        vp_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_volitionvpft_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    vp_stream *stream;
    const vp_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (vp_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];

    plain = (uint8_t *)xx_mem_alloc(member->data_size != 0U ? member->data_size
                                                            : 1U);
    if (!plain) goto done;
    if (member->data_size != 0U &&
        !vp_read_at(format->device, member->data_offset, plain,
                    member->data_size))
        goto done;

    path_option = vp_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->path)
               : xx_str_concat(base, member->path);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < (size_t)member->data_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         (size_t)member->data_size - written);
            if (amount <= 0 ||
                (size_t)amount > (size_t)member->data_size - written) {
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

void xx_volitionvpft_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
