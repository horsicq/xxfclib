/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the "SMSIPAK " distribution container, the .PAK volumes
 * shipped by the SMSI DOS/Win16 setup sets.  Layout ported from XArchive's
 * installers/xsmsipakarchive.cpp (XSmsiPakArchive::scanSmsiPak).
 *
 *   +0   8    "SMSIPAK "
 *   +8   u16  0x1a07 format stamp
 *   +10  u32  offset at which the member area ends -- NOT the file size, the
 *             volume index follows it
 *
 * then a flat chain of 34-byte records, each followed by its payload:
 *
 *   +0   12   member name, NUL padded (8.3, no path)
 *   +12  u8   terminator slot
 *   +13  u16  MS-DOS date
 *   +15  u16  MS-DOS time
 *   +17  u32  CRC-32 of the plaintext
 *   +21  u16  attribute word
 *   +23  u8   method: 1 = PKWARE DCL, 2 = stored
 *   +24  i32  plaintext size
 *   +28  i32  stored size
 *   +32  u16  zero
 *
 * After the last member sits a volume index -- u32 zero, u16 member count,
 * then one u32 record offset per member.  It is optional but, when present,
 * cross-checks the walk and marks the true end of the container.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/smsipak/xx_smsipak.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef SMSIPAK
#define XX_SMSIPAK_FILE_TYPE XX_FILE_TYPE_SMSIPAK
#else
#define XX_SMSIPAK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SMSIPAK_HEADER_SIZE 14
#define SMSIPAK_RECORD_SIZE 34
#define SMSIPAK_NAME_SIZE 12U
#define SMSIPAK_INDEX_HEADER_SIZE 6
#define SMSIPAK_STAMP UINT16_C(0x1a07)
#define SMSIPAK_METHOD_DCL 1U
#define SMSIPAK_METHOD_STORE 2U
#define SMSIPAK_MAX_MEMBERS 65535U

typedef struct smsipak_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t dos_time;
    uint16_t attributes;
    uint8_t method;
} smsipak_member;

typedef struct smsipak_stream_s {
    smsipak_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} smsipak_stream;

static uint16_t smsipak_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t smsipak_le32(const uint8_t *bytes) {
    return (uint32_t)smsipak_le16(bytes) |
           ((uint32_t)smsipak_le16(bytes + 2U) << 16U);
}

static bool smsipak_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Member names are 8.3 DOS names with no path component.  Anything a host
 * filesystem would object to is neutralized; an empty name is rejected by the
 * caller rather than being invented. */
static char *smsipak_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input, output = 0U;
    name = (char *)xx_mem_alloc(size + 1U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c == 0U) break;
        if (c < 0x20U || c == '/' || c == '\\' || c == '"' || c == '*' ||
            c == ':' || c == '<' || c == '>' || c == '?' || c == '|')
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output != 0U && (name[output - 1U] == ' ' ||
                            name[output - 1U] == '.'))
        --output;
    name[output] = 0;
    return name;
}

