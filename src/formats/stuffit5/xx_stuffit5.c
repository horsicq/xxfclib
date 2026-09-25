/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * StuffIt 5 archives ("StuffIt (c)1997-..." banner).  xx_stuffit5.h carries
 * the field tables.
 *
 * Sources.  The archive and entry header fields follow Deark's
 * modules/stuffit.c (MIT, Copyright (C) 2018 Jason Summers: do_v5_archivehdr,
 * do_v5_member_header).  The rest was measured on the 34 StuffIt 5 archives
 * of the reference corpus: entries are walked in file order (a return
 * receipt that StuffIt 7 appends is reachable only that way, not through
 * the next-entry links), the second header block is 4 bytes shorter for
 * entry version 3, every folder is closed by a marker entry whose child
 * offset is 0xFFFFFFFF, and the archive header CRC covers [0, first entry)
 * with the CRC field zeroed.
 *
 * Method 13 (tables, tree builder, symbol loop) comes from this library's
 * xx_stuffit.c, a port of XArchive's Algos/xdearkstuffit13_p.cpp, itself a
 * C port of the MIT-licensed compcol clean-room implementation (Copyright
 * (c) 2026 Karpeles Lab Inc.); here it streams through a 128 KiB ring
 * instead of a whole-fork buffer.
 *
 * Method 15, Arsenic, is written from the stream's structure.  An adaptive
 * arithmetic coder (26-bit code register, frequency models that halve past
 * a limit) carries "As", the block size, and for every block a
 * randomisation flag, the BWT primary index and move-to-front ranks with
 * zero runs in bijective base 2.  Each block is inverted (BWT), un-
 * randomised and run-length expanded (four equal bytes, then a repeat
 * count); the last block is followed by the CRC-32 of the whole fork.  The
 * randomisation gaps (SIT5_ARSENIC_GAPS) were measured on the corpus as
 * the distances between the bit-0 differences of a randomised block and
 * the reference output re-encoded with the same run-length rule.
 *
 * Every length, count and offset is bounded against the archive before it
 * is used, loops are bounded by the archive size and fixed caps, and a
 * fork is accepted only when its checksum matches.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stuffit5/xx_stuffit5.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* The enum entry is added by the coordinator; keep compiling until it is. */
#ifdef STUFFIT5
#define XX_STUFFIT5_FILE_TYPE XX_FILE_TYPE_STUFFIT5
#else
#define XX_STUFFIT5_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SIT5_HEADER_SIZE 100U
#define SIT5_VERSION 5U
#define SIT5_ENTRY_MAGIC UINT32_C(0xA5A5A5A5)
#define SIT5_ENTRY_MIN 48U
#define SIT5_ENTRY_MAX 0xFFFFU
/* The header CRC is checked over [0, first entry); the corpus has 100..152. */
#define SIT5_MAX_PREAMBLE 0x10000U
#define SIT5_MAX_ENTRIES 2000000U
#define SIT5_MAX_RECORDS 1000000U
#define SIT5_MAX_DEPTH 32U
#define SIT5_MAX_NAME 1024U
#define SIT5_MAX_SUFFIX 100000U
#define SIT5_IO_CHUNK 0x10000U
#define SIT5_SECOND_MAX (14U + 22U + 14U)

#define SIT5_FLAG_ENCRYPTED 0x20U
#define SIT5_FLAG_FOLDER 0x40U
#define SIT5_END_MARK UINT32_C(0xFFFFFFFF)

#define SIT5_METHOD_STORE 0U
#define SIT5_METHOD_RLE90 1U
#define SIT5_METHOD_LZHUFF 13U
#define SIT5_METHOD_ARSENIC 15U

static uint16_t sit5_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint32_t sit5_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static bool sit5_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ------------------------------------------------------------------ */
/* Fork input and output.                                             */
/* ------------------------------------------------------------------ */

/* Buffered reader over one packed fork. */
typedef struct sit5_input_s {
    xx_io_device *device;
    int64_t offset;
    uint64_t remaining;
    size_t position;
    size_t length;
    uint32_t overrun; /* zero bytes handed to Arsenic past the fork's end */
    bool failed;
    uint8_t buffer[SIT5_IO_CHUNK];
} sit5_input;

static bool sit5_input_fill(sit5_input *in) {
    size_t want;
    if (in->failed || in->remaining == 0U) return false;
    want = in->remaining < (uint64_t)SIT5_IO_CHUNK ? (size_t)in->remaining
                                                   : (size_t)SIT5_IO_CHUNK;
    if (!sit5_read_at(in->device, in->offset, in->buffer, want)) {
        in->failed = true;
        return false;
    }
    in->offset += (int64_t)want;
    in->remaining -= want;
    in->position = 0U;
    in->length = want;
    return true;
}

/* Next byte of the fork; false at its end or on a read error. */
static bool sit5_input_next(sit5_input *in, uint8_t *byte) {
    if (in->position >= in->length && !sit5_input_fill(in)) return false;
    *byte = in->buffer[in->position++];
    return true;
}

/* Buffered writer that counts, checksums and bounds what a decoder emits. */
typedef struct sit5_sink_s {
    xx_io_device *device; /* NULL: decode and verify only */
    uint64_t expected;
    uint64_t produced;
    uint32_t crc32;
    uint16_t crc16;
    size_t fill;
    bool failed;
    uint8_t buffer[SIT5_IO_CHUNK];
} sit5_sink;

static bool sit5_sink_flush(sit5_sink *s) {
    size_t done = 0U;
    if (s->failed) return false;
    if (s->fill == 0U) return true;
    s->crc32 = xx_crc32_calc(s->crc32, s->buffer, s->fill);
    s->crc16 = xx_crc16_arc_calc(s->crc16, s->buffer, s->fill);
    while (s->device && done < s->fill) {
        ssize_t amount = xx_io_write(s->device, s->buffer + done,
                                     s->fill - done);
        if (amount <= 0 || (size_t)amount > s->fill - done) {
            s->failed = true;
            return false;
        }
        done += (size_t)amount;
    }
    s->fill = 0U;
    return true;
}

static bool sit5_sink_byte(sit5_sink *s, uint8_t value) {
    if (s->failed || s->produced >= s->expected) {
        s->failed = true;
        return false;
    }
    s->buffer[s->fill++] = value;
    s->produced++;
    return s->fill < SIT5_IO_CHUNK || sit5_sink_flush(s);
}

static bool sit5_sink_bytes(sit5_sink *s, const uint8_t *data, size_t size) {
    if (s->failed || (uint64_t)size > s->expected - s->produced) {
        s->failed = true;
        return false;
    }
    while (size != 0U) {
        size_t amount = SIT5_IO_CHUNK - s->fill;
        if (amount > size) amount = size;
        xx_rt_memcpy(s->buffer + s->fill, data, amount);
        s->fill += amount;
        s->produced += amount;
        data += amount;
        size -= amount;
        if (s->fill == SIT5_IO_CHUNK && !sit5_sink_flush(s)) return false;
    }
    return true;
}

