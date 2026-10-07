/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 *
 * Amiga XPK packed file ("XPKF"): one member, cut into checksummed chunks,
 * each packed by the sub-packer named in the stream header.  The field table
 * is in xx_xpk_compressed_file.h.
 *
 * The stream walk, the two checksums and the MASH decoder follow Deark 1.7.3
 * (src/fmtutil-cmpr.c: fmtutil_xpk_codectype1; src/fmtutil-lzh.c: XPK:MASH),
 * Copyright (C) 2016-2026 Jason Summers, MIT license. As in Deark, MASH
 * history does not reach across chunks. Additional native subpackers
 * uses the wire layouts documented by Ancient (Teemu Suutari):
 * https://github.com/temisu/ancient/blob/master/src/BZIP2Decompressor.cpp
 * https://github.com/temisu/ancient/blob/master/src/DEFLATEDecompressor.cpp
 * https://github.com/temisu/ancient/blob/master/src/RLENDecompressor.cpp
 * https://github.com/temisu/ancient/blob/master/src/CBR0Decompressor.cpp
 * https://github.com/temisu/ancient/blob/master/src/FRLEDecompressor.cpp
 * https://github.com/temisu/ancient/blob/master/src/DLTADecode.cpp
 * https://github.com/temisu/ancient/blob/master/src/FASTDecompressor.cpp
 * These additions reuse xxfclib's C codecs; no C++ dependency is introduced.
 * SQSH, BLZW and SMPL are adapted to bounded native C from Ancient's
 * SQSHDecompressor.cpp, BLZWDecompressor.cpp/LZWDecoder and SMPLDecompressor.cpp,
 * Copyright (c) 2017-2026 Teemu Suutari, BSD-2-Clause. See LICENSE.ancient.
 * The TDCS, FBR2 and SLZ3 subpackers are likewise adapted from Ancient's
 * corresponding decompressors under BSD-2-Clause.
 *
 * Differences from Deark, all on malformed input only: the output may not
 * grow past the header's unpacked length, a chunk header cut by the end of
 * the file is an error rather than zero-filled, and one chunk's packed data
 * is capped at 16 MiB.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xpk_compressed_file/xx_xpk_compressed_file.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xx_xpk_nuke_native.h"
#include "xx_xpk_lin_native.h"
#include "xx_xpk_lz_native.h"
#include "xx_xpk_zeno_native.h"
#include "xx_xpk_rake_native.h"
#include "xx_xpk_tdcs_native.h"
#include "xx_xpk_cyber_native.h"
#include "xx_xpk_yafa_native.h"
#include "xx_xpk_huffman_native.h"
#include "xx_xpk_acca_native.h"
#include "xx_xpk_artm_native.h"
#include "xx_xpk_pwpk_native.h"
#include "xx_xpk_lzcb_native.h"
#include "xx_xpk_crm_native.h"
#include "xx_xpk_cyb2_native.h"
#include "xx_xpk_impl_native.h"
#include "xx_xpk_lhlb_native.h"
#include "xx_xpk_lzx_native.h"
#include "xx_xpk_sdhc_native.h"
#include "xx_xpk_shrx_native.h"
#include "xx_xpk_sasc_native.h"

#include <limits.h>
#include <stdio.h>

#ifdef XPK_COMPRESSED_FILE
#define XX_XPK_COMPRESSED_FILE_FILE_TYPE XX_FILE_TYPE_XPK_COMPRESSED_FILE
#else
#define XX_XPK_COMPRESSED_FILE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XPK_HEADER_SIZE 36
#define XPK_FLAG_LONG 0x01U
#define XPK_FLAG_PASSWORD 0x02U
#define XPK_FLAG_EXTHEADER 0x04U
#define XPK_CHUNK_RAW 0x00U
#define XPK_CHUNK_PACKED 0x01U
#define XPK_CHUNK_END 0x0fU
#define XPK_CODE_MASH UINT32_C(0x4d415348)
#define XPK_CODE_NONE UINT32_C(0x4e4f4e45)
#define XPK_CODE_BZP2 UINT32_C(0x425a5032)
#define XPK_CODE_GZIP UINT32_C(0x475a4950)
#define XPK_CODE_RLEN UINT32_C(0x524c454e)
#define XPK_CODE_CBR0 UINT32_C(0x43425230)
#define XPK_CODE_CBR1 UINT32_C(0x43425231)
#define XPK_CODE_FRLE UINT32_C(0x46524c45)
#define XPK_CODE_DLTA UINT32_C(0x444c5441)
#define XPK_CODE_FAST UINT32_C(0x46415354)
#define XPK_CODE_SQSH UINT32_C(0x53515348)
#define XPK_CODE_BLZW UINT32_C(0x424c5a57)
#define XPK_CODE_SMPL UINT32_C(0x534d504c)
#define XPK_CODE_NUKE UINT32_C(0x4e554b45)
#define XPK_CODE_DUKE UINT32_C(0x44554b45)
#define XPK_CODE_LIN1 UINT32_C(0x4c494e31)
#define XPK_CODE_LIN2 UINT32_C(0x4c494e32)
#define XPK_CODE_LIN3 UINT32_C(0x4c494e33)
#define XPK_CODE_LIN4 UINT32_C(0x4c494e34)
#define XPK_CODE_RDCN UINT32_C(0x5244434e)
#define XPK_CODE_ILZR UINT32_C(0x494c5a52)
#define XPK_CODE_ZENO UINT32_C(0x5a454e4f)
#define XPK_CODE_RAKE UINT32_C(0x52414b45)
#define XPK_CODE_FRHT UINT32_C(0x46524854)
#define XPK_CODE_TDCS UINT32_C(0x54444353)
#define XPK_CODE_FBR2 UINT32_C(0x46425232)
#define XPK_CODE_SLZ3 UINT32_C(0x534c5a33)
#define XPK_CODE_LZW2 UINT32_C(0x4c5a5732)
#define XPK_CODE_LZW3 UINT32_C(0x4c5a5733)
#define XPK_CODE_LZW4 UINT32_C(0x4c5a5734)
#define XPK_CODE_LZW5 UINT32_C(0x4c5a5735)
#define XPK_CODE_LZBS UINT32_C(0x4c5a4253)
#define XPK_CODE_HUFF UINT32_C(0x48554646)
#define XPK_CODE_HFMN UINT32_C(0x48464d4e)
#define XPK_CODE_ACCA UINT32_C(0x41434341)
#define XPK_CODE_ARTM UINT32_C(0x4152544d)
#define XPK_CODE_PWPK UINT32_C(0x5057504b)
#define XPK_CODE_LZCB UINT32_C(0x4c5a4342)
#define XPK_CODE_CRM2 UINT32_C(0x43524d32)
#define XPK_CODE_CRMS UINT32_C(0x43524d53)
#define XPK_CODE_CYB2 UINT32_C(0x43594232)
#define XPK_CODE_IMPL UINT32_C(0x494d504c)
#define XPK_CODE_LHLB UINT32_C(0x4c484c42)
#define XPK_CODE_ELZX UINT32_C(0x454c5a58)
#define XPK_CODE_SLZX UINT32_C(0x534c5a58)
#define XPK_CODE_SDHC UINT32_C(0x53444843)
#define XPK_CODE_SHR3 UINT32_C(0x53485233)
#define XPK_CODE_SHRI UINT32_C(0x53485249)
#define XPK_CODE_SASC UINT32_C(0x53415343)
/* Largest packed chunk the extractor loads (short headers stop at 64 KiB;
 * xpkmaster's long-header chunks are far below this). */
