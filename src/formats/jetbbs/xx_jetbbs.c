/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JetBBS archives (*.dat).
 *
 * JetBBS is LHA with a renamed method tag: the container, the header levels,
 * the header checksum and the bitstreams are all ordinary LHA, but where LHA
 * writes "-lh5-" JetBBS writes "-mg5-". Only three tags exist:
 *
 *   -mg0-   stored
 *   -mg4-   the -lh4- bitstream (12-bit dictionary)
 *   -mg5-   the -lh5- bitstream (13-bit dictionary)
 *
 * Members are laid end to end from offset 0. A member is a base header, any
 * level-1 extended headers, then the payload:
 *
 *   0x00  u8       base header size, NOT counting these first two bytes, so
 *                  the base header spans [0, size + 2)
 *   0x01  u8       header checksum: the low byte of the sum of every byte in
 *                  [0x02, size + 2)
 *   0x02  char[5]  method tag, "-mg" + one of '0'/'4'/'5' + '-'
 *   0x07  u32 LE   compressed size. At level 1 this ALSO covers the extended
 *                  headers, so the true payload length is this minus their
 *                  total size.
 *   0x0b  u32 LE   uncompressed size
 *   0x0f  u32 LE   MS-DOS packed time|date
 *   0x13  u8       MS-DOS attribute byte
 *   0x14  u8       header level, 0 or 1
 *   0x15  u8       file name length
 *   0x16  char[]   file name, that many bytes
 *   ..    u16 LE   CRC16 of the uncompressed payload
 *   level 1 only:
 *   ..    u8       OS identifier
 *   ..    u16 LE   size of the first extended header; it is the final word of
 *                  the base header, and each extended header ends with the
 *                  size of the next one, a zero word terminating the chain.
 *
 * An extended header is  u16 LE size | u8 type | data | u16 LE next size.
 * Type 0 carries a CRC16 of the whole header (computed with those two bytes
 * zeroed), type 1 replaces the file name, type 2 carries the directory (0xFF
 * separators, rewritten to '/'), type 0x42 restates the two sizes as 64-bit
 * values that must agree with the 32-bit fields.
 *
 * Levels 2 and 3 are refused: they widen the header-size field over the
 * checksum byte, which is precisely the check that makes a three-character
 * tag safe to detect on. No level 2/3 JetBBS archive exists.
 *
 * The archive ends at EOF, or at a zero byte where the next header size
 * would be - LHA's end-of-archive marker.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jetbbs/xx_jetbbs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzh/xx_lzh.h"

#include <stdio.h>

#define XX_JETBBS_COPY_CHUNK (64 * 1024)

typedef struct xx_jetbbs_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_jetbbs_member;

