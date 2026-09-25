/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * EALIB archives.
 *
 * Header, from offset 0:
 *
 *   0x00  char[5]  "EALIB"
 *   0x05  u16 LE   number of members, at least 1
 *   0x07           the directory starts here
 *
 * The directory holds member count PLUS ONE entries of 18 bytes each. The
 * extra entry is a sentinel: it carries no member, it exists only so that
 * the last real member has a next-offset to be measured against.
 *
 *   +0x00  char[13] member name, NUL padded; byte 12 is always the NUL
 *   +0x0D  u8       method
 *   +0x0E  u32 LE   absolute file offset of the member data, read signed
 *
 * A member's size is the NEXT entry's offset minus its own -- no size is
 * stored anywhere. Methods:
 *
 *   0, 3  stored; the stream is the member, size and all
 *   1     Okumura LZSS (4 KiB ring, F = 18, threshold 3, ring cleared to
 *         0x00 rather than 0x20)
 *   4     PKWARE DCL "implode"
 *
 * For methods 1 and 4 the member opens with a little-endian u32 uncompressed
 * size and the codec stream begins behind it, so the payload is four bytes
 * shorter than the entry-to-entry distance. Method 2 exists in the method
 * space but no decoder does, and the reference extractor refuses it too;
 * refusing the whole archive is better than emitting garbage for one member.
 *
 * "EALIB" is only five bytes of ordinary ASCII. What actually decides the
 * format is that the payload area tiles exactly: the first entry's offset is
 * the byte just past the directory, and the sentinel's offset is EOF. That
 * pins both ends of the file against the directory and is what makes the
 * short magic safe to detect on.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ealib/xx_ealib.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/ea/xx_ea.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_EALIB_COPY_CHUNK (64 * 1024)

typedef struct xx_ealib_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ealib_member;

