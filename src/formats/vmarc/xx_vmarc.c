/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * VMARC archives (John Fisher, Rice University), from VM/CMS.
 *
 * There is no archive-wide header and no directory: members are found by
 * scanning 80-byte boundaries for the 9-byte member signature, which is the
 * only magic the format has:
 *
 *     7A C3 C6 C6 40 40 40 40 01     -- 'z' then EBCDIC "FF    " then 01
 *
 *   member header, 38 (0x26) bytes:
 *     0x00  u8[9]    the signature above
 *     0x0a  char[8]  member name, EBCDIC, blank padded
 *     0x12  char[8]  member type (the CMS file type), EBCDIC, blank padded
 *     0x1c  u16 BE   LRECL
 *     0x24  u8       record format, EBCDIC 'F' (0xC6) or 'V' (0xE5)
 *     0x25  u8       flags:
 *                      0x01  a 12-byte extended header follows this one
 *                      0x40  member is STORED ("ASIS")
 *                      0x80  member uses a third codec this reader has no
 *                            decoder for
 *
 * The member name presented is "name.type", both fields decoded through
 * CP037 and right-stripped of blanks.
 *
 * The member data starts right after the header (plus the 12-byte extension
 * when flag 0x01 is set) and is written RAW EBCDIC with the CMS records
 * simply concatenated -- no separators, no length prefixes, no code-page
 * translation. NEITHER CODEC STORES AN OUTPUT LENGTH, and neither stores an
 * input length either, so a member's extent is learnt by running
 * xx_vmarc_scan_memory_ex() over it; the end offset it reports is what lets
 * the walk round up to the next 80-byte boundary and look for the next
 * header.
 *
 * Codecs:
 *     STORED (flags & 0x40)  u16 BE length-prefixed records, a zero length
 *                            ending the member.
 *     LZW                    12-bit MSB-first codes over a 4096-entry trie
 *                            with leaf recycling. Symbol 0 is end-of-record
 *                            and symbols 1..256 are data bytes value - 1; an
 *                            'F' member ends on one end-of-record, a 'V'
 *                            member needs two consecutive ones.
 *
 * A member with flag 0x80 and without 0x40 refuses the WHOLE container, not
 * just that member: member boundaries are discovered by decoding, so once a
 * member cannot be decoded nothing after it can be located either.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vmarc/xx_vmarc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/vmarc/xx_vmarc.h"

#include <stdio.h>

#define XX_VMARC_COPY_CHUNK (64 * 1024)

typedef struct xx_vmarc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_vmarc_member;

