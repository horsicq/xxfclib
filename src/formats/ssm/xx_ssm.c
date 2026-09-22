/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for the Pegasus/Accusoft PICTools SSM opcode module.  Header:
 *   "SSM\0", char name[12], u16 dos_date, u16 dos_time, u32 unpacked,
 *   u32 window (0x2000 on every sample), u32 unused, u32 method
 * with the opcode stream at 0x24.  Method is 3 or 5, which differ only in the
 * eight-byte MZ prologue the decoder synthesises before the stream proper.
 *
 * The container identifies and reports its single member, and the PICTools
 * opcode codec is decoded in full below: a 192-entry literal permutation for
 * the 0x00..0xBF commands, short and long match commands over a 16 KiB
 * window, F0..FE literal runs and an FF FF terminator.  The grammar is a
 * port of XArchive's Algos/xssmdecoder.cpp; the layout was derived from the
 * corpus in F:\ARC\ARC\SSM together with that decoder's header notes.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ssm/xx_ssm.h"


#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef SSM
#define XX_SSM_FILE_TYPE XX_FILE_TYPE_SSM
#else
#define XX_SSM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SSM_MAX_MEMBERS 1048576U

/* The distance field of the long match command is 14 bits wide, so the codec
 * can never look further back than this. */
#define SSM_WINDOW_SIZE 16384U

/* The declared plaintext length is a bare u32, so it is capped before it is
 * allowed to size an allocation.  Every corpus module decodes to well under
 * a megabyte; this ceiling is generous rather than tight. */
#define SSM_MAX_UNPACKED UINT32_C(0x10000000)

typedef struct ssm_member_s {
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
} ssm_member;

typedef struct ssm_stream_s {
    ssm_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} ssm_stream;

static uint16_t ssm_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t ssm_le32(const uint8_t *bytes) {
    return (uint32_t)ssm_le16(bytes) | ((uint32_t)ssm_le16(bytes + 2U) << 16U);
}

