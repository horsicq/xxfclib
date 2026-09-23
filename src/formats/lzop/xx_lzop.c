/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * lzop (.lzo) container reader.  The LZO1X bit decoder lives in src/algo/lzo
 * and the magic test in src/algo/lzop; this file adds the Abstractformat
 * surface.  It scans the container once to learn the exact extent, stored file
 * name and expanded size of every concatenated stream, then decodes one stream
 * at a time with its own block loop.  That loop applies the same header and
 * block rules as the src/algo/lzop codec, but it reads through a 64 KiB
 * read-ahead window, reuses its two block buffers, and batches output writes.
 * A container of millions of 1-byte blocks therefore costs a memory parse,
 * not three device reads, two allocations and one write per block.
 *
 * Record model: one archive record per lzop stream, named after the file name
 * that stream's header stores (a relative path when `lzop -P` stored one).
 * This is what `lzop -c a b > ab.lzo` creates and what `lzop -x` / U3
 * extract, and it matches the gz reader's one-record-per-member model.
 * xx_lzop_unpack_to_device() still produces the concatenation of every
 * stream, which is what `lzop -dc` writes.
 *
 * Naming: the codec in src/algo/lzop/xx_lzop.c already owns the exported
 * names xx_lzop_has_header(), xx_lzop_decode_device() and the macro
 * XX_LZOP_MAGIC_SIZE.  Every private helper here therefore carries the
 * xx_lzopfmt_ / XX_LZOPFMT_ prefix, which the codec does not use, and the
 * public reader entry points (xx_lzop_init, xx_lzop_check_is_valid, ...) are
 * names the codec does not define.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lzop/xx_lzop.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzo/xx_lzo.h"
#include "xxfclib/algo/lzop/xx_lzop.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved locally until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_LZOP exists in the enum. */
#ifdef LZOP
#define XX_LZOP_FILE_TYPE XX_FILE_TYPE_LZOP
#else
#define XX_LZOP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Name used when a stream header carries no usable stored file name. */
#define XX_LZOPFMT_PAYLOAD_NAME "payload"

#define XX_LZOPFMT_MIN_VERSION UINT16_C(0x0900)
#define XX_LZOPFMT_MAX_VERSION UINT16_C(0x1040)
#define XX_LZOPFMT_VERSION_LONG_HEADER UINT16_C(0x0940)

/* Absolute parse-time ceilings.  A block header is four attacker controlled
 * big-endian bytes, so a 12-byte block descriptor can otherwise claim a huge
 * expansion.  Both limits are enforced while SCANNING, before a single byte is
 * decoded or a single buffer is sized from the declared value.
 *
 * 64 MiB per block matches the codec and lzop's own MAX_BLOCK_SIZE (lzop never
 * writes a block larger than 256 KiB by default).  The total ceiling only
 * bounds the arithmetic; the decoder streams block by block, so memory stays
 * at two blocks whatever the total. */
#define XX_LZOPFMT_MAX_BLOCK_SIZE (UINT32_C(64) * UINT32_C(1024) * UINT32_C(1024))
#define XX_LZOPFMT_MAX_TOTAL_OUTPUT (UINT64_C(1) << 40U)
#define XX_LZOPFMT_MAX_BLOCKS UINT64_C(4194304) /* 1 TiB at lzop's 256 KiB block */
#define XX_LZOPFMT_MAX_STREAMS 4096U
#define XX_LZOPFMT_MAX_EXTRA_SIZE (UINT32_C(16) * UINT32_C(1024) * UINT32_C(1024))

#define XX_LZOPFMT_FLAG_ADLER_DATA UINT32_C(0x00000001)
#define XX_LZOPFMT_FLAG_ADLER_COMPRESSED UINT32_C(0x00000002)
#define XX_LZOPFMT_FLAG_HEADER_EXTRA UINT32_C(0x00000040)
#define XX_LZOPFMT_FLAG_CRC_DATA UINT32_C(0x00000100)
#define XX_LZOPFMT_FLAG_CRC_COMPRESSED UINT32_C(0x00000200)
#define XX_LZOPFMT_FLAG_MULTIPART UINT32_C(0x00000400)
#define XX_LZOPFMT_FLAG_FILTER UINT32_C(0x00000800)
#define XX_LZOPFMT_FLAG_HEADER_CRC UINT32_C(0x00001000)
/* Mirrors the codec: the documented flag bits plus the os/charset nibbles. */
#define XX_LZOPFMT_ALLOWED_FLAGS UINT32_C(0xfff03fff)

/** One concatenated lzop stream. */
typedef struct xx_lzopfmt_stream_s {
    int64_t offset;           /**< Absolute offset of the stream magic. */
    int64_t header_size;      /**< Magic + header (+ extra field) bytes. */
    int64_t size;             /**< Whole stream, end-of-stream marker included. */
    uint64_t uncompressed;    /**< Sum of the stream's expanded block sizes. */
    uint64_t blocks;
    uint64_t mtime;           /**< Seconds; high word only for >= 0x0940. */
    uint32_t mode;
    uint32_t flags;
    uint16_t version;
    uint16_t library_version;
    uint8_t method;
    uint8_t level;
    bool has_stored_name;     /**< Header carried a usable name. */
    char *name;               /**< Owned UTF-8 record name, never NULL. */
} xx_lzopfmt_stream;

/** Everything the one-pass scan learns about a container. */
typedef struct xx_lzopfmt_scan_s {
    xx_lzopfmt_stream *streams; /**< NULL unless the scan collected. */
    size_t count;               /**< Streams accepted. */
    size_t capacity;
    int64_t stream_size;        /**< Bytes consumed from base_address. */
    uint64_t uncompressed;      /**< Total expanded bytes, all streams. */
    uint64_t blocks;            /**< Block headers accepted, all streams. */
} xx_lzopfmt_scan;

/* Read-ahead window of the scan cursor.  Block headers are 8..20 bytes, so
 * without it a container of tiny blocks costs several device calls per block;
 * with it the walk is a memory parse plus one read per 64 KiB. */
#define XX_LZOPFMT_READAHEAD 65536U

/** Bounded, buffered forward cursor over the device. */
typedef struct xx_lzopfmt_cursor_s {
    xx_io_device *device;
    int64_t position;
    int64_t end;
    uint8_t *buffer;          /**< XX_LZOPFMT_READAHEAD bytes, owned. */
    int64_t buffer_start;     /**< Device offset of buffer[0]. */
    size_t buffer_length;     /**< Valid bytes in buffer. */
} xx_lzopfmt_cursor;

/** Running header checksums; lzop selects one of the two by flag. */
typedef struct xx_lzopfmt_sums_s {
    uint32_t adler32;
    uint32_t crc32;
} xx_lzopfmt_sums;

/* Output batching.  Blocks are verified before they are appended, so the
 * batch only ever holds bytes whose checksums passed. */
#define XX_LZOPFMT_OUTPUT_BUFFER 65536U

/** Block decoder state, shared by every stream one call decodes. */
typedef struct xx_lzopfmt_decoder_s {
    xx_lzopfmt_cursor cursor;
    xx_io_device *target;     /**< NULL: decode and verify only. */
    uint8_t *out;             /**< XX_LZOPFMT_OUTPUT_BUFFER bytes with a target. */
    size_t out_length;
    uint64_t produced;        /**< Expanded bytes of every stream decoded. */
    uint8_t *packed;          /**< Reused payload buffer. */
    size_t packed_capacity;
    uint8_t *expanded;        /**< Reused LZO1X output buffer. */
    size_t expanded_capacity;
} xx_lzopfmt_decoder;

/** One entry of the upper-case table: code points first..last map to
 *  code point + delta; with step 2 only every other one does. */
typedef struct xx_lzopfmt_case_range_s {
    uint16_t first;
    uint16_t last;
    int32_t delta;
    uint8_t step;
} xx_lzopfmt_case_range;

/** Case-folded record name, the key of the duplicate-name set. */
typedef struct xx_lzopfmt_key_s {
    uint8_t *data;
    size_t length;
} xx_lzopfmt_key;

static void xx_lzopfmt_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Cursor                                                              */
/* ------------------------------------------------------------------ */

static bool xx_lzopfmt_cursor_open(xx_lzopfmt_cursor *cursor,
                                   xx_io_device *device, int64_t start,
                                   int64_t end) {
    xx_mem_zero(cursor, sizeof(*cursor));
    if (!device || start < 0 || end < start) return false;
    cursor->buffer = (uint8_t *)xx_mem_alloc(XX_LZOPFMT_READAHEAD);
    if (!cursor->buffer) return false;
    cursor->device = device;
    cursor->position = start;
    cursor->end = end;
    cursor->buffer_start = start;
    return true;
}

static void xx_lzopfmt_cursor_close(xx_lzopfmt_cursor *cursor) {
    if (!cursor) return;
    if (cursor->buffer) xx_mem_free(cursor->buffer);
    cursor->buffer = NULL;
    cursor->buffer_length = 0U;
}

/* Re-aim an open cursor at [start, end); the window is dropped. */
static bool xx_lzopfmt_cursor_bound(xx_lzopfmt_cursor *cursor, int64_t start,
                                    int64_t end) {
    if (!cursor || !cursor->buffer || start < 0 || end < start) return false;
    cursor->position = start;
    cursor->end = end;
    cursor->buffer_start = start;
    cursor->buffer_length = 0U;
    return true;
}

/* Read size bytes at the current position straight into data, bypassing the
 * window; used for payloads at least as large as the window. */
