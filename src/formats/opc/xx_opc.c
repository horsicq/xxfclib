/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the OS2Point distribution package (.opc).  Layout ported
 * from XArchive's packages/xopc.cpp.
 *
 * The file opens with an 0x50-byte banner: "OS2POINT" plus a version and
 * part description, padded with NULs and terminated by 0x1A at 0x4F.  What
 * follows to end of file is an ordinary ZIP archive whose every byte has had
 * 0x67 added by the producer; subtracting 0x67 from each byte recovers it.
 * Members are stored or deflated and the directory is walked exactly as a
 * ZIP central directory, so the eight-byte banner is never the only thing
 * accepted: the end-of-central-directory record, every central entry
 * signature and every local header signature must also be there after the
 * de-obfuscation.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/opc/xx_opc.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef OPC
#define XX_OPC_FILE_TYPE XX_FILE_TYPE_OPC
#else
#define XX_OPC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define OPC_BANNER_SIZE 0x50
#define OPC_TERMINATOR_OFFSET 0x4F
#define OPC_TERMINATOR 0x1AU
#define OPC_SHIFT 0x67U
#define OPC_EOCD_SIZE 22
#define OPC_CDENTRY_SIZE 46
#define OPC_LOCAL_SIZE 30
/* A ZIP end-of-central-directory record may be trailed by a comment of at
 * most 65535 bytes, so it starts no earlier than this from the end. */
#define OPC_EOCD_SEARCH (65535 + OPC_EOCD_SIZE)
#define OPC_MAX_ENTRIES 200000
#define OPC_MAX_NAME 4096
#define OPC_MAX_INPUT INT64_C(0x10000000)

typedef struct opc_member_s {
    char *name;
    int64_t data_offset;   /* offset inside the de-obfuscated ZIP image */
    int64_t compressed_size;
    uint64_t uncompressed_size;
    uint32_t crc32;
    uint16_t method;
} opc_member;

typedef struct opc_stream_s {
    uint8_t *zip;          /* the de-obfuscated ZIP image */
    int64_t zip_size;
    opc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} opc_stream;

static uint16_t opc_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t opc_le32(const uint8_t *bytes) {
    return (uint32_t)opc_le16(bytes) | ((uint32_t)opc_le16(bytes + 2U) << 16U);
}

static bool opc_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool opc_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* ZIP member names are arbitrary bytes.  '/' stays a path separator,
 * everything the host filesystem cannot represent is escaped as %XX so the
 * mapping stays reversible and cannot collapse two members onto one output
 * file. */
static char *opc_sanitize_name(const uint8_t *raw, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    char *name;
    size_t index, output = 0U;
    if (size > (SIZE_MAX - 2U) / 3U) return NULL;
    name = (char *)xx_mem_alloc(size * 3U + 2U);
    if (!name) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c == (uint8_t)'/' || c == (uint8_t)'\\') {
            name[output++] = '/';
            continue;
        }
        if (c >= 0x20U && c < 0x7FU && c != (uint8_t)'%' &&
            c != (uint8_t)':' && c != (uint8_t)'*' && c != (uint8_t)'?' &&
            c != (uint8_t)'"' && c != (uint8_t)'<' && c != (uint8_t)'>' &&
            c != (uint8_t)'|') {
            name[output++] = (char)c;
        } else {
            name[output++] = '%';
            name[output++] = hex[(c >> 4U) & 0x0FU];
            name[output++] = hex[c & 0x0FU];
        }
    }
    name[output] = 0;
    return name;
}

static bool opc_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || (c != 0U && c < 0x20U))
            return false;
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

