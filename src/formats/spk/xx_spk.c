/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * !Spark / SparkFS: the sequential ARC member chain with RISC OS extensions.
 * The header test is U3's own recognition predicate (FUN_005b5860); the
 * method table and the directory rule come from deark's Spark module.
 * xx_spk.h has the field table and the corpus evidence.
 *
 * Directory members hold a nested member chain in their data extent, so the
 * walk is recursive and builds a path per member.  The decoders are the ones
 * the ArcFS reader already uses: the same RLE90 filter and the same ARC LZW.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/spk/xx_spk.h"

#include "xxfclib/algo/arcfs/xx_arcfs_lzw.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as SPK is registered there. */
#ifdef SPK
#define XX_SPK_FILE_TYPE XX_FILE_TYPE_SPK
#else
#define XX_SPK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SPK_METHOD_END_A 0x00U
#define SPK_METHOD_END_B 0x80U
#define SPK_METHOD_STORED_OLD 0x81U
#define SPK_METHOD_STORED 0x82U
#define SPK_METHOD_PACKED 0x83U
#define SPK_METHOD_CRUNCHED 0x88U
#define SPK_METHOD_SQUASHED 0x89U
#define SPK_METHOD_COMPRESSED 0xffU
#define SPK_SQUASHED_MAX_BITS 13U
/* A single member may not claim to expand beyond this. */
#define SPK_MAX_ORIGINAL_SIZE ((int64_t)512 * 1024 * 1024)

typedef struct spk_member_s {
    char *path;
    int64_t header_offset;
    int64_t data_offset;
    uint32_t packed_size;
    uint32_t original_size;
    uint32_t load_address;
    uint32_t exec_address;
    uint32_t attributes;
    uint16_t dos_date;
    uint16_t dos_time;
    uint16_t crc;
    uint8_t method;
    bool folder;
} spk_member;

typedef struct spk_stream_s {
    spk_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    size_t files;
    int64_t archive_size;
} spk_stream;

static uint16_t spk_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t spk_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool spk_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool spk_known_method(uint8_t method) {
    return (method >= SPK_METHOD_STORED_OLD && method <= SPK_METHOD_SQUASHED) ||
           method == SPK_METHOD_COMPRESSED;
}

static bool spk_has_decoder(uint8_t method) {
    return method == SPK_METHOD_STORED_OLD || method == SPK_METHOD_STORED ||
           method == SPK_METHOD_PACKED || method == SPK_METHOD_CRUNCHED ||
           method == SPK_METHOD_SQUASHED || method == SPK_METHOD_COMPRESSED;
}

/* Names come from untrusted content and become output file names, so every
 * separator and both dot-only shapes have to die here. */
