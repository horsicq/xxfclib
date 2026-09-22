/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Microsoft Transport Neutral Encapsulation Format
 * (winmail.dat).  The layout is ported from XArchive's
 * transport/xtnefarchive.cpp and agrees with MS-OXTNEF:
 *
 *   u32  signature 0x223E9F78
 *   u16  attach key (non-zero)
 *   then a flat run of attribute records:
 *     u8   level: 1 = message, 2 = attachment
 *     u32  attribute id (attType in the high half, attName in the low half)
 *     u32  length of the data that follows
 *     ...  data
 *     u16  checksum: the plain sum of the data bytes, modulo 65536
 *
 * The per-attribute checksum is the decode anchor: this reader computes it
 * for every record and stops the walk the moment one disagrees, which is
 * what bounds "garbage-at-end" style files.  Over the 17-sample corpus not a
 * single checksum disagrees and the walk lands exactly on end of file.
 *
 * The members are the attachments.  An attachment's bytes come either from
 * attAttachData (0x0006800F) directly or from PR_ATTACH_DATA_OBJ inside the
 * attAttachment (0x00069005) MAPI property stream; its name comes from
 * attAttachTitle (0x00018010) or the longer PR_ATTACH_LONG_FILENAME.  The
 * message body is not an attachment at all - it lives in the message-level
 * attributes as compressed RTF (LZFu, MS-OXRTFCP), HTML or plain text - so
 * it is rendered and appended as one synthetic member after every real
 * attachment, exactly as the reference does.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/tnef/xx_tnef.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef TNEF
#define XX_TNEF_FILE_TYPE XX_FILE_TYPE_TNEF
#else
#define XX_TNEF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define TNEF_SIGNATURE UINT32_C(0x223E9F78)
#define TNEF_HEADER_SIZE 6
#define TNEF_ATTRIBUTE_HEADER_SIZE 9
#define TNEF_CHECKSUM_SIZE 2
#define TNEF_MAX_MEMBERS 100000U
#define TNEF_MAX_ATTRIBUTE_SIZE INT64_C(0x10000000)
#define TNEF_MAX_VALUE_COUNT INT64_C(0x100000)
#define TNEF_MAX_INPUT INT64_C(0x10000000)

#define TNEF_ATT_ATTACH_RENDDATA UINT32_C(0x00069002)
#define TNEF_ATT_ATTACH_TITLE UINT32_C(0x00018010)
#define TNEF_ATT_ATTACH_DATA UINT32_C(0x0006800F)
#define TNEF_ATT_ATTACHMENT UINT32_C(0x00069005)

/* Message-level attributes are matched on the attName half only: the attType
 * half varies between writers for the same attribute. */
#define TNEF_ATT_NAME_BODY 0x800CU
#define TNEF_ATT_NAME_MAPI_PROPS 0x9003U

#define TNEF_PR_ATTACH_DATA_OBJ 0x3701U
#define TNEF_PR_ATTACH_LONG_FILENAME 0x3707U
#define TNEF_PR_BODY 0x1000U
#define TNEF_PR_RTF_COMPRESSED 0x1009U
#define TNEF_PR_BODY_HTML 0x1013U
#define TNEF_PR_BODY_HTML_A 0x1014U

#define TNEF_PT_UNSPECIFIED 0x0000U
#define TNEF_PT_NULL 0x0001U
#define TNEF_PT_OBJECT 0x000DU
#define TNEF_PT_STRING8 0x001EU
#define TNEF_PT_UNICODE 0x001FU
#define TNEF_PT_BINARY 0x0102U

/* The body is rendered in memory, so both the compressed source and its
 * expansion are bounded well below the generic attribute cap. */
#define TNEF_MAX_BODY_SOURCE_SIZE INT64_C(0x2000000)
#define TNEF_MAX_BODY_SIZE INT64_C(0x4000000)

#define TNEF_LZFU_COMPRESSED UINT32_C(0x75465A4C)
#define TNEF_LZFU_UNCOMPRESSED UINT32_C(0x414C454D)
#define TNEF_LZFU_DICTIONARY_SIZE 4096

static const char TNEF_LZFU_INIT[] =
    "{\\rtf1\\ansi\\mac\\deff0\\deftab720{\\fonttbl;}{\\f0\\fnil \\froman "
    "\\fswiss \\fmodern \\fscript \\fdecor MS Sans SerifSymbolArialTimes New "
    "RomanCourier{\\colortbl\\red0\\green0\\blue0\r\n\\par "
    "\\pard\\plain\\f0\\fs20\\b\\i\\u\\tab\\tx";

typedef struct tnef_member_s {
    char *name;
    int64_t data_offset;  /* offset into the file buffer; -1 when inline */
    int64_t size;
    uint8_t *inline_data; /* owned; set for the synthetic body member */
} tnef_member;

typedef struct tnef_stream_s {
    uint8_t *data;
    int64_t size;
    tnef_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} tnef_stream;

/* A growable byte buffer; every growth is checked and the caller sees a
 * failure instead of a truncated buffer. */
typedef struct tnef_buf_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
    bool failed;
} tnef_buf;

typedef struct tnef_sink_s {
    int64_t *data_offset;
    int64_t *data_size;
    char **file_name;
    const uint8_t **rtf;
    int64_t *rtf_size;
    const uint8_t **html;
    int64_t *html_size;
    const uint8_t **body;
    int64_t *body_size;
} tnef_sink;

