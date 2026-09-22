/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for the .BCW container (SuperBase / Software Publishing installer
 * payloads).  Four signature bytes 0A 14 1E 28, then a flat sequence of
 * records with no table and no lengths anywhere:
 *
 *   u8 kind, then
 *     kind 1  file:   NUL-terminated name, u16 DOS date, u16 DOS time,
 *                     then the LZW stream, which ends at its own end code
 *     kind 2  enter:  NUL-terminated directory name, pushed onto the path
 *     kind 3  leave:  no payload; pops one directory
 *     kind 0  end of archive (end of file also ends it)
 *
 * Because a member's packed extent is only known once its LZW stream has
 * reached its end code, the walk has to run the codec to find the next
 * record.  Parsing therefore scans each stream - tracking the code widths
 * and the per-code expansion lengths, but never materialising bytes - which
 * yields both the packed extent and the exact plaintext length cheaply, and
 * refuses the file if any stream is malformed.
 *
 * The codec is the LZW engine U3 shares between several of its formats
 * (F:\utils\U3\src, class `aqa` at VMT 0x005521d8: slot 1 -> FUN_00552410
 * walks the records, FUN_005522c0 unpacks one, configuring the engine with
 * FUN_004c5260(cfg, 0x0C, 0, 1, 1, 0, 0, 1, 1, 0) and then calling
 * FUN_004c55f0).  That configuration means:
 *
 *   LSB-first bit packing, 9-bit initial code width growing to 12,
 *   code 0x100 = clear, code 0x101 = end, first dictionary code 0x102,
 *   EARLY CHANGE - the width grows one code before the dictionary fills -
 *   and the very first code of a stream is emitted as a literal rather than
 *   looked up.
 *
 * Verified over F:\ARC\ARC\BCW: 12 of 12 archives parse, every stream ends
 * on its end code, the record chain lands exactly on end of file, and all
 * 564 extracted files are byte-identical to U3's own output.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bcw/xx_bcw.h"


#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef BCW
#define XX_BCW_FILE_TYPE XX_FILE_TYPE_BCW
#else
#define XX_BCW_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BCW_MAX_MEMBERS 1048576U

typedef struct bcw_member_s {
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
} bcw_member;

typedef struct bcw_stream_s {
    bcw_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} bcw_stream;

static uint16_t bcw_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t bcw_le32(const uint8_t *bytes) {
    return (uint32_t)bcw_le16(bytes) | ((uint32_t)bcw_le16(bytes + 2U) << 16U);
}