static bool xx_lzopfmt_direct(xx_lzopfmt_cursor *cursor, uint8_t *data,
                              size_t size) {
    size_t done = 0U;
    cursor->buffer_start = cursor->position;
    cursor->buffer_length = 0U;
    if (xx_io_seek64(cursor->device, cursor->position, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(cursor->device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    cursor->position += (int64_t)size;
    cursor->buffer_start = cursor->position;
    return true;
}

/* Load the window starting at the current position. */
static bool xx_lzopfmt_fill(xx_lzopfmt_cursor *cursor) {
    int64_t available = cursor->end - cursor->position;
    size_t want = available > (int64_t)XX_LZOPFMT_READAHEAD
                      ? XX_LZOPFMT_READAHEAD
                      : (size_t)available;
    size_t done = 0U;
    cursor->buffer_start = cursor->position;
    cursor->buffer_length = 0U;
    if (want == 0U ||
        xx_io_seek64(cursor->device, cursor->position, SEEK_SET) != 0) {
        return false;
    }
    while (done < want) {
        ssize_t amount = xx_io_read(cursor->device, cursor->buffer + done,
                                    want - done);
        if (amount <= 0 || (size_t)amount > want - done) return false;
        done += (size_t)amount;
    }
    cursor->buffer_length = done;
    return true;
}

static bool xx_lzopfmt_read(xx_lzopfmt_cursor *cursor, void *data,
                            size_t size) {
    uint8_t *out = (uint8_t *)data;
    if (!cursor || !cursor->buffer || (!data && size != 0U) ||
        cursor->position < 0 || cursor->position > cursor->end ||
        (uint64_t)size > (uint64_t)(cursor->end - cursor->position)) {
        return false;
    }
    while (size != 0U) {
        size_t offset;
        size_t chunk;
        if (cursor->position < cursor->buffer_start ||
            cursor->position - cursor->buffer_start >=
                (int64_t)cursor->buffer_length) {
            /* A large payload goes straight to the caller: no double copy. */
            if (size >= XX_LZOPFMT_READAHEAD) {
                return xx_lzopfmt_direct(cursor, out, size);
            }
            if (!xx_lzopfmt_fill(cursor)) return false;
        }
        offset = (size_t)(cursor->position - cursor->buffer_start);
        chunk = cursor->buffer_length - offset;
        if (chunk > size) chunk = size;
        xx_rt_memcpy(out, cursor->buffer + offset, chunk);
        out += chunk;
        size -= chunk;
        cursor->position += (int64_t)chunk;
    }
    return true;
}

/* Advance without reading; the skipped bytes must exist, because the cursor
 * end is the device end.  The window is refilled lazily on the next read. */
static bool xx_lzopfmt_skip(xx_lzopfmt_cursor *cursor, uint64_t size) {
    if (!cursor || cursor->position < 0 || cursor->position > cursor->end ||
        size > (uint64_t)(cursor->end - cursor->position)) {
        return false;
    }
    cursor->position += (int64_t)size;
    return true;
}

/* ------------------------------------------------------------------ */
/* Checksums                                                           */
/* ------------------------------------------------------------------ */

static uint32_t xx_lzopfmt_adler32(uint32_t initial, const uint8_t *data,
                                   size_t size) {
    uint32_t a = initial & UINT32_C(0xffff);
    uint32_t b = initial >> 16U;
    while (size != 0U) {
        size_t count = size > 5552U ? 5552U : size;
        size_t index;
        for (index = 0U; index < count; ++index) {
            a += data[index];
            b += a;
        }
        a %= UINT32_C(65521);
        b %= UINT32_C(65521);
        data += count;
        size -= count;
    }
    return (b << 16U) | a;
}

static void xx_lzopfmt_sums_init(xx_lzopfmt_sums *sums) {
    if (!sums) return;
    sums->adler32 = 1U;
    sums->crc32 = 0U;
}

static void xx_lzopfmt_sums_update(xx_lzopfmt_sums *sums, const void *data,
                                   size_t size) {
    if (!sums || (!data && size != 0U)) return;
    sums->adler32 = xx_lzopfmt_adler32(sums->adler32, (const uint8_t *)data,
                                       size);
    sums->crc32 = xx_crc32_calc(sums->crc32, data, size);
}

static bool xx_lzopfmt_read_sum(xx_lzopfmt_cursor *cursor, void *data,
                                size_t size, xx_lzopfmt_sums *sums) {
    if (!xx_lzopfmt_read(cursor, data, size)) return false;
    xx_lzopfmt_sums_update(sums, data, size);
    return true;
}

static bool xx_lzopfmt_read_u16(xx_lzopfmt_cursor *cursor, uint16_t *value,
                                xx_lzopfmt_sums *sums) {
    uint8_t bytes[2];
    if (!value || !xx_lzopfmt_read_sum(cursor, bytes, sizeof(bytes), sums)) {
        return false;
    }
    *value = (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
    return true;
}

static bool xx_lzopfmt_read_u32(xx_lzopfmt_cursor *cursor, uint32_t *value,
                                xx_lzopfmt_sums *sums) {
    uint8_t bytes[4];
    if (!value || !xx_lzopfmt_read_sum(cursor, bytes, sizeof(bytes), sums)) {
        return false;
    }
    *value = ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
             ((uint32_t)bytes[2] << 8U) | bytes[3];
    return true;
}

static bool xx_lzopfmt_read_u32_plain(xx_lzopfmt_cursor *cursor,
                                      uint32_t *value) {
    return xx_lzopfmt_read_u32(cursor, value, NULL);
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

/* Strict UTF-8: no overlong forms, no surrogates, nothing past U+10FFFF and
 * no C1 controls (U+0080..U+009F). */
static bool xx_lzopfmt_is_utf8(const uint8_t *data, size_t length) {
    size_t index = 0U;
    while (index < length) {
        uint8_t lead = data[index];
        uint32_t cp;
        size_t extra;
        size_t k;
        if (lead < 0x80U) {
            ++index;
            continue;
        }
        if (lead >= 0xc2U && lead <= 0xdfU) {
            extra = 1U;
            cp = lead & 0x1fU;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            extra = 2U;
            cp = lead & 0x0fU;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            extra = 3U;
            cp = lead & 0x07U;
        } else {
            return false;
        }
        if (extra > length - index - 1U) return false;
        for (k = 1U; k <= extra; ++k) {
            uint8_t next = data[index + k];
            if ((next & 0xc0U) != 0x80U) return false;
            cp = (cp << 6U) | (next & 0x3fU);
        }
        if ((extra == 2U && cp < 0x800U) || (extra == 3U && cp < 0x10000U) ||
            cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU) ||
            (cp >= 0x80U && cp <= 0x9fU)) {
            return false;
        }
        index += extra + 1U;
    }
    return true;
}

/* Windows reserved device names.  RtlIsDosDeviceName_U matches the stem (the
 * part before the first '.') after dropping its trailing spaces, whatever the
 * extension, so "NUL .txt" and "COM1.log" are devices too.  COM/LPT take a
 * digit 0-9 or a superscript 1-3 (U+00B9, U+00B2, U+00B3; UTF-8 C2 xx). */
static bool xx_lzopfmt_is_device_name(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",   "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U;
    size_t index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem != 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        if (stem == xx_str_len(devices[index]) &&
            xx_str_nicmp(name, devices[index], stem) == 0) {
            return true;
        }
    }
    if (stem >= 4U && (xx_str_nicmp(name, "COM", 3U) == 0 ||
                       xx_str_nicmp(name, "LPT", 3U) == 0)) {
        uint8_t digit = (uint8_t)name[3];
        if (stem == 4U && digit >= '0' && digit <= '9') return true;
        if (stem == 5U && digit == 0xc2U &&
            ((uint8_t)name[4] == 0xb9U || (uint8_t)name[4] == 0xb2U ||
             (uint8_t)name[4] == 0xb3U)) {
            return true;
        }
    }
    return false;
}

/* Longest record name: 255 stored bytes, at most doubled by the Latin-1 to
 * UTF-8 conversion, plus one "_<number>_<attempt>" suffix. */
#define XX_LZOPFMT_MAX_RECORD_NAME (4U * XX_LZOP_MAX_NAME_LENGTH)

static bool xx_lzopfmt_is_separator(uint8_t ch) {
    return ch == '/' || ch == '\\';
}

/* Characters Win32 cannot put in a file name.  U3 writes '_' for them, and so
 * does this reader. */
static bool xx_lzopfmt_is_reserved_char(uint8_t ch) {
    return ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '|' ||
           ch == '?' || ch == '*';
}

/* C0 controls, DEL and C1 controls (U+0080..U+009F, UTF-8 C2 80..C2 9F). */
static bool xx_lzopfmt_has_control(const uint8_t *text, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t ch = text[index];
        if (ch < 0x20U || ch == 0x7fU) return true;
        if (ch == 0xc2U && index + 1U < length && text[index + 1U] >= 0x80U &&
            text[index + 1U] <= 0x9fU) {
            return true;
        }
    }
    return false;
}

/* One component of a record name: non-empty, no separator, no control or
 * Win32-reserved character, no trailing dot or space (Win32 would strip it,
 * which also rules out "." and ".."), and not a device name. */
static bool xx_lzopfmt_safe_component(const char *name, size_t length) {
    size_t index;
    if (length == 0U || name[length - 1U] == '.' || name[length - 1U] == ' ' ||
        xx_lzopfmt_has_control((const uint8_t *)name, length)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t ch = (uint8_t)name[index];
        if (xx_lzopfmt_is_separator(ch) || xx_lzopfmt_is_reserved_char(ch)) {
            return false;
        }
    }
    return !xx_lzopfmt_is_device_name(name, length);
}

/* A record name is a relative path: safe components joined by single '/'.
 * No leading or trailing separator, no drive, nothing that could leave the
 * destination directory or that Windows would alias to another name. */
static bool xx_lzopfmt_safe_path(const char *name) {
    size_t length;
    size_t start = 0U;
    size_t index;
    if (!name) return false;
    length = xx_str_len(name);
    if (length == 0U || length > XX_LZOPFMT_MAX_RECORD_NAME) return false;
    for (index = 0U; index <= length; ++index) {
        if (index == length || name[index] == '/') {
            if (!xx_lzopfmt_safe_component(name + start, index - start)) {
                return false;
            }
            start = index + 1U;
        }
    }
    return true;
}

/* Turn stored name bytes into a UTF-8 record name.  Valid UTF-8 (which covers
 * ASCII) is kept; anything else is taken as Latin-1, lzop's F_CS_LATIN1 /
 * Windows ANSI default.
 *
 * `lzop -P` stores a path, which is kept as a safe relative path the way U3
 * extracts it ("dir/sub/file.txt" stays, "../x.txt" becomes "x.txt",
 * "/etc/passwd" becomes "etc/passwd"):
 *   - '/' and '\' both separate components; the result uses '/';
 *   - a leading "\\?\" or "\\.\", leading separators and a leading drive
 *     ("C:" as in "C:\x" or "C:x") are dropped;
 *   - trailing dots and spaces of a component are dropped, as Win32 does;
 *     a component left empty, and every "." and "..", is dropped (".." is not
 *     resolved against the previous component);
 *   - a component with a control character, or that is a device name, is
 *     dropped;
 *   - the Win32-reserved characters < > : " | ? * become '_'.
 * Returns NULL when no component is left (the caller then uses "payload"). */
static char *xx_lzopfmt_make_name(const uint8_t *raw, size_t length) {
    uint8_t *text;
    char *name;
    size_t text_length = 0U;
    size_t position = 0U;
    size_t out = 0U;
    size_t index;
    bool utf8;
    if (!raw || length == 0U || length > XX_LZOP_MAX_NAME_LENGTH) return NULL;
    utf8 = xx_lzopfmt_is_utf8(raw, length);
    text = (uint8_t *)xx_mem_alloc(length * 2U + 1U);
    name = (char *)xx_mem_alloc(length * 2U + 1U);
    if (!text || !name) {
        if (text) xx_mem_free(text);
        if (name) xx_mem_free(name);
        return NULL;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t ch = raw[index];
        if (utf8 || ch < 0x80U) {
            text[text_length++] = ch;
        } else {
            /* Latin-1; 0x80..0x9f become C1 controls and are refused below. */
            text[text_length++] = (uint8_t)(0xc0U | (ch >> 6U));
            text[text_length++] = (uint8_t)(0x80U | (ch & 0x3fU));
        }
    }
    if (text_length >= 4U && xx_lzopfmt_is_separator(text[0]) &&
        xx_lzopfmt_is_separator(text[1]) &&
        (text[2] == '?' || text[2] == '.') &&
        xx_lzopfmt_is_separator(text[3])) {
        position = 4U;
    }
    while (position < text_length && xx_lzopfmt_is_separator(text[position])) {
        ++position;
    }
    if (text_length - position >= 2U && text[position + 1U] == ':' &&
        ((text[position] >= 'A' && text[position] <= 'Z') ||
         (text[position] >= 'a' && text[position] <= 'z'))) {
        position += 2U;
    }
    /* Every kept component is copied with at most one separator in front of
     * it, and the text had a separator between any two of them, so the name
     * never outgrows the text. */
    while (position < text_length) {
        size_t start = position;
        size_t end;
        size_t first;
        while (position < text_length &&
               !xx_lzopfmt_is_separator(text[position])) {
            ++position;
        }
        end = position;
        if (position < text_length) ++position;
        while (end > start &&
               (text[end - 1U] == '.' || text[end - 1U] == ' ')) {
            --end;
        }
        if (end == start || xx_lzopfmt_has_control(text + start, end - start)) {
            continue;
        }
        first = out != 0U ? out + 1U : 0U;
        if (out != 0U) name[out] = '/';
        for (index = start; index < end; ++index) {
            name[first + index - start] =
                xx_lzopfmt_is_reserved_char(text[index]) ? '_'
                                                         : (char)text[index];
        }
        if (xx_lzopfmt_is_device_name(name + first, end - start)) continue;
        out = first + (end - start);
    }
    xx_mem_free(text);
    name[out] = '\0';
    if (out == 0U || !xx_lzopfmt_safe_path(name)) {
        xx_mem_free(name);
        return NULL;
    }
    return name;
}

static char *xx_lzopfmt_copy_string(const char *text) {
    size_t length = text ? xx_str_len(text) : 0U;
    char *copy;
    if (!text) return NULL;
    copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return NULL;
    xx_rt_memcpy(copy, text, length + 1U);
    return copy;
}

/* Duplicate names.  Two streams may store the same name (`cat a.lzo a.lzo`),
 * and Windows file systems compare names case-insensitively, so two records
 * must not share a path after case folding or the second extraction would
 * overwrite the first.  NTFS folds by upcasing every UTF-16 unit through its
 * $UpCase table (the Unicode simple upper-case mapping of the BMP, code points
 * above U+FFFF are left alone).  The table below is that mapping; folding a
 * little more than NTFS does only costs an extra rename, never a lost file. */
/* Generated by gen_upper.py from Unicode 15.1.0: 1165 BMP code points, 190
 * ranges {first, last, delta, step}; step 2 maps every other code point. */
static const xx_lzopfmt_case_range xx_lzopfmt_upper_ranges[] = {
    {0x00b5, 0x00b5, 743, 1}, {0x00df, 0x00df, 7615, 1},
    {0x00e0, 0x00f6, -32, 1}, {0x00f8, 0x00fe, -32, 1},
    {0x00ff, 0x00ff, 121, 1}, {0x0101, 0x012f, -1, 2},
    {0x0131, 0x0131, -232, 1}, {0x0133, 0x0137, -1, 2},
    {0x013a, 0x0148, -1, 2}, {0x014b, 0x0177, -1, 2}, {0x017a, 0x017e, -1, 2},
    {0x017f, 0x017f, -300, 1}, {0x0180, 0x0180, 195, 1},
    {0x0183, 0x0185, -1, 2}, {0x0188, 0x0188, -1, 1}, {0x018c, 0x018c, -1, 1},
    {0x0192, 0x0192, -1, 1}, {0x0195, 0x0195, 97, 1}, {0x0199, 0x0199, -1, 1},
    {0x019a, 0x019a, 163, 1}, {0x019e, 0x019e, 130, 1},
    {0x01a1, 0x01a5, -1, 2}, {0x01a8, 0x01a8, -1, 1}, {0x01ad, 0x01ad, -1, 1},
    {0x01b0, 0x01b0, -1, 1}, {0x01b4, 0x01b6, -1, 2}, {0x01b9, 0x01b9, -1, 1},
    {0x01bd, 0x01bd, -1, 1}, {0x01bf, 0x01bf, 56, 1}, {0x01c5, 0x01c5, -1, 1},
    {0x01c6, 0x01c6, -2, 1}, {0x01c8, 0x01c8, -1, 1}, {0x01c9, 0x01c9, -2, 1},
    {0x01cb, 0x01cb, -1, 1}, {0x01cc, 0x01cc, -2, 1}, {0x01ce, 0x01dc, -1, 2},
    {0x01dd, 0x01dd, -79, 1}, {0x01df, 0x01ef, -1, 2}, {0x01f2, 0x01f2, -1, 1},
    {0x01f3, 0x01f3, -2, 1}, {0x01f5, 0x01f5, -1, 1}, {0x01f9, 0x021f, -1, 2},
    {0x0223, 0x0233, -1, 2}, {0x023c, 0x023c, -1, 1},
    {0x023f, 0x0240, 10815, 1}, {0x0242, 0x0242, -1, 1},
    {0x0247, 0x024f, -1, 2}, {0x0250, 0x0250, 10783, 1},
    {0x0251, 0x0251, 10780, 1}, {0x0252, 0x0252, 10782, 1},
    {0x0253, 0x0253, -210, 1}, {0x0254, 0x0254, -206, 1},
    {0x0256, 0x0257, -205, 1}, {0x0259, 0x0259, -202, 1},
    {0x025b, 0x025b, -203, 1}, {0x025c, 0x025c, 42319, 1},
    {0x0260, 0x0260, -205, 1}, {0x0261, 0x0261, 42315, 1},
    {0x0263, 0x0263, -207, 1}, {0x0265, 0x0265, 42280, 1},
    {0x0266, 0x0266, 42308, 1}, {0x0268, 0x0268, -209, 1},
    {0x0269, 0x0269, -211, 1}, {0x026a, 0x026a, 42308, 1},
    {0x026b, 0x026b, 10743, 1}, {0x026c, 0x026c, 42305, 1},
    {0x026f, 0x026f, -211, 1}, {0x0271, 0x0271, 10749, 1},
    {0x0272, 0x0272, -213, 1}, {0x0275, 0x0275, -214, 1},
    {0x027d, 0x027d, 10727, 1}, {0x0280, 0x0280, -218, 1},
    {0x0282, 0x0282, 42307, 1}, {0x0283, 0x0283, -218, 1},
    {0x0287, 0x0287, 42282, 1}, {0x0288, 0x0288, -218, 1},
    {0x0289, 0x0289, -69, 1}, {0x028a, 0x028b, -217, 1},
    {0x028c, 0x028c, -71, 1}, {0x0292, 0x0292, -219, 1},
    {0x029d, 0x029d, 42261, 1}, {0x029e, 0x029e, 42258, 1},
    {0x0345, 0x0345, 84, 1}, {0x0371, 0x0373, -1, 2}, {0x0377, 0x0377, -1, 1},
    {0x037b, 0x037d, 130, 1}, {0x03ac, 0x03ac, -38, 1},
    {0x03ad, 0x03af, -37, 1}, {0x03b1, 0x03c1, -32, 1},
    {0x03c2, 0x03c2, -31, 1}, {0x03c3, 0x03cb, -32, 1},
    {0x03cc, 0x03cc, -64, 1}, {0x03cd, 0x03ce, -63, 1},
    {0x03d0, 0x03d0, -62, 1}, {0x03d1, 0x03d1, -57, 1},
    {0x03d5, 0x03d5, -47, 1}, {0x03d6, 0x03d6, -54, 1},
    {0x03d7, 0x03d7, -8, 1}, {0x03d9, 0x03ef, -1, 2}, {0x03f0, 0x03f0, -86, 1},
    {0x03f1, 0x03f1, -80, 1}, {0x03f2, 0x03f2, 7, 1},
    {0x03f3, 0x03f3, -116, 1}, {0x03f5, 0x03f5, -96, 1},
    {0x03f8, 0x03f8, -1, 1}, {0x03fb, 0x03fb, -1, 1}, {0x0430, 0x044f, -32, 1},
    {0x0450, 0x045f, -80, 1}, {0x0461, 0x0481, -1, 2}, {0x048b, 0x04bf, -1, 2},
    {0x04c2, 0x04ce, -1, 2}, {0x04cf, 0x04cf, -15, 1}, {0x04d1, 0x052f, -1, 2},
    {0x0561, 0x0586, -48, 1}, {0x10d0, 0x10fa, 3008, 1},
    {0x10fd, 0x10ff, 3008, 1}, {0x13f8, 0x13fd, -8, 1},
    {0x1c80, 0x1c80, -6254, 1}, {0x1c81, 0x1c81, -6253, 1},
    {0x1c82, 0x1c82, -6244, 1}, {0x1c83, 0x1c84, -6242, 1},
    {0x1c85, 0x1c85, -6243, 1}, {0x1c86, 0x1c86, -6236, 1},
    {0x1c87, 0x1c87, -6181, 1}, {0x1c88, 0x1c88, 35266, 1},
    {0x1d79, 0x1d79, 35332, 1}, {0x1d7d, 0x1d7d, 3814, 1},
    {0x1d8e, 0x1d8e, 35384, 1}, {0x1e01, 0x1e95, -1, 2},
    {0x1e9b, 0x1e9b, -59, 1}, {0x1ea1, 0x1eff, -1, 2}, {0x1f00, 0x1f07, 8, 1},
    {0x1f10, 0x1f15, 8, 1}, {0x1f20, 0x1f27, 8, 1}, {0x1f30, 0x1f37, 8, 1},
    {0x1f40, 0x1f45, 8, 1}, {0x1f51, 0x1f57, 8, 2}, {0x1f60, 0x1f67, 8, 1},
    {0x1f70, 0x1f71, 74, 1}, {0x1f72, 0x1f75, 86, 1}, {0x1f76, 0x1f77, 100, 1},
    {0x1f78, 0x1f79, 128, 1}, {0x1f7a, 0x1f7b, 112, 1},
    {0x1f7c, 0x1f7d, 126, 1}, {0x1f80, 0x1f87, 8, 1}, {0x1f90, 0x1f97, 8, 1},
    {0x1fa0, 0x1fa7, 8, 1}, {0x1fb0, 0x1fb1, 8, 1}, {0x1fb3, 0x1fb3, 9, 1},
    {0x1fbe, 0x1fbe, -7205, 1}, {0x1fc3, 0x1fc3, 9, 1}, {0x1fd0, 0x1fd1, 8, 1},
    {0x1fe0, 0x1fe1, 8, 1}, {0x1fe5, 0x1fe5, 7, 1}, {0x1ff3, 0x1ff3, 9, 1},
    {0x214e, 0x214e, -28, 1}, {0x2170, 0x217f, -16, 1},
    {0x2184, 0x2184, -1, 1}, {0x24d0, 0x24e9, -26, 1},
    {0x2c30, 0x2c5f, -48, 1}, {0x2c61, 0x2c61, -1, 1},
    {0x2c65, 0x2c65, -10795, 1}, {0x2c66, 0x2c66, -10792, 1},
    {0x2c68, 0x2c6c, -1, 2}, {0x2c73, 0x2c73, -1, 1}, {0x2c76, 0x2c76, -1, 1},
    {0x2c81, 0x2ce3, -1, 2}, {0x2cec, 0x2cee, -1, 2}, {0x2cf3, 0x2cf3, -1, 1},
    {0x2d00, 0x2d25, -7264, 1}, {0x2d27, 0x2d27, -7264, 1},
    {0x2d2d, 0x2d2d, -7264, 1}, {0xa641, 0xa66d, -1, 2},
    {0xa681, 0xa69b, -1, 2}, {0xa723, 0xa72f, -1, 2}, {0xa733, 0xa76f, -1, 2},
    {0xa77a, 0xa77c, -1, 2}, {0xa77f, 0xa787, -1, 2}, {0xa78c, 0xa78c, -1, 1},
    {0xa791, 0xa793, -1, 2}, {0xa794, 0xa794, 48, 1}, {0xa797, 0xa7a9, -1, 2},
    {0xa7b5, 0xa7c3, -1, 2}, {0xa7c8, 0xa7ca, -1, 2}, {0xa7d1, 0xa7d1, -1, 1},
    {0xa7d7, 0xa7d9, -1, 2}, {0xa7f6, 0xa7f6, -1, 1},
    {0xab53, 0xab53, -928, 1}, {0xab70, 0xabbf, -38864, 1},
    {0xff41, 0xff5a, -32, 1},
};

static uint32_t xx_lzopfmt_upper(uint32_t cp) {
    size_t low = 0U;
    size_t high = sizeof(xx_lzopfmt_upper_ranges) /
                  sizeof(xx_lzopfmt_upper_ranges[0]);
    if (cp < 0x80U) return cp >= 'a' && cp <= 'z' ? cp - 32U : cp;
    if (cp > 0xffffU) return cp;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        const xx_lzopfmt_case_range *range = &xx_lzopfmt_upper_ranges[middle];
        if (cp < range->first) {
            high = middle;
        } else if (cp > range->last) {
            low = middle + 1U;
        } else {
            if (range->step == 2U && ((cp - range->first) & 1U) != 0U) {
                return cp;
            }
            return (uint32_t)((int32_t)cp + range->delta);
        }
    }
    return cp;
}

/* Case-folded key of a record name, as UTF-8; '/' folds to itself, so the
 * key has the same components as the name.  Record names are valid UTF-8
 * (checked, or converted from Latin-1); a stray byte is kept as it is.
 * Upcasing can turn a 2-byte sequence into a 3-byte one, never more, so the
 * key needs at most 1.5 times the name length; the buffer is larger, which
 * leaves the spare byte xx_lzopfmt_key_is_dir() writes past the key. */
static uint8_t *xx_lzopfmt_fold_key(const char *name, size_t *key_length) {
    const uint8_t *text = (const uint8_t *)name;
    size_t length = xx_str_len(name);
    size_t index = 0U;
    size_t out = 0U;
    uint8_t *key;
    if (length > SIZE_MAX / 2U - 4U) return NULL;
    key = (uint8_t *)xx_mem_alloc(length * 2U + 4U);
    if (!key) return NULL;
    while (index < length) {
        uint8_t lead = text[index];
        uint32_t cp;
        size_t width;
        size_t k;
        if (lead < 0x80U) {
            cp = lead;
            width = 1U;
        } else if ((lead & 0xe0U) == 0xc0U) {
            cp = lead & 0x1fU;
            width = 2U;
        } else if ((lead & 0xf0U) == 0xe0U) {
            cp = lead & 0x0fU;
            width = 3U;
        } else if ((lead & 0xf8U) == 0xf0U) {
            cp = lead & 0x07U;
            width = 4U;
        } else {
            key[out++] = lead;
            ++index;
            continue;
        }
        if (width > length - index) {
            key[out++] = lead;
            ++index;
            continue;
        }
        for (k = 1U; k < width; ++k) cp = (cp << 6U) | (text[index + k] & 0x3fU);
        index += width;
        cp = xx_lzopfmt_upper(cp);
        if (cp < 0x80U) {
            key[out++] = (uint8_t)cp;
        } else if (cp < 0x800U) {
            key[out++] = (uint8_t)(0xc0U | (cp >> 6U));
            key[out++] = (uint8_t)(0x80U | (cp & 0x3fU));
        } else if (cp < 0x10000U) {
            key[out++] = (uint8_t)(0xe0U | (cp >> 12U));
            key[out++] = (uint8_t)(0x80U | ((cp >> 6U) & 0x3fU));
            key[out++] = (uint8_t)(0x80U | (cp & 0x3fU));
        } else {
            key[out++] = (uint8_t)(0xf0U | ((cp >> 18U) & 0x07U));
            key[out++] = (uint8_t)(0x80U | ((cp >> 12U) & 0x3fU));
            key[out++] = (uint8_t)(0x80U | ((cp >> 6U) & 0x3fU));
            key[out++] = (uint8_t)(0x80U | (cp & 0x3fU));
        }
    }
    *key_length = out;
    return key;
}

static int xx_lzopfmt_key_compare(const xx_lzopfmt_key *key,
                                  const uint8_t *data, size_t length) {
    size_t common = key->length < length ? key->length : length;
    int order = common != 0U ? xx_rt_memcmp(key->data, data, common) : 0;
    if (order != 0) return order;
    if (key->length == length) return 0;
    return key->length < length ? -1 : 1;
}

/* Binary search of the sorted key set; *slot receives the insert position. */
static bool xx_lzopfmt_key_find(const xx_lzopfmt_key *keys, size_t count,
                                const uint8_t *data, size_t length,
                                size_t *slot) {
    size_t low = 0U;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        int order = xx_lzopfmt_key_compare(&keys[middle], data, length);
        if (order == 0) {
            *slot = middle;
            return true;
        }
        if (order < 0) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    *slot = low;
    return false;
}

/* Does any held key start with data[0..length)?  Keys sharing a prefix are
 * contiguous in the sorted set, starting at the insert position. */
static bool xx_lzopfmt_key_prefix_held(const xx_lzopfmt_key *keys,
                                       size_t count, const uint8_t *data,
                                       size_t length) {
    size_t slot = 0U;
    if (xx_lzopfmt_key_find(keys, count, data, length, &slot)) return true;
    return slot < count && keys[slot].length >= length &&
           xx_rt_memcmp(keys[slot].data, data, length) == 0;
}

/* Length of the common prefix of a held key and data[0..length). */
static size_t xx_lzopfmt_key_common(const xx_lzopfmt_key *key,
                                    const uint8_t *data, size_t length) {
    size_t limit = key->length < length ? key->length : length;
    size_t index = 0U;
    while (index < limit && key->data[index] == data[index]) ++index;
    return index;
}

/* Is key[0..length) a directory of an earlier record?  That is, does a held
 * key start with key[0..length) + '/'?  key must have one spare byte at
 * key[length]; it is overwritten with the separator. */
static bool xx_lzopfmt_key_is_dir(const xx_lzopfmt_key *keys, size_t count,
                                  uint8_t *key, size_t length) {
    key[length] = '/';
    return xx_lzopfmt_key_prefix_held(keys, count, key, length + 1U);
}

static void xx_lzopfmt_key_insert(xx_lzopfmt_key *keys, size_t *used,
                                  uint8_t *data, size_t length) {
    size_t slot = 0U;
    (void)xx_lzopfmt_key_find(keys, *used, data, length, &slot);
    if (slot < *used) {
        xx_rt_memmove(&keys[slot + 1U], &keys[slot],
                      (*used - slot) * sizeof(*keys));
    }
    keys[slot].data = data;
    keys[slot].length = length;
    ++*used;
}

static size_t xx_lzopfmt_count_chars(const char *text, size_t length) {
    size_t count = 0U;
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (((uint8_t)text[index] & 0xc0U) != 0x80U) ++count;
    }
    return count;
}

