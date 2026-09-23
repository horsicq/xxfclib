/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Silmarils ALIS script container (.IO / .CO / .DO).  xx_silmarilsft.h
 * carries the field table.  The byte-run method (0x81) follows XArchive
 * (games/xsilmarils.cpp, Algos/xsilmarilsdecoder.cpp); the bit-stream method
 * (0xa1) follows the algorithm documented by the MIT-licensed silm-depack
 * project (github.com/maestun/silm-depack, unpack_new), re-derived and
 * checked against the 135 bit-stream members of the ARC Silmarils_FT corpus.
 *
 * Both decoders pull the packed stream through a small chunked reader, so
 * neither detection nor extraction ever holds the packed stream in memory;
 * only the plaintext (at most 16 MiB, the 24-bit size field) is allocated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/silmarilsft/xx_silmarilsft.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as SILMARILSFT is registered. */
#ifdef SILMARILSFT
#define XX_SILMARILSFT_FILE_TYPE XX_FILE_TYPE_SILMARILSFT
#else
#define XX_SILMARILSFT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SIL_SHORT_HEADER_SIZE 6
#define SIL_LONG_HEADER_SIZE 14
#define SIL_TABLE_OFFSET 6
#define SIL_TABLE_SIZE 8
#define SIL_METHOD_BYTERUN 0x81U
#define SIL_METHOD_BITSTREAM 0xa1U
/* The size field is 24 bits, so nothing beyond this can be described; the
 * file cap keeps the byte-run trial walk in detection bounded. */
#define SIL_MAX_RAW_SIZE INT64_C(0x00ffffff)
#define SIL_MAX_FILE_SIZE (INT64_C(64) * 1024 * 1024)
/* The bit-stream encoder sizes its output a word or two imprecisely: in the
 * corpus 92 of 135 streams end exactly, 28 carry one or two spare words and
 * 15 stop one or two words short of the final token.  The game reads past
 * the end regardless; this reader supplies zero words for at most this many
 * bytes past the end (sibling copies of the same asset then decode to the
 * same bytes), and it tolerates at most this many unread trailing bytes.
 * Anything further out is treated as a corrupt stream. */
#define SIL_BITSTREAM_SLACK 4U
#define SIL_CHUNK_SIZE 4096U

/* The 0xa1 parameter block never varies.  It is the only thing that makes
 * that method's header strong enough to detect on.  Entry v of the table is
 * the offset width used by match selector v. */
static const uint8_t sil_code_table[SIL_TABLE_SIZE] = {
    0x0bU, 0x09U, 0x0aU, 0x0bU, 0x07U, 0x05U, 0x06U, 0x07U};

typedef struct sil_context_s {
    int64_t input_size;
    int64_t header_size;
    int64_t stream_offset;
    int64_t stream_size;
    int64_t unpacked_size;
    uint32_t method;
    bool big_endian;
    bool supported;
} sil_context;

typedef struct sil_stream_s {
    sil_context context;
    size_t index;
    size_t count;
} sil_stream;

/* Bounded, chunked reader over [offset, offset + size) of the device.  Reads
 * past the end are refused; the bit reader decides what they mean. */
typedef struct sil_source_s {
    xx_io_device *device;
    int64_t offset;     /* device offset of the next chunk */
    int64_t remaining;  /* stream bytes not yet pulled into the chunk */
    size_t chunk_pos;
    size_t chunk_size;
    uint64_t consumed;  /* stream bytes handed out */
    uint8_t chunk[SIL_CHUNK_SIZE];
} sil_source;

static bool sil_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static void sil_source_init(sil_source *source, xx_io_device *device,
                            int64_t offset, int64_t size) {
    source->device = device;
    source->offset = offset;
    source->remaining = size;
    source->chunk_pos = 0U;
    source->chunk_size = 0U;
    source->consumed = 0U;
}