typedef struct xx_ealib_stream_s {
    xx_ealib_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ealib_stream;

static void xx_ealib_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ealib_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_ealib_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ealib_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_ealib_stream_free(void *pointer) {
    xx_ealib_stream *stream = (xx_ealib_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ealib_add(xx_ealib_stream *stream,
                          const xx_ealib_member *member) {
    xx_ealib_member *grown = (xx_ealib_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_EALIB_HEADER_SIZE 7
#define XX_EALIB_ENTRY_SIZE 18
#define XX_EALIB_NAME_SIZE 13
#define XX_EALIB_METHOD_OFFSET 13
#define XX_EALIB_OFFSET_OFFSET 14
#define XX_EALIB_MAX_MEMBERS 65535
#define XX_EALIB_NAME_BUFFER 48
#define XX_EALIB_MAX_DECODED (256 * 1024 * 1024)
#define XX_EALIB_METHOD_STORED 0U
#define XX_EALIB_METHOD_LZSS 1U
#define XX_EALIB_METHOD_STORED_ALT 3U
#define XX_EALIB_METHOD_DCL 4U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_ealib_le16(const uint8_t *data);
static uint32_t xx_ealib_le32(const uint8_t *data);
static bool xx_ealib_raw_name_valid(const uint8_t *entry);
static void xx_ealib_name_to_string(const uint8_t *entry, size_t index, char *out);
static xx_ealib_stream *xx_ealib_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_ealib_decode(Abstractformat *self, const xx_ealib_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The count field is 16 bit, so this is the producer's hard limit and not a
 * policy choice; the sentinel entry rides on top of it. */
/* Worst case each of the 13 name bytes escapes to three characters. */

static uint16_t xx_ealib_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_ealib_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The 13-byte name field is NUL padded and byte 12 is always the terminator.
 * Every member of the reference corpus is plain printable ASCII with CLEAN
 * padding -- nothing but NULs behind the first one. Unlike the sibling EA
 * format the packer does not leave a previous name's tail here, so demanding
 * clean padding costs nothing and is one of the structural rules that stops
 * random data from parsing as a directory. */
static bool xx_ealib_raw_name_valid(const uint8_t *entry) {
    bool padding = false;
    size_t index;

    if (entry[0] == 0U) return false;
    for (index = 0U; index < (size_t)XX_EALIB_NAME_SIZE; ++index) {
        uint8_t byte = entry[index];

        if (byte == 0U) {
            padding = true;
        } else if (padding) {
            return false;
        } else if (byte < 0x20U || byte > 0x7eU) {
            return false;
        }
    }
    return true;
}

/* Names are DOS 8.3 identifiers throughout the corpus, but the field is raw
 * bytes. Path separators and the Windows reserved punctuation are escaped as
 * %XX rather than folded to '_': escaping is reversible and, unlike folding,
 * cannot collapse two distinct members onto one output file. */
static void xx_ealib_name_to_string(const uint8_t *entry, size_t index,
                                    char *out) {
    static const char digits[] = "0123456789ABCDEF";
    size_t length = 0U;
    size_t at;
    size_t written = 0U;

    while (length < (size_t)XX_EALIB_NAME_SIZE && entry[length] != 0U) {
        ++length;
    }
    while (length > 0U && entry[length - 1U] == ' ') --length;

    for (at = 0U; at < length; ++at) {
        uint8_t byte = entry[at];
        bool safe = byte > 0x20U && byte < 0x7fU && byte != '%' &&
                    byte != '/' && byte != '\\' && byte != ':' &&
                    byte != '*' && byte != '?' && byte != '"' &&
                    byte != '<' && byte != '>' && byte != '|';

        if (safe) {
            out[written++] = (char)byte;
        } else {
            out[written++] = '%';
            out[written++] = digits[(byte >> 4) & 0x0fU];
            out[written++] = digits[byte & 0x0fU];
        }
    }
    out[written] = '\0';
    if (written == 0U) {
        xx_rt_snprintf(out, (size_t)XX_EALIB_NAME_BUFFER, "record%u",
                       (unsigned)index);
    }
}

static xx_ealib_stream *xx_ealib_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_ealib_stream *stream = NULL;
    uint8_t *directory = NULL;
    uint8_t header[XX_EALIB_HEADER_SIZE];
    uint8_t prefix[4];
    char name[XX_EALIB_NAME_BUFFER];
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t first_offset;
    int64_t sentinel_offset;
    int32_t member_count;
    int32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Header plus one member entry plus the sentinel entry. */
    if (span < XX_EALIB_HEADER_SIZE + 2 * XX_EALIB_ENTRY_SIZE) return NULL;

    if (!xx_ealib_read_at(self, self->base_address, header,
                          (size_t)XX_EALIB_HEADER_SIZE)) {
        return NULL;
    }
    if (xx_rt_memcmp(header, "EALIB", 5U) != 0) return NULL;

    member_count = (int32_t)xx_ealib_le16(header + 5);
    /* An archive with no members is not representable: the directory would
     * be the sentinel alone and the tiling rules below would be vacuous. */
    if (member_count < 1 || member_count > XX_EALIB_MAX_MEMBERS) return NULL;

    directory_offset = XX_EALIB_HEADER_SIZE;
    directory_size = (int64_t)(member_count + 1) * XX_EALIB_ENTRY_SIZE;
    if (!xx_ealib_range_within(span, directory_offset, directory_size)) {
        return NULL;
    }

    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_ealib_read_at(self, self->base_address + directory_offset,
                          directory, (size_t)directory_size)) {
        xx_mem_free(directory);
        return NULL;
    }

    /* Every entry, the sentinel included, must carry the name terminator and
     * a non-negative offset; the offset is a signed 32-bit quantity in the
     * reference detector. */
    for (index = 0; index <= member_count; ++index) {
        const uint8_t *entry = directory + (size_t)index * XX_EALIB_ENTRY_SIZE;

        if (pd && xx_pd_is_stopped(pd)) {
            xx_mem_free(directory);
            return NULL;
        }
        if (entry[XX_EALIB_NAME_SIZE - 1] != 0U) {
            xx_mem_free(directory);
            return NULL;
        }
        if ((int32_t)xx_ealib_le32(entry + XX_EALIB_OFFSET_OFFSET) < 0) {
            xx_mem_free(directory);
            return NULL;
        }
    }

    first_offset =
        (int64_t)(int32_t)xx_ealib_le32(directory + XX_EALIB_OFFSET_OFFSET);
    sentinel_offset = (int64_t)(int32_t)xx_ealib_le32(
        directory + (size_t)member_count * XX_EALIB_ENTRY_SIZE +
        XX_EALIB_OFFSET_OFFSET);
    /* Header, directory and payload tile the file exactly: the first entry
     * starts right behind the directory and the sentinel marks EOF. This is
     * the whole reason a five-byte ASCII magic is safe to detect on, so it
     * must not be relaxed into "every member lies somewhere inside the
     * file". */
    if (first_offset != directory_offset + directory_size) {
        xx_mem_free(directory);
        return NULL;
    }
    if (sentinel_offset != span) {
        xx_mem_free(directory);
        return NULL;
    }

    stream = (xx_ealib_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(directory);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0; index < member_count; ++index) {
        const uint8_t *entry = directory + (size_t)index * XX_EALIB_ENTRY_SIZE;
        xx_ealib_member member;
        uint32_t method;
        int64_t entry_offset;
        int64_t next_offset;
        int64_t size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry_offset =
            (int64_t)(int32_t)xx_ealib_le32(entry + XX_EALIB_OFFSET_OFFSET);
        next_offset = (int64_t)(int32_t)xx_ealib_le32(
            entry + XX_EALIB_ENTRY_SIZE + XX_EALIB_OFFSET_OFFSET);
        size = next_offset - entry_offset;
        /* Offsets must not run backwards: a negative size would otherwise
         * let two members claim overlapping payloads. */
        if (size < 0) goto fail;
        if (!xx_ealib_range_within(span, entry_offset, size)) goto fail;
        /* Only the real entries are name-checked. The sentinel carries no
         * member and the corpus leaves its name field zeroed, so requiring a
         * valid name there would reject every archive. */
        if (!xx_ealib_raw_name_valid(entry)) goto fail;

        method = (uint32_t)entry[XX_EALIB_METHOD_OFFSET];
        xx_mem_zero(&member, sizeof(member));
        if (method == XX_EALIB_METHOD_STORED ||
            method == XX_EALIB_METHOD_STORED_ALT) {
            member.data_offset = self->base_address + entry_offset;
            member.compressed_size = size;
            member.uncompressed_size = size;
        } else if (method == XX_EALIB_METHOD_LZSS ||
                   method == XX_EALIB_METHOD_DCL) {
            int64_t uncompressed;

            /* A compressed member opens with a little-endian u32
             * uncompressed size; the codec stream begins behind it. */
            if (size < 4) goto fail;
            if (!xx_ealib_read_at(self, self->base_address + entry_offset,
                                  prefix, 4U)) {
                goto fail;
            }
            uncompressed = (int64_t)(int32_t)xx_ealib_le32(prefix);
            if (uncompressed < 0) goto fail;
            member.data_offset = self->base_address + entry_offset + 4;
            member.compressed_size = size - 4;
            member.uncompressed_size = uncompressed;
        } else {
            /* Method 2 is in the method space but has no decoder anywhere,
             * and the reference extractor refuses it as well. Refusing the
             * archive beats listing a member whose bytes cannot be
             * produced. */
            goto fail;
        }
        xx_ealib_name_to_string(entry, (size_t)index, name);
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset =
            self->base_address + directory_offset +
            (int64_t)index * XX_EALIB_ENTRY_SIZE;
        member.header_size = XX_EALIB_ENTRY_SIZE;
        member.method = method;
        member.timestamp = 0U;
        member.is_folder = false;
        if (!xx_ealib_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    xx_mem_free(directory);
    directory = NULL;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    /* The sentinel already pinned the end of the payload to EOF, so the
     * archive is the whole span. */
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(directory);
    xx_ealib_stream_free(stream);
    return NULL;
}


/* The four-byte prefix of a compressed member is attacker-controlled, so it
 * is capped before it becomes an allocation. */

/* The container's own method numbers, unchanged: a listing shows what the
 * directory actually says, and the mapping to a codec lives only here. */

static bool xx_ealib_decode(Abstractformat *self,
                            const xx_ealib_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t plain_size;
    size_t written = 0U;
    bool stored;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    stored = (member->method == XX_EALIB_METHOD_STORED) ||
             (member->method == XX_EALIB_METHOD_STORED_ALT);
    /* Method 2, and anything else the directory might carry, has no decoder.
     * Falling through to the stored path would hand back a compressed
     * bitstream dressed as file data, which nothing downstream can tell from
     * the real thing. */
    if (!stored && member->method != XX_EALIB_METHOD_LZSS &&
        member->method != XX_EALIB_METHOD_DCL) {
        return false;
    }

    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > XX_EALIB_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;
    if (stored && member->compressed_size != member->uncompressed_size) {
        return false;
    }
    /* Both codecs emit at least one byte for at least one byte of input, so
     * an empty compressed member is malformed rather than an empty file. */
    if (!stored && (member->compressed_size == 0 ||
                    member->uncompressed_size == 0)) {
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    if (member->compressed_size > 0) {
        packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
        if (!packed) return false;
        if (!xx_ealib_read_at(self, member->data_offset, packed,
                              (size_t)member->compressed_size)) {
            xx_mem_free(packed);
            return false;
        }
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    /* xx_mem_alloc(0) returns NULL, which the caller cannot tell from a
     * failure, so an empty stored member still gets one byte. */
    plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : (size_t)1);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (stored) {
        size_t at;

        for (at = 0U; at < plain_size; ++at) plain[at] = packed[at];
        written = plain_size;
    } else if (member->method == XX_EALIB_METHOD_LZSS) {
        if (!xx_ea_lib_decode_memory(packed, (size_t)member->compressed_size,
                                     plain, plain_size, &written)) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
    } else if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size,
                                     plain, plain_size, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* Returning true with fewer bytes than the prefix promised is the one
     * failure the caller cannot detect. It matters most here because methods
     * 1 and 4 are told apart by a single directory byte, and the wrong codec
     * on the right bytes usually stops early rather than failing outright. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ealib_init(xx_ealib *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_EALIB;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ealib");
    xx_format_set_extension(&archive->format, "lib");
    archive->format.check_is_valid = xx_ealib_check_is_valid;
    archive->format.handle_base_info = xx_ealib_handle_base_info;
    archive->format.get_format_size = xx_ealib_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ealib_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ealib_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ealib_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ealib_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ealib_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ealib_free_archive_records_reading;
    archive->format.destroy = xx_ealib_vtable_destroy;
}

xx_ealib *xx_ealib_create(xx_io_device *device, int64_t base_address) {
    xx_ealib *archive = (xx_ealib *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ealib_init(archive, device, base_address);
    return archive;
}

void xx_ealib_destroy(xx_ealib *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ealib_free(xx_ealib *archive) {
    if (!archive) return;
    xx_ealib_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ealib_vtable_destroy(Abstractformat *self) {
    xx_ealib_destroy((xx_ealib *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ealib_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ealib_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ealib_parse(self, pd);
    if (!stream) return false;
    xx_ealib_stream_free(stream);
    return true;
}

bool xx_ealib_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ealib *archive = (xx_ealib *)self;
    xx_ealib_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ealib_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ealib_stream_free(stream);
    return true;
}

int64_t xx_ealib_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ealib_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ealib *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ealib_set_record(xx_archive_record *record,
                                 const xx_ealib_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_ealib_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_ealib_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_ealib_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ealib_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ealib_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ealib_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ealib_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ealib_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ealib_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ealib_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ealib_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ealib_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ealib_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ealib_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ealib_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ealib_stream *stream;
    const xx_ealib_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ealib_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ealib_path_safe(member->name)) return false;

    path_option = xx_ealib_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ealib_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_ealib_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_ealib_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
