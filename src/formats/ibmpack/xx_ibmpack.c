/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM PACK, the packer behind PACK.EXE/UNPACK.EXE on IBM's PC-DOS and OS/2
 * installation media.  Members are the files whose last extension character
 * the packer replaced with '_' or '@'.
 *
 * The layout below was recovered from the corpora in F:\ARC\ARC\IBMPACK1..3
 * and cross-checked field for field against Deark's os2pack module
 * (modules/os2pack.c), which is the only published description of the
 * container this reader could find.  Deark agrees on every field named here;
 * the six bytes at 0x09 in the 0xFFFF dialect are unknown to both.
 *
 *   0x00  u16  0xA5 0x96 signature (stored in that byte order)
 *   0x02  u16  dialect word, little endian.  Exactly four values exist:
 *              0x0A14 and 0x1400 - "PACK 1": one byte of slack at 0x09 and
 *                                  a FIXED 13-byte name field at 0x0A.
 *              0xFFFF           - six bytes of slack at 0x09, then a
 *                                  NUL-terminated name at 0x0F.  No length,
 *                                  no chain: the member runs to EOF.
 *              0xFEFF (word 0xFFFE) - the full header, see below.
 *   0x04  u16  MS-DOS packed date
 *   0x06  u16  MS-DOS packed time
 *   0x08  u8   MS-DOS file attributes (0x20 or 0x00 throughout the corpus)
 *
 * The 0xFFFE dialect continues:
 *   0x0C  u32  offset of this member's compressed extended-attribute blob,
 *              0 when the member has none
 *   0x10  u32  plaintext length.  A literal 1 means "not recorded" - the
 *              packer writes 1, not 0, and 1 is never a real length here.
 *   0x14  u32  offset of the NEXT member header, 0 at the end of the chain
 *   0x18  u16  size of the name FIELD (not the name): the name is stored
 *              NUL-terminated inside it, and the field is padded so the
 *              payload starts on a four-byte boundary
 *   0x1A  ...  the name field, then the compressed payload
 *
 * Every dialect's payload is the same codec: IBM's 12-bit LZW, the one
 * shared with SaveDskF .DSK images.  It is not ordinary LZW - the code table
 * has no clear code and never resets.  Instead all 3839 dynamic slots are
 * held on an LRU list and the least-recently-used one is recycled for each
 * new phrase, with a use count keeping a slot alive while another phrase
 * still links to it.  Codes are 12 bits packed big-endian-nibble-wise, and
 * code 0 ends the stream.  The implementation below is a from-scratch
 * CRT-free port of the public-domain dskdcmps reference (via Deark's
 * foreign/dskdcmps.h).
 *
 * Confidence: the signature, dialect word, date, time, attribute byte, the
 * 0xFEFF extras and both name conventions are certain - they were verified
 * over all 758 corpus samples and they agree with Deark.  The codec is
 * certain: 162 of the samples declare a plaintext length and the decoder
 * reproduces every one of them exactly.  What is NOT known is the meaning of
 * the slack bytes at 0x09 (0xFFFF dialect) and of the extended-attribute
 * blob's internal format; both are skipped rather than guessed.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ibmpack/xx_ibmpack.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The enumerator is added by the coordinator, not by this file.  Until it
 * exists the reader still compiles and simply reports UNKNOWN.  Delete this
 * block once XX_FILE_TYPE_IBMPACK is in the enum. */
#ifdef IBMPACK
#define XX_IBMPACK_FILE_TYPE XX_FILE_TYPE_IBMPACK
#else
#define XX_IBMPACK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_IBMPACK_SIG0 UINT8_C(0xa5)
#define XX_IBMPACK_SIG1 UINT8_C(0x96)

#define XX_IBMPACK_VARIANT_P1A UINT32_C(0x0a14)
#define XX_IBMPACK_VARIANT_P1B UINT32_C(0x1400)
#define XX_IBMPACK_VARIANT_CHAIN UINT32_C(0xfffe)
#define XX_IBMPACK_VARIANT_PLAIN UINT32_C(0xffff)