static bool ssm_read_at(xx_io_device *device, int64_t offset, void *buffer,
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
static char *ssm_normalize_name(const uint8_t *bytes, size_t size) {
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
static bool ssm_safe_output_name(const char *name) {
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
static bool ssm_plausible_raw_name(const uint8_t *bytes, size_t size) {
    size_t index;
    if (!bytes || size == 0U || bytes[0] == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U) return true;
        if (c < 0x20U || c == 0x7fU) return false;
    }
    return true;
}

static void ssm_stream_free(void *opaque) {
    ssm_stream *stream = (ssm_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool ssm_add_member(ssm_stream *stream, const ssm_member *member) {
    ssm_member *grown;
    if (!stream || !member || stream->count >= SSM_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (ssm_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define SSM_HEADER_SIZE 0x24U
#define SSM_NAME_SIZE 12U

static bool ssm_parse(Abstractformat *format, ssm_stream **result) {
    uint8_t header[SSM_HEADER_SIZE];
    ssm_stream *stream = NULL;
    ssm_member member;
    int64_t total, size;
    uint32_t method, unpacked;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)SSM_HEADER_SIZE ||
        !ssm_read_at(format->device, format->base_address, header,
                     sizeof(header)) ||
        xx_rt_memcmp(header, "SSM\x00", 4U) != 0)
        return false;
    if (!ssm_plausible_raw_name(header + 4U, SSM_NAME_SIZE)) return false;
    method = ssm_le32(header + 0x20U);
    unpacked = ssm_le32(header + 0x14U);
    /* Only the two documented opcode revisions exist, and a module that
     * claims to decode to nothing is not a module. */
    if ((method != 3U && method != 5U) || unpacked == 0U ||
        unpacked > SSM_MAX_UNPACKED)
        return false;
    /* The stream always carries the synthesised MZ prologue plus its FF FF
     * terminator, so anything shorter than that cannot be a module. */
    if (unpacked < 8U || size < (int64_t)SSM_HEADER_SIZE + 2) return false;
    stream = (ssm_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = ssm_normalize_name(header + 4U, SSM_NAME_SIZE);
    if (!member.name) {
        ssm_stream_free(stream);
        return false;
    }
    member.header_offset = format->base_address;
    member.header_size = (int64_t)SSM_HEADER_SIZE;
    member.data_offset = format->base_address + (int64_t)SSM_HEADER_SIZE;
    member.packed_size = size - (int64_t)SSM_HEADER_SIZE;
    member.unpacked_size = unpacked;
    member.method = method;
    /* Packed the way every other DOS-era reader here publishes it: the date
     * in the high half, the time in the low one.  The corpus confirms the
     * field order - PICN4413 carries 1997-06-06 14:20:38, which is what U3
     * stamps on the file it writes. */
    member.dos_time = ((uint32_t)ssm_le16(header + 0x10U) << 16U) |
                      ssm_le16(header + 0x12U);
    if (!ssm_add_member(stream, &member)) {
        xx_str_free(member.name);
        ssm_stream_free(stream);
        return false;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
}

static bool ssm_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ssm_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ssm_set_record(xx_archive_record *record,
                           const ssm_member *member) {
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

/* PICTools gives the 192 most frequent byte values a one-byte command of
 * their own; every other value has to travel inside an F0..FE literal run.
 * The permutation is the same in methods 3 and 5. */
static const uint8_t ssm_literal_table[192] = {
    0x00, 0xff, 0x8b, 0x01, 0x45, 0x03, 0x80, 0xe8,
    0x02, 0x66, 0x89, 0x08, 0x26, 0x04, 0x67, 0x05,
    0x83, 0x77, 0x10, 0x50, 0xc3, 0xcd, 0x0a, 0x76,
    0x0f, 0xcb, 0x06, 0x90, 0x0c, 0xc0, 0x0b, 0xeb,
    0x85, 0xb7, 0xe9, 0xa1, 0x55, 0x47, 0xc7, 0x14,
    0x75, 0xf0, 0x74, 0x46, 0x09, 0x20, 0x18, 0x88,
    0x07, 0x4c, 0x56, 0xec, 0x8a, 0x8d, 0xc4, 0xc1,
    0x1c, 0x5e, 0x31, 0xe0, 0x0e, 0xb8, 0xf8, 0x3b,
    0x40, 0x3f, 0xfe, 0xd0, 0xc2, 0x72, 0x7d, 0x81,
    0x44, 0x52, 0x6a, 0xb6, 0x24, 0xe4, 0x57, 0xd8,
    0x1f, 0x0d, 0x5d, 0x1e, 0x8e, 0x5f, 0x16, 0xfc,
    0x34, 0xe5, 0x11, 0xa0, 0xd2, 0x30, 0xfa, 0x33,
    0x12, 0xc8, 0x8c, 0x65, 0x94, 0x2b, 0xc6, 0x7e,
    0x53, 0xdc, 0x32, 0x35, 0x1a, 0x64, 0x15, 0x19,
    0x7c, 0x38, 0x4e, 0xf4, 0xb0, 0x51, 0xd3, 0x73,
    0x5b, 0xa3, 0x29, 0x28, 0x4d, 0x3d, 0x82, 0x58,
    0xf6, 0xc9, 0xca, 0x36, 0x2c, 0x39, 0x78, 0xf7,
    0xe2, 0x84, 0xd1, 0x70, 0x5a, 0x13, 0x68, 0xd4,
    0xfb, 0x9a, 0x6c, 0xf3, 0xbd, 0x21, 0xcc, 0xb4,
    0x69, 0x3e, 0x2e, 0x60, 0x42, 0x3c, 0x98, 0x6e,
    0x1d, 0x54, 0xb3, 0xa4, 0xdb, 0x25, 0xb2, 0xa5,
    0x6f, 0x2d, 0xe1, 0x61, 0x49, 0x95, 0x5c, 0x59,
    0x6d, 0xea, 0xf2, 0x17, 0xd6, 0xbc, 0x4a, 0xa8,
    0xda, 0xef, 0x3a, 0x37, 0xfd, 0x22, 0x86, 0x2a
};

/* The two methods differ only in the eight-byte MZ prologue the decoder is
 * required to emit before the first command; the stream itself then matches
 * against those bytes like any other history. */
static const uint8_t ssm_prefix_method3[8] = {
    0x4d, 0x5a, 0x80, 0x00, 0x01, 0x00, 0x00, 0x00
};
static const uint8_t ssm_prefix_method5[8] = {
    0x4d, 0x5a, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00
};

static bool ssm_emit_byte(uint8_t *out, size_t limit, size_t *produced,
                          uint8_t value) {
    if (*produced >= limit) return false;
    out[(*produced)++] = value;
    return true;
}

/* The plaintext is contiguous in memory, so the 16 KiB circular history of
 * the original is just its own tail; the window bound is still enforced so a
 * corrupt distance cannot reach arbitrarily far back. */
static bool ssm_emit_match(uint8_t *out, size_t limit, size_t *produced,
                           size_t distance, size_t length) {
    size_t index;
    if (distance == 0U || distance > SSM_WINDOW_SIZE ||
        distance > *produced || length > limit - *produced)
        return false;
    for (index = 0U; index < length; ++index) {
        out[*produced] = out[*produced - distance];
        ++(*produced);
    }
    return true;
}

/* Port of XArchive's Algos/xssmdecoder.cpp.  The stream must consume exactly
 * the packed extent, end on FF FF and produce exactly the declared plaintext
 * length; any other outcome is a decode failure, never a short result. */
static bool ssm_inflate(const uint8_t *in, size_t in_size, uint32_t method,
                        uint8_t *out, size_t out_size) {
    const uint8_t *prefix =
        (method == 3U) ? ssm_prefix_method3 : ssm_prefix_method5;
    size_t at = 0U, produced = 0U, index;
    for (index = 0U; index < 8U; ++index)
        if (!ssm_emit_byte(out, out_size, &produced, prefix[index]))
            return false;
    while (at < in_size) {
        uint8_t command = in[at++];
        if (command < 0xc0U) {
            if (!ssm_emit_byte(out, out_size, &produced,
                               ssm_literal_table[command]))
                return false;
        } else if (command < 0xe0U) {
            /* Short match: three length bits in the command, ten distance
             * bits split between the command and one trailing byte. */
            size_t length, distance;
            if (at >= in_size) return false;
            length = (size_t)((command < 0xd0U) ? 3U : 7U) +
                     (size_t)((command >> 2) & 3U);
            distance = (((size_t)(command & 3U) << 8) | (size_t)in[at]) + 1U;
            ++at;
            if (!ssm_emit_match(out, out_size, &produced, distance, length))
                return false;
        } else if (command < 0xf0U) {
            /* Long match: a big-endian word carries fourteen distance bits
             * and the low two bits of the length. */
            uint32_t word;
            size_t length, distance;
            if (in_size - at < 2U) return false;
            word = ((uint32_t)in[at] << 8) | (uint32_t)in[at + 1U];
            at += 2U;
            length = 4U + 4U * (size_t)(command & 0x0fU) +
                     (size_t)(word >> 14U);
            distance = (size_t)(word & 0x3fffU) + 1U;
            if (!ssm_emit_match(out, out_size, &produced, distance, length))
                return false;
        } else if (command < 0xffU) {
            size_t length = (size_t)(command & 0x0fU) + 1U;
            if (in_size - at < length) return false;
            while (length-- != 0U)
                if (!ssm_emit_byte(out, out_size, &produced, in[at++]))
                    return false;
        } else {
            /* FF FF is the terminator and it must be the last thing in the
             * extent, with the plaintext already complete. */
            if (at >= in_size || in[at] != 0xffU) return false;
            ++at;
            return at == in_size && produced == out_size;
        }
    }
    return false;
}

static bool ssm_decode_member(Abstractformat *format, const ssm_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL, *out = NULL;
    size_t packed_size, out_size;
    if (!format || !member || !plain || !plain_size) return false;
    *plain = NULL;
    *plain_size = 0U;
    if (member->packed_size < 2 ||
        (uint64_t)member->packed_size > (uint64_t)SSM_MAX_UNPACKED ||
        member->unpacked_size < 8U ||
        member->unpacked_size > (uint64_t)SSM_MAX_UNPACKED)
        return false;
    packed_size = (size_t)member->packed_size;
    out_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed) return false;
    out = (uint8_t *)xx_mem_alloc(out_size);
    if (!out ||
        !ssm_read_at(format->device, member->data_offset, packed,
                     packed_size) ||
        !ssm_inflate(packed, packed_size, member->method, out, out_size)) {
        if (out) xx_mem_free(out);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *plain = out;
    *plain_size = out_size;
    return true;
}

void xx_ssm_init(xx_ssm *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SSM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pictools-ssm");
    xx_format_set_extension(&archive->format, "ssm");
    archive->format.check_is_valid = xx_ssm_check_is_valid;
    archive->format.handle_base_info = xx_ssm_handle_base_info;
    archive->format.get_format_size = xx_ssm_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ssm_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ssm_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ssm_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ssm_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ssm_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ssm_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_ssm *xx_ssm_create(xx_io_device *device, int64_t base_address) {
    xx_ssm *archive = (xx_ssm *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ssm_init(archive, device, base_address);
    return archive;
}

void xx_ssm_destroy(xx_ssm *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ssm_free(xx_ssm *archive) {
    if (!archive) return;
    xx_ssm_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ssm_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    ssm_stream *stream;
    (void)pd;
    if (!ssm_parse(format, &stream)) return false;
    ssm_stream_free(stream);
    return true;
}

bool xx_ssm_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    ssm_stream *stream;
    xx_ssm *archive;
    (void)pd;
    if (!format || !ssm_parse(format, &stream)) return false;
    archive = (xx_ssm *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    ssm_stream_free(stream);
    return true;
}

int64_t xx_ssm_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ssm_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_ssm_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ssm_handle_base_info(format, pd))
               ? ((xx_ssm *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_ssm_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ssm_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!ssm_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ssm_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ssm_stream_free;
    state->total_records = stream->count;
    if (!ssm_copy_options(&state->options, options) ||
        !ssm_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ssm_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_ssm_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    ssm_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ssm_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ssm_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ssm_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    ssm_stream *stream;
    ssm_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ssm_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!ssm_safe_output_name(member->name) ||
        !ssm_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = ssm_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_ssm_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