/* NTFS 8.3 alias shape: "<base>~<digits>[.<ext>]" with at most eight
 * characters of base and one to three of extension.  Every short name NTFS
 * generates has this shape ("LONGFI~1.TXT", "LO3A2B~1"); a name that does not
 * have it can never be another file's short name. */
static bool xx_lzopfmt_is_short_alias(const char *name, size_t length) {
    size_t dot = length;
    size_t digits = 0U;
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (name[index] != '.') continue;
        if (dot != length) return false;
        dot = index;
    }
    if (dot != length) {
        size_t extension = xx_lzopfmt_count_chars(name + dot + 1U,
                                                  length - dot - 1U);
        if (extension == 0U || extension > 3U) return false;
    }
    if (xx_lzopfmt_count_chars(name, dot) > 8U) return false;
    while (digits < dot && name[dot - 1U - digits] >= '0' &&
           name[dot - 1U - digits] <= '9') {
        ++digits;
    }
    return digits != 0U && digits < dot && name[dot - 1U - digits] == '~';
}

/* Give every stream a record path that cannot land on an earlier record's
 * file.  Records are extracted in order into one directory tree, so three
 * things are refused, all compared on NTFS-folded keys:
 *   - the same path as an earlier record (also case- and accent-case-folded);
 *   - a path whose directory is an earlier record's FILE, or whose file is an
 *     earlier record's DIRECTORY (neither could be created);
 *   - an NTFS 8.3 alias ("LONGFI~1.TXT") as a NEW entry in a directory that
 *     already holds entries: it could be the short name NTFS generated for
 *     one of them, and opening it would overwrite that file.  An alias that
 *     is created first, or that names a directory an earlier record already
 *     created under that exact spelling, is safe and kept.
 * The first offending component (directory or file) is renamed:
 * "<component>_<stream number>", then "<component>_<number>_<attempt>",
 * always built from the original component, until the renamed prefix is not
 * held by any key at all (so nothing below it can clash either).  Existing
 * directories are shared ("dir/a" and "dir/b" both stay).
 *
 * Termination and cost: the renamed prefixes of one stream differ in that
 * component, so a held key blocks at most one attempt of it, and only
 * attempt 1 can still have the 8.3 shape (a suffix lengthens the extension
 * past three characters or ends the base in "_<digits>").  With i keys held,
 * attempts 1..i+2 therefore contain a free one.  Over the whole run a held
 * key blocks at most two attempts per component, because "_<number>" and
 * "_<number>_<attempt>" parse back from the end of a component and the stream
 * number is unique.  Attempt 0 is two binary searches whatever the depth
 * of the path, a later attempt two more, each of O(log n) key comparisons.
 * The container is never refused here (only an allocation failure fails), so
 * handle_base_info accepts exactly what check_is_valid accepts. */