/* Smallest header any dialect can produce: signature, dialect, date, time,
 * attributes.  Anything shorter cannot be classified at all. */
#define XX_IBMPACK_MIN_HEADER 10

/* PACK 1 stores the name in a fixed 13-byte field ("12345678.123" + NUL). */
#define XX_IBMPACK_P1_NAME_FIELD 13U
/* Deark's ceiling for the NUL scan in the 0xFFFF dialect; the longest name
 * in the corpus is 36 bytes, so this is generous rather than tight. */
#define XX_IBMPACK_MAX_NAME 260U

/* A chain cannot revisit an offset, so this only guards against a pathological
 * file with a huge number of tiny members. */
#define XX_IBMPACK_MAX_MEMBERS 65536U

/* Decode ceilings.  The codec emits at most 3840 bytes per 12-bit code, so a
 * stream expands by no more than 2560x; both limits are applied so a small
 * crafted file cannot buy a large allocation. */
#define XX_IBMPACK_MAX_DECODED ((int64_t)128 * 1024 * 1024)
#define XX_IBMPACK_MAX_EXPANSION ((int64_t)2560)

#define XX_IBMPACK_METHOD_LZW 0U

/* --------------------------------------------------------------- codec -- */

#define XX_IBMPACK_TABLE 4096U
/* dskdcmps refuses to emit a phrase longer than this, so the reversal buffer
 * is exactly one phrase wide. */
#define XX_IBMPACK_MAX_PHRASE (XX_IBMPACK_TABLE - 256U)

typedef struct xx_ibmpack_lzw_s {
    uint16_t older[XX_IBMPACK_TABLE];
    uint16_t newer[XX_IBMPACK_TABLE];
    uint16_t charlink[XX_IBMPACK_TABLE];
    uint16_t size[XX_IBMPACK_TABLE];
    int32_t usecount[XX_IBMPACK_TABLE];
    uint8_t valfirst[XX_IBMPACK_TABLE];
    uint8_t value[XX_IBMPACK_TABLE];
    uint8_t phrase[XX_IBMPACK_TABLE];
    uint16_t oldest;
    uint16_t newest;
    uint16_t oldcode;
    uint16_t hold;
    int nibble_high;
} xx_ibmpack_lzw;

static void xx_ibmpack_lzw_reset(xx_ibmpack_lzw *lzw) {
    uint16_t code;

    xx_mem_zero(lzw, sizeof(*lzw));
    for (code = 1U; code <= 256U; ++code) {
        lzw->valfirst[code] = (uint8_t)(code - 1U);
        lzw->value[code] = (uint8_t)(code - 1U);
        lzw->size[code] = 1U;
        /* Held at one for ever so a root code is never recycled. */
        lzw->usecount[code] = 1;
    }
    for (code = 257U; code <= 4095U; ++code) {
        if (code < 4095U) lzw->newer[code] = (uint16_t)(code + 1U);
        if (code > 257U) lzw->older[code] = (uint16_t)(code - 1U);
    }
    lzw->oldest = 257U;
    lzw->newest = 4095U;
    lzw->nibble_high = 1;
    lzw->oldcode = 0U;
    lzw->hold = 0U;
}

static void xx_ibmpack_lzw_unlink(xx_ibmpack_lzw *lzw, uint16_t code) {
    uint16_t next = lzw->newer[code];
    uint16_t prev = lzw->older[code];

    if (code == lzw->newest)
        lzw->newest = prev;
    else
        lzw->older[next] = prev;
    if (code == lzw->oldest)
        lzw->oldest = next;
    else
        lzw->newer[prev] = next;
    lzw->older[code] = 0U;
    lzw->newer[code] = 0U;
}

static void xx_ibmpack_lzw_touch(xx_ibmpack_lzw *lzw, uint16_t code) {
    lzw->newer[lzw->newest] = code;
    lzw->older[code] = lzw->newest;
    lzw->newer[code] = 0U;
    lzw->newest = code;
}

/* Recycle the least-recently-used slot and describe the phrase
 * oldcode + first(tcode) in it. */
