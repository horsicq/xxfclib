/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Compaq (later HP) SoftPaq distribution container,
 * ported from XArchive's xsoftpaq2.{h,cpp}.
 *
 * The file opens as a PKLITE compressed DOS extractor stub ("MZ" at +0 and
 * the "PKLITE Copr. 199..." banner at +0x1e) with the payload and a "[FIT]"
 * directory appended behind it.  The directory is anchored by a 37-byte
 * locator record that may sit anywhere in the file and identifies itself by
 * repeating its own file offset:
 *
 *   +0x00 char  tag[8]      "[FIT]" 00 01 00
 *   +0x10 int32 self_offset must equal the record's own offset
 *   +0x14 int32 dir_offset  first stored-entry record
 *   +0x18 int32 split       first compressed-entry record
 *
 * [dir_offset, split) is a table of 23-byte STORED entries and
 * [split, file_size) a table of 38-byte COMPRESSED entries; both tile their
 * range exactly, which is what makes the anchor safe to search for.
 *
 * STORED entry (23 bytes) -- the stub's own resources:
 *   +0x00 char  name[8]     space padded      +0x08 uint8 always 0
 *   +0x09 char  ext[4]      carries its own leading '.'
 *   +0x0d uint8 always 0    +0x0e int32 size  +0x12 int32 offset
 *   +0x16 uint8 published   0 = the extractor stub itself, not a member
 *
 * COMPRESSED entry (38 bytes) -- the payload proper:
 *   +0x00 char   name[8]    +0x08 uint8 always 0
 *   +0x09 char   ext[4]     +0x0d uint8 always 0
 *   +0x0e uint16 method     0 = stored, 6 = PKWARE DCL implode
 *   +0x10 uint16 dos_time   +0x12 uint16 dos_date
 *   +0x14 uint32 crc32      CRC-32 of the PACKED stream, not of the member
 *   +0x18 int32  uncompressed_size   +0x1c int32 compressed_size
 *   +0x20 uint16 attributes +0x22 int32 offset
 *
 * Method 6 is a plain PKWARE Data Compression Library implode stream with its
 * own literal-mode / dictionary-bits prelude still attached, so the shared
 * DCL decoder handles it unchanged.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/softpaq2/xx_softpaq2.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved through the alias macro that
 * the enumerator will define. Once XX_FILE_TYPE_SOFTPAQ2 lands the alias is
 * defined and this picks it up with no further change. */
#ifdef SOFTPAQ2
#define XX_SOFTPAQ2_FILE_TYPE XX_FILE_TYPE_SOFTPAQ2
#else
#define XX_SOFTPAQ2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- on-disk sizes ------------------------------------------------------ */

#define SOFTPAQ2_MIN_SIZE 0x40
#define SOFTPAQ2_PKLITE_OFFSET 0x1e
#define SOFTPAQ2_BANNER_SIZE 16U
#define SOFTPAQ2_STUB_PROBE (SOFTPAQ2_PKLITE_OFFSET + (int64_t)SOFTPAQ2_BANNER_SIZE)
#define SOFTPAQ2_LOCATOR_SIZE 0x25
#define SOFTPAQ2_TAG_SIZE 8U
#define SOFTPAQ2_STORED_ENTRY_SIZE 23U
#define SOFTPAQ2_PACKED_ENTRY_SIZE 38U
#define SOFTPAQ2_NAME_SIZE 8U
#define SOFTPAQ2_EXT_SIZE 4U
/* A directory can never be this large in practice; the cap only guards the
 * allocation that the entry count drives. */
#define SOFTPAQ2_MAX_ENTRIES 1000000U
#define SOFTPAQ2_SCAN_CHUNK 65536U

#define SOFTPAQ2_METHOD_STORED 0U
#define SOFTPAQ2_METHOD_IMPLODE 6U

static const char softpaq2_banner[SOFTPAQ2_BANNER_SIZE] = {
    'P', 'K', 'L', 'I', 'T', 'E', ' ', 'C', 'o', 'p', 'r', '.', ' ', '1', '9',
    '9'};
static const char softpaq2_tag[SOFTPAQ2_TAG_SIZE] = {'[', 'F', 'I', 'T', ']',
                                                     0,   1,   0};

typedef struct softpaq2_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint16_t method;
    uint16_t attributes;
    uint16_t dos_date;
    uint16_t dos_time;
    bool has_crc;
} softpaq2_member;