typedef struct xx_vmarc_stream_s {
    xx_vmarc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_vmarc_stream;

static void xx_vmarc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_vmarc_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_vmarc_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_vmarc_path_safe(const char *name) {
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

static void xx_vmarc_stream_free(void *pointer) {
    xx_vmarc_stream *stream = (xx_vmarc_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_vmarc_add(xx_vmarc_stream *stream,
                          const xx_vmarc_member *member) {
    xx_vmarc_member *grown = (xx_vmarc_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_VMARC_HEADER_SIZE 0x26
#define XX_VMARC_EXTENDED_SIZE 12
#define XX_VMARC_BLOCK 80
#define XX_VMARC_NAME_FIELD 8
#define XX_VMARC_NAME_OFFSET 0x0A
#define XX_VMARC_TYPE_OFFSET 0x12
#define XX_VMARC_LRECL_OFFSET 0x1C
#define XX_VMARC_RECFM_OFFSET 0x24
#define XX_VMARC_FLAGS_OFFSET 0x25
#define XX_VMARC_RECFM_FIXED 0xC6U /* EBCDIC 'F' */
#define XX_VMARC_FLAG_EXTENDED 0x01U
#define XX_VMARC_FLAG_STORED 0x40U
#define XX_VMARC_FLAG_OTHER_CODEC 0x80U
#define XX_VMARC_MAX_MEMBERS 100000
#define XX_VMARC_MAX_INPUT 0x10000000
#define XX_VMARC_MAX_DECODED 0x20000000
#define XX_VMARC_FALLBACK_NAME_SIZE 24

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_vmarc_be16(const uint8_t *data);
static bool xx_vmarc_is_header(const uint8_t *data, int64_t size, int64_t offset);
static int64_t xx_vmarc_align_up(int64_t value);
static bool xx_vmarc_field(const uint8_t *field, char *out, size_t *length);
static char *xx_vmarc_make_name(const uint8_t *header, size_t ordinal);
static xx_vmarc_stream *xx_vmarc_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_vmarc_decode(Abstractformat *self, const xx_vmarc_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Members start on 80-byte bounds: the CMS record length this was written
 * with. It is also the walk's step when a boundary holds no signature. */
/* A runaway guard, not a format limit: nothing states the member count. */
/* Mirrors the reference ceiling. The whole container is buffered so the
 * per-member scans can run, so this is also the largest allocation. */
/* "MEMBER" + up to 5 digits + NUL, with room to spare. */

/* CP037 (the code page the reference decodes member names with), reduced to
 * the bytes whose image is printable ASCII. Every other EBCDIC byte maps to
 * 0 here and is refused: the mapped characters are letters, digits, blank
 * and the CMS-legal specials, which is the whole of what a CMS file name or
 * file type may hold. Rejecting the rest is what keeps a chance 9-byte
 * signature match inside unrelated data from being published as a member. */
static const uint8_t xx_vmarc_cp037[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x2E, 0x3C, 0x28, 0x2B, 0x7C,
    0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x21, 0x24, 0x2A, 0x29, 0x3B, 0x00,
    0x2D, 0x2F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x2C, 0x25, 0x5F, 0x3E, 0x3F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22,
    0x00, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,
    0x71, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x7E, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x5E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x5B, 0x5D, 0x00, 0x00, 0x00, 0x00,
    0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
    0x51, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x5C, 0x00, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static uint16_t xx_vmarc_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

/* The 9-byte member signature. It is the format's only magic, and the walk
 * tries it at every 80-byte boundary, so it has to carry all nine bytes. */
static bool xx_vmarc_is_header(const uint8_t *data, int64_t size,
                               int64_t offset) {
    static const uint8_t magic[9] = {0x7A, 0xC3, 0xC6, 0xC6, 0x40,
                                     0x40, 0x40, 0x40, 0x01};

    if (offset < 0 || size < 9 || offset > size - 9) return false;
    return xx_rt_memcmp(data + offset, magic, sizeof(magic)) == 0;
}

static int64_t xx_vmarc_align_up(int64_t value) {
    if (value < 0) return 0;
    return ((value + XX_VMARC_BLOCK - 1) / XX_VMARC_BLOCK) * XX_VMARC_BLOCK;
}

/* Decode one blank-padded EBCDIC field into @p out, right-stripping blanks.
 * Returns false for any byte CP037 does not map to printable ASCII. Only the
 * trailing blanks go: a leading blank would be part of the name if a
 * producer ever emitted one, exactly as the reference has it. */
static bool xx_vmarc_field(const uint8_t *field, char *out, size_t *length) {
    size_t used = 0U;
    size_t index;

    for (index = 0U; index < (size_t)XX_VMARC_NAME_FIELD; ++index) {
        uint8_t mapped = xx_vmarc_cp037[field[index]];

        if (mapped == 0U) return false;
        out[used++] = (char)mapped;
    }
    while (used > 0U && out[used - 1U] == ' ') --used;
    out[used] = '\0';
    *length = used;
    return true;
}

/* "name.type", or "MEMBERn" when both fields are blank. */
static char *xx_vmarc_make_name(const uint8_t *header, size_t ordinal) {
    char name_text[XX_VMARC_NAME_FIELD + 1];
    char type_text[XX_VMARC_NAME_FIELD + 1];
    char fallback[XX_VMARC_FALLBACK_NAME_SIZE];
    char *result;
    size_t name_size = 0U;
    size_t type_size = 0U;
    size_t out = 0U;
    size_t index;

    if (!xx_vmarc_field(header + XX_VMARC_NAME_OFFSET, name_text,
                        &name_size) ||
        !xx_vmarc_field(header + XX_VMARC_TYPE_OFFSET, type_text,
                        &type_size)) {
        return NULL;
    }
    if (name_size == 0U && type_size == 0U) {
        if (xx_rt_snprintf(fallback, sizeof(fallback), "MEMBER%u",
                           (unsigned)ordinal) <= 0) {
            return NULL;
        }
        return xx_str_dup(fallback);
    }
    result = (char *)xx_mem_alloc(name_size + type_size + 2U);
    if (!result) return NULL;
    for (index = 0U; index < name_size; ++index) result[out++] = name_text[index];
    /* The type is the CMS file type, joined with a dot so it reads as an
     * extension; a member with a name but no type gets no trailing dot. */
    if (type_size > 0U) {
        if (name_size > 0U) result[out++] = '.';
        for (index = 0U; index < type_size; ++index) {
            result[out++] = type_text[index];
        }
    }
    result[out] = '\0';
    return result;
}

static xx_vmarc_stream *xx_vmarc_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_vmarc_stream *stream = NULL;
    uint8_t probe[XX_VMARC_HEADER_SIZE];
    uint8_t *data = NULL;
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t end = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_VMARC_HEADER_SIZE || span > XX_VMARC_MAX_INPUT) return NULL;
    if ((uint64_t)span > (uint64_t)SIZE_MAX) return NULL;

    if (!xx_vmarc_read_at(self, self->base_address, probe, sizeof(probe))) {
        return NULL;
    }
    /* The first member must sit at offset 0. The walk below would happily
     * find a signature further in, but accepting that would make every file
     * that happens to contain those nine bytes on an 80-byte boundary a
     * VMARC archive; the reference anchors the same way. */
    if (!xx_vmarc_is_header(probe, (int64_t)sizeof(probe), 0)) return NULL;

    data = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!data) return NULL;
    if (!xx_vmarc_read_at(self, self->base_address, data, (size_t)span)) {
        goto fail;
    }

    stream = (xx_vmarc_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    offset = 0;
    while ((offset + XX_VMARC_HEADER_SIZE) <= span) {
        xx_vmarc_member member;
        xx_vmarc_params params;
        const uint8_t *header;
        int64_t data_offset;
        int64_t member_end;
        int64_t next;
        size_t consumed = 0U;
        size_t produced = 0U;
        uint8_t flags;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_VMARC_MAX_MEMBERS) break;

        if (!xx_vmarc_is_header(data, span, offset)) {
            /* No signature here: step one header on, then back to the next
             * 80-byte boundary, which is where a member could start. */
            offset = xx_vmarc_align_up(offset + XX_VMARC_HEADER_SIZE);
            continue;
        }

        header = data + offset;
        flags = header[XX_VMARC_FLAGS_OFFSET];

        /* A third codec means no member after this one can be located
         * either, because member ends are discovered by decoding. Refusing
         * the container is the honest answer; skipping the member would
         * resume the walk at an offset that is only a guess. */
        if (!(flags & XX_VMARC_FLAG_STORED) &&
            (flags & XX_VMARC_FLAG_OTHER_CODEC)) {
            goto fail;
        }

        data_offset = offset + XX_VMARC_HEADER_SIZE;
        if (flags & XX_VMARC_FLAG_EXTENDED) {
            data_offset += XX_VMARC_EXTENDED_SIZE;
        }
        if (!xx_vmarc_range_within(span, offset, data_offset - offset)) break;
        if (data_offset >= span) break;

        params.lrecl = xx_vmarc_be16(header + XX_VMARC_LRECL_OFFSET);
        /* 'F' versus 'V' is not cosmetic: it decides whether ONE or TWO
         * consecutive end-of-record symbols terminate the member, so getting
         * it wrong changes where the member ends and therefore where the
         * next header is looked for. */
        params.fixed =
            (header[XX_VMARC_RECFM_OFFSET] == (uint8_t)XX_VMARC_RECFM_FIXED);
        params.mode = (flags & XX_VMARC_FLAG_STORED) ? XX_VMARC_MODE_STORED
                                                     : XX_VMARC_MODE_LZW;

        /* Nothing stores the member's length, in either direction, so the
         * scan is what produces both sizes and the member's end offset. A
         * member that does not terminate cleanly ends the walk: publishing
         * it would mean publishing an extent that is a guess. */
        if (!xx_vmarc_scan_memory_ex(data + data_offset,
                                     (size_t)(span - data_offset), &params,
                                     (size_t)XX_VMARC_MAX_DECODED, &consumed,
                                     &produced)) {
            break;
        }
        member_end = data_offset + (int64_t)consumed;
        if (member_end < data_offset || member_end > span) break;
        if (!xx_vmarc_range_within(span, data_offset,
                                   member_end - data_offset)) {
            break;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_vmarc_make_name(header, stream->count);
        if (!member.name) break;
        member.header_offset = self->base_address + offset;
        member.header_size = data_offset - offset;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = member_end - data_offset;
        member.uncompressed_size = (int64_t)produced;
        /* The raw flags byte, not a library enum: it is the container's own
         * codec field, and a listing should show what the archive said. */
        member.method = (uint32_t)flags;
        member.timestamp = 0U;
        member.is_folder = false;
        if (!xx_vmarc_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        end = member_end;
        next = xx_vmarc_align_up(member_end);
        /* Every member header is 38 bytes, so member_end is always past
         * offset and the walk always advances; the check is here so that
         * stays true if the arithmetic above is ever changed. */
        if (next <= offset) break;
        offset = next;
    }

    if (stream->count == 0U) goto fail;
    xx_mem_free(data);
    data = NULL;

    stream->archive_size = xx_vmarc_align_up(end);
    if (stream->archive_size < XX_VMARC_HEADER_SIZE) {
        stream->archive_size = XX_VMARC_HEADER_SIZE;
    }
    if (stream->archive_size > span) stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(data);
    xx_vmarc_stream_free(stream);
    return NULL;
}


/* The measured size passes through the member struct, so it is capped before
 * it becomes an allocation. */

static bool xx_vmarc_decode(Abstractformat *self,
                            const xx_vmarc_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t header[XX_VMARC_HEADER_SIZE];
    xx_vmarc_params params;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t plain_size;
    uint8_t flags;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > XX_VMARC_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;

    /* LRECL and the record format live in the member header and nowhere in
     * the member struct, so the header is read back. Re-reading it is also
     * how this function stays free of side effects: it derives everything it
     * needs from the container rather than from parse state. */
    if (!xx_vmarc_read_at(self, member->header_offset, header,
                          sizeof(header))) {
        return false;
    }
    if (!xx_vmarc_is_header(header, (int64_t)sizeof(header), 0)) return false;
    flags = header[XX_VMARC_FLAGS_OFFSET];
    /* The parse recorded the flags byte as the method; if the two disagree
     * the container changed underneath us and the recorded sizes no longer
     * describe this member. */
    if ((uint32_t)flags != member->method) return false;

    /* The one codec VMARC defines that this reader cannot produce bytes for.
     * Treating it as LZW would emit trie output from a stream that is not a
     * trie, which nothing downstream could tell from real data. */
    if (!(flags & XX_VMARC_FLAG_STORED) &&
        (flags & XX_VMARC_FLAG_OTHER_CODEC)) {
        return false;
    }

    params.lrecl = xx_vmarc_be16(header + XX_VMARC_LRECL_OFFSET);
    params.fixed =
        (header[XX_VMARC_RECFM_OFFSET] == (uint8_t)XX_VMARC_RECFM_FIXED);
    params.mode = (flags & XX_VMARC_FLAG_STORED) ? XX_VMARC_MODE_STORED
                                                 : XX_VMARC_MODE_LZW;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_vmarc_read_at(self, member->data_offset, packed,
                          (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    /* xx_mem_alloc(0) returns NULL, which the caller cannot tell from a
     * failure, so a genuinely empty member still gets one byte. */
    plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : (size_t)1);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (!xx_vmarc_decode_memory_ex(packed, (size_t)member->compressed_size,
                                   &params, plain, plain_size, &written,
                                   NULL)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* Returning true with fewer bytes than the parse measured is the one
     * failure a caller cannot detect. The measure and the decode run the
     * same core, so a disagreement means the container changed -- or that
     * the record format was read differently, which moves the terminator and
     * silently truncates. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_vmarc_init(xx_vmarc *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_VMARC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vmarc");
    xx_format_set_extension(&archive->format, "vmarc");
    archive->format.check_is_valid = xx_vmarc_check_is_valid;
    archive->format.handle_base_info = xx_vmarc_handle_base_info;
    archive->format.get_format_size = xx_vmarc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vmarc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vmarc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vmarc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vmarc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vmarc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vmarc_free_archive_records_reading;
    archive->format.destroy = xx_vmarc_vtable_destroy;
}

xx_vmarc *xx_vmarc_create(xx_io_device *device, int64_t base_address) {
    xx_vmarc *archive = (xx_vmarc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_vmarc_init(archive, device, base_address);
    return archive;
}

void xx_vmarc_destroy(xx_vmarc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_vmarc_free(xx_vmarc *archive) {
    if (!archive) return;
    xx_vmarc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_vmarc_vtable_destroy(Abstractformat *self) {
    xx_vmarc_destroy((xx_vmarc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_vmarc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_vmarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_vmarc_parse(self, pd);
    if (!stream) return false;
    xx_vmarc_stream_free(stream);
    return true;
}

bool xx_vmarc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_vmarc *archive = (xx_vmarc *)self;
    xx_vmarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_vmarc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_vmarc_stream_free(stream);
    return true;
}

int64_t xx_vmarc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_vmarc_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_vmarc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_vmarc_set_record(xx_archive_record *record,
                                 const xx_vmarc_member *member) {
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

static bool xx_vmarc_copy_options(xx_list_s *target,
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

static const xx_var *xx_vmarc_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_vmarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_vmarc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_vmarc_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_vmarc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_vmarc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_vmarc_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_vmarc_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_vmarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vmarc_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_vmarc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_vmarc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_vmarc_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_vmarc_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_vmarc_stream *stream;
    const xx_vmarc_member *member;
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
    stream = (xx_vmarc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_vmarc_path_safe(member->name)) return false;

    path_option = xx_vmarc_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_vmarc_decode(self, member, &plain, &plain_size, pd);
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
        !xx_vmarc_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_vmarc_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
