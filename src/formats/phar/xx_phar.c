/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PHP archive (PHAR).
 *
 * The file opens with an executable PHP stub of arbitrary length; the
 * container starts after the stub's "__HALT_COMPILER();" call, skipping the
 * optional blanks, the optional "?>" and the optional line break that PHP
 * itself skips.
 *
 * Manifest (little endian)
 *   u32  manifest length, counted from the next byte
 *   u32  file count
 *   u16  API version; must not exceed 0x1100
 *   u32  global flags; 0x00010000 means the archive is signed
 *   u32  alias length, then the alias
 *   u32  metadata length, then the serialized metadata
 * then per entry
 *   u32  name length, then the name
 *   u32  uncompressed size   u32  timestamp
 *   u32  compressed size     u32  CRC32
 *   u32  flags; bits 0..8 permissions, 0x1000 zlib, 0x2000 bzip2
 *   u32  entry metadata length, then the metadata
 *
 * The bodies follow the manifest back to back in declaration order, so an
 * entry's offset is the running sum of the preceding compressed sizes.  A
 * zlib member is RAW Deflate, not a zlib stream, and every decoded member is
 * checked against its stored CRC32 before it is written.
 *
 * U3 implements the same format as archive/556 (class cgb, VMT 0x00657508);
 * its predicate FUN_006575f0 gates on the PHP stub's opening line before
 * parsing the manifest, which is what this reader's prefilter mirrors.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/phar/xx_phar.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/crc/xx_crc.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef PHAR
#define XX_PHAR_FILE_TYPE XX_FILE_TYPE_PHAR
#else
#define XX_PHAR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PHAR_MAX_MEMBERS 1000000U

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct phar_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint64_t timestamp;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
    uint32_t method;
    uint32_t crc32;
    uint32_t attributes;
    uint32_t flags;
    bool has_crc;
    bool encrypted;
    bool folder;
} phar_member;

typedef struct phar_stream_s {
    phar_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} phar_stream;

static uint16_t phar_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t phar_le32(const uint8_t *b) {
    return (uint32_t)phar_le16(b) | ((uint32_t)phar_le16(b + 2U) << 16U);
}

static uint64_t phar_le64(const uint8_t *b) {
    return (uint64_t)phar_le32(b) | ((uint64_t)phar_le32(b + 4U) << 32U);
}

static uint32_t phar_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t phar_be64(const uint8_t *b) {
    return ((uint64_t)phar_be32(b) << 32U) | (uint64_t)phar_be32(b + 4U);
}