typedef struct softpaq2_stream_s {
    softpaq2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    int64_t locator_offset;
    int64_t directory_offset;
    int64_t split_offset;
} softpaq2_stream;

/* --- little-endian helpers ---------------------------------------------- */

static uint16_t softpaq2_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t softpaq2_le32(const uint8_t *bytes) {
    return (uint32_t)softpaq2_le16(bytes) |
           ((uint32_t)softpaq2_le16(bytes + 2U) << 16U);
}

static int64_t softpaq2_le32s(const uint8_t *bytes) {
    return (int64_t)(int32_t)softpaq2_le32(bytes);
}

static bool softpaq2_read_at(xx_io_device *device, int64_t offset,
                             void *buffer, size_t size) {
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

static bool softpaq2_range_within(int64_t total, int64_t offset,
                                  int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* --- member names -------------------------------------------------------
 * The 8-byte name and the 4-byte extension are NUL or space padded and the
 * extension carries its own leading '.'.  Both halves are trimmed the way the
 * reference extractor trims them and then concatenated verbatim.  Anything
 * that is not filesystem safe is escaped as %XX rather than folded to '_', so
 * two distinct members can never collapse onto one output file. */
static char *softpaq2_make_name(const uint8_t *entry, size_t index) {
    char *name;
    size_t output = 0U;
    unsigned part;
    /* Worst case every byte escapes to three characters. */
    name = (char *)xx_mem_alloc(
        (SOFTPAQ2_NAME_SIZE + SOFTPAQ2_EXT_SIZE) * 3U + 24U);
    if (!name) return NULL;
    for (part = 0U; part < 2U; ++part) {
        const uint8_t *field = (part == 0U) ? entry : entry + 0x09;
        size_t field_size = (part == 0U) ? SOFTPAQ2_NAME_SIZE
                                         : SOFTPAQ2_EXT_SIZE;
        size_t length = 0U, start = 0U, i;
        while (length < field_size && field[length] != 0U) ++length;
        while (start < length && field[start] == ' ') ++start;
        while (length > start && field[length - 1U] == ' ') --length;
        for (i = start; i < length; ++i) {
            uint8_t c = field[i];
            bool safe = c > 0x20U && c < 0x7fU && c != '%' && c != '/' &&
                        c != '\\' && c != ':' && c != '*' && c != '?' &&
                        c != '"' && c != '<' && c != '>' && c != '|';
            if (safe) {
                name[output++] = (char)c;
            } else {
                static const char digits[] = "0123456789ABCDEF";
                name[output++] = '%';
                name[output++] = digits[(c >> 4U) & 0x0fU];
                name[output++] = digits[c & 0x0fU];
            }
        }
    }
    if (output == 0U) {
        /* A nameless record still needs a distinct output file. */
        size_t value = index;
        char digits[24];
        size_t count = 0U;
        name[output++] = 'r';
        name[output++] = 'e';
        name[output++] = 'c';
        name[output++] = 'o';
        name[output++] = 'r';
        name[output++] = 'd';
        do {
            digits[count++] = (char)('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U && count < sizeof(digits));
        while (count != 0U) name[output++] = digits[--count];
    }
    name[output] = 0;
    return name;
}

static bool softpaq2_safe_output_name(const char *name) {
    const char *at;
    size_t length;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (length == 1U && name[0] == '.') return false;
    if (length == 2U && name[0] == '.' && name[1] == '.') return false;
    for (at = name; *at; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    }
    return true;
}

/* --- parsing ------------------------------------------------------------ */

static void softpaq2_stream_free(void *opaque) {
    softpaq2_stream *stream = (softpaq2_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool softpaq2_add_member(softpaq2_stream *stream,
                                const softpaq2_member *member) {
    softpaq2_member *grown;
    if (!stream || !member || stream->count >= SOFTPAQ2_MAX_ENTRIES ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (softpaq2_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* The locator has no fixed home, so the tag is searched for in overlapping
 * chunks and each hit is confirmed by its self-offset field before the
 * directory bounds it carries are trusted. */
static bool softpaq2_find_locator(xx_io_device *device, int64_t base,
                                  int64_t size, int64_t *locator_offset,
                                  int64_t *directory_offset,
                                  int64_t *split_offset, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t cursor = 0;
    bool found = false;
    if (size < SOFTPAQ2_LOCATOR_SIZE) return false;
    buffer = (uint8_t *)xx_mem_alloc(SOFTPAQ2_SCAN_CHUNK);
    if (!buffer) return false;
    while (!found && cursor + SOFTPAQ2_LOCATOR_SIZE <= size) {
        int64_t remaining = size - cursor;
        size_t chunk = remaining > (int64_t)SOFTPAQ2_SCAN_CHUNK
                           ? SOFTPAQ2_SCAN_CHUNK : (size_t)remaining;
        size_t limit, i;
        if (pd && xx_pd_is_stopped(pd)) break;
        if (!softpaq2_read_at(device, base + cursor, buffer, chunk)) break;
        limit = chunk < SOFTPAQ2_TAG_SIZE
                    ? 0U : chunk - SOFTPAQ2_TAG_SIZE + 1U;
        for (i = 0U; i < limit; ++i) {
            uint8_t locator[SOFTPAQ2_LOCATOR_SIZE];
            int64_t candidate = cursor + (int64_t)i;
            int64_t directory, split;
            if (buffer[i] != (uint8_t)softpaq2_tag[0] ||
                xx_rt_memcmp(buffer + i, softpaq2_tag, SOFTPAQ2_TAG_SIZE) != 0)
                continue;
            if (candidate + SOFTPAQ2_LOCATOR_SIZE > size) break;
            if (!softpaq2_read_at(device, base + candidate, locator,
                                  sizeof(locator)))
                continue;
            if (softpaq2_le32s(locator + 0x10) != candidate) continue;
            directory = softpaq2_le32s(locator + 0x14);
            split = softpaq2_le32s(locator + 0x18);
            if (directory <= 0 || split <= directory || split > size) continue;
            /* Both tables tile their range exactly. */
            if ((split - directory) % (int64_t)SOFTPAQ2_STORED_ENTRY_SIZE != 0)
                continue;
            if ((size - split) % (int64_t)SOFTPAQ2_PACKED_ENTRY_SIZE != 0)
                continue;
            *locator_offset = candidate;
            *directory_offset = directory;
            *split_offset = split;
            found = true;
            break;
        }
        if (found) break;
        if (chunk < SOFTPAQ2_TAG_SIZE) break;
        cursor += (int64_t)(chunk - SOFTPAQ2_TAG_SIZE + 1U);
    }
    xx_mem_free(buffer);
    return found;
}

static bool softpaq2_parse(Abstractformat *format, softpaq2_stream **result,
                           xx_pd_struct *pd) {
    uint8_t stub[SOFTPAQ2_STUB_PROBE];
    uint8_t *table = NULL;
    softpaq2_stream *stream = NULL;
    int64_t total, size, table_size;
    int64_t locator_offset = 0, directory_offset = 0, split_offset = 0;
    int64_t stored_count, packed_count, i;
    size_t index = 0U;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < SOFTPAQ2_MIN_SIZE) return false;
    /* Cheap gate first: the extractor stub is always a PKLITE compressed DOS
     * executable, so the banner sits at a fixed offset.  Only then is it worth
     * hunting for the directory locator. */
    if (!softpaq2_read_at(format->device, format->base_address, stub,
                          sizeof(stub)))
        return false;
    if (stub[0] != 'M' || stub[1] != 'Z') return false;
    if (xx_rt_memcmp(stub + SOFTPAQ2_PKLITE_OFFSET, softpaq2_banner,
                     SOFTPAQ2_BANNER_SIZE) != 0)
        return false;
    if (!softpaq2_find_locator(format->device, format->base_address, size,
                               &locator_offset, &directory_offset,
                               &split_offset, pd))
        return false;

    stored_count = (split_offset - directory_offset) /
                   (int64_t)SOFTPAQ2_STORED_ENTRY_SIZE;
    packed_count = (size - split_offset) /
                   (int64_t)SOFTPAQ2_PACKED_ENTRY_SIZE;
    if (stored_count + packed_count < 1) return false;
    if (stored_count > (int64_t)SOFTPAQ2_MAX_ENTRIES ||
        packed_count > (int64_t)SOFTPAQ2_MAX_ENTRIES)
        return false;

    /* The whole directory lies inside the file by construction, so this
     * allocation is bounded by the real file size. */
    table_size = size - directory_offset;
    if (table_size <= 0 || (uint64_t)table_size > (uint64_t)SIZE_MAX)
        return false;
    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!table) return false;
    if (!softpaq2_read_at(format->device,
                          format->base_address + directory_offset, table,
                          (size_t)table_size))
        goto fail;
    stream = (softpaq2_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;

    for (i = 0; i < stored_count; ++i) {
        const uint8_t *entry = table + i * (int64_t)SOFTPAQ2_STORED_ENTRY_SIZE;
        softpaq2_member member;
        int64_t member_size, offset;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (entry[0x08] != 0U || entry[0x0d] != 0U) goto fail;
        member_size = softpaq2_le32s(entry + 0x0e);
        offset = softpaq2_le32s(entry + 0x12);
        if (!softpaq2_range_within(size, offset, member_size)) goto fail;
        /* Entry 0 is the extractor stub itself; the publish flag is what
         * keeps it (and any other internal blob) out of the member list. */
        if (entry[0x16] == 0U) continue;
        xx_mem_zero(&member, sizeof(member));
        member.header_offset = format->base_address + directory_offset +
                               i * (int64_t)SOFTPAQ2_STORED_ENTRY_SIZE;
        member.header_size = (int64_t)SOFTPAQ2_STORED_ENTRY_SIZE;
        member.data_offset = format->base_address + offset;
        member.packed_size = member_size;
        member.unpacked_size = (uint64_t)member_size;
        member.method = SOFTPAQ2_METHOD_STORED;
        member.name = softpaq2_make_name(entry, index);
        if (!member.name) goto fail;
        if (!softpaq2_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        ++index;
    }

    for (i = 0; i < packed_count; ++i) {
        const uint8_t *entry = table +
                               stored_count *
                                   (int64_t)SOFTPAQ2_STORED_ENTRY_SIZE +
                               i * (int64_t)SOFTPAQ2_PACKED_ENTRY_SIZE;
        softpaq2_member member;
        uint16_t method;
        int64_t unpacked, packed, offset, stream_size;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (entry[0x08] != 0U || entry[0x0d] != 0U) goto fail;
        method = softpaq2_le16(entry + 0x0e);
        if (method != SOFTPAQ2_METHOD_STORED &&
            method != SOFTPAQ2_METHOD_IMPLODE)
            goto fail;
        unpacked = softpaq2_le32s(entry + 0x18);
        packed = softpaq2_le32s(entry + 0x1c);
        offset = softpaq2_le32s(entry + 0x22);
        if (unpacked < 0 || packed < 0 || offset < 0) goto fail;
        stream_size = (method == SOFTPAQ2_METHOD_STORED) ? unpacked : packed;
        if (!softpaq2_range_within(size, offset, stream_size)) goto fail;
        if ((uint64_t)unpacked > (uint64_t)SIZE_MAX) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.header_offset = format->base_address + directory_offset +
                               stored_count *
                                   (int64_t)SOFTPAQ2_STORED_ENTRY_SIZE +
                               i * (int64_t)SOFTPAQ2_PACKED_ENTRY_SIZE;
        member.header_size = (int64_t)SOFTPAQ2_PACKED_ENTRY_SIZE;
        member.data_offset = format->base_address + offset;
        member.packed_size = stream_size;
        member.unpacked_size = (uint64_t)unpacked;
        member.method = method;
        /* NOTE: this CRC-32 covers the PACKED stream, not the decompressed
         * member -- verified against the reference extractor on the whole
         * corpus.  It is checked on the stored bytes before decoding. */
        member.crc32 = softpaq2_le32(entry + 0x14);
        member.has_crc = true;
        member.dos_time = softpaq2_le16(entry + 0x10);
        member.dos_date = softpaq2_le16(entry + 0x12);
        member.attributes = softpaq2_le16(entry + 0x20);
        member.name = softpaq2_make_name(entry, index);
        if (!member.name) goto fail;
        if (!softpaq2_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        ++index;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = size;
    stream->locator_offset = format->base_address + locator_offset;
    stream->directory_offset = format->base_address + directory_offset;
    stream->split_offset = format->base_address + split_offset;
    xx_mem_free(table);
    *result = stream;
    return true;
fail:
    if (table) xx_mem_free(table);
    softpaq2_stream_free(stream);
    return false;
}

/* --- record plumbing ---------------------------------------------------- */

static bool softpaq2_copy_options(xx_list_s *destination,
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

static const xx_var *softpaq2_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool softpaq2_set_record(xx_archive_record *record,
                                const softpaq2_member *member) {
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
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_TIMESTAMP,
               ((uint64_t)member->dos_date << 16U) |
                   (uint64_t)member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static bool softpaq2_decode_member(Abstractformat *format,
                                   const softpaq2_member *member,
                                   uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->packed_size < 0 ||
        member->unpacked_size > (uint64_t)SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    if (member->method == SOFTPAQ2_METHOD_STORED &&
        (uint64_t)member->packed_size != member->unpacked_size)
        return false;
    packed = (uint8_t *)xx_mem_alloc(
        member->packed_size != 0 ? (size_t)member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output) goto fail;
    if (member->packed_size != 0 &&
        !softpaq2_read_at(format->device, member->data_offset, packed,
                          (size_t)member->packed_size))
        goto fail;
    /* The stored CRC covers the packed bytes, so it is the one integrity
     * check the container actually supports. */
    if (member->has_crc &&
        xx_crc32_calc(0U, packed, (size_t)member->packed_size) !=
            member->crc32)
        goto fail;
    if (member->method == SOFTPAQ2_METHOD_STORED) {
        if (output_size != 0U) xx_mem_copy(output, packed, output_size);
        written = output_size;
        decoded = true;
    } else if (member->method == SOFTPAQ2_METHOD_IMPLODE) {
        decoded = xx_dcl_decode_memory(packed, (size_t)member->packed_size,
                                       output, output_size, &written);
    }
    if (!decoded || written != output_size) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

/* --- lifecycle ---------------------------------------------------------- */

void xx_softpaq2_init(xx_softpaq2 *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SOFTPAQ2_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-softpaq");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_softpaq2_check_is_valid;
    archive->format.handle_base_info = xx_softpaq2_handle_base_info;
    archive->format.get_format_size = xx_softpaq2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_softpaq2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_softpaq2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_softpaq2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_softpaq2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_softpaq2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_softpaq2_free_archive_records_reading;
    archive->locator_offset = -1;
    archive->directory_offset = -1;
    archive->split_offset = -1;
}

xx_softpaq2 *xx_softpaq2_create(xx_io_device *device, int64_t base_address) {
    xx_softpaq2 *archive = (xx_softpaq2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_softpaq2_init(archive, device, base_address);
    return archive;
}

void xx_softpaq2_destroy(xx_softpaq2 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_softpaq2_free(xx_softpaq2 *archive) {
    if (!archive) return;
    xx_softpaq2_destroy(archive);
    xx_mem_free(archive);
}

bool xx_softpaq2_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    softpaq2_stream *stream;
    if (!softpaq2_parse(format, &stream, pd)) return false;
    softpaq2_stream_free(stream);
    return true;
}

bool xx_softpaq2_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    softpaq2_stream *stream;
    xx_softpaq2 *archive;
    if (!format || !softpaq2_parse(format, &stream, pd)) return false;
    archive = (xx_softpaq2 *)format;
    archive->number_of_records = stream->count;
    archive->locator_offset = stream->locator_offset;
    archive->directory_offset = stream->directory_offset;
    archive->split_offset = stream->split_offset;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    softpaq2_stream_free(stream);
    return true;
}

int64_t xx_softpaq2_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_softpaq2_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_softpaq2_get_number_of_archive_records(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_softpaq2_handle_base_info(format, pd))
               ? ((xx_softpaq2 *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_softpaq2_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    softpaq2_stream *stream;
    xx_archive_record_state *state;
    if (!softpaq2_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        softpaq2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = softpaq2_stream_free;
    state->total_records = stream->count;
    if (!softpaq2_copy_options(&state->options, options) ||
        !softpaq2_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_softpaq2_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_softpaq2_archive_record_move_to_next(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    softpaq2_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (softpaq2_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = softpaq2_set_record(&state->current_record,
                                            &stream->items[stream->index]);
    return state->has_record;
}

bool xx_softpaq2_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    softpaq2_stream *stream;
    softpaq2_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (softpaq2_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!softpaq2_safe_output_name(member->name) ||
        !softpaq2_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = softpaq2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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

void xx_softpaq2_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
