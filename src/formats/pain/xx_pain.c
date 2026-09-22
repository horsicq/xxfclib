/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for the CRDATA00 container used by the PAIN diskmag family (.DAT,
 * .CR).  Header: "CRDATA00", uint32 directory_offset, uint32 member_count.
 * The directory is member_count entries of 80 bytes,
 *   { uint32 data_offset; uint32 method; uint32 packed; uint32 unpacked;
 *     char name[64]; }
 * with the bodies packed into [16, directory_offset).
 *
 * Method 0 is stored verbatim.  Method 3 prefixes the payload with a uint32
 * that is 1 for a block stored as is and 0 for a compressed one, and the
 * compressed form is an LZ that does not encode distances at all:
 *
 *   - 16-bit flag words, consumed LSB first; a clear bit is one literal byte
 *     and a set bit a two-byte match token;
 *   - the token's length is (token[0] & 0x0f) + 3 and its other twelve bits,
 *     (token[0] & 0xf0) << 4 | token[1], are an index into a 4096-entry table
 *     of earlier output positions - a slot number, not a distance;
 *   - both sides fill that table identically: hash(3 bytes) -> position, for
 *     every position whose three-byte window a literal has just completed,
 *     and after a match the token's own slot is re-pointed at the position
 *     the match started from.  The hash is
 *         ((b0 << 8) ^ (b1 << 4) ^ b2) * 0x9e5f >> 4 & 0xfff.
 *
 * That is why no window origin could ever reproduce the distances: there is
 * no window.  The scheme was recovered from U3's own decoder (F:\utils\U3\src,
 * format 368 "PAiN": FUN_005dd8a0 walks the directory, FUN_005dd770 picks the
 * method, FUN_005dd540 reads the block tag, FUN_005dd1c0 is the loop and
 * FUN_005dd190 the hash) and checked against U3's output: all 705 compressed
 * members in F:\ARC\ARC\PAIN decode byte for byte identically.
 *
 * Layout derived from the corpus in F:\ARC\ARC\PAIN; XArchive has no module
 * for this container.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pain/xx_pain.h"


#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef PAIN
#define XX_PAIN_FILE_TYPE XX_FILE_TYPE_PAIN
#else
#define XX_PAIN_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PAIN_MAX_MEMBERS 1048576U

typedef struct pain_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;
    uint32_t crc;
    uint32_t dos_time;
    bool folder;
} pain_member;

typedef struct pain_stream_s {
    pain_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} pain_stream;

static uint16_t pain_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t pain_le32(const uint8_t *bytes) {
    return (uint32_t)pain_le16(bytes) | ((uint32_t)pain_le16(bytes + 2U) << 16U);
}

static bool pain_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Every name field in these containers is a fixed-width buffer whose tail is
 * uninitialised builder heap, so only the bytes before the first NUL are ever
 * surfaced, and separators and traversal components are made harmless. */
static char *pain_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U, limit = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    /* The field is fixed width and its tail is stale builder heap, so the
     * name ends at the first NUL and everything after it is discarded. */
    while (limit < size && bytes[limit] != 0U) ++limit;
    size = limit;
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
                c == '>' || c == '?' || c == '|' || c == 0U)
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

/* A member name that survives to the filesystem must be a plain relative
 * path; anything else makes the member invalid rather than renamed. */
static bool pain_safe_output_name(const char *name) {
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

/* The raw 8.3 fields of these DOS-era containers are the only evidence that a
 * candidate offset really is a header, so a byte that cannot appear in a name
 * rejects the file instead of being scrubbed. */
static bool pain_plausible_raw_name(const uint8_t *bytes, size_t size) {
    size_t index;
    if (!bytes || size == 0U || bytes[0] == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U) return true;
        if (c < 0x20U || c == 0x7fU) return false;
    }
    return true;
}