static uint16_t tnef_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t tnef_le32(const uint8_t *bytes) {
    return (uint32_t)tnef_le16(bytes) |
           ((uint32_t)tnef_le16(bytes + 2U) << 16U);
}

static bool tnef_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static int64_t tnef_pad4(int64_t value) {
    if (value < 0 || value > INT64_MAX - 3) return -1;
    return (value + 3) & ~(int64_t)3;
}

static bool tnef_is_variable_type(uint16_t base_type) {
    return base_type == TNEF_PT_STRING8 || base_type == TNEF_PT_UNICODE ||
           base_type == TNEF_PT_BINARY || base_type == TNEF_PT_OBJECT;
}

/* -1 for a type that has no fixed width. */
static int32_t tnef_fixed_type_size(uint16_t base_type) {
    switch (base_type) {
        case 0x0002U: return 2;  /* PT_I2 */
        case 0x0003U: return 4;  /* PT_LONG */
        case 0x0004U: return 4;  /* PT_R4 */
        case 0x0005U: return 8;  /* PT_DOUBLE */
        case 0x0006U: return 8;  /* PT_CURRENCY */
        case 0x0007U: return 8;  /* PT_APPTIME */
        case 0x000AU: return 4;  /* PT_ERROR */
        case 0x000BU: return 2;  /* PT_BOOLEAN */
        case 0x0014U: return 8;  /* PT_I8 */
        case 0x0040U: return 8;  /* PT_SYSTIME */
        case 0x0048U: return 16; /* PT_CLSID */
        default: return -1;
    }
}

static void tnef_buf_init(tnef_buf *buf) {
    buf->data = NULL;
    buf->size = 0U;
    buf->capacity = 0U;
    buf->failed = false;
}

static void tnef_buf_cleanup(tnef_buf *buf) {
    if (buf->data) xx_mem_free(buf->data);
    tnef_buf_init(buf);
}

static bool tnef_buf_reserve(tnef_buf *buf, size_t extra) {
    size_t needed, capacity;
    uint8_t *grown;
    if (buf->failed) return false;
    if (extra > SIZE_MAX - buf->size) {
        buf->failed = true;
        return false;
    }
    needed = buf->size + extra;
    if (needed <= buf->capacity) return true;
    capacity = buf->capacity != 0U ? buf->capacity : 256U;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) {
            buf->failed = true;
            return false;
        }
        capacity *= 2U;
    }
    grown = (uint8_t *)xx_mem_realloc(buf->data, capacity);
    if (!grown) {
        buf->failed = true;
        return false;
    }
    buf->data = grown;
    buf->capacity = capacity;
    return true;
}

static void tnef_buf_byte(tnef_buf *buf, uint8_t value) {
    if (!tnef_buf_reserve(buf, 1U)) return;
    buf->data[buf->size++] = value;
}

static void tnef_buf_bytes(tnef_buf *buf, const uint8_t *values, size_t size) {
    if (size == 0U || !tnef_buf_reserve(buf, size)) return;
    xx_rt_memcpy(buf->data + buf->size, values, size);
    buf->size += size;
}

static void tnef_buf_utf8(tnef_buf *buf, uint32_t code_point) {
    if (code_point < 0x80U) {
        tnef_buf_byte(buf, (uint8_t)code_point);
    } else if (code_point < 0x800U) {
        tnef_buf_byte(buf, (uint8_t)(0xC0U | (code_point >> 6U)));
        tnef_buf_byte(buf, (uint8_t)(0x80U | (code_point & 0x3FU)));
    } else if (code_point < 0x10000U) {
        tnef_buf_byte(buf, (uint8_t)(0xE0U | (code_point >> 12U)));
        tnef_buf_byte(buf, (uint8_t)(0x80U | ((code_point >> 6U) & 0x3FU)));
        tnef_buf_byte(buf, (uint8_t)(0x80U | (code_point & 0x3FU)));
    } else {
        tnef_buf_byte(buf, (uint8_t)(0xF0U | (code_point >> 18U)));
        tnef_buf_byte(buf, (uint8_t)(0x80U | ((code_point >> 12U) & 0x3FU)));
        tnef_buf_byte(buf, (uint8_t)(0x80U | ((code_point >> 6U) & 0x3FU)));
        tnef_buf_byte(buf, (uint8_t)(0x80U | (code_point & 0x3FU)));
    }
}

static bool tnef_word_eq(const uint8_t *word, size_t size, const char *name) {
    size_t length = xx_rt_strlen(name);
    return length == size && xx_rt_memcmp(word, name, size) == 0;
}

/* --------------------------------------------------------------------------
 * PR_RTF_COMPRESSED (LZFu, MS-OXRTFCP)
 *
 *   u32 compressed size (the stream length minus these four bytes)
 *   u32 uncompressed size
 *   u32 0x75465A4C "LZFu" compressed, 0x414C454D "MELA" stored
 *   u32 CRC
 *
 * then control bytes, low bit first: a clear bit is one literal byte, a set
 * bit is a 2-byte back reference (12-bit offset, 4-bit length biased by 2)
 * into a 4096-byte ring dictionary preloaded with the fixed RTF preamble and
 * written at its length.  A reference whose offset equals the write pointer
 * ends the stream. */
