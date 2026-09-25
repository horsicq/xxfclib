/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Computer Associates' CAZIP and CAZIPXP.  Despite the shared name these are
 * two unrelated containers, and the corpora keep them apart cleanly, so the
 * reader dispatches on the signature and then runs one of two parsers.
 *
 * No published description of either layout was found; everything below was
 * derived from F:\ARC\ARC\CAZIP (408 files) and F:\ARC\ARC\CAZIPXP (3 files)
 * and is labelled with how confident the derivation is.
 *
 * ---------------------------------------------------------------- CAZIP --
 * A 20-byte binary header followed by one nameless stream that runs to EOF:
 *
 *   0x00  0x0D 0x0A 0x1A          lead-in (the usual "TYPE-proof" prefix)
 *   0x03  "CAZIP"                 signature
 *   0x08  two ASCII digits        version; "33" throughout the corpus
 *   0x0A  u16                     always 1
 *   0x0C  u16                     volume flag: 1 (406 files) or 2 (2 files)
 *   0x0E  u32 LE                  CRC-32 of the plaintext (the ordinary
 *                                 reflected PKZIP/ISO-HDLC CRC-32) - it is
 *                                 CERTAINLY not a size: it is uncorrelated
 *                                 with the file length
 *   0x12  u8 u8                   always zero
 *   0x14  ...                     payload, to end of file
 *
 * The payload is a PKWARE Data Compression Library "implode" stream, used
 * whole and unmodified - which is why the header ends at 0x14 rather than at
 * 0x16.  What earlier looked like a third zero byte at 0x14 and a "method"
 * byte at 0x15 is in fact the DCL stream's own two-byte preamble: the literal
 * mode (0 = uncoded literals, the only value the corpus uses) and the
 * dictionary-size exponent, whose legal values 4, 5 and 6 are exactly the
 * three "methods" seen here.  So there is no CAZIP method field at all; the
 * value published as the record's method is that dictionary exponent, read
 * back out of the stream.
 *
 * That identification came from decoding the bitstream by hand - LSB-first,
 * a 0 flag bit introducing an eight-bit literal - which recovers "BM" and a
 * BMP header from the .BM_ samples, and it is confirmed end to end: 406 of
 * the 408 samples decode with the shared DCL decoder consuming the payload to
 * the last byte, and the CRC-32 of every decode matches the word at 0x0E.
 * The two that do not are the only two with the 0x0C word set to 2: both are
 * exactly 1457664 bytes, a 1.44 MB diskette image, and both are the first
 * volume of a set whose remaining volumes are not in the corpus.  Their
 * streams are therefore genuinely truncated, and they fail closed.
 *
 * No plaintext length is stored anywhere, so it is measured by scanning the
 * stream (xx_dcl_scan_memory) rather than trusted from a header field; the
 * scan doubles as a check that the stream ends exactly at end-of-file.  The
 * original file name is not stored either - the packer only rewrote the last
 * extension character - so the member carries a fixed placeholder name.
 *
 * -------------------------------------------------------------- CAZIPXP --
 * A text-framed archive.  Numbers are ASCII decimal, fields are separated by
 * spaces, a record's name is terminated by 0x04 and its field line by NUL:
 *
 *   "CAZIP" 0x04 <archive fields> 0x00 <number> 0x00
 *   then, repeatedly:
 *     <sep byte> <name> 0x04 <mode> ' ' <f1> ' ' <size> ' ' <atime> ' '
 *       <mtime> ' ' <ctime> ' ' <csize> ' ' <f7> 0x00 <csize bytes> ...
 *       <checksum> 0x00
 *
 * mode is a POSIX st_mode in decimal (33206 = regular 0666, 16895 = directory
 * 0777), size is the plaintext length and csize the length of the FIRST
 * compressed chunk.  Long members are split about every 16383 plaintext
 * bytes: after each chunk comes a FIXED 10-byte NUL-padded ASCII record
 * holding the next chunk's compressed size and a 1/0 "more follow" flag.
 * A chunk record is told from a member's closing checksum by field count -
 * two numbers versus one - so the split threshold never has to be assumed.
 * The last member of an archive ends at EOF with no trailing checksum.
 *
 * Each chunk is a complete Unix compress (.Z, 0x1F 0x9D) stream, so members
 * decode with the shared xx_compress decoder.  Very short members are stored
 * verbatim instead, recognisable because the chunk does not begin with the
 * .Z signature; those are accepted only when csize equals size exactly.
 *
 * Confident: the signature, the 0x04/NUL framing, the eight-field record
 * line, the meaning of mode/size/csize, the three timestamps being Unix
 * seconds, the chunking rule and the 10-byte chunk record - all three corpus
 * archives parse to EOF byte-exactly under these rules and every member's
 * chunks decompress to exactly the declared size.  Inferred: that the
 * trailing number is a checksum, and that the one byte before each name is a
 * flag (0x7F, 0xFF and 0x00 are all observed).  Both are skipped, not
 * validated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cazip/xx_cazip.h"