/* 1 = byte delivered, 0 = clean end of stream, -1 = I/O failure. */
static int sil_source_byte(sil_source *source, uint8_t *value) {
    if (source->chunk_pos >= source->chunk_size) {
        size_t amount;
        if (source->remaining <= 0) return 0;
        amount = source->remaining < (int64_t)SIL_CHUNK_SIZE
                     ? (size_t)source->remaining : (size_t)SIL_CHUNK_SIZE;
        if (!sil_read_at(source->device, source->offset, source->chunk,
                         amount)) return -1;
        source->offset += (int64_t)amount;
        source->remaining -= (int64_t)amount;
        source->chunk_pos = 0U;
        source->chunk_size = amount;
    }
    *value = source->chunk[source->chunk_pos++];
    ++source->consumed;
    return 1;
}

static uint32_t sil_u32(const uint8_t *bytes, bool big_endian) {
    return big_endian ? (((uint32_t)bytes[0] << 24U) |
                         ((uint32_t)bytes[1] << 16U) |
                         ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3])
                      : (((uint32_t)bytes[3] << 24U) |
                         ((uint32_t)bytes[2] << 16U) |
                         ((uint32_t)bytes[1] << 8U) | (uint32_t)bytes[0]);
}

/* Byte-run method 0x81.  Two token shapes, byte aligned, no end marker - the
 * stream simply runs to the declared plaintext size:
 *   c <  0x80  literal run: c raw bytes follow and are copied out
 *   c >= 0x80  byte run:    the NEXT byte is repeated (c & 0x7f) times
 * The counts are exact - neither token carries the "+1" bias that PackBits
 * and PDF /RunLengthDecode use.  The grammar has to produce exactly
 * `unpacked_size` bytes and claim exactly `stream_size` input bytes (the tail
 * of an over-long final literal run counts as claimed).  With `output` NULL
 * nothing is materialised, which is what the detection probe wants. */
static bool sil_byterun(sil_source *source, int64_t stream_size,
                        int64_t unpacked_size, uint8_t *output) {
    int64_t out = 0;
    uint64_t claimed = 0U;
    if (unpacked_size < 0 || stream_size < 0) return false;
    while (claimed < (uint64_t)stream_size && out < unpacked_size) {
        uint8_t token;
        if (sil_source_byte(source, &token) != 1) return false;
        ++claimed;
        if (token < 0x80U) {
            int64_t count = (int64_t)token, index;
            if ((uint64_t)stream_size - claimed < (uint64_t)count) return false;
            for (index = 0; index < count; ++index) {
                uint8_t value;
                if (sil_source_byte(source, &value) != 1) return false;
                if (out < unpacked_size) {
                    if (output) output[out] = value;
                    ++out;
                }
            }
            claimed += (uint64_t)count;
        } else {
            uint8_t value;
            int64_t take = (int64_t)(token & 0x7fU);
            if (claimed >= (uint64_t)stream_size ||
                sil_source_byte(source, &value) != 1) return false;
            ++claimed;
            if (take > unpacked_size - out) take = unpacked_size - out;
            if (output && take > 0)
                xx_rt_memset(output + out, (int)value, (size_t)take);
            out += take;
        }
    }
    return out == unpacked_size && claimed == (uint64_t)stream_size;
}

/* MSB-first bit reader over big-endian 16-bit words (both builds store the
 * stream the same way).  Past the end it yields zero words, at most
 * SIL_BITSTREAM_SLACK bytes' worth. */
typedef struct sil_bits_s {
    sil_source *source;
    uint64_t stream_size;
    uint64_t position;  /* bytes of word data taken, including padding */
    uint32_t word;
    unsigned available;
} sil_bits;

static bool sil_bits_refill(sil_bits *bits) {
    uint32_t word = 0U;
    unsigned index;
    if (bits->position + 2U > bits->stream_size + SIL_BITSTREAM_SLACK)
        return false;
    for (index = 0U; index < 2U; ++index) {
        uint8_t value = 0U;
        if (bits->position < bits->stream_size &&
            sil_source_byte(bits->source, &value) != 1) return false;
        word = (word << 8U) | value;
        ++bits->position;
    }
    bits->word = word;
    bits->available = 16U;
    return true;
}

