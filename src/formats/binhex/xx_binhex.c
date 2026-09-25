/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for BinHex 4.0 (.hqx), Yves Lempereur's printable transport
 * encoding for Macintosh files.  Layout and the tolerance rules are ported
 * from XArchive's transport module (transport/xlegacyencoded.cpp).
 *
 * The file opens with the literal banner
 *   "(This file must be converted with BinHex 4.0)"
 * on its own line; the payload then runs between two ':' characters, encoded
 * six bits per character against a 64-character alphabet and broken across
 * lines at will.  Unpacking those six-bit groups back into bytes yields an
 * RLE90 stream (0x90 followed by a count repeats the previous byte; a count
 * of zero is a literal 0x90).  Decoding the RLE yields:
 *
 *   0                 uint8   name length, 1..63
 *   1                 name    Mac Roman
 *   1+n               uint8   version, always 0
 *   2+n .. 5+n        4       Mac file type
 *   6+n .. 9+n        4       Mac creator
 *   10+n .. 11+n      uint16  Finder flags
 *   12+n .. 15+n      uint32  data fork length
 *   16+n .. 19+n      uint32  resource fork length
 *   20+n .. 21+n      uint16  CRC-16/XMODEM of the preceding 20+n bytes
 *   22+n              data fork, then its CRC-16, then the resource fork and
 *                     its CRC-16
 *
 * The three CRCs are the decode anchors: the header CRC is what establishes
 * that this really is BinHex, and each fork is only emitted once its own CRC
 * verifies.  A truncated or damaged stream whose header CRC still verifies
 * is listed as a single unverified data-fork record whose extraction fails
 * closed, rather than silently producing a short file.
 *
 * The two forks are surfaced as two stored archive records, the resource
 * fork named "<name>.rsrc" as in the MacBinary and AppleSingle readers.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/binhex/xx_binhex.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* The enum entry is added by the coordinator; keep compiling until it is. */
#ifdef BINHEX
#define XX_BINHEX_FILE_TYPE XX_FILE_TYPE_BINHEX
#else
#define XX_BINHEX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define HQX_BANNER "(This file must be converted with BinHex"
#define HQX_BANNER_SIZE 40U
#define HQX_MAX_ENCODED ((int64_t)128 * 1024 * 1024)
#define HQX_MAX_DECODED ((size_t)128 * 1024 * 1024)
#define HQX_MAX_NAME 63U

static const char hqx_alphabet[65] =
    "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";

typedef struct hqx_member_s {
    char *name;
    uint8_t *data;
    size_t size;
    uint64_t declared_size;
    bool resource;
    bool verified;
} hqx_member;

typedef struct hqx_stream_s {
    hqx_member items[2];
    size_t count;
    size_t index;
    int64_t archive_size;
    int64_t payload_offset;
    uint32_t mac_type;
    uint32_t mac_creator;
    uint16_t finder_flags;
    bool complete;
} hqx_stream;

static uint16_t hqx_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t hqx_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool hqx_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool hqx_is_space(uint8_t c) {
    return c == '\r' || c == '\n' || c == '\t' || c == ' ';
}

static int hqx_alphabet_value(uint8_t c) {
    unsigned index;
    for (index = 0U; index < 64U; ++index)
        if ((uint8_t)hqx_alphabet[index] == c) return (int)index;
    return -1;
}

/* Mac Roman names may legally contain bytes a file system would choke on, so
 * only the filesystem-facing representation is sanitized. */
static char *hqx_normalize_name(const uint8_t *bytes, size_t size,
                                const char *suffix) {
    size_t suffix_size = suffix ? xx_str_len(suffix) : 0U;
    size_t input;
    size_t output = 0U;
    char *name;
    if (size == 0U || size > HQX_MAX_NAME) return NULL;
    name = (char *)xx_mem_alloc(size + suffix_size + 2U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c == 0x7fU)
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output != 0U &&
           (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    for (input = 0U; input < suffix_size; ++input)
        name[output++] = suffix[input];
    name[output] = 0;
    return name;
}

static bool hqx_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '.' || name[1] == ':') return false;
    for (at = name; *at; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*' || c < 0x20U)
            return false;
    }
    return true;
}

static void hqx_stream_free(void *opaque) {
    hqx_stream *stream = (hqx_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
        if (stream->items[index].data) xx_mem_free(stream->items[index].data);
    }
    xx_mem_free(stream);
}

