/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * XAR (eXtensible ARchive) archives, as used by Apple .pkg installers.
 *
 *   header, 28 bytes, ALL big-endian:
 *     0x00  "xar!"
 *     0x04  u16 header size, >= 28; a later version may grow it
 *     0x06  u16 version, 1 is the only one ever shipped
 *     0x08  u64 compressed length of the table of contents
 *     0x10  u64 uncompressed length of the table of contents
 *     0x18  u32 checksum algorithm: 0 none, 1 SHA-1, 2 MD5, 3 SHA-256,
 *                                   4 SHA-512
 *
 * The table of contents starts at `header size` and runs for `compressed
 * length` bytes. It is one zlib (RFC 1950) stream inflating to exactly
 * `uncompressed length` bytes of XML:
 *
 *     <xar><toc>
 *       <checksum style="sha1"><offset>0</offset><size>20</size></checksum>
 *       <file id="1">
 *         <name>dir</name><type>directory</type>
 *         <file id="2">
 *           <name>a.txt</name><type>file</type>
 *           <data>
 *             <offset>20</offset>       heap-relative
 *             <length>37</length>       bytes stored
 *             <size>64</size>           bytes after decoding
 *             <encoding style="application/x-gzip"/>
 *           </data>
 *         </file>
 *       </file>
 *     </toc></xar>
 *
 * The heap begins immediately after the table of contents, at
 * `header size + compressed length`, and every <offset> is relative to it --
 * including the checksum digest, which is why the digest is simply the first
 * thing in the heap in every archive produced in practice.
 *
 * <file> elements nest to describe directories, so a member's path is the
 * chain of <name> values of the frames still open. Sizes are decimal text,
 * not binary, and a member's method is an encoding MIME type rather than a
 * number: absent or application/octet-stream means stored, and x-gzip is
 * really a bare zlib stream, not a gzip one.
 *
 * Identification does not rest on "xar!". The four bytes are checked, but
 * what actually rejects a coincidence is that the table of contents must be
 * a complete zlib stream inflating to exactly the declared length with a
 * matching Adler-32, and the XML inside it must be well formed with <xar> as
 * its root and a single <toc> child.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xar/xx_xar.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/xml/xx_xml.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/lzma_alone/xx_lzma_alone.h"

#include <stdio.h>

#define XX_XAR_COPY_CHUNK (64 * 1024)

typedef struct xx_xar_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_xar_member;