static void pain_stream_free(void *opaque) {
    pain_stream *stream = (pain_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool pain_add_member(pain_stream *stream, const pain_member *member) {
    pain_member *grown;
    if (!stream || !member || stream->count >= PAIN_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (pain_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define PAIN_HEADER_SIZE 16U
#define PAIN_ENTRY_SIZE 80U
#define PAIN_NAME_SIZE 64U
#define PAIN_METHOD_STORE 0U
#define PAIN_METHOD_LZSS 3U

static bool pain_parse(Abstractformat *format, pain_stream **result) {
    uint8_t header[PAIN_HEADER_SIZE];
    uint8_t *directory = NULL;
    pain_stream *stream = NULL;
    int64_t total, size, directory_offset;
    uint32_t count, index;
    size_t directory_size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)PAIN_HEADER_SIZE ||
        !pain_read_at(format->device, format->base_address, header,
                      sizeof(header)) ||
        xx_rt_memcmp(header, "CRDATA00", 8U) != 0)
        return false;
    directory_offset = (int64_t)pain_le32(header + 8U);
    count = pain_le32(header + 12U);
    /* The directory must start inside the file and hold exactly count
     * entries, ending on the last byte. */
    if (count == 0U || count > PAIN_MAX_MEMBERS ||
        directory_offset < (int64_t)PAIN_HEADER_SIZE ||
        directory_offset > size ||
        (int64_t)count > (size - directory_offset) / (int64_t)PAIN_ENTRY_SIZE)
        return false;
    directory_size = (size_t)count * PAIN_ENTRY_SIZE;
    if (directory_offset + (int64_t)directory_size != size) return false;
    directory = (uint8_t *)xx_mem_alloc(directory_size);
    if (!directory ||
        !pain_read_at(format->device, format->base_address + directory_offset,
                      directory, directory_size)) {
        if (directory) xx_mem_free(directory);
        return false;
    }
    stream = (pain_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        xx_mem_free(directory);
        return false;
    }
    for (index = 0U; index < count; ++index) {
        const uint8_t *entry = directory + (size_t)index * PAIN_ENTRY_SIZE;
        pain_member member;
        uint32_t data_offset = pain_le32(entry);
        uint32_t method = pain_le32(entry + 4U);
        uint32_t packed = pain_le32(entry + 8U);
        uint32_t unpacked = pain_le32(entry + 12U);
        if (method != PAIN_METHOD_STORE && method != PAIN_METHOD_LZSS)
            goto fail;
        if (!pain_plausible_raw_name(entry + 16U, PAIN_NAME_SIZE)) goto fail;
        /* Every declared extent has to live inside the body region. */
        if ((int64_t)data_offset < (int64_t)PAIN_HEADER_SIZE ||
            (int64_t)data_offset > directory_offset ||
            (int64_t)packed > directory_offset - (int64_t)data_offset)
            goto fail;
        if (method == PAIN_METHOD_STORE && packed != unpacked) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = pain_normalize_name(entry + 16U, PAIN_NAME_SIZE);
        if (!member.name) goto fail;
        member.header_offset = format->base_address + directory_offset +
                               (int64_t)index * (int64_t)PAIN_ENTRY_SIZE;
        member.header_size = (int64_t)PAIN_ENTRY_SIZE;
        member.data_offset = format->base_address + (int64_t)data_offset;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = unpacked;
        member.method = method;
        if (!pain_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }
    xx_mem_free(directory);
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (directory) xx_mem_free(directory);
    pain_stream_free(stream);
    return false;
}

static bool pain_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *pain_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool pain_set_record(xx_archive_record *record,
                           const pain_member *member) {
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
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

#define PAIN_LZ_TABLE_SIZE 4096U
#define PAIN_LZ_BLOCK_HEADER 4U
#define PAIN_LZ_BLOCK_PACKED 0U
#define PAIN_LZ_BLOCK_STORED 1U
/* One token spends two input bytes on at most eighteen output bytes, and
 * sixteen tokens share one two-byte flag word: 288 bytes out for 34 in, so
 * nothing can legitimately expand by more than 8.5 times.  A member claiming
 * more than nine is refused before a byte is allocated. */
#define PAIN_MAX_EXPANSION 9U

/* The three-byte hash both sides of the codec index the position table with. */
static uint32_t pain_lz_hash(const uint8_t *at) {
    uint32_t key = ((uint32_t)at[0] << 8) ^ ((uint32_t)at[1] << 4) ^
                   (uint32_t)at[2];
    return ((key * UINT32_C(0x9E5F)) >> 4) & 0xFFFU;
}

/* The method 3 loop.  `table` holds output positions, -1 meaning a slot no
 * stream has written yet; every bound is checked against the real buffers
 * before anything is read or written. */
static bool pain_lz_decode(const uint8_t *in, size_t in_size, uint8_t *out,
                           size_t out_size, int64_t *table,
                           size_t *produced) {
    size_t at = 0U, op = 0U, index;
    uint32_t flags = 1U;
    int literals = 0;
    int64_t remaining = (int64_t)in_size;
    if ((!in && in_size != 0U) || (!out && out_size != 0U) || !table ||
        !produced)
        return false;
    for (index = 0U; index < PAIN_LZ_TABLE_SIZE; ++index) table[index] = -1;
    *produced = 0U;
    /* An empty block produces nothing; the caller still has to match that
     * against the declared plaintext length. */
    if (in_size == 0U) return true;
    for (;;) {
        if (flags == 1U) {
            if (in_size - at < 2U) return false;
            flags = ((uint32_t)in[at] | ((uint32_t)in[at + 1U] << 8)) |
                    UINT32_C(0x10000);
            at += 2U;
            remaining -= 2;
        }
        if ((flags & 1U) == 0U) {
            if (at >= in_size || op >= out_size) return false;
            out[op++] = in[at++];
            remaining -= 1;
            /* Three literals in a row complete a hashable window, and every
             * literal after that completes one more. */
            if (++literals == 3) {
                table[pain_lz_hash(out + op - 3U)] = (int64_t)(op - 3U);
                literals = 2;
            }
        } else {
            size_t length, start = op, slot;
            int64_t source;
            if (in_size - at < 2U) return false;
            length = (size_t)(in[at] & 0x0FU) + 3U;
            slot = (size_t)((((uint32_t)in[at] & 0xF0U) << 4) |
                            (uint32_t)in[at + 1U]);
            at += 2U;
            remaining -= 2;
            if (out_size - op < length) return false;
            source = table[slot];
            if (source < 0) {
                /* U3 fills a slot no stream has written with a rolling
                 * '1'..'9','0'.  No corpus member reaches it; it is kept so
                 * that a stream U3 accepts decodes the same way here. */
                uint8_t value = 0x31U;
                while (length-- != 0U) {
                    out[op++] = value;
                    if (++value == 0x3AU) value = 0x30U;
                }
            } else {
                size_t from = (size_t)source;
                /* Byte at a time: a match is allowed to overlap the bytes it
                 * is still producing. */
                while (length-- != 0U) out[op++] = out[from++];
            }
            /* Literals left pending by this match now have complete windows
             * of their own, the match having supplied the bytes after them. */
            if (literals > 0) {
                size_t begin = start - (size_t)literals;
                table[pain_lz_hash(out + begin)] = (int64_t)begin;
                if (literals == 2)
                    table[pain_lz_hash(out + begin + 1U)] =
                        (int64_t)(begin + 1U);
                literals = 0;
            }
            table[slot] = (int64_t)start;
        }
        flags >>= 1;
        if (remaining <= 0) break;
    }
    *produced = op;
    return true;
}

/* A method 3 payload is a uint32 block tag and then either the LZ stream or
 * the plaintext stored as is.  Nothing is emitted unless the block produces
 * exactly the plaintext length the directory declares. */
static bool pain_decode_lzss(Abstractformat *format, const pain_member *member,
                             uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    int64_t *table = NULL;
    size_t packed_size, body_size, output_size, produced = 0U;
    uint32_t tag;
    if (member->packed_size < (int64_t)PAIN_LZ_BLOCK_HEADER ||
        (uint64_t)member->packed_size > (uint64_t)SIZE_MAX ||
        member->unpacked_size > (uint64_t)SIZE_MAX)
        return false;
    packed_size = (size_t)member->packed_size;
    body_size = packed_size - PAIN_LZ_BLOCK_HEADER;
    /* Bound the declared plaintext by what the stream could possibly make of
     * its input, so a small member cannot ask for a large allocation. */
    if (member->unpacked_size > (uint64_t)body_size * PAIN_MAX_EXPANSION)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        !pain_read_at(format->device, member->data_offset, packed,
                      packed_size))
        goto fail;
    tag = pain_le32(packed);
    if (tag == PAIN_LZ_BLOCK_STORED) {
        if (body_size != output_size) goto fail;
        if (output_size != 0U)
            xx_rt_memcpy(output, packed + PAIN_LZ_BLOCK_HEADER, output_size);
    } else if (tag == PAIN_LZ_BLOCK_PACKED) {
        /* The position table is 32 KiB, which is too much to put on the
         * stack of a library call. */
        table = (int64_t *)xx_mem_alloc(PAIN_LZ_TABLE_SIZE * sizeof(*table));
        if (!table ||
            !pain_lz_decode(packed + PAIN_LZ_BLOCK_HEADER, body_size, output,
                            output_size, table, &produced) ||
            produced != output_size)
            goto fail;
        xx_mem_free(table);
        table = NULL;
    } else {
        goto fail;
    }
    xx_mem_free(packed);
    *plain = output;
    *plain_size = output_size;
    return true;
fail:
    if (table) xx_mem_free(table);
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

static bool pain_decode_member(Abstractformat *format,
                               const pain_member *member, uint8_t **plain,
                               size_t *plain_size) {
    uint8_t *output;
    size_t output_size;
    if (!format || !member || !plain || !plain_size ||
        member->packed_size < 0 || member->unpacked_size > SIZE_MAX)
        return false;
    if (member->method == PAIN_METHOD_LZSS)
        return pain_decode_lzss(format, member, plain, plain_size);
    if (member->method != PAIN_METHOD_STORE ||
        (uint64_t)member->packed_size != member->unpacked_size)
        return false;
    output_size = (size_t)member->unpacked_size;
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!output) return false;
    if (output_size != 0U &&
        !pain_read_at(format->device, member->data_offset, output,
                      output_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = output_size;
    return true;
}

void xx_pain_init(xx_pain *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PAIN_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-crdata");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_pain_check_is_valid;
    archive->format.handle_base_info = xx_pain_handle_base_info;
    archive->format.get_format_size = xx_pain_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pain_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pain_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pain_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pain_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pain_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pain_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_pain *xx_pain_create(xx_io_device *device, int64_t base_address) {
    xx_pain *archive = (xx_pain *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_pain_init(archive, device, base_address);
    return archive;
}

void xx_pain_destroy(xx_pain *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_pain_free(xx_pain *archive) {
    if (!archive) return;
    xx_pain_destroy(archive);
    xx_mem_free(archive);
}

bool xx_pain_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    pain_stream *stream;
    (void)pd;
    if (!pain_parse(format, &stream)) return false;
    pain_stream_free(stream);
    return true;
}

bool xx_pain_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    pain_stream *stream;
    xx_pain *archive;
    (void)pd;
    if (!format || !pain_parse(format, &stream)) return false;
    archive = (xx_pain *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    pain_stream_free(stream);
    return true;
}

int64_t xx_pain_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pain_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_pain_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pain_handle_base_info(format, pd))
               ? ((xx_pain *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_pain_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pain_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!pain_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pain_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pain_stream_free;
    state->total_records = stream->count;
    if (!pain_copy_options(&state->options, options) ||
        !pain_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_pain_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_pain_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    pain_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (pain_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = pain_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pain_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    pain_stream *stream;
    pain_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pain_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!pain_safe_output_name(member->name) ||
        !pain_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = pain_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
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
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_pain_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