#define XPK_MAX_CHUNK ((int64_t)16 * 1024 * 1024)
#define XPK_MASH_WINDOW 32768U
#define XPK_OUT_STAGE 65536U
#define XPK_PAYLOAD_NAME "payload"

typedef struct xpk_context_s {
    int64_t base;         /**< Device offset of 'XPKF'. */
    int64_t avail;        /**< Bytes from base to the device end. */
    int64_t stream_start; /**< First chunk (relative, before alignment). */
    int64_t end;          /**< Format size: end of the END chunk, or avail. */
    uint32_t packed_len;
    uint32_t method;
    uint32_t unpacked_len;
    uint32_t chunk_count;
    uint8_t flags;
    uint8_t reference[16];
    bool has_end;
} xpk_context;

typedef struct xpk_chunk_s {
    uint8_t type;
    uint16_t data_check;
    int64_t header_pos;
    int64_t data_pos; /**< Relative to base. */
    int64_t clen;
    int64_t ulen;
} xpk_chunk;

typedef struct xpk_stream_s {
    xpk_context context;
    size_t index;
    size_t count;
} xpk_stream;

static bool xpk_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        (pd && xx_pd_is_stopped(pd)) ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t take = size - done;
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (take > 65536U) take = 65536U;
        amount = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (amount <= 0 || (size_t)amount > take) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xpk_parse_header(Abstractformat *format, xpk_context *ctx, xx_pd_struct *pd) {
    uint8_t header[XPK_HEADER_SIZE + 2];
    uint8_t check = 0U;
    int64_t total;
    size_t index;
    if (!format || !format->device || !ctx || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address ||
        total - format->base_address < XPK_HEADER_SIZE + 8)
        return false;
    xx_mem_zero(ctx, sizeof(*ctx));
    ctx->base = format->base_address;
    ctx->avail = total - format->base_address;
    if (!xpk_read_at(format->device, ctx->base, header, sizeof(header), pd))
        return false;
    if (header[0] != 'X' || header[1] != 'P' || header[2] != 'K' ||
        header[3] != 'F')
        return false;
    for (index = 0U; index < XPK_HEADER_SIZE; ++index) check ^= header[index];
    if (check != 0U) return false;
    ctx->packed_len = xx_data_get_u32(header + 4, 4, 0, true);
    if ((uint64_t)ctx->packed_len + 8U > (uint64_t)ctx->avail ||
        ctx->packed_len < XPK_HEADER_SIZE) return false;
    ctx->avail = (int64_t)ctx->packed_len + 8;
    ctx->method = xx_data_get_u32(header + 8, 4, 0, true);
    ctx->unpacked_len = xx_data_get_u32(header + 12, 4, 0, true);
    xx_rt_memcpy(ctx->reference, header + 16, sizeof(ctx->reference));
    ctx->flags = header[32];
    ctx->stream_start = XPK_HEADER_SIZE;
    if (ctx->flags & XPK_FLAG_EXTHEADER) {
        int64_t extra = (int64_t)xx_data_get_u16(header + XPK_HEADER_SIZE, 2, 0, true);
        ctx->stream_start = XPK_HEADER_SIZE + 2 + extra;
        if (ctx->stream_start > ctx->avail) return false;
    }
    return true;
}

/* Read the chunk header at the 4-aligned position at or after *pos.
 * 1: a chunk (END included), 0: fewer than 8 bytes left (Deark stops there
 * without error), -1: malformed. */
static int xpk_read_chunk(xx_io_device *device, const xpk_context *ctx,
                          int64_t *pos, xpk_chunk *chunk, xx_pd_struct *pd) {
    uint8_t header[12];
    uint8_t check = 0U;
    size_t header_size = (ctx->flags & XPK_FLAG_LONG) ? 12U : 8U;
    size_t index;
    int64_t at = *pos;
    if (at < 0 || at > ctx->avail) return -1;
    if (at & 3) at += 4 - (at & 3);
    if (at > ctx->avail - 8) return 0;
    if (at > ctx->avail - (int64_t)header_size) return -1;
    if (!xpk_read_at(device, ctx->base + at, header, header_size, pd)) return -1;
    for (index = 0U; index < header_size; ++index) check ^= header[index];
    if (check != 0U) return -1;
    xx_mem_zero(chunk, sizeof(*chunk));
    chunk->type = header[0];
    chunk->data_check = xx_data_get_u16(header + 2, 2, 0, true);
    chunk->header_pos = at;
    if (header_size == 12U) {
        chunk->clen = (int64_t)xx_data_get_u32(header + 4, 4, 0, true);
        chunk->ulen = (int64_t)xx_data_get_u32(header + 8, 4, 0, true);
    } else {
        chunk->clen = (int64_t)xx_data_get_u16(header + 4, 2, 0, true);
        chunk->ulen = (int64_t)xx_data_get_u16(header + 6, 2, 0, true);
    }
    chunk->data_pos = at + (int64_t)header_size;
    if (chunk->clen > ctx->avail - chunk->data_pos) return -1;
    if (chunk->type == XPK_CHUNK_END) {
        *pos = chunk->data_pos + chunk->clen;
        return 1;
    }
    if (chunk->type != XPK_CHUNK_RAW && chunk->type != XPK_CHUNK_PACKED)
        return -1;
    if (chunk->clen > ctx->avail - chunk->data_pos) return -1;
    *pos = chunk->data_pos + chunk->clen;
    return 1;
}

/* Full structural walk: every chunk header, its checksum and bounds. */
static bool xpk_walk(Abstractformat *format, xpk_context *ctx,
                     bool first_only, xx_pd_struct *pd) {
    int64_t pos = ctx->stream_start;
    uint32_t chunks = 0U;
    bool any = false;
    for (;;) {
        xpk_chunk chunk;
        int step;
        if (pd && xx_pd_is_stopped(pd)) return false;
        step = xpk_read_chunk(format->device, ctx, &pos, &chunk, pd);
        if (step < 0) return false;
        if (step == 0) break;
        any = true;
        if (chunk.type == XPK_CHUNK_END) {
            ctx->has_end = true;
            ctx->end = pos;
            if (ctx->avail - pos > 3) return false;
            if (ctx->avail > pos) {
                uint8_t padding[3];
                size_t i, count = (size_t)(ctx->avail - pos);
                if (!xpk_read_at(format->device, ctx->base + pos, padding, count, pd))
                    return false;
                for (i = 0U; i < count; ++i) if (padding[i]) return false;
                ctx->end = ctx->avail;
            }
            break;
        }
        if (chunks == UINT32_MAX) return false;
        ++chunks;
        if (first_only) break;
    }
    if (!any || (!first_only && !ctx->has_end)) return false;
    ctx->chunk_count = chunks;
    if (!ctx->has_end) ctx->end = ctx->avail;
    return true;
}

