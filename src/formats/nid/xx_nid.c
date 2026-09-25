/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "NI" install-set volume (DISK001.NID, RAID001.DAT, Z.PAC).  A DOS-era
 * installer data volume; the vendor is not identifiable from the bytes.
 *
 *   header, 0x78 bytes at the base address:
 *     0x00  2 bytes  "NI"
 *     0x02  2 bytes  0x15 0x01 (version)
 *     0x04  u16 LE   checksum, not verified by the reference extractor
 *     0x06  u16 LE   number of entries, never 0
 *     0x08  0x70     reserved, zero in every sample
 *
 *   directory, that many 29-byte entries at 0x78:
 *     0x00  u8       method; always 1.  The listing stops at the first entry
 *                    that is not 1: the continuation entries of a following
 *                    volume carry 2.
 *     0x01  i32 LE   data offset, ONE BASED - subtract 1
 *     0x05  u32 LE   folder key, opaque; members sharing a key share an output
 *                    folder, numbered "Folder1", "Folder2", ... in the order
 *                    the keys are first seen
 *     0x09  11 bytes FCB style 8 + 3 name, space padded
 *     0x14  u8       DOS attribute byte
 *     0x15  u16 LE   DOS time
 *     0x17  u16 LE   DOS date
 *     0x19  i32 LE   uncompressed size
 *
 * A member is a chain of five-byte-framed blocks; see algo/nid.  The extent
 * runs from the data offset to the end of the block carrying the last-block
 * flag.  A chain that leaves the file is CLAMPED, not rejected: a set split
 * across volumes has exactly that shape, and the reference lists such members
 * and only fails when it tries to expand them.
 *
 * REFERENCE DEFECT: XNID::parseContext reads the DOS time at entry+18 and the
 * date at entry+20, which land inside the 11-byte name field (entry+9 ..
 * entry+19); the size it reads at entry+25 fixes the rest of the layout, so
 * the documented 0x15 / 0x17 used here are the right ones and the reference
 * publishes two bytes of the file name as a timestamp.  See the port report.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/nid/xx_nid.h"

#include "xxfclib/algo/nid/xx_nid.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Pending registration in xxfc_defs.h.  Once the enumerator XX_FILE_TYPE_NID
 * and its short alias NID are added there this fallback switches itself
 * off. */
#ifndef NID
#define XX_FILE_TYPE_NID XX_FILE_TYPE_UNKNOWN
#endif

#define XX_NID_HEADER_SIZE 0x78
#define XX_NID_ENTRY_SIZE 29
#define XX_NID_NAME_SIZE 11
#define XX_NID_BLOCK_FRAME_SIZE 5
/* The count field is a u16, so 65535 members is the hard producer limit. */
#define XX_NID_MAX_ENTRIES 65535
/* Only two of the sixteen frame flag bits are read. */
#define XX_NID_BLOCK_COMPRESSED 0x0800U
#define XX_NID_BLOCK_LAST 0x0100U
/* A member never runs to a million blocks; the bound stops a corrupt chain
 * from spinning. */
#define XX_NID_MAX_BLOCKS 1000000
/* Both the compressed extent and the plaintext are materialised in memory
 * (the codec has no streaming entry point), so these bound what a corrupt
 * directory can make the reader allocate. */
#define XX_NID_MAX_COMPRESSED ((int64_t)256 * 1024 * 1024)
#define XX_NID_MAX_DECODED ((int64_t)256 * 1024 * 1024)

typedef struct xx_nid_member_s {
    char *name;
    int64_t record_offset;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t folder_key;
    uint16_t dos_time;
    uint16_t dos_date;
    uint8_t attributes;
    /* One stored block: the payload behind the frame IS the member, so no
     * codec is needed. */
    bool single_stored_block;
} xx_nid_member;

typedef struct xx_nid_stream_s {
    xx_nid_member *items;
    size_t count;
    size_t index;
    uint32_t *folder_keys;
    size_t folder_count;
    uint32_t number_of_entries;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t archive_size;
} xx_nid_stream;

