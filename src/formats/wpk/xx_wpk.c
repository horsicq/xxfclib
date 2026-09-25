/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * WPK - the Watcom installer "pack" archive.
 *
 *   header, 0x0c bytes at offset 0:
 *     0x00  u32 LE  magic, 0x01012403 or 0x01332403
 *     0x04  u16 LE  number of members
 *     0x06  u16 LE  directory size in bytes
 *     0x08  u32 LE  directory offset; directory_offset + directory_size is
 *                   exactly the file size, so the directory CLOSES the file
 *
 *   payload area, 0x0c .. directory_offset
 *
 *   directory record, 0x11 bytes plus the name:
 *     0x00  u32 LE  uncompressed size
 *     0x04  u32 LE  data offset, inside the payload area
 *     0x08  u32 LE  timestamp
 *     0x0c  u32 LE  CRC-32 of the CONSUMED compressed prefix, not of the
 *                   plaintext and not of the whole stored run
 *     0x10  u8      flags: bit 7 is the method (clear -> A, set -> B),
 *                   bits 0..6 are the length of the name that follows
 *
 * A member stores no compressed length. Its stream simply begins at its data
 * offset and the decoder stops itself, so compressed_size here is the whole
 * run from the member's data offset to the start of the directory - members
 * overlap, and the figure is an upper bound rather than an extent.
 *
 * Both methods are LZSS over a 4096-byte ring buffer preset to ' ', MSB
 * first, sharing LHA's "-lh1-" position tables for the distance. Method A
 * adds a Huffman literal/length alphabet whose code lengths are transmitted
 * first; method B has no Huffman stage.
 *
 * THE CODE-LENGTH SORT IS UNSTABLE AND ITS TIE-BREAK IS PART OF THE FORMAT,
 * and which of the three tie-breaks an archive wants is NOT recorded
 * anywhere in it - the magic does not discriminate. The archive-wide probe
 * below is therefore mandatory, not an optimisation: 5 of the 127 reference
 * archives need sorter B, and under sorter A they decode to plausible
 * garbage that no length or bounds check can catch. The stored CRC over the
 * consumed compressed prefix is the only oracle that tells the two apart.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wpk/xx_wpk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/wpk/xx_wpk.h"
#include "xxfclib/algo/crc/xx_crc.h"

#include <stdio.h>

#define XX_WPK_COPY_CHUNK (64 * 1024)

typedef struct xx_wpk_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_wpk_member;