static bool xpk_parse(Abstractformat *format, xpk_context *ctx, bool full,
                      xx_pd_struct *pd) {
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    bool valid = xpk_parse_header(format, ctx, pd) && xpk_walk(format, ctx, !full, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, SEEK_SET)) valid = false;
    return valid;
}

/* ---- MASH (ported from Deark, see the file header) ---- */

typedef struct xpk_sink_s {
    xx_io_device *device; /**< NULL: count only. */
    uint8_t *stage;
    size_t staged;
    bool failed;
    uint8_t prefix[16];
    size_t prefix_size;
    xx_pd_struct *pd;
} xpk_sink;

static bool xpk_sink_flush(xpk_sink *sink) {
    size_t done = 0U;
    if (sink->failed || (sink->pd && xx_pd_is_stopped(sink->pd))) return false;
    if (sink->device) {
        while (done < sink->staged) {
            ssize_t amount;
            if (sink->pd && xx_pd_is_stopped(sink->pd)) return false;
            amount = xx_io_write(sink->device, sink->stage + done, sink->staged - done);
            if (amount <= 0 || (size_t)amount > sink->staged - done) {
                sink->failed = true;
                return false;
            }
            done += (size_t)amount;
        }
    }
    sink->staged = 0U;
    return true;
}

static bool xpk_sink_write(xpk_sink *sink, const uint8_t *data, size_t size) {
    size_t first = sizeof(sink->prefix) - sink->prefix_size;
    if (sink->pd && xx_pd_is_stopped(sink->pd)) return false;
    if (first > size) first = size;
    if (first) {
        xx_rt_memcpy(sink->prefix + sink->prefix_size, data, first);
        sink->prefix_size += first;
    }
    while (size) {
        size_t room = XPK_OUT_STAGE - sink->staged;
        size_t take = size < room ? size : room;
        xx_rt_memcpy(sink->stage + sink->staged, data, take);
        sink->staged += take;
        data += take;
        size -= take;
        if (sink->staged == XPK_OUT_STAGE && !xpk_sink_flush(sink))
            return false;
    }
    return true;
}

typedef struct xpk_mash_s {
    const uint8_t *in;
    size_t pos;
    size_t len;
    uint32_t bitbuf;
    unsigned nbits;
    bool err;
    uint8_t *ring;
    uint32_t ring_pos;
    uint64_t produced;
    uint64_t wanted;
    xpk_sink *sink;
} xpk_mash;

static unsigned xpk_mash_bit(xpk_mash *m) {
    if (m->err) return 0U;
    if (m->nbits == 0U) {
        if (m->pos >= m->len) {
            m->err = true;
            return 0U;
        }
        m->bitbuf = m->in[m->pos++];
        m->nbits = 8U;
    }
    --m->nbits;
    return (m->bitbuf >> m->nbits) & 1U;
}

static uint32_t xpk_mash_bits(xpk_mash *m, unsigned count) {
    uint32_t value = 0U;
    unsigned index;
    for (index = 0U; index < count; ++index)
        value = (value << 1U) | xpk_mash_bit(m);
    return value;
}

static unsigned xpk_mash_ones(xpk_mash *m, unsigned limit) {
    unsigned count = 0U;
    while (count < limit && xpk_mash_bit(m)) ++count;
    return count;
}

static uint32_t xpk_mash_offset(xpk_mash *m) {
    static const uint8_t extra[8] = {5, 7, 9, 10, 11, 12, 13, 14};
    uint32_t code = xpk_mash_bits(m, 3U);
    uint32_t offset = xpk_mash_bits(m, extra[code]);
    if (code == 1U) offset += 32U;
    else if (code >= 2U) offset += (UINT32_C(1) << extra[code]) - 352U;
    return offset;
}

static void xpk_mash_emit(xpk_mash *m, uint8_t value) {
    m->ring[m->ring_pos & (XPK_MASH_WINDOW - 1U)] = value;
    ++m->ring_pos;
    /* Bytes past the chunk's declared length are dropped, as in Deark. */
    if (m->produced < m->wanted) {
        if (!xpk_sink_write(m->sink, &value, 1U)) m->err = true;
        ++m->produced;
    }
}

static bool xpk_mash_decode(xpk_mash *m) {
    while (!m->err && m->produced < m->wanted) {
        unsigned ones;
        uint32_t literals = 0U, length, offset, index;
        ones = xpk_mash_ones(m, 6U);
        if (ones <= 5U) {
            literals = ones;
        } else {
            ones = xpk_mash_ones(m, 16U);
            if (ones >= 16U) return false;
            literals = xpk_mash_bits(m, ones + 1U) + (UINT32_C(2) << ones) + 4U;
        }
        if (m->err) return false;
        for (index = 0U; index < literals; ++index) {
            if (m->pos >= m->len) return false;
            xpk_mash_emit(m, m->in[m->pos++]);
            if (m->err) return false;
        }
        if (m->produced >= m->wanted) break;
        if (xpk_mash_bit(m) == 0U) {
            if (xpk_mash_bit(m) == 0U) {
                length = 2U;
                offset = xpk_mash_bits(m, 9U);
            } else {
                length = 3U;
                offset = xpk_mash_offset(m);
            }
        } else {
            ones = xpk_mash_ones(m, 15U);
            if (ones >= 15U) return false;
            length = xpk_mash_bits(m, ones + 1U) + (UINT32_C(2) << ones) + 2U;
            offset = xpk_mash_offset(m);
        }
        if (m->err) return false;
        if (offset == 0U) break; /* Deark stops here; the length check fails */
        if ((uint64_t)offset > m->produced) return false;
        {
            uint32_t from = m->ring_pos - offset;
            for (index = 0U; index < length; ++index) {
                xpk_mash_emit(m, m->ring[from & (XPK_MASH_WINDOW - 1U)]);
                ++from;
                if (m->err) return false;
            }
        }
    }
    return !m->err && m->produced == m->wanted;
}

/* ---- Extraction ---- */

/* Each RLE family assigns a different length to the same control byte.
 * The top bit selects repeat/literal for all three; CBR0 and CBR1 share
 * their wire format. Refuse missing bytes, overrun and trailing commands;
 * RLEN producers may append one zero terminator after the full output. */
static bool xpk_rle(xpk_sink *sink, const uint8_t *packed, size_t size,
                    size_t wanted, uint32_t method, xx_pd_struct *pd) {
    size_t pos = 0U, produced = 0U;
    uint8_t repeat[129];
    while (produced < wanted) {
        unsigned code;
        size_t count;
        if ((pd && xx_pd_is_stopped(pd)) || pos >= size) return false;
        code = packed[pos++];
        if (method == XPK_CODE_RLEN)
            count = code < 128U ? code : 256U - code;
        else if (method == XPK_CODE_FRLE)
            count = (32U - (code & 31U)) + (code & 96U) + (code >= 128U);
        else
            count = code < 128U ? code + 1U : 257U - code;
        if (!count || count > wanted - produced) return false;
        if (code < 128U) {
            if (count > size - pos || !xpk_sink_write(sink, packed + pos, count))
                return false;
            pos += count;
        } else {
            if (pos >= size) return false;
            xx_rt_memset(repeat, packed[pos++], count);
            if (!xpk_sink_write(sink, repeat, count)) return false;
        }
        produced += count;
    }
    return pos == size || (method == XPK_CODE_RLEN && size - pos == 1U &&
                           packed[pos] == 0U);
}