static bool tnef_decompress_rtf(const uint8_t *stream, int64_t stream_size,
                                tnef_buf *output) {
    uint8_t dictionary[TNEF_LZFU_DICTIONARY_SIZE];
    uint32_t compressed_size, raw_size, compression_type;
    int32_t init_size, write;
    int64_t end, position;
    if (stream_size < 16) return false;
    compressed_size = tnef_le32(stream);
    raw_size = tnef_le32(stream + 4U);
    compression_type = tnef_le32(stream + 8U);
    if ((int64_t)raw_size > TNEF_MAX_BODY_SIZE) return false;
    if (compression_type == TNEF_LZFU_UNCOMPRESSED) {
        int64_t available = stream_size - 16;
        int64_t size = (int64_t)raw_size < available ? (int64_t)raw_size
                                                     : available;
        if (size <= 0) return false;
        tnef_buf_bytes(output, stream + 16, (size_t)size);
        return !output->failed && output->size != 0U;
    }
    if (compression_type != TNEF_LZFU_COMPRESSED) return false;
    /* The declared compressed size is advisory; the stream is the
     * authority. */
    end = stream_size;
    if ((int64_t)compressed_size + 4 < end) end = (int64_t)compressed_size + 4;
    init_size = (int32_t)(sizeof(TNEF_LZFU_INIT) - 1U);
    if (init_size <= 0 || init_size > TNEF_LZFU_DICTIONARY_SIZE) return false;
    xx_mem_zero(dictionary, sizeof(dictionary));
    xx_rt_memcpy(dictionary, TNEF_LZFU_INIT, (size_t)init_size);
    write = init_size;
    position = 16;
    while ((int64_t)output->size < (int64_t)raw_size && position < end) {
        uint8_t control = stream[position++];
        int bit;
        for (bit = 0; bit < 8; ++bit) {
            if ((int64_t)output->size >= (int64_t)raw_size) break;
            if ((control >> bit) & 1U) {
                uint32_t first, second;
                int32_t offset, length, index;
                if (position + 2 > end) return !output->failed &&
                                                output->size != 0U;
                first = stream[position];
                second = stream[position + 1];
                position += 2;
                offset = (int32_t)(((first << 4U) | (second >> 4U)) & 0xFFFU);
                length = (int32_t)((second & 0x0FU) + 2U);
                if (offset == write) return !output->failed &&
                                            output->size != 0U;
                for (index = 0; index < length; ++index) {
                    uint8_t byte;
                    if ((int64_t)output->size >= (int64_t)raw_size) break;
                    byte = dictionary[(offset + index) &
                                      (TNEF_LZFU_DICTIONARY_SIZE - 1)];
                    tnef_buf_byte(output, byte);
                    if (output->failed) return false;
                    dictionary[write] = byte;
                    write = (write + 1) & (TNEF_LZFU_DICTIONARY_SIZE - 1);
                }
            } else {
                uint8_t byte;
                if (position >= end) break;
                byte = stream[position++];
                tnef_buf_byte(output, byte);
                if (output->failed) return false;
                dictionary[write] = byte;
                write = (write + 1) & (TNEF_LZFU_DICTIONARY_SIZE - 1);
            }
        }
    }
    return !output->failed && output->size != 0U;
}

/* Destinations whose contents are markup bookkeeping rather than text. */
static bool tnef_is_skipped_destination(const uint8_t *word, size_t size) {
    static const char *const names[] = {
        "fonttbl", "colortbl", "stylesheet", "info", "pict", "object",
        "header", "footer", "headerl", "headerr", "footerl", "footerr",
        "generator", "filetbl", "listtable", "listoverridetable", "revtbl",
        "rsidtbl", "xmlnstbl", "panose", "falt", "fname", "author",
        "operator", "company", "creatim", "revtim", "printim", "buptim",
        "title", "subject", "keywords", "comment", "doccomm", "userprops",
        "themedata", "colorschememapping", "datastore", "latentstyles",
        "fldinst", "nonshppict"};
    size_t index;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index)
        if (tnef_word_eq(word, size, names[index])) return true;
    return false;
}

static int tnef_hex_digit(uint8_t c) {
    if (c >= (uint8_t)'0' && c <= (uint8_t)'9') return c - (uint8_t)'0';
    if (c >= (uint8_t)'a' && c <= (uint8_t)'f') return c - (uint8_t)'a' + 10;
    if (c >= (uint8_t)'A' && c <= (uint8_t)'F') return c - (uint8_t)'A' + 10;
    return -1;
}

/* A deliberately small RTF-to-text pass: it keeps literal text and the few
 * control words that carry layout, and drops the rest along with the
 * bookkeeping destinations above.  It is a rendering, not a parser. */
