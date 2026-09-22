/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Windows Clipboard (.CLP) files.
 *
 *   header, 4 bytes at offset 0:
 *     0x00  u16 LE identifier: 0xC350 (Windows 3.0) or 0xC351 (Windows NT)
 *     0x02  u16 LE record count
 *
 *   record, 0x59 bytes, count of them starting at offset 4:
 *     0x00  u16 LE clipboard format id (CF_*)
 *     0x02  i32 LE data size
 *     0x06  i32 LE absolute file offset of the data
 *     0x0a  0x4f bytes the format does not describe; never inspected
 *
 * The data blocks are NOT contiguous with the table and are not required to
 * be in record order, so there is no chain to walk and no end-of-file
 * relation to check. Everything this reader trusts therefore has to come out
 * of the identifier and the records themselves.
 *
 * A clipboard format id is either a standard one (1..17, up to CF_DIBV5) or a
 * privately registered one (0xC000 and above). Nothing legitimate falls in
 * the gap, which is what stops the four-byte header from matching arbitrary
 * files.
 *
 * Nothing in this container is compressed; extraction is a copy, and the
 * only bytes this reader ever invents are the fourteen of a
 * BITMAPFILEHEADER.  How each id becomes a file:
 *
 *   CF_TEXT (1), CF_OEMTEXT (7)  - the block up to its first NUL, as .txt
 *   CF_UNICODETEXT (13)          - the block up to its first UTF-16 NUL
 *   CF_DIB (8), CF_DIBV5 (17)    - the block is a BITMAPINFO(HEADER) plus
 *                                  pixels, i.e. a .bmp missing only its
 *                                  14-byte BITMAPFILEHEADER, which the
 *                                  decode synthesises and prepends
 *   CF_METAFILEPICT (3)          - an eight-byte Win16 METAFILEPICT followed
 *                                  by the metafile bits; the bits are
 *                                  published as .wmf, for exactly the length
 *                                  their own METAHEADER declares, and only
 *                                  when that header validates
 *   anything else                - the block exactly as stored, as .bin,
 *                                  named after its record index and id
 *
 * The last case covers CF_BITMAP, CF_PALETTE and privately registered ids.
 * Those are device-dependent or application-private objects with no file
 * form, so they are copied out verbatim rather than converted: turning a
 * Win16 DDB into a .bmp would need the companion CF_PALETTE record and a
 * planar de-interleave, i.e. a reconstruction, and this reader does not
 * reconstruct anything it cannot verify.
 *
 * The .txt and .bmp members are numbered the way U3 numbers them and match
 * its output byte for byte over the whole corpus; the .wmf and .bin members
 * carry their record index instead, so adding them cannot renumber the
 * members U3 also produces.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/clp/xx_clp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/sclsectors/xx_sclsectors.h"

#include <stdio.h>

#define XX_CLP_COPY_CHUNK (64 * 1024)

typedef struct xx_clp_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint32_t kind;
    uint64_t timestamp;
    bool is_folder;
} xx_clp_member;