static bool smsipak_safe_output_name(const char *name) {
    size_t length;
    if (!name || !name[0]) return false;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return false;
    for (length = 0U; name[length]; ++length) {
        unsigned char c = (unsigned char)name[length];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    return true;
}

static void smsipak_stream_free(void *opaque) {
    smsipak_stream *stream = (smsipak_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool smsipak_add_member(smsipak_stream *stream,
                               const smsipak_member *member) {
    smsipak_member *grown;
    if (!stream || !member || stream->count >= SMSIPAK_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (smsipak_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool smsipak_parse(Abstractformat *format, smsipak_stream **result) {
    uint8_t header[SMSIPAK_HEADER_SIZE];
    smsipak_stream *stream = NULL;
    int64_t total, size, member_end, cursor, archive_end, index_size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < SMSIPAK_HEADER_SIZE + SMSIPAK_RECORD_SIZE ||
        !smsipak_read_at(format->device, format->base_address, header,
                         sizeof(header)) ||
        xx_rt_memcmp(header, "SMSIPAK ", 8U) != 0 ||
        smsipak_le16(header + 8U) != SMSIPAK_STAMP)
        return false;

    /* +10 is where the members stop, not the file size. */
    member_end = (int64_t)smsipak_le32(header + 10U);
    if (member_end <= SMSIPAK_HEADER_SIZE || member_end > size) return false;

    stream = (smsipak_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    cursor = SMSIPAK_HEADER_SIZE;
    while (cursor < member_end) {
        uint8_t record[SMSIPAK_RECORD_SIZE];
        smsipak_member member;
        int32_t unpacked, packed;
        if (member_end - cursor < SMSIPAK_RECORD_SIZE ||
            !smsipak_read_at(format->device, format->base_address + cursor,
                             record, sizeof(record)))
            goto fail;
        if (smsipak_le16(record + 32U) != 0U) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.method = record[23];
        if (member.method != SMSIPAK_METHOD_DCL &&
            member.method != SMSIPAK_METHOD_STORE)
            goto fail;
        unpacked = (int32_t)smsipak_le32(record + 24U);
        packed = (int32_t)smsipak_le32(record + 28U);
        if (unpacked < 0 || packed < 0) goto fail;
        /* Bound the payload against the member area before it is trusted. */
        if (member_end - (cursor + SMSIPAK_RECORD_SIZE) < (int64_t)packed)
            goto fail;
        if (member.method == SMSIPAK_METHOD_STORE && packed != unpacked)
            goto fail;
        member.name = smsipak_normalize_name(record, SMSIPAK_NAME_SIZE);
        if (!member.name) goto fail;
        if (!member.name[0]) {
            xx_str_free(member.name);
            goto fail;
        }
        member.dos_time = ((uint32_t)smsipak_le16(record + 13U) << 16U) |
                          (uint32_t)smsipak_le16(record + 15U);
        member.crc32 = smsipak_le32(record + 17U);
        member.attributes = smsipak_le16(record + 21U);
        member.header_offset = format->base_address + cursor;
        member.data_offset = member.header_offset + SMSIPAK_RECORD_SIZE;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = (uint64_t)unpacked;
        if (!smsipak_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        cursor += SMSIPAK_RECORD_SIZE + (int64_t)packed;
    }
    if (stream->count == 0U || cursor != member_end) goto fail;

    /* The volume index repeats every record offset, so it is both the end of
     * the container and a cross-check on the walk above.  A volume that does
     * not carry one still reads. */
    archive_end = member_end;
    index_size = SMSIPAK_INDEX_HEADER_SIZE + (int64_t)stream->count * 4;
    if (size - member_end >= index_size) {
        uint8_t *index = (uint8_t *)xx_mem_alloc((size_t)index_size);
        bool valid = false;
        if (index &&
            smsipak_read_at(format->device, format->base_address + member_end,
                            index, (size_t)index_size)) {
            size_t i;
            valid = smsipak_le32(index) == 0U &&
                    (size_t)smsipak_le16(index + 4U) == stream->count;
            for (i = 0U; valid && i < stream->count; ++i) {
                int64_t offset = (int64_t)smsipak_le32(
                    index + SMSIPAK_INDEX_HEADER_SIZE + i * 4U);
                if (format->base_address + offset !=
                    stream->items[i].header_offset)
                    valid = false;
            }
        }
        if (index) xx_mem_free(index);
        if (valid) archive_end = member_end + index_size;
    }

    stream->archive_size = archive_end;
    *result = stream;
    return true;
fail:
    smsipak_stream_free(stream);
    return false;
}

static bool smsipak_copy_options(xx_list_s *destination,
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

static const xx_var *smsipak_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool smsipak_set_record(xx_archive_record *record,
                               const smsipak_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = SMSIPAK_RECORD_SIZE;
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
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool smsipak_decode_member(Abstractformat *format,
                                  const smsipak_member *member,
                                  uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->packed_size < 0 ||
        member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc(
        member->packed_size != 0 ? (size_t)member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        (member->packed_size != 0 &&
         !smsipak_read_at(format->device, member->data_offset, packed,
                          (size_t)member->packed_size)))
        goto fail;
    if (member->method == SMSIPAK_METHOD_STORE) {
        if (output_size != 0U) xx_rt_memcpy(output, packed, output_size);
        written = output_size;
        decoded = true;
    } else {
        decoded = xx_dcl_decode_memory(packed, (size_t)member->packed_size,
                                       output, output_size, &written);
    }
    if (!decoded || written != output_size ||
        xx_crc32_calc(0U, output, written) != member->crc32)
        goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_smsipak_init(xx_smsipak *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SMSIPAK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-smsipak");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_smsipak_check_is_valid;
    archive->format.handle_base_info = xx_smsipak_handle_base_info;
    archive->format.get_format_size = xx_smsipak_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_smsipak_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_smsipak_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_smsipak_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_smsipak_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_smsipak_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_smsipak_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_smsipak *xx_smsipak_create(xx_io_device *device, int64_t base_address) {
    xx_smsipak *archive = (xx_smsipak *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_smsipak_init(archive, device, base_address);
    return archive;
}

void xx_smsipak_destroy(xx_smsipak *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_smsipak_free(xx_smsipak *archive) {
    if (!archive) return;
    xx_smsipak_destroy(archive);
    xx_mem_free(archive);
}

bool xx_smsipak_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    smsipak_stream *stream;
    (void)pd;
    if (!smsipak_parse(format, &stream)) return false;
    smsipak_stream_free(stream);
    return true;
}

bool xx_smsipak_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    smsipak_stream *stream;
    xx_smsipak *archive;
    (void)pd;
    if (!format || !smsipak_parse(format, &stream)) return false;
    archive = (xx_smsipak *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    smsipak_stream_free(stream);
    return true;
}

int64_t xx_smsipak_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_smsipak_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_smsipak_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_smsipak_handle_base_info(format, pd))
               ? ((xx_smsipak *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_smsipak_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    smsipak_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!smsipak_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        smsipak_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = smsipak_stream_free;
    state->total_records = stream->count;
    if (!smsipak_copy_options(&state->options, options) ||
        !smsipak_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_smsipak_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_smsipak_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    smsipak_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (smsipak_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = smsipak_set_record(&state->current_record,
                                           &stream->items[stream->index]);
    return state->has_record;
}

bool xx_smsipak_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    smsipak_stream *stream;
    smsipak_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (smsipak_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!smsipak_safe_output_name(member->name) ||
        !smsipak_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = smsipak_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_smsipak_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
