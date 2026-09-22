/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JGPAK archives (.PAK).
 *
 * Header, from offset 0:
 *
 *   0x00  char[7]  "JGPAK" 00 01 -- the literal signature, version included
 *   0x07  i32 LE   length of the description banner
 *   0x0B  char[]   description banner, that many bytes, no terminator
 *   +     i32 LE   length of the version banner
 *   +     char[]   version banner
 *   +     u8       flag byte, not interpreted by the reference reader
 *   +     i32 LE   number of members, 0 allowed
 *   +              the directory starts here
 *
 * One directory record, repeated member count times, all fields little
 * endian:
 *
 *   +0x00  u8      name length, never 0
 *   +0x01  char[]  member name, that many bytes, no terminator
 *   +n+0   u16     MS-DOS time
 *   +n+2   u16     MS-DOS date
 *   +n+4   i32     uncompressed size
 *   +n+8   i32     absolute file offset of the stream
 *   +n+12  i32     compressed size
 *   +n+16  u32     CRC-32 of the decoded member, already finalised
 *   +n+20  u8[4]   unused
 *
 * so a record is 1 + name length + 24 bytes. Every member stream is LZHUF
 * ("-lh1-" style: 4 KiB ring buffer, adaptive Huffman); the container
 * carries no method field at all, so the reader states the single method
 * itself.
 *
 * The archive ends where the last stream ends; a file may carry trailing
 * bytes beyond that and the archive size is still the tiled extent.
 *
 * The signature is only seven bytes, five of them ordinary ASCII. What
 * actually decides this format is that the payload area tiles exactly: the
 * first member's stream starts at the byte after the directory, each later
 * one starts where the previous ended, no stream is empty, and the whole
 * chain stays inside the file. That check must not be loosened into "every
 * stream lies somewhere inside the file".
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jgpak/xx_jgpak.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzh/xx_lzh.h"

#include <stdio.h>

#define XX_JGPAK_COPY_CHUNK (64 * 1024)

typedef struct xx_jgpak_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_jgpak_member;

