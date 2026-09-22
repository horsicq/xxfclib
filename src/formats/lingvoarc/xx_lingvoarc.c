/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LINGVOARC installer container.  The layout is recovered from U3's own
 * handler (VMT slot 0 at 0x0051d100 -> FUN_0051cd70 for the predicate, slot 1
 * at 0x0051d120 -> FUN_0051cdc0 for the walk); xx_lingvoarc.h carries the
 * field table.
 *
 * Payloads are nested FINEAR streams - a 17-byte header and an LHA -lh1- body
 * carrying a stored plaintext length and a CRC-16/ARC - which is the anchor
 * every decode here is checked against.
 *
 * The descriptor's u16 method word says whether a payload is whole.  Over the
 * 19-archive, 439-member reference corpus it takes exactly three values and
 * they line up perfectly with where the payload sits:
 *   0  self-contained FINEAR member                            408, all FINEAR
 *   6  first fragment of a member that continues on the NEXT
 *      volume: the FINEAR header is here, the body is cut off
 *      at the end of the file                   15, all FINEAR, all end at EOF
 *   2  continuation fragment, the tail of a member that began
 *      on the PREVIOUS volume, so it has no FINEAR header of
 *      its own                             16, none FINEAR, none end at EOF
 * A fragment cannot be decoded from one volume, so unpack refuses for methods
 * 2 and 6 the same way it refuses anything else that will not verify.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lingvoarc/xx_lingvoarc.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as LINGVOARC is registered. */
#ifdef LINGVOARC
#define XX_LINGVOARC_FILE_TYPE XX_FILE_TYPE_LINGVOARC
#else
#define XX_LINGVOARC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define LVA_HEADER_SIZE 18
#define LVA_V1_ENTRY_SIZE 54
#define LVA_V2_ENTRY_SIZE 109
#define LVA_V1_NAME_SIZE 46
#define LVA_V2_NAME_SIZE 101
#define LVA_V1_DESCRIPTOR_SIZE 23
#define LVA_V2_DESCRIPTOR_SIZE 71
#define LVA_V1_SIZE_OFFSET 13
#define LVA_V2_SIZE_OFFSET 61
#define LVA_MAX_ENTRIES 65535U
#define LVA_MAX_PAYLOAD ((int64_t)512 * 1024 * 1024)

/* The nested payload format: "FINEAR" dd 88 dd, u32 CRC-16/ARC, u32 plaintext
 * size, then an LHA -lh1- stream. */
#define LVA_FINEAR_HEADER_SIZE 17

typedef struct lva_member_s {
    char *name;
    int64_t descriptor_offset;
    int64_t data_offset;
    int64_t size;
    uint32_t timestamp;
    uint16_t method;
} lva_member;

typedef struct lva_stream_s {
    lva_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t version;
} lva_stream;

static uint16_t lva_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t lva_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool lva_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Member paths are DOS style and use a backslash separator.  Only the
 * filesystem-facing representation is normalised: separators are unified,
 * traversal components are resolved away and characters a path may not carry
 * become underscores. */