static bool bcw_read_at(xx_io_device *device, int64_t offset, void *buffer,
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
static char *bcw_normalize_name(const uint8_t *bytes, size_t size) {
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
static bool bcw_safe_output_name(const char *name) {
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
static bool bcw_plausible_raw_name(const uint8_t *bytes, size_t size) {
    size_t index;
    if (!bytes || size == 0U || bytes[0] == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U) return true;
        if (c < 0x20U || c == 0x7fU) return false;
    }
    return true;
}

static void bcw_stream_free(void *opaque) {
    bcw_stream *stream = (bcw_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool bcw_add_member(bcw_stream *stream, const bcw_member *member) {
    bcw_member *grown;
    if (!stream || !member || stream->count >= BCW_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (bcw_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define BCW_MAX_NAME_SIZE 260U
#define BCW_MAX_DEPTH 32U
#define BCW_MAX_PATH 1024U

/* Record kinds. */
#define BCW_KIND_END 0U
#define BCW_KIND_FILE 1U
#define BCW_KIND_ENTER 2U
#define BCW_KIND_LEAVE 3U

/* LZW geometry, from U3's shared engine as configured for BCW. */
#define BCW_LZW_MAX_BITS 12U
#define BCW_LZW_MAX_CODES (1U << BCW_LZW_MAX_BITS)
/* The early-change rule lets the width reach one bit past the nominal
 * maximum before a clear code arrives; the engine does the same. */
#define BCW_LZW_HARD_BITS 13U
#define BCW_LZW_CLEAR 0x100U
#define BCW_LZW_END 0x101U
#define BCW_LZW_FIRST 0x102U

/* A member's plaintext length is not declared anywhere, so the scan's own
 * measurement is what bounds the allocation; this is the ceiling it may not
 * exceed. */
#define BCW_MAX_OUTPUT ((uint64_t)256 * 1024 * 1024)

/* A forward-only byte reader over the archive.  The record walk and the LZW
 * bit reader share it so that, after a stream reaches its end code, the
 * position is exactly where the next record byte begins - which is the only
 * way this container's member boundaries can be found at all. */
typedef struct bcw_reader_s {
    xx_io_device *device;
    int64_t position; /* absolute offset of buffer[at] */
    int64_t limit;    /* absolute end of the archive */
    size_t at;
    size_t filled;
    bool failed;
    uint8_t buffer[4096];
} bcw_reader;

static void bcw_reader_init(bcw_reader *reader, xx_io_device *device,
                            int64_t position, int64_t limit) {
    xx_mem_zero(reader, sizeof(*reader));
    reader->device = device;
    reader->position = position;
    reader->limit = limit;
}

/* Returns the next byte, or -1 at the end of the archive or on an I/O
 * failure; the two are told apart by reader->failed. */
static int bcw_reader_next(bcw_reader *reader) {
    if (reader->failed || reader->position >= reader->limit) return -1;
    if (reader->at >= reader->filled) {
        int64_t remaining = reader->limit - reader->position;
        size_t want = (remaining < (int64_t)sizeof(reader->buffer))
                          ? (size_t)remaining
                          : sizeof(reader->buffer);
        if (!bcw_read_at(reader->device, reader->position, reader->buffer,
                         want)) {
            reader->failed = true;
            return -1;
        }
        reader->filled = want;
        reader->at = 0U;
    }
    ++reader->position;
    return (int)reader->buffer[reader->at++];
}

/* Dictionary for one LZW pass.  The per-code expansion length is kept
 * alongside the usual prefix/suffix pair so a measuring pass can total the
 * plaintext without ever materialising a byte of it. */
typedef struct bcw_lzw_s {
    uint16_t prefix[BCW_LZW_MAX_CODES + 1U];
    uint8_t suffix[BCW_LZW_MAX_CODES + 1U];
    uint32_t length[BCW_LZW_MAX_CODES + 1U];
    uint8_t stack[BCW_LZW_MAX_CODES + 1U];
} bcw_lzw;

static bool bcw_lzw_read_code(bcw_reader *reader, uint32_t width,
                              uint32_t *accumulator, uint32_t *held,
                              uint32_t *code) {
    while (*held < width) {
        int byte = bcw_reader_next(reader);
        if (byte < 0) return false;
        *accumulator |= (uint32_t)byte << *held;
        *held += 8U;
    }
    *code = *accumulator & ((1U << width) - 1U);
    *accumulator >>= width;
    *held -= width;
    return true;
}

/* Runs one member's stream to its end code.  @p output may be NULL, in which
 * case nothing is written and only *produced is computed.  Returns false for
 * a stream that is malformed or that never reaches its end code: a member is
 * either complete or refused. */
static bool bcw_lzw_run(bcw_lzw *lzw, bcw_reader *reader, uint8_t *output,
                        uint64_t output_limit, uint64_t *produced) {
    uint32_t width = 9U, limit = 0x1ffU, free_code = BCW_LZW_FIRST;
    uint32_t accumulator = 0U, held = 0U, code = 0U, previous, first;
    uint32_t index;
    uint64_t total = 0U;

    for (index = 0U; index < 256U; ++index) {
        lzw->prefix[index] = 0U;
        lzw->suffix[index] = (uint8_t)index;
        lzw->length[index] = 1U;
    }
    /* The first code of a BCW stream is emitted as a literal without a
     * dictionary lookup; the engine reads it before entering its loop. */
    if (!bcw_lzw_read_code(reader, width, &accumulator, &held, &code))
        return false;
    if (code > 0xffU || output_limit == 0U) return false;
    if (output) output[0] = (uint8_t)code;
    total = 1U;
    previous = code;
    first = code;
    for (;;) {
        uint32_t walk, run;
        size_t depth = 0U;
        /* Early change: the width grows one code before the dictionary would
         * actually need it. */
        if (limit < free_code + 1U) {
            ++width;
            if (width > BCW_LZW_HARD_BITS) return false;
            limit = (width == BCW_LZW_MAX_BITS) ? BCW_LZW_MAX_CODES
                                                : ((1U << width) - 1U);
        }
        if (!bcw_lzw_read_code(reader, width, &accumulator, &held, &code))
            return false;
        if (code == BCW_LZW_END) break;
        if (code == BCW_LZW_CLEAR) {
            width = 9U;
            limit = 0x1ffU;
            free_code = BCW_LZW_FIRST;
            if (!bcw_lzw_read_code(reader, width, &accumulator, &held, &code))
                return false;
            if (code == BCW_LZW_END) break;
            if (code > 0xffU || total >= output_limit) return false;
            if (output) output[total] = (uint8_t)code;
            ++total;
            previous = code;
            first = code;
            continue;
        }
        /* A code past the next free slot references an entry that does not
         * exist yet and never will. */
        if (code > free_code) return false;
        /* KwKwK: a code equal to the next free slot expands to the previous
         * string plus its own first byte. */
        run = (code == free_code) ? lzw->length[previous] + 1U
                                  : lzw->length[code];
        if ((uint64_t)run > output_limit - total) return false;
        walk = (code == free_code) ? previous : code;
        if (output) {
            if (code == free_code) lzw->stack[depth++] = (uint8_t)first;
            while (walk > 0xffU) {
                if (depth > BCW_LZW_MAX_CODES) return false;
                lzw->stack[depth++] = lzw->suffix[walk];
                walk = lzw->prefix[walk];
            }
            if (depth > BCW_LZW_MAX_CODES) return false;
            lzw->stack[depth++] = (uint8_t)walk;
            while (depth != 0U) output[total++] = lzw->stack[--depth];
        } else {
            while (walk > 0xffU) walk = lzw->prefix[walk];
            total += run;
        }
        first = walk;
        if (free_code < BCW_LZW_MAX_CODES) {
            lzw->prefix[free_code] = (uint16_t)previous;
            lzw->suffix[free_code] = (uint8_t)first;
            lzw->length[free_code] = lzw->length[previous] + 1U;
            ++free_code;
        }
        previous = code;
    }
    *produced = total;
    return true;
}

/* Walks the record chain, measuring every member's stream on the way so the
 * next record's offset is known.  Nothing here trusts a declared size,
 * because the container declares none. */
static bool bcw_parse(Abstractformat *format, bcw_stream **result) {
    uint8_t header[4];
    bcw_stream *stream = NULL;
    bcw_reader reader;
    bcw_lzw *lzw = NULL;
    char path[BCW_MAX_PATH];
    size_t marks[BCW_MAX_DEPTH];
    size_t depth = 0U, path_length = 0U;
    int64_t total, size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)sizeof(header) + 1 ||
        !bcw_read_at(format->device, format->base_address, header,
                     sizeof(header)) ||
        header[0] != 0x0aU || header[1] != 0x14U || header[2] != 0x1eU ||
        header[3] != 0x28U)
        return false;
    stream = (bcw_stream *)xx_mem_calloc(1U, sizeof(*stream));
    lzw = (bcw_lzw *)xx_mem_alloc(sizeof(*lzw));
    if (!stream || !lzw) goto fail;
    bcw_reader_init(&reader, format->device,
                    format->base_address + (int64_t)sizeof(header), total);
    path[0] = 0;
    for (;;) {
        uint8_t raw[BCW_MAX_NAME_SIZE];
        size_t length = 0U;
        int64_t record_offset = reader.position;
        int kind = bcw_reader_next(&reader);
        if (kind < 0) {
            if (reader.failed) goto fail;
            break;
        }
        if (kind == (int)BCW_KIND_END) break;
        if (kind == (int)BCW_KIND_LEAVE) {
            /* A leave with nothing to leave is a malformed chain. */
            if (depth == 0U) goto fail;
            path_length = marks[--depth];
            path[path_length] = 0;
            continue;
        }
        if (kind != (int)BCW_KIND_FILE && kind != (int)BCW_KIND_ENTER)
            goto fail;
        /* The name is NUL-terminated and nothing states its length, so it is
         * read a byte at a time against a ceiling. */
        for (;;) {
            int byte = bcw_reader_next(&reader);
            if (byte < 0 || length >= sizeof(raw)) goto fail;
            if (byte == 0) break;
            if (byte < 0x20 || byte == 0x7f) goto fail;
            raw[length++] = (uint8_t)byte;
        }
        if (length == 0U) goto fail;
        if (kind == (int)BCW_KIND_ENTER) {
            size_t index;
            if (depth >= BCW_MAX_DEPTH) goto fail;
            marks[depth++] = path_length;
            if (path_length != 0U) {
                if (path_length + 1U >= sizeof(path)) goto fail;
                path[path_length++] = '/';
            }
            if (path_length + length >= sizeof(path)) goto fail;
            for (index = 0U; index < length; ++index)
                path[path_length++] = (char)raw[index];
            path[path_length] = 0;
            continue;
        }
        {
            /* A file record: the DOS timestamp, then the stream itself. */
            bcw_member member;
            uint8_t stamp[4];
            uint8_t joined[BCW_MAX_PATH];
            uint64_t produced = 0U;
            int64_t data_offset;
            size_t index, at = 0U;
            for (index = 0U; index < sizeof(stamp); ++index) {
                int byte = bcw_reader_next(&reader);
                if (byte < 0) goto fail;
                stamp[index] = (uint8_t)byte;
            }
            data_offset = reader.position;
            if (!bcw_lzw_run(lzw, &reader, NULL, BCW_MAX_OUTPUT, &produced))
                goto fail;
            xx_mem_zero(&member, sizeof(member));
            if (path_length + 1U + length >= sizeof(joined)) goto fail;
            for (index = 0U; index < path_length; ++index)
                joined[at++] = (uint8_t)path[index];
            if (at != 0U) joined[at++] = (uint8_t)'/';
            for (index = 0U; index < length; ++index) joined[at++] = raw[index];
            joined[at] = 0U;
            member.name = bcw_normalize_name(joined, at);
            if (!member.name) goto fail;
            member.header_offset = format->base_address + record_offset;
            member.header_size = data_offset - member.header_offset;
            member.data_offset = data_offset;
            member.packed_size = reader.position - data_offset;
            member.unpacked_size = produced;
            member.method = 1U;
            /* Date first, then time; republished in the packed order the
             * other DOS-era readers here use, with the time in the low
             * half.  SBLOCK.DLL carries 1991-06-25 12:53:00, which is the
             * stamp U3 puts on the file it writes. */
            member.dos_time = ((uint32_t)bcw_le16(stamp) << 16U) |
                              bcw_le16(stamp + 2U);
            member.folder = false;
            if (!bcw_add_member(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
        }
        if (stream->count >= BCW_MAX_MEMBERS) goto fail;
    }
    if (stream->count == 0U) goto fail;
    xx_mem_free(lzw);
    stream->archive_size = reader.position - format->base_address;
    *result = stream;
    return true;
fail:
    if (lzw) xx_mem_free(lzw);
    bcw_stream_free(stream);
    return false;
}

static bool bcw_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *bcw_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool bcw_set_record(xx_archive_record *record,
                           const bcw_member *member) {
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

/* Replays the member's stream, this time into a buffer sized by what the
 * parse measured; the two passes must agree exactly or the member is
 * refused. */
static bool bcw_decode_member(Abstractformat *format, const bcw_member *member,
                              uint8_t **plain, size_t *plain_size) {
    bcw_reader reader;
    bcw_lzw *lzw;
    uint8_t *output;
    uint64_t produced = 0U;
    if (!format || !member || !plain || !plain_size) return false;
    *plain = NULL;
    *plain_size = 0U;
    if (member->packed_size <= 0 || member->unpacked_size == 0U ||
        member->unpacked_size > BCW_MAX_OUTPUT ||
        member->unpacked_size > (uint64_t)SIZE_MAX)
        return false;
    lzw = (bcw_lzw *)xx_mem_alloc(sizeof(*lzw));
    output = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!lzw || !output) {
        if (lzw) xx_mem_free(lzw);
        if (output) xx_mem_free(output);
        return false;
    }
    bcw_reader_init(&reader, format->device, member->data_offset,
                    member->data_offset + member->packed_size);
    if (!bcw_lzw_run(lzw, &reader, output, member->unpacked_size, &produced) ||
        produced != member->unpacked_size) {
        xx_mem_free(lzw);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(lzw);
    *plain = output;
    *plain_size = (size_t)member->unpacked_size;
    return true;
}

void xx_bcw_init(xx_bcw *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BCW_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-bcw");
    xx_format_set_extension(&archive->format, "bcw");
    archive->format.check_is_valid = xx_bcw_check_is_valid;
    archive->format.handle_base_info = xx_bcw_handle_base_info;
    archive->format.get_format_size = xx_bcw_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bcw_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bcw_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bcw_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bcw_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bcw_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bcw_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_bcw *xx_bcw_create(xx_io_device *device, int64_t base_address) {
    xx_bcw *archive = (xx_bcw *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_bcw_init(archive, device, base_address);
    return archive;
}

void xx_bcw_destroy(xx_bcw *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_bcw_free(xx_bcw *archive) {
    if (!archive) return;
    xx_bcw_destroy(archive);
    xx_mem_free(archive);
}

bool xx_bcw_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    bcw_stream *stream;
    (void)pd;
    if (!bcw_parse(format, &stream)) return false;
    bcw_stream_free(stream);
    return true;
}

bool xx_bcw_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    bcw_stream *stream;
    xx_bcw *archive;
    (void)pd;
    if (!format || !bcw_parse(format, &stream)) return false;
    archive = (xx_bcw *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    bcw_stream_free(stream);
    return true;
}

int64_t xx_bcw_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_bcw_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_bcw_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_bcw_handle_base_info(format, pd))
               ? ((xx_bcw *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_bcw_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    bcw_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!bcw_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        bcw_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = bcw_stream_free;
    state->total_records = stream->count;
    if (!bcw_copy_options(&state->options, options) ||
        !bcw_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_bcw_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_bcw_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    bcw_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (bcw_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = bcw_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_bcw_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    bcw_stream *stream;
    bcw_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (bcw_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!bcw_safe_output_name(member->name) ||
        !bcw_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = bcw_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_bcw_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