static bool xx_lzopfmt_dedupe_names(xx_lzopfmt_scan *scan) {
    xx_lzopfmt_key *keys;
    size_t used = 0U;
    size_t index;
    bool result = false;
    if (!scan || scan->count == 0U) return scan != NULL;
    keys = (xx_lzopfmt_key *)xx_mem_alloc(scan->count * sizeof(*keys));
    if (!keys) return false;
    for (index = 0U; index < scan->count; ++index) {
        xx_lzopfmt_stream *stream = &scan->streams[index];
        const char *name = stream->name;
        size_t name_length = xx_str_len(name);
        size_t name_start = 0U;
        size_t name_end = 0U;
        size_t key_start = 0U;
        size_t key_end = 0U;
        size_t key_length = 0U;
        uint8_t *key = xx_lzopfmt_fold_key(name, &key_length);
        bool clash = false;
        bool found;
        size_t shared = 0U;
        size_t attempt;
        size_t slot = 0U;
        if (!key) goto done;
        /* Attempt 0, the name itself.  Skip the leading components that are
         * directories of earlier records: component j (ending at key_end, a
         * '/' in the key) is one exactly when some held key starts with
         * key[0..key_end] including that '/', i.e. shares more than key_end
         * bytes with the key.  The held key sharing the longest prefix is a
         * neighbour of the key's insert position, so one search finds the
         * whole directory depth.  A held key with the same spelling is a
         * clash on the file component itself. */
        found = xx_lzopfmt_key_find(keys, used, key, key_length, &slot);
        if (found) {
            shared = key_length;
        } else {
            if (slot > 0U) {
                shared = xx_lzopfmt_key_common(&keys[slot - 1U], key,
                                               key_length);
            }
            if (slot < used) {
                size_t right = xx_lzopfmt_key_common(&keys[slot], key,
                                                     key_length);
                if (right > shared) shared = right;
            }
        }
        for (;;) {
            while (name_end < name_length && name[name_end] != '/') {
                ++name_end;
            }
            while (key_end < key_length && key[key_end] != '/') ++key_end;
            if (key_end == key_length || shared <= key_end) break;
            name_start = ++name_end;
            key_start = ++key_end;
        }
        if (found) {
            clash = true; /* the same path as an earlier record */
        } else if (key_end == key_length
                       ? xx_lzopfmt_key_is_dir(keys, used, key, key_end)
                       : xx_lzopfmt_key_find(keys, used, key, key_end,
                                             &slot)) {
            /* The file component is an earlier DIRECTORY, or a directory
             * component is an earlier FILE. */
            clash = true;
        } else {
            /* A new entry, so everything below it is new too. */
            clash = (key_start != 0U || used != 0U) &&
                    xx_lzopfmt_is_short_alias(name + name_start,
                                              name_end - name_start);
        }
        if (!clash) {
            xx_lzopfmt_key_insert(keys, &used, key, key_length);
            continue;
        }
        /* Rename component [name_start, name_end) / [key_start, key_end). */
        for (attempt = 1U;; ++attempt) {
            char suffix[48];
            size_t extra;
            size_t prefix;
            uint8_t *candidate_key;
            char *candidate;
            if (attempt > used + 2U) {
                xx_mem_free(key); /* unreachable, see above */
                goto done;
            }
            if (attempt == 1U) {
                (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%lu",
                                     (unsigned long)(index + 1U));
            } else {
                (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%lu_%lu",
                                     (unsigned long)(index + 1U),
                                     (unsigned long)attempt);
            }
            extra = xx_str_len(suffix);
            /* The suffix is ASCII, so it folds to itself. */
            candidate_key = (uint8_t *)xx_mem_alloc(key_length + extra + 1U);
            candidate = (char *)xx_mem_alloc(name_length + extra + 1U);
            if (!candidate_key || !candidate) {
                if (candidate_key) xx_mem_free(candidate_key);
                if (candidate) xx_mem_free(candidate);
                xx_mem_free(key);
                goto done;
            }
            /* One spare byte at the end for xx_lzopfmt_key_is_dir(); a
             * directory component keeps its '/' at candidate_key[prefix]. */
            prefix = key_end + extra;
            xx_rt_memcpy(candidate_key, key, key_end);
            xx_rt_memcpy(candidate_key + key_end, suffix, extra);
            xx_rt_memcpy(candidate_key + prefix, key + key_end,
                         key_length - key_end);
            xx_rt_memcpy(candidate, name, name_end);
            xx_rt_memcpy(candidate + name_end, suffix, extra);
            xx_rt_memcpy(candidate + name_end + extra, name + name_end,
                         name_length - name_end + 1U);
            if (xx_lzopfmt_key_find(keys, used, candidate_key, prefix, &slot) ||
                xx_lzopfmt_key_is_dir(keys, used, candidate_key, prefix) ||
                xx_lzopfmt_is_short_alias(candidate + name_start,
                                          name_end + extra - name_start)) {
                xx_mem_free(candidate_key);
                xx_mem_free(candidate);
                continue;
            }
            xx_lzopfmt_key_insert(keys, &used, candidate_key,
                                  key_length + extra);
            xx_mem_free(stream->name);
            stream->name = candidate;
            xx_mem_free(key);
            break;
        }
    }
    result = true;
done:
    for (index = 0U; index < used; ++index) xx_mem_free(keys[index].data);
    xx_mem_free(keys);
    return result;
}

