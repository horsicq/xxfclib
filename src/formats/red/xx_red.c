/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Knowledge Dynamics Corp. RED installer archive, which
 * is also what its newer ".LIF" distribution volumes carry.  Ported from the
 * deark "red" module that XArchive vendors as
 * Algos/xdearkmodule_kdc_p.cpp (de_module_red / red_do_member).
 *
 * A file is a flat chain of members, each a header followed by its payload:
 *
 *   +0   u16be 0x5252 ("RR")
 *   +2   u8    format version, always 1
 *   +3   u8    header size, always 41 and never below 39
 *   +4   u16   MS-DOS time
 *   +6   u16   MS-DOS date
 *   +8   u32   compressed length
 *   +12  u32   plaintext length
 *   +16  u16   crc1 (0xFFFF across the corpus)
 *   +18  u16   crc2 - CRC-16/IBM-3740 of the plaintext
 *   +20  u16   fragment number
 *   +22  u16   last-fragment flag
 *   +24  u16   method: 1 = stored, 11 = LHA "-lh5-"
 *   +26  12    file name, NUL terminated
 *   ...        the last two bytes of the header are its own CRC-16/IBM-3740,
 *              big endian, taken over [+2 .. header_size-2)
 *
 * The next member starts at header_size + compressed length.
 *
 * A method 11 payload is NOT a bare -lh5- stream: the writer cuts the coded
 * data into 4094-byte segments and appends a two-byte CRC to each, so every
 * 4096-byte block has to lose its last two bytes before the codec sees it.
 * Decoding the payload as-is produces the first 4094 bytes and then garbage.
 * The ring starts filled with 0x20, which is what xx_lzh5_decode_memory does.
 *
 * Members that are fragments of a file split across volumes are listed but
 * fail to unpack: joining volumes is the caller's business, not this reader's.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/red/xx_red.h"

#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef RED
#define XX_RED_FILE_TYPE XX_FILE_TYPE_RED
#else
#define XX_RED_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RED_MIN_HEADER_SIZE 39
#define RED_STD_HEADER_SIZE 41
#define RED_NAME_OFFSET 26
#define RED_NAME_SIZE 12U
#define RED_MAX_MEMBERS 65535U
#define RED_SEGMENT_SIZE 4096
#define RED_METHOD_STORE 1U
#define RED_METHOD_LH5 11U
#define RED_MAX_UNPACKED UINT64_C(0x10000000)
#define RED_MAX_RATIO 1024U

typedef struct red_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t dos_time;
    uint16_t crc;
    uint16_t method;
    uint16_t fragment;
    uint16_t last_fragment;
} red_member;

typedef struct red_stream_s {
    red_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} red_stream;

static uint16_t red_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t red_le32(const uint8_t *bytes) {
    return (uint32_t)red_le16(bytes) | ((uint32_t)red_le16(bytes + 2U) << 16U);
}

/* CRC-16/IBM-3740: poly 0x1021, init 0xFFFF, no reflection, no final xor. */
static uint16_t red_crc16(const uint8_t *data, size_t size) {
    uint16_t crc = 0xFFFFU;
    size_t index;
    for (index = 0U; index < size; ++index) {
        unsigned bit;
        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x1021U)
                                  : (uint16_t)(crc << 1U);
    }
    return crc;
}