typedef struct xx_jetbbs_stream_s {
    xx_jetbbs_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_jetbbs_stream;

static void xx_jetbbs_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_jetbbs_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_jetbbs_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_jetbbs_path_safe(const char *name) {
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

static void xx_jetbbs_stream_free(void *pointer) {
    xx_jetbbs_stream *stream = (xx_jetbbs_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_jetbbs_add(xx_jetbbs_stream *stream,
                          const xx_jetbbs_member *member) {
    xx_jetbbs_member *grown = (xx_jetbbs_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_JETBBS_MAX_MEMBERS 100000
#define XX_JETBBS_MIN_PREFIX 22
#define XX_JETBBS_MAX_HEADER 65536
#define XX_JETBBS_MAX_NAME 1024
#define XX_JETBBS_MAX_DECODED (256 * 1024 * 1024)
#define XX_JETBBS_METHOD_STORE 0U
#define XX_JETBBS_METHOD_LZH4 4U
#define XX_JETBBS_METHOD_LZH5 5U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_jetbbs_le16(const uint8_t *data);
static uint32_t xx_jetbbs_le32(const uint8_t *data);
static uint16_t xx_jetbbs_crc16(const uint8_t *data, size_t size, size_t skip_offset);
static bool xx_jetbbs_checksum_ok(const uint8_t *header, int32_t base_size);
static bool xx_jetbbs_name_byte_ok(uint8_t byte);
static xx_jetbbs_stream *xx_jetbbs_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_jetbbs_decode(Abstractformat *self, const xx_jetbbs_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* 22 bytes gets us through the name-length byte of a level 0/1 header. */
/* Base header plus every extended header of one member. The reference caps
 * this at 1 MiB; 64 KiB is already far past anything a JetBBS writer emits
 * and keeps the per-parse scratch buffer small. */

static uint16_t xx_jetbbs_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_jetbbs_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* CRC-16/ARC, the polynomial LHA uses for its common extended header. */
static uint16_t xx_jetbbs_crc16(const uint8_t *data, size_t size,
                                size_t skip_offset) {
    uint16_t crc = 0U;
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t byte = data[index];
        int bit;

        /* The stored CRC counts as zero in its own computation. */
        if (index == skip_offset || index == skip_offset + 1U) byte = 0U;
        crc = (uint16_t)(crc ^ byte);
        for (bit = 0; bit < 8; ++bit) {
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1U) ? 0xA001U : 0U));
        }
    }
    return crc;
}

/* The header checksum is the whole of the format's structural self-check:
 * an 8-bit sum over the base header, stored in byte 1. Together with the
 * tag it is what stops arbitrary data from being read as JetBBS, so it must
 * never become advisory. */
static bool xx_jetbbs_checksum_ok(const uint8_t *header, int32_t base_size) {
    uint32_t sum = 0U;
    int32_t index;

    for (index = 2; index < base_size; ++index) {
        sum += header[index];
    }
    return ((sum & 0xFFU) == (uint32_t)header[1]);
}

/* JetBBS names are MS-DOS/ASCII; nothing in the format carries a code page,
 * so a byte outside 0x20..0x7E means this is not a member header. */
static bool xx_jetbbs_name_byte_ok(uint8_t byte) {
    return (byte >= 0x20U) && (byte <= 0x7EU);
}

static xx_jetbbs_stream *xx_jetbbs_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_jetbbs_stream *stream = NULL;
    uint8_t *header = NULL;
    uint8_t prefix[XX_JETBBS_MIN_PREFIX];
    char name[XX_JETBBS_MAX_NAME];
    int64_t total;
    int64_t span;
    int64_t offset = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Smallest archive: a level 0 base header (24 bytes) plus the
     * end-of-archive marker byte. */
    if (span < 25) return NULL;

    stream = (xx_jetbbs_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    header = (uint8_t *)xx_mem_alloc((size_t)XX_JETBBS_MAX_HEADER);
    if (!header) goto fail;

    while (offset < span) {
        xx_jetbbs_member member;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t ext_total = 0;
        int32_t base_size;
        int32_t header_total;
        int32_t name_length;
        int32_t name_pos;
        int32_t name_size;
        int32_t dir_pos = -1;
        int32_t dir_size = 0;
        int32_t crc_pos = -1;
        int32_t min_base;
        uint16_t common_crc = 0U;
        uint32_t method;
        uint8_t level;
        size_t out = 0U;
        int32_t index;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_JETBBS_MAX_MEMBERS) goto fail;
        /* Fewer bytes left than a header needs is the ordinary end of the
         * chain, not a malformed archive: writers pad. */
        if ((span - offset) < XX_JETBBS_MIN_PREFIX) break;
        if (!xx_jetbbs_read_at(self, self->base_address + offset, prefix,
                               (size_t)XX_JETBBS_MIN_PREFIX)) {
            goto fail;
        }
        /* A zero header-size byte is LHA's end-of-archive marker. */
        if (prefix[0] == 0U) break;

        /* The tag. Three fixed characters and a digit restricted to the
         * three methods that exist; anything else is not JetBBS. */
        if (prefix[2] != '-' || prefix[3] != 'm' || prefix[4] != 'g' ||
            prefix[6] != '-') {
            goto fail;
        }
        if (prefix[5] != '0' && prefix[5] != '4' && prefix[5] != '5') {
            goto fail;
        }
        /* Carried raw, so a listing shows what the archive itself says. */
        method = (uint32_t)(prefix[5] - '0');

        level = prefix[20];
        /* Levels 2 and 3 turn byte 1 into the high half of a 16-bit header
         * size, so the checksum test below would be meaningless there. With
         * only a three-character tag left, that test is the detection, so
         * those levels are refused rather than guessed at. */
        if (level > 1U) goto fail;
        min_base = (level == 0U) ? 24 : 27;
        base_size = (int32_t)prefix[0] + 2;
        if (base_size < min_base) goto fail;
        if ((int64_t)base_size > (span - offset)) goto fail;
        if (!xx_jetbbs_read_at(self, self->base_address + offset, header,
                               (size_t)base_size)) {
            goto fail;
        }
        if (!xx_jetbbs_checksum_ok(header, base_size)) goto fail;

        name_length = (int32_t)header[21];
        /* The name, the CRC16 after it and (at level 1) the OS byte and the
         * first extended-header size all have to fit inside the base
         * header the size byte declared. */
        if ((min_base + name_length) > base_size) goto fail;
        name_pos = 22;
        name_size = name_length;

        compressed_size = (int64_t)xx_jetbbs_le32(header + 7);
        uncompressed_size = (int64_t)xx_jetbbs_le32(header + 11);
        header_total = base_size;

        if (level == 1U) {
            /* Level 1's compressed size covers the extended headers too, so
             * they are read first and subtracted; what remains is the
             * payload. */
            for (;;) {
                int32_t next_size;

                if (pd && xx_pd_is_stopped(pd)) goto fail;
                next_size = (int32_t)xx_jetbbs_le16(header + header_total - 2);
                if (next_size == 0) break;
                /* size word + type byte + next-size word is the minimum. */
                if (next_size < 3) goto fail;
                if (next_size > (XX_JETBBS_MAX_HEADER - header_total)) {
                    goto fail;
                }
                if ((int64_t)next_size > (compressed_size - ext_total)) {
                    goto fail;
                }
                if ((int64_t)next_size >
                    (span - offset - (int64_t)header_total)) {
                    goto fail;
                }
                if (!xx_jetbbs_read_at(
                        self,
                        self->base_address + offset + (int64_t)header_total,
                        header + header_total, (size_t)next_size)) {
                    goto fail;
                }
                header_total += next_size;
                ext_total += next_size;
            }
            compressed_size -= ext_total;

            /* Walk the extended headers now that they are all in memory.
             * The chain starts at the last word of the base header. */
            index = base_size - 2;
            for (;;) {
                int32_t ext_size;
                int32_t data_pos;
                int32_t data_size;
                uint8_t type;

                if (pd && xx_pd_is_stopped(pd)) goto fail;
                if (index > (header_total - 2)) goto fail;
                ext_size = (int32_t)xx_jetbbs_le16(header + index);
                if (ext_size == 0) break;
                if (ext_size < 3) goto fail;
                if (ext_size > (header_total - index - 2)) goto fail;
                type = header[index + 2];
                data_pos = index + 3;
                data_size = ext_size - 3;
                if (type == 0x00U) {
                    /* Common header: a CRC16 over the whole header. Two of
                     * them would make "which one" undefined. */
                    if (data_size < 2 || crc_pos >= 0) goto fail;
                    crc_pos = data_pos;
                    common_crc = xx_jetbbs_le16(header + data_pos);
                } else if (type == 0x01U) {
                    name_pos = data_pos;
                    name_size = data_size;
                } else if (type == 0x02U) {
                    dir_pos = data_pos;
                    dir_size = data_size;
                } else if (type == 0x42U) {
                    /* 64-bit sizes. The member fields here are filled from
                     * the 32-bit ones, so a value that does not agree (or
                     * that needs the upper word) would make the extent this
                     * reader publishes a lie. */
                    if (data_size < 16) goto fail;
                    if (xx_jetbbs_le32(header + data_pos + 4) != 0U) goto fail;
                    if (xx_jetbbs_le32(header + data_pos + 12) != 0U) goto fail;
                    if ((int64_t)xx_jetbbs_le32(header + data_pos) !=
                        compressed_size) {
                        goto fail;
                    }
                    if ((int64_t)xx_jetbbs_le32(header + data_pos + 8) !=
                        uncompressed_size) {
                        goto fail;
                    }
                }
                index += ext_size;
            }
            if (crc_pos >= 0) {
                if (xx_jetbbs_crc16(header, (size_t)header_total,
                                    (size_t)crc_pos) != common_crc) {
                    goto fail;
                }
            }
        }

        if (compressed_size < 0 || uncompressed_size < 0) goto fail;
        /* A stored member states the same number twice; a disagreement
         * means the header is not describing what follows it. */
        if (method == 0U && compressed_size != uncompressed_size) goto fail;
        if (!xx_jetbbs_range_within(span, offset + (int64_t)header_total,
                                    compressed_size)) {
            goto fail;
        }

        /* Assemble directory + name. The directory uses 0xFF as its
         * separator and DOS names use '\', both of which become '/'. */
        if (dir_size > 0) {
            for (index = 0; index < dir_size; ++index) {
                uint8_t byte = header[dir_pos + index];

                if (out >= (size_t)(XX_JETBBS_MAX_NAME - 2)) goto fail;
                if (byte == 0xFFU || byte == (uint8_t)'\\') byte = (uint8_t)'/';
                if (!xx_jetbbs_name_byte_ok(byte)) goto fail;
                name[out++] = (char)byte;
            }
            if (out > 0U && name[out - 1U] != '/') {
                if (out >= (size_t)(XX_JETBBS_MAX_NAME - 2)) goto fail;
                name[out++] = '/';
            }
        }
        for (index = 0; index < name_size; ++index) {
            uint8_t byte = header[name_pos + index];

            /* MorphOS writers append a comment after a NUL inside the name
             * field; the field's length still covers it, only the name
             * ends here. */
            if (byte == 0U) break;
            if (out >= (size_t)(XX_JETBBS_MAX_NAME - 1)) goto fail;
            if (byte == (uint8_t)'\\') byte = (uint8_t)'/';
            if (!xx_jetbbs_name_byte_ok(byte)) goto fail;
            name[out++] = (char)byte;
        }
        name[out] = '\0';
        /* JetBBS has no directory method (-lhd- has no "-mg" spelling), so
         * every member is a file and must be named. */
        if (out == 0U) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = header_total;
        member.data_offset = self->base_address + offset + (int64_t)header_total;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = method;
        /* Raw MS-DOS time|date word pair, as stored. */
        member.timestamp = (uint64_t)xx_jetbbs_le32(header + 15);
        member.is_folder = false;
        if (!xx_jetbbs_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset += (int64_t)header_total + compressed_size;
    }

    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    xx_mem_free(header);
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(header);
    xx_jetbbs_stream_free(stream);
    return NULL;
}


/* The stated uncompressed size is attacker-controlled, so it is capped
 * before it becomes an allocation. */

/* member.method holds the container's own method digit, i.e. the character
 * between "-mg" and '-' converted to a number: 0, 4 or 5. 4 and 5 are the
 * -lh4-/-lh5- bitstreams and are passed straight through as xx_lzh5's
 * method argument; nothing else may reach a decoder. */

static bool xx_jetbbs_decode(Abstractformat *self,
                             const xx_jetbbs_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t plain_size;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > XX_JETBBS_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;
    /* A method the container defines but this reader does not implement has
     * to fail here. Falling through to the stored path would hand back an
     * LZH bitstream dressed up as file data, which nothing downstream can
     * tell from the real thing. */
    if (member->method != XX_JETBBS_METHOD_STORE &&
        member->method != XX_JETBBS_METHOD_LZH4 &&
        member->method != XX_JETBBS_METHOD_LZH5) {
        return false;
    }
    /* An empty payload only makes sense stored: the LZH bitstreams always
     * emit at least one code, so a zero-length -mg4-/-mg5- member is
     * malformed rather than empty. */
    if (member->uncompressed_size == 0 &&
        member->method != XX_JETBBS_METHOD_STORE) {
        return false;
    }
    if (member->method == XX_JETBBS_METHOD_STORE &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    if (member->compressed_size > 0) {
        packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
        if (!packed) return false;
        if (!xx_jetbbs_read_at(self, member->data_offset, packed,
                               (size_t)member->compressed_size)) {
            xx_mem_free(packed);
            return false;
        }
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    /* xx_mem_alloc(0) returns NULL, which the caller cannot distinguish from
     * failure, so a genuinely empty member still gets one byte. */
    plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : (size_t)1);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_JETBBS_METHOD_STORE) {
        size_t index;

        for (index = 0U; index < plain_size; ++index) {
            plain[index] = packed[index];
        }
        written = plain_size;
    } else if (!xx_lzh5_decode_memory(packed, (size_t)member->compressed_size,
                                      plain, plain_size,
                                      (int)member->method, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* Returning true with fewer bytes than the header promised is the one
     * failure a caller cannot detect, so the decoded length must match the
     * container exactly. -mg4- and -mg5- differ only in dictionary width and
     * a wrong guess decodes to plausible garbage of the right length, which
     * is why the method comes from the tag and never from a heuristic. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_jetbbs_init(xx_jetbbs *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_JETBBS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzh-compressed");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_jetbbs_check_is_valid;
    archive->format.handle_base_info = xx_jetbbs_handle_base_info;
    archive->format.get_format_size = xx_jetbbs_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_jetbbs_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_jetbbs_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_jetbbs_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_jetbbs_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_jetbbs_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_jetbbs_free_archive_records_reading;
    archive->format.destroy = xx_jetbbs_vtable_destroy;
}

xx_jetbbs *xx_jetbbs_create(xx_io_device *device, int64_t base_address) {
    xx_jetbbs *archive = (xx_jetbbs *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_jetbbs_init(archive, device, base_address);
    return archive;
}

void xx_jetbbs_destroy(xx_jetbbs *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_jetbbs_free(xx_jetbbs *archive) {
    if (!archive) return;
    xx_jetbbs_destroy(archive);
    xx_mem_free(archive);
}

static void xx_jetbbs_vtable_destroy(Abstractformat *self) {
    xx_jetbbs_destroy((xx_jetbbs *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_jetbbs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_jetbbs_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_jetbbs_parse(self, pd);
    if (!stream) return false;
    xx_jetbbs_stream_free(stream);
    return true;
}

bool xx_jetbbs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_jetbbs *archive = (xx_jetbbs *)self;
    xx_jetbbs_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_jetbbs_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_jetbbs_stream_free(stream);
    return true;
}

int64_t xx_jetbbs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_jetbbs_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_jetbbs *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_jetbbs_set_record(xx_archive_record *record,
                                 const xx_jetbbs_member *member) {
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

static bool xx_jetbbs_copy_options(xx_list_s *target,
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

static const xx_var *xx_jetbbs_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_jetbbs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_jetbbs_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_jetbbs_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_jetbbs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_jetbbs_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_jetbbs_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_jetbbs_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_jetbbs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_jetbbs_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_jetbbs_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_jetbbs_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_jetbbs_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_jetbbs_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_jetbbs_stream *stream;
    const xx_jetbbs_member *member;
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
    stream = (xx_jetbbs_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_jetbbs_path_safe(member->name)) return false;

    path_option = xx_jetbbs_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_jetbbs_decode(self, member, &plain, &plain_size, pd);
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
        !xx_jetbbs_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_jetbbs_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