/* ------------------------------------------------------------------ */
/* Scan                                                                */
/* ------------------------------------------------------------------ */

static void xx_lzopfmt_scan_cleanup(xx_lzopfmt_scan *scan) {
    size_t index;
    if (!scan) return;
    if (scan->streams) {
        for (index = 0U; index < scan->count; ++index) {
            if (scan->streams[index].name) {
                xx_mem_free(scan->streams[index].name);
            }
        }
        xx_mem_free(scan->streams);
    }
    xx_mem_zero(scan, sizeof(*scan));
    scan->stream_size = -1;
}

/* The optional extra field is length-prefixed and checksummed exactly like
 * the header (the checksum also covers the length); it is skipped but still
 * validated so a corrupt one is refused here instead of inside the codec. */
static bool xx_lzopfmt_scan_extra(xx_lzopfmt_cursor *cursor, bool use_crc) {
    uint32_t length;
    uint32_t expected;
    xx_lzopfmt_sums sums;
    uint8_t buffer[4096];
    uint8_t length_bytes[4];
    if (!xx_lzopfmt_read_u32_plain(cursor, &length) ||
        length > XX_LZOPFMT_MAX_EXTRA_SIZE ||
        (uint64_t)length > (uint64_t)(cursor->end - cursor->position)) {
        return false;
    }
    xx_lzopfmt_sums_init(&sums);
    length_bytes[0] = (uint8_t)(length >> 24U);
    length_bytes[1] = (uint8_t)(length >> 16U);
    length_bytes[2] = (uint8_t)(length >> 8U);
    length_bytes[3] = (uint8_t)length;
    xx_lzopfmt_sums_update(&sums, length_bytes, sizeof(length_bytes));
    while (length != 0U) {
        size_t count = length > sizeof(buffer) ? sizeof(buffer) : length;
        if (!xx_lzopfmt_read_sum(cursor, buffer, count, &sums)) return false;
        length -= (uint32_t)count;
    }
    return xx_lzopfmt_read_u32_plain(cursor, &expected) &&
           expected == (use_crc ? sums.crc32 : sums.adler32);
}