static void xx_nid_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_nid_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_nid_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_nid_read_at(Abstractformat *self, int64_t offset,
                           uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool xx_nid_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_nid_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 1U && cursor[0] == '.') return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* Every name in the reference corpus is a plain DOS 8.3 identifier.  Requiring
 * that of the FIRST entry is one of the structural rules that stops random
 * data from parsing as a directory. */
static bool xx_nid_valid_fcb_name(const uint8_t *raw) {
    bool any = false;
    size_t index;

    for (index = 0U; index < XX_NID_NAME_SIZE; ++index) {
        uint8_t character = raw[index];
        if (character <= 0x20U) continue; /* the reference drops these */
        if (character > 0x7eU) return false;
        any = true;
    }
    return any;
}

/* Render the 11-byte FCB field exactly the way the reference does: bytes above
 * 0x20 of the 8-byte stem, a dot, bytes above 0x20 of the 3-byte extension,
 * and a trailing dot dropped.  Bytes that are legal in the field but not in a
 * file name are escaped as %XX - escaping is reversible and cannot collapse
 * two distinct members onto one output file. */
static bool xx_nid_fcb_name(const uint8_t *raw, size_t member_index,
                            char *buffer, size_t buffer_size) {
    static const char digits[] = "0123456789ABCDEF";
    size_t used = 0U;
    int part;

    /* 11 bytes, each at most "%XX", plus the dot and the terminator. */
    if (!raw || !buffer || buffer_size < XX_NID_NAME_SIZE * 3U + 2U) {
        return false;
    }
    for (part = 0; part < 2; ++part) {
        size_t from = (part == 0) ? 0U : 8U;
        size_t to = (part == 0) ? 8U : XX_NID_NAME_SIZE;
        size_t index;
        if (part == 1) buffer[used++] = '.';
        for (index = from; index < to; ++index) {
            uint8_t character = raw[index];
            if (character <= 0x20U) continue;
            if (character < 0x7fU && character != '%' && character != '/' &&
                character != '\\' && character != ':' && character != '*' &&
                character != '?' && character != '"' && character != '<' &&
                character != '>' && character != '|' && character != '.') {
                buffer[used++] = (char)character;
            } else {
                buffer[used++] = '%';
                buffer[used++] = digits[(character >> 4) & 0x0f];
                buffer[used++] = digits[character & 0x0f];
            }
        }
    }
    if (used != 0U && buffer[used - 1U] == '.') --used;
    buffer[used] = '\0';
    if (used == 0U) {
        /* An all-blank field is legal; a positional stand-in beats dropping
         * the member. */
        if (xx_rt_snprintf(buffer, buffer_size, "record%u",
                           (unsigned)member_index) <= 0) {
            return false;
        }
    }
    return true;
}

/* Map a folder key to its one-based output-folder number, assigning the next
 * number the first time a key is seen. */
static bool xx_nid_folder_index(xx_nid_stream *stream, uint32_t key,
                                uint32_t *out_index) {
    size_t index;
    uint32_t *grown;

    if (!stream || !out_index) return false;
    for (index = 0U; index < stream->folder_count; ++index) {
        if (stream->folder_keys[index] == key) {
            *out_index = (uint32_t)(index + 1U);
            return true;
        }
    }
    /* Bounded by the entry count, which is a u16. */
    if (stream->folder_count >= XX_NID_MAX_ENTRIES) return false;
    grown = (uint32_t *)xx_mem_realloc(
        stream->folder_keys, sizeof(*grown) * (stream->folder_count + 1U));
    if (!grown) return false;
    stream->folder_keys = grown;
    stream->folder_keys[stream->folder_count++] = key;
    *out_index = (uint32_t)stream->folder_count;
    return true;
}

static void xx_nid_stream_free(void *pointer) {
    xx_nid_stream *stream = (xx_nid_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream->folder_keys);
    xx_mem_free(stream);
}

