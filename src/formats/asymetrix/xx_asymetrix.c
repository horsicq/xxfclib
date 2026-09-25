/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Asymetrix ToolBook Setup disk-set archives (.001, .002, ... - one file
 * per floppy volume, Windows 3.1 era).
 *
 *   header, 0x2C bytes, little-endian:
 *     0x00  u32  magic 0x63132260 ("60 22 13 63")
 *     0x04  u32  directory record stride, always 0x6C
 *     0x08  9    set name, NUL terminated inside its fixed field
 *     0x28  u16  volume number of THIS file, 1-based
 *     0x2A  u16  member count of the WHOLE SET, repeated on every volume
 *
 *   directory (volume 1 only), count * 0x6C bytes at 0x2C.  Every field sits
 *   at a fixed record offset; the bytes between the name's NUL and the next
 *   field are uninitialised writer memory and frequently look like a
 *   plausible DOS date/time pair, so nothing may be read relative to the
 *   name's length:
 *     +0x00  13   file name, NUL terminated inside its fixed field
 *     +0x1E  u32  high word = volume the member's data begins on
 *     +0x22  u32  offset of the member's data inside that volume
 *     +0x26  u32  uncompressed size
 *     +0x62  u16  DOS date  (the date comes BEFORE the time here)
 *     +0x64  u16  DOS time
 *     +0x66  u16  DOS attributes
 *     +0x68  u32  CRC-32 of the plaintext
 *
 *   member data, from the end of the directory to end of file: a chain of
 *   blocks, each
 *     u16  method: 0 = stored, 1 = PKWARE DCL "implode"
 *     u32  packed length
 *     ...  that many packed bytes; a method-1 block is a COMPLETE DCL stream
 *          with its own 00 05 prelude, fresh dictionary and end code
 *   A block's unpacked length is never stored: it is implied to be
 *   min(4096, bytes of the member still outstanding), which is why the
 *   member's declared size is mandatory to walk the chain at all.
 *
 * Only volume 1 carries the directory; later volumes are pure continuation
 * data behind an otherwise identical header.  A single device cannot resolve
 * a set, so this reader accepts volume 1 and publishes only those members
 * whose entire block chain lives inside the file it was handed - a member
 * that begins on a later volume, or that runs off the end of this one, is
 * left out rather than written out truncated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/asymetrix/xx_asymetrix.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/asymetrix/xx_asymetrix.h"

#include <stdio.h>

#define XX_ASYMETRIX_COPY_CHUNK (64 * 1024)

typedef struct xx_asymetrix_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_asymetrix_member;