typedef struct xx_wpk_stream_s {
    xx_wpk_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_wpk_stream;

static void xx_wpk_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_wpk_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_wpk_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_wpk_path_safe(const char *name) {
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

static void xx_wpk_stream_free(void *pointer) {
    xx_wpk_stream *stream = (xx_wpk_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_wpk_add(xx_wpk_stream *stream,
                          const xx_wpk_member *member) {
    xx_wpk_member *grown = (xx_wpk_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_WPK_HEADER_SIZE 0x0c
#define XX_WPK_RECORD_SIZE 0x11
#define XX_WPK_MAGIC_A 0x01012403U
#define XX_WPK_MAGIC_B 0x01332403U
#define XX_WPK_MAX_MEMBERS 65535
#define XX_WPK_MAX_DIRECTORY_SIZE ((int64_t)0x10000)
#define XX_WPK_MAX_DECODED ((int64_t)0x10000000)
#define XX_WPK_PROBE_MEMBERS 3
#define XX_WPK_SORTER_COUNT 3
#define XX_WPK_METHOD_A 0U
#define XX_WPK_METHOD_B 1U

typedef struct xx_wpk_record_s {
    int64_t record_offset;  /* relative to base_address */
    int64_t header_size;
    int64_t data_offset;    /* relative to base_address */
    int64_t compressed_size;
    int64_t uncompressed_size;
    int64_t name_offset;    /* into the directory buffer */
    int64_t name_size;
    uint32_t crc;
    uint32_t timestamp;
    bool method_b;
} xx_wpk_record;
typedef struct xx_wpk_dir_s {
    uint8_t *bytes;
    int64_t size;
    int64_t offset;   /* relative to base_address */
    int64_t span;
    uint32_t magic;
    xx_wpk_record *records;
    size_t count;
} xx_wpk_dir;

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_wpk_le16(const uint8_t *data);
static uint32_t xx_wpk_le32(const uint8_t *data);
static void xx_wpk_dir_free(xx_wpk_dir *dir);
static bool xx_wpk_dir_read(Abstractformat *self, xx_pd_struct *pd, xx_wpk_dir *dir);
static xx_wpk_stream *xx_wpk_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_wpk_resolve_sorter(Abstractformat *self, xx_pd_struct *pd);
static bool xx_wpk_decode(Abstractformat *self, const xx_wpk_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Plain integer constants, not int64_t casts: the header size also sizes a
 * stack array, and MSVC will not take a 64-bit constant there without a
 * conversion warning, which this build treats as an error. */

/* How many method A members the sorter probe decodes. One is what the
 * reference does; a handful costs little and covers a first member whose code
 * lengths carry no ties, where both sorts agree and the wrong one wins by
 * accident. */

/* The container's own method, which is a single flag bit: clear is the
 * Huffman + LZSS method A, set is the plain LZSS method B. */





static uint16_t xx_wpk_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_wpk_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void xx_wpk_dir_free(xx_wpk_dir *dir) {
    if (!dir) return;
    if (dir->bytes) xx_mem_free(dir->bytes);
    if (dir->records) xx_mem_free(dir->records);
    dir->bytes = NULL;
    dir->records = NULL;
    dir->count = 0U;
}

/* Read the header and the whole directory, and decode every record.
 *
 * Shared by the parse and by the sorter probe, because the probe needs the
 * per-record CRC and the member struct has nowhere to keep it. */
static bool xx_wpk_dir_read(Abstractformat *self, xx_pd_struct *pd,
                            xx_wpk_dir *dir) {
    uint8_t header[XX_WPK_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t position;
    int32_t count;
    int32_t index;

    if (!self || !self->device || self->base_address < 0 || !dir) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < XX_WPK_HEADER_SIZE) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    if (!xx_wpk_read_at(self, self->base_address, header, sizeof(header))) {
        return false;
    }

    dir->magic = xx_wpk_le32(header);
    /* Two magics, and they do NOT tell the two sorters apart - see the probe.
     * They are only the format gate. */
    if ((dir->magic != XX_WPK_MAGIC_A) && (dir->magic != XX_WPK_MAGIC_B)) {
        return false;
    }

    count = (int32_t)xx_wpk_le16(header + 4);
    directory_size = (int64_t)xx_wpk_le16(header + 6);
    directory_offset = (int64_t)xx_wpk_le32(header + 8);

    if ((count == 0) || (count > XX_WPK_MAX_MEMBERS)) return false;
    if (directory_offset <= XX_WPK_HEADER_SIZE) return false;
    /* The directory CLOSES the file. This exact-fit test is the format's
     * strongest structural check by far - a 32-bit magic plus a geometry that
     * has to land on the last byte is what keeps this from matching noise -
     * and it is the one a later reader will be tempted to loosen to "fits
     * inside the file" so that appended data still parses. It must not be. */
    if ((directory_size + directory_offset) != span) return false;
    /* Every record costs 0x11 header bytes plus at least one name byte, so
     * the directory has to be strictly larger than that product. */
    if ((((int64_t)count) * (XX_WPK_RECORD_SIZE + 1)) >= directory_size) {
        return false;
    }
    if (directory_size > XX_WPK_MAX_DIRECTORY_SIZE) return false;
    if (!xx_wpk_range_within(span, directory_offset, directory_size)) {
        return false;
    }

    dir->bytes = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!dir->bytes) return false;
    if (!xx_wpk_read_at(self, self->base_address + directory_offset, dir->bytes,
                        (size_t)directory_size)) {
        xx_wpk_dir_free(dir);
        return false;
    }
    dir->size = directory_size;
    dir->offset = directory_offset;
    dir->span = span;

    dir->records =
        (xx_wpk_record *)xx_mem_alloc(sizeof(xx_wpk_record) * (size_t)count);
    if (!dir->records) {
        xx_wpk_dir_free(dir);
        return false;
    }
    xx_mem_zero(dir->records, sizeof(xx_wpk_record) * (size_t)count);

    position = 0;
    for (index = 0; index < count; ++index) {
        xx_wpk_record *record = &dir->records[index];
        int64_t record_offset;
        int64_t name_size;
        uint8_t flags;

        if (pd && xx_pd_is_stopped(pd)) {
            xx_wpk_dir_free(dir);
            return false;
        }
        /* Strictly more than 0x10 bytes left: the fixed part is 0x11 wide. */
        if ((directory_size - position) <= 0x10) {
            xx_wpk_dir_free(dir);
            return false;
        }

        record_offset = directory_offset + position;
        record->uncompressed_size =
            (int64_t)(int32_t)xx_wpk_le32(dir->bytes + position);
        record->data_offset =
            (int64_t)(int32_t)xx_wpk_le32(dir->bytes + position + 4);
        record->timestamp = xx_wpk_le32(dir->bytes + position + 8);
        record->crc = xx_wpk_le32(dir->bytes + position + 0x0c);
        flags = dir->bytes[position + 0x10];
        name_size = (int64_t)(flags & 0x7fU);
        record->method_b = ((flags & 0x80U) != 0U);

        position += XX_WPK_RECORD_SIZE;

        /* Both sizes are written as u32 but read as signed; a negative one is
         * a rejection, not a four-gigabyte value. */
        if ((record->uncompressed_size < 0) || (record->data_offset < 0) ||
            (name_size == 0) || (name_size > (directory_size - position))) {
            xx_wpk_dir_free(dir);
            return false;
        }
        /* The payload area is bounded by the DIRECTORY, never by the file end:
         * a member's stream runs from its data offset up to where the
         * directory begins. */
        if ((record->data_offset < XX_WPK_HEADER_SIZE) ||
            (record->data_offset > directory_offset)) {
            xx_wpk_dir_free(dir);
            return false;
        }

        record->record_offset = record_offset;
        record->header_size = XX_WPK_RECORD_SIZE + name_size;
        record->compressed_size = directory_offset - record->data_offset;
        record->name_offset = position;
        record->name_size = name_size;

        position += name_size;
        ++dir->count;
    }

    return true;
}

static xx_wpk_stream *xx_wpk_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_wpk_stream *stream = NULL;
    xx_wpk_member member;
    xx_wpk_dir dir;
    size_t index;
    char *name = NULL;

    if (!self) return NULL;
    xx_mem_zero(&dir, sizeof(dir));
    if (!xx_wpk_dir_read(self, pd, &dir)) return NULL;

    stream = (xx_wpk_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_wpk_dir_free(&dir);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0U; index < dir.count; ++index) {
        const xx_wpk_record *record = &dir.records[index];
        int64_t position;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_WPK_MAX_MEMBERS) goto fail;
        if (!xx_wpk_range_within(dir.span, record->data_offset,
                                 record->compressed_size)) {
            goto fail;
        }
        if (record->uncompressed_size > XX_WPK_MAX_DECODED) goto fail;

        name = (char *)xx_mem_alloc((size_t)record->name_size + 1U);
        if (!name) goto fail;
        for (position = 0; position < record->name_size; ++position) {
            uint8_t byte = dir.bytes[record->name_offset + position];

            /* The installer writes DOS paths with backslashes; the extraction
             * layer only understands '/'. */
            if (byte == (uint8_t)'\\') byte = (uint8_t)'/';
            /* WPK names are plain DOS 8.3 path components. A byte outside
             * printable ASCII means the record walk has lost its place, and
             * accepting it would publish a member whose extent came from
             * misread fields. */
            if ((byte < 0x20U) || (byte > 0x7eU)) goto fail;
            name[position] = (char)byte;
        }
        name[record->name_size] = '\0';

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + record->record_offset;
        member.header_size = record->header_size;
        member.data_offset = self->base_address + record->data_offset;
        /* No compressed length is stored. This is the run from the member's
         * data offset to the directory, which is an upper bound the decoder
         * stops inside - members therefore overlap, and that is correct. */
        member.compressed_size = record->compressed_size;
        member.uncompressed_size = record->uncompressed_size;
        member.method = record->method_b ? XX_WPK_METHOD_B : XX_WPK_METHOD_A;
        member.timestamp = (uint64_t)record->timestamp;
        member.is_folder = false;

        if (!xx_wpk_add(stream, &member)) goto fail;
        name = NULL;
    }

    if (stream->count == 0U) goto fail;

    /* The directory closes the file, so the archive is the whole span. */
    stream->archive_size = dir.span;
    xx_wpk_dir_free(&dir);
    return stream;

fail:
    if (name) xx_str_free(name);
    xx_wpk_dir_free(&dir);
    xx_wpk_stream_free(stream);
    return NULL;
}


/* Resolve the archive-wide sorter, once.
 *
 * The reference probes in exactly this way and for exactly this reason: the
 * container records nothing about the tie-break, so the only way to learn it
 * is to decode a member under each candidate and check the stored CRC of the
 * prefix the decode consumed. Candidates are tried in the order the magic
 * suggests, so a well-formed archive settles on its first try.
 *
 * This is the one piece of state decode touches. It is a memo of a pure
 * function of the device bytes: running it again always produces the same
 * answer, so a decode called twice still returns identical bytes. */
static bool xx_wpk_resolve_sorter(Abstractformat *self, xx_pd_struct *pd) {
    xx_wpk *archive = (xx_wpk *)self;
    xx_wpk_dir dir;
    uint8_t *packed[XX_WPK_PROBE_MEMBERS];
    size_t probe[XX_WPK_PROBE_MEMBERS];
    int candidates[XX_WPK_SORTER_COUNT];
    size_t probe_count = 0U;
    size_t index;
    int candidate;
    bool ok = true;

    if (!archive) return false;
    if (archive->sorter_resolved) return true;

    xx_mem_zero(&dir, sizeof(dir));
    if (!xx_wpk_dir_read(self, pd, &dir)) return false;

    /* The starting candidate only; the probe below is what settles it. */
    archive->sorter = (dir.magic == XX_WPK_MAGIC_B) ? (uint32_t)XX_WPK_SORTER_B
                                                    : (uint32_t)XX_WPK_SORTER_A;

    for (index = 0U; index < XX_WPK_PROBE_MEMBERS; ++index) {
        packed[index] = NULL;
    }

    /* The smallest method A members, ascending, by bounded insertion - small
     * ones make the trial decodes cheap. Method B has no Huffman stage and so
     * no sort at all; an archive of nothing but method B members never needs
     * an answer. */
    for (index = 0U; index < dir.count; ++index) {
        const xx_wpk_record *record = &dir.records[index];
        size_t position;
        size_t last;
        size_t step;

        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        if (record->method_b || (record->uncompressed_size <= 0) ||
            (record->compressed_size <= 0)) {
            continue;
        }

        position = probe_count;
        while ((position > 0U) &&
               (dir.records[probe[position - 1U]].uncompressed_size >
                record->uncompressed_size)) {
            --position;
        }
        if (position >= (size_t)XX_WPK_PROBE_MEMBERS) continue;

        last = (probe_count < (size_t)XX_WPK_PROBE_MEMBERS)
                   ? probe_count
                   : (size_t)(XX_WPK_PROBE_MEMBERS - 1);
        for (step = last; step > position; --step) {
            probe[step] = probe[step - 1U];
        }
        probe[position] = index;
        if (probe_count < (size_t)XX_WPK_PROBE_MEMBERS) ++probe_count;
    }

    if (ok && (probe_count > 0U)) {
        for (index = 0U; index < probe_count; ++index) {
            const xx_wpk_record *record = &dir.records[probe[index]];

            packed[index] = (uint8_t *)xx_mem_alloc(
                (size_t)record->compressed_size);
            if (!packed[index]) {
                ok = false;
                break;
            }
            if (!xx_wpk_read_at(self, self->base_address + record->data_offset,
                                packed[index],
                                (size_t)record->compressed_size)) {
                ok = false;
                break;
            }
        }
    }

    if (ok && (probe_count > 0U)) {
        if (dir.magic == XX_WPK_MAGIC_B) {
            candidates[0] = XX_WPK_SORTER_B;
            candidates[1] = XX_WPK_SORTER_A;
        } else {
            candidates[0] = XX_WPK_SORTER_A;
            candidates[1] = XX_WPK_SORTER_B;
        }
        candidates[2] = XX_WPK_SORTER_STABLE;

        for (candidate = 0; candidate < XX_WPK_SORTER_COUNT; ++candidate) {
            bool accepted = true;

            for (index = 0U; index < probe_count; ++index) {
                const xx_wpk_record *record = &dir.records[probe[index]];
                uint8_t *plain;
                size_t written = 0U;
                size_t consumed = 0U;

                if (pd && xx_pd_is_stopped(pd)) {
                    accepted = false;
                    ok = false;
                    break;
                }
                plain = (uint8_t *)xx_mem_alloc(
                    (size_t)record->uncompressed_size);
                if (!plain) {
                    accepted = false;
                    ok = false;
                    break;
                }
                if (!xx_wpk_decode_method_a_memory(
                        packed[index], (size_t)record->compressed_size,
                        candidates[candidate], plain,
                        (size_t)record->uncompressed_size, &written,
                        &consumed) ||
                    (written != (size_t)record->uncompressed_size) ||
                    (consumed == 0U) ||
                    (consumed > (size_t)record->compressed_size)) {
                    xx_mem_free(plain);
                    accepted = false;
                    break;
                }
                xx_mem_free(plain);

                /* The stored CRC covers the CONSUMED compressed prefix, not
                 * the plaintext and not the whole run to the directory. This
                 * is the entire discriminating power the format offers - CRC
                 * the wrong span and every candidate fails equally. */
                if (xx_crc32(XX_CRC_TYPE_CRC32, packed[index], consumed) !=
                    record->crc) {
                    accepted = false;
                    break;
                }
            }
            if (!ok) break;
            if (accepted) {
                archive->sorter = (uint32_t)candidates[candidate];
                break;
            }
        }
    }

    for (index = 0U; index < XX_WPK_PROBE_MEMBERS; ++index) {
        if (packed[index]) xx_mem_free(packed[index]);
    }
    xx_wpk_dir_free(&dir);

    /* No candidate matching is not a failure: an archive whose only method A
     * members are unreadable still lists, and its method B members still
     * extract. The magic's guess stays in place, exactly as the reference
     * leaves it. */
    if (ok) archive->sorter_resolved = true;
    return ok;
}

static bool xx_wpk_decode(Abstractformat *self, const xx_wpk_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    size_t consumed = 0U;
    bool decoded;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if ((member->method != XX_WPK_METHOD_A) &&
        (member->method != XX_WPK_METHOD_B)) {
        return false;
    }
    if ((member->compressed_size <= 0) || (member->uncompressed_size <= 0)) {
        return false;
    }
    if (member->compressed_size > XX_WPK_MAX_DECODED) return false;
    if (member->uncompressed_size > XX_WPK_MAX_DECODED) return false;

    /* Method A cannot be decoded at all until the archive-wide probe has run;
     * method B has no sort, but resolving up front keeps the two paths from
     * drifting apart. */
    if (!xx_wpk_resolve_sorter(self, pd)) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_wpk_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_WPK_METHOD_B) {
        decoded = xx_wpk_decode_method_b_memory(
            input, (size_t)member->compressed_size, output,
            (size_t)member->uncompressed_size, &written, &consumed);
    } else {
        decoded = xx_wpk_decode_method_a_memory(
            input, (size_t)member->compressed_size,
            (int)((xx_wpk *)self)->sorter, output,
            (size_t)member->uncompressed_size, &written, &consumed);
    }

    if (!decoded || (written != (size_t)member->uncompressed_size)) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);

    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_wpk_init(xx_wpk *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_WPK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-wpk");
    xx_format_set_extension(&archive->format, "wpk");
    archive->format.check_is_valid = xx_wpk_check_is_valid;
    archive->format.handle_base_info = xx_wpk_handle_base_info;
    archive->format.get_format_size = xx_wpk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_wpk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_wpk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_wpk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_wpk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_wpk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_wpk_free_archive_records_reading;
    archive->format.destroy = xx_wpk_vtable_destroy;
}

xx_wpk *xx_wpk_create(xx_io_device *device, int64_t base_address) {
    xx_wpk *archive = (xx_wpk *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_wpk_init(archive, device, base_address);
    return archive;
}

void xx_wpk_destroy(xx_wpk *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_wpk_free(xx_wpk *archive) {
    if (!archive) return;
    xx_wpk_destroy(archive);
    xx_mem_free(archive);
}

static void xx_wpk_vtable_destroy(Abstractformat *self) {
    xx_wpk_destroy((xx_wpk *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_wpk_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_wpk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_wpk_parse(self, pd);
    if (!stream) return false;
    xx_wpk_stream_free(stream);
    return true;
}

bool xx_wpk_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_wpk *archive = (xx_wpk *)self;
    xx_wpk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_wpk_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_wpk_stream_free(stream);
    return true;
}

int64_t xx_wpk_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_wpk_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_wpk *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_wpk_set_record(xx_archive_record *record,
                                 const xx_wpk_member *member) {
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

static bool xx_wpk_copy_options(xx_list_s *target,
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

static const xx_var *xx_wpk_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_wpk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_wpk_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_wpk_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_wpk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_wpk_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_wpk_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_wpk_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_wpk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_wpk_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_wpk_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_wpk_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_wpk_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_wpk_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_wpk_stream *stream;
    const xx_wpk_member *member;
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
    stream = (xx_wpk_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_wpk_path_safe(member->name)) return false;

    path_option = xx_wpk_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_wpk_decode(self, member, &plain, &plain_size, pd);
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
        !xx_wpk_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_wpk_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
