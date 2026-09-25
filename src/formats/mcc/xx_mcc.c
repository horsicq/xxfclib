/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the MCC registration container (.REG files shipped by
 * the mid-1990s "MCC" registration tool).  XArchive has no module for it and
 * the format is undocumented; the layout below was derived from the 17
 * corpus samples and cross-checked against F:\ARC\U3.exe, whose member list
 * and stored-member bytes this reader reproduces exactly.
 *
 * Every member is a 34-byte header followed immediately by its payload; the
 * chain runs to end of file with no central directory and no terminator:
 *
 *   0x00  3    "MCC"
 *   0x03  1    method: 0 = stored, 1 = compressed
 *   0x04  4    uncompressed size (LE)
 *   0x08  4    compressed size (LE), == uncompressed size when stored
 *   0x0C  1    XOR key applied to the whole method 1 payload.  It happens
 *              to equal the first payload byte because the encoded stream
 *              always opens with a zero byte; the key, not that accident,
 *              is what the decoder uses.
 *   0x0D  1    DOS file attributes (0x20 throughout the corpus)
 *   0x0E  1    reserved, zero throughout the corpus
 *   0x0F  4    DOS date/time (LE, time in the low half)
 *   0x13  2    CRC-16/BUYPASS of the PLAINTEXT (poly 0x8005, init 0, no
 *              reflection, no final xor).  Enforced on every member, so a
 *              decode is either right or refused.
 *   0x15  1    name length, 1..12
 *   0x16  12   file name, space padded
 *
 * Method 1 is a plain LZW stream that has been XORed byte-wise with the key
 * at 0x0C.  The LZW parameters come from U3's decompiled decoder
 * (F:\utils\U3\src, class `cra` at VMT 0x0055d5a8: slot 1 -> FUN_0055d9f0
 * lists, FUN_0055d740 unpacks, which wraps the source in the XOR filter
 * class `dfa` -- FUN_0049ae80 is literally `b ^= key` -- and then calls the
 * shared LZW engine FUN_004c55f0 configured by
 * FUN_004c5260(cfg, 0x0D, 0, 1, 1, 0, 0, 0, 0, 0)):
 *
 *   LSB-first bit packing, 9-bit initial code width growing to 13,
 *   code 0x100 = clear, code 0x101 = end, first dictionary code 0x102,
 *   width grows when the next free code would not fit the current width.
 *
 * All 17 corpus archives decode with every member's CRC matching and every
 * plaintext exactly the declared length.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mcc/xx_mcc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef MCC
#define XX_MCC_FILE_TYPE XX_FILE_TYPE_MCC
#else
#define XX_MCC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MCC_HEADER_SIZE 34
#define MCC_NAME_FIELD 12U
#define MCC_METHOD_STORE 0U
#define MCC_METHOD_PACKED 1U
#define MCC_MAX_MEMBERS 65536U

typedef struct mcc_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t dos_time;
    uint16_t checksum;
    uint8_t method;
    uint8_t attributes;
    uint8_t key;
} mcc_member;

typedef struct mcc_stream_s {
    mcc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} mcc_stream;

static uint16_t mcc_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t mcc_le32(const uint8_t *bytes) {
    return (uint32_t)mcc_le16(bytes) | ((uint32_t)mcc_le16(bytes + 2U) << 16U);
}

static bool mcc_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Names are 8.3 DOS names; anything outside the printable ASCII range or
 * carrying a path separator makes the header - and so the archive - invalid
 * rather than being repaired. */
static char *mcc_copy_name(const uint8_t *field, size_t length) {
    char *name;
    size_t index;
    if (length == 0U || length > MCC_NAME_FIELD) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = field[index];
        if (c < 0x20U || c > 0x7EU || c == (uint8_t)'/' ||
            c == (uint8_t)'\\' || c == (uint8_t)':' || c == (uint8_t)'*' ||
            c == (uint8_t)'?' || c == (uint8_t)'"' || c == (uint8_t)'<' ||
            c == (uint8_t)'>' || c == (uint8_t)'|') return NULL;
    }
    if (field[0] == (uint8_t)'.' &&
        (length == 1U || (length == 2U && field[1] == (uint8_t)'.')))
        return NULL;
    /* The trailing filler is spaces; the declared length already excludes it,
     * but a short name with embedded trailing blanks is still refused. */
    if (field[length - 1U] == (uint8_t)' ') return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    xx_rt_memcpy(name, field, length);
    name[length] = 0;
    return name;
}