typedef struct xx_asymetrix_stream_s {
    xx_asymetrix_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_asymetrix_stream;

static void xx_asymetrix_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_asymetrix_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_asymetrix_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_asymetrix_path_safe(const char *name) {
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

static void xx_asymetrix_stream_free(void *pointer) {
    xx_asymetrix_stream *stream = (xx_asymetrix_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_asymetrix_add(xx_asymetrix_stream *stream,
                          const xx_asymetrix_member *member) {
    xx_asymetrix_member *grown = (xx_asymetrix_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ASYMETRIX_HEADER_SIZE 0x2c
#define XX_ASYMETRIX_MAGIC 0x63132260U
#define XX_ASYMETRIX_RECORD_STRIDE 0x6c
#define XX_ASYMETRIX_SETNAME_OFFSET 0x08
#define XX_ASYMETRIX_SETNAME_FIELD 9
#define XX_ASYMETRIX_VOLUME_OFFSET 0x28
#define XX_ASYMETRIX_COUNT_OFFSET 0x2a
#define XX_ASYMETRIX_RECORD_NAME_FIELD 13
#define XX_ASYMETRIX_RECORD_VOLUME 0x1e
#define XX_ASYMETRIX_RECORD_OFFSET 0x22
#define XX_ASYMETRIX_RECORD_SIZE 0x26
#define XX_ASYMETRIX_RECORD_DATE 0x62
#define XX_ASYMETRIX_RECORD_TIME 0x64
#define XX_ASYMETRIX_BLOCK_HEADER_SIZE 6
#define XX_ASYMETRIX_BLOCK_UNPACKED 4096
#define XX_ASYMETRIX_MAX_BLOCK_SIZE 0x10000
#define XX_ASYMETRIX_MAX_MEMBERS 0x4000
#define XX_ASYMETRIX_MAX_VOLUME 999
#define XX_ASYMETRIX_BLOCK_METHOD_STORED 0U
#define XX_ASYMETRIX_BLOCK_METHOD_IMPLODE 1U
#define XX_ASYMETRIX_METHOD_BLOCKS 1U
#define XX_ASYMETRIX_MAX_DECODED (256 * 1024 * 1024)
#define XX_ASYMETRIX_NAME_BUFFER 16

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_asymetrix_le16(const uint8_t *data);
static uint32_t xx_asymetrix_le32(const uint8_t *data);
static bool xx_asymetrix_name_character(uint8_t value);
static bool xx_asymetrix_read_name(const uint8_t *field, size_t field_size, char *out);
static bool xx_asymetrix_measure(Abstractformat *self, int64_t data_offset, int64_t region_end, int64_t uncompressed, int64_t *stream_size, xx_pd_struct *pd);
static xx_asymetrix_stream *xx_asymetrix_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_asymetrix_decode(Abstractformat *self, const xx_asymetrix_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* 60 22 13 63 followed by 6C 00 00 00.  The second dword is the directory
 * record stride and is a hard constant of the writer, so the gate is really a
 * 64-bit compare - the same signature Detect-It-Easy ships as "Asymetrix". */
/* The DOS date precedes the DOS time in this record, the opposite of the
 * order every other DOS-era container uses. */
/* Every block in the reference corpus is at most 4096 packed bytes; the wider
 * bound only keeps a hypothetical expanding block from being read as a
 * structural error while still refusing an attacker-sized allocation. */
/* The member-level method the parse publishes.  A member has no method field
 * of its own - the method is chosen per block - so this single value stands
 * for "walk the block chain", and the decode accepts nothing else. */

static uint32_t xx_asymetrix_le16(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8);
}

static uint32_t xx_asymetrix_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_asymetrix_name_character(uint8_t value) {
    if (value < 0x20U || value > 0x7eU) return false;
    /* DOS 8.3 names only.  A separator here would let a member escape the
     * extraction folder and never occurs in the format, so it is a rejection
     * rather than something to strip. */
    switch (value) {
        case '\\':
        case '/':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return false;
        default:
            return true;
    }
}

/* Copies a NUL-terminated name out of a fixed-width field into @p out, which
 * must hold @p field_size + 1 bytes.  Returns false when the field carries no
 * terminator inside its own width or holds a byte unusable in a DOS name.
 *
 * The terminator may sit in the LAST byte of the field: a name that exactly
 * fills the field is legal, and stopping the scan one byte short rejects
 * every archive holding a full-width 8.3 name such as "comptr01.ico" - 51 of
 * the 56 volume-1 archives in the reference corpus. */
static bool xx_asymetrix_read_name(const uint8_t *field, size_t field_size,
                                   char *out) {
    size_t index;

    for (index = 0U; index < field_size; ++index) {
        if (field[index] == 0U) {
            if (index == 0U) return false;
            out[index] = '\0';
            return true;
        }
        if (!xx_asymetrix_name_character(field[index])) return false;
        out[index] = (char)field[index];
    }
    return false;
}

/* Walks a member's block chain and reports its exact byte length.  Offsets are
 * relative to base_address; @p region_end is where the next member on this
 * volume starts, or the span when this is the last one. */
static bool xx_asymetrix_measure(Abstractformat *self, int64_t data_offset,
                                 int64_t region_end, int64_t uncompressed,
                                 int64_t *stream_size, xx_pd_struct *pd) {
    uint8_t header[XX_ASYMETRIX_BLOCK_HEADER_SIZE];
    int64_t offset = data_offset;
    int64_t produced = 0;

    *stream_size = 0;
    if (uncompressed < 0 || data_offset < 0 || region_end < data_offset) {
        return false;
    }
    while (produced < uncompressed) {
        uint32_t method;
        uint32_t block_size;
        int64_t wanted;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_asymetrix_range_within(region_end, offset,
                                       XX_ASYMETRIX_BLOCK_HEADER_SIZE)) {
            return false;
        }
        if (!xx_asymetrix_read_at(self, self->base_address + offset, header,
                                  sizeof(header))) {
            return false;
        }
        method = xx_asymetrix_le16(header);
        block_size = xx_asymetrix_le32(header + 2);
        if (method != XX_ASYMETRIX_BLOCK_METHOD_STORED &&
            method != XX_ASYMETRIX_BLOCK_METHOD_IMPLODE) {
            return false;
        }
        if ((int64_t)block_size > XX_ASYMETRIX_MAX_BLOCK_SIZE) return false;
        if (!xx_asymetrix_range_within(
                region_end, offset + XX_ASYMETRIX_BLOCK_HEADER_SIZE,
                (int64_t)block_size)) {
            return false;
        }

        /* The block's unpacked size is never stored: it is implied to be
         * min(4096, remaining).  A stored block must therefore carry exactly
         * that many bytes - which is also what rejects the six-zero-byte
         * end-of-volume trailer marking a member as continuing on the next
         * disk, so a spanning member is refused here instead of silently
         * truncated. */
        wanted = uncompressed - produced;
        if (wanted > XX_ASYMETRIX_BLOCK_UNPACKED) {
            wanted = XX_ASYMETRIX_BLOCK_UNPACKED;
        }
        if (method == XX_ASYMETRIX_BLOCK_METHOD_STORED) {
            if ((int64_t)block_size != wanted) return false;
        } else if (block_size == 0U) {
            return false;
        }

        produced += wanted;
        offset += XX_ASYMETRIX_BLOCK_HEADER_SIZE + (int64_t)block_size;
    }
    *stream_size = offset - data_offset;
    return true;
}

static xx_asymetrix_stream *xx_asymetrix_parse(Abstractformat *self,
                                               xx_pd_struct *pd) {
    xx_asymetrix_stream *stream = NULL;
    uint8_t header[XX_ASYMETRIX_HEADER_SIZE];
    uint8_t probe[XX_ASYMETRIX_BLOCK_HEADER_SIZE + 2];
    char set_name[XX_ASYMETRIX_NAME_BUFFER];
    int64_t total;
    int64_t span;
    int64_t data_offset;
    uint32_t volume;
    uint32_t count;
    uint32_t first_method;
    uint32_t first_size;
    uint32_t previous_volume = 0U;
    uint32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_ASYMETRIX_HEADER_SIZE + XX_ASYMETRIX_BLOCK_HEADER_SIZE) {
        return NULL;
    }
    if (!xx_asymetrix_read_at(self, self->base_address, header,
                              sizeof(header))) {
        return NULL;
    }
    /* Magic and stride together are the primary gate.  The stride is not a
     * free field - the writer emits 0x6C and the directory walk below depends
     * on it - so checking only the first dword would let a file that merely
     * opens with those four bytes be parsed as a record chain. */
    if (xx_asymetrix_le32(header) != XX_ASYMETRIX_MAGIC) return NULL;
    if (xx_asymetrix_le32(header + 4) != (uint32_t)XX_ASYMETRIX_RECORD_STRIDE) {
        return NULL;
    }
    /* The set name is a real, always-populated field; an empty or non-DOS one
     * means the 0x2C bytes are not this header. */
    if (!xx_asymetrix_read_name(header + XX_ASYMETRIX_SETNAME_OFFSET,
                                XX_ASYMETRIX_SETNAME_FIELD, set_name)) {
        return NULL;
    }

    volume = xx_asymetrix_le16(header + XX_ASYMETRIX_VOLUME_OFFSET);
    count = xx_asymetrix_le16(header + XX_ASYMETRIX_COUNT_OFFSET);
    if (volume < 1U || volume > (uint32_t)XX_ASYMETRIX_MAX_VOLUME) return NULL;
    if (count < 1U || count > (uint32_t)XX_ASYMETRIX_MAX_MEMBERS) return NULL;

    /* The member count is the count for the whole SET and is repeated verbatim
     * on every volume, but only volume 1 carries the directory.  A later
     * volume is pure continuation data: its block chain starts immediately
     * behind the 0x2C header, and trusting the count there would parse
     * compressed bytes as file names.  Such a volume is still a genuine file
     * of this format and is identified as one - it simply has nothing to list,
     * because a member's directory record, and therefore its name and its
     * length, live on volume 1 and cannot be recovered from this file alone.
     * Refusing to recognise it at all left every .002.. .015 of a multi-disk
     * set undetected. */
    if (volume != 1U) {
        data_offset = XX_ASYMETRIX_HEADER_SIZE;
    } else {
        if (!xx_asymetrix_range_within(span, XX_ASYMETRIX_HEADER_SIZE,
                                       (int64_t)count *
                                           XX_ASYMETRIX_RECORD_STRIDE)) {
            return NULL;
        }
        data_offset = XX_ASYMETRIX_HEADER_SIZE +
                      (int64_t)count * XX_ASYMETRIX_RECORD_STRIDE;
    }
    if (!xx_asymetrix_range_within(span, data_offset,
                                   XX_ASYMETRIX_BLOCK_HEADER_SIZE)) {
        return NULL;
    }

    /* Structural gate on the data area: whatever the directory says, the first
     * thing behind it must be a well-formed block header, and when that block
     * is imploded it must open with the PKWARE DCL prelude 00 05 (literal mode
     * 0, 2048-byte window) that every compressed block in this format carries.
     * With the magic, this is the whole defence against a crafted 0x2C-byte
     * header followed by arbitrary bytes. */
    if (!xx_asymetrix_read_at(self, self->base_address + data_offset, probe,
                              XX_ASYMETRIX_BLOCK_HEADER_SIZE)) {
        return NULL;
    }
    first_method = xx_asymetrix_le16(probe);
    first_size = xx_asymetrix_le32(probe + 2);
    if (first_method != XX_ASYMETRIX_BLOCK_METHOD_STORED &&
        first_method != XX_ASYMETRIX_BLOCK_METHOD_IMPLODE) {
        return NULL;
    }
    if (first_size == 0U ||
        (int64_t)first_size > XX_ASYMETRIX_MAX_BLOCK_SIZE ||
        !xx_asymetrix_range_within(
            span, data_offset + XX_ASYMETRIX_BLOCK_HEADER_SIZE,
            (int64_t)first_size)) {
        return NULL;
    }
    if (first_method == XX_ASYMETRIX_BLOCK_METHOD_IMPLODE) {
        /* Read the prelude separately: a stored first block may be as short as
         * one byte, and demanding eight bytes up front would reject it. */
        if (!xx_asymetrix_range_within(
                span, data_offset + XX_ASYMETRIX_BLOCK_HEADER_SIZE, 2)) {
            return NULL;
        }
        if (!xx_asymetrix_read_at(
                self,
                self->base_address + data_offset +
                    XX_ASYMETRIX_BLOCK_HEADER_SIZE,
                probe + XX_ASYMETRIX_BLOCK_HEADER_SIZE, 2)) {
            return NULL;
        }
        if (probe[XX_ASYMETRIX_BLOCK_HEADER_SIZE] != 0x00U ||
            probe[XX_ASYMETRIX_BLOCK_HEADER_SIZE + 1] != 0x05U) {
            return NULL;
        }
    }

    stream = (xx_asymetrix_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* A continuation volume has no directory to walk: the block-header gate
     * above is the whole of its validation and it publishes no records. */
    for (index = 0U; volume == 1U && index < count; ++index) {
        uint8_t record[XX_ASYMETRIX_RECORD_STRIDE];
        char name[XX_ASYMETRIX_NAME_BUFFER];
        xx_asymetrix_member member;
        int64_t record_offset;
        int64_t member_offset;
        int64_t member_size;
        int64_t region_end;
        int64_t chain_size = 0;
        uint32_t member_volume;
        char *owned;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        record_offset = XX_ASYMETRIX_HEADER_SIZE +
                        (int64_t)index * XX_ASYMETRIX_RECORD_STRIDE;
        if (!xx_asymetrix_read_at(self, self->base_address + record_offset,
                                  record, sizeof(record))) {
            goto fail;
        }
        if (!xx_asymetrix_read_name(record, XX_ASYMETRIX_RECORD_NAME_FIELD,
                                    name)) {
            goto fail;
        }
        member_volume =
            xx_asymetrix_le32(record + XX_ASYMETRIX_RECORD_VOLUME) >> 16;
        member_offset =
            (int64_t)xx_asymetrix_le32(record + XX_ASYMETRIX_RECORD_OFFSET);
        member_size =
            (int64_t)xx_asymetrix_le32(record + XX_ASYMETRIX_RECORD_SIZE);

        /* Records are written sorted by (volume, offset).  That order is what
         * lets the next record delimit this member's block region in one pass
         * instead of a quadratic rescan, and a directory that is not sorted is
         * not this format's directory. */
        if (member_volume < 1U || member_volume < previous_volume) goto fail;
        previous_volume = member_volume;

        if (index == 0U) {
            /* The first member must be the first thing after the directory and
             * must live on this volume; no zero-size member exists. */
            if (member_volume != volume || member_offset != data_offset ||
                member_size == 0) {
                goto fail;
            }
        }
        /* Sorted by volume, so everything from here on begins on a later disk
         * and cannot be resolved from this file. */
        if (member_volume != volume) continue;

        region_end = span;
        if (index + 1U < count) {
            uint8_t next[XX_ASYMETRIX_RECORD_STRIDE];
            int64_t next_offset = record_offset + XX_ASYMETRIX_RECORD_STRIDE;

            if (!xx_asymetrix_read_at(self, self->base_address + next_offset,
                                      next, sizeof(next))) {
                goto fail;
            }
            if ((xx_asymetrix_le32(next + XX_ASYMETRIX_RECORD_VOLUME) >> 16) ==
                volume) {
                region_end =
                    (int64_t)xx_asymetrix_le32(next +
                                               XX_ASYMETRIX_RECORD_OFFSET);
                if (region_end < member_offset || region_end > span) goto fail;
            }
        }

        if (member_size > XX_ASYMETRIX_MAX_DECODED) continue;
        if (!xx_asymetrix_measure(self, member_offset, region_end, member_size,
                                  &chain_size, pd)) {
            /* Listed by the reference, refused here: a member whose chain runs
             * past this volume has no complete extent inside the file, and the
             * contract has no way to publish "present but unextractable". */
            continue;
        }
        if (!xx_asymetrix_range_within(span, member_offset, chain_size)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        owned = xx_str_dup(name);
        if (!owned) goto fail;
        member.name = owned;
        member.header_offset = self->base_address + record_offset;
        member.header_size = XX_ASYMETRIX_RECORD_STRIDE;
        member.data_offset = self->base_address + member_offset;
        member.compressed_size = chain_size;
        member.uncompressed_size = member_size;
        member.method = XX_ASYMETRIX_METHOD_BLOCKS;
        /* DOS date in the high half, DOS time in the low half - the in-record
         * order is reversed, the packed value is not. */
        member.timestamp =
            ((uint64_t)xx_asymetrix_le16(record + XX_ASYMETRIX_RECORD_DATE)
             << 16) |
            (uint64_t)xx_asymetrix_le16(record + XX_ASYMETRIX_RECORD_TIME);
        member.is_folder = false;
        if (!xx_asymetrix_add(stream, &member)) {
            xx_str_free(owned);
            goto fail;
        }
        if (stream->count > (size_t)XX_ASYMETRIX_MAX_MEMBERS) goto fail;
    }

    /* An empty publish list is not a parse failure.  A continuation volume has
     * no directory at all, and a volume 1 whose very first member is larger
     * than the disk it starts on has a directory that validated - record 0
     * was forced to start at the data offset with a nonzero size - but not one
     * complete extent to hand out.  Both are genuine archives of this format;
     * the structural gate above, not the member count, is what identifies
     * them. */

    /* The block chain of every volume runs to exactly EOF, so the container
     * occupies the whole file and there is no overlay to split off. */
    stream->archive_size = span;
    return stream;

fail:
    xx_asymetrix_stream_free(stream);
    return NULL;
}


static bool xx_asymetrix_decode(Abstractformat *self,
                                const xx_asymetrix_member *member,
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
    /* The parse publishes one method for every member because the method is a
     * per-BLOCK field here, not a per-member one; a chain mixes stored and
     * imploded blocks freely.  Anything else did not come from this parse. */
    if (member->method != XX_ASYMETRIX_METHOD_BLOCKS) return false;
    if (member->uncompressed_size < 0 || member->compressed_size < 0) {
        return false;
    }
    if ((uint64_t)member->uncompressed_size >
            (uint64_t)XX_ASYMETRIX_MAX_DECODED ||
        (uint64_t)member->compressed_size >
            (uint64_t)XX_ASYMETRIX_MAX_DECODED) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    plain_size = (size_t)member->uncompressed_size;
    if (plain_size == 0U) {
        /* An empty member has an empty chain; there is nothing to hand the
         * decoder, and calling it with a zero capacity would be a refusal. */
        if (member->compressed_size != 0) return false;
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }

    packed_size = (size_t)member->compressed_size;
    if (packed_size == 0U) return false;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed) return false;
    if (!xx_asymetrix_read_at(self, member->data_offset, packed,
                              packed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The declared size is not a hint: it is what tells the block walker how
     * much each block is supposed to produce.  The decoder succeeds only when
     * the chain yields exactly that and ends exactly on the last input byte. */
    if (!xx_asymetrix_decode_memory(packed, packed_size, plain, plain_size,
                                    &written) ||
        written != plain_size) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_asymetrix_init(xx_asymetrix *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ASYMETRIX;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-asymetrix");
    xx_format_set_extension(&archive->format, "001");
    archive->format.check_is_valid = xx_asymetrix_check_is_valid;
    archive->format.handle_base_info = xx_asymetrix_handle_base_info;
    archive->format.get_format_size = xx_asymetrix_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_asymetrix_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_asymetrix_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_asymetrix_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_asymetrix_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_asymetrix_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_asymetrix_free_archive_records_reading;
    archive->format.destroy = xx_asymetrix_vtable_destroy;
}

xx_asymetrix *xx_asymetrix_create(xx_io_device *device, int64_t base_address) {
    xx_asymetrix *archive = (xx_asymetrix *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_asymetrix_init(archive, device, base_address);
    return archive;
}

void xx_asymetrix_destroy(xx_asymetrix *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_asymetrix_free(xx_asymetrix *archive) {
    if (!archive) return;
    xx_asymetrix_destroy(archive);
    xx_mem_free(archive);
}

static void xx_asymetrix_vtable_destroy(Abstractformat *self) {
    xx_asymetrix_destroy((xx_asymetrix *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_asymetrix_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_asymetrix_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_asymetrix_parse(self, pd);
    if (!stream) return false;
    xx_asymetrix_stream_free(stream);
    return true;
}

bool xx_asymetrix_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_asymetrix *archive = (xx_asymetrix *)self;
    xx_asymetrix_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_asymetrix_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_asymetrix_stream_free(stream);
    return true;
}

int64_t xx_asymetrix_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_asymetrix_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_asymetrix *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_asymetrix_set_record(xx_archive_record *record,
                                 const xx_asymetrix_member *member) {
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

static bool xx_asymetrix_copy_options(xx_list_s *target,
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

static const xx_var *xx_asymetrix_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_asymetrix_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_asymetrix_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_asymetrix_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_asymetrix_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_asymetrix_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_asymetrix_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_asymetrix_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_asymetrix_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_asymetrix_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_asymetrix_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_asymetrix_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_asymetrix_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_asymetrix_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_asymetrix_stream *stream;
    const xx_asymetrix_member *member;
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
    stream = (xx_asymetrix_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_asymetrix_path_safe(member->name)) return false;

    path_option = xx_asymetrix_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_asymetrix_decode(self, member, &plain, &plain_size, pd);
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
        !xx_asymetrix_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_asymetrix_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