static bool xx_ibmpack_lzw_build(xx_ibmpack_lzw *lzw, uint16_t newcode) {
    uint16_t lru = lzw->oldest;
    uint16_t parent = lzw->charlink[lru];
    uint16_t source;
    uint32_t length;

    if (lru < 257U) return false;
    xx_ibmpack_lzw_unlink(lzw, lru);
    if (parent != 0U) {
        if (lzw->usecount[parent] > 0) --lzw->usecount[parent];
        if (lzw->usecount[parent] == 0) xx_ibmpack_lzw_touch(lzw, parent);
    }
    if (lzw->size[lzw->oldcode] < 1U) return false;
    length = (uint32_t)lzw->size[lzw->oldcode] + 1U;
    if (length > XX_IBMPACK_TABLE) return false;
    source = (newcode != lru) ? newcode : lzw->oldcode;
    lzw->valfirst[lru] = lzw->valfirst[lzw->oldcode];
    lzw->value[lru] = lzw->valfirst[source];
    lzw->size[lru] = (uint16_t)length;
    lzw->charlink[lru] = lzw->oldcode;
    /* Reserve the parent so the LRU sweep cannot take it away while this
     * phrase still links back to it. */
    if (lzw->usecount[lzw->oldcode] > 0) {
        ++lzw->usecount[lzw->oldcode];
    } else {
        xx_ibmpack_lzw_unlink(lzw, lzw->oldcode);
        lzw->usecount[lzw->oldcode] = 1;
    }
    lzw->usecount[lru] = 0;
    xx_ibmpack_lzw_touch(lzw, lru);
    return true;
}

/* Walk a phrase back to its root, filling @p phrase from the end. */
static bool xx_ibmpack_lzw_expand(xx_ibmpack_lzw *lzw, uint16_t code,
                                  uint32_t *length) {
    uint32_t want;
    uint32_t index;
    uint32_t position;
    uint16_t cursor = code;

    if (code == 0U || code >= XX_IBMPACK_TABLE) return false;
    want = lzw->size[code];
    if (want < 1U || want > XX_IBMPACK_MAX_PHRASE) return false;
    position = want;
    for (index = 0U; index < want; ++index) {
        if (cursor == 0U || cursor >= XX_IBMPACK_TABLE) return false;
        if (lzw->size[cursor] != (uint16_t)(want - index)) return false;
        --position;
        lzw->phrase[position] = lzw->value[cursor];
        cursor = lzw->charlink[cursor];
    }
    *length = want;
    return true;
}

/*
 * Run the codec over @p input.  When @p output is NULL the run only measures,
 * which is how a member with no declared plaintext length is sized before
 * anything is allocated for it.  @p limit bounds the run in both modes.
 *
 * Returns false on a malformed stream.  Reaching the end of the input without
 * seeing code 0 is NOT an error: the container ends a member at a byte
 * boundary and several corpus members simply stop there.
 */
static bool xx_ibmpack_lzw_run(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t limit,
                               size_t *produced) {
    xx_ibmpack_lzw *lzw;
    size_t position = 0U;
    size_t written = 0U;
    bool ok = true;

    *produced = 0U;
    lzw = (xx_ibmpack_lzw *)xx_mem_alloc(sizeof(*lzw));
    if (!lzw) return false;
    xx_ibmpack_lzw_reset(lzw);
    for (;;) {
        uint16_t code;
        uint32_t length = 0U;

        if (position >= input_size) break;
        if (lzw->nibble_high) {
            code = (uint16_t)((uint16_t)input[position++] << 4);
            if (position >= input_size) break;
            lzw->hold = input[position++];
            code = (uint16_t)(code | (uint16_t)(lzw->hold >> 4));
        } else {
            code = (uint16_t)((uint16_t)(lzw->hold & 0x0fU) << 8);
            if (position >= input_size) break;
            code = (uint16_t)(code | input[position++]);
            lzw->hold = 0U;
        }
        lzw->nibble_high = !lzw->nibble_high;
        if (code == 0U) break;
        if (lzw->oldcode > 0U && !xx_ibmpack_lzw_build(lzw, code)) {
            ok = false;
            break;
        }
        if (!xx_ibmpack_lzw_expand(lzw, code, &length)) {
            ok = false;
            break;
        }
        if (length > limit - written) {
            ok = false;
            break;
        }
        if (output) xx_mem_copy(output + written, lzw->phrase, length);
        written += length;
        lzw->oldcode = code;
    }
    xx_mem_free(lzw);
    if (ok) *produced = written;
    return ok;
}