/* CRC-16/BUYPASS: poly 0x8005, init 0, no reflection, no final xor.  This is
 * the checksum U3 computes over the plaintext of every member, and it is the
 * only thing that distinguishes a correct decode from a plausible one. */
static uint16_t mcc_crc16(const uint8_t *data, size_t size) {
    uint16_t crc = 0U;
    size_t index, bit;
    for (index = 0U; index < size; ++index) {
        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x8005U)
                                             : (uint16_t)(crc << 1U));
    }
    return crc;
}

/* LZW geometry, from U3's shared engine as configured for MCC. */
#define MCC_LZW_MAX_BITS 13U
#define MCC_LZW_MAX_CODES (1U << MCC_LZW_MAX_BITS)
#define MCC_LZW_CLEAR 0x100U
#define MCC_LZW_END 0x101U
#define MCC_LZW_FIRST 0x102U

/* The declared plaintext length is a bare u32 and the codec's expansion
 * ratio is unbounded, so both are capped before anything is allocated. */
#define MCC_MAX_OUTPUT UINT32_C(0x8000000)

typedef struct mcc_lzw_s {
    const uint8_t *input;
    size_t input_size;
    size_t input_at;
    uint8_t key;
    uint32_t accumulator;
    uint32_t bits_held;
    uint8_t *output;
    size_t output_limit;
    size_t produced;
    uint16_t prefix[MCC_LZW_MAX_CODES];
    uint8_t suffix[MCC_LZW_MAX_CODES];
    uint8_t stack[MCC_LZW_MAX_CODES];
} mcc_lzw;

/* LSB-first, and the payload is deciphered on the way in so the XOR filter
 * never needs a second buffer. */
static bool mcc_lzw_read_code(mcc_lzw *lzw, uint32_t width, uint32_t *code) {
    while (lzw->bits_held < width) {
        uint8_t byte;
        if (lzw->input_at >= lzw->input_size) return false;
        byte = (uint8_t)(lzw->input[lzw->input_at++] ^ lzw->key);
        lzw->accumulator |= (uint32_t)byte << lzw->bits_held;
        lzw->bits_held += 8U;
    }
    *code = lzw->accumulator & ((1U << width) - 1U);
    lzw->accumulator >>= width;
    lzw->bits_held -= width;
    return true;
}

static bool mcc_lzw_emit(mcc_lzw *lzw, uint8_t value) {
    if (lzw->produced >= lzw->output_limit) return false;
    lzw->output[lzw->produced++] = value;
    return true;
}

/* Returns true only on the explicit end code; a stream that simply runs out
 * of input is a decode failure, not a short member. */
