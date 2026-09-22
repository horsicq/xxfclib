/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft Windows Imaging Format (WIM).
 *
 * Header (0x74..0xd0 bytes, little endian)
 *   +0x00  "MSWIM\0\0\0"
 *   +0x08  u32  header size
 *   +0x0c  u32  version (0x00010d00 for the shipping format)
 *   +0x10  u32  flags; bit 1 means the streams are compressed and
 *                bits 17..21 select XPRESS / LZX / LZMS
 *   +0x14  u32  chunk size
 *   +0x18  16   GUID
 *   +0x28  u16  part number   +0x2a  u16  total parts
 *   +0x2c  u32  image count
 *   +0x30  RESHDR offset table    +0x48  RESHDR XML data
 *   +0x60  RESHDR boot metadata   +0x7c  RESHDR integrity table
 *
 * A RESHDR is 24 bytes: a SEVEN byte size, a flag byte sharing its
 * quadword, then a u64 file offset and a u64 uncompressed size.  Flag 0x04
 * marks a compressed resource and 0x02 an image's metadata.
 *
 * The offset table is the stream directory: 50 bytes per entry, a RESHDR
 * followed by u16 part number, u32 reference count and a 20-byte SHA-1.  It
 * is stored uncompressed, which is what lets this reader enumerate an LZX
 * archive's members without implementing LZX.
 *
 * Layout taken from XArchive packages/xwim.cpp; U3 implements the same
 * format as archive/270 (class eua, VMT 0x00599838).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wim/xx_wim.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef WIM
#define XX_WIM_FILE_TYPE XX_FILE_TYPE_WIM
#else
#define XX_WIM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define WIM_MAX_MEMBERS 1048576U

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct wim_member_s {
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
} wim_member;

typedef struct wim_stream_s {
    wim_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} wim_stream;

static uint16_t wim_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t wim_le32(const uint8_t *b) {
    return (uint32_t)wim_le16(b) | ((uint32_t)wim_le16(b + 2U) << 16U);
}

static uint64_t wim_le64(const uint8_t *b) {
    return (uint64_t)wim_le32(b) | ((uint64_t)wim_le32(b + 4U) << 32U);
}

static uint32_t wim_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t wim_be64(const uint8_t *b) {
    return ((uint64_t)wim_be32(b) << 32U) | (uint64_t)wim_be32(b + 4U);
}