static bool sit5_sink_repeat(sit5_sink *s, uint8_t value, size_t count) {
    if (s->failed || (uint64_t)count > s->expected - s->produced) {
        s->failed = true;
        return false;
    }
    while (count != 0U) {
        size_t amount = SIT5_IO_CHUNK - s->fill;
        if (amount > count) amount = count;
        xx_rt_memset(s->buffer + s->fill, value, amount);
        s->fill += amount;
        s->produced += amount;
        count -= amount;
        if (s->fill == SIT5_IO_CHUNK && !sit5_sink_flush(s)) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Methods 0 and 1.                                                   */
/* ------------------------------------------------------------------ */

static bool sit5_store(sit5_input *in, sit5_sink *out) {
    while (out->produced < out->expected) {
        size_t amount;
        if (in->position >= in->length && !sit5_input_fill(in)) return false;
        amount = in->length - in->position;
        if ((uint64_t)amount > out->expected - out->produced)
            amount = (size_t)(out->expected - out->produced);
        if (!sit5_sink_bytes(out, in->buffer + in->position, amount))
            return false;
        in->position += amount;
    }
    return true;
}

/* RLE90: 0x90 introduces a repeat count for the byte just emitted; a count
 * of zero is a literal 0x90. */
static bool sit5_rle90(sit5_input *in, sit5_sink *out) {
    int previous = -1;
    while (out->produced < out->expected) {
        uint8_t value = 0U;
        uint8_t count = 0U;
        if (!sit5_input_next(in, &value)) return false;
        if (value != 0x90U) {
            if (!sit5_sink_byte(out, value)) return false;
            previous = (int)value;
            continue;
        }
        if (!sit5_input_next(in, &count)) return false;
        if (count == 0U) {
            if (!sit5_sink_byte(out, 0x90U)) return false;
            previous = 0x90;
        } else if (previous < 0 ||
                   !sit5_sink_repeat(out, (uint8_t)previous,
                                     (size_t)count - 1U)) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Method 13: LZ+Huffman.                                             */
/* ------------------------------------------------------------------ */

#define SIT13_NONE UINT32_MAX
#define SIT13_MAX_CODE_LENGTH 32U
#define SIT13_LITLEN_SYMBOLS 321U
#define SIT13_EOS 0x140U

/* Method-13 interoperability tables, ported verbatim from XArchive's
 * Algos/xdearkstuffit13tables_p.h, itself derived from the MIT-licensed
 * compcol clean-room implementation (Copyright (c) 2026 Karpeles Lab Inc.). */
static const uint32_t SIT13_META_CODE_VALUES[37] = {
    1496, 88, 64, 192, 0, 120, 43, 20, 12, 28, 27, 11, 16, 32, 56, 24, 216, 3032, 384, 1664, 896, 3968, 1920, 1152, 128, 640, 984, 4056, 2008, 2520, 472, 4, 1, 2, 7, 3, 8,
};

static const uint8_t SIT13_META_CODE_LENGTHS[37] = {
    11, 8, 8, 8, 8, 7, 6, 5, 5, 5, 5, 6, 5, 6, 7, 7, 9, 12, 10, 11, 11, 12, 12, 11, 11, 11, 12, 12, 12, 12, 12, 5, 2, 2, 3, 4, 5,
};

static const uint8_t SIT13_SET1_FIRST[321] = {
    4, 5, 7, 8, 8, 9, 9, 9, 9, 7, 9, 9, 9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 9, 9, 10, 10, 9, 10, 9, 9, 5, 9, 9, 9, 9, 10, 9, 9, 9, 9, 9, 9, 9, 9, 7, 9, 9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 8, 9, 9, 8, 8, 9, 9, 9, 9, 9, 9, 9, 7, 8, 9, 7, 9, 9, 7, 7, 9, 9, 9, 9, 10, 9, 10, 10, 10, 9, 9, 9, 5, 9, 8, 7, 5, 9, 8, 8, 7, 9, 9, 8, 8, 5, 5, 7, 10, 5, 8, 5, 8, 9, 9, 9, 9, 9, 10, 9, 9, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 9, 5, 6, 5, 5, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 9, 9, 9, 10, 9, 10, 9, 10, 9, 10, 9, 10, 10, 10, 9, 10, 9, 10, 10, 9, 9, 9, 6, 9, 9, 10, 9, 5,
};

static const uint8_t SIT13_SET1_SECOND[321] = {
    4, 5, 6, 6, 7, 7, 6, 7, 7, 7, 6, 8, 7, 8, 8, 8, 8, 9, 6, 9, 8, 9, 8, 9, 9, 9, 8, 10, 5, 9, 7, 9, 6, 9, 8, 10, 9, 10, 8, 8, 9, 9, 7, 9, 8, 9, 8, 9, 8, 8, 6, 9, 9, 8, 8, 9, 9, 10, 8, 9, 9, 10, 8, 10, 8, 8, 8, 8, 8, 9, 7, 10, 6, 9, 9, 11, 7, 8, 8, 9, 8, 10, 7, 8, 6, 9, 10, 9, 9, 10, 8, 11, 9, 11, 9, 10, 9, 8, 9, 8, 8, 8, 8, 10, 9, 9, 10, 10, 8, 9, 8, 8, 8, 11, 9, 8, 8, 9, 9, 10, 8, 11, 10, 10, 8, 10, 9, 10, 8, 9, 9, 11, 9, 11, 9, 10, 10, 11, 10, 12, 9, 12, 10, 11, 10, 11, 9, 10, 10, 11, 10, 11, 10, 11, 10, 11, 10, 10, 10, 9, 9, 9, 8, 7, 6, 8, 11, 11, 9, 12, 10, 12, 9, 11, 11, 11, 10, 12, 11, 11, 10, 12, 10, 11, 10, 10, 10, 11, 10, 11, 11, 11, 9, 12, 10, 12, 11, 12, 10, 11, 10, 12, 11, 12, 11, 12, 11, 12, 10, 12, 11, 12, 11, 11, 10, 12, 10, 11, 10, 12, 10, 12, 10, 12, 10, 11, 11, 11, 10, 11, 11, 11, 10, 12, 11, 12, 10, 10, 11, 11, 9, 12, 11, 12, 10, 11, 10, 12, 10, 11, 10, 12, 10, 11, 10, 7, 5, 4, 6, 6, 7, 7, 7, 8, 8, 7, 7, 6, 8, 6, 7, 7, 9, 8, 9, 9, 10, 11, 11, 11, 12, 11, 10, 11, 12, 11, 12, 11, 12, 12, 12, 12, 11, 12, 12, 11, 12, 11, 12, 11, 13, 11, 12, 10, 13, 10, 14, 14, 13, 14, 15, 14, 16, 15, 15, 18, 18, 18, 9, 18, 8,
};

static const uint8_t SIT13_SET1_OFFSET[11] = {
    5, 6, 3, 3, 3, 3, 3, 3, 3, 4, 6
};

static const uint8_t SIT13_SET2_FIRST[321] = {
    4, 7, 7, 8, 7, 8, 8, 8, 8, 7, 8, 7, 8, 7, 9, 8, 8, 8, 9, 9, 9, 9, 10, 10, 9, 10, 10, 10, 10, 10, 9, 9, 5, 9, 8, 9, 9, 11, 10, 9, 8, 9, 9, 9, 8, 9, 7, 8, 8, 8, 9, 9, 9, 9, 9, 10, 9, 9, 9, 10, 9, 9, 10, 9, 8, 8, 7, 7, 7, 8, 8, 9, 8, 8, 9, 9, 8, 8, 7, 8, 7, 10, 8, 7, 7, 9, 9, 9, 9, 10, 10, 11, 11, 11, 10, 9, 8, 6, 8, 7, 7, 5, 7, 7, 7, 6, 9, 8, 6, 7, 6, 6, 7, 9, 6, 6, 6, 7, 8, 8, 8, 8, 9, 10, 9, 10, 9, 9, 8, 9, 10, 10, 9, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 11, 10, 10, 10, 10, 10, 10, 10, 11, 10, 11, 10, 10, 9, 11, 10, 10, 10, 10, 10, 10, 9, 9, 10, 11, 10, 11, 10, 11, 10, 12, 10, 11, 10, 12, 11, 12, 10, 12, 10, 11, 10, 11, 11, 11, 9, 10, 11, 11, 11, 12, 12, 10, 10, 10, 11, 11, 10, 11, 10, 10, 9, 11, 10, 11, 10, 11, 11, 11, 10, 11, 11, 12, 11, 11, 10, 10, 10, 11, 10, 10, 11, 11, 12, 10, 10, 11, 11, 12, 11, 11, 10, 11, 9, 12, 10, 11, 11, 11, 10, 11, 10, 11, 10, 11, 9, 10, 9, 7, 3, 5, 6, 6, 7, 7, 8, 8, 8, 9, 9, 9, 11, 10, 10, 10, 12, 13, 11, 12, 12, 11, 13, 12, 12, 11, 12, 12, 13, 12, 14, 13, 14, 13, 15, 13, 14, 15, 15, 14, 13, 15, 15, 14, 15, 14, 15, 15, 14, 15, 13, 13, 14, 15, 15, 14, 14, 16, 16, 15, 15, 15, 12, 15, 10,
};

static const uint8_t SIT13_SET2_SECOND[321] = {
    5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 8, 7, 8, 7, 7, 7, 8, 8, 8, 8, 9, 8, 9, 8, 9, 9, 9, 7, 9, 8, 8, 6, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 8, 8, 8, 8, 9, 8, 9, 8, 9, 9, 10, 8, 10, 8, 9, 9, 8, 8, 8, 7, 8, 8, 9, 8, 9, 7, 9, 8, 10, 8, 9, 8, 9, 8, 9, 8, 8, 8, 9, 9, 9, 9, 10, 9, 11, 9, 10, 9, 10, 8, 8, 8, 9, 8, 8, 8, 9, 9, 8, 9, 10, 8, 9, 8, 8, 8, 11, 8, 7, 8, 9, 9, 9, 9, 10, 9, 10, 9, 10, 9, 8, 8, 9, 9, 10, 9, 10, 9, 10, 8, 10, 9, 10, 9, 11, 10, 11, 9, 11, 10, 10, 10, 11, 9, 11, 9, 10, 9, 11, 9, 11, 10, 10, 9, 10, 9, 9, 8, 10, 9, 11, 9, 9, 9, 11, 10, 11, 9, 11, 9, 11, 9, 11, 10, 11, 10, 11, 10, 11, 9, 10, 10, 11, 10, 10, 8, 10, 9, 10, 10, 11, 9, 11, 9, 10, 10, 11, 9, 10, 10, 9, 9, 10, 9, 10, 9, 10, 9, 10, 9, 11, 9, 11, 10, 10, 9, 10, 9, 11, 9, 11, 9, 11, 9, 10, 9, 11, 9, 11, 9, 11, 9, 10, 8, 11, 9, 10, 9, 10, 9, 10, 8, 10, 8, 9, 8, 9, 8, 7, 4, 4, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 7, 8, 8, 9, 9, 10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 12, 11, 11, 12, 12, 11, 12, 12, 11, 12, 12, 12, 12, 12, 12, 11, 12, 11, 13, 12, 13, 12, 13, 14, 14, 14, 15, 13, 14, 13, 14, 18, 18, 17, 7, 16, 9,
};

static const uint8_t SIT13_SET2_OFFSET[13] = {
    5, 6, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 6
};

static const uint8_t SIT13_SET3_FIRST[321] = {
    6, 6, 6, 6, 6, 9, 8, 8, 4, 9, 8, 9, 8, 9, 9, 9, 8, 9, 9, 10, 8, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 9, 9, 9, 8, 10, 9, 10, 9, 10, 9, 10, 9, 10, 9, 9, 8, 9, 8, 9, 9, 9, 10, 10, 10, 10, 9, 9, 9, 10, 9, 10, 9, 9, 7, 8, 8, 9, 8, 9, 9, 9, 8, 9, 9, 10, 9, 9, 8, 9, 8, 9, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 9, 8, 8, 9, 8, 9, 7, 8, 8, 9, 8, 10, 10, 8, 9, 8, 8, 8, 10, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 10, 9, 7, 9, 9, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 9, 8, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 9, 9, 9, 10, 10, 10, 10, 10, 10, 9, 9, 10, 9, 9, 8, 9, 8, 9, 4, 6, 6, 6, 7, 8, 8, 9, 9, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 7, 10, 10, 10, 7, 10, 10, 7, 7, 7, 7, 7, 6, 7, 10, 7, 7, 10, 7, 7, 7, 6, 7, 6, 6, 7, 7, 6, 6, 9, 6, 9, 10, 6, 10,
};

static const uint8_t SIT13_SET3_SECOND[321] = {
    5, 6, 6, 6, 6, 7, 7, 7, 6, 8, 7, 8, 7, 9, 8, 8, 7, 7, 8, 9, 9, 9, 9, 10, 8, 9, 9, 10, 8, 10, 9, 8, 6, 10, 8, 10, 8, 10, 9, 9, 9, 9, 9, 10, 9, 9, 8, 9, 8, 9, 8, 9, 9, 10, 9, 10, 9, 9, 8, 10, 9, 11, 10, 8, 8, 8, 8, 9, 7, 9, 9, 10, 8, 9, 8, 11, 9, 10, 9, 10, 8, 9, 9, 9, 9, 8, 9, 9, 10, 10, 10, 12, 10, 11, 10, 10, 8, 9, 9, 9, 8, 9, 8, 8, 10, 9, 10, 11, 8, 10, 9, 9, 8, 12, 8, 9, 9, 9, 9, 8, 9, 10, 9, 12, 10, 10, 10, 8, 7, 11, 10, 9, 10, 11, 9, 11, 7, 11, 10, 12, 10, 12, 10, 11, 9, 11, 9, 12, 10, 12, 10, 12, 10, 9, 11, 12, 10, 12, 10, 11, 9, 10, 9, 10, 9, 11, 11, 12, 9, 10, 8, 12, 11, 12, 9, 12, 10, 12, 10, 13, 10, 12, 10, 12, 10, 12, 10, 9, 10, 12, 10, 9, 8, 11, 10, 12, 10, 12, 10, 12, 10, 11, 10, 12, 8, 12, 10, 11, 10, 10, 10, 12, 9, 11, 10, 12, 10, 12, 11, 12, 10, 9, 10, 12, 9, 10, 10, 12, 10, 11, 10, 11, 10, 12, 8, 12, 9, 12, 8, 12, 8, 11, 10, 11, 10, 11, 9, 10, 8, 10, 9, 9, 8, 9, 8, 7, 4, 3, 5, 5, 6, 5, 6, 6, 7, 7, 8, 8, 8, 7, 7, 7, 9, 8, 9, 9, 11, 9, 11, 9, 8, 9, 9, 11, 12, 11, 12, 12, 13, 13, 12, 13, 14, 13, 14, 13, 14, 13, 13, 13, 12, 13, 13, 12, 13, 13, 14, 14, 13, 13, 14, 14, 14, 14, 15, 18, 17, 18, 8, 16, 10,
};

static const uint8_t SIT13_SET3_OFFSET[14] = {
    6, 7, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 5, 7
};

static const uint8_t SIT13_SET4_FIRST[321] = {
    2, 6, 6, 7, 7, 8, 7, 8, 7, 8, 8, 9, 8, 9, 9, 9, 8, 8, 9, 9, 9, 10, 10, 9, 8, 10, 9, 10, 9, 10, 9, 9, 6, 9, 8, 9, 9, 10, 9, 9, 9, 10, 9, 9, 9, 9, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 9, 7, 7, 8, 8, 8, 8, 9, 9, 7, 8, 9, 10, 8, 8, 7, 8, 8, 10, 8, 8, 8, 9, 8, 9, 9, 10, 9, 11, 10, 11, 9, 9, 8, 7, 9, 8, 8, 6, 8, 8, 8, 7, 10, 9, 7, 8, 7, 7, 8, 10, 7, 7, 7, 8, 9, 9, 9, 9, 10, 11, 9, 11, 10, 9, 7, 9, 10, 10, 10, 11, 11, 10, 10, 11, 10, 10, 10, 11, 11, 10, 9, 10, 10, 11, 10, 11, 10, 11, 10, 10, 10, 11, 10, 11, 10, 10, 9, 10, 10, 11, 10, 10, 10, 10, 9, 10, 10, 10, 10, 11, 10, 11, 10, 11, 10, 11, 11, 11, 10, 12, 10, 11, 10, 11, 10, 11, 11, 10, 8, 10, 10, 11, 10, 11, 11, 11, 10, 11, 10, 11, 10, 11, 11, 11, 9, 10, 11, 11, 10, 11, 11, 11, 10, 11, 11, 11, 10, 10, 10, 10, 10, 11, 10, 10, 11, 11, 10, 10, 9, 11, 10, 10, 11, 11, 10, 10, 10, 11, 10, 10, 10, 10, 10, 10, 9, 11, 10, 10, 8, 10, 8, 6, 5, 6, 6, 7, 7, 8, 8, 8, 9, 10, 11, 10, 10, 11, 11, 12, 12, 10, 11, 12, 12, 12, 12, 13, 13, 13, 13, 13, 12, 13, 13, 15, 14, 12, 14, 15, 16, 12, 12, 13, 15, 14, 16, 15, 17, 18, 15, 17, 16, 15, 15, 15, 15, 13, 13, 10, 14, 12, 13, 17, 17, 18, 10, 17, 4,
};

static const uint8_t SIT13_SET4_SECOND[321] = {
    4, 5, 6, 6, 6, 6, 7, 7, 6, 7, 7, 9, 6, 8, 8, 7, 7, 8, 8, 8, 6, 9, 8, 8, 7, 9, 8, 9, 8, 9, 8, 9, 6, 9, 8, 9, 8, 10, 9, 9, 8, 10, 8, 10, 8, 9, 8, 9, 8, 8, 7, 9, 9, 9, 9, 9, 8, 10, 9, 10, 9, 10, 9, 8, 7, 8, 9, 9, 8, 9, 9, 9, 7, 10, 9, 10, 9, 9, 8, 9, 8, 9, 8, 8, 8, 9, 9, 10, 9, 9, 8, 11, 9, 11, 10, 10, 8, 8, 10, 8, 8, 9, 9, 9, 10, 9, 10, 11, 9, 9, 9, 9, 8, 9, 8, 8, 8, 10, 10, 9, 9, 8, 10, 11, 10, 11, 11, 9, 8, 9, 10, 11, 9, 10, 11, 11, 9, 12, 10, 10, 10, 12, 11, 11, 9, 11, 11, 12, 9, 11, 9, 10, 10, 10, 10, 12, 9, 11, 10, 11, 9, 11, 11, 11, 10, 11, 11, 12, 9, 10, 10, 12, 11, 11, 10, 11, 9, 11, 10, 11, 10, 11, 9, 11, 11, 9, 8, 11, 10, 11, 11, 10, 7, 12, 11, 11, 11, 11, 11, 12, 10, 12, 11, 13, 11, 10, 12, 11, 10, 11, 10, 11, 10, 11, 11, 11, 10, 12, 11, 11, 10, 11, 10, 10, 10, 11, 10, 12, 11, 12, 10, 11, 9, 11, 10, 11, 10, 11, 10, 12, 9, 11, 11, 11, 9, 11, 10, 10, 9, 11, 10, 10, 9, 10, 9, 7, 4, 5, 5, 5, 6, 6, 7, 6, 8, 7, 8, 9, 9, 7, 8, 8, 10, 9, 10, 10, 12, 10, 11, 11, 11, 11, 10, 11, 12, 11, 11, 11, 11, 11, 13, 12, 11, 12, 13, 12, 12, 12, 13, 11, 9, 12, 13, 7, 13, 11, 13, 11, 10, 11, 13, 15, 15, 12, 14, 15, 15, 15, 6, 15, 5,
};

static const uint8_t SIT13_SET4_OFFSET[11] = {
    3, 6, 5, 4, 2, 3, 3, 3, 4, 4, 6
};

static const uint8_t SIT13_SET5_FIRST[321] = {
    7, 9, 9, 9, 9, 9, 9, 9, 9, 8, 9, 9, 9, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 9, 10, 9, 10, 9, 10, 9, 9, 5, 9, 7, 9, 9, 9, 9, 9, 7, 7, 7, 9, 7, 7, 8, 7, 8, 8, 7, 7, 9, 9, 9, 9, 7, 7, 7, 9, 9, 9, 9, 9, 9, 7, 9, 7, 7, 7, 7, 9, 9, 7, 9, 9, 7, 7, 7, 7, 7, 9, 7, 8, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 7, 8, 7, 7, 7, 8, 8, 6, 7, 9, 7, 7, 8, 7, 5, 6, 9, 5, 7, 5, 6, 7, 7, 9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 10, 9, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 10, 9, 9, 10, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 9, 10, 10, 10, 9, 9, 9, 10, 10, 10, 10, 10, 9, 10, 9, 10, 10, 9, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 10, 10, 10, 10, 10, 9, 10, 9, 10, 9, 10, 10, 9, 5, 6, 8, 8, 7, 7, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 10, 5, 10, 8, 9, 8, 9,
};

static const uint8_t SIT13_SET5_SECOND[321] = {
    8, 10, 11, 11, 11, 12, 11, 11, 12, 6, 11, 12, 10, 5, 12, 12, 12, 12, 12, 12, 12, 13, 13, 14, 13, 13, 12, 13, 12, 13, 12, 15, 4, 10, 7, 9, 11, 11, 10, 9, 6, 7, 8, 9, 6, 7, 6, 7, 8, 7, 7, 8, 8, 8, 8, 8, 8, 9, 8, 7, 10, 9, 10, 10, 11, 7, 8, 6, 7, 8, 8, 9, 8, 7, 10, 10, 8, 7, 8, 8, 7, 10, 7, 6, 7, 9, 9, 8, 11, 11, 11, 10, 11, 11, 11, 8, 11, 6, 7, 6, 6, 6, 6, 8, 7, 6, 10, 9, 6, 7, 6, 6, 7, 10, 6, 5, 6, 7, 7, 7, 10, 8, 11, 9, 13, 7, 14, 16, 12, 14, 14, 15, 15, 16, 16, 14, 15, 15, 15, 15, 15, 15, 15, 15, 14, 15, 13, 14, 14, 16, 15, 17, 14, 17, 15, 17, 12, 14, 13, 16, 12, 17, 13, 17, 14, 13, 13, 14, 14, 12, 13, 15, 15, 14, 15, 17, 14, 17, 15, 14, 15, 16, 12, 16, 15, 14, 15, 16, 15, 16, 17, 17, 15, 15, 17, 17, 13, 14, 15, 15, 13, 12, 16, 16, 17, 14, 15, 16, 15, 15, 13, 13, 15, 13, 16, 17, 15, 17, 17, 17, 16, 17, 14, 17, 14, 16, 15, 17, 15, 15, 14, 17, 15, 17, 15, 16, 15, 15, 16, 16, 14, 17, 17, 15, 15, 16, 15, 17, 15, 14, 16, 16, 16, 16, 16, 12, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 10, 10, 10, 11, 10, 11, 11, 11, 11, 11, 12, 12, 12, 13, 13, 12, 13, 12, 14, 14, 12, 13, 13, 13, 13, 14, 12, 13, 13, 14, 14, 14, 13, 14, 14, 15, 15, 13, 15, 13, 17, 17, 17, 9, 17, 7,
};

static const uint8_t SIT13_SET5_OFFSET[11] = {
    6, 7, 7, 6, 4, 3, 2, 2, 3, 3, 6
};

static const uint8_t *const SIT13_FIRST[5] = {
    SIT13_SET1_FIRST, SIT13_SET2_FIRST, SIT13_SET3_FIRST, SIT13_SET4_FIRST, SIT13_SET5_FIRST
};
static const uint8_t *const SIT13_SECOND[5] = {
    SIT13_SET1_SECOND, SIT13_SET2_SECOND, SIT13_SET3_SECOND, SIT13_SET4_SECOND, SIT13_SET5_SECOND
};
static const uint8_t *const SIT13_OFFSET[5] = {
    SIT13_SET1_OFFSET, SIT13_SET2_OFFSET, SIT13_SET3_OFFSET, SIT13_SET4_OFFSET, SIT13_SET5_OFFSET
};
static const uint8_t SIT13_OFFSET_SIZE[5] = {11, 13, 14, 11, 11};

/* LSB-first bits pulled from the fork; running out of fork is an error. */
typedef struct sit13_bitreader_s {
    sit5_input *input;
    uint32_t bits;
    uint32_t count;
} sit13_bitreader;

typedef struct sit13_huffman_s {
    uint32_t *links;
    uint32_t *leaf;
    size_t node_count;
    size_t node_capacity;
} sit13_huffman;

static int sit13_read_bit(sit13_bitreader *r, uint32_t *value) {
    if (!r || !value) return 1;
    if (r->count == 0U) {
        uint8_t byte = 0U;
        if (!sit5_input_next(r->input, &byte)) return 2;
        r->bits = byte;
        r->count = 8U;
    }
    *value = r->bits & 1U;
    r->bits >>= 1U;
    r->count--;
    return 0;
}

static int sit13_read_bits(sit13_bitreader *r, uint32_t count,
                           uint32_t *value) {
    uint32_t index;
    uint32_t result = 0U;
    if (!r || !value || count > 32U) return 1;
    for (index = 0U; index < count; ++index) {
        uint32_t bit = 0U;
        int rc = sit13_read_bit(r, &bit);
        if (rc) return rc;
        result |= bit << index;
    }
    *value = result;
    return 0;
}

static void sit13_huffman_destroy(sit13_huffman *tree) {
    if (!tree) return;
    if (tree->links) xx_mem_free(tree->links);
    if (tree->leaf) xx_mem_free(tree->leaf);
    xx_mem_zero(tree, sizeof(*tree));
}

static int sit13_huffman_init(sit13_huffman *tree, size_t symbols) {
    size_t capacity;
    if (!tree || symbols == 0U || symbols > (SIZE_MAX - 1U) / 32U) return 1;
    xx_mem_zero(tree, sizeof(*tree));
    capacity = symbols * 32U + 1U;
    tree->links = (uint32_t *)xx_mem_alloc(capacity * 2U * sizeof(uint32_t));
    tree->leaf = (uint32_t *)xx_mem_alloc(capacity * sizeof(uint32_t));
    if (!tree->links || !tree->leaf) {
        sit13_huffman_destroy(tree);
        return 4;
    }
    tree->node_capacity = capacity;
    tree->node_count = 1U;
    tree->links[0] = tree->links[1] = SIT13_NONE;
    tree->leaf[0] = SIT13_NONE;
    return 0;
}

static int sit13_huffman_new_node(sit13_huffman *tree, uint32_t *index) {
    size_t node;
    if (!tree || !index || tree->node_count >= tree->node_capacity) return 3;
    node = tree->node_count++;
    tree->links[node * 2U] = tree->links[node * 2U + 1U] = SIT13_NONE;
    tree->leaf[node] = SIT13_NONE;
    *index = (uint32_t)node;
    return 0;
}

static int sit13_huffman_insert(sit13_huffman *tree, uint32_t code,
                                uint32_t length, uint32_t symbol) {
    uint32_t node = 0U;
    uint32_t index;
    if (!tree || length == 0U || length > SIT13_MAX_CODE_LENGTH) return 3;
    for (index = 0U; index < length; ++index) {
        uint32_t bit;
        size_t slot;
        if (node >= tree->node_count || tree->leaf[node] != SIT13_NONE)
            return 3;
        bit = (code >> index) & 1U;
        slot = (size_t)node * 2U + bit;
        if (tree->links[slot] == SIT13_NONE) {
            uint32_t created = 0U;
            int rc = sit13_huffman_new_node(tree, &created);
            if (rc) return rc;
            tree->links[slot] = created;
        }
        node = tree->links[slot];
    }
    if (node >= tree->node_count || tree->leaf[node] != SIT13_NONE ||
        tree->links[(size_t)node * 2U] != SIT13_NONE ||
        tree->links[(size_t)node * 2U + 1U] != SIT13_NONE)
        return 3;
    tree->leaf[node] = symbol;
    return 0;
}

static uint32_t sit13_reverse_bits(uint32_t value, uint32_t count) {
    uint32_t index;
    uint32_t result = 0U;
    for (index = 0U; index < count; ++index)
        result |= ((value >> index) & 1U) << (count - 1U - index);
    return result;
}

static int sit13_huffman_from_lengths(sit13_huffman *tree,
                                      const uint8_t *lengths,
                                      size_t symbol_count) {
    uint32_t counts[SIT13_MAX_CODE_LENGTH + 1U];
    uint32_t next_code[SIT13_MAX_CODE_LENGTH + 1U];
    uint32_t max_length = 0U;
    uint32_t code = 0U;
    uint64_t kraft = 0U;
    size_t index;
    int rc;
    if (!tree || !lengths || symbol_count == 0U) return 1;
    xx_mem_zero(counts, sizeof(counts));
    xx_mem_zero(next_code, sizeof(next_code));
    for (index = 0U; index < symbol_count; ++index) {
        uint32_t length = lengths[index];
        if (length > SIT13_MAX_CODE_LENGTH) return 3;
        if (length) {
            counts[length]++;
            if (length > max_length) max_length = length;
        }
    }
    if (max_length == 0U) return 3;
    for (index = 1U; index <= max_length; ++index)
        kraft += (uint64_t)counts[index] << (max_length - (uint32_t)index);
    /* A non-complete code would leave a decodable hole; refuse it. */
    if (kraft != ((uint64_t)1U << max_length)) return 3;
    rc = sit13_huffman_init(tree, symbol_count);
    if (rc) return rc;
    for (index = 1U; index <= max_length; ++index) {
        next_code[index] = code;
        code = (code + counts[index]) << 1U;
    }
    for (index = 0U; index < symbol_count; ++index) {
        uint32_t length = lengths[index];
        if (length) {
            uint32_t canonical = next_code[length]++;
            rc = sit13_huffman_insert(tree,
                                      sit13_reverse_bits(canonical, length),
                                      length, (uint32_t)index);
            if (rc) {
                sit13_huffman_destroy(tree);
                return rc;
            }
        }
    }
    return 0;
}

static int sit13_huffman_from_codes(sit13_huffman *tree,
                                    const uint32_t *codes,
                                    const uint8_t *lengths,
                                    size_t symbol_count) {
    size_t index;
    int rc = sit13_huffman_init(tree, symbol_count);
    if (rc) return rc;
    for (index = 0U; index < symbol_count; ++index) {
        if (lengths[index]) {
            rc = sit13_huffman_insert(tree, codes[index], lengths[index],
                                      (uint32_t)index);
            if (rc) {
                sit13_huffman_destroy(tree);
                return rc;
            }
        }
    }
    return 0;
}

static int sit13_huffman_decode(const sit13_huffman *tree,
                                sit13_bitreader *reader, uint32_t *symbol) {
    uint32_t node = 0U;
    if (!tree || !reader || !symbol) return 1;
    for (;;) {
        uint32_t bit = 0U;
        uint32_t next;
        int rc;
        if (node >= tree->node_count) return 3;
        if (tree->leaf[node] != SIT13_NONE) {
            *symbol = tree->leaf[node];
            return 0;
        }
        rc = sit13_read_bit(reader, &bit);
        if (rc) return rc;
        next = tree->links[(size_t)node * 2U + bit];
        if (next == SIT13_NONE) return 3;
        node = next;
    }
}

static int sit13_read_code_lengths(sit13_bitreader *reader,
                                   const sit13_huffman *meta,
                                   uint8_t *lengths, size_t count) {
    size_t used = 0U;
    int accumulator = 0;
    if (!reader || !meta || !lengths) return 1;
    while (used < count) {
        uint32_t value = 0U;
        size_t extra = 0U;
        size_t index;
        uint8_t length;
        int rc = sit13_huffman_decode(meta, reader, &value);
        if (rc) return rc;
        if (value <= 30U) {
            accumulator = (int)value + 1;
        } else if (value == 31U) {
            accumulator = -1;
        } else if (value == 32U) {
            if (accumulator == INT_MAX) return 3;
            accumulator++;
        } else if (value == 33U) {
            if (accumulator == INT_MIN) return 3;
            accumulator--;
        } else if (value == 34U) {
            uint32_t bit = 0U;
            rc = sit13_read_bit(reader, &bit);
            if (rc) return rc;
            if (bit) extra = 1U;
        } else if (value == 35U) {
            uint32_t bits = 0U;
            rc = sit13_read_bits(reader, 3U, &bits);
            if (rc) return rc;
            extra = (size_t)bits + 2U;
        } else if (value == 36U) {
            uint32_t bits = 0U;
            rc = sit13_read_bits(reader, 6U, &bits);
            if (rc) return rc;
            extra = (size_t)bits + 10U;
        } else {
            return 3;
        }
        if (accumulator > (int)SIT13_MAX_CODE_LENGTH) return 3;
        length = accumulator >= 1 ? (uint8_t)accumulator : 0U;
        for (index = 0U; index <= extra && used < count; ++index)
            lengths[used++] = length;
    }
    return 0;
}

static int sit13_prepare_dynamic_codes(sit13_bitreader *reader,
                                       uint8_t control, sit13_huffman *first,
                                       sit13_huffman *second,
                                       sit13_huffman *offset) {
    sit13_huffman meta;
    uint8_t first_lengths[SIT13_LITLEN_SYMBOLS];
    uint8_t second_lengths[SIT13_LITLEN_SYMBOLS];
    uint8_t offset_lengths[17];
    size_t offset_count = (size_t)(control & 7U) + 10U;
    int rc;
    xx_mem_zero(&meta, sizeof(meta));
    rc = sit13_huffman_from_codes(&meta, SIT13_META_CODE_VALUES,
                                  SIT13_META_CODE_LENGTHS, 37U);
    if (rc) return rc;
    rc = sit13_read_code_lengths(reader, &meta, first_lengths,
                                 SIT13_LITLEN_SYMBOLS);
    if (!rc)
        rc = sit13_huffman_from_lengths(first, first_lengths,
                                        SIT13_LITLEN_SYMBOLS);
    if (!rc) {
        if (control & 8U)
            xx_rt_memcpy(second_lengths, first_lengths, SIT13_LITLEN_SYMBOLS);
        else
            rc = sit13_read_code_lengths(reader, &meta, second_lengths,
                                         SIT13_LITLEN_SYMBOLS);
    }
    if (!rc)
        rc = sit13_huffman_from_lengths(second, second_lengths,
                                        SIT13_LITLEN_SYMBOLS);
    if (!rc)
        rc = sit13_read_code_lengths(reader, &meta, offset_lengths,
                                     offset_count);
    if (!rc)
        rc = sit13_huffman_from_lengths(offset, offset_lengths, offset_count);
    sit13_huffman_destroy(&meta);
    return rc;
}


/* The window is 64 KiB; the ring keeps twice that, and at most 0x8000
 * bytes plus one match (under 0x8100) are ever waiting to be flushed. */
#define SIT13_RING 0x20000U
#define SIT13_RING_MASK (SIT13_RING - 1U)
#define SIT13_FLUSH_AT 0x8000U

static bool sit13_flush(const uint8_t *ring, uint64_t *flushed,
                        uint64_t produced, sit5_sink *out) {
    while (*flushed < produced) {
        size_t start = (size_t)(*flushed & SIT13_RING_MASK);
        uint64_t amount = produced - *flushed;
        if (amount > (uint64_t)(SIT13_RING - start))
            amount = (uint64_t)(SIT13_RING - start);
        if (!sit5_sink_bytes(out, ring + start, (size_t)amount)) return false;
        *flushed += amount;
    }
    return true;
}

static bool sit13_decode(sit5_input *input, sit5_sink *out) {
    sit13_bitreader reader;
    sit13_huffman first;
    sit13_huffman second;
    sit13_huffman offset;
    uint32_t control_value = 0U;
    uint8_t control;
    uint8_t high;
    uint8_t *ring = NULL;
    uint64_t output_size = out->expected;
    uint64_t output_pos = 0U;
    uint64_t flushed = 0U;
    bool use_first = true;
    int rc = 0;
    xx_mem_zero(&first, sizeof(first));
    xx_mem_zero(&second, sizeof(second));
    xx_mem_zero(&offset, sizeof(offset));
    if (output_size == 0U) return true;
    reader.input = input;
    reader.bits = 0U;
    reader.count = 0U;
    ring = (uint8_t *)xx_mem_alloc(SIT13_RING);
    if (!ring) return false;
    rc = sit13_read_bits(&reader, 8U, &control_value);
    if (rc) goto done;
    control = (uint8_t)control_value;
    high = (uint8_t)(control >> 4U);
    if (high == 0U) {
        rc = sit13_prepare_dynamic_codes(&reader, control, &first, &second,
                                         &offset);
        if (rc) goto done;
    } else if (high <= 5U) {
        size_t set = (size_t)high - 1U;
        rc = sit13_huffman_from_lengths(&first, SIT13_FIRST[set],
                                        SIT13_LITLEN_SYMBOLS);
        if (!rc)
            rc = sit13_huffman_from_lengths(&second, SIT13_SECOND[set],
                                            SIT13_LITLEN_SYMBOLS);
        if (!rc)
            rc = sit13_huffman_from_lengths(&offset, SIT13_OFFSET[set],
                                            SIT13_OFFSET_SIZE[set]);
        if (rc) goto done;
    } else {
        rc = 3;
        goto done;
    }
    while (output_pos < output_size) {
        uint32_t symbol = 0U;
        const sit13_huffman *code = use_first ? &first : &second;
        rc = sit13_huffman_decode(code, &reader, &symbol);
        if (rc) goto done;
        if (symbol <= 0xffU) {
            ring[output_pos & SIT13_RING_MASK] = (uint8_t)symbol;
            output_pos++;
            use_first = true;
        } else if (symbol == SIT13_EOS) {
            rc = 3;
            goto done;
        } else {
            uint64_t length;
            uint64_t distance;
            uint32_t offset_bits = 0U;
            uint32_t extra = 0U;
            uint64_t index;
            if (symbol <= 0x13dU) {
                length = (uint64_t)(symbol - 0x100U) + 3U;
            } else if (symbol == 0x13eU) {
                rc = sit13_read_bits(&reader, 10U, &extra);
                if (rc) goto done;
                length = (uint64_t)extra + 65U;
            } else if (symbol == 0x13fU) {
                rc = sit13_read_bits(&reader, 15U, &extra);
                if (rc) goto done;
                length = (uint64_t)extra + 65U;
            } else {
                rc = 3;
                goto done;
            }
            rc = sit13_huffman_decode(&offset, &reader, &offset_bits);
            if (rc) goto done;
            if (offset_bits == 0U) {
                distance = 1U;
            } else if (offset_bits == 1U) {
                distance = 2U;
            } else {
                if (offset_bits > 17U) {
                    rc = 3;
                    goto done;
                }
                rc = sit13_read_bits(&reader, offset_bits - 1U, &extra);
                if (rc) goto done;
                distance = ((uint64_t)1U << (offset_bits - 1U)) +
                           (uint64_t)extra + 1U;
            }
            if (distance == 0U || distance > 0x10000U ||
                distance > output_pos || length > output_size - output_pos) {
                rc = 3;
                goto done;
            }
            for (index = 0U; index < length; ++index) {
                ring[output_pos & SIT13_RING_MASK] =
                    ring[(output_pos - distance) & SIT13_RING_MASK];
                output_pos++;
            }
            use_first = false;
        }
        if (output_pos - flushed >= SIT13_FLUSH_AT &&
            !sit13_flush(ring, &flushed, output_pos, out)) {
            rc = 4;
            goto done;
        }
    }
    if (!sit13_flush(ring, &flushed, output_pos, out)) rc = 4;
done:
    sit13_huffman_destroy(&first);
    sit13_huffman_destroy(&second);
    sit13_huffman_destroy(&offset);
    xx_mem_free(ring);
    return rc == 0;
}

/* ------------------------------------------------------------------ */
/* Method 15: Arsenic.                                                */
/* ------------------------------------------------------------------ */

#define ARS_CODE_BITS 26U
#define ARS_ONE (UINT32_C(1) << (ARS_CODE_BITS - 1U))
#define ARS_HALF (UINT32_C(1) << (ARS_CODE_BITS - 2U))
#define ARS_MIN_BLOCK_BITS 9U
/* The coder looks ahead; the corpus streams need at most 4 bytes past
 * their packed size. */
#define ARS_MAX_OVERRUN 8U

/* Gap, in block positions, from one randomised byte to the next; the first
 * randomised byte is at position SIT5_ARSENIC_GAPS[0].  Measured on the
 * corpus (see the file comment). */
static const uint16_t SIT5_ARSENIC_GAPS[256] = {
    0xee, 0x56, 0xf8, 0xc3, 0x9d, 0x9f, 0xae, 0x2c,
    0xad, 0xcd, 0x24, 0x9d, 0xa6, 0x101, 0x18, 0xb9,
    0xa1, 0x82, 0x75, 0xe9, 0x9f, 0x55, 0x66, 0x6a,
    0x86, 0x71, 0xdc, 0x84, 0x56, 0x96, 0x56, 0xa1,
    0x84, 0x78, 0xb7, 0x32, 0x6a, 0x03, 0xe3, 0x02,
    0x11, 0x101, 0x08, 0x44, 0x83, 0x100, 0x43, 0xe3,
    0x1c, 0xf0, 0x86, 0x6a, 0x6b, 0x0f, 0x03, 0x2d,
    0x86, 0x17, 0x7b, 0x10, 0xf6, 0x80, 0x78, 0x7a,
    0xa1, 0xe1, 0xef, 0x8c, 0xf6, 0x87, 0x4b, 0xa7,
    0xe2, 0x77, 0xfa, 0xb8, 0x81, 0xee, 0x77, 0xc0,
    0x9d, 0x29, 0x20, 0x27, 0x71, 0x12, 0xe0, 0x6b,
    0xd1, 0x7c, 0x0a, 0x89, 0x7d, 0x87, 0xc4, 0x101,
    0xc1, 0x31, 0xaf, 0x38, 0x03, 0x68, 0x1b, 0x76,
    0x79, 0x3f, 0xdb, 0xc7, 0x1b, 0x36, 0x7b, 0xe2,
    0x63, 0x81, 0xee, 0x0c, 0x63, 0x8b, 0x78, 0x38,
    0x97, 0x9b, 0xd7, 0x8f, 0xdd, 0xf2, 0xa3, 0x77,
    0x8c, 0xc3, 0x39, 0x20, 0xb3, 0x12, 0x11, 0x0e,
    0x17, 0x42, 0x80, 0x2c, 0xc4, 0x92, 0x59, 0xc8,
    0xdb, 0x40, 0x76, 0x64, 0xb4, 0x55, 0x1a, 0x9e,
    0xfe, 0x5f, 0x06, 0x3c, 0x41, 0xef, 0xd4, 0xaa,
    0x98, 0x29, 0xcd, 0x1f, 0x02, 0xa8, 0x87, 0xd2,
    0xa0, 0x93, 0x98, 0xef, 0x0c, 0x43, 0xed, 0x9d,
    0xc2, 0xeb, 0x81, 0xe9, 0x64, 0x23, 0x68, 0x1e,
    0x25, 0x57, 0xde, 0x9a, 0xcf, 0x7f, 0xe5, 0xba,
    0x41, 0xea, 0xea, 0x36, 0x1a, 0x28, 0x79, 0x20,
    0x5e, 0x18, 0x4e, 0x7c, 0x8e, 0x58, 0x7a, 0xef,
    0x91, 0x02, 0x93, 0xbb, 0x56, 0xa1, 0x49, 0x1b,
    0x79, 0x92, 0xf3, 0x58, 0x4f, 0x52, 0x9c, 0x02,
    0x77, 0xaf, 0x2a, 0x8f, 0x49, 0xd0, 0x99, 0x4d,
    0x98, 0x101, 0x60, 0x93, 0x100, 0x75, 0x31, 0xce,
    0x49, 0x20, 0x56, 0x57, 0xe2, 0xf5, 0x26, 0x2b,
    0x8a, 0xbf, 0xde, 0xd0, 0x83, 0x34, 0xf4, 0x17,
};

/* An adaptive frequency model over the symbols first..first+count-1. */
typedef struct ars_model_s {
    uint32_t first;
    uint32_t count;
    uint32_t increment;
    uint32_t limit;
    uint32_t total;
    uint32_t frequency[128];
} ars_model;

typedef struct ars_decoder_s {
    sit5_input *input;
    uint32_t range;
    uint32_t code;
    uint32_t byte;
    uint32_t bits_left;
    bool failed;
    ars_model initial;  /* 0..1: flags and bit strings */
    ars_model selector; /* 0..10: zero-run digits, rank classes, end */
    ars_model rank[7];  /* 2..3, 4..7, ..., 128..255 */
} ars_decoder;

static void ars_model_reset(ars_model *m) {
    uint32_t index;
    for (index = 0U; index < m->count; ++index)
        m->frequency[index] = m->increment;
    m->total = m->increment * m->count;
}

static void ars_model_init(ars_model *m, uint32_t first, uint32_t last,
                           uint32_t increment, uint32_t limit) {
    m->first = first;
    m->count = last - first + 1U;
    m->increment = increment;
    m->limit = limit;
    ars_model_reset(m);
}

/* MSB-first bits; past the fork's end the coder is fed zero bytes, but only
 * a few, so a stream cut short cannot keep it running. */
static uint32_t ars_bit(ars_decoder *d) {
    if (d->bits_left == 0U) {
        uint8_t value = 0U;
        if (!sit5_input_next(d->input, &value)) {
            if (d->input->failed || ++d->input->overrun > ARS_MAX_OVERRUN)
                d->failed = true;
            value = 0U;
        }
        d->byte = value;
        d->bits_left = 8U;
    }
    d->bits_left--;
    return (d->byte >> d->bits_left) & 1U;
}

static uint32_t ars_symbol(ars_decoder *d, ars_model *m) {
    uint32_t scale;
    uint32_t target;
    uint32_t cumulative = 0U;
    uint32_t index = 0U;
    uint32_t low;
    /* code < range holds for every well-formed stream. */
    if (d->failed || d->code >= d->range) {
        d->failed = true;
        return m->first;
    }
    scale = d->range / m->total;
    target = d->code / scale;
    while (index + 1U < m->count &&
           cumulative + m->frequency[index] <= target) {
        cumulative += m->frequency[index];
        ++index;
    }
    low = scale * cumulative;
    d->code -= low;
    if (index + 1U == m->count)
        d->range -= low;
    else
        d->range = m->frequency[index] * scale;
    m->frequency[index] += m->increment;
    m->total += m->increment;
    if (m->total > m->limit) {
        uint32_t k;
        m->total = 0U;
        for (k = 0U; k < m->count; ++k) {
            m->frequency[k] = (m->frequency[k] + 1U) >> 1U;
            m->total += m->frequency[k];
        }
    }
    while (d->range <= ARS_HALF) {
        d->range <<= 1U;
        d->code = (d->code << 1U) | ars_bit(d);
    }
    return m->first + index;
}

/* A little-endian bit string of @p count binary symbols. */
static uint32_t ars_bits(ars_decoder *d, ars_model *m, uint32_t count) {
    uint32_t value = 0U;
    uint32_t index;
    for (index = 0U; index < count; ++index)
        if (ars_symbol(d, m) != 0U) value |= UINT32_C(1) << index;
    return value;
}

/* Inverse BWT, un-randomisation and run-length expansion of one block. */
static bool ars_emit_block(const uint8_t *block, uint32_t *next,
                           uint32_t length, uint32_t primary, bool randomised,
                           sit5_sink *out) {
    uint32_t counts[256];
    uint32_t index;
    uint32_t position = primary;
    uint32_t sum = 0U;
    uint32_t random_index = 0U;
    uint32_t random_next = SIT5_ARSENIC_GAPS[0];
    uint32_t run = 0U;
    int last = -1;
    xx_rt_memset(counts, 0, sizeof(counts));
    for (index = 0U; index < length; ++index) counts[block[index]]++;
    for (index = 0U; index < 256U; ++index) {
        uint32_t count = counts[index];
        counts[index] = sum;
        sum += count;
    }
    for (index = 0U; index < length; ++index)
        next[counts[block[index]]++] = index;
    for (index = 0U; index < length; ++index) {
        uint8_t value;
        position = next[position];
        value = block[position];
        if (randomised && index == random_next) {
            value ^= 1U;
            random_index = (random_index + 1U) & 255U;
            random_next += SIT5_ARSENIC_GAPS[random_index];
        }
        if (run == 4U) {
            run = 0U;
            if (!sit5_sink_repeat(out, (uint8_t)last, value)) return false;
            continue;
        }
        if ((int)value == last) {
            ++run;
        } else {
            run = 1U;
            last = (int)value;
        }
        if (!sit5_sink_byte(out, value)) return false;
    }
    return true;
}

static bool ars_decode(sit5_input *input, sit5_sink *out) {
    ars_decoder *d;
    uint8_t *block = NULL;
    uint32_t *next = NULL;
    uint32_t block_bits;
    uint32_t capacity;
    uint32_t stored_crc = 0U;
    uint32_t index;
    uint64_t bound;
    bool have_crc = false;
    bool end;
    bool ok = false;

    d = (ars_decoder *)xx_mem_calloc(1U, sizeof(*d));
    if (!d) return false;
    d->input = input;
    d->range = ARS_ONE;
    for (index = 0U; index < ARS_CODE_BITS; ++index)
        d->code = (d->code << 1U) | ars_bit(d);
    ars_model_init(&d->initial, 0U, 1U, 1U, 256U);
    ars_model_init(&d->selector, 0U, 10U, 8U, 1024U);
    ars_model_init(&d->rank[0], 2U, 3U, 8U, 1024U);
    ars_model_init(&d->rank[1], 4U, 7U, 4U, 1024U);
    ars_model_init(&d->rank[2], 8U, 15U, 4U, 1024U);
    ars_model_init(&d->rank[3], 16U, 31U, 4U, 1024U);
    ars_model_init(&d->rank[4], 32U, 63U, 2U, 1024U);
    ars_model_init(&d->rank[5], 64U, 127U, 2U, 1024U);
    ars_model_init(&d->rank[6], 128U, 255U, 1U, 1024U);

    if (ars_bits(d, &d->initial, 8U) != 'A' ||
        ars_bits(d, &d->initial, 8U) != 's' || d->failed)
        goto done;
    block_bits = ars_bits(d, &d->initial, 4U) + ARS_MIN_BLOCK_BITS;
    /* A block holds run-length coded bytes: never more than 5/4 of what the
     * whole fork expands to, and never more than the declared block size. */
    bound = out->expected + out->expected / 4U + 4U;
    capacity = UINT32_C(1) << block_bits;
    if (bound < (uint64_t)capacity) capacity = (uint32_t)bound;
    end = ars_symbol(d, &d->initial) != 0U;
    if (d->failed) goto done;
    if (!end) {
        block = (uint8_t *)xx_mem_alloc(capacity);
        next = (uint32_t *)xx_mem_alloc((size_t)capacity * sizeof(uint32_t));
        if (!block || !next) goto done;
    }
    while (!end) {
        uint8_t mtf[256];
        uint32_t length = 0U;
        uint32_t primary;
        bool randomised;
        for (index = 0U; index < 256U; ++index) mtf[index] = (uint8_t)index;
        randomised = ars_symbol(d, &d->initial) != 0U;
        primary = ars_bits(d, &d->initial, block_bits);
        for (;;) {
            uint32_t selector = ars_symbol(d, &d->selector);
            uint32_t rank;
            uint8_t value;
            if (d->failed) goto done;
            if (selector < 2U) {
                uint64_t zeros = 0U;
                uint64_t weight = 1U;
                while (selector < 2U) {
                    zeros += selector == 0U ? weight : 2U * weight;
                    if (zeros > (uint64_t)(capacity - length)) goto done;
                    weight <<= 1U;
                    selector = ars_symbol(d, &d->selector);
                    if (d->failed) goto done;
                }
                xx_rt_memset(block + length, mtf[0], (size_t)zeros);
                length += (uint32_t)zeros;
            }
            if (selector == 10U) break;
            rank = selector == 2U ? 1U : ars_symbol(d, &d->rank[selector - 3U]);
            if (d->failed || length >= capacity || rank > 255U) goto done;
            value = mtf[rank];
            for (index = rank; index != 0U; --index)
                mtf[index] = mtf[index - 1U];
            mtf[0] = value;
            block[length++] = value;
        }
        ars_model_reset(&d->selector);
        for (index = 0U; index < 7U; ++index) ars_model_reset(&d->rank[index]);
        if (ars_symbol(d, &d->initial) != 0U) {
            stored_crc = ars_bits(d, &d->initial, 32U);
            have_crc = true;
            end = true;
        }
        if (d->failed || length == 0U || primary >= length ||
            !ars_emit_block(block, next, length, primary, randomised, out))
            goto done;
    }
    if (!sit5_sink_flush(out) || out->produced != out->expected) goto done;
    ok = have_crc ? stored_crc == out->crc32 : out->expected == 0U;
done:
    if (block) xx_mem_free(block);
    if (next) xx_mem_free(next);
    xx_mem_free(d);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Container.                                                         */
/* ------------------------------------------------------------------ */

typedef struct sit5_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    uint64_t packed_size;
    uint64_t unpacked_size;
    uint32_t mac_type;
    uint32_t modified;
    uint16_t finder_flags;
    uint16_t crc16;
    uint8_t method;
    bool encrypted;
    bool folder;
    bool resource;
} sit5_member;

/* Output paths already handed out, compared the way a case-insensitive
 * file system would (ASCII and the Latin-1 letters Mac Roman carries). */
typedef struct sit5_names_s {
    const char **slots;
    size_t mask;
    size_t used;
} sit5_names;

typedef struct sit5_stream_s {
    sit5_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    sit5_names names;
    int64_t archive_size;
    uint32_t root_entries;
    uint8_t archive_flags;
} sit5_stream;

/* Mac Roman 0x80..0xFF as Unicode. */
static const uint16_t SIT5_MAC_ROMAN[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

static size_t sit5_put_utf8(char *out, uint32_t code) {
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
    }
    if (code < 0x800U) {
        out[0] = (char)(0xC0U | (code >> 6U));
        out[1] = (char)(0x80U | (code & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code >> 12U));
    out[1] = (char)(0x80U | ((code >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (code & 0x3FU));
    return 3U;
}

/* Length of the well-formed UTF-8 sequence at @p s, or 0. */
static size_t sit5_utf8_sequence(const uint8_t *s, size_t available) {
    uint32_t code;
    size_t length;
    size_t index;
    if (available == 0U) return 0U;
    if (s[0] < 0x80U) return 1U;
    if (s[0] >= 0xC2U && s[0] <= 0xDFU) {
        length = 2U;
        code = s[0] & 0x1FU;
    } else if (s[0] >= 0xE0U && s[0] <= 0xEFU) {
        length = 3U;
        code = s[0] & 0x0FU;
    } else if (s[0] >= 0xF0U && s[0] <= 0xF4U) {
        length = 4U;
        code = s[0] & 0x07U;
    } else {
        return 0U;
    }
    if (length > available) return 0U;
    for (index = 1U; index < length; ++index) {
        if ((s[index] & 0xC0U) != 0x80U) return 0U;
        code = (code << 6U) | (s[index] & 0x3FU);
    }
    if ((length == 3U && code < 0x800U) || (length == 4U && code < 0x10000U) ||
        code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU))
        return 0U;
    return length;
}

static bool sit5_is_utf8(const uint8_t *s, size_t size) {
    size_t at = 0U;
    while (at < size) {
        size_t length = sit5_utf8_sequence(s + at, size - at);
        if (length == 0U) return false;
        at += length;
    }
    return true;
}

static char sit5_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool sit5_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || sit5_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* Windows device names, with or without an extension and in any case. */
static bool sit5_is_device_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length = xx_str_len(name);
    size_t stem = 0U;
    size_t index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (sit5_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((sit5_upper(name[0]) == 'C' && sit5_upper(name[1]) == 'O' &&
             sit5_upper(name[2]) == 'M') ||
            (sit5_upper(name[0]) == 'L' && sit5_upper(name[1]) == 'P' &&
             sit5_upper(name[2]) == 'T'));
}

/* One path component, as UTF-8 safe to create on any file system.  StuffIt
 * 5 names are UTF-8 or Mac Roman depending on the writer; well-formed UTF-8
 * is kept, anything else is read as Mac Roman.  '/' is a legal character
 * inside a Mac name (the tree comes from the folder entries), so it is
 * replaced like every other reserved character. */
static char *sit5_component(const uint8_t *bytes, size_t size) {
    char *name;
    size_t in;
    size_t out = 1U; /* room for a '_' prefix */
    bool utf8;
    if (size > SIT5_MAX_NAME) return NULL;
    utf8 = sit5_is_utf8(bytes, size);
    name = (char *)xx_mem_alloc(size * 3U + 3U);
    if (!name) return NULL;
    for (in = 0U; in < size; ++in) {
        uint8_t c = bytes[in];
        if (c >= 0x80U) {
            if (utf8) {
                name[out++] = (char)c;
            } else {
                out += sit5_put_utf8(name + out,
                                     SIT5_MAC_ROMAN[c - 0x80U]);
            }
        } else if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' ||
                   c == ':' || c == '*' || c == '?' || c == '"' ||
                   c == '<' || c == '>' || c == '|') {
            name[out++] = '_';
        } else {
            name[out++] = (char)c;
        }
    }
    /* Windows drops trailing dots and spaces, which would also turn ".."
     * into a parent reference. */
    while (out > 1U && (name[out - 1U] == ' ' || name[out - 1U] == '.'))
        --out;
    if (out == 1U) name[out++] = '_';
    name[out] = 0;
    if (sit5_is_device_name(name + 1U)) {
        name[0] = '_';
        return name;
    }
    xx_rt_memmove(name, name + 1U, out);
    return name;
}

/* Next code point of a UTF-8 path, folded to upper case the way a
 * case-insensitive file system compares it; 0 at the end. */
static uint32_t sit5_fold_next(const char **cursor) {
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t code = s[0];
    size_t used = 1U;
    if (code == 0U) return 0U;
    if ((code & 0xE0U) == 0xC0U && s[1] != 0U) {
        code = ((code & 0x1FU) << 6U) | (s[1] & 0x3FU);
        used = 2U;
    } else if ((code & 0xF0U) == 0xE0U && s[1] != 0U && s[2] != 0U) {
        code = ((code & 0x0FU) << 12U) | ((uint32_t)(s[1] & 0x3FU) << 6U) |
               (s[2] & 0x3FU);
        used = 3U;
    } else if ((code & 0xF8U) == 0xF0U && s[1] != 0U && s[2] != 0U &&
               s[3] != 0U) {
        code = ((code & 0x07U) << 18U) | ((uint32_t)(s[1] & 0x3FU) << 12U) |
               ((uint32_t)(s[2] & 0x3FU) << 6U) | (s[3] & 0x3FU);
        used = 4U;
    }
    *cursor += used;
    if (code >= 'a' && code <= 'z') return code - 0x20U;
    if (code >= 0xE0U && code <= 0xFEU && code != 0xF7U) return code - 0x20U;
    if (code == 0xFFU) return 0x178U;
    if (code == 0x153U) return 0x152U;
    if (code == '\\') return '/';
    return code;
}

static uint32_t sit5_fold_hash(const char *name) {
    uint32_t hash = UINT32_C(2166136261);
    uint32_t code;
    while ((code = sit5_fold_next(&name)) != 0U) {
        hash ^= code;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool sit5_fold_equal(const char *a, const char *b) {
    for (;;) {
        uint32_t x = sit5_fold_next(&a);
        uint32_t y = sit5_fold_next(&b);
        if (x != y) return false;
        if (x == 0U) return true;
    }
}

static bool sit5_names_contains(const sit5_names *names, const char *name) {
    size_t slot;
    if (!names->slots) return false;
    slot = (size_t)sit5_fold_hash(name) & names->mask;
    while (names->slots[slot]) {
        if (sit5_fold_equal(names->slots[slot], name)) return true;
        slot = (slot + 1U) & names->mask;
    }
    return false;
}

static bool sit5_names_insert(sit5_names *names, const char *name) {
    size_t slot;
    if (!names->slots || (names->used + 1U) * 2U > names->mask + 1U) {
        size_t size = names->slots ? (names->mask + 1U) * 2U : 64U;
        const char **grown;
        size_t index;
        if (size > SIZE_MAX / sizeof(*grown)) return false;
        grown = (const char **)xx_mem_calloc(size, sizeof(*grown));
        if (!grown) return false;
        for (index = 0U; names->slots && index <= names->mask; ++index) {
            const char *old = names->slots[index];
            if (old) {
                slot = (size_t)sit5_fold_hash(old) & (size - 1U);
                while (grown[slot]) slot = (slot + 1U) & (size - 1U);
                grown[slot] = old;
            }
        }
        if (names->slots) xx_mem_free((void *)names->slots);
        names->slots = grown;
        names->mask = size - 1U;
    }
    slot = (size_t)sit5_fold_hash(name) & names->mask;
    while (names->slots[slot]) slot = (slot + 1U) & names->mask;
    names->slots[slot] = name;
    names->used++;
    return true;
}

/* Take ownership of @p candidate and return a path no earlier record uses,
 * appending " (2)", " (3)", ... when needed. */
static char *sit5_unique_name(sit5_stream *stream, char *candidate) {
    uint32_t suffix;
    if (!candidate) return NULL;
    if (!sit5_names_contains(&stream->names, candidate)) return candidate;
    for (suffix = 2U; suffix < SIT5_MAX_SUFFIX; ++suffix) {
        char number[24];
        char *next;
        (void)xx_rt_snprintf(number, sizeof(number), " (%u)",
                             (unsigned)suffix);
        next = xx_str_concat(candidate, number);
        if (!next) break;
        if (!sit5_names_contains(&stream->names, next)) {
            xx_str_free(candidate);
            return next;
        }
        xx_str_free(next);
    }
    xx_str_free(candidate);
    return NULL;
}

static bool sit5_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == 0x7FU || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void sit5_stream_free(void *opaque) {
    sit5_stream *stream = (sit5_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    if (stream->names.slots) xx_mem_free((void *)stream->names.slots);
    xx_mem_free(stream);
}

/* Record @p member under a unique version of the path it carries (whose
 * ownership passes here); the final path is returned through @p final. */
static bool sit5_add_member(sit5_stream *stream, sit5_member *member,
                            const char **final) {
    char *name;
    if (stream->count >= SIT5_MAX_RECORDS) {
        xx_str_free(member->name);
        return false;
    }
    if (stream->count == stream->capacity) {
        size_t size = stream->capacity ? stream->capacity * 2U : 16U;
        sit5_member *grown;
        if (size > SIZE_MAX / sizeof(*grown)) {
            xx_str_free(member->name);
            return false;
        }
        grown = (sit5_member *)xx_mem_realloc(stream->items,
                                              size * sizeof(*grown));
        if (!grown) {
            xx_str_free(member->name);
            return false;
        }
        stream->items = grown;
        stream->capacity = size;
    }
    name = sit5_unique_name(stream, member->name);
    member->name = NULL;
    if (!name) return false;
    if (!sit5_names_insert(&stream->names, name)) {
        xx_str_free(name);
        return false;
    }
    member->name = name;
    stream->items[stream->count++] = *member;
    if (final) *final = name;
    return true;
}

static char *sit5_join(const char *prefix, const char *component,
                       const char *suffix) {
    char *joined;
    char *result;
    if (!component) return NULL;
    joined = (prefix && prefix[0]) ? xx_str_concat3(prefix, "/", component)
                                   : xx_str_dup(component);
    if (!joined || !suffix) return joined;
    result = xx_str_concat(joined, suffix);
    xx_str_free(joined);
    return result;
}

static bool sit5_parse(Abstractformat *format, sit5_stream **result) {
    uint8_t header[SIT5_HEADER_SIZE];
    uint8_t second[SIT5_SECOND_MAX];
    uint8_t *entry = NULL;
    sit5_stream *stream = NULL;
    char *path[SIT5_MAX_DEPTH + 1U];
    uint32_t remaining[SIT5_MAX_DEPTH + 1U];
    size_t depth = 0U;
    uint32_t entries = 0U;
    int64_t total;
    int64_t size;
    int64_t base;
    int64_t archive_size;
    int64_t cursor;
    uint32_t first;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < 0 || base > total) return false;
    size = total - base;
    if (size < (int64_t)SIT5_HEADER_SIZE ||
        !sit5_read_at(format->device, base, header, sizeof(header)) ||
        xx_rt_memcmp(header, "StuffIt (c)1997-", 16U) != 0 ||
        header[82] != SIT5_VERSION)
        return false;
    archive_size = (int64_t)sit5_be32(header + 84U);
    first = sit5_be32(header + 94U);
    if (archive_size > size || first < SIT5_HEADER_SIZE ||
        first > SIT5_MAX_PREAMBLE || (int64_t)first > archive_size)
        return false;

    /* The header CRC covers everything up to the first entry (the
     * reserved block, the comment and the password check value). */
    {
        uint8_t *preamble = (uint8_t *)xx_mem_alloc(first);
        bool match;
        if (!preamble) return false;
        match = sit5_read_at(format->device, base, preamble, first);
        if (match) {
            preamble[98] = preamble[99] = 0U;
            match = xx_crc16_arc_calc(0U, preamble, first) ==
                    sit5_be16(header + 98U);
        }
        xx_mem_free(preamble);
        if (!match) return false;
    }

    stream = (sit5_stream *)xx_mem_calloc(1U, sizeof(*stream));
    entry = (uint8_t *)xx_mem_alloc(SIT5_ENTRY_MAX + 1U);
    if (!stream || !entry) goto fail;
    stream->archive_size = archive_size;
    stream->root_entries = sit5_be16(header + 92U);
    stream->archive_flags = header[83];
    path[0] = NULL;
    remaining[0] = stream->root_entries;
    cursor = (int64_t)first;

    for (;;) {
        uint32_t header_size;
        uint32_t name_length;
        uint32_t name_offset;
        uint32_t second_size;
        uint32_t flags2;
        uint8_t version;
        uint8_t flags;
        uint16_t crc;
        int64_t data_start;
        char *component;
        char *full;
        sit5_member member;

        while (depth != 0U && remaining[depth] == 0U) {
            xx_str_free(path[depth]);
            path[depth--] = NULL;
        }
        if (depth == 0U && remaining[0] == 0U) break;
        if (++entries > SIT5_MAX_ENTRIES ||
            cursor > archive_size - (int64_t)SIT5_ENTRY_MIN ||
            !sit5_read_at(format->device, base + cursor, entry,
                          SIT5_ENTRY_MIN) ||
            sit5_be32(entry) != SIT5_ENTRY_MAGIC)
            goto fail;
        header_size = sit5_be16(entry + 6U);
        if (header_size < SIT5_ENTRY_MIN ||
            (int64_t)header_size > archive_size - cursor ||
            !sit5_read_at(format->device, base + cursor + SIT5_ENTRY_MIN,
                          entry + SIT5_ENTRY_MIN,
                          header_size - SIT5_ENTRY_MIN))
            goto fail;
        crc = xx_crc16_arc_calc(0U, entry, 32U);
        crc = xx_crc16_arc_calc(crc, "\0\0", 2U);
        crc = xx_crc16_arc_calc(crc, entry + 34U, header_size - 34U);
        if (crc != sit5_be16(entry + 32U)) goto fail;
        version = entry[4];
        flags = entry[9];
        name_length = sit5_be16(entry + 30U);

        /* A folder closes with a bare marker entry; it is not one of the
         * parent's counted children. */
        if ((flags & SIT5_FLAG_FOLDER) != 0U &&
            sit5_be32(entry + 34U) == SIT5_END_MARK) {
            cursor += (int64_t)header_size;
            continue;
        }

        name_offset = (flags & SIT5_FLAG_FOLDER) != 0U
                          ? SIT5_ENTRY_MIN
                          : SIT5_ENTRY_MIN + (uint32_t)entry[47];
        if (name_length > SIT5_MAX_NAME ||
            name_offset + name_length > header_size)
            goto fail;
        second_size = 14U + (version == 1U ? 22U : 18U);
        data_start = cursor + (int64_t)header_size;
        if ((int64_t)second_size > archive_size - data_start ||
            !sit5_read_at(format->device, base + data_start, second,
                          second_size))
            goto fail;
        flags2 = sit5_be16(second);
        data_start += (int64_t)second_size;

        component = sit5_component(entry + name_offset, name_length);
        if (!component) goto fail;
        full = sit5_join(depth != 0U ? path[depth] : NULL, component, NULL);
        xx_str_free(component);
        if (!full) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = base + cursor;
        member.modified = sit5_be32(entry + 14U);
        member.mac_type = sit5_be32(second + 4U);
        member.finder_flags = sit5_be16(second + 12U);
        member.encrypted = (flags & SIT5_FLAG_ENCRYPTED) != 0U;

        if ((flags & SIT5_FLAG_FOLDER) != 0U) {
            const char *final = NULL;
            if (depth >= SIT5_MAX_DEPTH) {
                xx_str_free(full);
                goto fail;
            }
            member.name = full;
            member.folder = true;
            member.data_offset = base + data_start;
            if (!sit5_add_member(stream, &member, &final)) goto fail;
            remaining[depth]--;
            path[depth + 1U] = xx_str_dup(final);
            if (!path[depth + 1U]) goto fail;
            ++depth;
            remaining[depth] = sit5_be16(entry + 46U);
            cursor = data_start;
            continue;
        }

        {
            uint32_t data_unpacked = sit5_be32(entry + 34U);
            uint32_t data_packed = sit5_be32(entry + 38U);
            uint32_t rsrc_unpacked = 0U;
            uint32_t rsrc_packed = 0U;
            uint16_t rsrc_crc = 0U;
            uint8_t rsrc_method = 0U;
            bool has_rsrc;
            if ((flags2 & 1U) != 0U) {
                if ((int64_t)14 > archive_size - data_start ||
                    !sit5_read_at(format->device, base + data_start,
                                  second + second_size, 14U)) {
                    xx_str_free(full);
                    goto fail;
                }
                rsrc_unpacked = sit5_be32(second + second_size);
                rsrc_packed = sit5_be32(second + second_size + 4U);
                rsrc_crc = sit5_be16(second + second_size + 8U);
                rsrc_method = second[second_size + 12U];
                data_start += 14 + (int64_t)second[second_size + 13U];
            }
            if (data_start > archive_size ||
                (int64_t)rsrc_packed > archive_size - data_start ||
                (int64_t)data_packed >
                    archive_size - data_start - (int64_t)rsrc_packed) {
                xx_str_free(full);
                goto fail;
            }
            has_rsrc = (flags2 & 1U) != 0U &&
                       (rsrc_unpacked != 0U || rsrc_packed != 0U);
            /* An empty data fork next to a resource fork is not listed,
             * matching what The Unarchiver extracts. */
            if (data_unpacked != 0U || data_packed != 0U || !has_rsrc) {
                member.name = xx_str_dup(full);
                member.data_offset = base + data_start + (int64_t)rsrc_packed;
                member.packed_size = data_packed;
                member.unpacked_size = data_unpacked;
                member.crc16 = sit5_be16(entry + 42U);
                member.method = entry[46];
                member.resource = false;
                if (!member.name || !sit5_add_member(stream, &member, NULL)) {
                    xx_str_free(full);
                    goto fail;
                }
            }
            if (has_rsrc) {
                member.name = sit5_join(NULL, full, ".rsrc");
                member.data_offset = base + data_start;
                member.packed_size = rsrc_packed;
                member.unpacked_size = rsrc_unpacked;
                member.crc16 = rsrc_crc;
                member.method = rsrc_method;
                member.resource = true;
                if (!member.name || !sit5_add_member(stream, &member, NULL)) {
                    xx_str_free(full);
                    goto fail;
                }
            }
            xx_str_free(full);
            remaining[depth]--;
            cursor = data_start + (int64_t)rsrc_packed + (int64_t)data_packed;
        }
    }

    xx_mem_free(entry);
    *result = stream;
    return true;
fail:
    while (depth != 0U) xx_str_free(path[depth--]);
    if (entry) xx_mem_free(entry);
    sit5_stream_free(stream);
    return false;
}

static bool sit5_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
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

static const xx_var *sit5_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sit5_set_record(xx_archive_record *record,
                            const sit5_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->resource ? -1 : member->header_offset;
    record->header_size = SIT5_ENTRY_MIN;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc16) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->modified) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->finder_flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->mac_type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

/* Decode one fork into @p destination (NULL only verifies). */
static bool sit5_decode_member(Abstractformat *format,
                               const sit5_member *member,
                               xx_io_device *destination) {
    sit5_input *in;
    sit5_sink *out;
    bool ok = false;
    if (!format || !member || member->folder || member->encrypted)
        return member && member->folder;
    if (member->method == SIT5_METHOD_STORE &&
        member->packed_size != member->unpacked_size)
        return false;
    in = (sit5_input *)xx_mem_calloc(1U, sizeof(*in));
    out = (sit5_sink *)xx_mem_calloc(1U, sizeof(*out));
    if (in && out) {
        in->device = format->device;
        in->offset = member->data_offset;
        in->remaining = member->packed_size;
        out->device = destination;
        out->expected = member->unpacked_size;
        switch (member->method) {
        case SIT5_METHOD_STORE: ok = sit5_store(in, out); break;
        case SIT5_METHOD_RLE90: ok = sit5_rle90(in, out); break;
        case SIT5_METHOD_LZHUFF: ok = sit13_decode(in, out); break;
        case SIT5_METHOD_ARSENIC: ok = ars_decode(in, out); break;
        default: ok = false; break;
        }
        ok = ok && sit5_sink_flush(out) && out->produced == out->expected;
        /* Arsenic carries its own CRC-32 (checked above); the others are
         * only as good as the header's CRC-16. */
        if (ok && member->method != SIT5_METHOD_ARSENIC)
            ok = out->crc16 == member->crc16;
    }
    if (in) xx_mem_free(in);
    if (out) xx_mem_free(out);
    return ok;
}

void xx_stuffit5_init(xx_stuffit5 *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_STUFFIT5_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stuffit");
    xx_format_set_extension(&archive->format, "sit");
    archive->format.check_is_valid = xx_stuffit5_check_is_valid;
    archive->format.handle_base_info = xx_stuffit5_handle_base_info;
    archive->format.get_format_size = xx_stuffit5_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_stuffit5_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_stuffit5_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_stuffit5_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_stuffit5_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_stuffit5_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_stuffit5_free_archive_records_reading;
    archive->archive_size = -1;
}

xx_stuffit5 *xx_stuffit5_create(xx_io_device *device, int64_t base_address) {
    xx_stuffit5 *archive = (xx_stuffit5 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_stuffit5_init(archive, device, base_address);
    return archive;
}

void xx_stuffit5_destroy(xx_stuffit5 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_stuffit5_free(xx_stuffit5 *archive) {
    if (!archive) return;
    xx_stuffit5_destroy(archive);
    xx_mem_free(archive);
}

bool xx_stuffit5_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    sit5_stream *stream;
    (void)pd;
    if (!sit5_parse(format, &stream)) return false;
    sit5_stream_free(stream);
    return true;
}

bool xx_stuffit5_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    sit5_stream *stream;
    xx_stuffit5 *archive;
    (void)pd;
    if (!format || !sit5_parse(format, &stream)) {
        if (format) format->is_valid = false;
        return false;
    }
    archive = (xx_stuffit5 *)format;
    archive->number_of_records = stream->count;
    archive->archive_size = stream->archive_size;
    archive->root_entries = stream->root_entries;
    archive->archive_flags = stream->archive_flags;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_STUFFIT5_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    sit5_stream_free(stream);
    return true;
}

int64_t xx_stuffit5_get_format_size(Abstractformat *format,
                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_stuffit5_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_stuffit5_get_number_of_archive_records(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_stuffit5_handle_base_info(format, pd))
               ? ((xx_stuffit5 *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_stuffit5_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    sit5_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!sit5_parse(format, &stream)) return NULL;
    if (stream->count == 0U) {
        sit5_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sit5_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sit5_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!sit5_copy_options(&state->options, options) ||
        !sit5_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_stuffit5_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_stuffit5_archive_record_move_to_next(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    sit5_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (sit5_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = sit5_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_stuffit5_unpack_current_archive_record(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    sit5_stream *stream;
    const sit5_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (sit5_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!sit5_safe_output_name(member->name)) return false;
    path_option = sit5_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return sit5_decode_member(format, member, NULL);
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
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (member->encrypted || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = sit5_decode_member(format, member, destination);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_stuffit5_free_archive_records_reading(Abstractformat *format,
                                              xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