static char *spk_component(const uint8_t *raw, size_t size, bool strict) {
    char *result;
    size_t index, length = 0U;
    while (length < size && raw[length] != 0U) ++length;
    if (length == 0U) return NULL;
    /* U3 requires printable ASCII, but only of the FIRST member header,
     * where it stands in for a magic number.  Later members legitimately
     * carry RISC OS Latin-1 names - this corpus has "!<e9>lite" and several
     * "<a0>"-separated ones - so they are sanitised, not rejected. */
    for (index = 0U; index < length; ++index) {
        if (raw[index] < 0x20U) return NULL;
        if (strict && raw[index] >= 0x7fU) return NULL;
    }
    result = (char *)xx_mem_alloc(length + 2U);
    if (!result) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t value = raw[index];
        result[index] = (value >= 0x7fU || value == '/' || value == '\\' ||
                         value == ':' || value == '<' || value == '>' ||
                         value == '"' || value == '|' || value == '?' ||
                         value == '*')
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

static char *spk_join(const char *prefix, const char *component) {
    if (!component) return NULL;
    if (!prefix || !prefix[0]) return xx_str_concat(component, "");
    return xx_str_concat3(prefix, "/", component);
}

static void spk_stream_free(void *opaque) {
    spk_stream *stream = (spk_stream *)opaque;
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

static spk_member *spk_stream_push(spk_stream *stream) {
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity ? stream->capacity * 2U : 32U;
        spk_member *items;
        if (capacity > XX_SPK_MAX_MEMBERS) capacity = XX_SPK_MAX_MEMBERS;
        if (capacity == stream->capacity) return NULL;
        items = (spk_member *)xx_mem_calloc(capacity, sizeof(*items));
        if (!items) return NULL;
        if (stream->items) {
            xx_rt_memcpy(items, stream->items,
                         stream->count * sizeof(*items));
            xx_mem_free(stream->items);
        }
        stream->items = items;
        stream->capacity = capacity;
    }
    return &stream->items[stream->count++];
}

/* --------------------------------------------------------------- walk -- */

/* Walk one member chain.  @p start and @p limit are relative to
 * format->base_address; a directory's chain is walked inside its own data
 * extent, which is what keeps the recursion bounded by the file. */
static bool spk_walk(Abstractformat *format, spk_stream *stream,
                     int64_t start, int64_t limit, unsigned depth,
                     const char *prefix, int64_t *chain_end,
                     xx_pd_struct *pd) {
    int64_t offset = start;

    if (depth > XX_SPK_MAX_DEPTH) return false;
    while (offset + (int64_t)XX_SPK_HEADER_SIZE <= limit) {
        uint8_t header[XX_SPK_HEADER_SIZE];
        spk_member *member;
        char *component;
        char *path;
        uint8_t method;
        uint32_t packed_size, original_size, load_address;
        int64_t data_offset;
        bool folder;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (stream->count >= XX_SPK_MAX_MEMBERS) return false;
        if (!spk_read_at(format->device, format->base_address + offset, header,
                         sizeof(header)))
            return false;
        if (header[0] != XX_SPK_MARKER) return false;
        method = header[1];
        if (method == SPK_METHOD_END_A || method == SPK_METHOD_END_B) {
            /* End marker: only the two bytes belong to the chain. */
            offset += 2;
            break;
        }
        if (!spk_known_method(method)) return false;

        packed_size = spk_le32(header + 0x0f);
        original_size = spk_le32(header + 0x19);
        load_address = spk_le32(header + 0x1d);
        /* The declared extent must fit in what is left of this chain before
         * it is used to seek, recurse or allocate. */
        if ((int64_t)packed_size >
            limit - offset - (int64_t)XX_SPK_HEADER_SIZE)
            return false;
        if ((int64_t)original_size > SPK_MAX_ORIGINAL_SIZE) return false;
        /* U3's own rule for the stored method: the two sizes must agree. */
        if (method == SPK_METHOD_STORED && packed_size != original_size)
            return false;
        /* U3 rejects a member whose every field is zero; such a "member" is
         * indistinguishable from padding. */
        if (packed_size == 0U && original_size == 0U &&
            spk_le16(header + 0x13) == 0U && spk_le16(header + 0x15) == 0U &&
            spk_le16(header + 0x17) == 0U)
            return false;

        component = spk_component(header + 2, XX_SPK_NAME_SIZE,
                                  stream->count == 0U);
        if (!component) return false;
        path = spk_join(prefix, component);
        xx_mem_free(component);
        if (!path) return false;

        data_offset = offset + (int64_t)XX_SPK_HEADER_SIZE;
        /* A directory is a stored member whose RISC OS file type is 0xddc;
         * its data is a nested chain, not content. */
        folder = (method == SPK_METHOD_STORED &&
                  (load_address & 0xfff00000U) == 0xfff00000U &&
                  ((load_address >> 8U) & 0xfffU) == XX_SPK_TYPE_DIRECTORY);

        member = spk_stream_push(stream);
        if (!member) {
            xx_mem_free(path);
            return false;
        }
        member->path = path;
        member->header_offset = format->base_address + offset;
        member->data_offset = format->base_address + data_offset;
        member->packed_size = packed_size;
        member->original_size = folder ? 0U : original_size;
        member->dos_date = spk_le16(header + 0x13);
        member->dos_time = spk_le16(header + 0x15);
        member->crc = spk_le16(header + 0x17);
        member->load_address = load_address;
        member->exec_address = spk_le32(header + 0x21);
        member->attributes = spk_le32(header + 0x25);
        member->method = method;
        member->folder = folder;
        if (!folder) ++stream->files;

        if (folder) {
            int64_t nested_end = 0;
            if (!spk_walk(format, stream, data_offset,
                          data_offset + (int64_t)packed_size, depth + 1U, path,
                          &nested_end, pd))
                return false;
        }
        offset = data_offset + (int64_t)packed_size;
    }
    if (chain_end) *chain_end = offset;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static bool spk_parse(Abstractformat *format, spk_stream **result,
                      xx_pd_struct *pd) {
    spk_stream *stream;
    int64_t total, span, end = 0;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    if (span < (int64_t)XX_SPK_HEADER_SIZE) return false;

    stream = (spk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!spk_walk(format, stream, 0, span, 0U, NULL, &end, pd)) {
        spk_stream_free(stream);
        return false;
    }
    /* A chain with nothing in it is not an archive, and neither is one whose
     * first member is the only thing that parsed. */
    if (stream->count == 0U || stream->files == 0U) {
        spk_stream_free(stream);
        return false;
    }
    stream->archive_size = end;
    *result = stream;
    return true;
}

/* ------------------------------------------------------------- decode -- */

static bool spk_decode_member(Abstractformat *format, const spk_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool decoded = false;

    if (!format || !member || !plain || !plain_size || member->folder)
        return false;
    if (!spk_has_decoder(member->method)) return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0U
                                         ? member->packed_size
                                         : 1U);
    output = (uint8_t *)xx_mem_alloc(member->original_size != 0U
                                         ? member->original_size
                                         : 1U);
    if (!packed || !output) goto done;
    if (member->packed_size != 0U &&
        !spk_read_at(format->device, member->data_offset, packed,
                     member->packed_size))
        goto done;

    if (member->method == SPK_METHOD_STORED ||
        member->method == SPK_METHOD_STORED_OLD) {
        if (member->packed_size != member->original_size) goto done;
        if (member->original_size != 0U)
            xx_rt_memcpy(output, packed, member->original_size);
        written = member->original_size;
        decoded = true;
    } else if (member->method == SPK_METHOD_PACKED) {
        decoded = xx_arcfs_rle90_decode_memory(packed, member->packed_size,
                                               output, member->original_size,
                                               &written);
    } else if (member->method == SPK_METHOD_SQUASHED) {
        /* Squashed has no header byte: the code width is fixed at 13. */
        decoded = xx_arcfs_lzw_decode_memory(packed, member->packed_size,
                                             output, member->original_size,
                                             SPK_SQUASHED_MAX_BITS, false,
                                             &written);
    } else {
        /* Crunched and compressed both carry the Unix-compress one-byte
         * header; its low five bits are the maximum code width.  Crunched
         * additionally runs RLE90 over the LZW output. */
        if (member->packed_size < 1U) goto done;
        decoded = xx_arcfs_lzw_decode_memory(
            packed + 1, member->packed_size - 1U, output,
            member->original_size, (uint8_t)(packed[0] & 0x1fU),
            member->method == SPK_METHOD_CRUNCHED, &written);
    }
    if (!decoded || written != member->original_size) goto done;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
done:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

/* -------------------------------------------------------------- record -- */

static bool spk_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *spk_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool spk_set_record(xx_archive_record *record,
                           const spk_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = XX_SPK_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->folder ? 0 : (int64_t)member->packed_size;
    return xx_archive_record_set_original_name(record, member->path) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               member->folder ? 0U : member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_spk_init(xx_spk *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SPK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    archive->format.os = XX_OS_UNKNOWN;
    xx_format_set_mime_type(&archive->format, "application/x-spark");
    xx_format_set_extension(&archive->format, "spk");
    archive->format.check_is_valid = xx_spk_check_is_valid;
    archive->format.handle_base_info = xx_spk_handle_base_info;
    archive->format.get_format_size = xx_spk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_spk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_spk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_spk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_spk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_spk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_spk_free_archive_records_reading;
}

xx_spk *xx_spk_create(xx_io_device *device, int64_t base_address) {
    xx_spk *archive = (xx_spk *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_spk_init(archive, device, base_address);
    return archive;
}

void xx_spk_destroy(xx_spk *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_spk_free(xx_spk *archive) {
    if (!archive) return;
    xx_spk_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_spk_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    spk_stream *stream;
    if (!spk_parse(format, &stream, pd)) return false;
    spk_stream_free(stream);
    return true;
}

bool xx_spk_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    spk_stream *stream;
    xx_spk *archive;
    int64_t total;

    if (!format || !spk_parse(format, &stream, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_spk *)format;
    archive->number_of_records = stream->count;
    archive->number_of_files = stream->files;
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
    spk_stream_free(stream);
    return true;
}

int64_t xx_spk_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_spk_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_spk_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_spk_handle_base_info(format, pd))
               ? ((xx_spk *)format)->number_of_records
               : 0U;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_spk_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    spk_stream *stream;
    xx_archive_record_state *state;

    if (!spk_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        spk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = spk_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!spk_copy_options(&state->options, options) ||
        !spk_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_spk_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_spk_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    spk_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (spk_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        spk_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_spk_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    spk_stream *stream;
    const spk_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (spk_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];

    path_option = spk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);

    if (member->folder) {
        /* A directory has no content of its own; creating it is the whole
         * operation, and with no target path there is nothing to do. */
        if (!path_option) return true;
    } else if (!spk_decode_member(format, member, &plain, &plain_size)) {
        /* Methods 0x84..0x87 have no decoder here; they fail closed rather
         * than producing a guess. */
        return false;
    }
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
    if (!path) goto done;
    if (member->folder) {
        /* Create the directory itself by asking for a child path's parents. */
        char *marker = xx_str_concat(path, "/.");
        result = marker && xx_store_create_dirs_a(marker, false);
        if (marker) xx_str_free(marker);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount =
                xx_io_write(destination, plain + written, plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_spk_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