#include "xxfclib/algo/compress/xx_compress.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The enumerator is added by the coordinator, not by this file.  Until it
 * exists the reader still compiles and simply reports UNKNOWN.  Delete this
 * block once XX_FILE_TYPE_CAZIP is in the enum. */
#ifdef CAZIP
#define XX_CAZIP_FILE_TYPE XX_FILE_TYPE_CAZIP
#else
#define XX_CAZIP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Twenty bytes, not twenty-two: the two that follow belong to the DCL stream
 * itself and are handed to the decoder along with the rest of it. */
#define XX_CAZIP_CLASSIC_HEADER 20
#define XX_CAZIP_CLASSIC_NAME "cazip.bin"
/* The DCL preamble, read from the payload rather than from the header. */
#define XX_CAZIP_DCL_LITERAL_OFFSET 0
#define XX_CAZIP_DCL_DICT_OFFSET 1
#define XX_CAZIP_DCL_PREAMBLE 2
#define XX_CAZIP_DCL_DICT_MIN 4U
#define XX_CAZIP_DCL_DICT_MAX 6U
/* Ceiling on what a classic stream may be measured or decoded to.  The
 * largest corpus plaintext is a little over a megabyte; the stream stores no
 * length at all, so this is the only thing bounding the allocation. */
#define XX_CAZIP_CLASSIC_MAX_PLAIN ((size_t)256 * 1024 * 1024)

#define XX_CAZIP_XP_MAX_NAME 255U
#define XX_CAZIP_XP_MAX_FIELDS 200U
#define XX_CAZIP_XP_MAX_TRAILER 20U
#define XX_CAZIP_XP_CHUNK_RECORD 10U
/* The observed plaintext split threshold, recorded for documentation only:
 * the parser decides chunking from the bytes, not from this. */
#define XX_CAZIP_XP_CHUNK_PLAIN 16383
#define XX_CAZIP_XP_MAX_MEMBERS 262144U
/* No corpus member exceeds 1 MiB of plaintext; this is a safety ceiling for
 * the decode allocation, not a format limit. */
#define XX_CAZIP_XP_MAX_PLAIN ((int64_t)256 * 1024 * 1024)
/* POSIX S_IFMT / S_IFDIR, spelled out because this is a foreign mode word. */
#define XX_CAZIP_XP_IFMT 0170000U
#define XX_CAZIP_XP_IFDIR 0040000U

#define XX_CAZIP_METHOD_STORE 0U
#define XX_CAZIP_METHOD_COMPRESS 1U
/* Classic methods are published as 0x10 + the DCL dictionary exponent 4/5/6
 * so they cannot be confused with the two CAZIPXP methods above. */
#define XX_CAZIP_METHOD_CLASSIC_BASE 0x10U

typedef struct xx_cazip_chunk_s {
    int64_t offset;
    int64_t size;
} xx_cazip_chunk;