/* Despite its 4CC, XPK-GZIP stores raw Deflate or an RFC1950 zlib frame.
 * Reject preset dictionaries, which require separately supplied history.
 */
static bool xpk_deflate(const uint8_t *packed, size_t size, uint8_t *output,
                        size_t wanted, xx_pd_struct *pd) {
    bool wrapped = size >= 2U && (packed[0] & 15U) == 8U &&
                   (packed[0] >> 4U) <= 7U &&
                   ((((unsigned)packed[0] << 8U) | packed[1]) % 31U) == 0U;
    size_t start = wrapped ? 2U : 0U, consumed = 0U;
    size_t compressed;
    xx_io_device *device;
    bool valid;
    if (wrapped && (size < 6U || (packed[1] & 32U))) return false;
    compressed = size - start - (wrapped ? 4U : 0U);
    device = xx_io_mem_open(output, wanted);
    if (!device) return false;
    valid = xx_deflate_unpack_memory_to_device_ex(packed + start, compressed,
                device, &consumed, false, pd) &&
            consumed == compressed && xx_io_tell(device) == (int64_t)wanted;
    if (xx_io_close(device)) valid = false;
    if (valid && wrapped) {
        uint32_t a = 1U, b = 0U;
        size_t i;
        for (i = 0U; i < wanted; ++i) {
            if ((i & 65535U) == 0U && pd && xx_pd_is_stopped(pd)) return false;
            a = (a + output[i]) % 65521U;
            b = (b + a) % 65521U;
        }
        valid = ((b << 16U) | a) == xx_data_get_u32(packed + size - 4U, 4, 0, true);
    }
    return valid;
}

/* FAST reads literal bytes from the front and 16-bit control/match words
 * from the back. New control words contain 16 MSB-first flags. Both input
 * cursors share a strict boundary; a match may overlap its own history.
 * The final match is intentionally clipped to the declared output length. */
static bool xpk_fast(const uint8_t *packed, size_t size, uint8_t *output,
                     size_t wanted, xx_pd_struct *pd) {
    size_t front = 0U, back = size, produced = 0U;
    uint16_t flags = 0U;
    unsigned available_bits = 0U;
    while (produced < wanted) {
        bool match;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!available_bits) {
            if (back - front < 2U) return false;
            back -= 2U;
            flags = xx_data_get_u16(packed + back, 2, 0, true);
            available_bits = 16U;
        }
        match = (flags & 0x8000U) != 0U;
        flags = (uint16_t)(flags << 1U);
        --available_bits;
        if (!match) {
            if (front >= back) return false;
            output[produced++] = packed[front++];
        } else {
            uint16_t code;
            size_t distance, count;
            if (back - front < 2U) return false;
            back -= 2U;
            code = xx_data_get_u16(packed + back, 2, 0, true);
            distance = code >> 4U;
            count = 18U - (code & 15U);
            if (!distance || distance > produced) return false;
            if (count > wanted - produced) count = wanted - produced;
            while (count--) {
                output[produced] = output[produced - distance];
                ++produced;
            }
        }
    }
    /* Original producers can retain unused commands/padding between the
     * streams. FAST terminates by output length, without an input EOS code;
     * the enclosing XPK checksum covers every byte including that slack. */
    return true;
}

typedef struct xpk_sqsh_bits_s {
    const uint8_t *data;
    size_t size, pos;
    unsigned remaining;
    uint8_t byte;
    bool failed;
} xpk_sqsh_bits;
static uint32_t xpk_sqsh_read(xpk_sqsh_bits *stream, unsigned count) {
    uint32_t value = 0U;
    while (count--) {
        if (!stream->remaining) {
            if (stream->pos == stream->size) { stream->failed = true; return 0U; }
            stream->byte = stream->data[stream->pos++]; stream->remaining = 8U;
        }
        value = (value << 1U) | ((stream->byte >> --stream->remaining) & 1U);
    }
    return value;
}
/* SQSH predicts byte samples using adaptive signed deltas and copies.
 * Its packed chunk starts with a BE16 output size and the first sample.
 * Prefix codes choose the delta width, copy length and copy distance.
 * Like the original producer, the final run is clipped to the output size. */
static bool xpk_sqsh(const uint8_t *packed, size_t size, uint8_t *output,
                     size_t wanted, xx_pd_struct *pd) {
    static const uint8_t widths[7][7] = {
        {2,3,4,5,6,7,8}, {3,2,4,5,6,7,8}, {4,3,5,2,6,7,8},
        {5,4,6,2,3,7,8}, {6,5,7,2,3,4,8}, {7,6,8,2,3,4,5},
        {8,7,6,2,3,4,5}
    };
    static const unsigned length_bits[5] = {1,1,1,3,5};
    static const uint32_t length_base[5] = {2,4,6,8,16};
    static const unsigned distance_bits[3] = {8,12,14};
    static const uint32_t distance_base[3] = {1,257,4353};
    xpk_sqsh_bits input;
    uint32_t streak = 0U, weight = 0U;
    unsigned previous = 0U;
    size_t produced = 1U;
    uint8_t sample;
    if (size < 3U || !wanted || wanted > 65535U || xx_data_get_u16(packed, 2, 0, true) != wanted)
        return false;
    xx_mem_zero(&input, sizeof(input)); input.data = packed; input.size = size; input.pos = 3U;
    sample = packed[2]; output[0] = sample;
    while (produced < wanted) {
        unsigned bits = 0U;
        uint32_t count = 0U;
        bool repeat = false;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (streak < 8U) {
            repeat = xpk_sqsh_read(&input, 1U) != 0U;
            if (!repeat) { bits = 8U; count = 1U; }
        } else {
            unsigned rank = 0U;
            if (xpk_sqsh_read(&input, 1U)) bits = previous;
            else if (!xpk_sqsh_read(&input, 1U)) repeat = true;
            else if (!xpk_sqsh_read(&input, 1U)) rank = 2U;
            else if (!xpk_sqsh_read(&input, 1U)) rank = 3U;
            else rank = (unsigned)xpk_sqsh_read(&input, 2U) + 4U;
            if (rank) {
                if (previous < 2U || previous > 8U || rank > 7U) return false;
                bits = widths[previous - 2U][rank - 1U];
            }
            if (!repeat) {
                if (bits < 2U || bits > 8U) return false;
                if (bits == 8U) {
                    count = weight < 20U ? 1U : 2U;
                    if (weight >= 20U) weight += 8U;
                } else { count = 5U; weight += 8U; }
            }
        }
        if (repeat) {
            unsigned length_index = 0U, distance_index;
            uint32_t distance;
            while (length_index < 4U && xpk_sqsh_read(&input, 1U)) ++length_index;
            count = xpk_sqsh_read(&input, length_bits[length_index]) + length_base[length_index];
            if (count >= 3U && streak) --streak;
            if (count > 3U && streak) --streak;
            distance_index = xpk_sqsh_read(&input, 1U) ? 1U :
                             (xpk_sqsh_read(&input, 1U) ? 2U : 0U);
            distance = xpk_sqsh_read(&input, distance_bits[distance_index]) + distance_base[distance_index];
            if (input.failed || distance > produced) return false;
            if (count > wanted - produced) count = (uint32_t)(wanted - produced);
            while (count--) { output[produced] = output[produced - distance]; ++produced; }
            sample = output[produced - 1U];
        } else {
            if (input.failed) return false;
            if (count > wanted - produced) count = (uint32_t)(wanted - produced);
            while (count--) {
                uint32_t value = xpk_sqsh_read(&input, bits);
                int32_t delta = (int32_t)value;
                if (input.failed) return false;
                if (value & (1U << (bits - 1U))) delta -= (int32_t)(1U << bits);
                sample = (uint8_t)((int32_t)sample - delta); output[produced++] = sample;
            }
            if (streak < 31U) ++streak;
            previous = bits;
        }
        weight -= weight >> 3U;
    }
    return !input.failed;
}