static bool xx_nid_add(xx_nid_stream *stream, const xx_nid_member *member) {
    xx_nid_member *grown;

    if (!stream || !member || stream->count >= XX_NID_MAX_ENTRIES) {
        return false;
    }
    grown = (xx_nid_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Walk the block chain to find where the member ends.  A chain that leaves the
 * file is clamped rather than rejected - see the file header comment. */
static bool xx_nid_measure_member(Abstractformat *self, xx_nid_member *member,
                                  int64_t span, xx_pd_struct *pd) {
    int64_t position;
    int32_t blocks = 0;
    bool first_stored = false;
    int64_t first_block_size = 0;

    if (!self || !member) return false;
    member->single_stored_block = false;
    position = member->data_offset - self->base_address;
    if (position < 0 || position > span) return false;

    for (;;) {
        uint8_t frame[XX_NID_BLOCK_FRAME_SIZE];
        uint32_t flags;
        int64_t block_size;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (position + XX_NID_BLOCK_FRAME_SIZE > span) break;
        if (!xx_nid_read_at(self, self->base_address + position, frame,
                            sizeof(frame))) {
            return false;
        }
        flags = xx_nid_le16(frame + 1);
        block_size = (int64_t)xx_nid_le16(frame + 3);
        if (blocks == 0) {
            first_stored = (flags & XX_NID_BLOCK_COMPRESSED) == 0U;
            first_block_size = block_size;
        }
        ++blocks;
        position += XX_NID_BLOCK_FRAME_SIZE + block_size;
        if (position > span) {
            /* A member whose chain leaves the volume: clamp and list it. */
            position = span;
            break;
        }
        if (flags & XX_NID_BLOCK_LAST) {
            if (blocks == 1 && first_stored &&
                first_block_size == member->uncompressed_size) {
                member->single_stored_block = true;
            }
            break;
        }
        if (blocks > XX_NID_MAX_BLOCKS) break;
    }

    member->compressed_size = position - (member->data_offset -
                                          self->base_address);
    if (member->compressed_size < 0) member->compressed_size = 0;
    return true;
}

static xx_nid_stream *xx_nid_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_nid_stream *stream = NULL;
    uint8_t header[XX_NID_HEADER_SIZE];
    uint8_t *directory = NULL;
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t archive_end;
    int64_t first_offset;
    int32_t count;
    int32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_NID_HEADER_SIZE + XX_NID_ENTRY_SIZE) return NULL;
    if (!xx_nid_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (header[0] != 'N' || header[1] != 'I' || header[2] != 0x15U ||
        header[3] != 0x01U) {
        return NULL;
    }
    count = (int32_t)xx_nid_le16(header + 6);
    if (count < 1 || count > XX_NID_MAX_ENTRIES) return NULL;

    directory_offset = XX_NID_HEADER_SIZE;
    directory_size = (int64_t)count * XX_NID_ENTRY_SIZE;
    if (!xx_nid_range_within(span, directory_offset, directory_size)) {
        return NULL;
    }
    /* Bounded by the u16 count: at most 0xffff * 29 == ~1.9 MiB. */
    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_nid_read_at(self, self->base_address + directory_offset, directory,
                        (size_t)directory_size)) {
        goto fail;
    }

    /* Header and directory tile the front of the volume: the first entry's
     * data offset (one based) is fixed by the entry count.  This is the check
     * that makes the four-byte magic safe to detect on. */
    first_offset = (int64_t)(int32_t)xx_nid_le32(directory + 1);
    if (first_offset != directory_offset + directory_size + 1) goto fail;
    if (directory[0] != 1U) goto fail;
    if (!xx_nid_valid_fcb_name(directory + 9)) goto fail;
    if ((int32_t)xx_nid_le32(directory + 25) < 0) goto fail;

    stream = (xx_nid_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->number_of_entries = (uint32_t)count;
    stream->directory_offset = directory_offset;
    stream->directory_size = directory_size;

    archive_end = directory_offset + directory_size;
    for (index = 0; index < count; ++index) {
        const uint8_t *entry = directory + (size_t)index * XX_NID_ENTRY_SIZE;
        xx_nid_member member;
        char stem[XX_NID_NAME_SIZE * 3 + 2];
        char prefix[24];
        uint32_t folder_index = 0U;
        int64_t offset;
        int64_t size;
        int64_t member_end;
        char *name;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* The reference stops the walk here rather than failing: the
         * continuation entries of the next volume carry method 2. */
        if (entry[0] != 1U) break;
        offset = (int64_t)(int32_t)xx_nid_le32(entry + 1);
        size = (int64_t)(int32_t)xx_nid_le32(entry + 25);
        if (offset < 1 || size < 0) break;

        xx_mem_zero(&member, sizeof(member));
        member.record_offset = self->base_address + directory_offset +
                               (int64_t)index * XX_NID_ENTRY_SIZE;
        /* The stored offset is one based. */
        member.data_offset = self->base_address + (offset - 1);
        member.uncompressed_size = size;
        member.folder_key = xx_nid_le32(entry + 5);
        member.attributes = entry[0x14];
        member.dos_time = xx_nid_le16(entry + 0x15);
        member.dos_date = xx_nid_le16(entry + 0x17);

        if (!xx_nid_folder_index(stream, member.folder_key, &folder_index)) {
            goto fail;
        }
        if (!xx_nid_fcb_name(entry + 9, (size_t)index, stem, sizeof(stem))) {
            goto fail;
        }
        if (xx_rt_snprintf(prefix, sizeof(prefix), "Folder%u/",
                           (unsigned)folder_index) <= 0) {
            goto fail;
        }
        name = xx_str_concat(prefix, stem);
        if (!name) goto fail;
        if (!xx_nid_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }
        member.name = name;

        if (!xx_nid_measure_member(self, &member, span, pd)) {
            xx_str_free(name);
            goto fail;
        }
        member_end = member.data_offset - self->base_address +
                     member.compressed_size;
        if (member_end > archive_end) archive_end = member_end;
        if (!xx_nid_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
    }

    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    stream->archive_size = archive_end < span ? archive_end : span;
    xx_mem_free(directory);
    return stream;

fail:
    xx_mem_free(directory);
    xx_nid_stream_free(stream);
    return NULL;
}

/* Materialise one member.  A single stored block needs no codec: the payload
 * behind the five-byte frame IS the plaintext. */
static bool xx_nid_decode(Abstractformat *self, const xx_nid_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t plain_size;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member || (pd && xx_pd_is_stopped(pd))) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0 ||
        member->compressed_size > XX_NID_MAX_COMPRESSED ||
        member->uncompressed_size > XX_NID_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size == 0) {
        /* A legal empty member; hand back a freeable pointer. */
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    plain_size = (size_t)member->uncompressed_size;

    if (member->single_stored_block) {
        /* The frame is not part of the data. */
        if (member->compressed_size <
            XX_NID_BLOCK_FRAME_SIZE + member->uncompressed_size) {
            return false;
        }
        output = (uint8_t *)xx_mem_alloc(plain_size);
        if (!output) return false;
        if (!xx_nid_read_at(self, member->data_offset + XX_NID_BLOCK_FRAME_SIZE,
                            output, plain_size)) {
            xx_mem_free(output);
            return false;
        }
        *out = output;
        *out_size = plain_size;
        return true;
    }

    if (member->compressed_size < XX_NID_BLOCK_FRAME_SIZE) return false;
    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_nid_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc(plain_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* The codec wants the member extent starting at its first block frame and
     * the declared plaintext length; it produces exactly that many bytes or
     * fails.  A member whose chain was clamped at the end of the volume ends
     * up here and fails, which is the reference's behaviour for a set split
     * across volumes. */
    if (!xx_nid_decode_memory(input, (size_t)member->compressed_size, output,
                              plain_size, &written) ||
        written != plain_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = plain_size;
    return true;
}

static bool xx_nid_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_nid_get_option(const xx_list_s *options,
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

static bool xx_nid_set_record(xx_archive_record *record,
                              const xx_nid_member *member) {
    int64_t stream_offset;
    int64_t stream_size;

    if (!record || !member || !member->name) return false;
    /* A stored member is published as the payload behind the frame, so a
     * generic consumer can copy it without touching the codec. */
    stream_offset = member->single_stored_block
                        ? member->data_offset + XX_NID_BLOCK_FRAME_SIZE
                        : member->data_offset;
    stream_size = member->single_stored_block ? member->uncompressed_size
                                              : member->compressed_size;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->record_offset;
    record->header_size = XX_NID_ENTRY_SIZE;
    record->data_offset = stream_offset;
    record->compressed_size = stream_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           /* 0 = stored, 1 = NI block chain; the container's own method byte
            * is always 1, so this reports what the reader will actually do. */
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          member->single_stored_block ? 0U
                                                                      : 1U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           /* The record stores time first, date second. */
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_TIMESTAMP,
               ((uint64_t)member->dos_date << 16) |
                   (uint64_t)member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_nid_init(xx_nid *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_NID;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    archive->format.os = XX_OS_DOS;
    xx_format_set_mime_type(&archive->format, "application/x-nid");
    xx_format_set_extension(&archive->format, "nid");
    archive->format.check_is_valid = xx_nid_check_is_valid;
    archive->format.handle_base_info = xx_nid_handle_base_info;
    archive->format.get_format_size = xx_nid_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_nid_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_nid_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_nid_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_nid_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_nid_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_nid_free_archive_records_reading;
    archive->format.destroy = xx_nid_vtable_destroy;
    archive->archive_size = -1;
}

xx_nid *xx_nid_create(xx_io_device *device, int64_t base_address) {
    xx_nid *archive = (xx_nid *)xx_mem_alloc(sizeof(*archive));

    if (archive) xx_nid_init(archive, device, base_address);
    return archive;
}

void xx_nid_destroy(xx_nid *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->number_of_entries = 0U;
    archive->directory_offset = 0;
    archive->directory_size = 0;
    archive->archive_size = -1;
}

static void xx_nid_vtable_destroy(Abstractformat *self) {
    xx_nid_destroy((xx_nid *)self);
}

void xx_nid_free(xx_nid *archive) {
    if (!archive) return;
    xx_nid_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_nid_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_nid_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_nid_parse(self, pd);
    if (!stream) return false;
    xx_nid_stream_free(stream);
    return true;
}

bool xx_nid_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_nid *archive = (xx_nid *)self;
    xx_nid_stream *stream;
    int64_t total;

    if (!self) return false;
    stream = xx_nid_parse(self, pd);
    if (!stream) {
        archive->number_of_records = 0U;
        archive->number_of_entries = 0U;
        archive->directory_offset = 0;
        archive->directory_size = 0;
        archive->archive_size = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    archive->number_of_records = stream->count;
    archive->number_of_entries = stream->number_of_entries;
    archive->directory_offset = stream->directory_offset;
    archive->directory_size = stream->directory_size;
    archive->archive_size = stream->archive_size;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    total = xx_io_total_size(self->device);
    if (total > self->base_address + stream->archive_size) {
        self->overlay_offset = self->base_address + stream->archive_size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->file_type = XX_FILE_TYPE_NID;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    xx_nid_stream_free(stream);
    return true;
}

int64_t xx_nid_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_nid_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_nid *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

xx_archive_record_state *xx_nid_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_nid_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_nid_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_nid_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_nid_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_nid_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_nid_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_nid_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_nid_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_nid_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_nid_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_nid_set_record(&state->current_record,
                                          &stream->items[stream->index]);
    return state->has_record;
}

bool xx_nid_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_nid_stream *stream;
    const xx_nid_member *member;
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
    stream = (xx_nid_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_nid_path_safe(member->name)) return false;

    path_option = xx_nid_get_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_nid_decode(self, member, &plain, &plain_size, pd);
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

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_nid_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_nid_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

uint64_t xx_nid_get_number_of_records(const xx_nid *archive) {
    return archive ? archive->number_of_records : 0U;
}

int64_t xx_nid_get_archive_size(const xx_nid *archive) {
    return archive ? archive->archive_size : -1;
}