static bool sil_bits_get(sil_bits *bits, unsigned count, uint32_t *value) {
    uint32_t result = 0U;
    if (count > 16U) return false;
    while (count != 0U) {
        unsigned take;
        if (bits->available == 0U && !sil_bits_refill(bits)) return false;
        take = count < bits->available ? count : bits->available;
        result = (result << take) |
                 ((bits->word >> (bits->available - take)) &
                  ((UINT32_C(1) << take) - 1U));
        bits->available -= take;
        count -= take;
    }
    *value = result;
    return true;
}

/* Sum `width`-bit groups while each group is all ones (the escape value). */
static bool sil_bits_count(sil_bits *bits, unsigned width, uint64_t cap,
                           uint64_t *total) {
    uint32_t escape = (UINT32_C(1) << width) - 1U, group;
    uint64_t sum = 0U;
    do {
        if (!sil_bits_get(bits, width, &group)) return false;
        sum += group;
        if (sum > cap) sum = cap;  /* saturate: the output clamps anyway */
    } while (group == escape);
    *total = sum;
    return true;
}

/* Bit-stream LZ method 0xa1:
 *   1 bit      1 = a literal run precedes the next match
 *   literal    run length = 1 + sum of 2-bit groups, continuing while a
 *              group is 3; then that many 8-bit literals
 *   3 bits     selector v; the offset is table[v] bits wide
 *   offset     distance = offset + 1
 *   length     v & 3 != 0: copy (v & 3) + 1 bytes
 *              v & 3 == 0: copy 5 + sum of 3-bit groups, continuing while a
 *              group is 7
 * The loop runs while fewer than `unpacked_size - 1` bytes exist and it is
 * left straight after a literal run that reaches that mark, so the final byte
 * may never be written; it stays zero, as in the corpus's sibling copies.
 * A token that runs past the end is clamped to the declared size. */
static bool sil_bitstream(sil_source *source, int64_t stream_size,
                          int64_t unpacked_size, const uint8_t *table,
                          uint8_t *output) {
    sil_bits bits;
    uint64_t size, out = 0U, last;
    unsigned index;
    if (!source || !table || !output || stream_size <= 0 ||
        unpacked_size <= 1) return false;
    for (index = 0U; index < SIL_TABLE_SIZE; ++index)
        if (table[index] == 0U || table[index] > 16U) return false;
    size = (uint64_t)unpacked_size;
    last = size - 1U;
    xx_rt_memset(output, 0, (size_t)size);
    bits.source = source;
    bits.stream_size = (uint64_t)stream_size;
    bits.position = 0U;
    bits.word = 0U;
    bits.available = 0U;
    while (out < last) {
        uint32_t flag, selector, offset;
        uint64_t length, distance, take, copy;
        if (!sil_bits_get(&bits, 1U, &flag)) return false;
        if (flag) {
            uint64_t run;
            if (!sil_bits_count(&bits, 2U, size, &run)) return false;
            for (run += 1U; run != 0U && out < size; --run) {
                uint32_t literal;
                if (!sil_bits_get(&bits, 8U, &literal)) return false;
                output[out++] = (uint8_t)literal;
            }
            if (out >= last) break;
        }
        if (!sil_bits_get(&bits, 3U, &selector) ||
            !sil_bits_get(&bits, table[selector], &offset)) return false;
        if ((selector & 3U) != 0U) {
            length = (uint64_t)(selector & 3U) + 1U;
        } else {
            uint64_t extra;
            if (!sil_bits_count(&bits, 3U, size, &extra)) return false;
            length = extra + 5U;
        }
        distance = (uint64_t)offset + 1U;
        if (distance > out) return false;
        take = length < size - out ? length : size - out;
        /* Byte by byte: the source may overlap what is being written. */
        for (copy = 0U; copy < take; ++copy, ++out)
            output[out] = output[out - distance];
    }
    if (bits.position >= bits.stream_size)
        return bits.position - bits.stream_size <= SIL_BITSTREAM_SLACK;
    return bits.stream_size - bits.position <= SIL_BITSTREAM_SLACK;
}

