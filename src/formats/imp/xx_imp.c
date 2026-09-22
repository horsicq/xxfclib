/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IMP archives (Technelysium's IMP archiver).
 *
 * This is not the ZIP "implode" method and not PKWARE DCL "explode"; the
 * coder is a block driver over a solid stream, and a member is a byte range
 * of the decoded stream rather than a range of the file.
 *
 * Archive header, 42 bytes at the start of the format:
 *
 *   0x00  char[4]  "IMP\n"
 *   0x04  u32 LE   offset of the directory, which begins "IMPDE\0"
 *   0x08  u32 LE   number of member records
 *   0x26  u16 LE   archive flags; 0x0001 and 0x0004 are features this reader
 *                  does not implement and are refused
 *   0x28  u16 LE   the low half of the CRC-32 of these 42 bytes computed
 *                  with the two CRC bytes themselves taken as zero
 *
 * The directory is COMPRESSED, with the same LZ77 coder the members use, so
 * nothing can be listed without decoding it first. It is a chain of chunks,
 * each opening with "IMPDE\0" and a block header giving its decoded and
 * packed sizes; a chunk decodes to at most 0x2000 bytes. A member record
 * never straddles a chunk: when the tail of a chunk is too short for one,
 * the walk moves to the next chunk, so the chunk boundaries have to be kept
 * rather than flattened into one buffer.
 *
 * Member record, 0x26 bytes plus three variable-length tails:
 *
 *   0x00  u16 LE   version; the low 12 bits must be below 0x010b
 *   0x04  u32 LE   file offset of the member's solid stream, which begins
 *                  with its own six-byte "IMP.." signature
 *   0x0a  u8       extra length
 *   0x0b  u8       attributes; bits 1..2 select the x86 branch converter
 *                  (2 = 16 bit, 4 = 32 bit)
 *   0x0c  u32 LE   the member's offset inside the DECODED solid stream
 *   0x10  u32 LE   decoded size of the member
 *   0x14  u32 LE   CRC-32 of the decoded member
 *   0x18  u16 LE   aux length
 *   0x1a  u16 LE   name length
 *   0x20  u16 LE   time, low half
 *   0x22  u16 LE   time, high half
 *   0x24  u16 LE   the low half of the CRC-32 of the whole record, these two
 *                  bytes taken as zero
 *   0x26  char[]   name, then the extra bytes, then the aux bytes
 *
 * At the member's offset inside the decoded stream sits an 11-byte in-stream
 * header (u32 size, u16 name length, u16 version, u16 crc16, u8 attributes)
 * and that many name bytes; the payload follows. The codec skips it.
 *
 * Because the directory records live inside a compressed chunk they have no
 * individual file offset: every member reports the directory's own offset as
 * its header offset, and the record's decoded length as its header size.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/imp/xx_imp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/imp/xx_imp.h"
#include "xxfclib/algo/crc/xx_crc.h"

#include <stdio.h>

#define XX_IMP_COPY_CHUNK (64 * 1024)

typedef struct xx_imp_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_imp_member;

typedef struct xx_imp_stream_s {
    xx_imp_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_imp_stream;

static void xx_imp_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_imp_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_imp_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_imp_path_safe(const char *name) {
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

static void xx_imp_stream_free(void *pointer) {
    xx_imp_stream *stream = (xx_imp_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_imp_add(xx_imp_stream *stream,
                          const xx_imp_member *member) {
    xx_imp_member *grown = (xx_imp_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_IMP_HEADER_SIZE 0x2a
#define XX_IMP_RECORD_SIZE XX_IMP_DIRECTORY_RECORD_SIZE /* 0x26, from xx_imp.h */
#define XX_IMP_MAX_MEMBERS 100000
#define XX_IMP_MAX_DIRECTORY 0x400000 /* packed directory bytes considered */
#define XX_IMP_MAX_CHUNKS 65536
#define XX_IMP_CHUNK_SLACK 0x2000 /* the codec's per-chunk decode ceiling: the last chunk may overshoot the record total by this much */
#define XX_IMP_UNSUPPORTED_FLAGS 0x0005u
#define XX_IMP_MAX_NAME 4096
#define XX_IMP_MAX_DECODED (256 * 1024 * 1024)
#define XX_IMP_MAX_STREAM (256 * 1024 * 1024) /* a member is a slice of a solid stream, so the whole tail has to be resident */

typedef struct xx_imp_record_s {
    int64_t stream_base;   /* file offset of the solid stream, base-relative */
    int64_t stream_offset; /* offset inside that stream once decoded */
    int64_t decoded_size;
    uint8_t attributes;
} xx_imp_record;

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_imp_le16(const uint8_t *data);
static uint32_t xx_imp_le32(const uint8_t *data);
static uint16_t xx_imp_check16(const uint8_t *data, size_t size, size_t hole);
static bool xx_imp_name_byte_ok(uint8_t byte);
static bool xx_imp_record_add(xx_imp_record **records, size_t *count, const xx_imp_record *entry);
static bool xx_imp_walk(Abstractformat *self, xx_pd_struct *pd, xx_imp_stream *stream, xx_imp_record **records, size_t *record_count);
static xx_imp_stream *xx_imp_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_imp_same_member(const xx_imp_member *left, const xx_imp_member *right);
static bool xx_imp_decode(Abstractformat *self, const xx_imp_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);



/* The per-member facts the directory carries that the generator's member
 * struct has nowhere to put: a member's position inside the DECODED solid
 * stream, and the attribute byte that selects the branch converter. Built in
 * the same order as the members, one to one. */


static uint16_t xx_imp_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_imp_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* CRC-32 of @p data with the two bytes at @p hole taken as zero, which is how
 * both the archive header and every record store their own checksum. Computed
 * in three runs rather than by editing the buffer, so the check has no side
 * effect on the decoded directory. */
static uint16_t xx_imp_check16(const uint8_t *data, size_t size, size_t hole) {
    static const uint8_t zeros[2] = {0U, 0U};
    uint32_t crc;

    if (size < hole + 2U) return 0xFFFFU; /* cannot match a stored value */
    crc = xx_crc32_calc(0U, data, hole);
    crc = xx_crc32_calc(crc, zeros, 2U);
    crc = xx_crc32_calc(crc, data + hole + 2U, size - hole - 2U);
    return (uint16_t)(crc & 0xFFFFU);
}

/* IMP names come from DOS and early Windows and are raw bytes in the
 * writer's code page, so high bytes are passed through; a control byte is
 * not a name. */
static bool xx_imp_name_byte_ok(uint8_t byte) {
    return byte >= 0x20U && byte != 0x7FU;
}

static bool xx_imp_record_add(xx_imp_record **records, size_t *count,
                              const xx_imp_record *entry) {
    xx_imp_record *grown = (xx_imp_record *)xx_mem_realloc(
        *records, sizeof(*grown) * (*count + 1U));

    if (!grown) return false;
    *records = grown;
    (*records)[(*count)++] = *entry;
    return true;
}

/* The single walk of the archive, shared by the parse and the decode. The
 * directory has to be decoded before anything can be listed, and the decode
 * needs facts the member struct cannot carry, so both callers run this and
 * the two views are built side by side in the same pass. */
static bool xx_imp_walk(Abstractformat *self, xx_pd_struct *pd,
                        xx_imp_stream *stream, xx_imp_record **records,
                        size_t *record_count) {
    uint8_t header[XX_IMP_HEADER_SIZE];
    uint8_t tag[6];
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    uint32_t *chunk_sizes = NULL;
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t packed_size;
    int64_t plain_capacity;
    size_t chunk_capacity;
    size_t chunk_count = 0U;
    size_t written = 0U;
    size_t chunk = 0U;
    size_t chunk_base = 0U;
    size_t position = 0U;
    uint32_t records_declared;
    uint32_t record_index;
    bool result = false;

    if (!self || !self->device || !stream || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < XX_IMP_HEADER_SIZE) return false;
    /* The codec refuses anything past 2 GiB, and every offset below is a
     * 32-bit field, so a larger span could never be addressed anyway. */
    if (span > 0x7FFFFFFF) return false;
    if (!xx_imp_read_at(self, self->base_address, header, sizeof(header))) {
        return false;
    }
    if (header[0] != (uint8_t)'I' || header[1] != (uint8_t)'M' ||
        header[2] != (uint8_t)'P' || header[3] != (uint8_t)'\n') {
        return false;
    }
    /* Four magic bytes are not enough on their own -- "IMP\n" occurs in text.
     * The header's own CRC-16 is what makes this format identifiable, and it
     * is the check a later reader must not turn advisory. */
    if (xx_imp_check16(header, sizeof(header), 0x28U) !=
        xx_imp_le16(header + 0x28)) {
        return false;
    }

    directory_offset = (int64_t)xx_imp_le32(header + 4);
    records_declared = xx_imp_le32(header + 8);
    /* Flags 0x0001 and 0x0004 turn on container features with no support
     * here; listing under them would describe members that are not laid out
     * the way this walk assumes. */
    if (xx_imp_le16(header + 0x26) & XX_IMP_UNSUPPORTED_FLAGS) return false;
    if (records_declared > (uint32_t)XX_IMP_MAX_MEMBERS) return false;

    if (records_declared == 0U) {
        /* A valid, empty archive: the header CRC already vouched for it. */
        stream->archive_size = span;
        return true;
    }

    if (!xx_imp_range_within(span, directory_offset, 6)) return false;
    if (!xx_imp_read_at(self, self->base_address + directory_offset, tag,
                        sizeof(tag))) {
        return false;
    }
    if (tag[0] != (uint8_t)'I' || tag[1] != (uint8_t)'M' ||
        tag[2] != (uint8_t)'P' || tag[3] != (uint8_t)'D' ||
        tag[4] != (uint8_t)'E' || tag[5] != 0U) {
        return false;
    }

    packed_size = span - directory_offset;
    if (packed_size > XX_IMP_MAX_DIRECTORY) packed_size = XX_IMP_MAX_DIRECTORY;
    /* The chunk walk stops once the decoded total reaches records * 0x26, and
     * one more chunk can overshoot by its own ceiling, so this bound is exact
     * rather than a guess. */
    plain_capacity =
        (int64_t)records_declared * XX_IMP_RECORD_SIZE + XX_IMP_CHUNK_SLACK;
    /* Every chunk consumes at least its six-byte tag plus a packed count. */
    chunk_capacity = (size_t)(packed_size / 7) + 2U;
    if (chunk_capacity > (size_t)XX_IMP_MAX_CHUNKS) {
        chunk_capacity = (size_t)XX_IMP_MAX_CHUNKS;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)plain_capacity);
    chunk_sizes = (uint32_t *)xx_mem_alloc(sizeof(uint32_t) * chunk_capacity);
    name = (char *)xx_mem_alloc((size_t)XX_IMP_MAX_NAME + 1U);
    if (!packed || !plain || !chunk_sizes || !name) goto done;
    if (!xx_imp_read_at(self, self->base_address + directory_offset, packed,
                        (size_t)packed_size)) {
        goto done;
    }
    if (pd && xx_pd_is_stopped(pd)) goto done;
    if (!xx_imp_decode_directory(packed, (size_t)packed_size, records_declared,
                                 plain, (size_t)plain_capacity, chunk_sizes,
                                 chunk_capacity, &chunk_count, &written)) {
        goto done;
    }
    if (chunk_count == 0U) goto done;

    for (record_index = 0U; record_index < records_declared; ++record_index) {
        xx_imp_member member;
        xx_imp_record entry;
        const uint8_t *record;
        int64_t record_total;
        int64_t stream_base;
        int64_t decoded_size;
        int32_t name_length;
        int32_t index;

        if (pd && xx_pd_is_stopped(pd)) goto done;
        /* A record never straddles a chunk: a tail too short for the fixed
         * part is padding, and the next record starts at the next chunk. */
        while ((chunk_sizes[chunk] - position) < (size_t)XX_IMP_RECORD_SIZE) {
            chunk_base += chunk_sizes[chunk];
            ++chunk;
            if (chunk >= chunk_count) goto done;
            position = 0U;
        }
        record = plain + chunk_base + position;

        /* The version field is the format's own compatibility gate. */
        if ((xx_imp_le16(record) & 0x0FFFU) >= 0x010BU) goto done;
        name_length = (int32_t)xx_imp_le16(record + 0x1A);
        record_total = (int64_t)XX_IMP_RECORD_SIZE + (int64_t)name_length +
                       (int64_t)record[0x0A] +
                       (int64_t)xx_imp_le16(record + 0x18);
        if ((int64_t)(chunk_sizes[chunk] - position) < record_total) goto done;
        /* Each record carries its own CRC-16 as well. With the directory
         * itself decoded from a compressed chunk, this is what tells a
         * correct decode from one that produced plausible bytes. */
        if (xx_imp_check16(record, (size_t)record_total, 0x24U) !=
            xx_imp_le16(record + 0x24)) {
            goto done;
        }

        stream_base = (int64_t)xx_imp_le32(record + 4);
        decoded_size = (int64_t)xx_imp_le32(record + 0x10);
        /* The solid stream must be inside the file and must carry its own
         * signature. The codec skips six bytes there without checking them,
         * so this is the only thing standing between a wrong stream base and
         * a block driver started on arbitrary bytes. Only the three-letter
         * prefix is required: the exact six-byte spelling is the codec's
         * documented contract rather than something any writer confirms. */
        if (!xx_imp_range_within(span, stream_base, 6)) goto done;
        if (!xx_imp_read_at(self, self->base_address + stream_base, tag, 6)) {
            goto done;
        }
        if (tag[0] != (uint8_t)'I' || tag[1] != (uint8_t)'M' ||
            tag[2] != (uint8_t)'P') {
            goto done;
        }
        if (decoded_size > XX_IMP_MAX_DECODED) goto done;

        if (name_length > XX_IMP_MAX_NAME) goto done;
        for (index = 0; index < name_length; ++index) {
            uint8_t byte = record[XX_IMP_RECORD_SIZE + index];

            if (byte == 0U) break;
            if (byte == (uint8_t)'\\') byte = (uint8_t)'/';
            if (!xx_imp_name_byte_ok(byte)) goto done;
            name[index] = (char)byte;
        }
        name[index] = '\0';
        /* An unnamed member cannot be listed or written out. */
        if (index == 0) goto done;

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto done;
        /* The record has no file offset of its own; see the file comment. */
        member.header_offset = self->base_address + directory_offset;
        member.header_size = record_total;
        member.data_offset = self->base_address + stream_base;
        /* The stream is solid, so a member has no packed size of its own:
         * what it costs to reach it is the whole stream from its base. */
        member.compressed_size = span - stream_base;
        member.uncompressed_size = decoded_size;
        /* IMP has no per-member method number -- the method is chosen per
         * block inside the stream. The attribute byte is the only per-member
         * coder selector the container has (bits 1..2 pick the x86 branch
         * converter), so it is what is published raw here. */
        member.method = (uint32_t)record[0x0B];
        member.timestamp =
            (uint64_t)xx_imp_le16(record + 0x20) |
            ((uint64_t)xx_imp_le16(record + 0x22) << 16);
        member.is_folder = false;
        if (!xx_imp_add(stream, &member)) {
            xx_str_free(member.name);
            goto done;
        }

        if (records) {
            xx_mem_zero(&entry, sizeof(entry));
            entry.stream_base = stream_base;
            entry.stream_offset = (int64_t)xx_imp_le32(record + 0x0C);
            entry.decoded_size = decoded_size;
            entry.attributes = record[0x0B];
            if (!xx_imp_record_add(records, record_count, &entry)) goto done;
        }

        position += (size_t)record_total;
    }

    stream->archive_size = span;
    result = true;

done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    xx_mem_free(chunk_sizes);
    xx_str_free(name);
    return result;
}

static xx_imp_stream *xx_imp_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_imp_stream *stream = (xx_imp_stream *)xx_mem_alloc(sizeof(*stream));

    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    if (!xx_imp_walk(self, pd, stream, NULL, NULL)) goto fail;
    return stream;

fail:
    xx_imp_stream_free(stream);
    return NULL;
}


/* Compare a published member against one the walk just rebuilt. The two come
 * from the same pass over the same bytes, so equality across every published
 * field is equality of the record -- and where it is not (two records alike
 * in all of them but pointing at different places in the solid stream) the
 * decode refuses rather than guessing, because picking the wrong one would
 * hand back a plausible slice of some other file. */
static bool xx_imp_same_member(const xx_imp_member *left,
                               const xx_imp_member *right) {
    size_t length;

    if (left->data_offset != right->data_offset) return false;
    if (left->uncompressed_size != right->uncompressed_size) return false;
    if (left->compressed_size != right->compressed_size) return false;
    if (left->header_size != right->header_size) return false;
    if (left->method != right->method) return false;
    if (left->timestamp != right->timestamp) return false;
    if (!left->name || !right->name) return false;
    length = xx_rt_strlen(left->name);
    if (length != xx_rt_strlen(right->name)) return false;
    return xx_rt_memcmp(left->name, right->name, length) == 0;
}

static bool xx_imp_decode(Abstractformat *self, const xx_imp_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    xx_imp_stream *rebuilt = NULL;
    xx_imp_record *records = NULL;
    size_t record_count = 0U;
    size_t target = 0U;
    size_t matches = 0U;
    size_t index;
    size_t written = 0U;
    size_t plain_size;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    int64_t total;
    int64_t span;
    int64_t stream_size;

    *out = NULL;
    *out_size = 0U;
    if (!self || !self->device || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->uncompressed_size < 0) return false;
    /* The stored decoded size is attacker-controlled, so it is capped before
     * it becomes an allocation. */
    if (member->uncompressed_size > XX_IMP_MAX_DECODED) return false;
    plain_size = (size_t)member->uncompressed_size;

    if (plain_size == 0U) {
        /* An empty member has no slice to take. xx_mem_alloc(0) returns
         * NULL, which the caller cannot tell from a failure, so it still
         * gets one byte. */
        plain = (uint8_t *)xx_mem_alloc((size_t)1);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }

    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;

    /* The directory is compressed and the generator hands the decode one
     * member with no way back to the member list, so the record facts the
     * member struct cannot carry -- the offset inside the decoded solid
     * stream above all -- are recovered by re-running the walk. */
    rebuilt = (xx_imp_stream *)xx_mem_alloc(sizeof(*rebuilt));
    if (!rebuilt) return false;
    xx_mem_zero(rebuilt, sizeof(*rebuilt));
    if (!xx_imp_walk(self, pd, rebuilt, &records, &record_count)) goto fail;
    if (record_count != rebuilt->count) goto fail;

    for (index = 0U; index < rebuilt->count; ++index) {
        if (xx_imp_same_member(member, &rebuilt->items[index])) {
            target = index;
            ++matches;
        }
    }
    if (matches != 1U) goto fail;
    if (records[target].decoded_size != (int64_t)plain_size) goto fail;
    /* The attribute byte is published as the method, so a member whose two
     * copies disagree has been tampered with between listing and extraction
     * -- and that byte chooses whether an x86 branch converter is undone. */
    if ((uint32_t)records[target].attributes != member->method) goto fail;

    stream_size = span - records[target].stream_base;
    if (stream_size < 6) goto fail;
    if (stream_size > XX_IMP_MAX_STREAM) goto fail;
    packed = (uint8_t *)xx_mem_alloc((size_t)stream_size);
    if (!packed) goto fail;
    if (!xx_imp_read_at(self,
                        self->base_address + records[target].stream_base,
                        packed, (size_t)stream_size)) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) goto fail;
    /* The codec drives the whole block chain from the stream's signature up
     * to the wanted offset; there is no per-member entry point, and no
     * method switch here, because IMP chooses its method per block. A block
     * whose method this codec does not implement fails inside it. */
    if (!xx_imp_decode_memory(packed, (size_t)stream_size,
                              (uint64_t)records[target].stream_offset,
                              records[target].attributes, plain, plain_size,
                              &written)) {
        goto fail;
    }
    /* Never true with fewer bytes than the record promised: a solid slice
     * that stopped early looks like ordinary file data. */
    if (written != plain_size) goto fail;

    xx_mem_free(packed);
    xx_mem_free(records);
    xx_imp_stream_free(rebuilt);
    *out = plain;
    *out_size = written;
    return true;

fail:
    xx_mem_free(plain);
    xx_mem_free(packed);
    xx_mem_free(records);
    xx_imp_stream_free(rebuilt);
    return false;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_imp_init(xx_imp *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_IMP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-imp");
    xx_format_set_extension(&archive->format, "imp");
    archive->format.check_is_valid = xx_imp_check_is_valid;
    archive->format.handle_base_info = xx_imp_handle_base_info;
    archive->format.get_format_size = xx_imp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_imp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_imp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_imp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_imp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_imp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_imp_free_archive_records_reading;
    archive->format.destroy = xx_imp_vtable_destroy;
}

xx_imp *xx_imp_create(xx_io_device *device, int64_t base_address) {
    xx_imp *archive = (xx_imp *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_imp_init(archive, device, base_address);
    return archive;
}

void xx_imp_destroy(xx_imp *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_imp_free(xx_imp *archive) {
    if (!archive) return;
    xx_imp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_imp_vtable_destroy(Abstractformat *self) {
    xx_imp_destroy((xx_imp *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_imp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_imp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_imp_parse(self, pd);
    if (!stream) return false;
    xx_imp_stream_free(stream);
    return true;
}

bool xx_imp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_imp *archive = (xx_imp *)self;
    xx_imp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_imp_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_imp_stream_free(stream);
    return true;
}

int64_t xx_imp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_imp_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_imp *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_imp_set_record(xx_archive_record *record,
                                 const xx_imp_member *member) {
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

static bool xx_imp_copy_options(xx_list_s *target,
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

static const xx_var *xx_imp_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_imp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_imp_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_imp_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_imp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_imp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_imp_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_imp_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_imp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_imp_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_imp_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_imp_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_imp_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_imp_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_imp_stream *stream;
    const xx_imp_member *member;
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
    stream = (xx_imp_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_imp_path_safe(member->name)) return false;

    path_option = xx_imp_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_imp_decode(self, member, &plain, &plain_size, pd);
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
        !xx_imp_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_imp_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
