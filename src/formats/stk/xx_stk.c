/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Coktel Vision STK / ITK / LTK / JTK archives.
 *
 * Two unrelated layouts ship under the same extensions. All fields are
 * LITTLE-endian in both.
 *
 * CLASSIC (no signature at all -- the directory itself is the evidence):
 *
 *     0x00  u16  member count, 1..20000
 *     0x02  directory, 22 bytes per member:
 *             +0x00  13 bytes  DOS name, NUL-terminated if shorter
 *             +0x0d  u32       stored size
 *             +0x11  u32       absolute offset of the member's chunk
 *             +0x15  u8        0 = stored, 1 = Coktel LZSS
 *     members follow the directory, addressed by their offset field rather
 *     than laid out in order.
 *
 * STK2 ("STK2." plus a version digit at offset 0):
 *
 *     0x00  6 bytes  "STK2." + version digit
 *     0x06  14 bytes timestamp text
 *     0x14  8 bytes  creator
 *     0x1c  u32      directory offset
 *     member data starts at 0x20 and runs back to back up to the directory.
 *
 *     directory:
 *       +0x00  u32   member count, 1..65535
 *       +0x04  u32   metadata offset
 *       +0x08  packed run of NUL-terminated names, up to the metadata offset
 *       metadata, 61 bytes per member:
 *         +0x00  u32  absolute file offset of this member's name
 *         +0x28  u32  stored size
 *         +0x2c  u32  plaintext size
 *
 * A compressed chunk in either generation is a u32 plaintext size followed by
 * a classic Okumura LZSS stream (4096-byte window preset to spaces, first
 * write at 4078, unbiased offsets).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stk/xx_stk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/coktellz/xx_coktellz.h"
#include <stdio.h>

#define XX_STK_COPY_CHUNK (64 * 1024)

typedef struct xx_stk_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_stk_member;