static bool hqx_add_member(hqx_stream *stream, const char *name,
                           const uint8_t *source, size_t size,
                           uint64_t declared_size, bool resource,
                           bool verified) {
    hqx_member *member;
    if (!stream || stream->count >= 2U || !name) return false;
    member = &stream->items[stream->count];
    xx_mem_zero(member, sizeof(*member));
    member->name = xx_str_dup(name);
    if (!member->name) return false;
    member->data = (uint8_t *)xx_mem_alloc(size != 0U ? size : 1U);
    if (!member->data) {
        xx_str_free(member->name);
        member->name = NULL;
        return false;
    }
    if (size != 0U) xx_mem_copy(member->data, source, size);
    member->size = size;
    member->declared_size = declared_size;
    member->resource = resource;
    member->verified = verified;
    ++stream->count;
    return true;
}

/* Six-bit unpack followed by RLE90.  Both stages are bounded against
 * HQX_MAX_DECODED, and a dangling 0x90 is only tolerated at the end of a
 * stream that had no terminating ':' to begin with. */
static bool hqx_decode_payload(const uint8_t *source, size_t size,
                               size_t start, uint8_t **out, size_t *out_size,
                               bool *truncated, size_t *quartet_remainder,
                               size_t *trailing_zeros) {
    uint8_t *rle = NULL;
    uint8_t *decoded = NULL;
    size_t rle_size = 0U;
    size_t decoded_size = 0U;
    size_t capacity;
    size_t index;
    uint32_t bits = 0U;
    unsigned bit_count = 0U;
    size_t six_bit_count = 0U;
    int previous = -1;
    bool saw_end = false;

    capacity = size - start;
    rle = (uint8_t *)xx_mem_alloc(capacity != 0U ? capacity : 1U);
    if (!rle) return false;
    for (index = start; index < size; ++index) {
        uint8_t c = source[index];
        int value;
        if (c == ':') {
            saw_end = true;
            break;
        }
        value = hqx_alphabet_value(c);
        if (value < 0) {
            if (hqx_is_space(c)) continue;
            xx_mem_free(rle);
            return false;
        }
        ++six_bit_count;
        bits = (bits << 6U) | (uint32_t)value;
        bit_count += 6U;
        if (bit_count >= 8U) {
            bit_count -= 8U;
            if (rle_size >= capacity || rle_size >= HQX_MAX_DECODED) {
                xx_mem_free(rle);
                return false;
            }
            rle[rle_size++] = (uint8_t)((bits >> bit_count) & 0xffU);
            bits &= bit_count ? ((1U << bit_count) - 1U) : 0U;
        }
    }
    if (six_bit_count < 4U) {
        xx_mem_free(rle);
        return false;
    }

    /* RLE90 can expand by at most 255x, so grow the output on demand rather
     * than trusting any declared size. */
    capacity = rle_size + 1U;
    decoded = (uint8_t *)xx_mem_alloc(capacity);
    if (!decoded) {
        xx_mem_free(rle);
        return false;
    }
    for (index = 0U; index < rle_size;) {
        size_t repeat = 1U;
        uint8_t value;
        uint8_t byte = rle[index++];
        if (byte != 0x90U) {
            value = byte;
        } else {
            uint8_t count;
            if (index >= rle_size) {
                /* A dangling marker at the end of a truncated transfer has no
                 * count byte; a complete stream must not have one. */
                if (saw_end) goto fail;
                break;
            }
            count = rle[index++];
            if (count == 0U) {
                value = 0x90U;
            } else {
                if (count == 1U || previous < 0) goto fail;
                value = (uint8_t)previous;
                repeat = (size_t)count - 1U;
            }
        }
        if (repeat > HQX_MAX_DECODED - decoded_size) goto fail;
        if (decoded_size + repeat > capacity) {
            size_t wanted = decoded_size + repeat;
            uint8_t *grown;
            size_t next = capacity;
            while (next < wanted) {
                if (next > HQX_MAX_DECODED / 2U) {
                    next = wanted;
                    break;
                }
                next *= 2U;
            }
            grown = (uint8_t *)xx_mem_realloc(decoded, next);
            if (!grown) goto fail;
            decoded = grown;
            capacity = next;
        }
        while (repeat-- != 0U) decoded[decoded_size++] = value;
        previous = (int)value;
    }

    xx_mem_free(rle);
    *out = decoded;
    *out_size = decoded_size;
    *truncated = !saw_end;
    *quartet_remainder = six_bit_count % 4U;
    /* Count the literal zero bytes the last quartet may have padded with. */
    {
        size_t zeros = 0U;
        while (zeros < 2U && zeros < decoded_size &&
               decoded[decoded_size - 1U - zeros] == 0U)
            ++zeros;
        *trailing_zeros = zeros;
    }
    return true;
fail:
    xx_mem_free(rle);
    xx_mem_free(decoded);
    return false;
}