static bool wim_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool wim_write_all(xx_io_device *device, const void *data, size_t size,
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
static bool wim_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
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
        if (!wim_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool wim_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_mem_zero(buffer, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!wim_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *wim_make_name(const char *prefix, int64_t index,
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
static char *wim_clean_name(const uint8_t *bytes, size_t size) {
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

static bool wim_safe_output_name(const char *name) {
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

static void wim_stream_free(void *opaque) {
    wim_stream *stream = (wim_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool wim_add_member(wim_stream *stream, const wim_member *member) {
    wim_member *grown;
    if (!stream || !member || stream->count >= WIM_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (wim_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define WIM_HEADER_MIN 0x74
#define WIM_HEADER_MAX 0xd0
#define WIM_RESHDR_SIZE 24U
#define WIM_LOOKUP_ENTRY_SIZE 50U
#define WIM_MAX_TABLE_BYTES (64U * 1024U * 1024U)
#define WIM_RESHDR_FLAG_COMPRESSED 0x04U
#define WIM_RESHDR_FLAG_METADATA 0x02U

/* A WIM resource header: a 7-byte size, a flag byte, then two 64-bit words.
 * The size is deliberately 7 bytes so the flags can share the first quadword,
 * which is why it cannot simply be read as a u64. */
static bool wim_reshdr(const uint8_t *raw, int64_t limit, uint64_t *packed,
                       uint64_t *offset, uint64_t *unpacked, uint8_t *flags) {
    uint64_t size = 0U;
    unsigned at;
    for (at = 0U; at < 7U; ++at) size |= (uint64_t)raw[at] << (at * 8U);
    *flags = raw[7];
    *offset = wim_le64(raw + 8U);
    *unpacked = wim_le64(raw + 16U);
    *packed = size;
    if (*offset > (uint64_t)limit) return false;
    if (size > (uint64_t)limit - *offset) return false;
    if (*unpacked > (uint64_t)INT64_MAX) return false;
    return true;
}

static bool wim_parse(Abstractformat *format, wim_stream **result) {
    uint8_t header[WIM_HEADER_MAX];
    uint8_t *lookup = NULL;
    wim_stream *stream = NULL;
    wim_member member;
    int64_t total, size;
    uint32_t header_size, version, image_count;
    uint64_t table_packed, table_offset, table_unpacked;
    uint64_t xml_packed, xml_offset, xml_unpacked;
    uint8_t table_flags, xml_flags;
    uint64_t entries, at;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < WIM_HEADER_MIN) return false;
    xx_mem_zero(header, sizeof(header));
    if (!wim_read_at(format->device, format->base_address, header,
                     size < WIM_HEADER_MAX ? (size_t)size : WIM_HEADER_MAX) ||
        xx_rt_memcmp(header, "MSWIM\0\0\0", 8U) != 0)
        return false;

    header_size = wim_le32(header + 8U);
    version = wim_le32(header + 12U);
    if (header_size < WIM_HEADER_MIN || header_size > WIM_HEADER_MAX ||
        (int64_t)header_size > size)
        return false;
    image_count = wim_le32(header + 0x2cU);

    if (!wim_reshdr(header + 0x30U, size, &table_packed, &table_offset,
                    &table_unpacked, &table_flags) ||
        !wim_reshdr(header + 0x48U, size, &xml_packed, &xml_offset,
                    &xml_unpacked, &xml_flags))
        return false;

    stream = (wim_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->aux0 = wim_le32(header + 16U); /* header flags */
    stream->aux1 = wim_le32(header + 20U); /* chunk size */
    stream->aux2 = image_count;

    /* The XML resource names the images and is never compressed, so it is
     * always publishable even when everything else in the file is LZX. */
    if (xml_packed != 0U) {
        xx_mem_zero(&member, sizeof(member));
        member.name = wim_make_name("wim", -1, ".xml");
        if (!member.name) goto fail;
        member.header_offset = format->base_address + 0x48;
        member.header_size = (int64_t)WIM_RESHDR_SIZE;
        member.data_offset = format->base_address + (int64_t)xml_offset;
        member.packed_size = (int64_t)xml_packed;
        member.unpacked_size = xml_unpacked;
        member.flags = xml_flags;
        member.method = (xml_flags & WIM_RESHDR_FLAG_COMPRESSED) ? 1U : 0U;
        if (!wim_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto fail;
        }
    }

    /* The offset table is the stream directory.  It is stored uncompressed in
     * every WIM this reader has seen, but a compressed one is possible and is
     * simply not enumerated rather than guessed at. */
    entries = 0U;
    if ((table_flags & WIM_RESHDR_FLAG_COMPRESSED) == 0U &&
        table_packed == table_unpacked && table_packed != 0U &&
        table_packed <= WIM_MAX_TABLE_BYTES &&
        (table_packed % WIM_LOOKUP_ENTRY_SIZE) == 0U) {
        entries = table_packed / WIM_LOOKUP_ENTRY_SIZE;
        lookup = (uint8_t *)xx_mem_alloc((size_t)table_packed);
        if (!lookup ||
            !wim_read_at(format->device,
                         format->base_address + (int64_t)table_offset, lookup,
                         (size_t)table_packed))
            goto fail;
    }

    for (at = 0U; at < entries; ++at) {
        const uint8_t *raw = lookup + at * WIM_LOOKUP_ENTRY_SIZE;
        uint64_t packed, offset, unpacked;
        uint8_t flags;
        if (!wim_reshdr(raw, size, &packed, &offset, &unpacked, &flags))
            goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = wim_make_name(
            (flags & WIM_RESHDR_FLAG_METADATA) ? "metadata_" : "stream_",
            (int64_t)at, ".bin");
        if (!member.name) goto fail;
        member.header_offset =
            format->base_address + (int64_t)table_offset +
            (int64_t)(at * WIM_LOOKUP_ENTRY_SIZE);
        member.header_size = (int64_t)WIM_LOOKUP_ENTRY_SIZE;
        member.data_offset = format->base_address + (int64_t)offset;
        member.packed_size = (int64_t)packed;
        member.unpacked_size = unpacked;
        member.flags = flags;
        member.method = (flags & WIM_RESHDR_FLAG_COMPRESSED) ? 1U : 0U;
        member.attributes = wim_le32(raw + 0x1aU); /* reference count */
        if (!wim_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto fail;
        }
    }

    if (stream->count == 0U) goto fail;
    if (lookup) xx_mem_free(lookup);
    stream->archive_size = size;
    (void)version;
    *result = stream;
    return true;
fail:
    if (lookup) xx_mem_free(lookup);
    wim_stream_free(stream);
    return false;
}

static bool wim_write_member(Abstractformat *format, wim_stream *stream,
                             const wim_member *member,
                             xx_io_device *destination, xx_pd_struct *pd) {
    (void)stream;
    if (!format || !member) return false;
    /* A compressed resource is a chunk table plus LZX or XPRESS chunks; that
     * codec is not implemented here, so those members fail closed rather than
     * emit the packed bytes as if they were the file. */
    if (member->method != 0U) return false;
    if (member->unpacked_size != (uint64_t)member->packed_size) return false;
    return wim_copy_range(format->device, member->data_offset,
                          member->unpacked_size, destination, pd);
}

static bool wim_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *wim_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool wim_set_record(xx_archive_record *record,
                           const wim_member *member) {
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

void xx_wim_init(xx_wim *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_WIM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ms-wim");
    xx_format_set_extension(&archive->format, "wim");
    archive->format.check_is_valid = xx_wim_check_is_valid;
    archive->format.handle_base_info = xx_wim_handle_base_info;
    archive->format.get_format_size = xx_wim_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_wim_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_wim_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_wim_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_wim_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_wim_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_wim_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_wim *xx_wim_create(xx_io_device *device, int64_t base_address) {
    xx_wim *archive = (xx_wim *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_wim_init(archive, device, base_address);
    return archive;
}

void xx_wim_destroy(xx_wim *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_wim_free(xx_wim *archive) {
    if (!archive) return;
    xx_wim_destroy(archive);
    xx_mem_free(archive);
}

bool xx_wim_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    wim_stream *stream;
    (void)pd;
    if (!wim_parse(format, &stream)) return false;
    wim_stream_free(stream);
    return true;
}

bool xx_wim_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    wim_stream *stream;
    xx_wim *archive;
    (void)pd;
    if (!format || !wim_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_wim *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_WIM_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    wim_stream_free(stream);
    return true;
}

int64_t xx_wim_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_wim_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_wim_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_wim_handle_base_info(format, pd))
               ? ((xx_wim *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_wim_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    wim_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!wim_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        wim_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = wim_stream_free;
    state->total_records = stream->count;
    if (!wim_copy_options(&state->options, options) ||
        !wim_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_wim_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_wim_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    wim_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (wim_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        wim_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_wim_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    wim_stream *stream;
    wim_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (wim_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!wim_safe_output_name(member->name)) return false;
    path_option = wim_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return wim_write_member(format, stream, member, NULL, pd);
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
    if (!destination) goto done;
    result = wim_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_wim_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
