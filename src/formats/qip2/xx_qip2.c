/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Quarterdeck install archive version 2.  A 16-byte header is followed by a
 * contiguous index of 16-byte entries, each carrying the ABSOLUTE offset of a
 * 36-byte "QD" member record; the packed bytes follow that record inline.
 * Members are PKWARE DCL streams.  The header test is U3's own recognition
 * predicate; the record layout is XArchive's QIP1 record plus one u32.
 * xx_qip2.h has the field table and the corpus evidence.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qip2/xx_qip2.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as QIP2 is registered there. */
#ifdef QIP2
#define XX_QIP2_FILE_TYPE XX_FILE_TYPE_QIP2
#else
#define XX_QIP2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The smallest DCL stream is its two-byte header plus an end marker. */
#define QIP2_MIN_PACKED_SIZE 3

typedef struct qip2_member_s {
    char name[XX_QIP2_RECORD_NAME_SIZE + 1];
    int64_t record_offset;
    int64_t data_offset;
    uint32_t packed_size;
    uint32_t unpacked_size;
    uint32_t opaque; /**< u32 at 0x0f of the record; never interpreted. */
    uint16_t dos_time;
    uint16_t dos_date;
    uint16_t sequence;
    uint8_t flags;
} qip2_member;

typedef struct qip2_stream_s {
    qip2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t format_version;
} qip2_stream;

static uint16_t qip2_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t qip2_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool qip2_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* A member name reaches the extractor as an output file name, so every
 * separator and both dot-only shapes have to die here. */
static bool qip2_copy_name(const uint8_t *field, size_t width, char *out) {
    size_t length = 0U, index;
    while (length < width && field[length] != 0U) ++length;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t value = field[index];
        if (value < 0x20U || value >= 0x7fU) return false;
        if (value == '/' || value == '\\' || value == ':' || value == '<' ||
            value == '>' || value == '"' || value == '|' || value == '?' ||
            value == '*')
            return false;
        out[index] = (char)value;
    }
    out[length] = 0;
    if (out[0] == '.' &&
        (length == 1U || (length == 2U && out[1] == '.')))
        return false;
    return true;
}