/* Parse one stream header, the magic having been consumed already.  The field
 * rules are the codec's (src/algo/lzop), so a header accepted here is one the
 * codec will accept. */
static bool xx_lzopfmt_scan_header(xx_lzopfmt_cursor *cursor,
                                   xx_lzopfmt_stream *stream, uint8_t *name,
                                   uint8_t *name_length) {
    xx_lzopfmt_sums sums;
    uint16_t needed_version = 0U;
    uint32_t mtime_low = 0U;
    uint32_t mtime_high = 0U;
    uint32_t expected;

    xx_lzopfmt_sums_init(&sums);
    if (!xx_lzopfmt_read_u16(cursor, &stream->version, &sums) ||
        !xx_lzopfmt_read_u16(cursor, &stream->library_version, &sums) ||
        stream->version < XX_LZOPFMT_MIN_VERSION ||
        stream->version > XX_LZOPFMT_MAX_VERSION) {
        return false;
    }
    if (stream->version >= XX_LZOPFMT_VERSION_LONG_HEADER &&
        (!xx_lzopfmt_read_u16(cursor, &needed_version, &sums) ||
         needed_version < XX_LZOPFMT_MIN_VERSION ||
         needed_version > XX_LZOPFMT_MAX_VERSION)) {
        return false;
    }
    if (!xx_lzopfmt_read_sum(cursor, &stream->method, 1U, &sums) ||
        (stream->method != 1U && stream->method != 2U &&
         stream->method != 3U)) {
        return false;
    }
    if (stream->version >= XX_LZOPFMT_VERSION_LONG_HEADER &&
        !xx_lzopfmt_read_sum(cursor, &stream->level, 1U, &sums)) {
        return false;
    }
    /* The filter and multipart flags describe payloads the codec refuses to
     * decode, so they are refused here too rather than listed and then failed
     * at extraction time. */
    if (!xx_lzopfmt_read_u32(cursor, &stream->flags, &sums) ||
        (stream->flags & ~XX_LZOPFMT_ALLOWED_FLAGS) != 0U ||
        (stream->flags &
         (XX_LZOPFMT_FLAG_FILTER | XX_LZOPFMT_FLAG_MULTIPART)) != 0U ||
        !xx_lzopfmt_read_u32(cursor, &stream->mode, &sums) ||
        !xx_lzopfmt_read_u32(cursor, &mtime_low, &sums) ||
        (stream->version >= XX_LZOPFMT_VERSION_LONG_HEADER &&
         !xx_lzopfmt_read_u32(cursor, &mtime_high, &sums)) ||
        !xx_lzopfmt_read_sum(cursor, name_length, 1U, &sums)) {
        return false;
    }
    if (*name_length != 0U &&
        !xx_lzopfmt_read_sum(cursor, name, *name_length, &sums)) {
        return false;
    }
    if (!xx_lzopfmt_read_u32_plain(cursor, &expected) ||
        expected != ((stream->flags & XX_LZOPFMT_FLAG_HEADER_CRC) != 0U
                         ? sums.crc32
                         : sums.adler32)) {
        return false;
    }
    if ((stream->flags & XX_LZOPFMT_FLAG_HEADER_EXTRA) != 0U &&
        !xx_lzopfmt_scan_extra(
            cursor, (stream->flags & XX_LZOPFMT_FLAG_HEADER_CRC) != 0U)) {
        return false;
    }
    stream->mtime = ((uint64_t)mtime_high << 32U) | mtime_low;
    return true;
}

/* Walk the block chain of one stream, skipping payloads.  Every declared
 * length is bounded here, at parse time. */
static bool xx_lzopfmt_scan_blocks(xx_lzopfmt_cursor *cursor,
                                   xx_lzopfmt_stream *stream,
                                   const xx_lzopfmt_scan *scan,
                                   xx_pd_struct *pd) {
    uint32_t flags = stream->flags;
    for (;;) {
        uint32_t expanded;
        uint32_t packed;
        uint32_t discard;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_lzopfmt_read_u32_plain(cursor, &expanded)) return false;
        if (expanded == 0U) return true;  /* end-of-stream marker */
        /* Expansion-bomb refusal: an absolute per-block ceiling and an
         * absolute total ceiling, both checked before anything is sized or
         * read from the declared value. */
        if (expanded > XX_LZOPFMT_MAX_BLOCK_SIZE ||
            scan->uncompressed + stream->uncompressed >
                XX_LZOPFMT_MAX_TOTAL_OUTPUT - expanded ||
            scan->blocks + stream->blocks >= XX_LZOPFMT_MAX_BLOCKS) {
            return false;
        }
        if (!xx_lzopfmt_read_u32_plain(cursor, &packed) || packed == 0U ||
            packed > expanded) {
            return false;
        }
        if (((flags & XX_LZOPFMT_FLAG_ADLER_DATA) != 0U &&
             !xx_lzopfmt_read_u32_plain(cursor, &discard)) ||
            ((flags & XX_LZOPFMT_FLAG_CRC_DATA) != 0U &&
             !xx_lzopfmt_read_u32_plain(cursor, &discard)) ||
            (packed < expanded &&
             (flags & XX_LZOPFMT_FLAG_ADLER_COMPRESSED) != 0U &&
             !xx_lzopfmt_read_u32_plain(cursor, &discard)) ||
            (packed < expanded &&
             (flags & XX_LZOPFMT_FLAG_CRC_COMPRESSED) != 0U &&
             !xx_lzopfmt_read_u32_plain(cursor, &discard))) {
            return false;
        }
        /* packed == expanded is a STORED block; the skip and the accounting
         * are identical, only the codec's treatment differs. */
        if (!xx_lzopfmt_skip(cursor, packed)) return false;
        stream->uncompressed += expanded;
        ++stream->blocks;
    }
}

static bool xx_lzopfmt_scan_append(xx_lzopfmt_scan *scan,
                                   xx_lzopfmt_stream *stream) {
    if (scan->count == scan->capacity) {
        size_t capacity = scan->capacity ? scan->capacity * 2U : 4U;
        xx_lzopfmt_stream *grown;
        if (capacity > XX_LZOPFMT_MAX_STREAMS) capacity = XX_LZOPFMT_MAX_STREAMS;
        if (capacity <= scan->count) return false;
        grown = (xx_lzopfmt_stream *)xx_mem_realloc(
            scan->streams, capacity * sizeof(*grown));
        if (!grown) return false;
        scan->streams = grown;
        scan->capacity = capacity;
    }
    scan->streams[scan->count] = *stream;
    stream->name = NULL;  /* ownership moved */
    return true;
}

/* One pass over the whole container: every concatenated stream, every block.
 * The first stream must be complete; a later stream that does not parse ends
 * the container and the rest becomes overlay.  With collect set, a stream
 * descriptor (with its record name) is kept for every stream. */
static bool xx_lzopfmt_scan_run(Abstractformat *self, xx_lzopfmt_scan *scan,
                                bool collect, xx_pd_struct *pd) {
    xx_lzopfmt_cursor cursor;
    int64_t total_size;
    xx_mem_zero(&cursor, sizeof(cursor));
    if (scan) {
        xx_mem_zero(scan, sizeof(*scan));
        scan->stream_size = -1;
    }
    if (!self || !self->device || !scan || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < (int64_t)XX_LZOP_MAGIC_SIZE) {
        goto fail;
    }
    if (!xx_lzopfmt_cursor_open(&cursor, self->device, self->base_address,
                                total_size)) {
        goto fail;
    }

    while (cursor.position < cursor.end) {
        xx_lzopfmt_stream stream;
        uint8_t magic[XX_LZOP_MAGIC_SIZE];
        uint8_t name[XX_LZOP_MAX_NAME_LENGTH];
        uint8_t name_length = 0U;
        bool first = scan->count == 0U;
        int64_t before = cursor.position;
        if (scan->count >= XX_LZOPFMT_MAX_STREAMS) goto fail;
        xx_mem_zero(&stream, sizeof(stream));
        if (!xx_lzopfmt_read(&cursor, magic, sizeof(magic)) ||
            !xx_lzop_has_header(magic, sizeof(magic)) ||
            !xx_lzopfmt_scan_header(&cursor, &stream, name, &name_length)) {
            if (first) goto fail;
            /* Trailing bytes that are not a further stream become overlay. */
            cursor.position = before;
            break;
        }
        stream.offset = before;
        stream.header_size = cursor.position - before;
        if (!xx_lzopfmt_scan_blocks(&cursor, &stream, scan, pd)) {
            if (first || (pd && xx_pd_is_stopped(pd))) goto fail;
            cursor.position = before;
            break;
        }
        stream.size = cursor.position - before;
        if (collect) {
            stream.name = xx_lzopfmt_make_name(name, name_length);
            stream.has_stored_name = stream.name != NULL;
            if (!stream.name) {
                stream.name = xx_lzopfmt_copy_string(XX_LZOPFMT_PAYLOAD_NAME);
            }
            if (!stream.name || !xx_lzopfmt_scan_append(scan, &stream)) {
                if (stream.name) xx_mem_free(stream.name);
                goto fail;
            }
        }
        scan->uncompressed += stream.uncompressed;
        scan->blocks += stream.blocks;
        ++scan->count;
    }
    if (scan->count == 0U || cursor.position <= self->base_address ||
        (collect && !xx_lzopfmt_dedupe_names(scan))) {
        goto fail;
    }
    scan->stream_size = cursor.position - self->base_address;
    xx_lzopfmt_cursor_close(&cursor);
    return true;
fail:
    xx_lzopfmt_cursor_close(&cursor);
    xx_lzopfmt_scan_cleanup(scan);
    return false;
}