static void tnef_rtf_to_text(const uint8_t *rtf, int64_t size,
                             tnef_buf *text) {
    int64_t position = 0;
    int32_t depth = 0, skip_depth = -1;
    int32_t unicode_skip = 1, pending_skip = 0;
    bool skipping = false, ignorable = false;
    while (position < size && !text->failed) {
        uint8_t character = rtf[position];
        if (character == (uint8_t)'\\') {
            uint8_t next;
            int64_t word_end, number_end;
            int64_t number = 0;
            bool negative = false, has_number = false;
            const uint8_t *word;
            size_t word_size;
            ++position;
            if (position >= size) break;
            next = rtf[position];
            if (next == (uint8_t)'\'') {
                if (position + 2 < size) {
                    int high = tnef_hex_digit(rtf[position + 1]);
                    int low = tnef_hex_digit(rtf[position + 2]);
                    if (high >= 0 && low >= 0 && !skipping) {
                        if (pending_skip > 0) --pending_skip;
                        else tnef_buf_byte(text, (uint8_t)((high << 4) | low));
                    }
                    position += 3;
                } else {
                    ++position;
                }
                continue;
            }
            if (next == (uint8_t)'*') {
                ignorable = true;
                ++position;
                continue;
            }
            if (next == (uint8_t)'\\' || next == (uint8_t)'{' ||
                next == (uint8_t)'}') {
                if (!skipping) {
                    if (pending_skip > 0) --pending_skip;
                    else tnef_buf_byte(text, next);
                }
                ++position;
                continue;
            }
            if (next == (uint8_t)'\r' || next == (uint8_t)'\n') {
                ++position;
                continue;
            }
            if (next == (uint8_t)'~') {
                if (!skipping) tnef_buf_byte(text, (uint8_t)' ');
                ++position;
                continue;
            }
            word_end = position;
            while (word_end < size &&
                   ((rtf[word_end] >= (uint8_t)'a' &&
                     rtf[word_end] <= (uint8_t)'z') ||
                    (rtf[word_end] >= (uint8_t)'A' &&
                     rtf[word_end] <= (uint8_t)'Z')))
                ++word_end;
            if (word_end == position) {
                /* an unknown one-character control symbol */
                ++position;
                ignorable = false;
                continue;
            }
            word = rtf + position;
            word_size = (size_t)(word_end - position);
            number_end = word_end;
            if (number_end < size && rtf[number_end] == (uint8_t)'-') {
                negative = true;
                ++number_end;
            }
            while (number_end < size && rtf[number_end] >= (uint8_t)'0' &&
                   rtf[number_end] <= (uint8_t)'9') {
                if (number < 0x7FFFFFFF)
                    number = number * 10 + (rtf[number_end] - (uint8_t)'0');
                has_number = true;
                ++number_end;
            }
            if (number_end < size && rtf[number_end] == (uint8_t)' ')
                ++number_end;
            position = number_end;
            if (ignorable || tnef_is_skipped_destination(word, word_size)) {
                if (!skipping) {
                    skipping = true;
                    skip_depth = depth - 1;
                }
                ignorable = false;
                continue;
            }
            ignorable = false;
            if (skipping) continue;
            if (tnef_word_eq(word, word_size, "par") ||
                tnef_word_eq(word, word_size, "line") ||
                tnef_word_eq(word, word_size, "sect")) {
                tnef_buf_byte(text, (uint8_t)'\r');
            } else if (tnef_word_eq(word, word_size, "tab")) {
                tnef_buf_byte(text, (uint8_t)'\t');
            } else if (tnef_word_eq(word, word_size, "uc")) {
                if (has_number && !negative && number >= 0 && number <= 0xFF)
                    unicode_skip = (int32_t)number;
            } else if (tnef_word_eq(word, word_size, "u")) {
                if (has_number) {
                    int64_t code_point = negative ? (65536 - number) : number;
                    if (code_point > 0 && code_point <= 0x10FFFF)
                        tnef_buf_utf8(text, (uint32_t)code_point);
                    pending_skip = unicode_skip;
                }
            } else if (tnef_word_eq(word, word_size, "lquote") ||
                       tnef_word_eq(word, word_size, "rquote")) {
                tnef_buf_byte(text, (uint8_t)'\'');
            } else if (tnef_word_eq(word, word_size, "ldblquote") ||
                       tnef_word_eq(word, word_size, "rdblquote")) {
                tnef_buf_byte(text, (uint8_t)'"');
            } else if (tnef_word_eq(word, word_size, "emdash") ||
                       tnef_word_eq(word, word_size, "endash")) {
                tnef_buf_byte(text, (uint8_t)'-');
            } else if (tnef_word_eq(word, word_size, "bullet")) {
                tnef_buf_byte(text, (uint8_t)'*');
            }
            continue;
        }
        if (character == (uint8_t)'{') {
            ++depth;
            ++position;
            continue;
        }
        if (character == (uint8_t)'}') {
            --depth;
            if (skipping && depth <= skip_depth) {
                skipping = false;
                skip_depth = -1;
            }
            ++position;
            continue;
        }
        if (character == (uint8_t)'\r' || character == (uint8_t)'\n') {
            /* Literal line breaks in an RTF file are formatting, not text. */
            ++position;
            continue;
        }
        if (!skipping) {
            if (pending_skip > 0) --pending_skip;
            else tnef_buf_byte(text, character);
        }
        ++position;
    }
}

static uint8_t tnef_lower(uint8_t c) {
    return (c >= (uint8_t)'A' && c <= (uint8_t)'Z') ? (uint8_t)(c + 32U) : c;
}

static bool tnef_name_eq(const uint8_t *name, size_t size,
                         const char *expected) {
    size_t length = xx_rt_strlen(expected);
    size_t index;
    if (length != size) return false;
    for (index = 0U; index < size; ++index)
        if (tnef_lower(name[index]) != (uint8_t)expected[index]) return false;
    return true;
}