typedef struct xx_stk_stream_s {
    xx_stk_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_stk_stream;

static void xx_stk_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_stk_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_stk_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_stk_path_safe(const char *name) {
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

static void xx_stk_stream_free(void *pointer) {
    xx_stk_stream *stream = (xx_stk_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_stk_add(xx_stk_stream *stream,
                          const xx_stk_member *member) {
    xx_stk_member *grown = (xx_stk_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_STK_MAX_MEMBERS 65535
#define XX_STK_CLASSIC_MAX_MEMBERS 20000
#define XX_STK_ENTRY_SIZE 22
#define XX_STK_NAME_SIZE 13
#define XX_STK2_HEADER_SIZE 32
#define XX_STK2_RECORD_SIZE 61
#define XX_STK_MAX_NAME 255
#define XX_STK_METHOD_STORE 0U
#define XX_STK_METHOD_COKTEL_LZ 1U
#define XX_STK_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_stk_le16(const uint8_t *data);
static uint32_t xx_stk_le32(const uint8_t *data);
static bool xx_stk_name_byte_valid(uint8_t value);
static int64_t xx_stk_copy_name(const uint8_t *source, int64_t max_length, uint8_t *target);
static bool xx_stk_publish(xx_stk_stream *stream, const char *name, int64_t header_offset, int64_t header_size, int64_t data_offset, int64_t packed, int64_t plain, uint32_t method);
static xx_stk_stream *xx_stk_parse_classic(Abstractformat *self, int64_t span, xx_pd_struct *pd);
static xx_stk_stream *xx_stk_parse_stk2(Abstractformat *self, int64_t span, xx_pd_struct *pd);
static xx_stk_stream *xx_stk_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_stk_decode(Abstractformat *self, const xx_stk_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The classic generation stores its count in a u16 but the reference reader
 * refuses anything above 20000: the format has no signature, so a plausible
 * count is part of what identifies it at all. STK2's count is a u32 capped at
 * 65535. The larger of the two is the array cap. */

/* Longest name accepted from STK2's packed name blob. */

static uint16_t xx_stk_le16(const uint8_t *data) {
    return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

static uint32_t xx_stk_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* DOS names only; anything outside printable ASCII in a format that has no
 * signature is far more likely to be a false positive than an exotic name.
 * For the classic generation this is one of only three checks standing
 * between this reader and any file whose first two bytes look like a count. */
static bool xx_stk_name_byte_valid(uint8_t value) {
    return value >= 0x20U && value <= 0x7EU;
}

/* Copy a NUL-terminated or field-filling DOS name out of a directory entry,
 * rewriting the DOS separator. Returns the length, or 0 to reject. */
static int64_t xx_stk_copy_name(const uint8_t *source, int64_t max_length,
                                uint8_t *target) {
    int64_t index;

    for (index = 0; index < max_length; ++index) {
        uint8_t value = source[index];
        if (value == 0U) break;
        if (!xx_stk_name_byte_valid(value)) return 0;
        /* Coktel wrote DOS paths; normalise so a listing is portable. */
        target[index] = (value == '\\') ? (uint8_t)'/' : value;
    }
    if (index == 0) return 0;
    target[index] = 0U;
    return index;
}

static bool xx_stk_publish(xx_stk_stream *stream, const char *name,
                           int64_t header_offset, int64_t header_size,
                           int64_t data_offset, int64_t packed,
                           int64_t plain, uint32_t method) {
    xx_stk_member member;
    char *copy = xx_str_dup(name);

    if (!copy) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = copy;
    member.header_offset = header_offset;
    member.header_size = header_size;
    member.data_offset = data_offset;
    member.compressed_size = packed;
    member.uncompressed_size = plain;
    member.method = method;
    /* The classic directory has no date field at all, and STK2's timestamp is
     * a single archive-wide text string, not a per-member value. */
    member.timestamp = 0U;
    /* Neither layout records directories; a '/' in a name is part of the
     * name, not a tree. */
    member.is_folder = false;
    if (!xx_stk_add(stream, &member)) {
        xx_str_free(copy);
        return false;
    }
    return true;
}

/* Classic generation: u16 count, then count 22-byte entries. */
static xx_stk_stream *xx_stk_parse_classic(Abstractformat *self, int64_t span,
                                           xx_pd_struct *pd) {
    xx_stk_stream *stream = NULL;
    uint8_t entry[XX_STK_ENTRY_SIZE];
    uint8_t count_field[2];
    uint8_t prefix[4];
    uint8_t name[XX_STK_NAME_SIZE + 1];
    int64_t count;
    int64_t directory_end;
    int64_t index;
    int64_t entry_offset;
    int64_t stored_size;
    int64_t data_offset;
    int64_t stream_size;
    int64_t plain_size;
    uint32_t method;

    if (span < 2 + XX_STK_ENTRY_SIZE) return NULL;
    if (!xx_stk_read_at(self, self->base_address, count_field,
                        sizeof(count_field))) {
        return NULL;
    }
    count = (int64_t)xx_stk_le16(count_field);
    /* A zero count is not an archive, and the upper bound is what stops a
     * file whose first two bytes happen to be large from being walked as a
     * directory. */
    if (count == 0 || count > XX_STK_CLASSIC_MAX_MEMBERS) return NULL;
    directory_end = 2 + count * XX_STK_ENTRY_SIZE;
    if (!xx_stk_range_within(span, 0, directory_end)) return NULL;

    stream = (xx_stk_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0; index < count; ++index) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry_offset = 2 + index * XX_STK_ENTRY_SIZE;
        if (!xx_stk_read_at(self, self->base_address + entry_offset, entry,
                            sizeof(entry))) {
            goto fail;
        }
        if (xx_stk_copy_name(entry, XX_STK_NAME_SIZE, name) <= 0) goto fail;

        stored_size = (int64_t)xx_stk_le32(entry + 13);
        data_offset = (int64_t)xx_stk_le32(entry + 17);
        method = (uint32_t)entry[21];

        /* Only 0 and 1 were ever assigned, and this byte is the most
         * selective one in the entry. */
        if (method > 1U) goto fail;
        /* A member cannot start inside the directory that describes it, and
         * it cannot start past EOF. Together with the printable-name rule and
         * the count bound, this is what makes a signature-less format
         * identifiable at all. */
        if (data_offset < directory_end || data_offset > span) goto fail;

        if (method == 0U) {
            if (!xx_stk_range_within(span, data_offset, stored_size)) {
                goto fail;
            }
            if (!xx_stk_publish(stream, (const char *)name,
                                self->base_address + entry_offset,
                                XX_STK_ENTRY_SIZE,
                                self->base_address + data_offset, stored_size,
                                stored_size, 0U)) {
                goto fail;
            }
        } else {
            /* A compressed chunk is a u32 plaintext size then the LZSS
             * stream, so the chunk must hold at least that prefix. */
            if (!xx_stk_range_within(span, data_offset, 4)) goto fail;
            if (!xx_stk_read_at(self, self->base_address + data_offset, prefix,
                                sizeof(prefix))) {
                goto fail;
            }
            plain_size = (int64_t)xx_stk_le32(prefix);
            /* The stored size field of a compressed entry counts the stream
             * plus three, not plus four: the writer charged three bytes of
             * overhead for the four-byte prefix. Matching the reference here
             * matters because the codec stops on output count, so one byte of
             * slack at the end is harmless but a byte short is a failure. */
            stream_size = (stored_size > 3) ? (stored_size - 3) : 0;
            if (stream_size > span - (data_offset + 4)) {
                stream_size = span - (data_offset + 4);
            }
            if (stream_size < 0) stream_size = 0;
            if (!xx_stk_publish(stream, (const char *)name,
                                self->base_address + entry_offset,
                                XX_STK_ENTRY_SIZE,
                                self->base_address + data_offset + 4,
                                stream_size, plain_size, 1U)) {
                goto fail;
            }
        }
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_stk_stream_free(stream);
    return NULL;
}

/* STK2 generation: signed header, directory placed after the member data. */
static xx_stk_stream *xx_stk_parse_stk2(Abstractformat *self, int64_t span,
                                        xx_pd_struct *pd) {
    xx_stk_stream *stream = NULL;
    uint8_t header[XX_STK2_HEADER_SIZE];
    uint8_t directory[8];
    uint8_t record[XX_STK2_RECORD_SIZE];
    uint8_t name[XX_STK_MAX_NAME + 1];
    uint8_t byte;
    int64_t directory_offset;
    int64_t count;
    int64_t meta_offset;
    int64_t name_base;
    int64_t meta_bytes;
    int64_t index;
    int64_t cursor;
    int64_t record_offset;
    int64_t name_pointer;
    int64_t stored_size;
    int64_t plain_size;
    int64_t length;
    int64_t chunk_offset;
    bool compressed;

    if (span < XX_STK2_HEADER_SIZE) return NULL;
    if (!xx_stk_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    directory_offset = (int64_t)xx_stk_le32(header + 28);
    /* The directory sits after the member data, so it cannot overlap the
     * header, and its first two words must fit. */
    if (directory_offset < XX_STK2_HEADER_SIZE) return NULL;
    if (!xx_stk_range_within(span, directory_offset, 8)) return NULL;
    if (!xx_stk_read_at(self, self->base_address + directory_offset, directory,
                        sizeof(directory))) {
        return NULL;
    }
    count = (int64_t)xx_stk_le32(directory);
    if (count == 0 || count > XX_STK_MAX_MEMBERS) return NULL;
    meta_offset = (int64_t)xx_stk_le32(directory + 4);
    name_base = directory_offset + 8;
    /* The packed name blob fills [name_base, meta_offset); an inverted pair
     * means the directory is not one. */
    if (meta_offset < name_base || meta_offset > span) return NULL;
    meta_bytes = count * XX_STK2_RECORD_SIZE;
    if (!xx_stk_range_within(span, meta_offset, meta_bytes)) return NULL;

    stream = (xx_stk_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* Members are laid end to end from the end of the header, so the running
     * total of stored sizes must land exactly on the directory offset. That
     * equality is the decisive structural invariant of this generation -- the
     * header's six signature bytes alone would be weak, and the offsets are
     * never stated per member, only implied by this chain. */
    cursor = XX_STK2_HEADER_SIZE;
    for (index = 0; index < count; ++index) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        record_offset = meta_offset + index * XX_STK2_RECORD_SIZE;
        if (!xx_stk_read_at(self, self->base_address + record_offset, record,
                            sizeof(record))) {
            goto fail;
        }
        name_pointer = (int64_t)xx_stk_le32(record);
        stored_size = (int64_t)xx_stk_le32(record + 40);
        plain_size = (int64_t)xx_stk_le32(record + 44);

        chunk_offset = cursor;
        if (stored_size > directory_offset - cursor) goto fail;
        cursor += stored_size;

        /* The name pointer is an absolute file offset into the blob. A bad
         * one is not fatal: the member's position comes from the chain above,
         * not from its name, so an index name is substituted rather than
         * dropping a member whose data is perfectly locatable. */
        length = 0;
        if (name_pointer >= name_base && name_pointer < meta_offset) {
            while (length < XX_STK_MAX_NAME &&
                   name_pointer + length < meta_offset) {
                if (!xx_stk_read_at(self,
                                    self->base_address + name_pointer + length,
                                    &byte, 1U)) {
                    goto fail;
                }
                if (byte == 0U) break;
                if (!xx_stk_name_byte_valid(byte)) {
                    length = 0;
                    break;
                }
                name[length] = (byte == '\\') ? (uint8_t)'/' : byte;
                ++length;
            }
        }
        if (length <= 0) {
            if (xx_rt_snprintf((char *)name, sizeof(name), "file%d",
                               (int)index) <= 0) {
                goto fail;
            }
        } else {
            name[length] = 0U;
        }

        /* STK2 records no method byte. A stored size that differs from the
         * plaintext size means the chunk carries the 4-byte prefix and an
         * LZSS stream; equal sizes mean the bytes are the member. */
        compressed = (plain_size != stored_size) && (stored_size >= 4);
        if (compressed) {
            if (!xx_stk_range_within(span, chunk_offset, stored_size)) {
                goto fail;
            }
            if (!xx_stk_publish(stream, (const char *)name,
                                self->base_address + record_offset,
                                XX_STK2_RECORD_SIZE,
                                self->base_address + chunk_offset + 4,
                                stored_size - 4, plain_size, 1U)) {
                goto fail;
            }
        } else {
            if (!xx_stk_range_within(span, chunk_offset, stored_size)) {
                goto fail;
            }
            if (!xx_stk_publish(stream, (const char *)name,
                                self->base_address + record_offset,
                                XX_STK2_RECORD_SIZE,
                                self->base_address + chunk_offset, stored_size,
                                stored_size, 0U)) {
                goto fail;
            }
        }
    }
    /* Anything but an exact landing means these records do not describe this
     * file's member data. Relaxing this to "<=" would accept arbitrary bytes
     * behind a "STK2." prefix. */
    if (cursor != directory_offset) goto fail;
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_stk_stream_free(stream);
    return NULL;
}

static xx_stk_stream *xx_stk_parse(Abstractformat *self, xx_pd_struct *pd) {
    uint8_t signature[5];
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < 2 + XX_STK_ENTRY_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    /* "STK2." is the only signature either generation has. Its absence is not
     * evidence against the classic layout -- that one has none -- so the
     * classic walk is tried whenever the signature is missing. */
    if (span >= XX_STK2_HEADER_SIZE &&
        xx_stk_read_at(self, self->base_address, signature,
                       sizeof(signature)) &&
        signature[0] == 'S' && signature[1] == 'T' && signature[2] == 'K' &&
        signature[3] == '2' && signature[4] == '.') {
        return xx_stk_parse_stk2(self, span, pd);
    }
    return xx_stk_parse_classic(self, span, pd);
}


/* The classic generation's own compression byte. STK2 has no such field --
 * it infers compression from stored size != plaintext size -- so these two
 * numbers are reused there, keeping one meaning for member->method across
 * both layouts. */

/* The plaintext size is an attacker-controlled u32 in both layouts: refuse
 * rather than attempt the allocation. */

/* Extraction of one STK member.
 *
 * parse has already stripped the compressed chunk's 4-byte plaintext-size
 * prefix: data_offset points at the LZSS bytes and uncompressed_size holds
 * the value that prefix carried, which is what the codec wants. */
static bool xx_stk_decode(Abstractformat *self, const xx_stk_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t packed_size;
    size_t plain_size;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_STK_MAX_DECODED ||
        member->uncompressed_size > XX_STK_MAX_DECODED) {
        return false;
    }
    if (member->method != XX_STK_METHOD_STORE &&
        member->method != XX_STK_METHOD_COKTEL_LZ) {
        /* Nothing else is defined; treating an unknown value as stored would
         * hand the caller an LZSS stream that looks like data. */
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    packed_size = (size_t)member->compressed_size;

    if (member->uncompressed_size == 0) {
        /* A genuinely empty member. xx_mem_alloc(0) is not worth relying on. */
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }
    /* A nonempty member with no bytes behind it cannot be decoded; the LZSS
     * codec treats input EOF before the declared size as a failure and so
     * does this. */
    if (member->compressed_size == 0) return false;

    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed) return false;
    if (!xx_stk_read_at(self, member->data_offset, packed, packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_STK_METHOD_STORE) {
        /* parse gives a stored member the same two sizes; a disagreement
         * would mean handing back a buffer that is not the member. */
        if (packed_size != plain_size) {
            xx_mem_free(packed);
            return false;
        }
        *out = packed;
        *out_size = plain_size;
        return true;
    }

    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The declared size is authoritative for this codec: the stream may end
     * mid-match and the final flag byte may carry unused bits, so it stops on
     * output count, not input EOF. */
    if (!xx_coktellz_decode_memory(packed, packed_size, plain, plain_size,
                                   &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(plain);
        return false;
    }
    /* Never report success with fewer bytes than the chunk prefix promised. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = plain_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_stk_init(xx_stk *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_STK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-coktel-stk");
    xx_format_set_extension(&archive->format, "stk");
    archive->format.check_is_valid = xx_stk_check_is_valid;
    archive->format.handle_base_info = xx_stk_handle_base_info;
    archive->format.get_format_size = xx_stk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_stk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_stk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_stk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_stk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_stk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_stk_free_archive_records_reading;
    archive->format.destroy = xx_stk_vtable_destroy;
}

xx_stk *xx_stk_create(xx_io_device *device, int64_t base_address) {
    xx_stk *archive = (xx_stk *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_stk_init(archive, device, base_address);
    return archive;
}

void xx_stk_destroy(xx_stk *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_stk_free(xx_stk *archive) {
    if (!archive) return;
    xx_stk_destroy(archive);
    xx_mem_free(archive);
}

static void xx_stk_vtable_destroy(Abstractformat *self) {
    xx_stk_destroy((xx_stk *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_stk_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_stk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_stk_parse(self, pd);
    if (!stream) return false;
    xx_stk_stream_free(stream);
    return true;
}

bool xx_stk_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_stk *archive = (xx_stk *)self;
    xx_stk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_stk_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_stk_stream_free(stream);
    return true;
}

int64_t xx_stk_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_stk_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_stk *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_stk_set_record(xx_archive_record *record,
                                 const xx_stk_member *member) {
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

static bool xx_stk_copy_options(xx_list_s *target,
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

static const xx_var *xx_stk_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_stk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_stk_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_stk_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_stk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_stk_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_stk_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_stk_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_stk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_stk_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_stk_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_stk_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_stk_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_stk_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_stk_stream *stream;
    const xx_stk_member *member;
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
    stream = (xx_stk_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_stk_path_safe(member->name)) return false;

    path_option = xx_stk_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_stk_decode(self, member, &plain, &plain_size, pd);
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
        !xx_stk_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_stk_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