static bool hqx_parse(Abstractformat *format, hqx_stream **result) {
    uint8_t *source = NULL;
    uint8_t *decoded = NULL;
    hqx_stream *stream = NULL;
    int64_t total;
    int64_t size;
    size_t source_size;
    size_t index;
    size_t colon;
    size_t decoded_size = 0U;
    size_t quartet_remainder = 0U;
    size_t trailing_zeros = 0U;
    size_t name_length;
    size_t header_size;
    size_t payload_offset;
    uint64_t required;
    uint32_t data_size;
    uint32_t resource_size;
    bool truncated = false;
    bool complete;
    char *name = NULL;
    char *resource_name = NULL;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < 0 || format->base_address > total) return false;
    size = total - format->base_address;
    if (size < (int64_t)HQX_BANNER_SIZE + 8 || size > HQX_MAX_ENCODED)
        return false;
    source_size = (size_t)size;
    source = (uint8_t *)xx_mem_alloc(source_size);
    if (!source) return false;
    if (!hqx_read_at(format->device, format->base_address, source,
                     source_size) ||
        xx_rt_memcmp(source, HQX_BANNER, HQX_BANNER_SIZE) != 0)
        goto fail;

    /* The banner occupies its own line; the payload starts at the first ':'
     * after it. */
    for (index = 0U; index < source_size; ++index)
        if (source[index] == '\r' || source[index] == '\n') break;
    if (index >= source_size) goto fail;
    colon = index;
    while (colon < source_size && hqx_is_space(source[colon])) ++colon;
    if (colon >= source_size || source[colon] != ':') goto fail;

    if (!hqx_decode_payload(source, source_size, colon + 1U, &decoded,
                            &decoded_size, &truncated, &quartet_remainder,
                            &trailing_zeros))
        goto fail;

    if (decoded_size < 23U) goto fail;
    name_length = decoded[0];
    if (name_length < 1U || name_length > HQX_MAX_NAME ||
        decoded_size < 22U + name_length)
        goto fail;
    header_size = 20U + name_length;
    /* The header CRC is what establishes that this is really BinHex. */
    if (xx_crc16_xmodem_calc(0U, decoded, header_size) !=
        hqx_be16(decoded + header_size))
        goto fail;

    data_size = hqx_be32(decoded + 12U + name_length);
    resource_size = hqx_be32(decoded + 16U + name_length);
    payload_offset = 22U + name_length;
    if ((uint64_t)data_size + (uint64_t)resource_size >
        (uint64_t)HQX_MAX_DECODED)
        goto fail;
    required = (uint64_t)payload_offset + (uint64_t)data_size + 2U +
               (uint64_t)resource_size + 2U;
    /* Some encoders zero-pad the final quartet after RLE; at most two such
     * literal zero bytes are tolerated after the resource CRC. */
    complete = !truncated && required <= (uint64_t)decoded_size &&
               ((uint64_t)decoded_size == required ||
                (quartet_remainder == 0U &&
                 (uint64_t)decoded_size - required <= 2U &&
                 (uint64_t)decoded_size - required <= (uint64_t)trailing_zeros));

    name = hqx_normalize_name(decoded + 1U, name_length, NULL);
    if (!name) goto fail;

    stream = (hqx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->mac_type = hqx_be32(decoded + 2U + name_length);
    stream->mac_creator = hqx_be32(decoded + 6U + name_length);
    stream->finder_flags = hqx_be16(decoded + 10U + name_length);
    stream->archive_size = size;
    stream->payload_offset = format->base_address + (int64_t)colon;

    if (complete &&
        xx_crc16_xmodem_calc(0U, decoded + payload_offset, data_size) ==
            hqx_be16(decoded + payload_offset + data_size)) {
        size_t resource_offset = payload_offset + data_size + 2U;
        if (xx_crc16_xmodem_calc(0U, decoded + resource_offset,
                                 resource_size) !=
            hqx_be16(decoded + resource_offset + resource_size))
            goto fail;
        if (!hqx_add_member(stream, name, decoded + payload_offset, data_size,
                            data_size, false, true))
            goto fail;
        if (resource_size != 0U) {
            resource_name = hqx_normalize_name(decoded + 1U, name_length,
                                               ".rsrc");
            if (!resource_name ||
                !hqx_add_member(stream, resource_name,
                                decoded + resource_offset, resource_size,
                                resource_size, true, true))
                goto fail;
        }
        stream->complete = true;
    } else {
        /* The header CRC verified, so this is BinHex, but the stream is
         * truncated or the data fork is damaged.  List the recovered prefix
         * as unverified; extraction refuses it and the resource fork is
         * never emitted on this path. */
        size_t available = decoded_size > payload_offset
                               ? decoded_size - payload_offset
                               : 0U;
        if (available > data_size) available = data_size;
        if (available == 0U) goto fail;
        if (!hqx_add_member(stream, name, decoded + payload_offset, available,
                            data_size, false, false))
            goto fail;
    }

    xx_str_free(name);
    if (resource_name) xx_str_free(resource_name);
    xx_mem_free(decoded);
    xx_mem_free(source);
    *result = stream;
    return true;
fail:
    if (name) xx_str_free(name);
    if (resource_name) xx_str_free(resource_name);
    if (stream) hqx_stream_free(stream);
    if (decoded) xx_mem_free(decoded);
    if (source) xx_mem_free(source);
    return false;
}