static void tnef_replace_entity(tnef_buf *buf, const char *entity,
                                uint8_t replacement) {
    size_t length = xx_rt_strlen(entity);
    size_t read = 0U, write = 0U;
    if (length == 0U || buf->failed) return;
    while (read < buf->size) {
        if (buf->size - read >= length &&
            xx_rt_memcmp(buf->data + read, entity, length) == 0) {
            buf->data[write++] = replacement;
            read += length;
        } else {
            buf->data[write++] = buf->data[read++];
        }
    }
    buf->size = write;
}

/* PR_BODY_HTML is real HTML rather than RTF-encapsulated HTML, so it gets
 * its own equally small rendering. */
static void tnef_html_to_text(const uint8_t *html, int64_t size,
                              tnef_buf *text) {
    int64_t position = 0;
    bool in_script = false;
    while (position < size && !text->failed) {
        if (html[position] == (uint8_t)'<') {
            int64_t tag_end = position + 1;
            const uint8_t *tag;
            size_t tag_size, name_size;
            bool closing;
            while (tag_end < size && html[tag_end] != (uint8_t)'>') ++tag_end;
            if (tag_end >= size) break;
            tag = html + position + 1;
            tag_size = (size_t)(tag_end - position - 1);
            closing = tag_size != 0U && tag[0] == (uint8_t)'/';
            if (closing) {
                ++tag;
                --tag_size;
            }
            name_size = 0U;
            while (name_size < tag_size && tag[name_size] != (uint8_t)' ')
                ++name_size;
            if (tnef_name_eq(tag, name_size, "script") ||
                tnef_name_eq(tag, name_size, "style"))
                in_script = !closing;
            if (!in_script &&
                (tnef_name_eq(tag, name_size, "br") ||
                 tnef_name_eq(tag, name_size, "p") ||
                 tnef_name_eq(tag, name_size, "div") ||
                 tnef_name_eq(tag, name_size, "tr") ||
                 tnef_name_eq(tag, name_size, "li"))) {
                /* HTML collapses run-in whitespace, so a line does not keep
                 * the spaces that only separated it from the breaking tag. */
                while (text->size != 0U &&
                       (text->data[text->size - 1U] == (uint8_t)' ' ||
                        text->data[text->size - 1U] == (uint8_t)'\t'))
                    --text->size;
                tnef_buf_byte(text, (uint8_t)'\r');
                tnef_buf_byte(text, (uint8_t)'\n');
            }
            position = tag_end + 1;
            continue;
        }
        if (html[position] == (uint8_t)'\r' || html[position] == (uint8_t)'\n') {
            ++position;
            continue;
        }
        if (!in_script) tnef_buf_byte(text, html[position]);
        ++position;
    }
    tnef_replace_entity(text, "&nbsp;", (uint8_t)' ');
    tnef_replace_entity(text, "&amp;", (uint8_t)'&');
    tnef_replace_entity(text, "&lt;", (uint8_t)'<');
    tnef_replace_entity(text, "&gt;", (uint8_t)'>');
    tnef_replace_entity(text, "&quot;", (uint8_t)'"');
    tnef_replace_entity(text, "&#39;", (uint8_t)'\'');
}

/* Attachment names travel in from the message, so the filesystem-facing form
 * folds the reserved set to '_' and refuses a name that would leave the
 * output directory. */
static char *tnef_make_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t index, output = 0U;
    for (index = 0U; index < size; ++index)
        if (bytes[index] == 0U) break;
    size = index;
    if (size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = bytes[index];
        bool reserved = c < 0x20U || c == (uint8_t)'<' || c == (uint8_t)'>' ||
                        c == (uint8_t)':' || c == (uint8_t)'"' ||
                        c == (uint8_t)'/' || c == (uint8_t)'\\' ||
                        c == (uint8_t)'|' || c == (uint8_t)'?' ||
                        c == (uint8_t)'*';
        name[output++] = reserved ? '_' : (char)c;
    }
    while (output != 0U &&
           (name[output - 1U] == ' ' || name[output - 1U] == '.')) --output;
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static char *tnef_default_name(size_t index) {
    static const char prefix[] = "attachment";
    char digits[24];
    char *name;
    size_t prefix_size = sizeof(prefix) - 1U;
    size_t count = 0U, at;
    size_t value = index;
    do {
        digits[count++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    name = (char *)xx_mem_alloc(prefix_size + count + 1U);
    if (!name) return NULL;
    xx_rt_memcpy(name, prefix, prefix_size);
    for (at = 0U; at < count; ++at)
        name[prefix_size + at] = digits[count - 1U - at];
    name[prefix_size + count] = 0;
    return name;
}

static bool tnef_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    for (at = name; *at; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '/' || c == '\\' || c < 0x20U)
            return false;
    }
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return false;
    return true;
}

static void tnef_stream_free(void *opaque) {
    tnef_stream *stream = (tnef_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
        if (stream->items[index].inline_data)
            xx_mem_free(stream->items[index].inline_data);
    }
    if (stream->items) xx_mem_free(stream->items);
    if (stream->data) xx_mem_free(stream->data);
    xx_mem_free(stream);
}