typedef struct xx_jgpak_stream_s {
    xx_jgpak_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_jgpak_stream;

static void xx_jgpak_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_jgpak_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_jgpak_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_jgpak_path_safe(const char *name) {
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

static void xx_jgpak_stream_free(void *pointer) {
    xx_jgpak_stream *stream = (xx_jgpak_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_jgpak_add(xx_jgpak_stream *stream,
                          const xx_jgpak_member *member) {
    xx_jgpak_member *grown = (xx_jgpak_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_JGPAK_SIGNATURE_SIZE 7
#define XX_JGPAK_TAIL_SIZE 24
#define XX_JGPAK_MAX_NAME_SIZE 255
#define XX_JGPAK_RECORD_BUFFER (XX_JGPAK_MAX_NAME_SIZE + XX_JGPAK_TAIL_SIZE)
#define XX_JGPAK_MAX_MEMBERS 65535
#define XX_JGPAK_MAX_STRING 4096
#define XX_JGPAK_MAX_DECODED (256 * 1024 * 1024)
#define XX_JGPAK_METHOD_LZH1 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_jgpak_le16(const uint8_t *data);
static uint32_t xx_jgpak_le32(const uint8_t *data);
static bool xx_jgpak_name_valid(const uint8_t *name, size_t size);
static xx_jgpak_stream *xx_jgpak_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_jgpak_decode(Abstractformat *self, const xx_jgpak_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Fixed part of a directory record, everything after the name. */
/* The count is i32, but every member costs at least 26 directory bytes plus
 * one payload byte, so the writer cannot reach anything like this. */
/* Both leading strings are short product banners in every known archive. */

static uint16_t xx_jgpak_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_jgpak_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Member names are bare DOS/Windows file names -- no directory component
 * ever appears -- so anything that could escape the output directory, or any
 * control byte, means this is not a directory record. The format genuinely
 * permits bytes above 0x7E here: the writer stores the name as the local
 * OEM code page gave it, so accented DOS file names are normal and are NOT
 * a rejection. */
static bool xx_jgpak_name_valid(const uint8_t *name, size_t size) {
    size_t index;

    if (size == 0U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t byte = name[index];

        if (byte < 0x20U) return false;
        if (byte == '/' || byte == '\\' || byte == ':' || byte == '*' ||
            byte == '?' || byte == '"' || byte == '<' || byte == '>' ||
            byte == '|') {
            return false;
        }
    }
    if (size == 1U && name[0] == '.') return false;
    if (size == 2U && name[0] == '.' && name[1] == '.') return false;
    return true;
}

static xx_jgpak_stream *xx_jgpak_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    static const uint8_t signature[XX_JGPAK_SIGNATURE_SIZE] = {
        'J', 'G', 'P', 'A', 'K', 0x00U, 0x01U};
    xx_jgpak_stream *stream = NULL;
    uint8_t scratch[XX_JGPAK_RECORD_BUFFER];
    char name[XX_JGPAK_MAX_NAME_SIZE + 1];
    int64_t total;
    int64_t span;
    int64_t offset = 0;
    int64_t directory_end = 0;
    int64_t expected = 0;
    int32_t member_count;
    int32_t index;
    size_t position;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Signature, two empty strings, the flag byte and the member count. */
    if (span < XX_JGPAK_SIGNATURE_SIZE + 4 + 4 + 1 + 4) return NULL;

    if (!xx_jgpak_read_at(self, self->base_address, scratch,
                          (size_t)XX_JGPAK_SIGNATURE_SIZE)) {
        return NULL;
    }
    /* The literal signature, version byte included. Seven bytes is not much
     * on its own -- the payload tiling at the bottom is what decides. */
    if (xx_rt_memcmp(scratch, signature, (size_t)XX_JGPAK_SIGNATURE_SIZE) !=
        0) {
        return NULL;
    }
    offset = XX_JGPAK_SIGNATURE_SIZE;

    for (index = 0; index < 2; ++index) {
        int64_t length;

        if (!xx_jgpak_range_within(span, offset, 4)) return NULL;
        if (!xx_jgpak_read_at(self, self->base_address + offset, scratch,
                              4U)) {
            return NULL;
        }
        length = (int64_t)(int32_t)xx_jgpak_le32(scratch);
        if (length < 0 || length > XX_JGPAK_MAX_STRING) return NULL;
        offset += 4;
        if (!xx_jgpak_range_within(span, offset, length)) return NULL;
        /* The banners are plain 8-bit text. A control byte here means the
         * file only happens to start with the signature, so this is part of
         * the detection and not cosmetic validation. Tab is the one control
         * byte the writer emits. */
        while (length > 0) {
            size_t chunk = (size_t)((length > (int64_t)sizeof(scratch))
                                        ? (int64_t)sizeof(scratch)
                                        : length);

            if (pd && xx_pd_is_stopped(pd)) return NULL;
            if (!xx_jgpak_read_at(self, self->base_address + offset, scratch,
                                  chunk)) {
                return NULL;
            }
            for (position = 0U; position < chunk; ++position) {
                if (scratch[position] < 0x20U && scratch[position] != '\t') {
                    return NULL;
                }
            }
            offset += (int64_t)chunk;
            length -= (int64_t)chunk;
        }
    }

    if (!xx_jgpak_range_within(span, offset, 5)) return NULL;
    if (!xx_jgpak_read_at(self, self->base_address + offset, scratch, 5U)) {
        return NULL;
    }
    /* scratch[0] is a flag byte the reference reader does not interpret. */
    member_count = (int32_t)xx_jgpak_le32(scratch + 1);
    if (member_count < 0 || member_count > XX_JGPAK_MAX_MEMBERS) return NULL;
    offset += 5;

    stream = (xx_jgpak_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* An empty archive carries no payload at all, so the whole header is the
     * archive. The signature and both banners have already been checked, so
     * this is still a decision and not a guess. */
    if (member_count == 0) {
        stream->archive_size = offset;
        return stream;
    }

    for (index = 0; index < member_count; ++index) {
        xx_jgpak_member member;
        const uint8_t *tail;
        int64_t name_size;
        int64_t record_offset = offset;
        int64_t uncompressed_size;
        int64_t data_offset;
        int64_t compressed_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_jgpak_range_within(span, offset, 1)) goto fail;
        if (!xx_jgpak_read_at(self, self->base_address + offset, scratch,
                              1U)) {
            goto fail;
        }
        name_size = (int64_t)scratch[0];
        /* A zero-length name would make the record ambiguous with the tail
         * of the previous one. */
        if (name_size == 0) goto fail;
        offset += 1;
        if (!xx_jgpak_range_within(span, offset,
                                   name_size + XX_JGPAK_TAIL_SIZE)) {
            goto fail;
        }
        if (!xx_jgpak_read_at(self, self->base_address + offset, scratch,
                              (size_t)(name_size + XX_JGPAK_TAIL_SIZE))) {
            goto fail;
        }
        if (!xx_jgpak_name_valid(scratch, (size_t)name_size)) goto fail;
        for (position = 0U; position < (size_t)name_size; ++position) {
            name[position] = (char)scratch[position];
        }
        name[(size_t)name_size] = '\0';

        tail = scratch + name_size;
        uncompressed_size = (int64_t)(int32_t)xx_jgpak_le32(tail + 4);
        data_offset = (int64_t)(int32_t)xx_jgpak_le32(tail + 8);
        compressed_size = (int64_t)(int32_t)xx_jgpak_le32(tail + 12);
        /* All three are i32 in the container and the reference reader
         * refuses any of them negative. */
        if (uncompressed_size < 0 || data_offset < 0 || compressed_size < 0) {
            goto fail;
        }
        if (!xx_jgpak_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + record_offset;
        member.header_size = 1 + name_size + XX_JGPAK_TAIL_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = XX_JGPAK_METHOD_LZH1;
        /* Raw MS-DOS time and date, packed time | (date << 16). The CRC-32
         * at tail+16 is already finalised, but the member struct has no
         * place for it. */
        member.timestamp = (uint64_t)xx_jgpak_le16(tail) |
                           ((uint64_t)xx_jgpak_le16(tail + 2) << 16);
        member.is_folder = false;
        if (!xx_jgpak_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset += name_size + XX_JGPAK_TAIL_SIZE;
    }
    directory_end = offset;

    /* The payload area tiles exactly: the first stream starts at the byte
     * after the directory and each later one starts where the previous
     * ended. This is the structural check that makes the short signature
     * safe, so it must not be relaxed to "each stream lies inside the
     * file". */
    expected = directory_end;
    for (position = 0U; position < stream->count; ++position) {
        int64_t relative = stream->items[position].data_offset -
                           self->base_address;

        if (relative != expected) goto fail;
        /* An empty stream cannot exist: LZHUF always emits at least one
         * byte, and a zero-length stream would let two members claim the
         * same offset. */
        if (stream->items[position].compressed_size == 0) goto fail;
        expected = relative + stream->items[position].compressed_size;
    }
    if (expected > span) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* Trailing bytes past the last stream are not part of the archive. */
    stream->archive_size = expected;
    return stream;

fail:
    xx_jgpak_stream_free(stream);
    return NULL;
}


/* The directory's uncompressed size is attacker-controlled; refuse rather
 * than attempt an allocation above this. */

/* The container states no method anywhere: every JGPAK stream is LZHUF.
 * parse puts this single derived value in member.method so that a future
 * variant adding a real method field cannot silently reuse it. */

static bool xx_jgpak_decode(Abstractformat *self,
                            const xx_jgpak_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t alloc_size = 0U;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_JGPAK_MAX_DECODED ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    /* Any other value would mean a later reader added a method and forgot
     * this switch; decoding it as LZHUF anyway produces plausible garbage. */
    if (member->method != XX_JGPAK_METHOD_LZH1) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_jgpak_read_at(self, member->data_offset, packed,
                          (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    /* An empty member is legal (the directory just states size 0), and a
     * zero-byte allocation is not something to rely on, so keep one spare
     * byte; the capacity handed to the decoder is still the stated size. */
    alloc_size = (size_t)member->uncompressed_size;
    if (alloc_size == 0U) alloc_size = 1U;
    plain = (uint8_t *)xx_mem_alloc(alloc_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (!xx_lzh1_decode_memory(packed, (size_t)member->compressed_size, plain,
                               (size_t)member->uncompressed_size, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* A short decode reported as success is the one failure the caller
     * cannot detect, so the produced length must match the directory. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_jgpak_init(xx_jgpak *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_JGPAK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-jgpak");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_jgpak_check_is_valid;
    archive->format.handle_base_info = xx_jgpak_handle_base_info;
    archive->format.get_format_size = xx_jgpak_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_jgpak_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_jgpak_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_jgpak_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_jgpak_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_jgpak_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_jgpak_free_archive_records_reading;
    archive->format.destroy = xx_jgpak_vtable_destroy;
}

xx_jgpak *xx_jgpak_create(xx_io_device *device, int64_t base_address) {
    xx_jgpak *archive = (xx_jgpak *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_jgpak_init(archive, device, base_address);
    return archive;
}

void xx_jgpak_destroy(xx_jgpak *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_jgpak_free(xx_jgpak *archive) {
    if (!archive) return;
    xx_jgpak_destroy(archive);
    xx_mem_free(archive);
}

static void xx_jgpak_vtable_destroy(Abstractformat *self) {
    xx_jgpak_destroy((xx_jgpak *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_jgpak_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_jgpak_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_jgpak_parse(self, pd);
    if (!stream) return false;
    xx_jgpak_stream_free(stream);
    return true;
}

bool xx_jgpak_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_jgpak *archive = (xx_jgpak *)self;
    xx_jgpak_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_jgpak_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_jgpak_stream_free(stream);
    return true;
}

int64_t xx_jgpak_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_jgpak_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_jgpak *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_jgpak_set_record(xx_archive_record *record,
                                 const xx_jgpak_member *member) {
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

static bool xx_jgpak_copy_options(xx_list_s *target,
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

static const xx_var *xx_jgpak_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_jgpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_jgpak_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_jgpak_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_jgpak_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_jgpak_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_jgpak_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_jgpak_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_jgpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_jgpak_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_jgpak_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_jgpak_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_jgpak_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_jgpak_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_jgpak_stream *stream;
    const xx_jgpak_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_jgpak_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_jgpak_path_safe(member->name)) return false;

    path_option = xx_jgpak_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_jgpak_decode(self, member, &plain, &plain_size, pd);
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
        !xx_jgpak_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_jgpak_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
