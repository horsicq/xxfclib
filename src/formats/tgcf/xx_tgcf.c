/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TGCF archives.
 *
 *   volume header, 0x1c bytes at the base address:
 *     0x00  "TGCF"
 *     0x04  unused
 *     0x06  u16 BE version: only 0x0130, 0x0140 and 0x0160 exist
 *     0x08  unused
 *     0x1a  u16 BE volume-name length, 1..0x400
 *     0x1c  volume name, that many bytes, code-page text
 *           u32 BE member-list offset   <- only when version >= 0x0160
 *           u32 BE header CRC
 *
 *   member record, repeated; the fixed part is 0x24 bytes:
 *     0x00  "TGCF"         the magic is repeated on every record
 *     0x0c  u16 LE split fragment kind; 2 = continuation of a split member
 *     0x0e  u16 LE method: 0 = stored, 4 = zlib
 *     0x10  u32 BE timestamp
 *     0x14  u32 BE compressed size
 *     0x18  u32 BE uncompressed size
 *     0x1c  u32 BE member CRC
 *     0x20  u32 BE attributes
 *     0x24  short name, NUL terminated
 *           long name, NUL terminated
 *           one flag byte
 *           u32 BE payload offset       <- only when version >= 0x0160
 *           u32 BE record CRC
 *
 * The two layouts differ in where the payload lives. Before 0x0160 a member's
 * bytes follow its record immediately and the walk steps over them; from
 * 0x0160 the records form a separate list, located by the header's list
 * offset, and each record points at its payload. Mixing the two conventions
 * is the mistake this format invites, so the version decides both, once.
 *
 * A record whose split field is 2 is the tail of a member that spans volumes:
 * it is walked over for its size but never published, because its bytes are
 * only part of a file.
 *
 * Names are absolute Windows paths with a drive letter ("F:\DOCS\A.TXT").
 * They are normalised to relative paths; the long name wins when present.
 *
 * Endianness is mixed on purpose: everything is big-endian except the two
 * u16 fields at 0x0c and 0x0e of a record, which are little-endian.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/tgcf/xx_tgcf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include <stdio.h>

#define XX_TGCF_COPY_CHUNK (64 * 1024)

typedef struct xx_tgcf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_tgcf_member;