static bool phar_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool phar_write_all(xx_io_device *device, const void *data, size_t size,
                          xx_pd_struct *pd) {
    size_t done = 0U;
    if (!data && size != 0U) return false;
    if (!device) return true; /* verify-only pass: nothing is materialized */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Copy a run of source bytes straight through to the destination. */
static bool phar_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!source || offset < 0) return false;
    if (!destination) return true;
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer + done, want - done);
            if (amount <= 0 || (size_t)amount > want - done) return false;
            done += (size_t)amount;
        }
        if (!phar_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool phar_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_mem_zero(buffer, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!phar_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *phar_make_name(const char *prefix, int64_t index,
                           const char *suffix) {
    char buffer[96];
    size_t used = 0U;
    size_t at;
    char *result;
    for (at = 0U; prefix && prefix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[at];
    }
    if (index >= 0) {
        char digits[24];
        size_t count = 0U;
        int64_t value = index;
        do {
            digits[count++] = (char)('0' + (int)(value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 4U && count < sizeof(digits)) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    for (at = 0U; suffix && suffix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[at];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

/* Names that DO come from the container are normalized here: separators are
 * unified, traversal components are removed and anything a filesystem would
 * choke on becomes '_'. */
static char *phar_clean_name(const uint8_t *bytes, size_t size) {
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

static bool phar_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
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

static void phar_stream_free(void *opaque) {
    phar_stream *stream = (phar_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool phar_add_member(phar_stream *stream, const phar_member *member) {
    phar_member *grown;
    if (!stream || !member || stream->count >= PHAR_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (phar_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define PHAR_STUB_LIMIT (8U * 1024U * 1024U)
#define PHAR_SCAN_CHUNK (0x10000U)
#define PHAR_MANIFEST_MIN 18U
#define PHAR_MAX_MANIFEST (64U * 1024U * 1024U)
#define PHAR_MAX_NAME 4096U
#define PHAR_MAX_MEMBER (256U * 1024U * 1024U)
#define PHAR_COMP_MASK 0x0000f000U
#define PHAR_COMP_GZ 0x00001000U
#define PHAR_COMP_BZ2 0x00002000U

static const char PHAR_TOKEN[] = "__HALT_COMPILER();";
#define PHAR_TOKEN_SIZE 18U

/* Find the stub terminator.  PHP writes "__HALT_COMPILER(); ?>\r\n" but only
 * the call itself is mandatory, so the optional blanks, the optional "?>" and
 * the optional line break are each skipped independently. */
static bool phar_find_manifest(xx_io_device *device, int64_t base, int64_t size,
                               int64_t *manifest_offset) {
    uint8_t buffer[PHAR_SCAN_CHUNK + PHAR_TOKEN_SIZE];
    int64_t limit = size < (int64_t)PHAR_STUB_LIMIT ? size
                                                    : (int64_t)PHAR_STUB_LIMIT;
    int64_t window = 0;
    while (window < limit) {
        size_t want = (size_t)(limit - window);
        size_t at;
        if (want > PHAR_SCAN_CHUNK + PHAR_TOKEN_SIZE - 1U)
            want = PHAR_SCAN_CHUNK + PHAR_TOKEN_SIZE - 1U;
        if (want < PHAR_TOKEN_SIZE) break;
        if (!phar_read_at(device, base + window, buffer, want)) return false;
        for (at = 0U; at + PHAR_TOKEN_SIZE <= want; ++at) {
            int64_t after;
            uint8_t tail[8];
            size_t left;
            if (xx_rt_memcmp(buffer + at, PHAR_TOKEN, PHAR_TOKEN_SIZE) != 0)
                continue;
            after = window + (int64_t)at + (int64_t)PHAR_TOKEN_SIZE;
            left = (size_t)(size - after);
            if (left > sizeof(tail)) left = sizeof(tail);
            xx_mem_zero(tail, sizeof(tail));
            if (left != 0U && !phar_read_at(device, base + after, tail, left))
                return false;
            {
                size_t skip = 0U;
                while (skip < left && (tail[skip] == ' ' || tail[skip] == '\t'))
                    ++skip;
                if (skip + 2U <= left && tail[skip] == '?' &&
                    tail[skip + 1U] == '>')
                    skip += 2U;
                if (skip + 2U <= left && tail[skip] == '\r' &&
                    tail[skip + 1U] == '\n')
                    skip += 2U;
                else if (skip < left && tail[skip] == '\n')
                    skip += 1U;
                *manifest_offset = after + (int64_t)skip;
            }
            return true;
        }
        window += (int64_t)(want - (PHAR_TOKEN_SIZE - 1U));
    }
    return false;
}

/* The manifest: a length, a file count, an API version, global flags, an
 * alias, archive metadata and then one fixed 24-byte record plus a name and
 * per-entry metadata for every member.  The bodies follow the manifest in
 * exactly the order the entries were declared. */
static bool phar_parse(Abstractformat *format, phar_stream **result) {
    uint8_t head[18];
    uint8_t *manifest = NULL;
    phar_stream *stream = NULL;
    int64_t total, size, manifest_offset, data_cursor;
    uint32_t manifest_size, count, flags, alias_size, meta_size;
    uint16_t api;
    uint64_t cursor;
    uint32_t index;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(PHAR_TOKEN_SIZE + PHAR_MANIFEST_MIN)) return false;
    if (!phar_find_manifest(format->device, format->base_address, size,
                            &manifest_offset))
        return false;
    if (manifest_offset < 0 ||
        size - manifest_offset < (int64_t)PHAR_MANIFEST_MIN)
        return false;
    if (!phar_read_at(format->device, format->base_address + manifest_offset,
                      head, sizeof(head)))
        return false;

    manifest_size = phar_le32(head);
    count = phar_le32(head + 4U);
    api = phar_le16(head + 8U);
    flags = phar_le32(head + 10U);
    alias_size = phar_le32(head + 14U);
    if (manifest_size < PHAR_MANIFEST_MIN - 4U ||
        manifest_size > PHAR_MAX_MANIFEST ||
        (int64_t)manifest_size > size - manifest_offset - 4)
        return false;
    /* PHP's own test: the manifest API version must not exceed the one the
     * reader knows (1.1.0 == 0x1100). */
    if (api > 0x1100U) return false;
    if (count == 0U || count > 1000000U) return false;
    if ((uint64_t)alias_size > manifest_size) return false;

    manifest = (uint8_t *)xx_mem_alloc(manifest_size);
    if (!manifest ||
        !phar_read_at(format->device,
                      format->base_address + manifest_offset + 4,
                      manifest, manifest_size))
        goto fail;

    /* `manifest` starts AFTER the length word, so the fixed part is
     * count(4) + api(2) + flags(4) + alias length(4) == 14 bytes. */
    cursor = 14U;
    if ((uint64_t)alias_size > manifest_size - cursor) goto fail;
    cursor += alias_size;
    if (cursor + 4U > manifest_size) goto fail;
    meta_size = phar_le32(manifest + cursor);
    cursor += 4U;
    if (meta_size > manifest_size - cursor) goto fail;
    cursor += meta_size;

    stream = (phar_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->aux0 = api;
    stream->aux1 = flags;

    data_cursor = manifest_offset + 4 + (int64_t)manifest_size;
    for (index = 0U; index < count; ++index) {
        phar_member member;
        uint32_t name_size, entry_meta;
        const uint8_t *fixed;
        if (cursor + 4U > manifest_size) goto fail;
        name_size = phar_le32(manifest + cursor);
        cursor += 4U;
        if (name_size == 0U || name_size > PHAR_MAX_NAME ||
            name_size > manifest_size - cursor)
            goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = phar_clean_name(manifest + cursor, name_size);
        if (!member.name) goto fail;
        cursor += name_size;
        if (cursor + 24U > manifest_size) {
            xx_mem_free(member.name);
            goto fail;
        }
        fixed = manifest + cursor;
        member.unpacked_size = phar_le32(fixed);
        member.timestamp = phar_le32(fixed + 4U);
        member.packed_size = (int64_t)phar_le32(fixed + 8U);
        member.crc32 = phar_le32(fixed + 12U);
        member.has_crc = true;
        member.flags = phar_le32(fixed + 16U);
        entry_meta = phar_le32(fixed + 20U);
        cursor += 24U;
        if (entry_meta > manifest_size - cursor) {
            xx_mem_free(member.name);
            goto fail;
        }
        cursor += entry_meta;
        member.method = member.flags & PHAR_COMP_MASK;
        member.attributes = member.flags & 0x1ffU;
        member.header_offset = format->base_address + manifest_offset;
        member.header_size = 0;
        member.data_offset = format->base_address + data_cursor;
        if (member.unpacked_size > PHAR_MAX_MEMBER ||
            member.packed_size < 0 ||
            member.packed_size > size - data_cursor) {
            xx_mem_free(member.name);
            goto fail;
        }
        data_cursor += member.packed_size;
        if (!phar_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto fail;
        }
    }

    xx_mem_free(manifest);
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    if (manifest) xx_mem_free(manifest);
    phar_stream_free(stream);
    return false;
}

static bool phar_write_member(Abstractformat *format, phar_stream *stream,
                              const phar_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t output_size;
    bool decoded = false;
    bool result = false;

    (void)stream;
    if (!format || !member || member->packed_size < 0) return false;
    output_size = (size_t)member->unpacked_size;
    if (member->method == 0U) {
        /* Stored: the packed extent IS the file, so it is streamed rather
         * than materialized. */
        if ((uint64_t)member->packed_size != member->unpacked_size)
            return false;
        return phar_copy_range(format->device, member->data_offset,
                               member->unpacked_size, destination, pd);
    }
    if (member->method != PHAR_COMP_GZ && member->method != PHAR_COMP_BZ2)
        return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size
                                         : 1U);
    plain = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !plain ||
        (member->packed_size != 0 &&
         !phar_read_at(format->device, member->data_offset, packed,
                       (size_t)member->packed_size)))
        goto done;
    if (member->method == PHAR_COMP_GZ)
        decoded = xx_deflate_decompress_memory(
            packed, (size_t)member->packed_size, plain, output_size, &written,
            false);
    else
        decoded = xx_bzip2_decompress_memory(packed,
                                             (size_t)member->packed_size,
                                             plain, output_size, &written);
    if (!decoded || written != output_size) goto done;
    if (xx_crc32_calc(0U, plain, written) != member->crc32) goto done;
    result = phar_write_all(destination, plain, written, pd);
done:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

static bool phar_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *phar_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool phar_set_record(xx_archive_record *record,
                           const phar_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

void xx_phar_init(xx_phar *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PHAR_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-php-archive");
    xx_format_set_extension(&archive->format, "phar");
    archive->format.check_is_valid = xx_phar_check_is_valid;
    archive->format.handle_base_info = xx_phar_handle_base_info;
    archive->format.get_format_size = xx_phar_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_phar_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_phar_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_phar_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_phar_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_phar_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_phar_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_phar *xx_phar_create(xx_io_device *device, int64_t base_address) {
    xx_phar *archive = (xx_phar *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_phar_init(archive, device, base_address);
    return archive;
}

void xx_phar_destroy(xx_phar *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_phar_free(xx_phar *archive) {
    if (!archive) return;
    xx_phar_destroy(archive);
    xx_mem_free(archive);
}

bool xx_phar_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    phar_stream *stream;
    (void)pd;
    if (!phar_parse(format, &stream)) return false;
    phar_stream_free(stream);
    return true;
}

bool xx_phar_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    phar_stream *stream;
    xx_phar *archive;
    (void)pd;
    if (!format || !phar_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_phar *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_PHAR_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    phar_stream_free(stream);
    return true;
}

int64_t xx_phar_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_phar_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_phar_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_phar_handle_base_info(format, pd))
               ? ((xx_phar *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_phar_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    phar_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!phar_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        phar_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = phar_stream_free;
    state->total_records = stream->count;
    if (!phar_copy_options(&state->options, options) ||
        !phar_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_phar_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_phar_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    phar_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (phar_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        phar_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_phar_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    phar_stream *stream;
    phar_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (phar_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!phar_safe_output_name(member->name)) return false;
    path_option = phar_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return phar_write_member(format, stream, member, NULL, pd);
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
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    result = phar_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_phar_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