typedef struct xx_clp_stream_s {
    xx_clp_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_clp_stream;

static void xx_clp_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_clp_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_clp_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_clp_path_safe(const char *name) {
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

static void xx_clp_stream_free(void *pointer) {
    xx_clp_stream *stream = (xx_clp_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_clp_add(xx_clp_stream *stream,
                          const xx_clp_member *member) {
    xx_clp_member *grown = (xx_clp_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_CLP_SCAN_CHUNK 4096
#define XX_CLP_HEADER_SIZE 4
#define XX_CLP_RECORD_SIZE 0x59
#define XX_CLP_MAX_MEMBERS 100000
#define XX_CLP_ID_WIN3 0xC350U
#define XX_CLP_ID_WINNT 0xC351U
#define XX_CLP_CF_MAX_STANDARD 17U
#define XX_CLP_CF_REGISTERED_FIRST 0xC000U
#define XX_CLP_CF_TEXT 1U
#define XX_CLP_CF_OEMTEXT 7U
#define XX_CLP_CF_DIB 8U
#define XX_CLP_CF_UNICODETEXT 13U
#define XX_CLP_CF_DIBV5 17U
#define XX_CLP_DIB_INFO_SIZE 0x28
#define XX_CLP_DIB_V5_INFO_SIZE 0x7C
#define XX_CLP_BMP_PREFIX_SIZE 14
#define XX_CLP_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_CLP_CF_METAFILEPICT 3U
/* Win16 METAFILEPICT: int mm, int xExt, int yExt, HANDLE hMF - the handle
 * slot is dead on disk and the metafile bits start right after it. */
#define XX_CLP_MFP_PREFIX_SIZE 8
#define XX_CLP_WMF_HEADER_SIZE 18

/* How a record turns into a file.  The id alone does not decide it: a
 * CF_METAFILEPICT whose embedded METAHEADER does not check out is carried as
 * an opaque block rather than published under a .wmf that would not open. */
#define XX_CLP_KIND_TEXT 0U
#define XX_CLP_KIND_DIB 1U
#define XX_CLP_KIND_WMF 2U
#define XX_CLP_KIND_RAW 3U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_clp_le16(const uint8_t *data);
static uint32_t xx_clp_le32(const uint8_t *data);
static bool xx_clp_text_length(Abstractformat *self, int64_t offset, int64_t size, bool wide, xx_pd_struct *pd, int64_t *out_length);
static xx_clp_stream *xx_clp_parse(Abstractformat *self, xx_pd_struct *pd);
static void xx_clp_put_le16(uint8_t *data, uint16_t value);
static void xx_clp_put_le32(uint8_t *data, uint32_t value);
static void xx_clp_bmp_prefix(const uint8_t *info, int64_t data_size, uint8_t *prefix);
static bool xx_clp_metafile_extent(Abstractformat *self, int64_t offset, int64_t size, int64_t *out_size);
static bool xx_clp_decode(Abstractformat *self, const xx_clp_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Text members are scanned for their terminator rather than loaded whole.
 * The chunk length is even so a UTF-16 code unit never straddles two reads. */

static uint16_t xx_clp_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_clp_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Length of the string inside a text block: up to the first NUL for the
 * single-byte ids, up to the first aligned NUL pair for CF_UNICODETEXT. The
 * block is padded with whatever the producing application had in its buffer,
 * so publishing the whole extent would append that slack to every .txt. */
static bool xx_clp_text_length(Abstractformat *self, int64_t offset,
                               int64_t size, bool wide, xx_pd_struct *pd,
                               int64_t *out_length) {
    uint8_t chunk[XX_CLP_SCAN_CHUNK];
    int64_t position = 0;

    *out_length = size;
    while (position < size) {
        int64_t remaining = size - position;
        size_t portion = (size_t)(remaining < (int64_t)sizeof(chunk)
                                      ? remaining
                                      : (int64_t)sizeof(chunk));
        size_t index;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_clp_read_at(self, offset + position, chunk, portion)) {
            return false;
        }
        if (wide) {
            for (index = 0U; index + 1U < portion; index += 2U) {
                if (chunk[index] == 0U && chunk[index + 1U] == 0U) {
                    *out_length = position + (int64_t)index;
                    return true;
                }
            }
        } else {
            for (index = 0U; index < portion; ++index) {
                if (chunk[index] == 0U) {
                    *out_length = position + (int64_t)index;
                    return true;
                }
            }
        }
        position += (int64_t)portion;
    }
    return true;
}

/* A CF_METAFILEPICT block is a Win16 METAFILEPICT followed by the metafile
 * bits themselves.  Publishing those bits as a .wmf is a copy of a
 * sub-extent, not an interpretation - but only once the embedded METAHEADER
 * has said so, and only for the length that header declares, because the
 * block is padded to the clipboard allocation and the slack is not part of
 * the metafile.  A block that does not check out stays opaque. */
static bool xx_clp_metafile_extent(Abstractformat *self, int64_t offset,
                                   int64_t size, int64_t *out_size) {
    uint8_t header[XX_CLP_WMF_HEADER_SIZE];
    uint16_t type, header_words, version;
    uint32_t words;

    if (!out_size) return false;
    *out_size = 0;
    if (size < XX_CLP_MFP_PREFIX_SIZE + XX_CLP_WMF_HEADER_SIZE) return false;
    if (!xx_clp_read_at(self, offset + XX_CLP_MFP_PREFIX_SIZE, header,
                        sizeof(header))) {
        return false;
    }
    type = xx_clp_le16(header);
    header_words = xx_clp_le16(header + 2);
    version = xx_clp_le16(header + 4);
    words = xx_clp_le32(header + 6);
    /* mtType 1 is a memory metafile, 2 a disk one; mtHeaderSize is fixed at
     * nine words and mtVersion is 0x0100 or 0x0300. */
    if ((type != 1U && type != 2U) || header_words != 9U ||
        (version != 0x0100U && version != 0x0300U)) {
        return false;
    }
    if ((int64_t)words < XX_CLP_WMF_HEADER_SIZE / 2 ||
        (int64_t)words > (size - XX_CLP_MFP_PREFIX_SIZE) / 2) {
        return false;
    }
    *out_size = (int64_t)words * 2;
    return true;
}

static xx_clp_stream *xx_clp_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_clp_stream *stream;
    uint8_t header[XX_CLP_HEADER_SIZE];
    uint8_t record[XX_CLP_RECORD_SIZE];
    char buffer[64];
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t index;
    int64_t accepted = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_CLP_HEADER_SIZE) return NULL;
    if (!xx_clp_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* The identifier at +0 is the ONLY signature this format has. Without it
     * the reader accepts any file with a small number at +2 followed by a few
     * in-range 32-bit pairs, which is common enough that it shadowed two
     * dozen archives of other formats that have working readers of their
     * own. This is the check a later reader will be tempted to loosen. */
    if (xx_clp_le16(header) != (uint16_t)XX_CLP_ID_WIN3 &&
        xx_clp_le16(header) != (uint16_t)XX_CLP_ID_WINNT) {
        return NULL;
    }
    count = (int64_t)xx_clp_le16(header + 2);
    if (count <= 0 || count > XX_CLP_MAX_MEMBERS) return NULL;
    /* The whole table has to be inside the file before any of it is read. */
    if (!xx_clp_range_within(span, XX_CLP_HEADER_SIZE,
                             count * XX_CLP_RECORD_SIZE)) {
        return NULL;
    }

    stream = (xx_clp_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0; index < count; ++index) {
        xx_clp_member member;
        int64_t record_offset = XX_CLP_HEADER_SIZE + index * XX_CLP_RECORD_SIZE;
        int64_t data_size;
        int64_t data_offset;
        int64_t info_size;
        int64_t length;
        uint16_t format;
        bool is_text;
        bool is_dib;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_clp_read_at(self, self->base_address + record_offset, record,
                            sizeof(record))) {
            goto fail;
        }

        format = xx_clp_le16(record);
        /* Signed on purpose: the reference reads both fields as int32, so a
         * value with the top bit set is a corrupt record, not a two-gigabyte
         * block. */
        data_size = (int64_t)(int32_t)xx_clp_le32(record + 2);
        data_offset = (int64_t)(int32_t)xx_clp_le32(record + 6);
        if (data_size < 0 || data_offset < 0) goto fail;
        /* An id in neither the standard nor the registered range is not a
         * clipboard record, so the file is not a clipboard file. Together
         * with the identifier this is the whole false-positive defence: the
         * blocks are scattered, so there is no chain and no EOF relation to
         * fall back on. */
        if (format == 0U || (format > (uint16_t)XX_CLP_CF_MAX_STANDARD &&
                             format < (uint16_t)XX_CLP_CF_REGISTERED_FIRST)) {
            goto fail;
        }
        /* Data blocks are addressed absolutely and may sit anywhere, so each
         * one is checked against the whole span rather than against a
         * cursor. A block running past EOF is a rejection. */
        if (!xx_clp_range_within(span, data_offset, data_size)) goto fail;

        is_text = format == (uint16_t)XX_CLP_CF_TEXT ||
                  format == (uint16_t)XX_CLP_CF_OEMTEXT ||
                  format == (uint16_t)XX_CLP_CF_UNICODETEXT;
        is_dib = format == (uint16_t)XX_CLP_CF_DIB ||
                 format == (uint16_t)XX_CLP_CF_DIBV5;

        if (is_dib) {
            info_size = (format == (uint16_t)XX_CLP_CF_DIBV5)
                            ? XX_CLP_DIB_V5_INFO_SIZE
                            : XX_CLP_DIB_INFO_SIZE;
            /* A DIB too short for its own info header is skipped, not
             * rejected: the record is well formed, it simply carries no
             * bitmap, and the reference drops it from the listing. */
            if (data_size < info_size) continue;
        }

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = self->base_address + record_offset;
        member.header_size = XX_CLP_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = data_size;
        /* The clipboard format id, unchanged, so a listing shows what the
         * file actually says. The decode is the only place it is mapped. */
        member.method = (uint32_t)format;
        /* Clipboard files carry no timestamps and no directory entries. */
        member.timestamp = 0U;
        member.is_folder = false;

        if (is_text) {
            if (!xx_clp_text_length(
                    self, self->base_address + data_offset, data_size,
                    format == (uint16_t)XX_CLP_CF_UNICODETEXT, pd, &length)) {
                goto fail;
            }
            ++accepted;
            member.kind = XX_CLP_KIND_TEXT;
            member.compressed_size = length;
            member.uncompressed_size = length;
            xx_rt_snprintf(buffer, sizeof(buffer), "%lld.txt",
                           (long long)accepted);
        } else if (is_dib) {
            ++accepted;
            member.kind = XX_CLP_KIND_DIB;
            /* The synthesised BITMAPFILEHEADER is not in the file, so the
             * member is longer than its stored extent by exactly 14 bytes. */
            member.uncompressed_size = data_size + XX_CLP_BMP_PREFIX_SIZE;
            xx_rt_snprintf(buffer, sizeof(buffer), "%lld.bmp",
                           (long long)accepted);
        } else {
            int64_t metafile_size = 0;
            /* The .txt and .bmp numbering above is the reference's, so the
             * records it drops keep their own naming and never shift it. */
            member.kind = XX_CLP_KIND_RAW;
            if (format == (uint16_t)XX_CLP_CF_METAFILEPICT &&
                xx_clp_metafile_extent(self, self->base_address + data_offset,
                                       data_size, &metafile_size)) {
                member.kind = XX_CLP_KIND_WMF;
                member.data_offset += XX_CLP_MFP_PREFIX_SIZE;
                member.compressed_size = metafile_size;
                member.uncompressed_size = metafile_size;
                xx_rt_snprintf(buffer, sizeof(buffer), "record%lld.fmt%u.wmf",
                               (long long)(index + 1), (unsigned)format);
            } else {
                /* Nothing here interprets the block, so it is published
                 * exactly as stored, named after its position and id so two
                 * records of the same id cannot collide. */
                xx_rt_snprintf(buffer, sizeof(buffer), "record%lld.fmt%u.bin",
                               (long long)(index + 1), (unsigned)format);
            }
        }

        member.name = xx_str_dup(buffer);
        if (!member.name) goto fail;
        if (!xx_clp_path_safe(member.name) ||
            !xx_clp_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    if (stream->count == 0U) goto fail;
    /* The blocks are scattered and the table does not describe the end of the
     * data, so the whole input is the archive. */
    stream->archive_size = span;
    return stream;

fail:
    xx_clp_stream_free(stream);
    return NULL;
}


/* The reference caps the record count here; the table itself is bounded by
 * the file, so this is a runaway guard rather than a format limit. */


/* Standard clipboard formats stop at CF_DIBV5; anything a program registers
 * by name lands at or above CF_PRIVATEFIRST. Real .CLP files use both, but
 * nothing legitimate falls in the gap between them. */


/* BITMAPINFOHEADER and BITMAPV5HEADER, the two sizes a DIB block can open
 * with, and the BITMAPFILEHEADER the decode puts in front of it. */

/* A clipboard block is attacker-controlled in size; refuse rather than
 * attempt the allocation. */

static void xx_clp_put_le16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void xx_clp_put_le32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8) & 0xFFU);
    data[2] = (uint8_t)((value >> 8 >> 8) & 0xFFU);
    data[3] = (uint8_t)((value >> 8 >> 8 >> 8) & 0xFFU);
}

/* Build the BITMAPFILEHEADER a DIB block does not carry.
 *
 * bfOffBits is derived the way the reference derives it: from the END of the
 * file backwards by the pixel-array size, not forwards from the info header.
 * A DIB with a palette or with V5 colour-profile data placed after the pixels
 * cannot be located any other way, because the block states neither the
 * palette entry count nor where the pixels begin. */
static void xx_clp_bmp_prefix(const uint8_t *info, int64_t data_size,
                              uint8_t *prefix) {
    int32_t width = (int32_t)xx_clp_le32(info + 4);
    int32_t height = (int32_t)xx_clp_le32(info + 8);
    uint16_t bit_count = xx_clp_le16(info + 14);
    int32_t file_size = (int32_t)(uint32_t)(data_size + XX_CLP_BMP_PREFIX_SIZE);
    int32_t pixel_bytes =
        (int32_t)(uint32_t)(((int64_t)width * height * bit_count) / 8);

    xx_clp_put_le16(prefix, 0x4D42U);               /* 'BM' */
    xx_clp_put_le32(prefix + 2, (uint32_t)file_size);
    xx_clp_put_le16(prefix + 6, 0U);                /* bfReserved1 */
    xx_clp_put_le16(prefix + 8, 0U);                /* bfReserved2 */
    xx_clp_put_le32(prefix + 10, (uint32_t)(file_size - pixel_bytes));
}

/* Nothing in this container is compressed. The text ids are a verbatim copy
 * of an already-trimmed extent, and the DIB ids are prefix-then-copy, which
 * is exactly what the SCL-sectors pseudo codec does. */
static bool xx_clp_decode(Abstractformat *self, const xx_clp_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t prefix[XX_CLP_BMP_PREFIX_SIZE];
    uint8_t *input;
    uint8_t *output;
    int64_t info_size;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_CLP_MAX_DECODED ||
        member->uncompressed_size > XX_CLP_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!input) return false;
    if (member->compressed_size != 0 &&
        !xx_clp_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    if (member->kind == XX_CLP_KIND_DIB) {
        info_size = (member->method == XX_CLP_CF_DIBV5)
                        ? XX_CLP_DIB_V5_INFO_SIZE
                        : XX_CLP_DIB_INFO_SIZE;
        /* parse refuses a DIB record too short for its info header, so this
         * only fires if the two halves ever disagree. */
        if (member->compressed_size < info_size) {
            xx_mem_free(output);
            xx_mem_free(input);
            return false;
        }
        xx_clp_bmp_prefix(input, member->compressed_size, prefix);
        if (!xx_sclsectors_decode_memory_ex(
                prefix, (size_t)XX_CLP_BMP_PREFIX_SIZE, input,
                (size_t)member->compressed_size, output,
                (size_t)member->uncompressed_size, &written)) {
            xx_mem_free(output);
            xx_mem_free(input);
            return false;
        }
    } else if (!xx_sclsectors_decode_memory(
                   input, (size_t)member->compressed_size, output,
                   (size_t)member->uncompressed_size, &written)) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }

    xx_mem_free(input);
    /* Short output reported as success is the one failure the caller cannot
     * detect, so the produced length must equal the published one exactly. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_clp_init(xx_clp *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_CLP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-clipboard");
    xx_format_set_extension(&archive->format, "clp");
    archive->format.check_is_valid = xx_clp_check_is_valid;
    archive->format.handle_base_info = xx_clp_handle_base_info;
    archive->format.get_format_size = xx_clp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_clp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_clp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_clp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_clp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_clp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_clp_free_archive_records_reading;
    archive->format.destroy = xx_clp_vtable_destroy;
}

xx_clp *xx_clp_create(xx_io_device *device, int64_t base_address) {
    xx_clp *archive = (xx_clp *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_clp_init(archive, device, base_address);
    return archive;
}

void xx_clp_destroy(xx_clp *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_clp_free(xx_clp *archive) {
    if (!archive) return;
    xx_clp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_clp_vtable_destroy(Abstractformat *self) {
    xx_clp_destroy((xx_clp *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_clp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_clp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_clp_parse(self, pd);
    if (!stream) return false;
    xx_clp_stream_free(stream);
    return true;
}

bool xx_clp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_clp *archive = (xx_clp *)self;
    xx_clp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_clp_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_clp_stream_free(stream);
    return true;
}

int64_t xx_clp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_clp_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_clp *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_clp_set_record(xx_archive_record *record,
                                 const xx_clp_member *member) {
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

static bool xx_clp_copy_options(xx_list_s *target,
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

static const xx_var *xx_clp_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_clp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_clp_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_clp_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_clp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_clp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_clp_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_clp_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_clp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_clp_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_clp_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_clp_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_clp_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_clp_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_clp_stream *stream;
    const xx_clp_member *member;
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
    stream = (xx_clp_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_clp_path_safe(member->name)) return false;

    path_option = xx_clp_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_clp_decode(self, member, &plain, &plain_size, pd);
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
        !xx_clp_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_clp_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