typedef struct xx_tgcf_stream_s {
    xx_tgcf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_tgcf_stream;

static void xx_tgcf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_tgcf_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_tgcf_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_tgcf_path_safe(const char *name) {
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

static void xx_tgcf_stream_free(void *pointer) {
    xx_tgcf_stream *stream = (xx_tgcf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_tgcf_add(xx_tgcf_stream *stream,
                          const xx_tgcf_member *member) {
    xx_tgcf_member *grown = (xx_tgcf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_TGCF_MIN_SIZE 0x20
#define XX_TGCF_HEADER_SIZE 0x1c
#define XX_TGCF_RECORD_SIZE 0x24
#define XX_TGCF_TAIL_SIZE 0x0b
#define XX_TGCF_NAME_WINDOW 0x900
#define XX_TGCF_WINDOW_SIZE (XX_TGCF_RECORD_SIZE + XX_TGCF_NAME_WINDOW)
#define XX_TGCF_MAX_NAME_SIZE 1024
#define XX_TGCF_MAX_MEMBERS 200000
#define XX_TGCF_SPLIT_FRAGMENT 2
#define XX_TGCF_EXTENDED_FROM 0x0160
#define XX_TGCF_METHOD_STORE 0U
#define XX_TGCF_METHOD_ZLIB 4U
#define XX_TGCF_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_tgcf_be16(const uint8_t *data);
static uint32_t xx_tgcf_be32(const uint8_t *data);
static uint16_t xx_tgcf_le16(const uint8_t *data);
static char *xx_tgcf_make_name(const uint8_t *raw, size_t length);
static xx_tgcf_stream *xx_tgcf_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_tgcf_decode(Abstractformat *self, const xx_tgcf_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The shortest thing that could still be a record tail; the walk stops when
 * fewer bytes than this remain. */
/* Two names, the flag byte and the trailing offsets all live in one window
 * read after the fixed part of a record. */

static uint16_t xx_tgcf_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t xx_tgcf_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

/* Only the two u16s at 0x0c and 0x0e of a record are little-endian. */
static uint16_t xx_tgcf_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/* Stored names are absolute Windows paths ("F:\DOCS\A.TXT"), which cannot be
 * recreated as such: fold the separators, drop the drive and the leading
 * separators, and the member name becomes relative. */
static char *xx_tgcf_make_name(const uint8_t *raw, size_t length) {
    char *name = (char *)xx_mem_alloc(length + 1U);
    size_t start = 0U;
    size_t index;

    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        name[index] = (raw[index] == (uint8_t)'\\') ? '/' : (char)raw[index];
    }
    name[length] = '\0';
    if (length >= 2U && name[1] == ':') start = 2U;
    while (start < length && name[start] == '/') ++start;
    if (start != 0U) {
        for (index = 0U; (start + index) <= length; ++index) {
            name[index] = name[start + index];
        }
    }
    return name;
}

static xx_tgcf_stream *xx_tgcf_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic[4] = {'T', 'G', 'C', 'F'};
    xx_tgcf_stream *stream = NULL;
    uint8_t header[XX_TGCF_HEADER_SIZE];
    uint8_t pointer_field[4];
    uint8_t window[XX_TGCF_WINDOW_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t payload_end;
    int64_t volume_name_length;
    int64_t list_start = -1;
    uint32_t version;
    bool extended;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_TGCF_MIN_SIZE) return NULL;
    if (!xx_tgcf_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;

    /* Four ASCII bytes are cheap to hit by accident, so the magic alone
     * proves nothing. The version whitelist is the format's real defence
     * against a false positive: only three builds of the writer ever
     * existed, so an unrecognised version is noise, not a newer archive.
     * Do not relax this into a range or a minimum. */
    version = xx_tgcf_be16(header + 6);
    if (version != 0x0130U && version != 0x0140U && version != 0x0160U) {
        return NULL;
    }
    /* One version bit decides two different container layouts; see below. */
    extended = version >= (uint32_t)XX_TGCF_EXTENDED_FROM;

    volume_name_length = (int64_t)xx_tgcf_be16(header + 0x1a);
    if (volume_name_length == 0 || volume_name_length > 0x400) return NULL;
    if (!xx_tgcf_range_within(span, XX_TGCF_HEADER_SIZE,
                              volume_name_length)) {
        return NULL;
    }
    /* The volume name itself is not carried on a member, so it is bounded
     * and skipped rather than read. */
    offset = XX_TGCF_HEADER_SIZE + volume_name_length;

    if (extended) {
        if (!xx_tgcf_range_within(span, offset, 4) ||
            !xx_tgcf_read_at(self, self->base_address + offset, pointer_field,
                             sizeof(pointer_field))) {
            return NULL;
        }
        list_start = (int64_t)xx_tgcf_be32(pointer_field);
        offset += 4;
    }
    offset += 4; /* header CRC */
    if (extended) {
        /* From 0x0160 the records are a list elsewhere in the file; the
         * bytes right after the header are payload, not a record. */
        if (list_start < 0 || list_start > span) return NULL;
        offset = list_start;
    }
    if (offset > span) return NULL;

    stream = (xx_tgcf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    payload_end = offset;
    while ((span - offset) >= XX_TGCF_TAIL_SIZE) {
        xx_tgcf_member member;
        char *name;
        int64_t read_size;
        int64_t cursor;
        int64_t record_end;
        int64_t payload_offset = -1;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t name_start[2];
        int64_t name_length[2];
        int64_t chosen;
        uint32_t timestamp;
        uint16_t split;
        uint16_t method;
        int which;
        bool names_ok = true;

        name_start[0] = 0;
        name_start[1] = 0;
        name_length[0] = 0;
        name_length[1] = 0;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_TGCF_MAX_MEMBERS) break;
        if (!xx_tgcf_range_within(span, offset, XX_TGCF_RECORD_SIZE)) break;

        read_size = span - offset;
        if (read_size > XX_TGCF_WINDOW_SIZE) read_size = XX_TGCF_WINDOW_SIZE;
        if (!xx_tgcf_read_at(self, self->base_address + offset, window,
                             (size_t)read_size)) {
            goto fail;
        }
        /* Every record repeats the volume magic. That is what ends the walk:
         * the container has no member count, so the chain runs until the
         * next four bytes are not "TGCF". */
        if (xx_rt_memcmp(window, magic, sizeof(magic)) != 0) break;

        split = xx_tgcf_le16(window + 0x0c);
        method = xx_tgcf_le16(window + 0x0e);
        timestamp = xx_tgcf_be32(window + 0x10);
        compressed_size = (int64_t)xx_tgcf_be32(window + 0x14);
        uncompressed_size = (int64_t)xx_tgcf_be32(window + 0x18);
        /* 0x1c member CRC and 0x20 attributes are not carried by a member. */

        cursor = XX_TGCF_RECORD_SIZE;
        for (which = 0; which < 2; ++which) {
            int64_t start = cursor;

            while ((cursor < read_size) && (window[cursor] != 0U)) {
                /* 0x80..0xff is deliberately allowed: these are code-page
                 * encoded Windows paths and accented names are ordinary.
                 * Control bytes never appear in one, and a run of them is
                 * the signature of a window that is not a record at all. */
                if ((window[cursor] < 0x20U) || (window[cursor] == 0x7fU)) {
                    names_ok = false;
                    break;
                }
                ++cursor;
            }
            /* The terminator must fall inside the window: a name running to
             * the end of 0x924 bytes is misparsed data, not a path. */
            if (!names_ok || (cursor >= read_size) ||
                ((cursor - start) > XX_TGCF_MAX_NAME_SIZE)) {
                names_ok = false;
                break;
            }
            name_start[which] = start;
            name_length[which] = cursor - start;
            ++cursor; /* the NUL */
        }
        if (!names_ok) break;
        if (cursor >= read_size) break;
        ++cursor; /* the flag byte */

        if (extended) {
            if ((cursor + 4) > read_size) break;
            payload_offset = (int64_t)xx_tgcf_be32(window + cursor);
            cursor += 4;
        }
        cursor += 4; /* record CRC */
        record_end = offset + cursor;
        if (record_end > span) break;
        /* Before 0x0160 there is no payload pointer: the bytes begin where
         * the record ends. */
        if (!extended) payload_offset = record_end;
        /* A member whose extent leaves the file is a rejection, not a short
         * read to be patched up later. */
        if (!xx_tgcf_range_within(span, payload_offset, compressed_size)) {
            break;
        }

        if (split != XX_TGCF_SPLIT_FRAGMENT) {
            /* The long name is authoritative when the writer stored one. */
            chosen = (name_length[1] != 0) ? 1 : 0;
            name = xx_tgcf_make_name(window + name_start[chosen],
                                     (size_t)name_length[chosen]);
            if (!name) goto fail;
            /* A bare drive ("F:\") normalises away to nothing, and a name
             * that escapes the extraction directory is not a path this
             * writer produces. Either means the window was not a record. */
            if ((name[0] == '\0') || !xx_tgcf_path_safe(name)) {
                xx_str_free(name);
                goto fail;
            }

            xx_mem_zero(&member, sizeof(member));
            member.name = name;
            member.header_offset = self->base_address + offset;
            member.header_size = cursor;
            member.data_offset = self->base_address + payload_offset;
            member.compressed_size = compressed_size;
            member.uncompressed_size = uncompressed_size;
            /* The container's own method number, unchanged, so a listing
             * shows what the archive says; decode does the mapping. */
            member.method = method;
            member.timestamp = timestamp;
            if (!xx_tgcf_add(stream, &member)) {
                xx_str_free(name);
                goto fail;
            }
        }

        if ((payload_offset + compressed_size) > payload_end) {
            payload_end = payload_offset + compressed_size;
        }
        /* Extended volumes walk the record list; older ones step over the
         * payload that follows each record. Either way cursor is at least
         * 0x24, so the offset strictly advances and the walk terminates. */
        offset = extended ? record_end : (payload_offset + compressed_size);
        if (offset > payload_end) payload_end = offset;
    }

    /* A header alone is not an archive: something must have parsed as a
     * member, or this was four bytes that happened to spell TGCF. */
    if (stream->count == 0U) goto fail;
    if (offset > payload_end) payload_end = offset;
    stream->archive_size = (payload_end < span) ? payload_end : span;
    return stream;

fail:
    xx_tgcf_stream_free(stream);
    return NULL;
}


/* Both size fields are attacker-controlled u32s; refuse an absurd member
 * rather than attempt the allocation it asks for. */

static bool xx_tgcf_decode(Abstractformat *self, const xx_tgcf_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *packed;
    uint8_t *plain;
    size_t packed_size;
    size_t plain_size;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_TGCF_MAX_DECODED ||
        member->uncompressed_size > XX_TGCF_MAX_DECODED) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed_size = (size_t)member->compressed_size;
    plain_size = (size_t)member->uncompressed_size;

    packed = (uint8_t *)xx_mem_alloc(packed_size != 0U ? packed_size : 1U);
    if (!packed) return false;
    if (packed_size != 0U &&
        !xx_tgcf_read_at(self, member->data_offset, packed, packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    /* The compressed read is the large one, so re-check the cancel flag. */
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_TGCF_METHOD_STORE) {
        /* A stored member whose two size dwords disagree is not a stored
         * member: one of the two fields is being read as something it is
         * not, and handing back either length would be a guess. */
        if (plain_size != packed_size) {
            xx_mem_free(packed);
            return false;
        }
        *out = packed;
        *out_size = packed_size;
        return true;
    }
    if (member->method != XX_TGCF_METHOD_ZLIB) {
        /* The format defines method numbers this reader does not implement.
         * Falling back to "stored" would hand the caller compressed bytes
         * labelled as the file, which nothing downstream can detect. */
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(plain_size != 0U ? plain_size : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The container records the exact compressed length, so the Adler-32 may
     * or may not be inside it; the length check below is what authenticates
     * the result, not the trailer. */
    if (!xx_zlib_stream_header_is_valid(packed, packed_size) ||
        !xx_zlib_stream_decode_memory(packed, packed_size, plain, plain_size,
                                      &written) ||
        written != plain_size) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = plain_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_tgcf_init(xx_tgcf *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_TGCF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-tgcf");
    xx_format_set_extension(&archive->format, "tgcf");
    archive->format.check_is_valid = xx_tgcf_check_is_valid;
    archive->format.handle_base_info = xx_tgcf_handle_base_info;
    archive->format.get_format_size = xx_tgcf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_tgcf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_tgcf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_tgcf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_tgcf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_tgcf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_tgcf_free_archive_records_reading;
    archive->format.destroy = xx_tgcf_vtable_destroy;
}

xx_tgcf *xx_tgcf_create(xx_io_device *device, int64_t base_address) {
    xx_tgcf *archive = (xx_tgcf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_tgcf_init(archive, device, base_address);
    return archive;
}

void xx_tgcf_destroy(xx_tgcf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_tgcf_free(xx_tgcf *archive) {
    if (!archive) return;
    xx_tgcf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_tgcf_vtable_destroy(Abstractformat *self) {
    xx_tgcf_destroy((xx_tgcf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_tgcf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tgcf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_tgcf_parse(self, pd);
    if (!stream) return false;
    xx_tgcf_stream_free(stream);
    return true;
}

bool xx_tgcf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tgcf *archive = (xx_tgcf *)self;
    xx_tgcf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_tgcf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_tgcf_stream_free(stream);
    return true;
}

int64_t xx_tgcf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_tgcf_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_tgcf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_tgcf_set_record(xx_archive_record *record,
                                 const xx_tgcf_member *member) {
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

static bool xx_tgcf_copy_options(xx_list_s *target,
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

static const xx_var *xx_tgcf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_tgcf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tgcf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_tgcf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_tgcf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_tgcf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_tgcf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_tgcf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_tgcf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_tgcf_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_tgcf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tgcf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_tgcf_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_tgcf_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_tgcf_stream *stream;
    const xx_tgcf_member *member;
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
    stream = (xx_tgcf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_tgcf_path_safe(member->name)) return false;

    path_option = xx_tgcf_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_tgcf_decode(self, member, &plain, &plain_size, pd);
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
        !xx_tgcf_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_tgcf_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