/* ------------------------------------------------------------------ */
/* Decoding                                                            */
/* ------------------------------------------------------------------ */

static bool xx_lzopfmt_write_all(xx_io_device *target, const uint8_t *data,
                                 size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(target, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_lzopfmt_flush(xx_lzopfmt_decoder *decoder) {
    bool result = true;
    if (decoder->target && decoder->out_length != 0U) {
        result = xx_lzopfmt_write_all(decoder->target, decoder->out,
                                      decoder->out_length);
    }
    decoder->out_length = 0U;
    return result;
}

/* Append verified bytes to the output.  Small blocks are batched; a block
 * at least as large as the batch is written through. */
static bool xx_lzopfmt_emit(xx_lzopfmt_decoder *decoder, const uint8_t *data,
                            size_t size) {
    if (!decoder->target || size == 0U) return true;
    if (size > XX_LZOPFMT_OUTPUT_BUFFER - decoder->out_length) {
        if (!xx_lzopfmt_flush(decoder)) return false;
        if (size >= XX_LZOPFMT_OUTPUT_BUFFER) {
            return xx_lzopfmt_write_all(decoder->target, data, size);
        }
    }
    xx_rt_memcpy(decoder->out + decoder->out_length, data, size);
    decoder->out_length += size;
    return true;
}

/* Grow a reused block buffer to hold need bytes.  need is at most
 * XX_LZOPFMT_MAX_BLOCK_SIZE (checked by the caller), and the capacity is a
 * power of two from 64 KiB up to that same ceiling, so a run of growing blocks
 * reallocates at most eleven times. */
static bool xx_lzopfmt_reserve(uint8_t **buffer, size_t *capacity,
                               size_t need) {
    size_t size = 65536U;
    if (need <= *capacity) return true;
    if (need > XX_LZOPFMT_MAX_BLOCK_SIZE) return false;
    while (size < need) size <<= 1U;
    if (*buffer) xx_mem_free(*buffer);
    *capacity = 0U;
    *buffer = (uint8_t *)xx_mem_alloc(size);
    if (!*buffer) return false;
    *capacity = size;
    return true;
}

static bool xx_lzopfmt_decoder_init(xx_lzopfmt_decoder *decoder,
                                    xx_io_device *source,
                                    xx_io_device *target) {
    xx_mem_zero(decoder, sizeof(*decoder));
    if (!xx_lzopfmt_cursor_open(&decoder->cursor, source, 0, 0)) return false;
    decoder->target = target;
    if (target) {
        decoder->out = (uint8_t *)xx_mem_alloc(XX_LZOPFMT_OUTPUT_BUFFER);
        if (!decoder->out) return false;
    }
    return true;
}

static void xx_lzopfmt_decoder_cleanup(xx_lzopfmt_decoder *decoder) {
    xx_lzopfmt_cursor_close(&decoder->cursor);
    if (decoder->out) xx_mem_free(decoder->out);
    if (decoder->packed) xx_mem_free(decoder->packed);
    if (decoder->expanded) xx_mem_free(decoder->expanded);
    xx_mem_zero(decoder, sizeof(*decoder));
}

/* Decode one stream over the exact extent the scan measured.  The header goes
 * through the scan's own parser, and every block is held to the codec's rules
 * (src/algo/lzop): 1 <= packed <= expanded <= 64 MiB, the compressed checksum
 * only on really compressed blocks, the data checksum on every block, LZO1X
 * output exactly expanded bytes.  On top of that the stream must end exactly
 * at the scanned extent and expand to exactly the scanned size. */
static bool xx_lzopfmt_decode_one(xx_lzopfmt_decoder *decoder,
                                  const xx_lzopfmt_stream *stream,
                                  xx_pd_struct *pd) {
    xx_lzopfmt_cursor *cursor = &decoder->cursor;
    xx_lzopfmt_stream header;
    uint8_t magic[XX_LZOP_MAGIC_SIZE];
    uint8_t name[XX_LZOP_MAX_NAME_LENGTH];
    uint8_t name_length = 0U;
    uint64_t produced = 0U;
    uint32_t flags;
    if (!stream || stream->offset < 0 || stream->size <= 0 ||
        stream->size > INT64_MAX - stream->offset ||
        !xx_lzopfmt_cursor_bound(cursor, stream->offset,
                                 stream->offset + stream->size)) {
        return false;
    }
    xx_mem_zero(&header, sizeof(header));
    if (!xx_lzopfmt_read(cursor, magic, sizeof(magic)) ||
        !xx_lzop_has_header(magic, sizeof(magic)) ||
        !xx_lzopfmt_scan_header(cursor, &header, name, &name_length) ||
        header.flags != stream->flags ||
        cursor->position - stream->offset != stream->header_size) {
        return false;
    }
    flags = header.flags;
    for (;;) {
        uint32_t expanded;
        uint32_t packed;
        uint32_t adler_data = 0U;
        uint32_t crc_data = 0U;
        uint32_t adler_packed = 0U;
        uint32_t crc_packed = 0U;
        const uint8_t *data;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_lzopfmt_read_u32_plain(cursor, &expanded)) return false;
        if (expanded == 0U) break;
        if (expanded > XX_LZOPFMT_MAX_BLOCK_SIZE ||
            (uint64_t)expanded > stream->uncompressed - produced ||
            !xx_lzopfmt_read_u32_plain(cursor, &packed) || packed == 0U ||
            packed > expanded) {
            return false;
        }
        if (((flags & XX_LZOPFMT_FLAG_ADLER_DATA) != 0U &&
             !xx_lzopfmt_read_u32_plain(cursor, &adler_data)) ||
            ((flags & XX_LZOPFMT_FLAG_CRC_DATA) != 0U &&
             !xx_lzopfmt_read_u32_plain(cursor, &crc_data)) ||
            (packed < expanded &&
             (flags & XX_LZOPFMT_FLAG_ADLER_COMPRESSED) != 0U &&
             !xx_lzopfmt_read_u32_plain(cursor, &adler_packed)) ||
            (packed < expanded &&
             (flags & XX_LZOPFMT_FLAG_CRC_COMPRESSED) != 0U &&
             !xx_lzopfmt_read_u32_plain(cursor, &crc_packed))) {
            return false;
        }
        /* The payload must exist before a buffer is sized from it. */
        if ((uint64_t)packed > (uint64_t)(cursor->end - cursor->position) ||
            !xx_lzopfmt_reserve(&decoder->packed, &decoder->packed_capacity,
                                packed) ||
            !xx_lzopfmt_read(cursor, decoder->packed, packed)) {
            return false;
        }
        if (packed < expanded) {
            size_t written = 0U;
            if (((flags & XX_LZOPFMT_FLAG_ADLER_COMPRESSED) != 0U &&
                 xx_lzopfmt_adler32(1U, decoder->packed, packed) !=
                     adler_packed) ||
                ((flags & XX_LZOPFMT_FLAG_CRC_COMPRESSED) != 0U &&
                 xx_crc32_calc(0U, decoder->packed, packed) != crc_packed) ||
                !xx_lzopfmt_reserve(&decoder->expanded,
                                    &decoder->expanded_capacity, expanded) ||
                !xx_lzo1x_decompress(decoder->packed, packed,
                                     decoder->expanded, expanded, &written) ||
                written != (size_t)expanded) {
                return false;
            }
            data = decoder->expanded;
        } else {
            data = decoder->packed; /* stored block */
        }
        if (((flags & XX_LZOPFMT_FLAG_ADLER_DATA) != 0U &&
             xx_lzopfmt_adler32(1U, data, expanded) != adler_data) ||
            ((flags & XX_LZOPFMT_FLAG_CRC_DATA) != 0U &&
             xx_crc32_calc(0U, data, expanded) != crc_data) ||
            !xx_lzopfmt_emit(decoder, data, expanded)) {
            return false;
        }
        produced += expanded;
    }
    if (cursor->position != cursor->end || produced != stream->uncompressed) {
        return false;
    }
    decoder->produced += produced;
    return true;
}

static const xx_lzopfmt_stream *xx_lzopfmt_stream_at(const xx_lzop *archive,
                                                     uint64_t index) {
    const xx_lzopfmt_scan *scan =
        archive ? (const xx_lzopfmt_scan *)archive->internal : NULL;
    if (!scan || !scan->streams || index >= (uint64_t)scan->count) return NULL;
    return &scan->streams[(size_t)index];
}

/* Decode streams first..first+count-1 into destination (NULL: verify only). */
static bool xx_lzopfmt_decode_range(xx_lzop *archive, uint64_t first,
                                    uint64_t count, xx_io_device *destination,
                                    uint64_t *produced, xx_pd_struct *pd) {
    xx_lzopfmt_decoder decoder;
    uint64_t index;
    bool result;
    if (!archive || !archive->format.device || count == 0U) return false;
    result = xx_lzopfmt_decoder_init(&decoder, archive->format.device,
                                     destination);
    for (index = 0U; result && index < count; ++index) {
        result = xx_lzopfmt_decode_one(
            &decoder, xx_lzopfmt_stream_at(archive, first + index), pd);
    }
    if (result) result = xx_lzopfmt_flush(&decoder);
    if (produced) *produced = decoder.produced;
    xx_lzopfmt_decoder_cleanup(&decoder);
    return result;
}

static bool xx_lzopfmt_decode_stream(xx_lzop *archive, uint64_t index,
                                     xx_io_device *destination,
                                     xx_pd_struct *pd) {
    return xx_lzopfmt_decode_range(archive, index, 1U, destination, NULL, pd);
}

/* ------------------------------------------------------------------ */
/* Options and records                                                 */
/* ------------------------------------------------------------------ */

static bool xx_lzopfmt_copy_options(xx_list_s *destination,
                                    const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_lzopfmt_find_option(const xx_list_s *options,
                                            uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_lzopfmt_populate_record(Abstractformat *self, uint64_t index,
                                       xx_archive_record *record) {
    const xx_lzopfmt_stream *stream;
    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    stream = xx_lzopfmt_stream_at((const xx_lzop *)self, index);
    if (!stream || stream->header_size <= 0 ||
        stream->size < stream->header_size) {
        return false;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = stream->offset;
    record->header_size = stream->header_size;
    record->data_offset = stream->offset + stream->header_size;
    record->compressed_size = stream->size - stream->header_size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          stream->uncompressed) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          stream->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_LEVEL,
                                          stream->level) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          stream->mtime) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void xx_lzopfmt_reset_public(xx_lzop *archive) {
    archive->number_of_streams = 0U;
    archive->number_of_blocks = 0U;
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
    archive->version = 0U;
    archive->library_version = 0U;
    archive->flags = 0U;
    archive->method = 0U;
    archive->level = 0U;
}

static void xx_lzopfmt_release_internal(xx_lzop *archive) {
    if (archive && archive->internal) {
        xx_lzopfmt_scan_cleanup((xx_lzopfmt_scan *)archive->internal);
        xx_mem_free(archive->internal);
        archive->internal = NULL;
    }
}

void xx_lzop_init(xx_lzop *archive, xx_io_device *dev, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, dev, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_LZOP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzop");
    xx_format_set_extension(&archive->format, "lzo");
    archive->format.check_is_valid = xx_lzop_check_is_valid;
    archive->format.handle_base_info = xx_lzop_handle_base_info;
    archive->format.get_format_size = xx_lzop_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lzop_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lzop_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lzop_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lzop_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lzop_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lzop_free_archive_records_reading;
    archive->format.destroy = xx_lzopfmt_vtable_destroy;
    archive->stream_end = -1;
}

xx_lzop *xx_lzop_create(xx_io_device *dev, int64_t base_address) {
    xx_lzop *archive = (xx_lzop *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lzop_init(archive, dev, base_address);
    return archive;
}

void xx_lzop_destroy(xx_lzop *archive) {
    if (!archive) return;
    xx_lzopfmt_release_internal(archive);
    xx_format_cleanup_extra_parameters(&archive->format);
    xx_lzopfmt_reset_public(archive);
}

static void xx_lzopfmt_vtable_destroy(Abstractformat *self) {
    xx_lzop_destroy((xx_lzop *)self);
}

void xx_lzop_free(xx_lzop *archive) {
    if (!archive) return;
    xx_lzop_destroy(archive);
    xx_mem_free(archive);
}

/* ------------------------------------------------------------------ */
/* Abstractformat surface                                              */
/* ------------------------------------------------------------------ */

bool xx_lzop_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzopfmt_scan scan;
    bool result = xx_lzopfmt_scan_run(self, &scan, false, pd);
    xx_lzopfmt_scan_cleanup(&scan);
    return result;
}

bool xx_lzop_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzopfmt_scan *scan;
    xx_lzop *archive = (xx_lzop *)self;
    const xx_lzopfmt_stream *first;
    int64_t total_size;
    if (!self) return false;
    scan = (xx_lzopfmt_scan *)xx_mem_alloc(sizeof(*scan));
    if (!scan || !xx_lzopfmt_scan_run(self, scan, true, pd)) {
        if (scan) xx_mem_free(scan);
        xx_lzopfmt_release_internal(archive);
        xx_lzopfmt_reset_public(archive);
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    xx_lzopfmt_release_internal(archive);
    archive->internal = scan;
    first = &scan->streams[0];
    archive->number_of_streams = (uint64_t)scan->count;
    archive->number_of_blocks = scan->blocks;
    archive->uncompressed_size = scan->uncompressed;
    archive->version = first->version;
    archive->library_version = first->library_version;
    archive->flags = first->flags;
    archive->method = first->method;
    archive->level = first->level;
    archive->stream_end = self->base_address + scan->stream_size;
    total_size = xx_io_total_size(self->device);
    self->format_size = scan->stream_size;
    self->overlay_offset = archive->stream_end < total_size
                               ? archive->stream_end
                               : -1;
    self->overlay_size = archive->stream_end < total_size
                             ? total_size - archive->stream_end
                             : 0;
    self->number_of_archive_records = (uint64_t)scan->count;
    self->file_type = XX_LZOP_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_lzop_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_lzop_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_lzop *)self)->number_of_streams;
}