typedef struct xx_xar_stream_s {
    xx_xar_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_xar_stream;

static void xx_xar_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_xar_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_xar_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_xar_path_safe(const char *name) {
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

static void xx_xar_stream_free(void *pointer) {
    xx_xar_stream *stream = (xx_xar_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_xar_add(xx_xar_stream *stream,
                          const xx_xar_member *member) {
    xx_xar_member *grown = (xx_xar_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_XAR_HEADER_SIZE 28
#define XX_XAR_MAX_HEADER_SIZE 1024
#define XX_XAR_MAX_TOC_PACKED 0x4000000
#define XX_XAR_MAX_TOC_PLAIN 0x8000000
#define XX_XAR_MAX_MEMBERS 0x40000
#define XX_XAR_MAX_DEPTH 256
#define XX_XAR_MAX_PATH 0x1000
#define XX_XAR_KIND_OTHER 0U
#define XX_XAR_KIND_XAR 1U
#define XX_XAR_KIND_TOC 2U
#define XX_XAR_KIND_FILE 3U
#define XX_XAR_KIND_DATA 4U
#define XX_XAR_KIND_NAME 5U
#define XX_XAR_KIND_TYPE 6U
#define XX_XAR_KIND_OFFSET 7U
#define XX_XAR_KIND_LENGTH 8U
#define XX_XAR_KIND_SIZE 9U
#define XX_XAR_KIND_ENCODING 10U
#define XX_XAR_METHOD_STORE 0U
#define XX_XAR_METHOD_ZLIB 1U
#define XX_XAR_METHOD_BZIP2 2U
#define XX_XAR_METHOD_LZMA 3U
#define XX_XAR_METHOD_XZ 4U
#define XX_XAR_METHOD_UNKNOWN 0xFFFFFFFFU
#define XX_XAR_MAX_DECODED (256 * 1024 * 1024)
#define XX_XAR_LZMA_ALONE_HEADER 13

typedef struct xx_xar_frame_s {
    char *name;
    int64_t offset;
    int64_t length;
    int64_t size;
    uint32_t method;
    bool is_folder;
    bool has_data;
    bool inside_data;
    bool name_seen;
    bool type_seen;
    bool offset_seen;
    bool length_seen;
    bool size_seen;
    bool encoding_seen;
} xx_xar_frame;

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_xar_be16(const uint8_t *data);
static uint32_t xx_xar_be32(const uint8_t *data);
static uint64_t xx_xar_be64(const uint8_t *data);
static bool xx_xar_is_space(char value);
static bool xx_xar_str_ieq(const char *left, const char *right);
static bool xx_xar_parse_u64(const char *text, int64_t *value);
static uint32_t xx_xar_method_from_style(const char *style);
static bool xx_xar_name_is_ok(const char *name);
static char *xx_xar_build_path(const xx_xar_frame *frames, int count);
static bool xx_xar_emit(Abstractformat *self, xx_xar_stream *stream, const xx_xar_frame *frames, int count, int64_t span, int64_t heap_offset, int64_t *archive_size);
static xx_xar_stream *xx_xar_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_xar_decode(Abstractformat *self, const xx_xar_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);



/* What the element currently open is, in XAR terms. Kept per open element so
 * a <name> or an <offset> is only believed where the schema puts it: XAR
 * files embed foreign metadata (signatures, property lists) under <toc>, and
 * a stray <offset> there must not become a member's location. */



static uint16_t xx_xar_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t xx_xar_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint64_t xx_xar_be64(const uint8_t *data) {
    return ((uint64_t)xx_xar_be32(data) << 32) |
           (uint64_t)xx_xar_be32(data + 4);
}

static bool xx_xar_is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

/* ASCII case-insensitive equality: encoding styles are MIME types, which are
 * case-insensitive by RFC 2045 even though every producer writes lower case. */
static bool xx_xar_str_ieq(const char *left, const char *right) {
    size_t index = 0U;

    if (!left || !right) return false;
    for (;; ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
        if (a == '\0') return true;
    }
}

/* XAR writes every number as decimal text. Anything else -- empty, signed,
 * hexadecimal, or wider than int64 -- is a malformed table of contents. */
static bool xx_xar_parse_u64(const char *text, int64_t *value) {
    uint64_t accumulator = 0U;
    size_t index = 0U;
    bool any = false;

    if (!text || !value) return false;
    while (text[index] && xx_xar_is_space(text[index])) ++index;
    while (text[index] >= '0' && text[index] <= '9') {
        uint64_t digit = (uint64_t)(text[index] - '0');
        if (accumulator > (0x7FFFFFFFFFFFFFFFULL - digit) / 10ULL) {
            return false;
        }
        accumulator = accumulator * 10ULL + digit;
        any = true;
        ++index;
    }
    while (text[index] && xx_xar_is_space(text[index])) ++index;
    if (!any || text[index] != '\0') return false;
    *value = (int64_t)accumulator;
    return true;
}

static uint32_t xx_xar_method_from_style(const char *style) {
    if (!style || style[0] == '\0' ||
        xx_xar_str_ieq(style, "application/octet-stream")) {
        return XX_XAR_METHOD_STORE;
    }
    if (xx_xar_str_ieq(style, "application/x-gzip") ||
        xx_xar_str_ieq(style, "application/gzip") ||
        xx_xar_str_ieq(style, "application/zlib") ||
        xx_xar_str_ieq(style, "application/x-zlib")) {
        return XX_XAR_METHOD_ZLIB;
    }
    if (xx_xar_str_ieq(style, "application/x-bzip2") ||
        xx_xar_str_ieq(style, "application/bzip2")) {
        return XX_XAR_METHOD_BZIP2;
    }
    if (xx_xar_str_ieq(style, "application/x-lzma") ||
        xx_xar_str_ieq(style, "application/lzma")) {
        return XX_XAR_METHOD_LZMA;
    }
    if (xx_xar_str_ieq(style, "application/x-xz") ||
        xx_xar_str_ieq(style, "application/xz")) {
        return XX_XAR_METHOD_XZ;
    }
    return XX_XAR_METHOD_UNKNOWN;
}

/* XAR names are UTF-8 by specification, so the usual "printable ASCII only"
 * rule would throw away legitimate archives: bytes >= 0x80 are accepted as
 * UTF-8 and only the C0 controls, DEL and the path separators are refused --
 * a name is one path component, and a '/' inside one would forge a
 * directory the table of contents never declared. */
static bool xx_xar_name_is_ok(const char *name) {
    size_t index;

    if (!name || name[0] == '\0') return false;
    for (index = 0U; name[index]; ++index) {
        uint8_t byte = (uint8_t)name[index];
        if (byte < 0x20U || byte == 0x7FU) return false;
        if (byte == (uint8_t)'/' || byte == (uint8_t)'\\') return false;
    }
    return index <= XX_XAR_MAX_PATH;
}

/* A member's path is the chain of <name> values of the <file> frames still
 * open, parents first. A frame with no name of its own is not fatal -- XAR
 * permits metadata-only entries -- so it contributes a placeholder and the
 * listing stays usable. */
static char *xx_xar_build_path(const xx_xar_frame *frames, int count) {
    static const char unnamed[] = "unnamed";
    size_t total = 0U;
    size_t position = 0U;
    char *path;
    int index;

    if (count <= 0) return NULL;
    for (index = 0; index < count; ++index) {
        const char *part = frames[index].name ? frames[index].name : unnamed;
        total += xx_rt_strlen(part) + 1U;
        if (total > (size_t)XX_XAR_MAX_PATH) return NULL;
    }
    path = (char *)xx_mem_alloc(total);
    if (!path) return NULL;
    for (index = 0; index < count; ++index) {
        const char *part = frames[index].name ? frames[index].name : unnamed;
        size_t length = xx_rt_strlen(part);
        size_t cursor;
        if (index > 0) path[position++] = '/';
        for (cursor = 0U; cursor < length; ++cursor) {
            path[position++] = part[cursor];
        }
    }
    path[position] = '\0';
    return path;
}

/* Publish the <file> frame on top of the stack. Everything that can make an
 * archive a rejection rather than a member is decided here. */
static bool xx_xar_emit(Abstractformat *self, xx_xar_stream *stream,
                        const xx_xar_frame *frames, int count, int64_t span,
                        int64_t heap_offset, int64_t *archive_size) {
    const xx_xar_frame *frame;
    xx_xar_member member;
    char *path;
    int64_t data_offset = heap_offset;
    int64_t packed = 0;
    int64_t plain = 0;
    uint32_t method = XX_XAR_METHOD_STORE;

    if (count <= 0) return false;
    if (stream->count >= (size_t)XX_XAR_MAX_MEMBERS) return false;
    frame = &frames[count - 1];

    if (frame->has_data) {
        /* <data> without all three of offset, length and size is not a
         * member this reader can describe, let alone extract. */
        if (!frame->offset_seen || !frame->length_seen || !frame->size_seen) {
            return false;
        }
        method = frame->method;
        if (method == XX_XAR_METHOD_UNKNOWN) return false;
        /* Offsets are heap-relative; a member whose extent leaves the file is
         * a rejection, not something to clamp. */
        if (frame->offset > span - heap_offset) return false;
        data_offset = heap_offset + frame->offset;
        if (!xx_xar_range_within(span, data_offset, frame->length)) {
            return false;
        }
        /* A stored member's two lengths are the same number written twice;
         * when they disagree the entry describes something other than what
         * the heap holds. */
        if (method == XX_XAR_METHOD_STORE && frame->length != frame->size) {
            return false;
        }
        packed = frame->length;
        plain = frame->size;
    }

    path = xx_xar_build_path(frames, count);
    if (!path) return false;

    xx_mem_zero(&member, sizeof(member));
    member.name = path;
    /* A member's metadata lives in the compressed table of contents and has
     * no byte extent of its own in the file, so the header is reported as the
     * table of contents itself with no length. */
    member.header_offset = self->base_address + heap_offset;
    member.header_size = 0;
    member.data_offset = self->base_address + data_offset;
    member.compressed_size = packed;
    member.uncompressed_size = plain;
    member.method = method;
    member.timestamp = 0U; /* <mtime> is ISO-8601 text; not parsed here. */
    member.is_folder = frame->is_folder;
    if (!xx_xar_add(stream, &member)) {
        xx_str_free(path);
        return false;
    }

    if (data_offset + packed > *archive_size) {
        *archive_size = data_offset + packed;
    }
    return true;
}

static xx_xar_stream *xx_xar_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_xar_stream *stream = NULL;
    xx_xar_frame *frames = NULL;
    uint8_t *kinds = NULL;
    uint8_t *packed = NULL;
    uint8_t *toc = NULL;
    uint8_t header[XX_XAR_HEADER_SIZE];
    xx_xml xml;
    int64_t total;
    int64_t span;
    int64_t header_size;
    int64_t toc_packed;
    int64_t toc_plain;
    int64_t heap_offset;
    int64_t digest_size;
    int64_t archive_size;
    uint32_t version;
    uint32_t checksum_alg;
    uint32_t current_field = XX_XAR_KIND_OTHER;
    size_t written = 0U;
    int element_depth = 0;
    int frame_count = 0;
    bool xml_started = false;
    bool bad = false;
    bool root_seen = false;
    bool toc_seen = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_XAR_HEADER_SIZE) return NULL;
    if (!xx_xar_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (header[0] != 'x' || header[1] != 'a' || header[2] != 'r' ||
        header[3] != '!') {
        return NULL;
    }

    header_size = (int64_t)xx_xar_be16(header + 4);
    version = (uint32_t)xx_xar_be16(header + 6);
    toc_packed = (int64_t)(xx_xar_be64(header + 8) & 0x7FFFFFFFFFFFFFFFULL);
    toc_plain = (int64_t)(xx_xar_be64(header + 16) & 0x7FFFFFFFFFFFFFFFULL);
    checksum_alg = xx_xar_be32(header + 24);

    /* The header size may grow in a later revision, but it can never be
     * smaller than the fields already defined, and 1 KiB of header is far
     * past anything a real archive uses. */
    if (header_size < XX_XAR_HEADER_SIZE ||
        header_size > XX_XAR_MAX_HEADER_SIZE) {
        return NULL;
    }
    if (version > 1U) return NULL;
    if (checksum_alg > 4U) return NULL;
    /* An empty table of contents is not a XAR: even an archive with no
     * members carries <xar><toc/></xar>, and the smallest zlib stream is six
     * bytes. The ceilings keep a forged header from asking for a huge
     * allocation before anything has been authenticated. */
    if (toc_packed < 6 || toc_packed > XX_XAR_MAX_TOC_PACKED) return NULL;
    if (toc_plain <= 0 || toc_plain > XX_XAR_MAX_TOC_PLAIN) return NULL;
    if (!xx_xar_range_within(span, header_size, toc_packed)) return NULL;

    heap_offset = header_size + toc_packed;
    digest_size = (checksum_alg == 1U)   ? 20
                  : (checksum_alg == 2U) ? 16
                  : (checksum_alg == 3U) ? 32
                  : (checksum_alg == 4U) ? 64
                                         : 0;
    /* The digest is the first thing in the heap; a file with no room for it
     * is truncated whatever else it claims. The digest itself is not
     * verified here -- the hashes live outside this reader. */
    if (!xx_xar_range_within(span, heap_offset, digest_size)) return NULL;

    packed = (uint8_t *)xx_mem_alloc((size_t)toc_packed);
    toc = (uint8_t *)xx_mem_alloc((size_t)toc_plain);
    if (!packed || !toc) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    if (!xx_xar_read_at(self, self->base_address + header_size, packed,
                        (size_t)toc_packed)) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* This is the format's real defence against a false positive. "xar!" is
     * four bytes and turns up in unrelated data; what does not is a complete
     * zlib stream of exactly the declared length that inflates to exactly the
     * declared size and whose Adler-32 matches. Loosen any of the three --
     * accept a short inflate, skip the trailer -- and arbitrary bytes behind
     * a forged magic start parsing as a table of contents. */
    if (!xx_zlib_stream_header_is_valid(packed, (size_t)toc_packed)) {
        goto fail;
    }
    if (!xx_zlib_stream_decode_memory(packed, (size_t)toc_packed, toc,
                                      (size_t)toc_plain, &written)) {
        goto fail;
    }
    if (written != (size_t)toc_plain) goto fail;
    if (!xx_zlib_stream_trailer_matches(packed, (size_t)toc_packed, toc,
                                        written)) {
        goto fail;
    }
    xx_mem_free(packed);
    packed = NULL;

    stream = (xx_xar_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    /* Both stacks are heap allocated: XX_XAR_MAX_DEPTH frames on the stack
     * would put tens of kilobytes in this frame. */
    frames = (xx_xar_frame *)xx_mem_alloc(sizeof(*frames) *
                                          (size_t)XX_XAR_MAX_DEPTH);
    kinds = (uint8_t *)xx_mem_alloc((size_t)XX_XAR_MAX_DEPTH);
    if (!frames || !kinds) goto fail;
    xx_mem_zero(frames, sizeof(*frames) * (size_t)XX_XAR_MAX_DEPTH);
    xx_mem_zero(kinds, (size_t)XX_XAR_MAX_DEPTH);

    archive_size = heap_offset + digest_size;

    /* The table of contents is walked with the cursor rather than built into
     * a tree: a XAR can describe hundreds of thousands of entries, and only
     * the frames on the path from <toc> to the current <file> are ever
     * needed at once. */
    xx_xml_init(&xml, toc, (size_t)toc_plain);
    xml_started = true;

    while (!bad && xx_xml_next(&xml)) {
        xx_xml_type_t node = xx_xml_type(&xml);
        const char *name = xx_xml_name(&xml);
        uint32_t kind = XX_XAR_KIND_OTHER;
        uint32_t parent;
        bool closes_now;

        if (pd && xx_pd_is_stopped(pd)) {
            bad = true;
            break;
        }

        if (node == XX_XML_TEXT) {
            xx_xar_frame *frame =
                frame_count > 0 ? &frames[frame_count - 1] : NULL;
            const char *text = xx_xml_text(&xml);

            /* Text is only believed while one of the five leaf elements is
             * open, and each of them may carry it exactly once. */
            if (current_field == XX_XAR_KIND_OTHER || !frame || !text) {
                continue;
            }
            if (current_field == XX_XAR_KIND_NAME) {
                if (frame->name_seen || !xx_xar_name_is_ok(text)) {
                    bad = true;
                    break;
                }
                frame->name = xx_str_dup(text);
                if (!frame->name) {
                    bad = true;
                    break;
                }
                frame->name_seen = true;
            } else if (current_field == XX_XAR_KIND_TYPE) {
                if (frame->type_seen) {
                    bad = true;
                    break;
                }
                frame->type_seen = true;
                frame->is_folder = xx_xar_str_ieq(text, "directory");
            } else if (current_field == XX_XAR_KIND_OFFSET) {
                if (frame->offset_seen ||
                    !xx_xar_parse_u64(text, &frame->offset)) {
                    bad = true;
                    break;
                }
                frame->offset_seen = true;
            } else if (current_field == XX_XAR_KIND_LENGTH) {
                if (frame->length_seen ||
                    !xx_xar_parse_u64(text, &frame->length)) {
                    bad = true;
                    break;
                }
                frame->length_seen = true;
            } else if (current_field == XX_XAR_KIND_SIZE) {
                if (frame->size_seen ||
                    !xx_xar_parse_u64(text, &frame->size)) {
                    bad = true;
                    break;
                }
                frame->size_seen = true;
            }
            continue;
        }

        if (node == XX_XML_END) {
            uint32_t closing;

            if (element_depth <= 0) {
                bad = true;
                break;
            }
            closing = kinds[--element_depth];
            current_field = XX_XAR_KIND_OTHER;
            if (closing == XX_XAR_KIND_FILE) {
                if (frame_count <= 0 ||
                    !xx_xar_emit(self, stream, frames, frame_count, span,
                                 heap_offset, &archive_size)) {
                    bad = true;
                    break;
                }
                --frame_count;
                xx_str_free(frames[frame_count].name);
                xx_mem_zero(&frames[frame_count], sizeof(frames[0]));
            } else if (closing == XX_XAR_KIND_DATA) {
                if (frame_count > 0) frames[frame_count - 1].inside_data = false;
            } else if (closing == XX_XAR_KIND_TOC) {
                /* Every <file> must close inside the <toc> that opened it. */
                if (frame_count != 0) {
                    bad = true;
                    break;
                }
            }
            continue;
        }

        if (node != XX_XML_START) continue; /* comments, PIs, the DOCTYPE */

        current_field = XX_XAR_KIND_OTHER;
        closes_now = xml.self_closing;

        if (element_depth == 0) {
            /* The document root is <xar> and nothing else. A zlib stream
             * that inflates to well-formed XML of some other schema is not
             * an archive. */
            if (root_seen || !name || !xx_xar_str_ieq(name, "xar")) {
                bad = true;
                break;
            }
            root_seen = true;
            kind = XX_XAR_KIND_XAR;
        } else {
            parent = kinds[element_depth - 1];
            if (parent == XX_XAR_KIND_XAR && xx_xar_str_ieq(name, "toc")) {
                /* Exactly one table of contents; a second one would let a
                 * crafted document append members after the first closed. */
                if (toc_seen) {
                    bad = true;
                    break;
                }
                toc_seen = true;
                kind = XX_XAR_KIND_TOC;
            } else if ((parent == XX_XAR_KIND_TOC ||
                        parent == XX_XAR_KIND_FILE) &&
                       xx_xar_str_ieq(name, "file")) {
                if (frame_count >= XX_XAR_MAX_DEPTH ||
                    stream->count >= (size_t)XX_XAR_MAX_MEMBERS) {
                    bad = true;
                    break;
                }
                xx_mem_zero(&frames[frame_count], sizeof(frames[0]));
                frames[frame_count].method = XX_XAR_METHOD_STORE;
                ++frame_count;
                kind = XX_XAR_KIND_FILE;
            } else if (parent == XX_XAR_KIND_FILE && frame_count > 0) {
                if (xx_xar_str_ieq(name, "name")) {
                    kind = XX_XAR_KIND_NAME;
                } else if (xx_xar_str_ieq(name, "type")) {
                    kind = XX_XAR_KIND_TYPE;
                } else if (xx_xar_str_ieq(name, "data")) {
                    /* Two <data> blocks in one <file> would leave which
                     * heap extent the member means undecided. */
                    if (frames[frame_count - 1].has_data) {
                        bad = true;
                        break;
                    }
                    frames[frame_count - 1].has_data = true;
                    frames[frame_count - 1].inside_data = true;
                    kind = XX_XAR_KIND_DATA;
                }
            } else if (parent == XX_XAR_KIND_DATA && frame_count > 0 &&
                       frames[frame_count - 1].inside_data) {
                xx_xar_frame *frame = &frames[frame_count - 1];
                if (xx_xar_str_ieq(name, "offset")) {
                    kind = XX_XAR_KIND_OFFSET;
                } else if (xx_xar_str_ieq(name, "length")) {
                    kind = XX_XAR_KIND_LENGTH;
                } else if (xx_xar_str_ieq(name, "size")) {
                    kind = XX_XAR_KIND_SIZE;
                } else if (xx_xar_str_ieq(name, "encoding")) {
                    /* The method is an attribute, not text, so it is read
                     * here and works for the self-closing form producers
                     * actually write. An encoding this reader does not know
                     * is a rejection: guessing "stored" would hand the
                     * caller compressed bytes as if they were data. */
                    if (frame->encoding_seen) {
                        bad = true;
                        break;
                    }
                    frame->encoding_seen = true;
                    frame->method = xx_xar_method_from_style(
                        xx_xml_attribute_value(&xml, "style"));
                    if (frame->method == XX_XAR_METHOD_UNKNOWN) {
                        bad = true;
                        break;
                    }
                    kind = XX_XAR_KIND_ENCODING;
                }
            }
        }

        if (closes_now) {
            /* A self-closing element is reported once and never pushed, so
             * its close has to be applied right here. */
            if (kind == XX_XAR_KIND_FILE) {
                if (!xx_xar_emit(self, stream, frames, frame_count, span,
                                 heap_offset, &archive_size)) {
                    bad = true;
                    break;
                }
                --frame_count;
                xx_str_free(frames[frame_count].name);
                xx_mem_zero(&frames[frame_count], sizeof(frames[0]));
            } else if (kind == XX_XAR_KIND_DATA) {
                frames[frame_count - 1].inside_data = false;
            } else if (kind == XX_XAR_KIND_TOC && frame_count != 0) {
                bad = true;
                break;
            }
            continue;
        }

        if (element_depth >= XX_XAR_MAX_DEPTH) {
            bad = true;
            break;
        }
        kinds[element_depth++] = (uint8_t)kind;
        if (kind == XX_XAR_KIND_NAME || kind == XX_XAR_KIND_TYPE ||
            kind == XX_XAR_KIND_OFFSET || kind == XX_XAR_KIND_LENGTH ||
            kind == XX_XAR_KIND_SIZE) {
            current_field = kind;
        }
    }

    if (xx_xml_failed(&xml)) bad = true;
    xx_xml_cleanup(&xml);
    xml_started = false;

    /* A document that stops mid-element, never names <xar>, or never opens a
     * <toc> is not a table of contents this reader may believe in part. */
    if (bad || !root_seen || !toc_seen || element_depth != 0 ||
        frame_count != 0) {
        goto fail;
    }

    if (archive_size > span) archive_size = span;
    stream->archive_size = archive_size;

    xx_mem_free(kinds);
    xx_mem_free(frames);
    xx_mem_free(toc);
    return stream;

fail:
    if (xml_started) xx_xml_cleanup(&xml);
    while (frame_count > 0) {
        --frame_count;
        xx_str_free(frames[frame_count].name);
    }
    xx_mem_free(kinds);
    xx_mem_free(frames);
    xx_mem_free(packed);
    xx_mem_free(toc);
    xx_xar_stream_free(stream);
    return NULL;
}


/* The container names a member's method with a MIME type; these are this
 * reader's own numbering of the ones XAR actually uses, and the parse stores
 * the value it mapped so a listing still shows what the archive said. An
 * encoding this table does not know is a rejection at parse time, never a
 * silent fall-through to "stored": handing back compressed bytes as plain
 * data is a failure the caller cannot detect. */

/* Both lengths come out of the table of contents, which is attacker
 * controlled: refuse rather than attempt the allocation. */

/* LZMA-Alone transport: 5 property bytes, then the 8-byte expanded size. */

static bool xx_xar_decode(Abstractformat *self, const xx_xar_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t packed_size;
    size_t plain_size;
    size_t written = 0U;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_XAR_MAX_DECODED ||
        member->uncompressed_size > XX_XAR_MAX_DECODED) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed_size = (size_t)member->compressed_size;
    plain_size = (size_t)member->uncompressed_size;

    /* A directory, or a <file> carrying no <data>, has nothing in the heap:
     * an empty result is correct, not a short read. */
    if (packed_size == 0U && plain_size == 0U) {
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }

    input = (uint8_t *)xx_mem_alloc(packed_size != 0U ? packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(plain_size != 0U ? plain_size : 1U);
    if (!input || !output) goto decode_fail;
    if (packed_size != 0U &&
        !xx_xar_read_at(self, member->data_offset, input, packed_size)) {
        goto decode_fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto decode_fail;

    if (member->method == XX_XAR_METHOD_STORE) {
        /* The parse already refuses a stored member whose two lengths
         * disagree, so this is a restatement rather than a new rule. */
        if (packed_size != plain_size) goto decode_fail;
        xx_rt_memcpy(output, input, packed_size);
        written = packed_size;
    } else if (member->method == XX_XAR_METHOD_ZLIB) {
        /* "application/x-gzip" in a XAR table of contents is a lie inherited
         * from the original implementation: the heap holds a bare zlib
         * stream, header and Adler-32 included, never a gzip wrapper. */
        if (!xx_zlib_stream_header_is_valid(input, packed_size)) {
            goto decode_fail;
        }
        if (!xx_zlib_stream_decode_memory(input, packed_size, output,
                                          plain_size, &written)) {
            goto decode_fail;
        }
        /* <length> is the exact stored extent, so the RFC 1950 trailer is
         * inside the member and the checksum can be insisted on. */
        if (written != plain_size ||
            !xx_zlib_stream_trailer_matches(input, packed_size, output,
                                            written)) {
            goto decode_fail;
        }
    } else if (member->method == XX_XAR_METHOD_BZIP2) {
        if (!xx_bzip2_decompress_memory(input, packed_size, output,
                                        plain_size, &written)) {
            goto decode_fail;
        }
    } else if (member->method == XX_XAR_METHOD_LZMA) {
        /* XAR stores application/x-lzma as the LZMA-Alone transport, so the
         * five property bytes and the 8-byte expanded size sit in front of
         * the raw stream the decoder wants. */
        if (packed_size <= (size_t)XX_XAR_LZMA_ALONE_HEADER) goto decode_fail;
        if (!xx_lzma_alone_has_header(input, packed_size)) goto decode_fail;
        if (!xx_lzma_decompress_memory(
                input + XX_XAR_LZMA_ALONE_HEADER,
                packed_size - (size_t)XX_XAR_LZMA_ALONE_HEADER, input,
                (size_t)XX_LZMA_PROPS_SIZE, member->uncompressed_size, output,
                plain_size, &written)) {
            goto decode_fail;
        }
    } else {
        /* XX_XAR_METHOD_XZ lands here. The library exposes XZ only as a
         * whole-file format reader over an xx_io_device (xx_xz_*), with no
         * in-memory block decode, so an XZ member cannot be expanded here.
         * Listing it is still worth doing: the member, its extent and its
         * method are all correct, only extraction is refused. */
        goto decode_fail;
    }

    /* Never return true with fewer bytes than the member claims: a partially
     * decoded member reported as success is invisible to the caller. */
    if (written != plain_size) goto decode_fail;
    if (pd && xx_pd_is_stopped(pd)) goto decode_fail;

    xx_mem_free(input);
    *out = output;
    *out_size = written;
    return true;

decode_fail:
    xx_mem_free(input);
    xx_mem_free(output);
    return false;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_xar_init(xx_xar *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_XAR;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-xar");
    xx_format_set_extension(&archive->format, "xar");
    archive->format.check_is_valid = xx_xar_check_is_valid;
    archive->format.handle_base_info = xx_xar_handle_base_info;
    archive->format.get_format_size = xx_xar_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_xar_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_xar_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_xar_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_xar_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_xar_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_xar_free_archive_records_reading;
    archive->format.destroy = xx_xar_vtable_destroy;
}

xx_xar *xx_xar_create(xx_io_device *device, int64_t base_address) {
    xx_xar *archive = (xx_xar *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_xar_init(archive, device, base_address);
    return archive;
}

void xx_xar_destroy(xx_xar *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_xar_free(xx_xar *archive) {
    if (!archive) return;
    xx_xar_destroy(archive);
    xx_mem_free(archive);
}

static void xx_xar_vtable_destroy(Abstractformat *self) {
    xx_xar_destroy((xx_xar *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_xar_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_xar_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_xar_parse(self, pd);
    if (!stream) return false;
    xx_xar_stream_free(stream);
    return true;
}

bool xx_xar_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_xar *archive = (xx_xar *)self;
    xx_xar_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_xar_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_xar_stream_free(stream);
    return true;
}

int64_t xx_xar_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_xar_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_xar *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_xar_set_record(xx_archive_record *record,
                                 const xx_xar_member *member) {
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

static bool xx_xar_copy_options(xx_list_s *target,
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

static const xx_var *xx_xar_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_xar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_xar_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_xar_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_xar_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_xar_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_xar_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_xar_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_xar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_xar_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_xar_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_xar_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_xar_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_xar_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_xar_stream *stream;
    const xx_xar_member *member;
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
    stream = (xx_xar_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_xar_path_safe(member->name)) return false;

    path_option = xx_xar_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_xar_decode(self, member, &plain, &plain_size, pd);
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
        !xx_xar_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_xar_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
