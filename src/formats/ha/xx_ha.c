/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HA archives (Harri Hirvola's HA 0.98/0.999, DOS and Unix).
 *
 *   archive header, 4 bytes at offset 0:
 *     0x00  "HA"
 *     0x02  u16 LE  number of members
 *
 *   member header, 17 fixed bytes then three variable fields:
 *     0x00  u8      type: version in the high nibble, method in the low one
 *     0x01  u32 LE  packed size
 *     0x05  u32 LE  original size
 *     0x09  u32 LE  CRC-32 of the original data
 *     0x0d  u32 LE  Unix timestamp
 *     0x11  char[]  directory, NUL terminated, 0xFF as the separator
 *     ..    char[]  file name, NUL terminated
 *     ..    u8      machine/OS byte, whose VALUE is also the length of the
 *                   machine-specific extension area that follows it
 *     ..    u8[machine]  extension area
 *
 *   so header_size = 20 + directory_length + name_length + machine, and the
 *   payload starts there. The next member header starts at
 *   data_offset + packed_size: nothing stores a member offset.
 *
 * Methods (the low nibble of the type byte):
 *     0  CPY  stored
 *     1  ASC  LZ77 + arithmetic coding
 *     2  HSC  order-4 PPM + arithmetic coding
 *     e  directory entry
 *     f  directory entry
 *
 * A type byte of 0xFF is a deleted/skipped entry: its header is parsed so
 * the chain can be walked past it, but it produces no member. Every other
 * entry must carry version 2 in the high nibble, which is the only version
 * HA ever wrote and, with a two-character magic, is the discriminator this
 * format really rests on.
 *
 * Methods this reader decodes: CPY, ASC and HSC. The two directory methods
 * are listed as folders and carry no payload.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ha/xx_ha.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/ha/xx_ha.h"

#include <stdio.h>

#define XX_HA_COPY_CHUNK (64 * 1024)

typedef struct xx_ha_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ha_member;

typedef struct xx_ha_stream_s {
    xx_ha_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ha_stream;

static void xx_ha_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ha_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_ha_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ha_path_safe(const char *name) {
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

static void xx_ha_stream_free(void *pointer) {
    xx_ha_stream *stream = (xx_ha_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ha_add(xx_ha_stream *stream,
                          const xx_ha_member *member) {
    xx_ha_member *grown = (xx_ha_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_HA_HEADER_SIZE 4
#define XX_HA_MEMBER_HEADER 17
#define XX_HA_MAX_MEMBERS 65535
#define XX_HA_MAX_FIELD 1024
#define XX_HA_MAX_DECODED (256 * 1024 * 1024)
#define XX_HA_HEADER_FIXED 20
#define XX_HA_VERSION 2U
#define XX_HA_TYPE_SKIP 0xFFU
#define XX_HA_METHOD_CPY 0x00U
#define XX_HA_METHOD_ASC 0x01U
#define XX_HA_METHOD_HSC 0x02U
#define XX_HA_METHOD_DIR1 0x0EU
#define XX_HA_METHOD_DIR2 0x0FU
#define XX_HA_PATH_SEPARATOR 0xFFU

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_ha_le16(const uint8_t *data);
static uint32_t xx_ha_le32(const uint8_t *data);
static bool xx_ha_read_field(Abstractformat *self, int64_t span, int64_t *cursor, uint8_t *buffer, size_t *length);
static char *xx_ha_make_name(const uint8_t *directory, size_t directory_size, const uint8_t *file, size_t file_size);
static xx_ha_stream *xx_ha_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_ha_decode(Abstractformat *self, const xx_ha_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The count is a u16, so this is a hard ceiling, not a policy. */
/* A runaway guard on the two NUL-terminated fields: nothing before them
 * states their length, so without a cap a file of non-zero bytes would be
 * walked to EOF twice per member. */
/* The fixed part, both NUL terminators and the machine byte: the constant
 * addend in header_size = 20 + directory + name + machine. */
/* 0xFF inside the directory field is HA's path separator, not a character. */

static uint16_t xx_ha_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_ha_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Read one NUL-terminated field, a byte at a time. The two fields are
 * variable length and nothing ahead of them says how long they are, so the
 * terminator has to be found before the next field's offset is known. The
 * terminator is consumed; it is not stored. */
static bool xx_ha_read_field(Abstractformat *self, int64_t span,
                             int64_t *cursor, uint8_t *buffer,
                             size_t *length) {
    size_t used = 0U;
    uint8_t byte;

    *length = 0U;
    for (;;) {
        if (*cursor >= span) return false;
        if (!xx_ha_read_at(self, self->base_address + *cursor, &byte, 1U)) {
            return false;
        }
        ++(*cursor);
        if (byte == 0U) break;
        if (used >= (size_t)XX_HA_MAX_FIELD) return false;
        buffer[used++] = byte;
    }
    *length = used;
    return true;
}

/* Join the directory and name fields into one '/'-separated path. Returns
 * NULL for anything that is not plain printable text, which -- together with
 * the version nibble -- is what stops unrelated data carrying the two bytes
 * "HA" from being walked as a member chain. */
static char *xx_ha_make_name(const uint8_t *directory, size_t directory_size,
                             const uint8_t *file, size_t file_size) {
    char *name;
    size_t out = 0U;
    size_t index;

    if (directory_size + file_size == 0U) return NULL;
    name = (char *)xx_mem_alloc(directory_size + file_size + 1U);
    if (!name) return NULL;
    for (index = 0U; index < directory_size; ++index) {
        uint8_t byte = directory[index];

        if (byte == (uint8_t)XX_HA_PATH_SEPARATOR) {
            name[out++] = '/';
            continue;
        }
        if (byte < 0x20U || byte > 0x7EU) {
            xx_str_free(name);
            return NULL;
        }
        name[out++] = (char)byte;
    }
    /* The separator byte never appears in the name field: HA writes it only
     * between directory components. */
    for (index = 0U; index < file_size; ++index) {
        uint8_t byte = file[index];

        if (byte < 0x20U || byte > 0x7EU) {
            xx_str_free(name);
            return NULL;
        }
        name[out++] = (char)byte;
    }
    name[out] = '\0';
    if (out == 0U) {
        xx_str_free(name);
        return NULL;
    }
    return name;
}

static xx_ha_stream *xx_ha_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_ha_stream *stream;
    uint8_t header[XX_HA_HEADER_SIZE];
    uint8_t entry[XX_HA_MEMBER_HEADER];
    uint8_t directory[XX_HA_MAX_FIELD];
    uint8_t file[XX_HA_MAX_FIELD];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count;
    int64_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_HA_HEADER_SIZE + XX_HA_MEMBER_HEADER) return NULL;
    if (!xx_ha_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (header[0] != (uint8_t)'H' || header[1] != (uint8_t)'A') return NULL;
    count = (int64_t)xx_ha_le16(header + 2);
    /* An archive with no members is not an archive; the count is the only
     * thing bounding the walk, since no member stores the next one's
     * offset. */
    if (count < 1 || count > XX_HA_MAX_MEMBERS) return NULL;

    stream = (xx_ha_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_HA_HEADER_SIZE;
    for (index = 0; index < count; ++index) {
        xx_ha_member member;
        int64_t cursor;
        int64_t header_size;
        int64_t data_offset;
        int64_t packed;
        int64_t original;
        uint32_t packed_raw;
        uint32_t original_raw;
        size_t directory_size = 0U;
        size_t file_size = 0U;
        uint8_t machine;
        uint8_t type;
        uint8_t method;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* A truncated tail ends the walk rather than failing the archive:
         * the members already read are genuine. */
        if (!xx_ha_range_within(span, offset, XX_HA_MEMBER_HEADER)) break;
        if (!xx_ha_read_at(self, self->base_address + offset, entry,
                           sizeof(entry))) {
            goto fail;
        }

        type = entry[0];
        packed_raw = xx_ha_le32(entry + 1);
        original_raw = xx_ha_le32(entry + 5);
        /* HA's own writer treats both size fields as signed 32-bit, so a
         * value with the top bit set is not a 3 GiB member, it is
         * nonsense. */
        if (packed_raw > 0x7FFFFFFFU || original_raw > 0x7FFFFFFFU) break;
        packed = (int64_t)packed_raw;
        original = (int64_t)original_raw;

        cursor = offset + XX_HA_MEMBER_HEADER;
        if (!xx_ha_read_field(self, span, &cursor, directory,
                              &directory_size) ||
            !xx_ha_read_field(self, span, &cursor, file, &file_size)) {
            break;
        }
        if (cursor >= span) break;
        if (!xx_ha_read_at(self, self->base_address + cursor, &machine, 1U)) {
            goto fail;
        }
        ++cursor;

        /* The machine byte is both the OS identifier and the length of the
         * area that follows it, so it is added twice over: once as the byte
         * itself (inside the fixed 20) and once as that area's size. */
        header_size = XX_HA_HEADER_FIXED + (int64_t)directory_size +
                      (int64_t)file_size + (int64_t)machine;
        data_offset = offset + header_size;
        if (!xx_ha_range_within(span, offset, header_size)) break;

        if (type != (uint8_t)XX_HA_TYPE_SKIP) {
            /* Version 2 is the only version HA ever wrote. With a magic of
             * just "HA" this nibble is the format's real gate: loosen it and
             * any two-byte coincidence walks a chain of garbage. */
            if ((uint8_t)(type >> 4) != (uint8_t)XX_HA_VERSION) break;
            method = (uint8_t)(type & 0x0FU);

            /* Unlike the Qt reference, a payload that runs past EOF is not
             * silently clamped: a member is published only when its whole
             * extent is inside the file. */
            if (!xx_ha_range_within(span, data_offset, packed)) break;

            xx_mem_zero(&member, sizeof(member));
            member.name = xx_ha_make_name(directory, directory_size, file,
                                          file_size);
            if (!member.name) break;
            member.header_offset = self->base_address + offset;
            member.header_size = header_size;
            member.data_offset = self->base_address + data_offset;
            member.compressed_size = packed;
            member.uncompressed_size = original;
            member.method = (uint32_t)method;
            member.timestamp = (uint64_t)xx_ha_le32(entry + 13);
            member.is_folder = (method == XX_HA_METHOD_DIR1) ||
                               (method == XX_HA_METHOD_DIR2);
            if (member.is_folder) {
                member.compressed_size = 0;
                member.uncompressed_size = 0;
            }
            if (!xx_ha_add(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
        }

        offset = data_offset + packed;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = offset < span ? offset : span;
    return stream;

fail:
    xx_ha_stream_free(stream);
    return NULL;
}


/* The stated original size is attacker-controlled, so it is capped before it
 * becomes an allocation. */

static bool xx_ha_decode(Abstractformat *self, const xx_ha_member *member,
                         uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t plain_size;
    bool ok;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > XX_HA_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;

    /* Everything HA defines but this reader cannot produce bytes for has to
     * fail here: the directory methods 0x0E/0x0F have no payload at all, and
     * methods 3..0x0D were never assigned. Falling through to the stored
     * path for any of them would hand back an arithmetic-coded bitstream
     * dressed as file data, which nothing downstream can tell from the real
     * thing. */
    if (member->method != XX_HA_METHOD_CPY &&
        member->method != XX_HA_METHOD_ASC &&
        member->method != XX_HA_METHOD_HSC) {
        return false;
    }
    if (member->method == XX_HA_METHOD_CPY &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }
    /* Both coders emit at least one byte of arithmetic-coder state, so an
     * empty payload can only ever be a stored empty member. */
    if (member->method != XX_HA_METHOD_CPY &&
        (member->compressed_size == 0 || member->uncompressed_size == 0)) {
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    if (member->compressed_size > 0) {
        packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
        if (!packed) return false;
        if (!xx_ha_read_at(self, member->data_offset, packed,
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
     * failure, so a genuinely empty member still gets one byte. */
    plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : (size_t)1);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_HA_METHOD_CPY) {
        size_t index;

        for (index = 0U; index < plain_size; ++index) {
            plain[index] = packed[index];
        }
        written = plain_size;
        ok = true;
    } else if (member->method == XX_HA_METHOD_ASC) {
        ok = xx_ha_asc_decode_memory(packed, (size_t)member->compressed_size,
                                     plain, plain_size, &written);
    } else {
        ok = xx_ha_hsc_decode_memory(packed, (size_t)member->compressed_size,
                                     plain, plain_size, &written);
    }
    xx_mem_free(packed);

    /* Returning true with fewer bytes than the header promised is the one
     * failure a caller cannot detect. ASC and HSC share an arithmetic coder
     * and differ only in the model driving it, so a member decoded with the
     * wrong one runs on and stops at a plausible length -- which is why the
     * model comes from the method nibble and never from a retry. */
    if (!ok || written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ha_init(xx_ha *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_HA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ha");
    xx_format_set_extension(&archive->format, "ha");
    archive->format.check_is_valid = xx_ha_check_is_valid;
    archive->format.handle_base_info = xx_ha_handle_base_info;
    archive->format.get_format_size = xx_ha_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ha_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ha_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ha_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ha_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ha_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ha_free_archive_records_reading;
    archive->format.destroy = xx_ha_vtable_destroy;
}

xx_ha *xx_ha_create(xx_io_device *device, int64_t base_address) {
    xx_ha *archive = (xx_ha *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ha_init(archive, device, base_address);
    return archive;
}

void xx_ha_destroy(xx_ha *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ha_free(xx_ha *archive) {
    if (!archive) return;
    xx_ha_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ha_vtable_destroy(Abstractformat *self) {
    xx_ha_destroy((xx_ha *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ha_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ha_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ha_parse(self, pd);
    if (!stream) return false;
    xx_ha_stream_free(stream);
    return true;
}

bool xx_ha_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ha *archive = (xx_ha *)self;
    xx_ha_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ha_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ha_stream_free(stream);
    return true;
}

int64_t xx_ha_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ha_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ha *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ha_set_record(xx_archive_record *record,
                                 const xx_ha_member *member) {
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

static bool xx_ha_copy_options(xx_list_s *target,
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

static const xx_var *xx_ha_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ha_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ha_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ha_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ha_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ha_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ha_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ha_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ha_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ha_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ha_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ha_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ha_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ha_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ha_stream *stream;
    const xx_ha_member *member;
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
    stream = (xx_ha_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ha_path_safe(member->name)) return false;

    path_option = xx_ha_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ha_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ha_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_ha_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