static void sil_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool sil_parse(Abstractformat *format, sil_context *out) {
    uint8_t header[SIL_LONG_HEADER_SIZE];
    sil_context context;
    uint32_t packed;
    int64_t total, size, raw_size;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    xx_mem_zero(&context, sizeof(context));
    context.input_size = size;
    if (size <= (int64_t)SIL_LONG_HEADER_SIZE || size > SIL_MAX_FILE_SIZE)
        return false;
    if (!sil_read_at(format->device, format->base_address, header,
                     sizeof(header))) return false;
    /* Offset 4 is the "not the main script" word, always 1 in a resource
     * file.  It is the byte-order oracle: nothing else in the header is
     * constant across both builds.  A main script (word 0, followed by 16
     * bytes of VM specs) cannot be told apart by byte order and is not
     * claimed. */
    if (header[4] == 0x01U && header[5] == 0x00U)
        context.big_endian = false;
    else if (header[4] == 0x00U && header[5] == 0x01U)
        context.big_endian = true;
    else
        return false;
    packed = sil_u32(header, context.big_endian);
    context.method = packed >> 24U;
    raw_size = (int64_t)(packed & 0x00ffffffU);
    if (context.method != SIL_METHOD_BYTERUN &&
        context.method != SIL_METHOD_BITSTREAM) return false;
    /* The raw size counts the six-byte header, so anything at or below it
     * describes an empty member and is not a container this reader claims. */
    if (raw_size <= (int64_t)SIL_SHORT_HEADER_SIZE ||
        raw_size > SIL_MAX_RAW_SIZE) return false;
    context.unpacked_size = raw_size - SIL_SHORT_HEADER_SIZE;
    /* Every member of the reference corpus - both methods, both byte orders -
     * decodes to a whole number of eight-byte resource units.  It is a cheap,
     * very discriminating extra constraint on a header this thin. */
    if ((context.unpacked_size % 8) != 0) return false;
    context.header_size = context.method == SIL_METHOD_BITSTREAM
                              ? SIL_LONG_HEADER_SIZE : SIL_SHORT_HEADER_SIZE;
    context.stream_offset = context.header_size;
    context.stream_size = size - context.header_size;
    if (context.stream_size <= 0) return false;
    context.supported = true;
    if (context.method == SIL_METHOD_BITSTREAM) {
        if (xx_rt_memcmp(header + SIL_TABLE_OFFSET, sil_code_table,
                         SIL_TABLE_SIZE) != 0) return false;
        /* Structural limits of the codec: at best a 3-bit length group buys
         * seven bytes (under 19 output bytes per input byte, with the zero
         * slack counted), and at worst a literal costs 8 2/3 bits.  The
         * lower bound is left loose; the parameter block does the real
         * detection work for this method. */
        if (context.unpacked_size >
                19 * (context.stream_size + (int64_t)SIL_BITSTREAM_SLACK) ||
            context.stream_size > 2 * context.unpacked_size + 64)
            return false;
    } else {
        sil_source source;
        /* Structural ceiling on the byte-run codec: a run token spends two
         * input bytes per output byte at worst, a literal run one count byte
         * per 127, and the final literal run may overshoot by at most 127.
         * Anything fatter cannot be this stream. */
        if (context.stream_size > 2 * context.unpacked_size + 128) return false;
        /* A six-byte header with a version word is far too weak to hand an
         * arbitrary file to a decoder.  Walk the real token grammar: it has
         * to produce exactly the declared plaintext length and land exactly
         * on the last input byte.  That is what keeps this reader from
         * stealing files and, equally, from being stolen from. */
        sil_source_init(&source, format->device,
                        format->base_address + context.stream_offset,
                        context.stream_size);
        if (!sil_byterun(&source, context.stream_size, context.unpacked_size,
                         NULL)) return false;
    }
    context.stream_offset += format->base_address;
    *out = context;
    return true;
}

static bool sil_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *sil_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* The container stores no member name, so the single member gets a fixed
 * one.  Nothing in the file could supply a better one, and inventing a name
 * from the host file would make the listing depend on where the archive
 * happens to live. */
static const char sil_member_name[] = "silmarils.bin";