/* BLZW uses explicit reset/width control codes, an MSB stream, and a bounded
 * prefix/suffix dictionary. Only the next free code is the KwKwK case. */
static bool xpk_blzw(const uint8_t *packed, size_t size, uint8_t *output,
                     size_t wanted, xx_pd_struct *pd) {
    xpk_sqsh_bits input;
    uint32_t *prefix = NULL;
    uint8_t *suffix = NULL, *stack = NULL;
    uint32_t capacity, next = 259U, previous, bits = 9U;
    size_t stack_size, produced = 0U;
    unsigned max_bits;
    bool valid = false;
    if (size < 6U || !wanted || (pd && xx_pd_is_stopped(pd))) return false;
    max_bits = xx_data_get_u16(packed, 2, 0, true);
    if (max_bits < 9U || max_bits > 20U) return false;
    capacity = UINT32_C(1) << max_bits;
    stack_size = (size_t)xx_data_get_u16(packed + 2U, 2, 0, true) + 5U;
    prefix = (uint32_t *)xx_mem_alloc((size_t)capacity * sizeof(*prefix));
    suffix = (uint8_t *)xx_mem_alloc(capacity);
    stack = (uint8_t *)xx_mem_alloc(stack_size);
    if (!prefix || !suffix || !stack) goto done;
    xx_mem_zero(&input, sizeof(input));
    input.data = packed + 4U; input.size = size - 4U;
    previous = xpk_sqsh_read(&input, bits);
    if (input.failed || previous >= 256U) goto done;
    output[produced++] = (uint8_t)previous;
    while (produced < wanted) {
        uint32_t code, cursor;
        size_t length = 0U;
        bool special;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        code = xpk_sqsh_read(&input, bits);
        if (input.failed || code == 256U) goto done;
        if (code == 257U) {
            bits = 9U; next = 259U;
            previous = xpk_sqsh_read(&input, bits);
            if (input.failed || previous >= 256U) goto done;
            output[produced++] = (uint8_t)previous;
            continue;
        }
        if (code == 258U) {
            if (bits >= 24U) goto done;
            ++bits; continue;
        }
        if (code > next || code >= capacity) goto done;
        special = code == next;
        cursor = special ? previous : code;
        while (cursor >= 259U) {
            if (cursor >= next || length + 1U >= stack_size) goto done;
            stack[length++] = suffix[cursor];
            /* Earlier prefixes also exclude cycles on malformed code streams. */
            if (prefix[cursor] >= cursor) goto done;
            cursor = prefix[cursor];
            if ((length & 255U) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        }
        if (cursor >= 256U || length >= stack_size ||
            length + 1U + (special ? 1U : 0U) > wanted - produced) goto done;
        stack[length++] = (uint8_t)cursor;
        while (length) output[produced++] = stack[--length];
        if (special) output[produced++] = (uint8_t)cursor;
        if (next < capacity) {
            prefix[next] = previous; suffix[next] = (uint8_t)cursor; ++next;
        }
        previous = code;
    }
    valid = !input.failed && !(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(stack); xx_mem_free(suffix); xx_mem_free(prefix); return valid;
}

typedef struct xpk_smpl_node_s {
    uint16_t child[2];
    int16_t symbol;
} xpk_smpl_node;
/* SMPL transmits explicit prefix codes (up to30 bits) for byte deltas. The
 * finite trie rejects duplicate/prefix collisions and missing decode paths. */
static bool xpk_smpl(const uint8_t *packed, size_t size, uint8_t *output,
                     size_t wanted, xx_pd_struct *pd) {
    const unsigned max_nodes = 1U + 256U * 30U;
    xpk_smpl_node *nodes;
    xpk_sqsh_bits input;
    unsigned count = 1U, symbols = 0U, i;
    uint8_t accumulated = 0U;
    size_t produced;
    bool valid = false;
    if (size < 130U || xx_data_get_u16(packed, 2, 0, true) != 1U ||
        (pd && xx_pd_is_stopped(pd))) return false;
    nodes = (xpk_smpl_node *)xx_mem_alloc((size_t)max_nodes * sizeof(*nodes));
    if (!nodes) return false;
    xx_mem_zero(nodes, (size_t)max_nodes * sizeof(*nodes)); nodes[0].symbol = -1;
    xx_mem_zero(&input, sizeof(input)); input.data = packed + 2U; input.size = size - 2U;
    for (i = 0U; i < 256U; ++i) {
        unsigned length = (unsigned)xpk_sqsh_read(&input, 4U), node = 0U, bit;
        uint32_t code;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!length) continue;
        if (length == 15U) length += (unsigned)xpk_sqsh_read(&input, 4U);
        code = xpk_sqsh_read(&input, length);
        if (input.failed) goto done;
        for (bit = length; bit; --bit) {
            unsigned side = (unsigned)((code >> (bit - 1U)) & 1U);
            if (nodes[node].symbol >= 0) goto done;
            if (!nodes[node].child[side]) {
                if (count >= max_nodes) goto done;
                nodes[node].child[side] = (uint16_t)count;
                nodes[count++].symbol = -1;
            }
            node = nodes[node].child[side];
        }
        if (nodes[node].symbol >= 0 || nodes[node].child[0] || nodes[node].child[1]) goto done;
        nodes[node].symbol = (int16_t)i; ++symbols;
    }
    if (input.failed || (!symbols && wanted)) goto done;
    for (produced = 0U; produced < wanted; ++produced) {
        unsigned node = 0U;
        if ((produced & 4095U) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        while (nodes[node].symbol < 0) {
            node = nodes[node].child[xpk_sqsh_read(&input, 1U)];
            if (input.failed || !node) goto done;
        }
        accumulated = (uint8_t)(accumulated + nodes[node].symbol);
        output[produced] = accumulated;
    }
    valid = !input.failed && !(pd && xx_pd_is_stopped(pd));
done:
    xx_mem_free(nodes); return valid;
}

static bool xpk_sdhc_child_decode(const uint8_t *packed, size_t size,
                                  uint8_t *output, size_t wanted,
                                  void *opaque, xx_pd_struct *pd);
/* Stateful SHRI can refer to prior XPK chunks, at most 65,535 bytes back. */
static void xpk_history_push(uint8_t *history, size_t *used,
                             const uint8_t *data, size_t size) {
    const size_t limit = 65535U;
    size_t keep;
    if (size >= limit) {
        xx_rt_memcpy(history, data + size - limit, limit);
        *used = limit;
        return;
    }
    keep = *used < limit - size ? *used : limit - size;
    if (keep) xx_rt_memmove(history, history + *used - keep, keep);
    if (size) xx_rt_memcpy(history + keep, data, size);
    *used = keep + size;
}
static bool xpk_decode_inner(Abstractformat *format, const xpk_context *ctx,
                       xx_io_device *destination, xx_pd_struct *pd) {
    xpk_sink sink;
    uint8_t *packed = NULL;
    uint8_t *ring = NULL;
    uint8_t *decoded = NULL;
    uint8_t *history = NULL;
    size_t decoded_capacity = 0U;
    size_t history_used = 0U;
    size_t capacity = 0U;
    int64_t pos = ctx->stream_start;
    uint32_t pwpk_mode = UINT32_MAX;
    xpk_shrx_state shrx_state;
    uint64_t total = 0U;
    bool result = false;
    if (destination == format->device ||
        (ctx->method != XPK_CODE_NONE && ctx->method != XPK_CODE_MASH &&
         ctx->method != XPK_CODE_BZP2 && ctx->method != XPK_CODE_GZIP &&
         ctx->method != XPK_CODE_RLEN && ctx->method != XPK_CODE_CBR0 &&
         ctx->method != XPK_CODE_CBR1 && ctx->method != XPK_CODE_FRLE &&
         ctx->method != XPK_CODE_DLTA && ctx->method != XPK_CODE_FAST &&
         ctx->method != XPK_CODE_SQSH && ctx->method != XPK_CODE_BLZW &&
         ctx->method != XPK_CODE_SMPL && ctx->method != XPK_CODE_NUKE &&
         ctx->method != XPK_CODE_DUKE && ctx->method != XPK_CODE_LIN1 &&
         ctx->method != XPK_CODE_LIN2 && ctx->method != XPK_CODE_LIN3 &&
         ctx->method != XPK_CODE_LIN4 && ctx->method != XPK_CODE_RDCN &&
         ctx->method != XPK_CODE_ILZR && ctx->method != XPK_CODE_ZENO &&
         ctx->method != XPK_CODE_RAKE && ctx->method != XPK_CODE_FRHT &&
         ctx->method != XPK_CODE_TDCS && ctx->method != XPK_CODE_FBR2 &&
         ctx->method != XPK_CODE_SLZ3 && ctx->method != XPK_CODE_LZW2 &&
         ctx->method != XPK_CODE_LZW3 && ctx->method != XPK_CODE_LZW4 &&
         ctx->method != XPK_CODE_LZW5 && ctx->method != XPK_CODE_LZBS &&
         ctx->method != XPK_CODE_HUFF && ctx->method != XPK_CODE_HFMN &&
         ctx->method != XPK_CODE_ACCA && ctx->method != XPK_CODE_ARTM &&
         ctx->method != XPK_CODE_PWPK && ctx->method != XPK_CODE_LZCB &&
         ctx->method != XPK_CODE_CRM2 && ctx->method != XPK_CODE_CRMS &&
         ctx->method != XPK_CODE_CYB2 && ctx->method != XPK_CODE_IMPL &&
         ctx->method != XPK_CODE_LHLB && ctx->method != XPK_CODE_ELZX &&
         ctx->method != XPK_CODE_SLZX && ctx->method != XPK_CODE_SDHC &&
         ctx->method != XPK_CODE_SHR3 && ctx->method != XPK_CODE_SHRI &&
         ctx->method != XPK_CODE_SASC))
        return false;
    if (ctx->flags & XPK_FLAG_PASSWORD) return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device = destination;
    sink.pd = pd;
    sink.stage = (uint8_t *)xx_mem_alloc(XPK_OUT_STAGE);
    if (!sink.stage) return false;
    xx_mem_zero(&shrx_state, sizeof(shrx_state));
    if (ctx->method == XPK_CODE_SHR3 || ctx->method == XPK_CODE_SHRI) {
        history = (uint8_t *)xx_mem_alloc(65535U);
        if (!history) goto done;
    }
    if (ctx->method == XPK_CODE_MASH || ctx->method == XPK_CODE_CYB2) {
        ring = (uint8_t *)xx_mem_alloc(XPK_MASH_WINDOW);
        if (!ring) goto done;
    }
    for (;;) {
        xpk_chunk chunk;
        int64_t padded, readable;
        size_t index;
        uint16_t check = 0U;
        int step;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        step = xpk_read_chunk(format->device, ctx, &pos, &chunk, pd);
        if (step < 0) goto done;
        if (step == 0 || chunk.type == XPK_CHUNK_END) break;
        if (chunk.clen > XPK_MAX_CHUNK) goto done;
        padded = (chunk.clen + 3) & ~(int64_t)3;
        readable = ctx->avail - chunk.data_pos;
        if (readable > padded) readable = padded;
        if ((size_t)padded > capacity) {
            uint8_t *grown = (uint8_t *)(packed
                ? xx_mem_realloc(packed, (size_t)padded)
                : xx_mem_alloc((size_t)padded));
            if (!grown) goto done;
            packed = grown;
            capacity = (size_t)padded;
        }
        xx_rt_memset(packed, 0, (size_t)padded);
        if (!xpk_read_at(format->device, ctx->base + chunk.data_pos, packed,
                         (size_t)readable, pd)) goto done;
        /* Data check: XOR of big-endian 16-bit words, over the data padded
         * to 4 with whatever bytes follow it in the file. */
        for (index = 0U; index < (size_t)padded; index += 2U)
            check ^= xx_data_get_u16(packed + index, 2, 0, true);
        if (check != chunk.data_check) goto done;
        if (chunk.type == XPK_CHUNK_RAW || ctx->method == XPK_CODE_NONE) {
            if (chunk.clen != chunk.ulen ||
                (uint64_t)chunk.clen > (uint64_t)ctx->unpacked_len - total)
                goto done;
            if (!xpk_sink_write(&sink, packed, (size_t)chunk.clen)) goto done;
            if (history) xpk_history_push(history, &history_used,
                                           packed, (size_t)chunk.clen);
            total += (uint64_t)chunk.clen;
        } else if (ctx->method == XPK_CODE_MASH ||
                   ctx->method == XPK_CODE_CYB2) {
            xpk_mash mash;
            const uint8_t *mash_data = packed;
            size_t mash_size = (size_t)chunk.clen;
            if ((uint64_t)chunk.ulen > (uint64_t)ctx->unpacked_len - total)
                goto done;
            if (ctx->method == XPK_CODE_CYB2 &&
                !xpk_cyb2_mash_view(packed, (size_t)chunk.clen,
                                     &mash_data, &mash_size, pd)) goto done;
            xx_mem_zero(&mash, sizeof(mash));
            mash.in = mash_data;
            mash.len = mash_size;
            mash.ring = ring;
            mash.wanted = (uint64_t)chunk.ulen;
            mash.sink = &sink;
            xx_rt_memset(ring, 0, XPK_MASH_WINDOW);
            if (!xpk_mash_decode(&mash)) goto done;
            total += (uint64_t)chunk.ulen;
        } else {
            size_t wanted = (size_t)chunk.ulen;
            if (chunk.ulen > XPK_MAX_CHUNK ||
                (uint64_t)chunk.ulen > (uint64_t)ctx->unpacked_len - total)
                goto done;
            if (ctx->method == XPK_CODE_RLEN || ctx->method == XPK_CODE_CBR0 ||
                ctx->method == XPK_CODE_CBR1 || ctx->method == XPK_CODE_FRLE) {
                if (!xpk_rle(&sink, packed, (size_t)chunk.clen, wanted,
                             ctx->method, pd))
                    goto done;
            } else if (ctx->method == XPK_CODE_DLTA) {
                uint8_t accumulated = 0U;
                if (chunk.clen != chunk.ulen) goto done;
                for (index = 0U; index < wanted; ++index) {
                    if ((index & 65535U) == 0U && pd && xx_pd_is_stopped(pd))
                        goto done;
                    accumulated = (uint8_t)(accumulated + packed[index]);
                    packed[index] = accumulated;
                }
                if (!xpk_sink_write(&sink, packed, wanted)) goto done;
            } else {
                if (wanted >= decoded_capacity) {
                    uint8_t *grown = (uint8_t *)xx_mem_realloc(decoded, wanted + 1U);
                    if (!grown) goto done;
                    decoded = grown;
                    decoded_capacity = wanted + 1U;
                }
                if (ctx->method == XPK_CODE_BZP2) {
                    size_t written = 0U;
                    xx_io_device *source = xx_io_mem_open_ro(packed, (size_t)chunk.clen);
                    bool valid;
                    if (!source) goto done;
                    valid = xx_bzip2_unpack_device_to_memory(source, 0, chunk.clen,
                                decoded, wanted, &written, pd);
                    if (xx_io_close(source)) valid = false;
                    if (!valid || written != wanted)
                        goto done;
                } else if (ctx->method == XPK_CODE_FAST) {
                    if (!xpk_fast(packed, (size_t)chunk.clen, decoded, wanted, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_SQSH) {
                    if (!xpk_sqsh(packed, (size_t)chunk.clen, decoded, wanted, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_BLZW) {
                    if (!xpk_blzw(packed, (size_t)chunk.clen, decoded, wanted, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_SMPL) {
                    if (!xpk_smpl(packed, (size_t)chunk.clen, decoded, wanted, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_NUKE || ctx->method == XPK_CODE_DUKE) {
                    if (!xpk_nuke_native(packed, (size_t)chunk.clen, decoded,
                                         wanted, ctx->method == XPK_CODE_DUKE, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_LIN1 || ctx->method == XPK_CODE_LIN3) {
                    if (!xpk_lin1_native(packed, (size_t)chunk.clen, decoded, wanted, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_LIN2 || ctx->method == XPK_CODE_LIN4) {
                    if (!xpk_lin2_native(packed, (size_t)chunk.clen, decoded, wanted,
                                         ctx->method == XPK_CODE_LIN4, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_RDCN) {
                    if (!xpk_rdcn_native(packed, (size_t)chunk.clen, decoded, wanted, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_ILZR) {
                    if (!xpk_ilzr_native(packed, (size_t)chunk.clen, decoded, wanted, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_ZENO) {
                    if (!xpk_zeno_native(packed, (size_t)chunk.clen, decoded, wanted, pd))
                        goto done;
                } else if (ctx->method == XPK_CODE_RAKE ||
                           ctx->method == XPK_CODE_FRHT) {
                    if (!xpk_rake_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_TDCS) {
                    if (!xpk_tdcs_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_FBR2) {
                    if (!xpk_fbr2_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_SLZ3) {
                    if (!xpk_slz3_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method >= XPK_CODE_LZW2 &&
                           ctx->method <= XPK_CODE_LZW5) {
                    if (!xpk_yafa_lzw_native(packed, (size_t)chunk.clen,
                                             decoded, wanted,
                                             (unsigned)(ctx->method - XPK_CODE_LZW2) + 13U,
                                             pd)) goto done;
                } else if (ctx->method == XPK_CODE_LZBS) {
                    if (!xpk_yafa_lzbs_native(packed, (size_t)chunk.clen,
                                              decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_HUFF) {
                    if (!xpk_huff_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_HFMN) {
                    if (!xpk_hfmn_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_ACCA) {
                    if (!xpk_acca_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_ARTM) {
                    if (!xpk_artm_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_PWPK) {
                    if (!xpk_pwpk_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, &pwpk_mode, pd)) goto done;
                } else if (ctx->method == XPK_CODE_LZCB) {
                    if (!xpk_lzcb_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_CRM2 ||
                           ctx->method == XPK_CODE_CRMS) {
                    if (!xpk_crm_native(packed, (size_t)chunk.clen,
                                        decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_IMPL) {
                    if (!xpk_impl_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_LHLB) {
                    if (!xpk_lhlb_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (ctx->method == XPK_CODE_ELZX ||
                           ctx->method == XPK_CODE_SLZX) {
                    if (!xpk_lzx_native(packed, (size_t)chunk.clen,
                                        decoded, wanted,
                                        ctx->method == XPK_CODE_SLZX, pd)) goto done;
                } else if (ctx->method == XPK_CODE_SDHC) {
                    if (!xpk_sdhc_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, xpk_sdhc_child_decode,
                                         NULL, pd)) goto done;
                } else if (ctx->method == XPK_CODE_SHR3 ||
                           ctx->method == XPK_CODE_SHRI) {
                    if (!xpk_shrx_native(packed, (size_t)chunk.clen,
                                         decoded, wanted,
                                         ctx->method == XPK_CODE_SHR3,
                                         history, history_used,
                                         &shrx_state, pd)) goto done;
                } else if (ctx->method == XPK_CODE_SASC) {
                    if (!xpk_sasc_native(packed, (size_t)chunk.clen,
                                         decoded, wanted, pd)) goto done;
                } else if (!xpk_deflate(packed, (size_t)chunk.clen, decoded, wanted, pd)) {
                    goto done;
                }
                if ((pd && xx_pd_is_stopped(pd)) ||
                    !xpk_sink_write(&sink, decoded, wanted)) goto done;
                if (history) xpk_history_push(history, &history_used,
                                               decoded, wanted);
            }
            total += (uint64_t)chunk.ulen;
        }
    }
    result = total == ctx->unpacked_len &&
             sink.prefix_size == (ctx->unpacked_len < 16U ? ctx->unpacked_len : 16U) &&
             !xx_rt_memcmp(sink.prefix, ctx->reference, sink.prefix_size) &&
             xpk_sink_flush(&sink);
done:
    if (packed) xx_mem_free(packed);
    if (ring) xx_mem_free(ring);
    if (decoded) xx_mem_free(decoded);
    if (history) xx_mem_free(history);
    xx_mem_free(sink.stage);
    return result;
}
static bool xpk_sdhc_child_decode(const uint8_t *packed, size_t size,
                                  uint8_t *output, size_t wanted,
                                  void *opaque, xx_pd_struct *pd) {
    xx_io_device *source = NULL, *target = NULL;
    Abstractformat nested;
    xpk_context child;
    bool valid = false;
    (void)opaque;
    if (!packed || !output || !wanted || size > XPK_MAX_CHUNK ||
        (pd && xx_pd_is_stopped(pd))) return false;
    source = xx_io_mem_open_ro(packed, size);
    target = xx_io_mem_open(output, wanted);
    if (!source || !target) goto done;
    xx_mem_zero(&nested, sizeof(nested));
    nested.device = source;
    nested.base_address = 0;
    if (!xpk_parse(&nested, &child, true, pd) ||
        child.avail != (int64_t)size || child.unpacked_len != wanted ||
        child.method != XPK_CODE_GZIP) goto done;
    valid = xpk_decode_inner(&nested, &child, target, pd);
done:
    if (target) xx_io_close(target);
    if (source) xx_io_close(source);
    return valid && !(pd && xx_pd_is_stopped(pd));
}
static bool xpk_decode(Abstractformat *format, const xpk_context *ctx,
                        xx_io_device *destination, xx_pd_struct *pd) {
    int64_t cursor;
    bool valid;
    if (!format || !format->device || !ctx) return false;
    cursor = xx_io_tell(format->device);
    valid = xpk_decode_inner(format, ctx, destination, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, SEEK_SET)) valid = false;
    return valid;
}

/* ---- Records ---- */

static void xpk_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool xpk_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *xpk_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool xpk_set_record(xx_archive_record *record, const xpk_context *ctx) {
    char comment[10];
    size_t index;
    comment[0] = 'X';
    comment[1] = 'P';
    comment[2] = 'K';
    comment[3] = '-';
    for (index = 0U; index < 4U; ++index) {
        uint8_t c = (uint8_t)(ctx->method >> (24U - 8U * index));
        comment[4 + index] = (c >= 0x20U && c < 0x7fU) ? (char)c : '?';
    }
    comment[8] = '\0';
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = ctx->base;
    record->header_size = ctx->stream_start;
    record->data_offset = ctx->base + ctx->stream_start;
    record->compressed_size = ctx->end - ctx->stream_start;
    return xx_archive_record_set_original_name(record, XPK_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)(ctx->end - ctx->stream_start)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)ctx->unpacked_len) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
               ctx->method == XPK_CODE_NONE ? 0U : 1U) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          comment) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
               (ctx->flags & XPK_FLAG_PASSWORD) != 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_xpk_compressed_file_init(xx_xpk_compressed_file *archive,
                                 xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_XPK_COMPRESSED_FILE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-xpk");
    xx_format_set_extension(&archive->format, "xpk");
    archive->format.check_is_valid = xx_xpk_compressed_file_check_is_valid;
    archive->format.handle_base_info = xx_xpk_compressed_file_handle_base_info;
    archive->format.get_format_size = xx_xpk_compressed_file_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_xpk_compressed_file_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_xpk_compressed_file_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_xpk_compressed_file_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_xpk_compressed_file_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_xpk_compressed_file_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_xpk_compressed_file_free_archive_records_reading;
}

xx_xpk_compressed_file *xx_xpk_compressed_file_create(xx_io_device *device,
                                                      int64_t base_address) {
    xx_xpk_compressed_file *archive =
        (xx_xpk_compressed_file *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_xpk_compressed_file_init(archive, device, base_address);
    return archive;
}

void xx_xpk_compressed_file_destroy(xx_xpk_compressed_file *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_xpk_compressed_file_free(xx_xpk_compressed_file *archive) {
    if (!archive) return;
    xx_xpk_compressed_file_destroy(archive);
    xx_mem_free(archive);
}

bool xx_xpk_compressed_file_check_is_valid(Abstractformat *format,
                                           xx_pd_struct *pd) {
    xpk_context ctx;
    return xpk_parse(format, &ctx, false, pd);
}

bool xx_xpk_compressed_file_handle_base_info(Abstractformat *format,
                                             xx_pd_struct *pd) {
    xpk_context ctx;
    xx_xpk_compressed_file *archive;
    if (!format || !xpk_parse(format, &ctx, true, pd)) return false;
    archive = (xx_xpk_compressed_file *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = ctx.unpacked_len;
    archive->method = ctx.method;
    archive->chunk_count = ctx.chunk_count;
    archive->flags = ctx.flags;
    format->number_of_archive_records = 1U;
    format->format_size = ctx.end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_xpk_compressed_file_get_format_size(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xpk_compressed_file_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_xpk_compressed_file_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xpk_compressed_file_handle_base_info(format, pd))
               ? ((xx_xpk_compressed_file *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_xpk_compressed_file_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xpk_stream *stream;
    xx_archive_record_state *state;
    xpk_context ctx;
    if (!xpk_parse(format, &ctx, true, pd)) return NULL;
    stream = (xpk_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = ctx;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xpk_stream_free;
    state->total_records = 1U;
    if (!xpk_copy_options(&state->options, options) ||
        !xpk_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_xpk_compressed_file_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_xpk_compressed_file_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    xpk_stream *stream;
    if (!format || !state || state->format != format ||
        !state->has_record || !(stream = (xpk_stream *)state->internal_state))
        return false;
    if ((pd && xx_pd_is_stopped(pd)) || ++stream->index >= stream->count) {
        state->has_record = false;
        return false;
    }
    return false;
}

bool xx_xpk_compressed_file_unpack_to_device(xx_xpk_compressed_file *archive,
                                             xx_io_device *destination,
                                             xx_pd_struct *pd) {
    xpk_context ctx;
    int64_t saved;
    bool valid;
    if (!archive || !archive->format.device) return false;
    saved = xx_io_tell(archive->format.device);
    if (saved < 0) return false;
    valid = xpk_parse(&archive->format, &ctx, true, pd) &&
            xpk_decode(&archive->format, &ctx, destination, pd);
    if (xx_io_seek64(archive->format.device, saved, SEEK_SET)) valid = false;
    return valid;
}

bool xx_xpk_compressed_file_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    xpk_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL, *stage_path = NULL;
    bool result = false;
    bool overwrite = false;
    xx_io_device *destination = NULL;
    unsigned attempt;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (xpk_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = xpk_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xpk_decode(format, &stream->context, NULL, pd);
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
               ? xx_str_concat3(base, "/", XPK_PAYLOAD_NAME)
               : xx_str_concat(base, XPK_PAYLOAD_NAME);
    {
        const xx_var *v = xpk_option(&state->options, XX_META_ID_OPT_OVERWRITE);
        if (v) overwrite = xx_var_get_bool(v);
    }
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    stage_path = (char *)xx_mem_alloc(xx_str_len(path) + 40U);
    if (!stage_path) goto done;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (xx_rt_snprintf(stage_path, xx_str_len(path) + 40U,
                "%s.xxfc-xpk-%u.tmp", path, attempt) <= 0) goto done;
        destination = xx_io_file_open(stage_path, "wbx");
        if (destination) break;
    }
    if (!destination) goto done;
    {
        result = xpk_decode(format, &stream->context, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
        destination = NULL;
        if (result && !(pd && xx_pd_is_stopped(pd)))
            result = xx_io_file_replace_a(stage_path, path, overwrite);
        else result = false;
        if (!result) (void)xx_io_file_remove_a(stage_path);
    }
done:
    if (destination) {
        (void)xx_io_close(destination);
        (void)xx_io_file_remove_a(stage_path);
    }
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_xpk_compressed_file_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