static bool red_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static char *red_normalize_name(const uint8_t *bytes, size_t size) {
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
    while (output != 0U &&
           (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    name[output] = 0;
    return name;
}

static bool red_safe_output_name(const char *name) {
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

static void red_stream_free(void *opaque) {
    red_stream *stream = (red_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool red_add_member(red_stream *stream, const red_member *member) {
    red_member *grown;
    if (!stream || !member || stream->count >= RED_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (red_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool red_parse(Abstractformat *format, red_stream **result) {
    red_stream *stream = NULL;
    uint8_t header[256];
    int64_t total, size, cursor = 0;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < RED_MIN_HEADER_SIZE) return false;

    stream = (red_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;

    while (cursor < size) {
        red_member member;
        int64_t header_size, packed;
        uint16_t reported, calculated;

        if (size - cursor < RED_MIN_HEADER_SIZE) goto fail;
        if (!red_read_at(format->device, format->base_address + cursor, header,
                         RED_MIN_HEADER_SIZE))
            goto fail;
        if (header[0] != 'R' || header[1] != 'R' || header[2] != 1U) goto fail;
        header_size = (int64_t)header[3];
        if (header_size < RED_MIN_HEADER_SIZE ||
            header_size > (int64_t)sizeof(header) || header_size > size - cursor)
            goto fail;
        if (header_size > RED_MIN_HEADER_SIZE &&
            !red_read_at(format->device, format->base_address + cursor, header,
                         (size_t)header_size))
            goto fail;

        /* The last two header bytes are its own big-endian CRC over the rest,
         * which is what makes the chain walk safe to trust. */
        reported = (uint16_t)(((uint16_t)header[header_size - 2] << 8U) |
                              header[header_size - 1]);
        calculated = red_crc16(header + 2, (size_t)(header_size - 4));
        if (reported != calculated) goto fail;

        packed = (int64_t)red_le32(header + 8U);
        if (packed < 0 || packed > size - cursor - header_size) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.unpacked_size = red_le32(header + 12U);
        member.crc = red_le16(header + 18U);
        member.fragment = red_le16(header + 20U);
        member.last_fragment = red_le16(header + 22U);
        member.method = red_le16(header + 24U);
        member.dos_time = ((uint32_t)red_le16(header + 6U) << 16U) |
                          (uint32_t)red_le16(header + 4U);
        if (member.method != RED_METHOD_STORE &&
            member.method != RED_METHOD_LH5)
            goto fail;
        if (member.unpacked_size > RED_MAX_UNPACKED) goto fail;
        /* A tiny payload cannot legitimately declare a huge plaintext. */
        if (member.unpacked_size >
            (uint64_t)packed * RED_MAX_RATIO + 0x10000U)
            goto fail;

        member.name = red_normalize_name(header + RED_NAME_OFFSET,
                                         RED_NAME_SIZE);
        if (!member.name) goto fail;
        if (!member.name[0]) {
            xx_str_free(member.name);
            goto fail;
        }
        member.header_offset = format->base_address + cursor;
        member.header_size = header_size;
        member.data_offset = member.header_offset + header_size;
        member.packed_size = packed;
        if (!red_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        cursor += header_size + packed;
    }
    if (stream->count == 0U || cursor != size) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    red_stream_free(stream);
    return false;
}

static bool red_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *red_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool red_set_record(xx_archive_record *record,
                           const red_member *member) {
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
                                          member->crc) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->fragment) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool red_decode_member(Abstractformat *format, const red_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *joined = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->packed_size < 0 ||
        member->unpacked_size > SIZE_MAX)
        return false;
    /* A fragment of a file split across volumes cannot be decoded alone. */
    if (member->fragment > 1U || member->last_fragment == 0U) return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc(
        member->packed_size != 0 ? (size_t)member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        (member->packed_size != 0 &&
         !red_read_at(format->device, member->data_offset, packed,
                      (size_t)member->packed_size)))
        goto fail;

    if (member->method == RED_METHOD_STORE) {
        if (output_size != (size_t)member->packed_size) goto fail;
        if (output_size != 0U) xx_rt_memcpy(output, packed, output_size);
        written = output_size;
        decoded = true;
    } else {
        /* Drop the two-byte CRC that closes every 4096-byte segment. */
        size_t remaining = (size_t)member->packed_size;
        size_t read_at = 0U, write_at = 0U;
        joined = (uint8_t *)xx_mem_alloc(remaining != 0U ? remaining : 1U);
        if (!joined) goto fail;
        while (read_at < remaining) {
            size_t block = remaining - read_at;
            if (block > (size_t)RED_SEGMENT_SIZE)
                block = (size_t)RED_SEGMENT_SIZE;
            if (block >= 2U) {
                xx_rt_memcpy(joined + write_at, packed + read_at, block - 2U);
                write_at += block - 2U;
            }
            read_at += block;
        }
        decoded = xx_lzh5_decode_memory(joined, write_at, output, output_size,
                                        5, &written);
        xx_mem_free(joined);
        joined = NULL;
    }
    if (!decoded || written != output_size) goto fail;
    if (red_crc16(output, written) != member->crc) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (joined) xx_mem_free(joined);
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_red_init(xx_red *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_RED_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-kdc-red");
    xx_format_set_extension(&archive->format, "red");
    archive->format.check_is_valid = xx_red_check_is_valid;
    archive->format.handle_base_info = xx_red_handle_base_info;
    archive->format.get_format_size = xx_red_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_red_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_red_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_red_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_red_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_red_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_red_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_red *xx_red_create(xx_io_device *device, int64_t base_address) {
    xx_red *archive = (xx_red *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_red_init(archive, device, base_address);
    return archive;
}

void xx_red_destroy(xx_red *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_red_free(xx_red *archive) {
    if (!archive) return;
    xx_red_destroy(archive);
    xx_mem_free(archive);
}

bool xx_red_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    red_stream *stream;
    (void)pd;
    if (!red_parse(format, &stream)) return false;
    red_stream_free(stream);
    return true;
}

bool xx_red_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    red_stream *stream;
    xx_red *archive;
    (void)pd;
    if (!format || !red_parse(format, &stream)) return false;
    archive = (xx_red *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    red_stream_free(stream);
    return true;
}

int64_t xx_red_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_red_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_red_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_red_handle_base_info(format, pd))
               ? ((xx_red *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_red_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    red_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!red_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        red_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = red_stream_free;
    state->total_records = stream->count;
    if (!red_copy_options(&state->options, options) ||
        !red_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_red_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_red_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    red_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (red_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = red_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_red_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    red_stream *stream;
    red_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (red_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!red_safe_output_name(member->name) ||
        !red_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = red_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_red_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