static bool mcc_lzw_decode(mcc_lzw *lzw) {
    uint32_t width = 9U, limit = 0x1ffU, free_code = MCC_LZW_FIRST;
    uint32_t previous = 0U, first = 0U;
    bool have_previous = false;
    uint32_t index;
    for (index = 0U; index < 256U; ++index) {
        lzw->prefix[index] = 0U;
        lzw->suffix[index] = (uint8_t)index;
    }
    for (;;) {
        uint32_t code, walk;
        size_t depth = 0U;
        if (limit < free_code) {
            ++width;
            if (width > MCC_LZW_MAX_BITS) return false;
            limit = (width == MCC_LZW_MAX_BITS) ? MCC_LZW_MAX_CODES
                                                : ((1U << width) - 1U);
        }
        if (!mcc_lzw_read_code(lzw, width, &code)) return false;
        if (code == MCC_LZW_END) return true;
        if (code == MCC_LZW_CLEAR) {
            width = 9U;
            limit = 0x1ffU;
            free_code = MCC_LZW_FIRST;
            if (!mcc_lzw_read_code(lzw, width, &code)) return false;
            if (code == MCC_LZW_END) return true;
            if (code > 0xffU) return false;
            if (!mcc_lzw_emit(lzw, (uint8_t)code)) return false;
            previous = code;
            first = code;
            have_previous = true;
            continue;
        }
        if (code >= free_code) {
            /* The KwKwK case: the entry being referenced is the one this
             * step is about to create. */
            if (!have_previous || code > free_code) return false;
            lzw->stack[depth++] = (uint8_t)first;
            walk = previous;
        } else {
            walk = code;
        }
        while (walk > 0xffU) {
            if (walk >= MCC_LZW_MAX_CODES || depth >= MCC_LZW_MAX_CODES)
                return false;
            lzw->stack[depth++] = lzw->suffix[walk];
            walk = lzw->prefix[walk];
        }
        if (depth >= MCC_LZW_MAX_CODES) return false;
        lzw->stack[depth++] = (uint8_t)walk;
        first = walk;
        while (depth != 0U)
            if (!mcc_lzw_emit(lzw, lzw->stack[--depth])) return false;
        if (have_previous && free_code < MCC_LZW_MAX_CODES) {
            lzw->prefix[free_code] = (uint16_t)previous;
            lzw->suffix[free_code] = (uint8_t)walk;
            ++free_code;
        }
        previous = code;
        have_previous = true;
    }
}