static bool sil_set_record(xx_archive_record *record,
                           const sil_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->stream_offset - context->header_size;
    record->header_size = context->header_size;
    record->data_offset = context->stream_offset;
    record->compressed_size = context->stream_size;
    return xx_archive_record_set_original_name(record, sil_member_name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          context->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_silmarilsft_init(xx_silmarilsft *archive, xx_io_device *device,
                         int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SILMARILSFT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-silmarils");
    xx_format_set_extension(&archive->format, "io");
    archive->format.check_is_valid = xx_silmarilsft_check_is_valid;
    archive->format.handle_base_info = xx_silmarilsft_handle_base_info;
    archive->format.get_format_size = xx_silmarilsft_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_silmarilsft_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_silmarilsft_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_silmarilsft_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_silmarilsft_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_silmarilsft_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_silmarilsft_free_archive_records_reading;
}

xx_silmarilsft *xx_silmarilsft_create(xx_io_device *device,
                                      int64_t base_address) {
    xx_silmarilsft *archive = (xx_silmarilsft *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_silmarilsft_init(archive, device, base_address);
    return archive;
}

void xx_silmarilsft_destroy(xx_silmarilsft *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_silmarilsft_free(xx_silmarilsft *archive) {
    if (!archive) return;
    xx_silmarilsft_destroy(archive);
    xx_mem_free(archive);
}

bool xx_silmarilsft_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    sil_context context;
    (void)pd;
    return sil_parse(format, &context);
}

bool xx_silmarilsft_handle_base_info(Abstractformat *format,
                                     xx_pd_struct *pd) {
    sil_context context;
    xx_silmarilsft *archive;
    (void)pd;
    if (!format || !sil_parse(format, &context)) return false;
    archive = (xx_silmarilsft *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = (uint64_t)context.unpacked_size;
    archive->method = context.method;
    archive->big_endian = context.big_endian;
    archive->method_supported = context.supported;
    format->endian = context.big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    format->number_of_archive_records = 1U;
    format->format_size = context.input_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_silmarilsft_get_format_size(Abstractformat *format,
                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_silmarilsft_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_silmarilsft_get_number_of_archive_records(Abstractformat *format,
                                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_silmarilsft_handle_base_info(format, pd))
               ? ((xx_silmarilsft *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_silmarilsft_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    sil_stream *stream;
    xx_archive_record_state *state;
    sil_context context;
    (void)pd;
    if (!sil_parse(format, &context)) return NULL;
    stream = (sil_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sil_stream_free;
    state->total_records = 1U;
    if (!sil_copy_options(&state->options, options) ||
        !sil_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_silmarilsft_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_silmarilsft_archive_record_move_to_next(Abstractformat *format,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    sil_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (sil_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_silmarilsft_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    sil_stream *stream;
    sil_source source;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool decoded, result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (sil_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!stream->context.supported) return false;
    if (stream->context.unpacked_size <= 0 ||
        stream->context.unpacked_size > SIL_MAX_RAW_SIZE ||
        (uint64_t)stream->context.unpacked_size > (uint64_t)SIZE_MAX ||
        stream->context.stream_size <= 0)
        return false;
    plain_size = (size_t)stream->context.unpacked_size;
    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) goto done;
    sil_source_init(&source, format->device, stream->context.stream_offset,
                    stream->context.stream_size);
    if (stream->context.method == SIL_METHOD_BITSTREAM) {
        uint8_t table[SIL_TABLE_SIZE];
        /* Re-read rather than trust the constant: the device is the input. */
        decoded = sil_read_at(format->device,
                              stream->context.stream_offset -
                                  (int64_t)SIL_TABLE_SIZE,
                              table, sizeof(table)) &&
                  sil_bitstream(&source, stream->context.stream_size,
                                stream->context.unpacked_size, table, plain);
    } else {
        decoded = sil_byterun(&source, stream->context.stream_size,
                              stream->context.unpacked_size, plain);
    }
    if (!decoded) goto done;
    path_option = sil_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
               ? xx_str_concat3(base, "/", sil_member_name)
               : xx_str_concat(base, sil_member_name);
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

void xx_silmarilsft_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
