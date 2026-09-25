/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZOO archives (Rahul Dhesi's zoo, 1.x and 2.x).
 *
 * The container is a linked list, not a sequence: the archive header names
 * the offset of the first directory entry, and every entry names the offset
 * of the next one and, separately, the offset of its own data. Entries and
 * data are therefore not required to be adjacent, and in practice zoo writes
 * all the data first and the whole directory chain last.
 *
 * Archive header, 34 bytes at the start of the format:
 *
 *   0x00  char[20]  free text, conventionally "ZOO 2.10 Archive.\x1a\0\0"
 *   0x14  u32 LE    magic 0xFDC4A7DC
 *   0x18  u32 LE    zoo_start: offset of the first directory entry
 *   0x1c  u32 LE    zoo_minus: the two's complement of zoo_start
 *   0x20  u8        major version needed to extract
 *   0x21  u8        minor version
 *
 * Version 2 writers append an optional tail (entry type, archive comment
 * offset and length, generation limit) between 0x22 and the first directory
 * entry; it is not needed to walk the archive and is skipped.
 *
 * Directory entry, fixed part, 51 bytes:
 *
 *   0x00  u32 LE    the same magic 0xFDC4A7DC
 *   0x04  u8        entry type: 1 = fixed part only, 2 = variable part follows
 *   0x05  u8        packing method: 0 stored, 1 lzd, 2 lzh
 *   0x06  u32 LE    offset of the next directory entry, 0 = end of chain
 *   0x0a  u32 LE    offset of this member's data
 *   0x0e  u16 LE    MS-DOS date
 *   0x10  u16 LE    MS-DOS time
 *   0x12  u16 LE    CRC-16 of the decoded member
 *   0x14  u32 LE    original (decoded) size
 *   0x18  u32 LE    current (stored) size
 *   0x1c  u8        major version needed to extract
 *   0x1d  u8        minor version
 *   0x1e  u8        deleted flag: 1 means the member is logically erased
 *   0x1f  u8        file structure byte
 *   0x20  u32 LE    offset of this member's comment
 *   0x24  u16 LE    comment length
 *   0x26  char[13]  short (8.3) name, NUL padded
 *
 * Variable part, present only when the entry type is 2:
 *
 *   0x33  u16 LE    var_dir_len: length of everything from 0x35 on
 *   0x35  u8        timezone, 0x7F = unspecified
 *   0x36  u16 LE    CRC-16 of the directory entry
 *   0x38  u8        long name length, NUL included
 *   0x39  u8        directory name length, NUL included
 *   0x3a  char[]    long name, then directory name, then a u16 system id and
 *                   a 3-byte file attribute word
 *
 * so an entry occupies 51 bytes at type 1 and 53 + var_dir_len at type 2.
 *
 * Methods this reader decodes: 0 (stored), 1 (lzd, Dhesi's variable-width
 * LZW) and 2 (lzh, the -lh5- block Huffman coder). Any other method byte is
 * listed with its raw value and refused at extraction.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zoo/xx_zoo.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/zoo/xx_zoo.h"

#include <stdio.h>

#define XX_ZOO_COPY_CHUNK (64 * 1024)

typedef struct xx_zoo_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_zoo_member;

typedef struct xx_zoo_stream_s {
    xx_zoo_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_zoo_stream;

static void xx_zoo_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_zoo_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_zoo_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_zoo_path_safe(const char *name) {
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

static void xx_zoo_stream_free(void *pointer) {
    xx_zoo_stream *stream = (xx_zoo_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_zoo_add(xx_zoo_stream *stream,
                          const xx_zoo_member *member) {
    xx_zoo_member *grown = (xx_zoo_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ZOO_MAGIC 0xFDC4A7DCu
#define XX_ZOO_HEADER_SIZE 34 /* text, magic, zoo_start, zoo_minus, two version bytes */
#define XX_ZOO_ENTRY_FIXED 51 /* through the 13-byte short name */
#define XX_ZOO_ENTRY_VAR_LEN 53 /* fixed part + the var_dir_len word, which is all a type-2 entry is guaranteed to have */
#define XX_ZOO_ENTRY_VAR_PREFIX 58 /* ... plus tz, dir CRC and the two name lengths, present only when var_dir_len >= 5 */
#define XX_ZOO_VAR_MIN 5 /* tz + dir CRC + the two name lengths: what var_dir_len must cover before any name bytes */
#define XX_ZOO_MAX_MEMBERS 100000 /* the chain is unbounded by the format: a runaway guard */
#define XX_ZOO_MAX_NAME 600 /* 255 directory + separator + 255 long name, with slack */
#define XX_ZOO_MAX_DECODED (256 * 1024 * 1024)
#define XX_ZOO_METHOD_STORE 0u
#define XX_ZOO_METHOD_LZD 1u /* variable-width LZW, 9..13 bits, LSB-first */
#define XX_ZOO_METHOD_LZH 2u /* the LHA -lh5- coder */

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_zoo_le16(const uint8_t *data);
static uint32_t xx_zoo_le32(const uint8_t *data);
static bool xx_zoo_name_byte_ok(uint8_t byte);
static bool xx_zoo_append_name(char *name, size_t *used, const uint8_t *source, int32_t limit);
static xx_zoo_stream *xx_zoo_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_zoo_decode(Abstractformat *self, const xx_zoo_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);



static uint16_t xx_zoo_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_zoo_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* ZOO names are DOS or Unix file names written as plain ASCII; nothing in the
 * container declares a code page, and no writer emits high bytes. A byte
 * outside the printable range is therefore a walk that has wandered into
 * payload rather than an exotic name. */
static bool xx_zoo_name_byte_ok(uint8_t byte) {
    return byte >= 0x20U && byte <= 0x7EU;
}

/* Append a NUL-terminated run of at most @p limit bytes, rewriting DOS
 * separators. Returns false on a bad byte or on overflow. */
static bool xx_zoo_append_name(char *name, size_t *used, const uint8_t *source,
                               int32_t limit) {
    int32_t index;

    for (index = 0; index < limit; ++index) {
        uint8_t byte = source[index];

        /* Both name fields are stored with their terminating NUL counted in
         * the length, so the NUL ends the string rather than being copied. */
        if (byte == 0U) break;
        if (byte == (uint8_t)'\\') byte = (uint8_t)'/';
        if (!xx_zoo_name_byte_ok(byte)) return false;
        if (*used >= (size_t)(XX_ZOO_MAX_NAME - 2)) return false;
        name[(*used)++] = (char)byte;
    }
    return true;
}

static xx_zoo_stream *xx_zoo_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_zoo_stream *stream = NULL;
    uint8_t header[XX_ZOO_HEADER_SIZE];
    uint8_t entry[XX_ZOO_ENTRY_VAR_PREFIX];
    uint8_t names[512];
    char name[XX_ZOO_MAX_NAME];
    int64_t total;
    int64_t span;
    int64_t position;
    uint32_t zoo_start;
    uint32_t zoo_minus;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Archive header plus one directory entry is the smallest possible ZOO. */
    if (span < (XX_ZOO_HEADER_SIZE + XX_ZOO_ENTRY_FIXED)) return NULL;

    if (!xx_zoo_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_zoo_le32(header + 20) != XX_ZOO_MAGIC) return NULL;
    zoo_start = xx_zoo_le32(header + 24);
    zoo_minus = xx_zoo_le32(header + 28);
    /* zoo's own damaged-archive check: the two fields are a value and its
     * two's complement, so their 32-bit sum is zero. Together with the magic
     * this is what keeps an unrelated file that happens to carry four
     * matching bytes at 0x14 from being walked as a directory chain, and it
     * costs nothing on a real archive because every zoo writer emits it. */
    /* Both halves are held unsigned on purpose: zoo_minus is stored as the
     * two's complement (0x2A becomes 0xFFFFFFD6), and unsigned addition wraps
     * to zero by definition, where signed overflow would not be defined. */
    if ((uint32_t)(zoo_start + zoo_minus) != 0U) return NULL;
    if (zoo_start < (uint32_t)XX_ZOO_HEADER_SIZE) return NULL;
    if (!xx_zoo_range_within(span, (int64_t)zoo_start, XX_ZOO_ENTRY_FIXED)) {
        return NULL;
    }

    stream = (xx_zoo_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    position = (int64_t)zoo_start;
    for (;;) {
        xx_zoo_member member;
        int64_t next_position;
        int64_t data_offset;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t entry_size;
        int32_t var_length = 0;
        int32_t long_name_length = 0;
        int32_t dir_name_length = 0;
        uint32_t method;
        uint8_t type;
        uint8_t deleted;
        size_t used = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_ZOO_MAX_MEMBERS) goto fail;
        if (!xx_zoo_range_within(span, position, XX_ZOO_ENTRY_FIXED)) goto fail;
        if (!xx_zoo_read_at(self, self->base_address + position, entry,
                            XX_ZOO_ENTRY_FIXED)) {
            goto fail;
        }
        /* Every directory entry repeats the archive magic. This is the check
         * that makes the chain self-validating: a "next" pointer that lands
         * anywhere but on a real entry stops the walk instead of letting
         * arbitrary bytes become a member. */
        if (xx_zoo_le32(entry) != XX_ZOO_MAGIC) goto fail;

        type = entry[4];
        /* 1 = fixed part only, 2 = a variable part follows. Nothing else has
         * ever been written, and the byte decides how long the entry is, so
         * guessing at an unknown value would mis-place the next entry. */
        if (type != 1U && type != 2U) goto fail;

        method = (uint32_t)entry[5];
        next_position = (int64_t)xx_zoo_le32(entry + 6);
        data_offset = (int64_t)xx_zoo_le32(entry + 10);
        uncompressed_size = (int64_t)xx_zoo_le32(entry + 20);
        compressed_size = (int64_t)xx_zoo_le32(entry + 24);
        deleted = entry[30];

        entry_size = XX_ZOO_ENTRY_FIXED;
        if (type == 2U) {
            /* Only the var_dir_len word may be read on spec: it is what says
             * how long the entry is. The terminator entry every zoo writer
             * puts at the end of the chain has var_dir_len 0, so it is 53
             * bytes and the file ends a few bytes later -- reading the full
             * 58-byte prefix here would run past EOF on every real archive. */
            if (!xx_zoo_range_within(span, position, XX_ZOO_ENTRY_VAR_LEN)) {
                goto fail;
            }
            if (!xx_zoo_read_at(self,
                                self->base_address + position +
                                    XX_ZOO_ENTRY_FIXED,
                                entry + XX_ZOO_ENTRY_FIXED,
                                XX_ZOO_ENTRY_VAR_LEN - XX_ZOO_ENTRY_FIXED)) {
                goto fail;
            }
            /* var_dir_len counts from the timezone byte at 0x35 onward, so
             * the entry runs to 53 + var_dir_len. */
            var_length = (int32_t)xx_zoo_le16(entry + 51);
            entry_size = XX_ZOO_ENTRY_VAR_LEN + (int64_t)var_length;
            if (!xx_zoo_range_within(span, position, entry_size)) goto fail;
            if (var_length >= XX_ZOO_VAR_MIN) {
                /* var_dir_len >= 5 means the entry is at least 58 bytes, so
                 * the two name-length bytes are inside the extent just
                 * checked and can be read now. */
                if (!xx_zoo_read_at(self,
                                    self->base_address + position +
                                        XX_ZOO_ENTRY_VAR_LEN,
                                    entry + XX_ZOO_ENTRY_VAR_LEN,
                                    XX_ZOO_ENTRY_VAR_PREFIX -
                                        XX_ZOO_ENTRY_VAR_LEN)) {
                    goto fail;
                }
                long_name_length = (int32_t)entry[56];
                dir_name_length = (int32_t)entry[57];
                /* The two name runs live inside the variable part, after the
                 * five bytes of fixed variable-part fields. */
                if ((XX_ZOO_VAR_MIN + long_name_length + dir_name_length) >
                    var_length) {
                    goto fail;
                }
            }
        }

        name[0] = '\0';
        if ((long_name_length + dir_name_length) > 0) {
            if (!xx_zoo_read_at(self,
                                self->base_address + position +
                                    XX_ZOO_ENTRY_VAR_PREFIX,
                                names,
                                (size_t)(long_name_length + dir_name_length))) {
                goto fail;
            }
            /* The directory name is a path prefix for the long name; zoo
             * stores the two separately and neither carries a separator. */
            if (dir_name_length > 0) {
                if (!xx_zoo_append_name(name, &used,
                                        names + long_name_length,
                                        dir_name_length)) {
                    goto fail;
                }
                if (used > 0U && name[used - 1U] != '/') {
                    if (used >= (size_t)(XX_ZOO_MAX_NAME - 2)) goto fail;
                    name[used++] = '/';
                }
            }
            if (long_name_length > 0) {
                if (!xx_zoo_append_name(name, &used, names,
                                        long_name_length)) {
                    goto fail;
                }
            }
        }
        /* A version-1 entry, or a version-2 entry whose long name is empty,
         * falls back to the 8.3 name in the fixed part. */
        if (used == 0U || name[used - 1U] == '/') {
            if (!xx_zoo_append_name(name, &used, entry + 38, 13)) goto fail;
        }
        name[used] = '\0';

        if ((deleted != 1U) && (used != 0U) && (data_offset > 0) &&
            (uncompressed_size >= 0) && (compressed_size >= 0)) {
            /* A member whose data runs past EOF is a rejection: the entry is
             * not describing bytes this file contains. */
            if (!xx_zoo_range_within(span, data_offset, compressed_size)) {
                goto fail;
            }
            xx_mem_zero(&member, sizeof(member));
            member.name = xx_str_dup(name);
            if (!member.name) goto fail;
            member.header_offset = self->base_address + position;
            member.header_size = entry_size;
            member.data_offset = self->base_address + data_offset;
            member.compressed_size = compressed_size;
            member.uncompressed_size = uncompressed_size;
            member.method = method;
            /* Stored raw as an MS-DOS date|time pair: ZOO carries no other
             * clock, and the timezone byte in the variable part says only
             * how to interpret it, not what it is. */
            member.timestamp =
                ((uint64_t)xx_zoo_le16(entry + 14) << 16) |
                (uint64_t)xx_zoo_le16(entry + 16);
            member.is_folder = false;
            if (!xx_zoo_add(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
        }

        if (next_position == 0) break; /* end of chain */
        /* The chain must move forward. A "next" that points at or before the
         * current entry is the only way a corrupt archive can loop, and the
         * member cap alone would turn that into 100000 wasted reads. */
        if (next_position <= position) goto fail;
        position = next_position;
    }

    /* A chain that produced nothing is not a ZOO archive worth reporting;
     * an archive with every member deleted is indistinguishable from a false
     * positive that walked one terminator entry. */
    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    /* ZOO stores no archive length anywhere, the directory chain normally
     * sits after the data, and zoo appends a short trailer behind the final
     * entry. The format therefore spans the whole file. */
    stream->archive_size = span;
    return stream;

fail:
    xx_zoo_stream_free(stream);
    return NULL;
}


static bool xx_zoo_decode(Abstractformat *self, const xx_zoo_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t plain_size;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    /* The stored original size is attacker-controlled, so it is capped before
     * it becomes an allocation. */
    if (member->uncompressed_size > XX_ZOO_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;

    /* ZOO defines exactly three methods and this reader implements all three.
     * A fourth value has never been written by any zoo release, but treating
     * one as stored would hand back a bitstream dressed as file data, which
     * nothing downstream can tell from the real thing. */
    if (member->method != XX_ZOO_METHOD_STORE &&
        member->method != XX_ZOO_METHOD_LZD &&
        member->method != XX_ZOO_METHOD_LZH) {
        return false;
    }
    /* A stored member states the same number twice; a disagreement means the
     * entry is not describing the bytes it points at. */
    if (member->method == XX_ZOO_METHOD_STORE &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }
    /* Both coders emit at least one code, so a coded member can be neither
     * empty nor sourced from an empty stream. */
    if (member->method != XX_ZOO_METHOD_STORE &&
        (member->compressed_size == 0 || member->uncompressed_size == 0)) {
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    if (member->compressed_size > 0) {
        packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
        if (!packed) return false;
        if (!xx_zoo_read_at(self, member->data_offset, packed,
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
     * failure, so a genuinely empty stored member still gets one byte. */
    plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : (size_t)1);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_ZOO_METHOD_STORE) {
        size_t index;

        for (index = 0U; index < plain_size; ++index) plain[index] = packed[index];
        written = plain_size;
    } else if (member->method == XX_ZOO_METHOD_LZD) {
        if (!xx_zoo_lzd_decode_memory(packed, (size_t)member->compressed_size,
                                      plain, plain_size, &written)) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
    } else if (!xx_zoo_lzh_decode_memory(packed,
                                         (size_t)member->compressed_size,
                                         plain, plain_size, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* Returning true with fewer bytes than the entry promised is the one
     * failure a caller cannot detect. lzd and lzh differ only in the method
     * byte and both will consume a prefix of the wrong stream before giving
     * up, so the length is the last line of defence. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_zoo_init(xx_zoo *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZOO;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zoo");
    xx_format_set_extension(&archive->format, "zoo");
    archive->format.check_is_valid = xx_zoo_check_is_valid;
    archive->format.handle_base_info = xx_zoo_handle_base_info;
    archive->format.get_format_size = xx_zoo_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zoo_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zoo_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zoo_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zoo_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zoo_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zoo_free_archive_records_reading;
    archive->format.destroy = xx_zoo_vtable_destroy;
}

xx_zoo *xx_zoo_create(xx_io_device *device, int64_t base_address) {
    xx_zoo *archive = (xx_zoo *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zoo_init(archive, device, base_address);
    return archive;
}

void xx_zoo_destroy(xx_zoo *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_zoo_free(xx_zoo *archive) {
    if (!archive) return;
    xx_zoo_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zoo_vtable_destroy(Abstractformat *self) {
    xx_zoo_destroy((xx_zoo *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_zoo_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_zoo_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_zoo_parse(self, pd);
    if (!stream) return false;
    xx_zoo_stream_free(stream);
    return true;
}

bool xx_zoo_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zoo *archive = (xx_zoo *)self;
    xx_zoo_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_zoo_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_zoo_stream_free(stream);
    return true;
}

int64_t xx_zoo_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zoo_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zoo *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zoo_set_record(xx_archive_record *record,
                                 const xx_zoo_member *member) {
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

static bool xx_zoo_copy_options(xx_list_s *target,
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

static const xx_var *xx_zoo_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_zoo_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_zoo_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_zoo_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_zoo_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_zoo_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_zoo_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_zoo_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zoo_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zoo_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_zoo_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zoo_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_zoo_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_zoo_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_zoo_stream *stream;
    const xx_zoo_member *member;
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
    stream = (xx_zoo_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_zoo_path_safe(member->name)) return false;

    path_option = xx_zoo_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_zoo_decode(self, member, &plain, &plain_size, pd);
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
        !xx_zoo_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_zoo_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