static void mcc_stream_free(void *opaque) {
    mcc_stream *stream = (mcc_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool mcc_add_member(mcc_stream *stream, const mcc_member *member) {
    mcc_member *grown;
    if (!stream || !member || stream->count >= MCC_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (mcc_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) *
                                             sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool mcc_parse(Abstractformat *format, mcc_stream **result) {
    mcc_stream *stream = NULL;
    int64_t total, size, cursor;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < MCC_HEADER_SIZE) return false;
    stream = (mcc_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    cursor = 0;
    while (cursor < size) {
        uint8_t header[MCC_HEADER_SIZE];
        mcc_member member;
        uint32_t unpacked, packed;
        uint8_t method, name_length;
        if (size - cursor < MCC_HEADER_SIZE ||
            !mcc_read_at(format->device, format->base_address + cursor,
                         header, sizeof(header)) ||
            xx_rt_memcmp(header, "MCC", 3U) != 0) goto fail;
        method = header[3];
        unpacked = mcc_le32(header + 4U);
        packed = mcc_le32(header + 8U);
        name_length = header[21];
        if (method != MCC_METHOD_STORE && method != MCC_METHOD_PACKED)
            goto fail;
        if (method == MCC_METHOD_STORE && packed != unpacked) goto fail;
        /* Bound the declared payload against what the file actually holds
         * before it is used for anything. */
        if ((uint64_t)packed >
            (uint64_t)(size - cursor - MCC_HEADER_SIZE)) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = mcc_copy_name(header + 22U, name_length);
        if (!member.name) goto fail;
        member.method = method;
        member.attributes = header[13];
        member.dos_time = mcc_le32(header + 15U);
        member.checksum = mcc_le16(header + 19U);
        member.key = header[12];
        member.header_offset = format->base_address + cursor;
        member.data_offset = format->base_address + cursor + MCC_HEADER_SIZE;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = unpacked;
        if (!mcc_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        cursor += MCC_HEADER_SIZE + (int64_t)packed;
    }
    /* The chain must consume the file exactly; a short or long tail means
     * this is not an MCC archive. */
    if (cursor != size || stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    mcc_stream_free(stream);
    return false;
}

static bool mcc_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *mcc_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool mcc_set_record(xx_archive_record *record,
                           const mcc_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = MCC_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

/* Produces a member's plaintext, whatever its method, and refuses unless the
 * result is both the declared length and the declared CRC.  Callers get a
 * correct member or nothing. */
static bool mcc_decode_member(Abstractformat *format, const mcc_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *out = NULL, *packed = NULL;
    size_t out_size;
    if (!format || !member || !plain || !plain_size) return false;
    *plain = NULL;
    *plain_size = 0U;
    if (member->unpacked_size > (uint64_t)MCC_MAX_OUTPUT ||
        member->packed_size < 0 ||
        (uint64_t)member->packed_size > (uint64_t)MCC_MAX_OUTPUT)
        return false;
    out_size = (size_t)member->unpacked_size;
    if (member->method == MCC_METHOD_STORE) {
        if ((uint64_t)member->packed_size != member->unpacked_size)
            return false;
        if (out_size != 0U) {
            out = (uint8_t *)xx_mem_alloc(out_size);
            if (!out) return false;
            if (!mcc_read_at(format->device, member->data_offset, out,
                             out_size)) {
                xx_mem_free(out);
                return false;
            }
        }
    } else {
        mcc_lzw *lzw;
        bool decoded;
        if (out_size == 0U || member->packed_size == 0) return false;
        lzw = (mcc_lzw *)xx_mem_calloc(1U, sizeof(*lzw));
        packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
        out = (uint8_t *)xx_mem_alloc(out_size);
        if (!lzw || !packed || !out ||
            !mcc_read_at(format->device, member->data_offset, packed,
                         (size_t)member->packed_size)) {
            if (lzw) xx_mem_free(lzw);
            if (packed) xx_mem_free(packed);
            if (out) xx_mem_free(out);
            return false;
        }
        lzw->input = packed;
        lzw->input_size = (size_t)member->packed_size;
        lzw->key = member->key;
        lzw->output = out;
        lzw->output_limit = out_size;
        decoded = mcc_lzw_decode(lzw) && lzw->produced == out_size;
        xx_mem_free(packed);
        xx_mem_free(lzw);
        if (!decoded) {
            xx_mem_free(out);
            return false;
        }
    }
    /* The header's CRC is the anchor; without it a plausible decode is
     * indistinguishable from a right one. */
    if (mcc_crc16(out, out_size) != member->checksum) {
        if (out) xx_mem_free(out);
        return false;
    }
    *plain = out;
    *plain_size = out_size;
    return true;
}

void xx_mcc_init(xx_mcc *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MCC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mcc");
    xx_format_set_extension(&archive->format, "reg");
    archive->format.check_is_valid = xx_mcc_check_is_valid;
    archive->format.handle_base_info = xx_mcc_handle_base_info;
    archive->format.get_format_size = xx_mcc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mcc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mcc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mcc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mcc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mcc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mcc_free_archive_records_reading;
}

xx_mcc *xx_mcc_create(xx_io_device *device, int64_t base_address) {
    xx_mcc *archive = (xx_mcc *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_mcc_init(archive, device, base_address);
    return archive;
}

void xx_mcc_destroy(xx_mcc *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_mcc_free(xx_mcc *archive) {
    if (!archive) return;
    xx_mcc_destroy(archive);
    xx_mem_free(archive);
}

bool xx_mcc_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    mcc_stream *stream;
    (void)pd;
    if (!mcc_parse(format, &stream)) return false;
    mcc_stream_free(stream);
    return true;
}

bool xx_mcc_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    mcc_stream *stream;
    xx_mcc *archive;
    (void)pd;
    if (!format || !mcc_parse(format, &stream)) return false;
    archive = (xx_mcc *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    mcc_stream_free(stream);
    return true;
}

int64_t xx_mcc_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_mcc_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_mcc_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_mcc_handle_base_info(format, pd))
               ? ((xx_mcc *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_mcc_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    mcc_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!mcc_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mcc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = mcc_stream_free;
    state->total_records = stream->count;
    if (!mcc_copy_options(&state->options, options) ||
        !mcc_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_mcc_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_mcc_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    mcc_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (mcc_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = mcc_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_mcc_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    mcc_stream *stream;
    mcc_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (mcc_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!mcc_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = mcc_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_mcc_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