typedef struct xx_cazip_member_s {
    char *name;
    xx_cazip_chunk *chunks;
    size_t chunk_count;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size; /* -1 when the container does not record it */
    uint32_t method;
    uint32_t mode;
    uint64_t timestamp;
    uint32_t crc32;    /* meaningful only when has_crc32 is set */
    bool has_crc32;
    bool is_folder;
} xx_cazip_member;

typedef struct xx_cazip_stream_s {
    xx_cazip_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t variant;
} xx_cazip_stream;

static void xx_cazip_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------ helpers --- */

static uint16_t xx_cazip_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t xx_cazip_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool xx_cazip_read_at(Abstractformat *self, int64_t offset,
                             uint8_t *buffer, size_t size) {
    size_t done = 0U;

    if (!self || !self->device || offset < 0 || (!buffer && size != 0U) ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t got = xx_io_read(self->device, buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* Scan forward for @p terminator, at most @p limit bytes past @p offset.
 * The found position is returned through @p found. */
static bool xx_cazip_scan(Abstractformat *self, int64_t span, int64_t offset,
                          uint8_t terminator, int64_t limit, int64_t *found) {
    uint8_t window[64];
    int64_t cursor = offset;
    int64_t stop;

    if (offset < 0 || offset > span) return false;
    stop = offset + limit;
    if (stop > span) stop = span;
    while (cursor < stop) {
        int64_t want = stop - cursor;
        int64_t index;
        if (want > (int64_t)sizeof(window)) want = (int64_t)sizeof(window);
        if (!xx_cazip_read_at(self, self->base_address + cursor, window,
                              (size_t)want))
            return false;
        for (index = 0; index < want; ++index) {
            if (window[index] == terminator) {
                *found = cursor + index;
                return true;
            }
        }
        cursor += want;
    }
    return false;
}

/* Parse @p count space-separated non-negative decimal fields out of @p text.
 * Anything else - a sign, a letter, a missing or extra field - fails. */
static bool xx_cazip_parse_fields(const uint8_t *text, size_t size,
                                  uint64_t *values, size_t count) {
    size_t position = 0U;
    size_t index;

    for (index = 0U; index < count; ++index) {
        uint64_t value = 0U;
        size_t digits = 0U;
        while (position < size && text[position] == ' ') ++position;
        while (position < size && text[position] >= '0' &&
               text[position] <= '9') {
            if (value > (UINT64_MAX - 9U) / 10U) return false;
            value = value * 10U + (uint64_t)(text[position] - '0');
            ++position;
            ++digits;
        }
        if (digits == 0U) return false;
        values[index] = value;
    }
    while (position < size && text[position] == ' ') ++position;
    return position == size;
}

static char *xx_cazip_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U;
    size_t output = 0U;

    if ((!bytes && size != 0U) || size > 4096U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start;
        size_t end;
        size_t component;

        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && bytes[start] == '.'))
            continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|')
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool xx_cazip_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U || (length == 1U && cursor[0] == '.') ||
            (length == 2U && cursor[0] == '.' && cursor[1] == '.'))
            return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_cazip_stream_free(void *pointer) {
    xx_cazip_stream *stream = (xx_cazip_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
        xx_mem_free(stream->items[index].chunks);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_cazip_add(xx_cazip_stream *stream,
                         const xx_cazip_member *member) {
    xx_cazip_member *grown;

    if (stream->count >= XX_CAZIP_XP_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (xx_cazip_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* ------------------------------------------------------- CAZIP classic --- */

/* Read the classic payload whole.  It is needed twice - once to measure the
 * plaintext, once to produce it - and the DCL entry points are memory based,
 * so there is no streaming alternative. */
static uint8_t *xx_cazip_classic_payload(Abstractformat *self,
                                         const xx_cazip_member *member) {
    uint8_t *input;

    if (member->compressed_size < XX_CAZIP_DCL_PREAMBLE ||
        (uint64_t)member->compressed_size > XX_CAZIP_CLASSIC_MAX_PLAIN) {
        return NULL;
    }
    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return NULL;
    if (!xx_cazip_read_at(self, member->data_offset, input,
                          (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return NULL;
    }
    return input;
}

static xx_cazip_stream *xx_cazip_parse_classic(Abstractformat *self,
                                               int64_t span) {
    uint8_t header[XX_CAZIP_CLASSIC_HEADER + XX_CAZIP_DCL_PREAMBLE];
    xx_cazip_stream *stream;
    xx_cazip_member member;
    uint8_t *payload;
    uint16_t flavour;

    if (span <= (int64_t)sizeof(header)) return NULL;
    if (!xx_cazip_read_at(self, self->base_address, header, sizeof(header)))
        return NULL;
    if (header[0] != 0x0dU || header[1] != 0x0aU || header[2] != 0x1aU ||
        xx_rt_memcmp(header + 3, "CAZIP", 5U) != 0)
        return NULL;
    if (header[8] < '0' || header[8] > '9' || header[9] < '0' ||
        header[9] > '9')
        return NULL;
    if (xx_cazip_le16(header + 10) != 1U) return NULL;
    flavour = xx_cazip_le16(header + 12);
    if (flavour != 1U && flavour != 2U) return NULL;
    if (header[18] != 0U || header[19] != 0U) return NULL;
    /* The next two bytes are the DCL preamble, so they are validated as one:
     * a literal mode outside 0/1 or a dictionary exponent outside 4..6 is not
     * a stream this reader can speak for. */
    if (header[XX_CAZIP_CLASSIC_HEADER + XX_CAZIP_DCL_LITERAL_OFFSET] > 1U)
        return NULL;
    if (header[XX_CAZIP_CLASSIC_HEADER + XX_CAZIP_DCL_DICT_OFFSET] <
            XX_CAZIP_DCL_DICT_MIN ||
        header[XX_CAZIP_CLASSIC_HEADER + XX_CAZIP_DCL_DICT_OFFSET] >
            XX_CAZIP_DCL_DICT_MAX)
        return NULL;

    stream = (xx_cazip_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->variant = XX_CAZIP_VARIANT_CLASSIC;
    xx_mem_zero(&member, sizeof(member));
    member.name = xx_str_dup(XX_CAZIP_CLASSIC_NAME);
    if (!member.name) {
        xx_cazip_stream_free(stream);
        return NULL;
    }
    member.header_offset = self->base_address;
    member.header_size = XX_CAZIP_CLASSIC_HEADER;
    member.data_offset = self->base_address + XX_CAZIP_CLASSIC_HEADER;
    member.compressed_size = span - XX_CAZIP_CLASSIC_HEADER;
    member.uncompressed_size = -1;
    member.method = XX_CAZIP_METHOD_CLASSIC_BASE +
                    header[XX_CAZIP_CLASSIC_HEADER + XX_CAZIP_DCL_DICT_OFFSET];
    member.crc32 = xx_cazip_le32(header + 14);
    member.has_crc32 = true;
    member.mode = 0U;
    member.timestamp = 0U;
    member.is_folder = false;

    /* Measure the plaintext.  Nothing in the file declares it, so the only
     * way to publish a size is to run the stream; the scan keeps a window
     * rather than the output, so it costs no allocation proportional to the
     * result.  A stream that does not measure - the truncated first volume of
     * a split set is the corpus's one example - still identifies as CAZIP and
     * still reports its extent; it simply has no size and will refuse to
     * extract.  Identification must not depend on the payload decoding. */
    payload = xx_cazip_classic_payload(self, &member);
    if (payload) {
        size_t consumed = 0U;
        size_t produced = 0U;

        if (xx_dcl_scan_memory(payload, (size_t)member.compressed_size,
                               XX_CAZIP_CLASSIC_MAX_PLAIN, &consumed,
                               &produced) &&
            consumed == (size_t)member.compressed_size &&
            produced <= XX_CAZIP_CLASSIC_MAX_PLAIN) {
            member.uncompressed_size = (int64_t)produced;
        }
        xx_mem_free(payload);
    }

    if (!xx_cazip_add(stream, &member)) {
        xx_str_free(member.name);
        xx_cazip_stream_free(stream);
        return NULL;
    }
    stream->archive_size = span;
    return stream;
}

/* ------------------------------------------------------------ CAZIPXP --- */

/* Append one chunk extent, growing the array one entry at a time.  The array
 * is never sized from a declared value: each entry costs at least ten bytes
 * of real file (the chunk record that introduces it), so the count is bounded
 * by the file itself. */
static bool xx_cazip_xp_add_chunk(xx_cazip_member *member, int64_t offset,
                                  int64_t size) {
    xx_cazip_chunk *grown;

    if (member->chunk_count > SIZE_MAX / sizeof(xx_cazip_chunk) - 1U)
        return false;
    grown = (xx_cazip_chunk *)xx_mem_realloc(
        member->chunks, sizeof(xx_cazip_chunk) * (member->chunk_count + 1U));
    if (!grown) return false;
    member->chunks = grown;
    member->chunks[member->chunk_count].offset = offset;
    member->chunks[member->chunk_count].size = size;
    ++member->chunk_count;
    return true;
}

/*
 * Walk a member's compressed extent.  The first chunk's size comes from the
 * record line; any further chunks are introduced by a fixed ten-byte ASCII
 * record.
 *
 * Whether such a record is present is decided from the bytes, not from the
 * declared plaintext size: a chunk record holds TWO space-separated numbers
 * ("<next size> <more flag>") while the record that ends a member holds ONE.
 * That distinction is unambiguous and it avoids having to guess the packer's
 * split threshold, which is 16383 rather than the round 16384 one would
 * expect and is nowhere stated in the file.
 */
static bool xx_cazip_xp_read_chunks(Abstractformat *self, int64_t span,
                                    int64_t *cursor,
                                    xx_cazip_member *member,
                                    int64_t first_size) {
    int64_t position = *cursor;

    if (first_size < 0 || first_size > span - position) return false;
    if (!xx_cazip_xp_add_chunk(member, self->base_address + position,
                               first_size))
        return false;
    position += first_size;
    for (;;) {
        uint8_t record[XX_CAZIP_XP_CHUNK_RECORD];
        uint64_t values[2];
        size_t length = 0U;
        size_t index;
        bool two_fields = false;
        int64_t size;

        if (span - position < (int64_t)sizeof(record)) break;
        if (!xx_cazip_read_at(self, self->base_address + position, record,
                              sizeof(record)))
            return false;
        while (length < sizeof(record) && record[length] != 0U) ++length;
        /* Unterminated inside ten bytes: not a chunk record. */
        if (length == 0U || length == sizeof(record)) break;
        for (index = 0U; index < length; ++index)
            if (record[index] == ' ') two_fields = true;
        if (!two_fields) break;
        if (!xx_cazip_parse_fields(record, length, values, 2U)) break;
        if (values[1] > 1U) break;
        /* The padding between the terminator and the tenth byte is NUL. */
        for (index = length; index < sizeof(record); ++index)
            if (record[index] != 0U) return false;
        position += (int64_t)sizeof(record);
        if (values[0] > (uint64_t)(span - position)) return false;
        size = (int64_t)values[0];
        if (!xx_cazip_xp_add_chunk(member, self->base_address + position,
                                   size))
            return false;
        position += size;
        if (values[1] == 0U) break;
    }
    *cursor = position;
    return true;
}

static xx_cazip_stream *xx_cazip_parse_xp(Abstractformat *self, int64_t span) {
    xx_cazip_stream *stream;
    uint8_t signature[6];
    uint8_t namebuf[XX_CAZIP_XP_MAX_NAME + 1U];
    uint8_t fieldbuf[XX_CAZIP_XP_MAX_FIELDS + 1U];
    int64_t cursor;
    int64_t found;

    if (span < 16) return NULL;
    if (!xx_cazip_read_at(self, self->base_address, signature,
                          sizeof(signature)))
        return NULL;
    if (xx_rt_memcmp(signature, "CAZIP", 5U) != 0 || signature[5] != 0x04U)
        return NULL;
    cursor = 6;
    /* Archive field line, then the archive's own trailing number. */
    if (!xx_cazip_scan(self, span, cursor, 0U,
                       (int64_t)XX_CAZIP_XP_MAX_FIELDS, &found))
        return NULL;
    cursor = found + 1;
    if (!xx_cazip_scan(self, span, cursor, 0U,
                       (int64_t)XX_CAZIP_XP_MAX_TRAILER, &found))
        return NULL;
    cursor = found + 1;

    stream = (xx_cazip_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->variant = XX_CAZIP_VARIANT_XP;
    while (cursor < span) {
        xx_cazip_member member;
        uint64_t values[8];
        int64_t name_start;
        int64_t field_start;
        int64_t data_start;
        int64_t header_start = cursor;
        size_t name_length;
        size_t field_length;

        /* One byte of unidentified per-record flag precedes every name. */
        ++cursor;
        if (cursor >= span) goto fail;
        name_start = cursor;
        if (!xx_cazip_scan(self, span, cursor, 0x04U,
                           (int64_t)XX_CAZIP_XP_MAX_NAME, &found))
            goto fail;
        name_length = (size_t)(found - name_start);
        if (name_length == 0U) goto fail;
        if (!xx_cazip_read_at(self, self->base_address + name_start, namebuf,
                              name_length))
            goto fail;
        cursor = found + 1;

        field_start = cursor;
        if (!xx_cazip_scan(self, span, cursor, 0U,
                           (int64_t)XX_CAZIP_XP_MAX_FIELDS, &found))
            goto fail;
        field_length = (size_t)(found - field_start);
        if (field_length == 0U) goto fail;
        if (!xx_cazip_read_at(self, self->base_address + field_start,
                              fieldbuf, field_length))
            goto fail;
        if (!xx_cazip_parse_fields(fieldbuf, field_length, values, 8U))
            goto fail;
        cursor = found + 1;

        /* Both declared sizes are bounded against the real file before the
         * chunk table is built or anything is allocated from them. */
        if (values[2] > (uint64_t)XX_CAZIP_XP_MAX_PLAIN) goto fail;
        if (values[6] > (uint64_t)(span - cursor)) goto fail;

        xx_mem_zero(&member, sizeof(member));
        data_start = cursor;
        if (!xx_cazip_xp_read_chunks(self, span, &cursor, &member,
                                     (int64_t)values[6])) {
            xx_mem_free(member.chunks);
            goto fail;
        }
        member.name = xx_cazip_normalize_name(namebuf, name_length);
        if (!member.name) {
            xx_mem_free(member.chunks);
            goto fail;
        }
        member.mode = (uint32_t)values[0];
        member.is_folder =
            ((uint32_t)values[0] & XX_CAZIP_XP_IFMT) == XX_CAZIP_XP_IFDIR;
        member.uncompressed_size = (int64_t)values[2];
        member.timestamp = values[4];
        member.header_offset = self->base_address + header_start;
        member.header_size = data_start - header_start;
        member.data_offset = self->base_address + data_start;
        member.compressed_size = cursor - data_start;
        member.method = XX_CAZIP_METHOD_COMPRESS;
        if (member.chunk_count == 1U && member.chunks[0].size != 0) {
            uint8_t probe[3];
            if (!xx_cazip_read_at(self, member.chunks[0].offset, probe,
                                  member.chunks[0].size >= 3
                                      ? 3U
                                      : (size_t)member.chunks[0].size)) {
                xx_str_free(member.name);
                xx_mem_free(member.chunks);
                goto fail;
            }
            if (member.chunks[0].size < 3 ||
                !xx_compress_has_header(probe, 3U)) {
                /* Stored: only credible when the two sizes agree. */
                if (member.chunks[0].size != member.uncompressed_size) {
                    xx_str_free(member.name);
                    xx_mem_free(member.chunks);
                    goto fail;
                }
                member.method = XX_CAZIP_METHOD_STORE;
            }
        } else if (member.chunks[0].size == 0) {
            member.method = XX_CAZIP_METHOD_STORE;
        }
        if (!xx_cazip_add(stream, &member)) {
            xx_str_free(member.name);
            xx_mem_free(member.chunks);
            goto fail;
        }
        /* The final member ends at EOF with no trailing checksum. */
        if (cursor == span) break;
        if (!xx_cazip_scan(self, span, cursor, 0U,
                           (int64_t)XX_CAZIP_XP_MAX_TRAILER, &found))
            goto fail;
        cursor = found + 1;
    }
    if (stream->count == 0U || cursor != span) goto fail;
    stream->archive_size = span;
    return stream;
fail:
    xx_cazip_stream_free(stream);
    return NULL;
}

static xx_cazip_stream *xx_cazip_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    int64_t total;
    int64_t span;
    xx_cazip_stream *stream;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_CAZIP_CLASSIC_HEADER) return NULL;
    stream = xx_cazip_parse_classic(self, span);
    if (stream) return stream;
    return xx_cazip_parse_xp(self, span);
}

/* ------------------------------------------------------------- decode --- */

/* CAZIP classic: one DCL stream, checked against the CRC-32 in the header.
 * The plaintext length was measured by the parser, so a decode that does not
 * land on it exactly, or whose CRC does not match, is a failure rather than
 * something to hand back with a caveat. */
static bool xx_cazip_decode_classic(Abstractformat *self,
                                    const xx_cazip_member *member,
                                    uint8_t **plain, size_t *plain_size,
                                    xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    if (member->uncompressed_size <= 0) return false;
    if ((uint64_t)member->uncompressed_size > XX_CAZIP_CLASSIC_MAX_PLAIN)
        return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    input = xx_cazip_classic_payload(self, member);
    if (!input) return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_dcl_decode_memory(input, (size_t)member->compressed_size, output,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    /* The header's CRC-32 is the format's own statement about the plaintext,
     * so it is verified, not merely published. */
    if (member->has_crc32 &&
        xx_crc32_calc(0U, output, written) != member->crc32) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = written;
    return true;
}

static bool xx_cazip_decode(Abstractformat *self, const xx_cazip_member *member,
                            uint8_t **plain, size_t *plain_size,
                            xx_pd_struct *pd) {
    uint8_t *output;
    size_t written = 0U;
    size_t index;

    *plain = NULL;
    *plain_size = 0U;
    if (!self || !member) return false;
    if (member->is_folder) return true;
    /* A classic member whose stream did not measure at parse time - a
     * truncated volume - has no known plaintext length, and there is nothing
     * honest to hand back. */
    if (member->uncompressed_size < 0) return false;
    if (member->method >= XX_CAZIP_METHOD_CLASSIC_BASE) {
        return xx_cazip_decode_classic(self, member, plain, plain_size, pd);
    }
    if (member->uncompressed_size == 0) return true;
    if (member->uncompressed_size > XX_CAZIP_XP_MAX_PLAIN) return false;
    if ((uint64_t)member->uncompressed_size > SIZE_MAX) return false;
    if (member->chunk_count == 0U) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) return false;
    for (index = 0U; index < member->chunk_count; ++index) {
        const xx_cazip_chunk *chunk = &member->chunks[index];
        size_t room = (size_t)member->uncompressed_size - written;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (chunk->size < 0 || (uint64_t)chunk->size > SIZE_MAX) goto fail;
        if (member->method == XX_CAZIP_METHOD_STORE) {
            if ((uint64_t)chunk->size > (uint64_t)room) goto fail;
            if (!xx_cazip_read_at(self, chunk->offset, output + written,
                                  (size_t)chunk->size))
                goto fail;
            written += (size_t)chunk->size;
        } else {
            xx_io_device *sink;
            int64_t produced = 0;
            bool ok;

            if (room == 0U) goto fail;
            sink = xx_io_mem_open(output + written, room);
            if (!sink) goto fail;
            ok = xx_compress_decode_device(self->device, chunk->offset,
                                           chunk->size, sink, &produced, pd);
            xx_io_close(sink);
            if (!ok || produced < 0 || (uint64_t)produced > (uint64_t)room)
                goto fail;
            written += (size_t)produced;
        }
    }
    if (written != (size_t)member->uncompressed_size) goto fail;
    *plain = output;
    *plain_size = written;
    return true;
fail:
    xx_mem_free(output);
    return false;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_cazip_init(xx_cazip *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CAZIP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cazip");
    xx_format_set_extension(&archive->format, "caz");
    archive->format.check_is_valid = xx_cazip_check_is_valid;
    archive->format.handle_base_info = xx_cazip_handle_base_info;
    archive->format.get_format_size = xx_cazip_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_cazip_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_cazip_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_cazip_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_cazip_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_cazip_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_cazip_free_archive_records_reading;
    archive->format.destroy = xx_cazip_vtable_destroy;
}

xx_cazip *xx_cazip_create(xx_io_device *device, int64_t base_address) {
    xx_cazip *archive = (xx_cazip *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_cazip_init(archive, device, base_address);
    return archive;
}

void xx_cazip_destroy(xx_cazip *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_cazip_free(xx_cazip *archive) {
    if (!archive) return;
    xx_cazip_destroy(archive);
    xx_mem_free(archive);
}

static void xx_cazip_vtable_destroy(Abstractformat *self) {
    xx_cazip_destroy((xx_cazip *)self);
}

/* ------------------------------------------------------------- format --- */

bool xx_cazip_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_cazip_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_cazip_parse(self, pd);
    if (!stream) return false;
    xx_cazip_stream_free(stream);
    return true;
}

bool xx_cazip_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_cazip *archive = (xx_cazip *)self;
    xx_cazip_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_cazip_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->variant = stream->variant;
    xx_cazip_stream_free(stream);
    return true;
}

int64_t xx_cazip_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_cazip_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_cazip *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------ records --- */

static bool xx_cazip_set_record(xx_archive_record *record,
                                const xx_cazip_member *member) {
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
               member->uncompressed_size >= 0
                   ? (uint64_t)member->uncompressed_size
                   : 0U) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->mode) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           (!member->has_crc32 ||
            xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                           member->crc32)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_cazip_copy_options(xx_list_s *target,
                                  const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
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

static const xx_var *xx_cazip_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_cazip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_cazip_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_cazip_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_cazip_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_cazip_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_cazip_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_cazip_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_cazip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_cazip_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_cazip_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (xx_cazip_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_cazip_set_record(&state->current_record,
                            &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cazip_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_cazip_stream *stream;
    const xx_cazip_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted = NULL;
    char *target = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (xx_cazip_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_cazip_path_safe(member->name)) return false;

    path_option =
        xx_cazip_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        if (member->is_folder) return true;
        result = xx_cazip_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted;
    }
    if (!base_path) {
        xx_str_free(converted);
        return false;
    }
    target = (base_path[0] != '\0' &&
              base_path[xx_str_len(base_path) - 1U] != '/' &&
              base_path[xx_str_len(base_path) - 1U] != '\\')
                 ? xx_str_concat3(base_path, "/", member->name)
                 : xx_str_concat(base_path, member->name);
    xx_str_free(converted);
    if (!target) return false;
    if (member->is_folder) {
        result = xx_store_create_dirs_a(target, true);
        xx_str_free(target);
        return result;
    }
    if (!xx_store_create_dirs_a(target, false) ||
        !xx_cazip_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target, "wb");
        created = output != NULL;
        size_t done = 0U;

        result = output != NULL;
        while (result && done < plain_size) {
            ssize_t sent = xx_io_write(output, plain + done, plain_size - done);
            if (sent <= 0 || (size_t)sent > plain_size - done) {
                result = false;
                break;
            }
            done += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target);
    xx_str_free(target);
    return result;
}

void xx_cazip_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