static char *lva_normalize_name(const uint8_t *bytes, size_t size) {
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

static bool lva_safe_output_name(const char *name) {
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

static void lva_stream_free(void *opaque) {
    lva_stream *stream = (lva_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool lva_parse(Abstractformat *format, lva_stream **result) {
    uint8_t header[LVA_HEADER_SIZE];
    uint8_t entry[LVA_V2_ENTRY_SIZE];
    uint8_t descriptor[LVA_V2_DESCRIPTOR_SIZE];
    lva_stream *stream = NULL;
    int64_t total, size, cursor, reach = 0;
    size_t entry_size, name_size, descriptor_size, size_offset;
    uint32_t count, index;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)LVA_HEADER_SIZE ||
        !lva_read_at(format->device, format->base_address, header,
                     sizeof(header))) return false;
    if (xx_rt_memcmp(header, "lingvoArc", 9U) != 0) return false;
    if (header[9] != '1' && header[9] != '2') return false;
    /* Six constant bytes; U3's own predicate tests them, and they are what
     * keeps a ten-byte ASCII magic from being the whole of the check. */
    if (header[10] != 0x00U || header[11] != 0xfdU || header[12] != 0x00U ||
        header[13] != 0xdfU || header[14] != 0x00U || header[15] != 0xffU)
        return false;
    count = lva_le16(header + 16U);
    if (count == 0U || count > LVA_MAX_ENTRIES) return false;
    if (header[9] == '1') {
        entry_size = LVA_V1_ENTRY_SIZE;
        name_size = LVA_V1_NAME_SIZE;
        descriptor_size = LVA_V1_DESCRIPTOR_SIZE;
        size_offset = LVA_V1_SIZE_OFFSET;
    } else {
        entry_size = LVA_V2_ENTRY_SIZE;
        name_size = LVA_V2_NAME_SIZE;
        descriptor_size = LVA_V2_DESCRIPTOR_SIZE;
        size_offset = LVA_V2_SIZE_OFFSET;
    }
    stream = (lva_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->version = (uint32_t)(header[9] - '0');
    stream->items = (lva_member *)xx_mem_calloc(count, sizeof(*stream->items));
    if (!stream->items) goto fail;
    cursor = LVA_HEADER_SIZE;
    for (index = 0U; index < count; ++index) {
        lva_member *member = &stream->items[index];
        int64_t descriptor_offset, payload_size, data_offset;
        size_t terminator = 0U;
        if (size - cursor < (int64_t)entry_size ||
            !lva_read_at(format->device, format->base_address + cursor, entry,
                         entry_size)) goto fail;
        cursor += (int64_t)entry_size;
        while (terminator < name_size && entry[terminator] != 0U)
            ++terminator;
        /* An entry whose name field never terminates is not this layout. */
        if (terminator == 0U || terminator >= name_size) goto fail;
        descriptor_offset = (int64_t)lva_le32(entry + name_size);
        if (descriptor_offset < (int64_t)LVA_HEADER_SIZE ||
            descriptor_offset > size - (int64_t)descriptor_size) goto fail;
        if (!lva_read_at(format->device,
                         format->base_address + descriptor_offset, descriptor,
                         descriptor_size)) goto fail;
        payload_size = (int64_t)lva_le32(descriptor + size_offset);
        if (payload_size < 0 || payload_size > LVA_MAX_PAYLOAD) goto fail;
        data_offset = descriptor_offset + (int64_t)descriptor_size;
        if (payload_size > size - data_offset) goto fail;
        member->name = lva_normalize_name(entry, terminator);
        if (!member->name) goto fail;
        stream->count = index + 1U;
        member->descriptor_offset = format->base_address + descriptor_offset;
        member->data_offset = format->base_address + data_offset;
        member->size = payload_size;
        member->timestamp = lva_le32(descriptor + size_offset + 4U);
        member->method = lva_le16(descriptor + size_offset + 8U);
        if (data_offset + payload_size > reach)
            reach = data_offset + payload_size;
    }
    /* The members tile the file: the furthest one ends on the last byte.  A
     * header this cheap needs that, and on real archives it holds exactly. */
    if (reach != size) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    lva_stream_free(stream);
    return false;
}

/* Decode a nested FINEAR payload, verifying its own stored plaintext length
 * and CRC-16/ARC.  Fails closed: a payload that is not FINEAR, or one that
 * does not reproduce both, is not returned at all. */
static bool lva_decode_member(Abstractformat *format, const lva_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t header[LVA_FINEAR_HEADER_SIZE];
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    uint32_t checksum, unpacked;
    int64_t packed_size;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->size <= (int64_t)LVA_FINEAR_HEADER_SIZE) return false;
    if (!lva_read_at(format->device, member->data_offset, header,
                     sizeof(header))) return false;
    if (xx_rt_memcmp(header, "FINEAR", 6U) != 0 || header[6] != 0xddU ||
        header[7] != 0x88U || header[8] != 0xddU) return false;
    checksum = lva_le32(header + 9U);
    unpacked = lva_le32(header + 13U);
    /* The checksum field is a 16-bit CRC written into a 32-bit slot. */
    if (checksum > 0xffffU) return false;
    packed_size = member->size - (int64_t)LVA_FINEAR_HEADER_SIZE;
    if ((uint64_t)unpacked > (uint64_t)LVA_MAX_PAYLOAD) return false;
    /* LHA -lh1- barely expands: only the coder's own framing can make a
     * member longer than its plaintext, and on the reference corpus that
     * costs a single byte (a one-byte file is stored in two).  The slack
     * below is deliberately generous - the real check is the stored length
     * and the CRC below, not this pre-filter. */
    if ((int64_t)unpacked + 64 < packed_size) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
    output = (uint8_t *)xx_mem_alloc(unpacked != 0U ? (size_t)unpacked : 1U);
    if (!packed || !output ||
        !lva_read_at(format->device,
                     member->data_offset + LVA_FINEAR_HEADER_SIZE, packed,
                     (size_t)packed_size)) goto fail;
    if (!xx_lzh1_decode_memory(packed, (size_t)packed_size, output,
                               (size_t)unpacked, &written) ||
        written != (size_t)unpacked ||
        (uint32_t)xx_crc16(XX_CRC_TYPE_CRC16_ARC, output, written) != checksum)
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

static bool lva_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *lva_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool lva_set_record(xx_archive_record *record,
                           const lva_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->descriptor_offset;
    record->header_size = member->data_offset - member->descriptor_offset;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_lingvoarc_init(xx_lingvoarc *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LINGVOARC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lingvoarc");
    xx_format_set_extension(&archive->format, "lva");
    archive->format.check_is_valid = xx_lingvoarc_check_is_valid;
    archive->format.handle_base_info = xx_lingvoarc_handle_base_info;
    archive->format.get_format_size = xx_lingvoarc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lingvoarc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lingvoarc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lingvoarc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lingvoarc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lingvoarc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lingvoarc_free_archive_records_reading;
}

xx_lingvoarc *xx_lingvoarc_create(xx_io_device *device, int64_t base_address) {
    xx_lingvoarc *archive = (xx_lingvoarc *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lingvoarc_init(archive, device, base_address);
    return archive;
}

void xx_lingvoarc_destroy(xx_lingvoarc *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_lingvoarc_free(xx_lingvoarc *archive) {
    if (!archive) return;
    xx_lingvoarc_destroy(archive);
    xx_mem_free(archive);
}

bool xx_lingvoarc_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    lva_stream *stream;
    (void)pd;
    if (!lva_parse(format, &stream)) return false;
    lva_stream_free(stream);
    return true;
}

bool xx_lingvoarc_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    lva_stream *stream;
    xx_lingvoarc *archive;
    (void)pd;
    if (!format || !lva_parse(format, &stream)) return false;
    archive = (xx_lingvoarc *)format;
    archive->number_of_records = stream->count;
    archive->container_version = stream->version;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    lva_stream_free(stream);
    return true;
}

int64_t xx_lingvoarc_get_format_size(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lingvoarc_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_lingvoarc_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lingvoarc_handle_base_info(format, pd))
               ? ((xx_lingvoarc *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_lingvoarc_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    lva_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!lva_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        lva_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = lva_stream_free;
    state->total_records = stream->count;
    if (!lva_copy_options(&state->options, options) ||
        !lva_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_lingvoarc_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_lingvoarc_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    lva_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (lva_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = lva_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lingvoarc_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    lva_stream *stream;
    lva_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (lva_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!lva_safe_output_name(member->name) ||
        !lva_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = lva_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_lingvoarc_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