static bool hqx_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *hqx_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool hqx_set_record(xx_archive_record *record, const hqx_stream *stream,
                           const hqx_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->resource ? -1 : 0;
    record->header_size = member->resource ? 0 : stream->payload_offset;
    record->data_offset = stream->payload_offset;
    record->compressed_size = stream->archive_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->archive_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->declared_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          stream->finder_flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          stream->mac_type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

void xx_binhex_init(xx_binhex *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_BINHEX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/mac-binhex40");
    xx_format_set_extension(&archive->format, "hqx");
    archive->format.check_is_valid = xx_binhex_check_is_valid;
    archive->format.handle_base_info = xx_binhex_handle_base_info;
    archive->format.get_format_size = xx_binhex_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_binhex_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_binhex_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_binhex_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_binhex_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_binhex_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_binhex_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_binhex *xx_binhex_create(xx_io_device *device, int64_t base_address) {
    xx_binhex *archive = (xx_binhex *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_binhex_init(archive, device, base_address);
    return archive;
}

void xx_binhex_destroy(xx_binhex *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_binhex_free(xx_binhex *archive) {
    if (!archive) return;
    xx_binhex_destroy(archive);
    xx_mem_free(archive);
}

bool xx_binhex_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    hqx_stream *stream;
    (void)pd;
    if (!hqx_parse(format, &stream)) return false;
    hqx_stream_free(stream);
    return true;
}

bool xx_binhex_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    hqx_stream *stream;
    xx_binhex *archive;
    (void)pd;
    if (!format || !hqx_parse(format, &stream)) {
        if (format) format->is_valid = false;
        return false;
    }
    archive = (xx_binhex *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->complete = stream->complete;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_BINHEX_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    hqx_stream_free(stream);
    return true;
}

int64_t xx_binhex_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_binhex_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_binhex_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_binhex_handle_base_info(format, pd))
               ? ((xx_binhex *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_binhex_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    hqx_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!hqx_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        hqx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = hqx_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!hqx_copy_options(&state->options, options) ||
        !hqx_set_record(&state->current_record, stream, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_binhex_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_binhex_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    hqx_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (hqx_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = hqx_set_record(&state->current_record, stream,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_binhex_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    hqx_stream *stream;
    const hqx_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    size_t written = 0U;
    bool result = false;
    bool created = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (hqx_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    /* A member whose fork CRC did not verify is never emitted. */
    if (!member->verified || !hqx_safe_output_name(member->name)) return false;
    path_option = hqx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
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
        while (written < member->size) {
            ssize_t amount = xx_io_write(destination, member->data + written,
                                         member->size - written);
            if (amount <= 0 || (size_t)amount > member->size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_binhex_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