static void opc_stream_free(void *opaque) {
    opc_stream *stream = (opc_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    if (stream->zip) xx_mem_free(stream->zip);
    xx_mem_free(stream);
}

static bool opc_add_member(opc_stream *stream, const opc_member *member) {
    opc_member *grown;
    if (!stream || !member || stream->count >= (size_t)OPC_MAX_ENTRIES ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (opc_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) *
                                             sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool opc_parse(Abstractformat *format, opc_stream **result) {
    opc_stream *stream = NULL;
    uint8_t banner[OPC_BANNER_SIZE];
    uint8_t *zip = NULL;
    int64_t total, size, zip_size, at, eocd_offset = -1;
    int64_t directory_size, directory_offset, cursor;
    int32_t entry_count, entry;
    const uint8_t *eocd;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < OPC_BANNER_SIZE + OPC_EOCD_SIZE + OPC_LOCAL_SIZE ||
        size > OPC_MAX_INPUT) return false;
    if (!opc_read_at(format->device, format->base_address, banner,
                     sizeof(banner)) ||
        xx_rt_memcmp(banner, "OS2POINT", 8U) != 0 ||
        banner[OPC_TERMINATOR_OFFSET] != OPC_TERMINATOR) return false;
    zip_size = size - OPC_BANNER_SIZE;
    zip = (uint8_t *)xx_mem_alloc((size_t)zip_size);
    if (!zip) return false;
    if (!opc_read_at(format->device, format->base_address + OPC_BANNER_SIZE,
                     zip, (size_t)zip_size)) {
        xx_mem_free(zip);
        return false;
    }
    for (at = 0; at < zip_size; ++at)
        zip[at] = (uint8_t)(zip[at] - OPC_SHIFT);
    /* The de-obfuscated payload has to be a real ZIP: locate its
     * end-of-central-directory record.  This, with the signature checks
     * below, is what makes the eight-byte banner safe to detect on. */
    {
        int64_t tail = zip_size < OPC_EOCD_SEARCH ? zip_size
                                                  : (int64_t)OPC_EOCD_SEARCH;
        int64_t start = zip_size - tail;
        for (at = zip_size - OPC_EOCD_SIZE; at >= start; --at) {
            const uint8_t *p = zip + at;
            if (p[0] == 'P' && p[1] == 'K' && p[2] == 5U && p[3] == 6U) {
                int64_t comment = (int64_t)opc_le16(p + 20U);
                if (at + OPC_EOCD_SIZE + comment <= zip_size) {
                    eocd_offset = at;
                    break;
                }
            }
        }
    }
    if (eocd_offset < 0) {
        xx_mem_free(zip);
        return false;
    }
    eocd = zip + eocd_offset;
    entry_count = (int32_t)opc_le16(eocd + 10U);
    directory_size = (int64_t)opc_le32(eocd + 12U);
    directory_offset = (int64_t)opc_le32(eocd + 16U);
    if (entry_count < 1 || entry_count > OPC_MAX_ENTRIES ||
        !opc_range_within(zip_size, directory_offset, directory_size) ||
        directory_size < (int64_t)entry_count * OPC_CDENTRY_SIZE ||
        directory_offset + directory_size > eocd_offset) {
        xx_mem_free(zip);
        return false;
    }
    stream = (opc_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        xx_mem_free(zip);
        return false;
    }
    stream->zip = zip;
    stream->zip_size = zip_size;
    cursor = 0;
    for (entry = 0; entry < entry_count; ++entry) {
        const uint8_t *record;
        const uint8_t *local;
        int64_t compressed, uncompressed, name_size, extra_size;
        int64_t comment_size, local_offset, entry_size, data_offset;
        uint16_t flags, method;
        uint32_t crc32;
        opc_member member;
        if (cursor + OPC_CDENTRY_SIZE > directory_size) goto fail;
        record = zip + directory_offset + cursor;
        if (record[0] != 'P' || record[1] != 'K' || record[2] != 1U ||
            record[3] != 2U) goto fail;
        flags = opc_le16(record + 8U);
        method = opc_le16(record + 10U);
        crc32 = opc_le32(record + 16U);
        compressed = (int64_t)opc_le32(record + 20U);
        uncompressed = (int64_t)opc_le32(record + 24U);
        name_size = (int64_t)opc_le16(record + 28U);
        extra_size = (int64_t)opc_le16(record + 30U);
        comment_size = (int64_t)opc_le16(record + 32U);
        local_offset = (int64_t)opc_le32(record + 42U);
        entry_size = OPC_CDENTRY_SIZE + name_size + extra_size + comment_size;
        if (cursor + entry_size > directory_size) goto fail;
        if (name_size < 1 || name_size > OPC_MAX_NAME) goto fail;
        /* Encrypted members carry no key here and would silently produce
         * noise. */
        if (flags & 0x0001U) goto fail;
        if (!opc_range_within(zip_size, local_offset, OPC_LOCAL_SIZE))
            goto fail;
        local = zip + local_offset;
        if (local[0] != 'P' || local[1] != 'K' || local[2] != 3U ||
            local[3] != 4U) goto fail;
        data_offset = local_offset + OPC_LOCAL_SIZE +
                      (int64_t)opc_le16(local + 26U) +
                      (int64_t)opc_le16(local + 28U);
        /* Every declared extent is bounded against the real image before it
         * is used to read or allocate. */
        if (!opc_range_within(zip_size, data_offset, compressed)) goto fail;
        {
            const uint8_t *raw = record + OPC_CDENTRY_SIZE;
            cursor += entry_size;
            /* Directory entries carry no payload; the extraction chain
             * recreates the folders from the member paths. */
            if (raw[name_size - 1] == (uint8_t)'/' ||
                raw[name_size - 1] == (uint8_t)'\\') continue;
            xx_mem_zero(&member, sizeof(member));
            member.name = opc_sanitize_name(raw, (size_t)name_size);
            if (!member.name || !member.name[0]) {
                if (member.name) xx_str_free(member.name);
                goto fail;
            }
            member.data_offset = data_offset;
            member.compressed_size = compressed;
            member.uncompressed_size = (uint64_t)uncompressed;
            member.crc32 = crc32;
            member.method = method;
            if (!opc_add_member(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
        }
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    opc_stream_free(stream);
    return false;
}

static bool opc_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *opc_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool opc_set_record(xx_archive_record *record, const opc_member *member,
                           int64_t base_address) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base_address + OPC_BANNER_SIZE +
                            member->data_offset;
    record->header_size = 0;
    record->data_offset = record->header_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool opc_decode(const opc_stream *stream, const opc_member *member,
                       uint8_t **plain, size_t *plain_size) {
    const uint8_t *packed;
    uint8_t *output;
    size_t written = 0U, output_size;
    bool decoded = false;
    if (!stream || !member || !plain || !plain_size ||
        member->uncompressed_size > (uint64_t)SIZE_MAX ||
        !opc_range_within(stream->zip_size, member->data_offset,
                          member->compressed_size)) return false;
    output_size = (size_t)member->uncompressed_size;
    packed = stream->zip + member->data_offset;
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!output) return false;
    if (member->method == 0U) {
        if ((uint64_t)member->compressed_size != member->uncompressed_size)
            goto fail;
        if (output_size != 0U) xx_rt_memcpy(output, packed, output_size);
        written = output_size;
        decoded = true;
    } else if (member->method == 8U) {
        decoded = xx_deflate_decompress_memory(packed,
                                               (size_t)member->compressed_size,
                                               output, output_size, &written,
                                               false);
    }
    if (!decoded || written != output_size ||
        xx_crc32_calc(0U, output, written) != member->crc32) goto fail;
    *plain = output;
    *plain_size = written;
    return true;
fail:
    xx_mem_free(output);
    return false;
}

void xx_opc_init(xx_opc *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_OPC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-os2point");
    xx_format_set_extension(&archive->format, "opc");
    archive->format.check_is_valid = xx_opc_check_is_valid;
    archive->format.handle_base_info = xx_opc_handle_base_info;
    archive->format.get_format_size = xx_opc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_opc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_opc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_opc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_opc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_opc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_opc_free_archive_records_reading;
}

xx_opc *xx_opc_create(xx_io_device *device, int64_t base_address) {
    xx_opc *archive = (xx_opc *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_opc_init(archive, device, base_address);
    return archive;
}

void xx_opc_destroy(xx_opc *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_opc_free(xx_opc *archive) {
    if (!archive) return;
    xx_opc_destroy(archive);
    xx_mem_free(archive);
}

bool xx_opc_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    opc_stream *stream;
    (void)pd;
    if (!opc_parse(format, &stream)) return false;
    opc_stream_free(stream);
    return true;
}

bool xx_opc_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    opc_stream *stream;
    xx_opc *archive;
    (void)pd;
    if (!format || !opc_parse(format, &stream)) return false;
    archive = (xx_opc *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    opc_stream_free(stream);
    return true;
}

int64_t xx_opc_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_opc_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_opc_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_opc_handle_base_info(format, pd))
               ? ((xx_opc *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_opc_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    opc_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!opc_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        opc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = opc_stream_free;
    state->total_records = stream->count;
    if (!opc_copy_options(&state->options, options) ||
        !opc_set_record(&state->current_record, &stream->items[0],
                        format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_opc_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_opc_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    opc_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (opc_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = opc_set_record(&state->current_record,
                                       &stream->items[stream->index],
                                       format->base_address);
    return state->has_record;
}

bool xx_opc_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    opc_stream *stream;
    opc_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (opc_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!opc_safe_output_name(member->name) ||
        !opc_decode(stream, member, &plain, &plain_size)) goto done;
    path_option = opc_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
        created = destination != NULL;
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
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_opc_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