/* -------------------------------------------------------------- reader -- */

typedef struct xx_ibmpack_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size; /* -1 when the dialect does not record it */
    uint32_t variant;
    uint32_t attributes;
    uint64_t timestamp;
} xx_ibmpack_member;

typedef struct xx_ibmpack_stream_s {
    xx_ibmpack_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t variant;
} xx_ibmpack_stream;

static void xx_ibmpack_vtable_destroy(Abstractformat *self);

static uint16_t xx_ibmpack_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t xx_ibmpack_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool xx_ibmpack_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_ibmpack_variant_known(uint32_t variant) {
    return variant == XX_IBMPACK_VARIANT_P1A ||
           variant == XX_IBMPACK_VARIANT_P1B ||
           variant == XX_IBMPACK_VARIANT_CHAIN ||
           variant == XX_IBMPACK_VARIANT_PLAIN;
}

/* The stored name is an MS-DOS path with backslash separators.  Only the
 * filesystem-facing form is rewritten; the bytes themselves are kept for the
 * caller's code page. */
static char *xx_ibmpack_normalize_name(const uint8_t *bytes, size_t size) {
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

        while (input < size &&
               (bytes[input] == '/' || bytes[input] == '\\'))
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

static bool xx_ibmpack_path_safe(const char *name) {
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

static void xx_ibmpack_stream_free(void *pointer) {
    xx_ibmpack_stream *stream = (xx_ibmpack_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        xx_str_free(stream->items[index].name);
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_ibmpack_add(xx_ibmpack_stream *stream,
                           const xx_ibmpack_member *member) {
    xx_ibmpack_member *grown;

    if (stream->count >= XX_IBMPACK_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (xx_ibmpack_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Parse one member header at @p offset (relative to base_address).  On
 * success @p next receives the chained successor's relative offset, or 0. */
static bool xx_ibmpack_read_member(Abstractformat *self, int64_t span,
                                   int64_t offset, xx_ibmpack_member *member,
                                   int64_t *next) {
    uint8_t header[26];
    uint8_t namebuf[XX_IBMPACK_MAX_NAME];
    uint32_t variant;
    uint32_t extended_attributes = 0U;
    uint32_t declared = 0U;
    uint32_t successor = 0U;
    int64_t name_offset;
    int64_t name_field;
    int64_t data_offset;
    int64_t member_end;
    size_t name_length;

    xx_mem_zero(member, sizeof(*member));
    *next = 0;
    if (offset < 0 || span - offset < XX_IBMPACK_MIN_HEADER) return false;
    if (!xx_ibmpack_read_at(self, self->base_address + offset, header,
                            XX_IBMPACK_MIN_HEADER))
        return false;
    if (header[0] != XX_IBMPACK_SIG0 || header[1] != XX_IBMPACK_SIG1)
        return false;
    variant = xx_ibmpack_le16(header + 2);
    if (!xx_ibmpack_variant_known(variant)) return false;

    member->variant = variant;
    member->attributes = header[8];
    member->timestamp = ((uint64_t)xx_ibmpack_le16(header + 4) << 16) |
                        (uint64_t)xx_ibmpack_le16(header + 6);

    if (variant == XX_IBMPACK_VARIANT_CHAIN) {
        if (span - offset < 26) return false;
        if (!xx_ibmpack_read_at(self, self->base_address + offset, header,
                                sizeof(header)))
            return false;
        extended_attributes = xx_ibmpack_le32(header + 12);
        declared = xx_ibmpack_le32(header + 16);
        successor = xx_ibmpack_le32(header + 20);
        name_field = (int64_t)xx_ibmpack_le16(header + 24);
        name_offset = offset + 26;
        /* A name field must fit in the file and must be able to hold a
         * NUL.  Bound it before it is used to place the payload. */
        if (name_field < 1 || name_field > (int64_t)XX_IBMPACK_MAX_NAME ||
            name_field > span - name_offset)
            return false;
    } else if (variant == XX_IBMPACK_VARIANT_PLAIN) {
        name_offset = offset + 15;
        if (name_offset > span) return false;
        name_field = span - name_offset;
        if (name_field > (int64_t)XX_IBMPACK_MAX_NAME)
            name_field = (int64_t)XX_IBMPACK_MAX_NAME;
    } else {
        name_offset = offset + 10;
        name_field = (int64_t)XX_IBMPACK_P1_NAME_FIELD;
        if (name_field > span - name_offset) return false;
    }

    if (!xx_ibmpack_read_at(self, self->base_address + name_offset, namebuf,
                            (size_t)name_field))
        return false;
    name_length = 0U;
    while (name_length < (size_t)name_field && namebuf[name_length] != 0U)
        ++name_length;
    if (name_length == 0U) return false;
    if (variant == XX_IBMPACK_VARIANT_PLAIN) {
        /* No field size is stored: the terminator itself delimits it, and a
         * run with no terminator is not a header. */
        if (name_length == (size_t)name_field) return false;
        name_field = (int64_t)name_length + 1;
    }
    data_offset = name_offset + name_field;
    if (data_offset > span) return false;

    if (successor != 0U) {
        if ((int64_t)successor <= offset || (int64_t)successor > span ||
            (int64_t)successor < data_offset)
            return false;
        member_end = (int64_t)successor;
        *next = (int64_t)successor;
    } else {
        member_end = span;
    }
    /* When the field really is an offset the blob lives between the payload
     * and the member's end, and the payload stops there; its own format is
     * not decoded, only excluded.  But the corpus also carries members whose
     * field holds a small value (2, 8, 0x0A, 0x88D ...) far below the
     * payload - plainly not an offset, and the meaning is unknown.  Those are
     * ignored rather than treated as fatal: the payload keeps its full extent
     * and the codec stops at its own end code.  Do not "tighten" this into a
     * rejection without first explaining those small values - 4 of the 257
     * IBMPACK2 samples depend on it. */
    if (extended_attributes != 0U &&
        (int64_t)extended_attributes >= data_offset &&
        (int64_t)extended_attributes <= member_end)
        member_end = (int64_t)extended_attributes;
    if (member_end < data_offset) return false;

    member->name = xx_ibmpack_normalize_name(namebuf, name_length);
    if (!member->name) return false;
    member->header_offset = self->base_address + offset;
    member->header_size = data_offset - offset;
    member->data_offset = self->base_address + data_offset;
    member->compressed_size = member_end - data_offset;
    /* The packer writes a literal 1 when it did not record a length. */
    member->uncompressed_size =
        (variant == XX_IBMPACK_VARIANT_CHAIN && declared != 0U &&
         declared != 1U)
            ? (int64_t)declared
            : -1;
    if (member->uncompressed_size > XX_IBMPACK_MAX_DECODED) {
        xx_str_free(member->name);
        member->name = NULL;
        return false;
    }
    return true;
}

static xx_ibmpack_stream *xx_ibmpack_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    xx_ibmpack_stream *stream;
    int64_t total;
    int64_t span;
    int64_t cursor = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_IBMPACK_MIN_HEADER) return NULL;

    stream = (xx_ibmpack_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    for (;;) {
        xx_ibmpack_member member;
        int64_t next = 0;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_ibmpack_read_member(self, span, cursor, &member, &next))
            goto fail;
        if (stream->count == 0U) stream->variant = member.variant;
        if (!xx_ibmpack_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        if (next == 0) break;
        cursor = next;
    }
    stream->archive_size = span;
    return stream;
fail:
    xx_ibmpack_stream_free(stream);
    return NULL;
}

/* Produce the member's plaintext.  Members whose dialect records a length
 * must reproduce it exactly; the others are measured first so the allocation
 * is never larger than the stream can actually produce. */
static bool xx_ibmpack_decode(Abstractformat *self,
                              const xx_ibmpack_member *member,
                              uint8_t **plain, size_t *plain_size,
                              xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    int64_t ceiling;
    size_t produced = 0U;
    size_t wanted;

    *plain = NULL;
    *plain_size = 0U;
    if (!self || !member || member->compressed_size < 0) return false;
    if (member->compressed_size == 0) return false;
    if (member->compressed_size > XX_IBMPACK_MAX_DECODED) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    ceiling = XX_IBMPACK_MAX_DECODED;
    if (member->compressed_size < ceiling / XX_IBMPACK_MAX_EXPANSION)
        ceiling = member->compressed_size * XX_IBMPACK_MAX_EXPANSION;
    if (member->uncompressed_size >= 0) {
        if (member->uncompressed_size > ceiling) return false;
        ceiling = member->uncompressed_size;
    }
    if (ceiling <= 0 || (uint64_t)ceiling > SIZE_MAX) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_ibmpack_read_at(self, member->data_offset, input,
                            (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (member->uncompressed_size >= 0) {
        wanted = (size_t)member->uncompressed_size;
    } else {
        /* Measuring pass: nothing is allocated for the plaintext until its
         * real size is known. */
        if (!xx_ibmpack_lzw_run(input, (size_t)member->compressed_size, NULL,
                                (size_t)ceiling, &produced) ||
            produced == 0U) {
            xx_mem_free(input);
            return false;
        }
        wanted = produced;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc(wanted);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_ibmpack_lzw_run(input, (size_t)member->compressed_size, output,
                            wanted, &produced) ||
        produced != wanted) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *plain = output;
    *plain_size = wanted;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ibmpack_init(xx_ibmpack *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_IBMPACK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ibm-pack");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_ibmpack_check_is_valid;
    archive->format.handle_base_info = xx_ibmpack_handle_base_info;
    archive->format.get_format_size = xx_ibmpack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ibmpack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ibmpack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ibmpack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ibmpack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ibmpack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ibmpack_free_archive_records_reading;
    archive->format.destroy = xx_ibmpack_vtable_destroy;
}

xx_ibmpack *xx_ibmpack_create(xx_io_device *device, int64_t base_address) {
    xx_ibmpack *archive = (xx_ibmpack *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ibmpack_init(archive, device, base_address);
    return archive;
}

void xx_ibmpack_destroy(xx_ibmpack *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ibmpack_free(xx_ibmpack *archive) {
    if (!archive) return;
    xx_ibmpack_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ibmpack_vtable_destroy(Abstractformat *self) {
    xx_ibmpack_destroy((xx_ibmpack *)self);
}

/* ------------------------------------------------------------- format --- */

bool xx_ibmpack_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ibmpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ibmpack_parse(self, pd);
    if (!stream) return false;
    xx_ibmpack_stream_free(stream);
    return true;
}

bool xx_ibmpack_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ibmpack *archive = (xx_ibmpack *)self;
    xx_ibmpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_ibmpack_parse(self, pd);
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
    xx_ibmpack_stream_free(stream);
    return true;
}

int64_t xx_ibmpack_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ibmpack_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_ibmpack *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------ records --- */

static bool xx_ibmpack_set_record(xx_archive_record *record,
                                  const xx_ibmpack_member *member) {
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
                                          XX_IBMPACK_METHOD_LZW) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->variant) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_ibmpack_copy_options(xx_list_s *target,
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

static const xx_var *xx_ibmpack_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ibmpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ibmpack_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ibmpack_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ibmpack_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ibmpack_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ibmpack_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ibmpack_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ibmpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ibmpack_archive_record_move_to_next(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_ibmpack_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (xx_ibmpack_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_ibmpack_set_record(&state->current_record,
                              &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ibmpack_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_ibmpack_stream *stream;
    const xx_ibmpack_member *member;
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
    stream = (xx_ibmpack_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ibmpack_path_safe(member->name)) return false;

    path_option =
        xx_ibmpack_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_ibmpack_decode(self, member, &plain, &plain_size, pd);
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
    if (!xx_store_create_dirs_a(target, false) ||
        !xx_ibmpack_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_ibmpack_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
