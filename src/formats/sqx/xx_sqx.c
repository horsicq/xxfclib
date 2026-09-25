/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SQX archives (SQX-Software, Michael Bierhoff).
 *
 * Main header, 25 bytes at the start of the format:
 *
 *   0x00  u16 LE   CRC of the header
 *   0x02  u8       'R'
 *   0x03  u16 LE   archive flags; 0x0010 means the headers are encrypted and
 *                  nothing at all can be listed
 *   0x05  u16 LE   header size, always 0x19 = 25
 *   0x07  char[5]  "-sqx-"
 *   0x0c  u8[13]   writer version and reserved bytes
 *
 * Every record that follows opens with the same 7-byte stub:
 *
 *   0x00  u16 LE   CRC of the record header
 *   0x02  u8       record type: 'D' (0x44) is a file, 'A'/'S'/'X' end the
 *                  archive, anything else is a record kind this reader walks
 *                  over without listing
 *   0x03  u16 LE   record flags
 *   0x05  u16 LE   total size of this record header, the 7 bytes included
 *
 * and a file record's body, starting at +7, is at least 26 bytes:
 *
 *   +0x00  u8      "b0" preprocessor filter selector
 *   +0x05  u8      method: 0 stored, 1..4 the Huffman coder, 5+ undefined
 *   +0x06  u32 LE  CRC-32 of the decoded member
 *   +0x0a  u32 LE  attributes; 0x10 marks a directory
 *   +0x0e  u32 LE  timestamp
 *   +0x12  u32 LE  packed size, low 32 bits
 *   +0x16  u32 LE  unpacked size, low 32 bits
 *   +0x1a  u32 LE  packed size, high 32 bits    ) only when flag 0x0080
 *   +0x1e  u32 LE  unpacked size, high 32 bits  ) ("large") is set
 *   +0x1a or +0x22  u16 LE  name length, then that many name bytes
 *
 * When flag 0x8000 is set the record is followed by a chain of extension
 * headers, each a 7-byte stub of its own carrying its own size and its own
 * copy of the extension flag; the payload starts only after the chain ends.
 *
 * Record flags used here: 0x0004 solid, 0x0008 encrypted (refused), 0x0080
 * large, 0x8000 extension follows.
 *
 * SOLIDITY. A member with flag 0x0004 continues the previous coded member's
 * LZ state -- the window, the write cursor, the recent distances and the
 * last match all carry over -- so its bytes cannot be produced from its own
 * packed range alone. The codec takes the whole archive image plus the table
 * of coded members in archive order and replays them up to the wanted one,
 * which is why extraction here re-walks the directory and loads the archive
 * rather than reading a single member's range.
 *
 * Methods this reader decodes: 0 (stored) and 1..4 (the Huffman coder).
 * Method 5 and up, a nonzero "b0" filter and the alternative in-block
 * "direct" coder are refused by the codec rather than approximated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sqx/xx_sqx.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_SQX_COPY_CHUNK (64 * 1024)

typedef struct xx_sqx_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_sqx_member;