bool xx_lzop_unpack_to_device(xx_lzop *archive, xx_io_device *destination,
                              xx_pd_struct *pd) {
    uint64_t produced = 0U;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_lzopfmt_decode_range(archive, 0U, archive->number_of_streams,
                                 destination, &produced, pd)) {
        return false;
    }
    /* Every stream already had to match its scanned extent and size. */
    return produced == archive->uncompressed_size;
}

bool xx_lzop_unpack_stream_to_device(xx_lzop *archive, uint64_t index,
                                     xx_io_device *destination,
                                     xx_pd_struct *pd) {
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid) {
        return false;
    }
    return xx_lzopfmt_decode_stream(archive, index, destination, pd);
}

xx_archive_record_state *xx_lzop_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid || ((xx_lzop *)self)->number_of_streams == 0U) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_lzopfmt_copy_options(&state->options, options) ||
        !xx_lzopfmt_populate_record(self, 0U, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = (int64_t)((xx_lzop *)self)->number_of_streams;
    return state;
}

const xx_archive_record *xx_lzop_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lzop_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    int64_t next;
    if (!self || !state || state->format != self || !state->has_record ||
        state->current_index < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    next = state->current_index + 1;
    if ((uint64_t)next >= ((xx_lzop *)self)->number_of_streams ||
        !xx_lzopfmt_populate_record(self, (uint64_t)next,
                                    &state->current_record)) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    return true;
}

bool xx_lzop_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    const char *name;
    char *owned_path = NULL;
    char *destination_path;
    size_t base_length;
    bool result;
    xx_lzop *archive = (xx_lzop *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        state->current_index < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    name = xx_archive_record_get_original_name(&state->current_record);
    /* Defence in depth: the scan already made every name a safe relative
     * path; its directories are created below. */
    if (!xx_lzopfmt_safe_path(name)) name = XX_LZOPFMT_PAYLOAD_NAME;
    path_value = xx_lzopfmt_find_option(&state->options,
                                        XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        /* No destination: verify the stream decodes end to end. */
        return xx_lzopfmt_decode_stream(archive,
                                        (uint64_t)state->current_index, NULL,
                                        pd);
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) {
        if (owned_path) xx_str_free(owned_path);
        return false;
    }
    base_length = xx_str_len(base_path);
    if (base_length != 0U && base_path[base_length - 1U] != '/' &&
        base_path[base_length - 1U] != '\\') {
        destination_path = xx_str_concat3(base_path, "/", name);
    } else {
        destination_path = xx_str_concat(base_path, name);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        result = output &&
                 xx_lzopfmt_decode_stream(archive,
                                          (uint64_t)state->current_index,
                                          output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_lzop_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

uint64_t xx_lzop_get_number_of_streams(const xx_lzop *archive) {
    return archive ? archive->number_of_streams : 0U;
}
uint64_t xx_lzop_get_number_of_blocks(const xx_lzop *archive) {
    return archive ? archive->number_of_blocks : 0U;
}
uint64_t xx_lzop_get_uncompressed_size(const xx_lzop *archive) {
    return archive ? archive->uncompressed_size : 0U;
}
int64_t xx_lzop_get_stream_end(const xx_lzop *archive) {
    return archive ? archive->stream_end : -1;
}
uint32_t xx_lzop_get_flags(const xx_lzop *archive) {
    return archive ? archive->flags : 0U;
}
uint8_t xx_lzop_get_method(const xx_lzop *archive) {
    return archive ? archive->method : 0U;
}
uint8_t xx_lzop_get_level(const xx_lzop *archive) {
    return archive ? archive->level : 0U;
}
const char *xx_lzop_get_stored_name(const xx_lzop *archive) {
    const xx_lzopfmt_stream *stream = xx_lzopfmt_stream_at(archive, 0U);
    return stream && stream->has_stored_name ? stream->name : NULL;
}
const char *xx_lzop_get_stream_name(const xx_lzop *archive, uint64_t index) {
    const xx_lzopfmt_stream *stream = xx_lzopfmt_stream_at(archive, index);
    return stream ? stream->name : NULL;
}
uint64_t xx_lzop_get_stream_uncompressed_size(const xx_lzop *archive,
                                              uint64_t index) {
    const xx_lzopfmt_stream *stream = xx_lzopfmt_stream_at(archive, index);
    return stream ? stream->uncompressed : 0U;
}