static void qip2_stream_free(void *opaque) {
    qip2_stream *stream = (qip2_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

static bool qip2_parse(Abstractformat *format, qip2_stream **result,
                       xx_pd_struct *pd) {
    uint8_t header[XX_QIP2_HEADER_SIZE];
    qip2_stream *stream = NULL;
    int64_t total, span, table_size, end;
    uint32_t declared_table;
    uint16_t count, entry;
    bool valid = false;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    if (span < (int64_t)(XX_QIP2_HEADER_SIZE + XX_QIP2_INDEX_ENTRY_SIZE +
                         XX_QIP2_RECORD_SIZE))
        return false;
    if (!qip2_read_at(format->device, format->base_address, header,
                      sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, XX_QIP2_SIGNATURE, XX_QIP2_SIGNATURE_SIZE) != 0)
        return false;

    count = qip2_le16(header + 2);
    declared_table = qip2_le32(header + 4);
    if (count == 0U) return false;
    /* U3's own four tests on the index size.  The last one ties it to the
     * count, which is what makes this two-byte magic usable at all. */
    if (declared_table == 0U || (declared_table & 0x80000000U) != 0U) return false;
    if ((declared_table & 0xfU) != 0U) return false;
    if ((declared_table >> 4U) != (uint32_t)count) return false;

    table_size = (int64_t)declared_table;
    /* The declared index must fit in the real file before it is used to
     * allocate or loop. */
    if (table_size > span - (int64_t)XX_QIP2_HEADER_SIZE) return false;

    stream = (qip2_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->items =
        (qip2_member *)xx_mem_calloc((size_t)count, sizeof(*stream->items));
    if (!stream->items) goto done;
    stream->format_version = qip2_le32(header + 8);
    end = (int64_t)XX_QIP2_HEADER_SIZE + table_size;

    for (entry = 0U; entry < count; ++entry) {
        uint8_t index_entry[XX_QIP2_INDEX_ENTRY_SIZE];
        uint8_t record[XX_QIP2_RECORD_SIZE];
        qip2_member *member = &stream->items[entry];
        int64_t index_offset = (int64_t)XX_QIP2_HEADER_SIZE +
                               (int64_t)entry *
                                   (int64_t)XX_QIP2_INDEX_ENTRY_SIZE;
        int64_t record_offset;
        uint32_t packed_size, unpacked_size;

        if ((pd && xx_pd_is_stopped(pd)) ||
            !qip2_read_at(format->device, format->base_address + index_offset,
                          index_entry, sizeof(index_entry)))
            goto done;
        record_offset = (int64_t)qip2_le32(index_entry);
        /* A record has to sit after the index and leave room for itself. */
        if (record_offset < (int64_t)XX_QIP2_HEADER_SIZE + table_size ||
            record_offset > span - (int64_t)XX_QIP2_RECORD_SIZE)
            goto done;
        if (!qip2_read_at(format->device,
                          format->base_address + record_offset, record,
                          sizeof(record)))
            goto done;
        if (xx_rt_memcmp(record, XX_QIP2_RECORD_SIGNATURE, 2U) != 0) goto done;
        /* Only file records are indexed.  A path record here would mean the
         * index does not describe what this reader thinks it describes, so
         * it is rejected rather than skipped. */
        if (qip2_le16(record + 2) != XX_QIP2_KIND_FILE) goto done;

        packed_size = qip2_le32(record + 4);
        unpacked_size = qip2_le32(record + 0x13);
        /* Both halves of the extent must lie inside the real file. */
        if ((int64_t)packed_size <
                (int64_t)QIP2_MIN_PACKED_SIZE ||
            (int64_t)packed_size >
                span - record_offset - (int64_t)XX_QIP2_RECORD_SIZE)
            goto done;
        if ((int64_t)unpacked_size > XX_QIP2_MAX_UNCOMPRESSED_SIZE) goto done;

        /* The record's own name field is the wider one; the index entry is
         * the fallback when it is empty or unusable. */
        if (!qip2_copy_name(record + 0x17, XX_QIP2_RECORD_NAME_SIZE,
                            member->name) &&
            !qip2_copy_name(index_entry + 4, XX_QIP2_INDEX_NAME_SIZE,
                            member->name))
            goto done;

        member->record_offset = format->base_address + record_offset;
        member->data_offset = format->base_address + record_offset +
                              (int64_t)XX_QIP2_RECORD_SIZE;
        member->packed_size = packed_size;
        member->unpacked_size = unpacked_size;
        member->sequence = qip2_le16(record + 8);
        member->flags = record[10];
        member->dos_time = qip2_le16(record + 11);
        member->dos_date = qip2_le16(record + 13);
        member->opaque = qip2_le32(record + 15);

        if (record_offset + (int64_t)XX_QIP2_RECORD_SIZE +
                (int64_t)packed_size >
            end)
            end = record_offset + (int64_t)XX_QIP2_RECORD_SIZE +
                  (int64_t)packed_size;
    }

    stream->count = (size_t)count;
    stream->archive_size = end;
    valid = true;
done:
    if (!valid) {
        qip2_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

/* -------------------------------------------------------------- record -- */

static bool qip2_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
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

static const xx_var *qip2_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool qip2_set_record(xx_archive_record *record,
                            const qip2_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->record_offset;
    record->header_size = XX_QIP2_RECORD_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           /* PKWARE DCL; the container has no method field of its own. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          10U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_qip2_init(xx_qip2 *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_QIP2_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-qip");
    xx_format_set_extension(&archive->format, "qip");
    xx_format_set_version(&archive->format, "2");
    archive->format.check_is_valid = xx_qip2_check_is_valid;
    archive->format.handle_base_info = xx_qip2_handle_base_info;
    archive->format.get_format_size = xx_qip2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_qip2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_qip2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_qip2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_qip2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_qip2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_qip2_free_archive_records_reading;
}

xx_qip2 *xx_qip2_create(xx_io_device *device, int64_t base_address) {
    xx_qip2 *archive = (xx_qip2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_qip2_init(archive, device, base_address);
    return archive;
}

void xx_qip2_destroy(xx_qip2 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_qip2_free(xx_qip2 *archive) {
    if (!archive) return;
    xx_qip2_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_qip2_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    qip2_stream *stream;
    if (!qip2_parse(format, &stream, pd)) return false;
    qip2_stream_free(stream);
    return true;
}

bool xx_qip2_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    qip2_stream *stream;
    xx_qip2 *archive;
    int64_t total;

    if (!format || !qip2_parse(format, &stream, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_qip2 *)format;
    archive->number_of_records = stream->count;
    archive->format_version = stream->format_version;
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
    qip2_stream_free(stream);
    return true;
}

int64_t xx_qip2_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_qip2_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_qip2_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_qip2_handle_base_info(format, pd))
               ? ((xx_qip2 *)format)->number_of_records
               : 0U;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_qip2_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    qip2_stream *stream;
    xx_archive_record_state *state;

    if (!qip2_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        qip2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = qip2_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!qip2_copy_options(&state->options, options) ||
        !qip2_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_qip2_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qip2_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    qip2_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (qip2_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        qip2_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_qip2_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    qip2_stream *stream;
    const qip2_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t produced = 0U;
    size_t written = 0U;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (qip2_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];

    packed = (uint8_t *)xx_mem_alloc(member->packed_size);
    /* A zero-length member is legitimate here - six of them exist in the
     * corpus, each a bare DCL header plus end marker - so the buffer is
     * padded to one byte rather than the member being refused. */
    plain = (uint8_t *)xx_mem_alloc(member->unpacked_size != 0U
                                        ? member->unpacked_size
                                        : 1U);
    if (!packed || !plain) goto done;
    if (!qip2_read_at(format->device, member->data_offset, packed,
                      member->packed_size))
        goto done;
    if (member->unpacked_size != 0U) {
        if (!xx_dcl_decode_memory(packed, member->packed_size, plain,
                                  member->unpacked_size, &produced))
            goto done;
        /* The record states the decoded length; a decoder that produces a
         * different one has not reproduced this member. */
        if (produced != (size_t)member->unpacked_size) goto done;
    } else {
        produced = 0U;
    }

    path_option = qip2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < produced) {
            ssize_t amount =
                xx_io_write(destination, plain + written, produced - written);
            if (amount <= 0 || (size_t)amount > produced - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_qip2_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