static bool tnef_add_member(tnef_stream *stream, const tnef_member *member) {
    tnef_member *grown;
    if (!stream || !member || stream->count >= TNEF_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (tnef_member *)xx_mem_realloc(stream->items,
                                          (stream->count + 1U) *
                                              sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* A faithful walk of the MAPI property stream: the offsets only stay in step
 * with the file if every property is measured, including the ones nothing
 * here is interested in. */
static bool tnef_scan_properties(const uint8_t *stream, int64_t stream_size,
                                 const tnef_sink *sink) {
    int64_t property_count, position, property;
    if (stream_size < 4) return false;
    property_count = (int64_t)tnef_le32(stream);
    if (property_count < 0 || property_count > TNEF_MAX_VALUE_COUNT)
        return false;
    position = 4;
    for (property = 0; property < property_count; ++property) {
        uint16_t type, id, base_type;
        bool multi, variable;
        int32_t fixed_size;
        int64_t value_count, value;
        if (position + 4 > stream_size) break;
        type = tnef_le16(stream + position);
        id = tnef_le16(stream + position + 2);
        position += 4;
        if (id >= 0x8000U) {
            uint32_t kind;
            if (position + 20 > stream_size) break;
            position += 16;
            kind = tnef_le32(stream + position);
            position += 4;
            if (kind == 0U) {
                position += 4;
            } else if (kind == 1U) {
                int64_t name_length, padded_name;
                if (position + 4 > stream_size) break;
                name_length = (int64_t)tnef_le32(stream + position);
                padded_name = tnef_pad4(name_length);
                if (padded_name < 0) break;
                position += 4 + padded_name;
            } else {
                break;
            }
        }
        base_type = (uint16_t)(type & 0x0FFFU);
        multi = (type & 0x1000U) != 0U;
        variable = tnef_is_variable_type(base_type);
        fixed_size = tnef_fixed_type_size(base_type);
        value_count = 1;
        if (multi || variable) {
            if (position + 4 > stream_size) break;
            value_count = (int64_t)tnef_le32(stream + position);
            position += 4;
        }
        if (value_count < 0 || value_count > TNEF_MAX_VALUE_COUNT) break;
        for (value = 0; value < value_count; ++value) {
            if (fixed_size >= 0) {
                int64_t padded = tnef_pad4(fixed_size);
                if (padded < 0 || position + padded > stream_size) return true;
                position += padded;
                continue;
            }
            if (variable) {
                int64_t length, value_offset, value_size, padded;
                if (position + 4 > stream_size) return true;
                length = (int64_t)tnef_le32(stream + position);
                position += 4;
                if (length < 0 || position + length > stream_size) return true;
                value_offset = position;
                value_size = length;
                /* A PT_OBJECT value keeps a 16-byte interface GUID in front
                 * of the real payload. */
                if (base_type == TNEF_PT_OBJECT && value_size >= 16) {
                    value_offset += 16;
                    value_size -= 16;
                }
                if (id == TNEF_PR_ATTACH_DATA_OBJ && sink->data_offset &&
                    sink->data_size && *sink->data_offset < 0) {
                    *sink->data_offset = value_offset;
                    *sink->data_size = value_size;
                } else if (id == TNEF_PR_ATTACH_LONG_FILENAME &&
                           sink->file_name && !*sink->file_name &&
                           value_size > 0) {
                    *sink->file_name = tnef_make_name(stream + value_offset,
                                                      (size_t)value_size);
                } else if (id == TNEF_PR_RTF_COMPRESSED && sink->rtf &&
                           !*sink->rtf && value_size > 0 &&
                           value_size <= TNEF_MAX_BODY_SOURCE_SIZE) {
                    *sink->rtf = stream + value_offset;
                    *sink->rtf_size = value_size;
                } else if ((id == TNEF_PR_BODY_HTML ||
                            id == TNEF_PR_BODY_HTML_A) && sink->html &&
                           !*sink->html && value_size > 0 &&
                           value_size <= TNEF_MAX_BODY_SOURCE_SIZE) {
                    *sink->html = stream + value_offset;
                    *sink->html_size = value_size;
                } else if (id == TNEF_PR_BODY && sink->body && !*sink->body &&
                           value_size > 0 &&
                           value_size <= TNEF_MAX_BODY_SOURCE_SIZE) {
                    *sink->body = stream + value_offset;
                    *sink->body_size = value_size;
                }
                padded = tnef_pad4(length);
                if (padded < 0) return true;
                position += padded;
            } else if (base_type == TNEF_PT_UNSPECIFIED ||
                       base_type == TNEF_PT_NULL) {
                /* no payload */
            } else {
                /* An unknown fixed width makes the rest of the stream
                 * unparseable; keep whatever was already recovered. */
                return true;
            }
        }
    }
    return true;
}

static bool tnef_parse(Abstractformat *format, tnef_stream **result) {
    tnef_stream *stream = NULL;
    uint8_t *data = NULL;
    int64_t total, size, offset;
    tnef_member current;
    bool has_current = false;
    const uint8_t *rtf_source = NULL, *html_source = NULL;
    const uint8_t *mapi_body = NULL, *plain_body = NULL;
    int64_t rtf_size = 0, html_size = 0, mapi_body_size = 0;
    int64_t plain_body_size = 0;
    size_t index;
    tnef_buf body_text;
    unsigned valid_attributes = 0U;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < TNEF_HEADER_SIZE || size > TNEF_MAX_INPUT) return false;
    data = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!data) return false;
    if (!tnef_read_at(format->device, format->base_address, data,
                      (size_t)size) ||
        tnef_le32(data) != TNEF_SIGNATURE || tnef_le16(data + 4U) == 0U) {
        xx_mem_free(data);
        return false;
    }
    stream = (tnef_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        xx_mem_free(data);
        return false;
    }
    stream->data = data;
    stream->size = size;
    xx_mem_zero(&current, sizeof(current));
    offset = TNEF_HEADER_SIZE;
    while (offset + TNEF_ATTRIBUTE_HEADER_SIZE <= size) {
        uint8_t level;
        uint32_t att_id, raw_length;
        int64_t length, body_offset, checksum_offset;
        uint32_t sum = 0U;
        int64_t at;
        if (stream->count >= TNEF_MAX_MEMBERS) break;
        level = data[offset];
        att_id = tnef_le32(data + offset + 1);
        raw_length = tnef_le32(data + offset + 5);
        if (raw_length > (uint32_t)INT32_MAX) break;
        length = (int64_t)raw_length;
        body_offset = offset + TNEF_ATTRIBUTE_HEADER_SIZE;
        if ((level != 1U && level != 2U) ||
            body_offset > size || length > size - body_offset) break;
        /* The trailing checksum is the anchor: an attribute whose data does
         * not sum to it ends the walk rather than being trusted. */
        checksum_offset = body_offset + length;
        if (checksum_offset + TNEF_CHECKSUM_SIZE > size) break;
        for (at = 0; at < length; ++at) sum += data[body_offset + at];
        if ((uint16_t)(sum & 0xFFFFU) != tnef_le16(data + checksum_offset))
            break;
        ++valid_attributes;
        offset = checksum_offset + TNEF_CHECKSUM_SIZE;
        if (level == 1U) {
            /* Message level: only the body sources are read here. */
            uint16_t att_name = (uint16_t)(att_id & 0xFFFFU);
            if (att_name == TNEF_ATT_NAME_BODY && !plain_body && length > 0 &&
                length <= TNEF_MAX_BODY_SOURCE_SIZE) {
                plain_body = data + body_offset;
                plain_body_size = length;
            } else if (att_name == TNEF_ATT_NAME_MAPI_PROPS && length > 0 &&
                       length <= TNEF_MAX_ATTRIBUTE_SIZE) {
                const uint8_t *rtf = NULL, *html = NULL, *body = NULL;
                int64_t rtf_length = 0, html_length = 0, body_length = 0;
                tnef_sink sink;
                xx_mem_zero(&sink, sizeof(sink));
                sink.rtf = &rtf;
                sink.rtf_size = &rtf_length;
                sink.html = &html;
                sink.html_size = &html_length;
                sink.body = &body;
                sink.body_size = &body_length;
                if (tnef_scan_properties(data + body_offset, length, &sink)) {
                    if (!rtf_source) {
                        rtf_source = rtf;
                        rtf_size = rtf_length;
                    }
                    if (!html_source) {
                        html_source = html;
                        html_size = html_length;
                    }
                    if (!mapi_body) {
                        mapi_body = body;
                        mapi_body_size = body_length;
                    }
                }
            }
            continue;
        }
        if (att_id == TNEF_ATT_ATTACH_RENDDATA) {
            if (has_current && !tnef_add_member(stream, &current)) goto fail;
            xx_mem_zero(&current, sizeof(current));
            current.data_offset = -1;
            has_current = true;
        } else if (!has_current) {
            xx_mem_zero(&current, sizeof(current));
            current.data_offset = -1;
            has_current = true;
        }
        if (att_id == TNEF_ATT_ATTACH_TITLE) {
            if (length > 0) {
                char *title = tnef_make_name(data + body_offset,
                                             (size_t)length);
                if (!title) goto fail;
                if (current.name) xx_str_free(current.name);
                current.name = title;
            }
        } else if (att_id == TNEF_ATT_ATTACH_DATA) {
            current.data_offset = body_offset;
            current.size = length;
        } else if (att_id == TNEF_ATT_ATTACHMENT) {
            if (length > 0 && length <= TNEF_MAX_ATTRIBUTE_SIZE) {
                int64_t value_offset = -1, value_size = -1;
                char *long_name = NULL;
                tnef_sink sink;
                xx_mem_zero(&sink, sizeof(sink));
                sink.data_offset = &value_offset;
                sink.data_size = &value_size;
                sink.file_name = &long_name;
                if (tnef_scan_properties(data + body_offset, length, &sink)) {
                    if (current.data_offset < 0 && value_offset >= 0 &&
                        value_size >= 0) {
                        current.data_offset = body_offset + value_offset;
                        current.size = value_size;
                    }
                    if (long_name) {
                        if (current.name) xx_str_free(current.name);
                        current.name = long_name;
                        long_name = NULL;
                    }
                }
                if (long_name) xx_str_free(long_name);
            }
        }
    }
    if (has_current && !tnef_add_member(stream, &current)) goto fail;
    has_current = false;
    if (valid_attributes == 0U) goto fail;
    for (index = 0U; index < stream->count; ++index) {
        tnef_member *member = &stream->items[index];
        if (member->data_offset < 0) {
            /* An attachment the walker saw but that carries no payload; the
             * reference emits it as an empty file. */
            member->data_offset = 0;
            member->size = 0;
        }
        if (!member->name) {
            member->name = tnef_default_name(index + 1U);
            if (!member->name) goto fail;
        }
    }
    /* The message body is not stored as an archive member by the format.  It
     * is rendered here and appended as ONE synthetic member, after every
     * attachment, so no attachment name, index, offset or byte changes.
     * Compressed RTF wins where several sources are present. */
    tnef_buf_init(&body_text);
    if (rtf_source) {
        tnef_buf rtf;
        tnef_buf_init(&rtf);
        if (tnef_decompress_rtf(rtf_source, rtf_size, &rtf))
            tnef_rtf_to_text(rtf.data, (int64_t)rtf.size, &body_text);
        tnef_buf_cleanup(&rtf);
    }
    if (body_text.size == 0U && html_source)
        tnef_html_to_text(html_source, html_size, &body_text);
    if (body_text.size == 0U && mapi_body)
        tnef_buf_bytes(&body_text, mapi_body, (size_t)mapi_body_size);
    if (body_text.size == 0U && plain_body)
        tnef_buf_bytes(&body_text, plain_body, (size_t)plain_body_size);
    if (body_text.failed) {
        tnef_buf_cleanup(&body_text);
        goto fail;
    }
    /* A rendering that ends in the RTF stream's terminating NUL is trimmed:
     * the NUL belongs to the container, not to the text. */
    while (body_text.size != 0U && body_text.data[body_text.size - 1U] == 0U)
        --body_text.size;
    if (body_text.size != 0U &&
        (int64_t)body_text.size <= TNEF_MAX_BODY_SIZE &&
        stream->count < TNEF_MAX_MEMBERS) {
        tnef_member body;
        static const char base_name[] = "Content.txt";
        char *name = (char *)xx_mem_alloc(sizeof(base_name));
        if (!name) {
            tnef_buf_cleanup(&body_text);
            goto fail;
        }
        xx_rt_memcpy(name, base_name, sizeof(base_name));
        xx_mem_zero(&body, sizeof(body));
        body.name = name;
        body.data_offset = -1;
        body.size = (int64_t)body_text.size;
        body.inline_data = body_text.data;
        body_text.data = NULL;
        if (!tnef_add_member(stream, &body)) {
            xx_str_free(name);
            xx_mem_free(body.inline_data);
            tnef_buf_cleanup(&body_text);
            goto fail;
        }
    }
    tnef_buf_cleanup(&body_text);
    /* A signed, checksum-clean TNEF that carries neither an attachment nor a
     * body is still a TNEF - it is simply empty - so validity rests on the
     * signature plus at least one attribute whose checksum agreed, not on
     * the member count. */
    stream->archive_size = offset < size ? offset : size;
    *result = stream;
    return true;
fail:
    if (has_current) {
        if (current.name) xx_str_free(current.name);
        if (current.inline_data) xx_mem_free(current.inline_data);
    }
    tnef_stream_free(stream);
    return false;
}

static bool tnef_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *tnef_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool tnef_set_record(xx_archive_record *record,
                            const tnef_member *member, int64_t base_address) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->inline_data
                                ? 0
                                : base_address + member->data_offset;
    record->header_size = 0;
    record->data_offset = record->header_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

void xx_tnef_init(xx_tnef *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TNEF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/vnd.ms-tnef");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_tnef_check_is_valid;
    archive->format.handle_base_info = xx_tnef_handle_base_info;
    archive->format.get_format_size = xx_tnef_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_tnef_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_tnef_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_tnef_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_tnef_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_tnef_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_tnef_free_archive_records_reading;
}

xx_tnef *xx_tnef_create(xx_io_device *device, int64_t base_address) {
    xx_tnef *archive = (xx_tnef *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_tnef_init(archive, device, base_address);
    return archive;
}

void xx_tnef_destroy(xx_tnef *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_tnef_free(xx_tnef *archive) {
    if (!archive) return;
    xx_tnef_destroy(archive);
    xx_mem_free(archive);
}

bool xx_tnef_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    tnef_stream *stream;
    (void)pd;
    if (!tnef_parse(format, &stream)) return false;
    tnef_stream_free(stream);
    return true;
}

bool xx_tnef_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    tnef_stream *stream;
    xx_tnef *archive;
    (void)pd;
    if (!format || !tnef_parse(format, &stream)) return false;
    archive = (xx_tnef *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    tnef_stream_free(stream);
    return true;
}

int64_t xx_tnef_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_tnef_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_tnef_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_tnef_handle_base_info(format, pd))
               ? ((xx_tnef *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_tnef_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    tnef_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!tnef_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        tnef_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = tnef_stream_free;
    state->total_records = stream->count;
    if (!tnef_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count == 0U) return state;
    if (!tnef_set_record(&state->current_record, &stream->items[0],
                         format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_tnef_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_tnef_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    tnef_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (tnef_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = tnef_set_record(&state->current_record,
                                        &stream->items[stream->index],
                                        format->base_address);
    return state->has_record;
}

bool xx_tnef_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    tnef_stream *stream;
    tnef_member *member;
    const xx_var *path_option;
    const uint8_t *payload;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    size_t payload_size, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (tnef_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!tnef_safe_output_name(member->name) || member->size < 0 ||
        (uint64_t)member->size > (uint64_t)SIZE_MAX) return false;
    payload_size = (size_t)member->size;
    if (member->inline_data) {
        payload = member->inline_data;
    } else {
        if (member->data_offset < 0 || member->data_offset > stream->size ||
            member->size > stream->size - member->data_offset) return false;
        payload = stream->data + member->data_offset;
    }
    path_option = tnef_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < payload_size) {
            ssize_t amount = xx_io_write(destination, payload + written,
                                         payload_size - written);
            if (amount <= 0 || (size_t)amount > payload_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_tnef_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