typedef struct xx_sqx_stream_s {
    xx_sqx_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_sqx_stream;

static void xx_sqx_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_sqx_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_sqx_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_sqx_path_safe(const char *name) {
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

static void xx_sqx_stream_free(void *pointer) {
    xx_sqx_stream *stream = (xx_sqx_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_sqx_add(xx_sqx_stream *stream,
                          const xx_sqx_member *member) {
    xx_sqx_member *grown = (xx_sqx_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_SQX_MIN_SIZE 25
#define XX_SQX_FIRST_HEADER 25
#define XX_SQX_RECORD_STUB 7 /* CRC, type, flags, size: shared by every record and every extension header */
#define XX_SQX_BODY_MIN 26 /* through the 32-bit unpacked size */
#define XX_SQX_MAX_MEMBERS 200000
#define XX_SQX_MAX_NAME 4096
#define XX_SQX_MAX_BODY 65535 /* the record size field is 16 bits */
#define XX_SQX_MAX_DECODED (256 * 1024 * 1024)
#define XX_SQX_MAX_IMAGE (256 * 1024 * 1024) /* the solid replay needs the whole archive resident */
#define XX_SQX_MAIN_FLAG_ENCRYPTED 0x0010u
#define XX_SQX_FLAG_SOLID 0x0004u
#define XX_SQX_FLAG_ENCRYPTED 0x0008u
#define XX_SQX_FLAG_LARGE 0x0080u
#define XX_SQX_FLAG_EXTENSION 0x8000u
#define XX_SQX_TYPE_FILE 0x44u /* 'D' */
#define XX_SQX_TYPE_END_A 0x41u
#define XX_SQX_TYPE_END_S 0x53u
#define XX_SQX_TYPE_END_X 0x58u

typedef struct xx_sqx_member {
    uint64_t data_offset;
    uint64_t packed_size;
    uint64_t unpacked_size;
    uint16_t flags;
    uint8_t filter;
    uint8_t method;
} xx_sqx_codec_member;

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_sqx_le16(const uint8_t *data);
static uint32_t xx_sqx_le32(const uint8_t *data);
static bool xx_sqx_is_end_type(uint8_t type);
static bool xx_sqx_name_byte_ok(uint8_t byte);
static bool xx_sqx_table_add(xx_sqx_codec_member **table, size_t *count, const xx_sqx_codec_member *entry);
static bool xx_sqx_walk(Abstractformat *self, xx_pd_struct *pd, xx_sqx_stream *stream, xx_sqx_codec_member **table, size_t *table_count);
static xx_sqx_stream *xx_sqx_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_sqx_decode(Abstractformat *self, const xx_sqx_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);



/* xxfclib/algo/sqx/xx_sqx.h is deliberately NOT included: it typedefs the
 * name xx_sqx_member for the codec's member descriptor, and the generator
 * uses that same name for this reader's member. The two struct TAGS differ
 * (struct xx_sqx_member against struct xx_sqx_member_s), so completing the
 * library's tag here and giving it a second, non-colliding alias produces a
 * type compatible with the library's own declaration; the entry point
 * declared below is therefore the same function, not a copy of it. Keep this
 * struct in step with xx_sqx.h. */


XXFC_API bool xx_sqx_decode_memory(const uint8_t *input, size_t input_size,
                                   const struct xx_sqx_member *members,
                                   size_t member_count, size_t target_index,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

static uint16_t xx_sqx_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_sqx_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_sqx_is_end_type(uint8_t type) {
    return type == XX_SQX_TYPE_END_A || type == XX_SQX_TYPE_END_S ||
           type == XX_SQX_TYPE_END_X;
}

/* SQX is a DOS/Windows archiver from the OEM code-page era and its name field
 * is raw bytes with nothing to say which page, so high bytes are passed
 * through unchanged. A control byte, on the other hand, cannot occur in a
 * name and marks a record header that is not one. */
static bool xx_sqx_name_byte_ok(uint8_t byte) {
    return byte >= 0x20U && byte != 0x7FU;
}

/* Append one coded member to the replay table the codec needs. */
static bool xx_sqx_table_add(xx_sqx_codec_member **table, size_t *count,
                             const xx_sqx_codec_member *entry) {
    xx_sqx_codec_member *grown = (xx_sqx_codec_member *)xx_mem_realloc(
        *table, sizeof(*grown) * (*count + 1U));

    if (!grown) return false;
    *table = grown;
    (*table)[(*count)++] = *entry;
    return true;
}

/* The one walk of the record chain, shared by the parse and the decode.
 *
 * @p stream, when given, receives every listable member. @p table, when
 * given, receives only the CODED members, in archive order -- that ordering
 * is the whole point, because the codec replays the table from index 0 to
 * rebuild the solid window, and a stored member never touches it and so must
 * not appear. Having one function build both is what keeps the two views
 * from drifting: a record the parse lists but the table skips, or vice
 * versa, would silently shift every later member's window. */
static bool xx_sqx_walk(Abstractformat *self, xx_pd_struct *pd,
                        xx_sqx_stream *stream, xx_sqx_codec_member **table,
                        size_t *table_count) {
    uint8_t head[XX_SQX_FIRST_HEADER];
    uint8_t stub[XX_SQX_RECORD_STUB];
    uint8_t *body = NULL;
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t offset;
    size_t member_index = 0U;
    bool result = false;

    if (!self || !self->device || self->base_address < 0) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < XX_SQX_MIN_SIZE) return false;
    if (!xx_sqx_read_at(self, self->base_address, head, sizeof(head))) {
        return false;
    }
    /* The three fixed things in the main header. "-sqx-" at 0x07 carries most
     * of the weight; the 'R' at 0x02 and the size field that must read
     * exactly 25 are what stop the five characters appearing inside unrelated
     * data from being taken for an archive start. */
    if (head[2] != (uint8_t)'R') return false;
    if (xx_sqx_le16(head + 5) != (uint16_t)XX_SQX_FIRST_HEADER) return false;
    if (head[7] != (uint8_t)'-' || head[8] != (uint8_t)'s' ||
        head[9] != (uint8_t)'q' || head[10] != (uint8_t)'x' ||
        head[11] != (uint8_t)'-') {
        return false;
    }
    /* Encrypted headers: not even the member list can be produced, and
     * guessing would publish names read out of ciphertext. */
    if (xx_sqx_le16(head + 3) & XX_SQX_MAIN_FLAG_ENCRYPTED) return false;

    body = (uint8_t *)xx_mem_alloc((size_t)XX_SQX_MAX_BODY);
    name = (char *)xx_mem_alloc((size_t)XX_SQX_MAX_NAME + 1U);
    if (!body || !name) goto done;

    offset = XX_SQX_FIRST_HEADER;
    while ((offset + XX_SQX_RECORD_STUB) <= span) {
        xx_sqx_member member;
        xx_sqx_codec_member entry;
        int64_t header_size;
        int64_t header_offset = offset;
        int64_t body_size;
        int64_t cursor;
        int64_t packed;
        int64_t unpacked;
        int32_t name_size;
        int32_t name_available;
        int32_t index;
        uint32_t attributes;
        uint16_t flags;
        uint16_t chain_flags;
        uint8_t type;
        bool is_folder;

        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (member_index >= (size_t)XX_SQX_MAX_MEMBERS) break;
        if (!xx_sqx_read_at(self, self->base_address + offset, stub,
                            sizeof(stub))) {
            goto done;
        }
        type = stub[2];
        flags = xx_sqx_le16(stub + 3);
        header_size = (int64_t)xx_sqx_le16(stub + 5);

        if (xx_sqx_is_end_type(type)) break;
        /* A record that does not cover its own stub, or that runs past EOF,
         * ends the walk: the chain is advanced by this field alone, so a bad
         * value means every later offset is fiction. */
        if (header_size < XX_SQX_RECORD_STUB) break;
        if (!xx_sqx_range_within(span, offset, header_size)) break;

        if (type != XX_SQX_TYPE_FILE) {
            /* A record kind this reader does not list still has to be walked
             * over exactly, extension chain included, or the next file
             * record would be looked for in the middle of this one. */
            offset += header_size;
            chain_flags = flags;
            while (chain_flags & XX_SQX_FLAG_EXTENSION) {
                int64_t chain_size;

                if (pd && xx_pd_is_stopped(pd)) goto done;
                if ((offset + XX_SQX_RECORD_STUB) > span) break;
                if (!xx_sqx_read_at(self, self->base_address + offset, stub,
                                    sizeof(stub))) {
                    goto done;
                }
                chain_size = (int64_t)xx_sqx_le16(stub + 5);
                if (chain_size < XX_SQX_RECORD_STUB) break;
                offset += chain_size;
                chain_flags = xx_sqx_le16(stub + 3);
            }
            continue;
        }

        /* An encrypted member's header fields past the flag are ciphertext. */
        if (flags & XX_SQX_FLAG_ENCRYPTED) break;

        body_size = header_size - XX_SQX_RECORD_STUB;
        if (body_size < XX_SQX_BODY_MIN) break;
        if (!xx_sqx_read_at(self,
                            self->base_address + offset + XX_SQX_RECORD_STUB,
                            body, (size_t)body_size)) {
            goto done;
        }

        attributes = xx_sqx_le32(body + 10);
        packed = (int64_t)xx_sqx_le32(body + 18);
        unpacked = (int64_t)xx_sqx_le32(body + 22);
        cursor = 26;
        if (flags & XX_SQX_FLAG_LARGE) {
            if ((cursor + 8) > body_size) break;
            packed |= ((int64_t)xx_sqx_le32(body + 26)) << 32;
            unpacked |= ((int64_t)xx_sqx_le32(body + 30)) << 32;
            cursor = 34;
        }
        if ((cursor + 2) > body_size) break;
        name_size = (int32_t)xx_sqx_le16(body + cursor);
        cursor += 2;
        name_available = name_size;
        if ((int64_t)name_available > (body_size - cursor)) {
            name_available = (int32_t)(body_size - cursor);
        }
        if (name_available > XX_SQX_MAX_NAME) name_available = XX_SQX_MAX_NAME;
        for (index = 0; index < name_available; ++index) {
            uint8_t byte = body[cursor + index];

            if (byte == 0U) break;
            if (byte == (uint8_t)'\\') byte = (uint8_t)'/';
            if (!xx_sqx_name_byte_ok(byte)) goto done;
            name[index] = (char)byte;
        }
        name[index] = '\0';
        if (index == 0) {
            /* SQX permits an empty name field; the record is still a real
             * member, so it gets a positional stand-in rather than being
             * dropped, which would shift the replay table. */
            xx_rt_snprintf(name, (size_t)XX_SQX_MAX_NAME + 1U, "MEMBER%u",
                           (unsigned int)member_index);
        }

        /* The payload starts only after the extension chain. */
        offset += header_size;
        chain_flags = flags;
        while (chain_flags & XX_SQX_FLAG_EXTENSION) {
            int64_t chain_size;

            if (pd && xx_pd_is_stopped(pd)) goto done;
            if ((offset + XX_SQX_RECORD_STUB) > span) break;
            if (!xx_sqx_read_at(self, self->base_address + offset, stub,
                                sizeof(stub))) {
                goto done;
            }
            chain_size = (int64_t)xx_sqx_le16(stub + 5);
            if (chain_size < XX_SQX_RECORD_STUB) break;
            offset += chain_size;
            chain_flags = xx_sqx_le16(stub + 3);
        }

        if (packed < 0 || unpacked < 0) break;
        /* A member whose packed extent runs past EOF is a rejection. The
         * reference truncates it to what is left of the file instead; that
         * makes a member out of whatever bytes happen to be there, and for a
         * solid archive it also silently changes the window every later
         * member is decoded against. */
        if (!xx_sqx_range_within(span, offset, packed)) goto done;

        is_folder = (attributes & 0x10U) != 0U;
        if (stream) {
            xx_mem_zero(&member, sizeof(member));
            member.name = xx_str_dup(name);
            if (!member.name) goto done;
            member.header_offset = self->base_address + header_offset;
            member.header_size = header_size;
            member.data_offset = self->base_address + offset;
            member.compressed_size = packed;
            member.uncompressed_size = unpacked;
            /* The container's own method byte, unchanged: the mapping to a
             * codec lives only in the decode. */
            member.method = (uint32_t)body[5];
            member.timestamp = (uint64_t)xx_sqx_le32(body + 14);
            member.is_folder = is_folder;
            if (!xx_sqx_add(stream, &member)) {
                xx_str_free(member.name);
                goto done;
            }
        }
        if (table && !is_folder && body[5] >= 1U && body[5] <= 4U) {
            xx_mem_zero(&entry, sizeof(entry));
            /* Offsets in the table are relative to the image the decode
             * loads, which starts at base_address -- not absolute file
             * offsets. */
            entry.data_offset = (uint64_t)offset;
            entry.packed_size = (uint64_t)packed;
            entry.unpacked_size = (uint64_t)unpacked;
            entry.flags = flags;
            entry.filter = body[0];
            entry.method = body[5];
            if (!xx_sqx_table_add(table, table_count, &entry)) goto done;
        }

        ++member_index;
        offset += packed;
    }

    if (member_index == 0U) goto done;
    if (pd && xx_pd_is_stopped(pd)) goto done;
    if (stream) {
        stream->archive_size = (offset < span) ? offset : span;
    }
    result = true;

done:
    xx_mem_free(body);
    xx_str_free(name);
    return result;
}

static xx_sqx_stream *xx_sqx_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_sqx_stream *stream = (xx_sqx_stream *)xx_mem_alloc(sizeof(*stream));

    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    if (!xx_sqx_walk(self, pd, stream, NULL, NULL)) goto fail;
    if (stream->count == 0U) goto fail;
    return stream;

fail:
    xx_sqx_stream_free(stream);
    return NULL;
}


/* Extraction of a coded member re-walks the chain and loads the whole
 * archive. That is not laziness: flag 0x0004 makes a member's plaintext
 * depend on the LZ state left by every coded member in front of it, and the
 * generator hands the decode one member with no way back to the member list,
 * so the table the codec replays has to be rebuilt here. A stored member is
 * exempt and takes the cheap path. */

static bool xx_sqx_decode(Abstractformat *self, const xx_sqx_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    xx_sqx_codec_member *table = NULL;
    size_t table_count = 0U;
    size_t target_index = 0U;
    size_t index;
    size_t written = 0U;
    size_t plain_size;
    uint8_t *image = NULL;
    uint8_t *plain = NULL;
    int64_t total;
    int64_t span;
    uint64_t wanted;
    bool found = false;

    *out = NULL;
    *out_size = 0U;
    if (!self || !self->device || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    /* The stored unpacked size is attacker-controlled, so it is capped before
     * it becomes an allocation. */
    if (member->uncompressed_size > XX_SQX_MAX_DECODED) return false;
    plain_size = (size_t)member->uncompressed_size;

    if (member->method == 0U) {
        /* Stored: the two sizes are the same number stated twice. */
        if (member->compressed_size != member->uncompressed_size) return false;
        plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : (size_t)1);
        if (!plain) return false;
        if (plain_size != 0U &&
            !xx_sqx_read_at(self, member->data_offset, plain, plain_size)) {
            xx_mem_free(plain);
            return false;
        }
        *out = plain;
        *out_size = plain_size;
        return true;
    }
    /* Methods 5 and up exist in the container but have no decoder here.
     * Falling through to the stored path would hand back a bitstream dressed
     * as file data. */
    if (member->method > 4U) return false;
    /* Every coded stream emits at least one block. */
    if (member->compressed_size == 0 || member->uncompressed_size == 0) {
        return false;
    }

    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span > XX_SQX_MAX_IMAGE) return false;
    if (!xx_sqx_walk(self, pd, NULL, &table, &table_count)) goto fail;
    if (table_count == 0U) goto fail;

    /* Coded members occupy disjoint, strictly increasing packed ranges, so
     * the payload offset identifies one of them exactly. A member the walk
     * did not put in the table cannot be replayed at all and is refused
     * rather than decoded against an empty window. */
    wanted = (uint64_t)(member->data_offset - self->base_address);
    for (index = 0U; index < table_count; ++index) {
        if (table[index].data_offset == wanted) {
            target_index = index;
            found = true;
            break;
        }
    }
    if (!found) goto fail;
    if (table[target_index].unpacked_size != (uint64_t)plain_size) goto fail;

    image = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!image) goto fail;
    if (!xx_sqx_read_at(self, self->base_address, image, (size_t)span)) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) goto fail;
    if (!xx_sqx_decode_memory(image, (size_t)span,
                              (const struct xx_sqx_member *)table, table_count,
                              target_index, plain, plain_size, &written)) {
        goto fail;
    }
    /* Never true with fewer bytes than the header promised: a solid member
     * decoded against a window that was rebuilt only partway produces
     * plausible bytes of very nearly the right length, and the count is the
     * only thing that catches it. */
    if (written != plain_size) goto fail;

    xx_mem_free(image);
    xx_mem_free(table);
    *out = plain;
    *out_size = written;
    return true;

fail:
    xx_mem_free(plain);
    xx_mem_free(image);
    xx_mem_free(table);
    return false;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_sqx_init(xx_sqx *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SQX;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sqx");
    xx_format_set_extension(&archive->format, "sqx");
    archive->format.check_is_valid = xx_sqx_check_is_valid;
    archive->format.handle_base_info = xx_sqx_handle_base_info;
    archive->format.get_format_size = xx_sqx_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sqx_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sqx_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sqx_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sqx_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sqx_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sqx_free_archive_records_reading;
    archive->format.destroy = xx_sqx_vtable_destroy;
}

xx_sqx *xx_sqx_create(xx_io_device *device, int64_t base_address) {
    xx_sqx *archive = (xx_sqx *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_sqx_init(archive, device, base_address);
    return archive;
}

void xx_sqx_destroy(xx_sqx *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_sqx_free(xx_sqx *archive) {
    if (!archive) return;
    xx_sqx_destroy(archive);
    xx_mem_free(archive);
}

static void xx_sqx_vtable_destroy(Abstractformat *self) {
    xx_sqx_destroy((xx_sqx *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_sqx_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_sqx_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_sqx_parse(self, pd);
    if (!stream) return false;
    xx_sqx_stream_free(stream);
    return true;
}

bool xx_sqx_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sqx *archive = (xx_sqx *)self;
    xx_sqx_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_sqx_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_sqx_stream_free(stream);
    return true;
}

int64_t xx_sqx_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_sqx_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_sqx *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_sqx_set_record(xx_archive_record *record,
                                 const xx_sqx_member *member) {
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

static bool xx_sqx_copy_options(xx_list_s *target,
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

static const xx_var *xx_sqx_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_sqx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_sqx_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_sqx_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_sqx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_sqx_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_sqx_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_sqx_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sqx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sqx_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_sqx_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sqx_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_sqx_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_sqx_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_sqx_stream *stream;
    const xx_sqx_member *member;
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
    stream = (xx_sqx_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_sqx_path_safe(member->name)) return false;

    path_option = xx_sqx_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_sqx_decode(self, member, &plain, &plain_size, pd);
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
        !xx_sqx_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_sqx_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
