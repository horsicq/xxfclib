/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for Installer VISE for Windows (MindVision) packages.
 *
 * The container layout is described in the header.  Two ideas are taken
 * from XArchive's FT_VISE_SFX handling (core/xlegacystorearchive.cpp and
 * Algos/xvisedeflatedecoder.cpp, MIT, Copyright (c) 2026 hors): locating the
 * header through the "ESIV" footer / "SIVM" wrapper, and finding the second
 * setup table and the install objects by their fixed fields, accepting each
 * only after its stream decodes.  The code here is written from the file
 * structure; the decoder is an independent RFC 1951 inflater that reads the
 * 16-bit word framing VISE uses (XArchive feeds a byte-oriented Deflate
 * decoder and so rejects streams whose stored blocks sit on odd bytes).
 *
 * The stub executable is parsed only as far as its section table, optional
 * header security directory and overlay; nothing in it is executed.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installer_vise_windows/xx_installer_vise_windows.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>

/* xxfc_defs.h is shared and is not edited from here, so the file-type
 * constant is resolved through the alias macro that the enumerator defines. */
#ifdef INSTALLER_VISE_WINDOWS
#define XX_INSTALLER_VISE_WINDOWS_FILE_TYPE XX_FILE_TYPE_INSTALLER_VISE_WINDOWS
#else
#define XX_INSTALLER_VISE_WINDOWS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- limits -------------------------------------------------------------- */

#define VISE_MIN_SIZE 0x100
#define VISE_MAX_LFANEW INT64_C(0x10000000)
#define VISE_MAX_SECTIONS 96U
#define VISE_MAX_OPTIONAL 0x1000U
#define VISE_HEADER_SIZE 16
#define VISE_FOOTER_SIZE 8
#define VISE_WRAPPER_SIZE 8
#define VISE_TAIL_PROBE 64
#define VISE_SEARCH_CHUNK 65536U
#define VISE_MAX_TABLE 1024U
#define VISE_MAX_STRING16 4096U
#define VISE_MAX_LANGUAGES 4096U
#define VISE_MAX_MEMBERS 65536U
#define VISE_MAX_NAME16 260U
#define VISE_MAX_NAME8 128U
/* A setup stream has no declared size; this bounds the probe decode. */
#define VISE_SETUP_MAX_RAW (UINT64_C(256) << 20)
/* The installer script is read into memory for the object scan. */
#define VISE_SCAN_WINDOW (16U << 20)
/* Output the listing may decode while validating candidates. */
#define VISE_BUDGET_BASE (UINT64_C(256) << 20)
#define VISE_BUDGET_FACTOR 16U
#define VISE_METHOD_DEFLATE 8U

#define VISE_IN_BUFFER 65536U
/* The first read of a stream is small, so a candidate that fails in its
 * first block costs little; later reads double up to VISE_IN_BUFFER. */
#define VISE_FIRST_CHUNK 1024U
#define VISE_WINDOW 32768U
#define VISE_WINDOW_MASK (VISE_WINDOW - 1U)
#define VISE_FAST_BITS 9U
#define VISE_MAX_BITS 15U

enum {
    VISE_KIND_SETUP = 0,
    VISE_KIND_SETTINGS = 1,
    VISE_KIND_SUPPORT = 2,
    VISE_KIND_FILE = 3
};

/* --- little-endian helpers and device access ----------------------------- */

static uint16_t vise_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t vise_le32(const uint8_t *bytes) {
    return (uint32_t)vise_le16(bytes) | ((uint32_t)vise_le16(bytes + 2U) << 16U);
}

static bool vise_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool vise_range(int64_t limit, int64_t offset, int64_t size) {
    return limit >= 0 && offset >= 0 && size >= 0 && offset <= limit &&
           size <= limit - offset;
}

/* ======================================================================== */
/* Decoder: RFC 1951 Deflate behind the VISE 16-bit word framing.           */
/* ======================================================================== */

typedef struct vise_huff_s {
    uint16_t count[VISE_MAX_BITS + 1U];
    uint16_t symbol[320];
    /* (length << 12) | symbol for codes of at most VISE_FAST_BITS bits. */
    uint16_t fast[1U << VISE_FAST_BITS];
} vise_huff;

typedef struct vise_inflate_s {
    xx_io_device *device;       /* stream source, or */
    const uint8_t *memory;      /* an in-memory stream */
    int64_t next_offset;        /* device offset of the next chunk */
    uint64_t packed;            /* stream length */
    uint64_t unfetched;         /* stream bytes not yet buffered */
    size_t chunk_size;          /* size of the next read (even) */
    size_t buf_pos;
    size_t buf_len;
    uint64_t bitbuf;
    unsigned bitcnt;
    uint64_t consumed;          /* stream bits consumed */

    xx_io_device *sink;         /* optional device sink */
    uint8_t *out_memory;        /* optional memory sink */
    uint64_t out_capacity;
    uint64_t produced;
    uint64_t limit;             /* never produce more than this */
    size_t wpos;
    bool sink_failed;
    xx_pd_struct *pd;

    vise_huff lit;
    vise_huff dist;
    uint8_t lengths[320];
    uint8_t window[VISE_WINDOW];
    uint8_t buffer[VISE_IN_BUFFER];
} vise_inflate;

static const uint16_t vise_len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t vise_len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t vise_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577};
static const uint8_t vise_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const uint8_t vise_clen_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/* The writer emits 16-bit words high byte first; swapping each pair turns
 * the stream back into an ordinary LSB-first Deflate bit stream. */
static bool vise_fetch(vise_inflate *z) {
    size_t chunk, index;
    if (z->unfetched == 0U) return false;
    chunk = z->unfetched > z->chunk_size ? z->chunk_size
                                         : (size_t)z->unfetched;
    if (z->memory) {
        xx_mem_copy(z->buffer, z->memory + (size_t)(z->packed - z->unfetched),
                    chunk);
    } else if (!vise_read_at(z->device, z->next_offset, z->buffer, chunk)) {
        z->unfetched = 0U;
        return false;
    }
    for (index = 0U; index + 1U < chunk; index += 2U) {
        uint8_t value = z->buffer[index];
        z->buffer[index] = z->buffer[index + 1U];
        z->buffer[index + 1U] = value;
    }
    z->next_offset += (int64_t)chunk;
    z->unfetched -= chunk;
    z->buf_pos = 0U;
    z->buf_len = chunk;
    if (z->chunk_size < VISE_IN_BUFFER) z->chunk_size <<= 1U;
    return true;
}

/* The window and the input buffer need no clearing. */
static vise_inflate *vise_inflate_new(uint64_t packed) {
    vise_inflate *z = (vise_inflate *)xx_mem_alloc(sizeof(*z));
    if (!z) return NULL;
    xx_mem_zero(z, offsetof(vise_inflate, window));
    z->packed = packed;
    z->unfetched = packed;
    z->chunk_size = VISE_FIRST_CHUNK;
    return z;
}

static void vise_refill(vise_inflate *z) {
    while (z->bitcnt <= 56U) {
        if (z->buf_pos >= z->buf_len && !vise_fetch(z)) break;
        z->bitbuf |= (uint64_t)z->buffer[z->buf_pos++] << z->bitcnt;
        z->bitcnt += 8U;
    }
}

static bool vise_bits(vise_inflate *z, unsigned count, uint32_t *value) {
    if (count > 32U) return false;
    if (z->bitcnt < count) {
        vise_refill(z);
        if (z->bitcnt < count) return false;
    }
    *value = count ? (uint32_t)(z->bitbuf & ((UINT64_C(1) << count) - 1U))
                   : 0U;
    z->bitbuf >>= count;
    z->bitcnt -= count;
    z->consumed += count;
    return true;
}

/* Byte alignment in the word writer is alignment to the next word. */
static bool vise_align_word(vise_inflate *z) {
    uint32_t ignored;
    unsigned drop = (unsigned)((16U - (unsigned)(z->consumed & 15U)) & 15U);
    return vise_bits(z, drop, &ignored);
}

static bool vise_flush(vise_inflate *z, size_t size) {
    if (size == 0U) return true;
    if (z->out_memory) {
        uint64_t start = z->produced - size;
        if (start > z->out_capacity || size > z->out_capacity - start)
            return false;
        xx_mem_copy(z->out_memory + (size_t)start, z->window, size);
    }
    if (z->sink) {
        size_t done = 0U;
        while (done < size) {
            ssize_t amount = xx_io_write(z->sink, z->window + done,
                                         size - done);
            if (amount <= 0 || (size_t)amount > size - done) {
                z->sink_failed = true;
                return false;
            }
            done += (size_t)amount;
        }
    }
    return true;
}

static bool vise_put(vise_inflate *z, uint8_t value) {
    if (z->produced >= z->limit) return false;
    z->window[z->wpos++] = value;
    ++z->produced;
    if (z->wpos == VISE_WINDOW) {
        if (!vise_flush(z, VISE_WINDOW)) return false;
        z->wpos = 0U;
    }
    return true;
}

static uint32_t vise_reverse(uint32_t code, unsigned length) {
    uint32_t result = 0U;
    unsigned index;
    for (index = 0U; index < length; ++index) {
        result = (result << 1U) | (code & 1U);
        code >>= 1U;
    }
    return result;
}

/* Builds canonical decoding tables.  Returns a negative value for an
 * over-subscribed set, zero for a complete one and the number of unused
 * code slots for an incomplete one. */
static int vise_build(vise_huff *h, const uint8_t *lengths, unsigned n) {
    uint16_t offsets[VISE_MAX_BITS + 2U];
    unsigned symbol, length, index;
    uint32_t code;
    int left;
    xx_mem_zero(h->count, sizeof(h->count));
    xx_mem_zero(h->fast, sizeof(h->fast));
    for (symbol = 0U; symbol < n; ++symbol) {
        if (lengths[symbol] > VISE_MAX_BITS) return -1;
        ++h->count[lengths[symbol]];
    }
    if (h->count[0] == n) return 0;
    left = 1;
    for (length = 1U; length <= VISE_MAX_BITS; ++length) {
        left <<= 1;
        left -= (int)h->count[length];
        if (left < 0) return -1;
    }
    offsets[1] = 0U;
    for (length = 1U; length < VISE_MAX_BITS; ++length)
        offsets[length + 1U] = (uint16_t)(offsets[length] + h->count[length]);
    for (symbol = 0U; symbol < n; ++symbol)
        if (lengths[symbol] != 0U)
            h->symbol[offsets[lengths[symbol]]++] = (uint16_t)symbol;
    code = 0U;
    index = 0U;
    for (length = 1U; length <= VISE_MAX_BITS; ++length) {
        unsigned k;
        for (k = 0U; k < h->count[length]; ++k, ++index, ++code) {
            if (length <= VISE_FAST_BITS) {
                uint32_t slot = vise_reverse(code, length);
                for (; slot < (1U << VISE_FAST_BITS); slot += 1U << length)
                    h->fast[slot] =
                        (uint16_t)((length << 12U) | h->symbol[index]);
            }
        }
        code <<= 1U;
    }
    return left;
}

static bool vise_decode_symbol(vise_inflate *z, const vise_huff *h,
                               unsigned *symbol) {
    uint16_t entry;
    uint32_t code = 0U, first = 0U, index = 0U;
    unsigned length;
    if (z->bitcnt < VISE_MAX_BITS) vise_refill(z);
    entry = h->fast[z->bitbuf & ((1U << VISE_FAST_BITS) - 1U)];
    if (entry != 0U && (unsigned)(entry >> 12U) <= z->bitcnt) {
        unsigned used = entry >> 12U;
        z->bitbuf >>= used;
        z->bitcnt -= used;
        z->consumed += used;
        *symbol = entry & 0x0fffU;
        return true;
    }
    for (length = 1U; length <= VISE_MAX_BITS; ++length) {
        uint32_t count;
        if (length > z->bitcnt) return false;
        code |= (uint32_t)((z->bitbuf >> (length - 1U)) & 1U);
        count = h->count[length];
        if (code < first + count) {
            z->bitbuf >>= length;
            z->bitcnt -= length;
            z->consumed += length;
            *symbol = h->symbol[index + (code - first)];
            return true;
        }
        index += count;
        first += count;
        first <<= 1U;
        code <<= 1U;
    }
    return false;
}

static bool vise_codes(vise_inflate *z) {
    for (;;) {
        unsigned symbol;
        if (!vise_decode_symbol(z, &z->lit, &symbol)) return false;
        if (symbol < 256U) {
            if (!vise_put(z, (uint8_t)symbol)) return false;
        } else if (symbol == 256U) {
            return true;
        } else {
            uint32_t extra, length, distance;
            symbol -= 257U;
            if (symbol >= 29U) return false;
            if (!vise_bits(z, vise_len_extra[symbol], &extra)) return false;
            length = vise_len_base[symbol] + extra;
            if (!vise_decode_symbol(z, &z->dist, &symbol) || symbol >= 30U)
                return false;
            if (!vise_bits(z, vise_dist_extra[symbol], &extra)) return false;
            distance = vise_dist_base[symbol] + extra;
            if ((uint64_t)distance > z->produced) return false;
            while (length-- != 0U) {
                uint8_t value = z->window[(z->wpos - distance) &
                                          VISE_WINDOW_MASK];
                if (!vise_put(z, value)) return false;
            }
        }
    }
}

static bool vise_stored(vise_inflate *z) {
    uint32_t length, inverse, value;
    if (!vise_align_word(z) || !vise_bits(z, 16U, &length) ||
        !vise_bits(z, 16U, &inverse) || (length ^ inverse) != 0xffffU)
        return false;
    while (length-- != 0U) {
        if (!vise_bits(z, 8U, &value) || !vise_put(z, (uint8_t)value))
            return false;
    }
    /* The writer resumes on a word boundary after the stored bytes. */
    return vise_align_word(z);
}

static bool vise_fixed(vise_inflate *z) {
    unsigned symbol;
    for (symbol = 0U; symbol < 144U; ++symbol) z->lengths[symbol] = 8U;
    for (; symbol < 256U; ++symbol) z->lengths[symbol] = 9U;
    for (; symbol < 280U; ++symbol) z->lengths[symbol] = 7U;
    for (; symbol < 288U; ++symbol) z->lengths[symbol] = 8U;
    if (vise_build(&z->lit, z->lengths, 288U) != 0) return false;
    for (symbol = 0U; symbol < 32U; ++symbol) z->lengths[symbol] = 5U;
    return vise_build(&z->dist, z->lengths, 32U) == 0;
}

static bool vise_dynamic(vise_inflate *z) {
    uint32_t hlit, hdist, hclen, value;
    unsigned index = 0U, symbol;
    int result;
    if (!vise_bits(z, 5U, &hlit) || !vise_bits(z, 5U, &hdist) ||
        !vise_bits(z, 4U, &hclen))
        return false;
    hlit += 257U;
    hdist += 1U;
    hclen += 4U;
    if (hlit > 286U || hdist > 30U) return false;
    xx_mem_zero(z->lengths, sizeof(z->lengths));
    for (index = 0U; index < hclen; ++index) {
        if (!vise_bits(z, 3U, &value)) return false;
        z->lengths[vise_clen_order[index]] = (uint8_t)value;
    }
    if (vise_build(&z->lit, z->lengths, 19U) != 0) return false;
    index = 0U;
    while (index < hlit + hdist) {
        uint32_t repeat;
        uint8_t fill = 0U;
        if (!vise_decode_symbol(z, &z->lit, &symbol)) return false;
        if (symbol < 16U) {
            z->lengths[index++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16U) {
            if (index == 0U || !vise_bits(z, 2U, &repeat)) return false;
            fill = z->lengths[index - 1U];
            repeat += 3U;
        } else if (symbol == 17U) {
            if (!vise_bits(z, 3U, &repeat)) return false;
            repeat += 3U;
        } else {
            if (!vise_bits(z, 7U, &repeat)) return false;
            repeat += 11U;
        }
        if (index + repeat > hlit + hdist) return false;
        while (repeat-- != 0U) z->lengths[index++] = fill;
    }
    if (z->lengths[256] == 0U) return false;
    result = vise_build(&z->lit, z->lengths, hlit);
    if (result < 0 ||
        (result > 0 && hlit - z->lit.count[0] != 1U))
        return false;
    result = vise_build(&z->dist, z->lengths + hlit, hdist);
    if (result < 0 ||
        (result > 0 && hdist - z->dist.count[0] != 1U))
        return false;
    return true;
}

/* Runs one stream to its final block and checks the framing: the stream is
 * padded to a word boundary and, after a final stored block, carries one
 * more zero word. */
static bool vise_inflate_run(vise_inflate *z) {
    uint32_t final_block = 0U, type = 0U, value;
    uint64_t remaining;
    unsigned blocks = 0U;
    while (!final_block) {
        if (z->pd && (++blocks & 63U) == 0U && xx_pd_is_stopped(z->pd))
            return false;
        if (!vise_bits(z, 1U, &final_block) || !vise_bits(z, 2U, &type))
            return false;
        if (type == 0U) {
            if (!vise_stored(z)) return false;
        } else if (type == 1U) {
            if (!vise_fixed(z) || !vise_codes(z)) return false;
        } else if (type == 2U) {
            if (!vise_dynamic(z) || !vise_codes(z)) return false;
        } else {
            return false;
        }
    }
    if (!vise_align_word(z)) return false;
    remaining = z->packed * 8U - z->consumed;
    if (remaining == 16U) {
        if (!vise_bits(z, 16U, &value) || value != 0U) return false;
        remaining = 0U;
    }
    if (remaining != 0U) return false;
    if (z->wpos != 0U && !vise_flush(z, z->wpos)) return false;
    z->wpos = 0U;
    return !z->sink_failed;
}

/* Decodes the stream at [offset, offset + packed) of @p device.  With
 * @p expected >= 0 the stream must produce exactly that many bytes;
 * otherwise it may produce up to @p cap bytes and @p produced reports them. */
static bool vise_decode_device(xx_io_device *device, int64_t offset,
                               uint64_t packed, int64_t expected, uint64_t cap,
                               xx_io_device *sink, uint64_t *produced,
                               uint64_t *fetched, xx_pd_struct *pd) {
    vise_inflate *z;
    bool ok;
    if (!device || offset < 0 || packed < 2U || (packed & 1U) != 0U ||
        packed > (uint64_t)INT64_MAX / 8U)
        return false;
    z = vise_inflate_new(packed);
    if (!z) return false;
    z->device = device;
    z->next_offset = offset;
    z->sink = sink;
    z->limit = expected >= 0 ? (uint64_t)expected : cap;
    z->pd = pd;
    ok = vise_inflate_run(z);
    if (ok && expected >= 0 && z->produced != (uint64_t)expected) ok = false;
    if (produced) *produced = z->produced;
    if (fetched) *fetched = z->packed - z->unfetched;
    xx_mem_free(z);
    return ok;
}

bool xx_installer_vise_windows_decode_memory(const uint8_t *packed,
                                             size_t packed_size,
                                             uint8_t *output,
                                             size_t output_size,
                                             size_t *written) {
    vise_inflate *z;
    bool ok;
    if (written) *written = 0U;
    if (!packed || packed_size < 2U || (packed_size & 1U) != 0U ||
        (uint64_t)packed_size > (uint64_t)INT64_MAX / 8U)
        return false;
    z = vise_inflate_new(packed_size);
    if (!z) return false;
    z->memory = packed;
    z->out_memory = output;
    z->out_capacity = output ? output_size : 0U;
    z->limit = output_size;
    ok = vise_inflate_run(z);
    if (written) *written = (size_t)z->produced;
    xx_mem_free(z);
    return ok;
}

/* ======================================================================== */
/* Container                                                                */
/* ======================================================================== */

typedef struct vise_layout_s {
    int64_t total;          /* bytes in the view */
    int64_t header;         /* "ESIV" header */
    int64_t wrapper;        /* "SIVM" wrapper, or -1 */
    int64_t wrapper_size;
    int64_t footer;         /* "ESIV" footer, or -1 */
    int64_t end;            /* end of the container data */
    int64_t image_end;      /* end of the section raw data, clamped */
    int64_t cert_offset;    /* Authenticode block, or -1 */
    int64_t cert_size;
} vise_layout;

typedef struct vise_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint16_t dos_date;
    uint16_t dos_time;
    uint8_t kind;
} vise_member;

typedef struct vise_parsed_s {
    vise_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    vise_layout layout;
    int64_t format_size;
    uint64_t budget;        /* validation output still allowed */
} vise_parsed;

/* --- PE stub and container location -------------------------------------- */

static bool vise_is_tag(const uint8_t *bytes, const char *tag) {
    return bytes[0] == (uint8_t)tag[0] && bytes[1] == (uint8_t)tag[1] &&
           bytes[2] == (uint8_t)tag[2] && bytes[3] == (uint8_t)tag[3];
}

/* The pointer in a footer names the header, or the SIVM wrapper in front of
 * it.  Returns the header offset or -1. */
static int64_t vise_follow_pointer(xx_io_device *device, int64_t base,
                                   int64_t total, int64_t pointer,
                                   int64_t footer, int64_t *wrapper,
                                   int64_t *wrapper_size) {
    uint8_t head[12];
    if (pointer < 0x40 || !vise_range(total, pointer, 12) ||
        pointer + VISE_HEADER_SIZE > footer ||
        !vise_read_at(device, base + pointer, head, sizeof(head)))
        return -1;
    if (vise_is_tag(head, "ESIV")) {
        *wrapper = -1;
        *wrapper_size = 0;
        return pointer;
    }
    if (vise_is_tag(head, "SIVM") && vise_is_tag(head + 8, "ESIV") &&
        pointer + VISE_WRAPPER_SIZE + VISE_HEADER_SIZE <= footer) {
        *wrapper = pointer;
        *wrapper_size = (int64_t)vise_le32(head + 4);
        return pointer + VISE_WRAPPER_SIZE;
    }
    return -1;
}

static bool vise_locate(Abstractformat *format, vise_layout *layout) {
    uint8_t dos[0x40];
    uint8_t nt[24];
    uint8_t optional[256];
    uint8_t *sections = NULL;
    uint8_t head[VISE_HEADER_SIZE];
    xx_io_device *device;
    int64_t base, total, lfanew, table, overlay = 0;
    uint32_t count, optional_size, index;
    size_t optional_read;
    if (!format || !format->device || format->base_address < 0 || !layout)
        return false;
    device = format->device;
    base = format->base_address;
    total = xx_io_total_size(device);
    if (total < base) return false;
    total -= base;
    if (total < VISE_MIN_SIZE) return false;
    xx_mem_zero(layout, sizeof(*layout));
    layout->total = total;
    layout->header = -1;
    layout->wrapper = -1;
    layout->footer = -1;
    layout->cert_offset = -1;

    if (!vise_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    lfanew = (int64_t)vise_le32(dos + 0x3c);
    if (lfanew < 4 || lfanew > VISE_MAX_LFANEW ||
        !vise_range(total, lfanew, (int64_t)sizeof(nt)) ||
        !vise_read_at(device, base + lfanew, nt, sizeof(nt)) ||
        nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U)
        return false;
    count = vise_le16(nt + 6);
    optional_size = vise_le16(nt + 20);
    if (count == 0U || count > VISE_MAX_SECTIONS ||
        optional_size > VISE_MAX_OPTIONAL)
        return false;
    table = lfanew + 24 + (int64_t)optional_size;
    if (!vise_range(total, table, (int64_t)count * 40)) return false;

    /* The security directory is a file offset, not an RVA. */
    optional_read = optional_size < sizeof(optional) ? optional_size
                                                     : sizeof(optional);
    if (optional_read >= 2U &&
        vise_read_at(device, base + lfanew + 24, optional, optional_read)) {
        uint16_t magic = vise_le16(optional);
        size_t directories = magic == 0x10bU ? 96U
                             : magic == 0x20bU ? 112U : 0U;
        if (directories != 0U && optional_read >= directories + 40U &&
            vise_le32(optional + directories - 4U) >= 5U) {
            int64_t offset = (int64_t)vise_le32(optional + directories + 32U);
            int64_t size = (int64_t)vise_le32(optional + directories + 36U);
            if (size >= 8 && offset >= 0x40 && vise_range(total, offset, size)) {
                layout->cert_offset = offset;
                layout->cert_size = size;
            }
        }
    }

    sections = (uint8_t *)xx_mem_alloc((size_t)count * 40U);
    if (!sections) return false;
    if (!vise_read_at(device, base + table, sections, (size_t)count * 40U))
        goto fail;
    for (index = 0U; index < count; ++index) {
        const uint8_t *row = sections + (size_t)index * 40U;
        int64_t raw_size = (int64_t)vise_le32(row + 16);
        int64_t raw_offset = (int64_t)vise_le32(row + 20);
        if (raw_size == 0) continue;
        if (raw_offset + raw_size > overlay) overlay = raw_offset + raw_size;
    }
    layout->image_end = overlay < total ? overlay : total;

    /* 1. The container at the overlay. */
    if (overlay >= 0x40 && vise_range(total, overlay, VISE_HEADER_SIZE)) {
        uint8_t tag[4];
        if (vise_read_at(device, base + overlay, tag, sizeof(tag)) &&
            vise_is_tag(tag, "ESIV"))
            layout->header = overlay;
    }
    /* 2. The container in a section, behind its SIVM wrapper. */
    for (index = 0U; layout->header < 0 && index < count; ++index) {
        const uint8_t *row = sections + (size_t)index * 40U;
        int64_t raw_size = (int64_t)vise_le32(row + 16);
        int64_t raw_offset = (int64_t)vise_le32(row + 20);
        uint8_t wrap[12];
        int64_t size;
        if (raw_size < VISE_WRAPPER_SIZE + VISE_HEADER_SIZE +
                           VISE_FOOTER_SIZE ||
            raw_offset < 0x40 || !vise_range(total, raw_offset, 12) ||
            !vise_read_at(device, base + raw_offset, wrap, sizeof(wrap)) ||
            !vise_is_tag(wrap, "SIVM") || !vise_is_tag(wrap + 8, "ESIV"))
            continue;
        size = (int64_t)vise_le32(wrap + 4);
        if (size < VISE_WRAPPER_SIZE + VISE_HEADER_SIZE ||
            !vise_range(total, raw_offset, size + VISE_FOOTER_SIZE))
            continue;
        layout->header = raw_offset + VISE_WRAPPER_SIZE;
        layout->wrapper = raw_offset;
        layout->wrapper_size = size;
    }
    /* 3. A footer at the end of the file (or in front of the signature). */
    if (layout->header < 0) {
        uint8_t tail[VISE_TAIL_PROBE];
        int64_t tail_end = layout->cert_offset >= 0 &&
                                   layout->cert_offset >= overlay
                               ? layout->cert_offset : total;
        int64_t tail_start = tail_end > VISE_TAIL_PROBE
                                 ? tail_end - VISE_TAIL_PROBE : 0;
        size_t size = (size_t)(tail_end - tail_start);
        if (size >= VISE_FOOTER_SIZE &&
            vise_read_at(device, base + tail_start, tail, size)) {
            size_t at = size - VISE_FOOTER_SIZE + 1U;
            while (at-- != 0U) {
                int64_t header;
                if (!vise_is_tag(tail + at, "ESIV")) continue;
                header = vise_follow_pointer(
                    device, base, total, (int64_t)vise_le32(tail + at + 4U),
                    tail_start + (int64_t)at, &layout->wrapper,
                    &layout->wrapper_size);
                if (header >= 0) {
                    layout->header = header;
                    layout->footer = tail_start + (int64_t)at;
                    break;
                }
            }
        }
    }
    xx_mem_free(sections);
    sections = NULL;
    if (layout->header < 0 ||
        !vise_range(total, layout->header, VISE_HEADER_SIZE + 3) ||
        !vise_read_at(device, base + layout->header, head, sizeof(head)) ||
        !vise_is_tag(head, "ESIV") || vise_le32(head + 8) != 1U)
        return false;
    layout->end = total;
    if (layout->wrapper >= 0) {
        int64_t footer = layout->wrapper + layout->wrapper_size;
        if (vise_range(total, footer, VISE_FOOTER_SIZE)) layout->end = footer;
    }
    return true;
fail:
    if (sections) xx_mem_free(sections);
    return false;
}

static bool vise_footer_at(xx_io_device *device, int64_t base, int64_t total,
                           int64_t at, uint32_t pointer) {
    uint8_t footer[VISE_FOOTER_SIZE];
    return vise_range(total, at, VISE_FOOTER_SIZE) &&
           vise_read_at(device, base + at, footer, sizeof(footer)) &&
           vise_is_tag(footer, "ESIV") && vise_le32(footer + 4) == pointer;
}

/* Finds the footer that closes the container.  It normally sits at the end
 * of the SIVM wrapper, at the end of the file or right before the signature;
 * a package embedded in larger data is found by a forward search. */
static void vise_find_footer(Abstractformat *format, vise_layout *layout,
                             xx_pd_struct *pd) {
    xx_io_device *device = format->device;
    int64_t base = format->base_address;
    int64_t total = layout->total;
    int64_t anchor = layout->wrapper >= 0 ? layout->wrapper : layout->header;
    uint32_t pointer = (uint32_t)anchor;
    uint8_t *buffer;
    int64_t cursor;
    if (layout->footer >= 0) {
        layout->end = layout->footer;
        return;
    }
    if ((int64_t)pointer != anchor) return;
    if (layout->wrapper >= 0) {
        int64_t at = layout->wrapper + layout->wrapper_size;
        if (vise_footer_at(device, base, total, at, pointer)) {
            layout->footer = at;
            layout->end = at;
        }
        return;
    }
    {
        int64_t ends[2];
        unsigned k;
        ends[0] = total;
        ends[1] = layout->cert_offset >= 0 ? layout->cert_offset : -1;
        for (k = 0U; k < 2U; ++k) {
            int64_t at, stop;
            if (ends[k] < layout->header + VISE_HEADER_SIZE + VISE_FOOTER_SIZE)
                continue;
            stop = ends[k] - VISE_TAIL_PROBE;
            for (at = ends[k] - VISE_FOOTER_SIZE;
                 at >= stop && at >= layout->header + VISE_HEADER_SIZE; --at) {
                if (vise_footer_at(device, base, total, at, pointer)) {
                    layout->footer = at;
                    layout->end = at;
                    return;
                }
                if (k == 0U) break; /* the file end is exact */
            }
        }
    }
    buffer = (uint8_t *)xx_mem_alloc(VISE_SEARCH_CHUNK);
    if (!buffer) return;
    cursor = layout->header + VISE_HEADER_SIZE;
    while (cursor + VISE_FOOTER_SIZE <= total) {
        int64_t remaining = total - cursor;
        size_t chunk = remaining > (int64_t)VISE_SEARCH_CHUNK
                           ? VISE_SEARCH_CHUNK : (size_t)remaining;
        size_t i;
        if (pd && xx_pd_is_stopped(pd)) break;
        if (!vise_read_at(device, base + cursor, buffer, chunk)) break;
        for (i = 0U; i + VISE_FOOTER_SIZE <= chunk; ++i) {
            if (buffer[i] == 'E' && vise_is_tag(buffer + i, "ESIV") &&
                vise_le32(buffer + i + 4U) == pointer) {
                layout->footer = cursor + (int64_t)i;
                layout->end = layout->footer;
                xx_mem_free(buffer);
                return;
            }
        }
        if (chunk < VISE_FOOTER_SIZE) break;
        cursor += (int64_t)(chunk - VISE_FOOTER_SIZE + 1U);
    }
    xx_mem_free(buffer);
}

/* --- a bounded reading cursor ------------------------------------------- */

typedef struct vise_cursor_s {
    xx_io_device *device;
    int64_t base;
    int64_t pos;
    int64_t end;
    bool ok;
} vise_cursor;

static bool vise_take(vise_cursor *c, void *out, size_t size) {
    if (!c->ok || !vise_range(c->end, c->pos, (int64_t)size) ||
        !vise_read_at(c->device, c->base + c->pos, out, size)) {
        c->ok = false;
        return false;
    }
    c->pos += (int64_t)size;
    return true;
}

static bool vise_skip(vise_cursor *c, int64_t size) {
    if (!c->ok || !vise_range(c->end, c->pos, size)) {
        c->ok = false;
        return false;
    }
    c->pos += size;
    return true;
}

static uint32_t vise_u8(vise_cursor *c) {
    uint8_t value = 0U;
    (void)vise_take(c, &value, 1U);
    return value;
}

static uint32_t vise_u16(vise_cursor *c) {
    uint8_t value[2] = {0U, 0U};
    (void)vise_take(c, value, 2U);
    return vise_le16(value);
}

static uint32_t vise_u32(vise_cursor *c) {
    uint8_t value[4] = {0U, 0U, 0U, 0U};
    (void)vise_take(c, value, 4U);
    return vise_le32(value);
}

static bool vise_skip_str16(vise_cursor *c) {
    uint32_t length = vise_u16(c);
    if (length > VISE_MAX_STRING16) c->ok = false;
    return vise_skip(c, (int64_t)length);
}

/* --- member names --------------------------------------------------------- */

/* A stored name byte that could be a file name character. */
static bool vise_name_byte(uint8_t c) {
    return c >= 0x20U && c != 0x7fU;
}

static bool vise_name_ok(const uint8_t *name, size_t length) {
    size_t index;
    if (length == 0U || name[0] == ' ') return false;
    for (index = 0U; index < length; ++index)
        if (!vise_name_byte(name[index])) return false;
    return true;
}

static char vise_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool vise_is_device_stem(const char *name, size_t stem) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t k, i;
    for (k = 0U; k < sizeof(devices) / sizeof(devices[0]); ++k) {
        const char *word = devices[k];
        for (i = 0U; i < stem && word[i]; ++i)
            if (vise_upper(name[i]) != word[i]) break;
        if (i == stem && word[i] == 0) return true;
    }
    if (stem == 4U && name[3] >= '1' && name[3] <= '9' &&
        ((vise_upper(name[0]) == 'C' && vise_upper(name[1]) == 'O' &&
          vise_upper(name[2]) == 'M') ||
         (vise_upper(name[0]) == 'L' && vise_upper(name[1]) == 'P' &&
          vise_upper(name[2]) == 'T')))
        return true;
    return false;
}

/* One path component from stored bytes.  Characters a file system could
 * misread are escaped as %XX, so two different stored names never map to
 * one output file and none can leave the output directory. */
static char *vise_component(const uint8_t *bytes, size_t length) {
    static const char digits[] = "0123456789ABCDEF";
    char *out;
    size_t index, used = 0U, stem;
    bool meaningful = false;
    if (length > 4096U) return NULL;
    out = (char *)xx_mem_alloc(length * 3U + 2U);
    if (!out) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = bytes[index];
        bool edge = (index == length - 1U) && (c == '.' || c == ' ');
        bool safe = c > 0x20U && c < 0x7fU && c != '%' && c != '/' &&
                    c != '\\' && c != ':' && c != '*' && c != '?' &&
                    c != '"' && c != '<' && c != '>' && c != '|';
        if (c == ' ' && index != 0U && index != length - 1U) safe = true;
        if (safe && !edge) {
            out[used++] = (char)c;
            if (c != '.') meaningful = true;
        } else {
            out[used++] = '%';
            out[used++] = digits[(c >> 4U) & 0x0fU];
            out[used++] = digits[c & 0x0fU];
            meaningful = true;
        }
    }
    out[used] = 0;
    if (!meaningful) {
        /* Only dots: escape them all. */
        used = 0U;
        for (index = 0U; index < length; ++index) {
            out[used++] = '%';
            out[used++] = '2';
            out[used++] = 'E';
        }
        out[used] = 0;
    }
    stem = 0U;
    while (out[stem] && out[stem] != '.') ++stem;
    if (vise_is_device_stem(out, stem)) {
        char *prefixed = (char *)xx_mem_alloc(used + 2U);
        if (!prefixed) {
            xx_mem_free(out);
            return NULL;
        }
        prefixed[0] = '_';
        xx_mem_copy(prefixed + 1, out, used + 1U);
        xx_mem_free(out);
        out = prefixed;
    }
    return out;
}

static char *vise_join(const char *prefix, const char *leaf) {
    size_t a = prefix ? xx_str_len(prefix) : 0U;
    size_t b = xx_str_len(leaf);
    char *out = (char *)xx_mem_alloc(a + b + 1U);
    if (!out) return NULL;
    if (a) xx_mem_copy(out, prefix, a);
    xx_mem_copy(out + a, leaf, b + 1U);
    return out;
}

static char *vise_make_name(uint8_t kind, const uint8_t *bytes, size_t length) {
    char *leaf, *name;
    if (kind == VISE_KIND_SETTINGS) return vise_join("Setup/", "miscdata.xyz");
    leaf = vise_component(bytes, length);
    if (!leaf) return NULL;
    name = vise_join(kind == VISE_KIND_FILE ? NULL : "Setup/", leaf);
    xx_mem_free(leaf);
    return name;
}

/* Final check before a name is used as an output path: '/'-separated
 * components, none empty, "." or "..", none with a forbidden character. */
static bool vise_safe_output_name(const char *name) {
    size_t start = 0U, index = 0U;
    if (!name || !name[0]) return false;
    for (;;) {
        char c = name[index];
        if (c == '/' || c == 0) {
            size_t length = index - start, stem = 0U;
            if (length == 0U) return false;
            if (length == 1U && name[start] == '.') return false;
            if (length == 2U && name[start] == '.' && name[start + 1U] == '.')
                return false;
            if (name[index - 1U] == '.' || name[index - 1U] == ' ')
                return false;
            while (stem < length && name[start + stem] != '.') ++stem;
            if (vise_is_device_stem(name + start, stem)) return false;
            if (c == 0) break;
            start = index + 1U;
        } else if ((unsigned char)c < 0x20U || c == '\\' || c == ':' ||
                   c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                   c == '|') {
            return false;
        }
        ++index;
    }
    return true;
}

/* Keeps duplicate names (VISE installs the same file name into several
 * folders) from overwriting each other: later ones get "_2", "_3", ...
 * in front of their extension.  Comparison ignores ASCII case. */
static uint32_t vise_name_hash(const char *name) {
    uint32_t hash = 2166136261U;
    for (; *name; ++name) {
        hash ^= (uint8_t)vise_upper(*name);
        hash *= 16777619U;
    }
    return hash;
}

static bool vise_same_name(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b)
        if (vise_upper(*a) != vise_upper(*b)) return false;
    return *a == *b;
}

static char *vise_with_suffix(const char *name, size_t number) {
    char digits[24];
    size_t count = 0U, length = xx_str_len(name), dot = length, index, used;
    char *out;
    for (index = length; index > 0U; --index) {
        if (name[index - 1U] == '/') break;
        if (name[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    if (dot == 0U || name[dot - 1U] == '/') dot = length;
    do {
        digits[count++] = (char)('0' + number % 10U);
        number /= 10U;
    } while (number != 0U && count < sizeof(digits));
    out = (char *)xx_mem_alloc(length + count + 2U);
    if (!out) return NULL;
    xx_mem_copy(out, name, dot);
    used = dot;
    out[used++] = '_';
    while (count != 0U) out[used++] = digits[--count];
    xx_mem_copy(out + used, name + dot, length - dot + 1U);
    return out;
}

/* Returns the table slot holding @p name, or the free slot where it would
 * go (*found tells which). */
static size_t vise_name_slot(const vise_parsed *parsed, const size_t *table,
                             size_t slots, const char *name, bool *found) {
    size_t slot = vise_name_hash(name) & (slots - 1U);
    *found = false;
    while (table[slot] != SIZE_MAX) {
        if (vise_same_name(parsed->items[table[slot]].name, name)) {
            *found = true;
            break;
        }
        slot = (slot + 1U) & (slots - 1U);
    }
    return slot;
}

static bool vise_unique_names(vise_parsed *parsed) {
    size_t slots = 16U, index;
    size_t *table;
    if (parsed->count == 0U) return true;
    while (slots < parsed->count * 2U) slots <<= 1U;
    table = (size_t *)xx_mem_alloc(slots * sizeof(*table));
    if (!table) return false;
    for (index = 0U; index < slots; ++index) table[index] = SIZE_MAX;
    for (index = 0U; index < parsed->count; ++index) {
        bool found;
        size_t slot = vise_name_slot(parsed, table, slots,
                                     parsed->items[index].name, &found);
        if (found) {
            /* Every candidate derives from the stored name; the table holds
             * at most count names, so one of count + 1 suffixes is free. */
            size_t number;
            char *renamed = NULL;
            for (number = 2U; number <= parsed->count + 1U; ++number) {
                renamed = vise_with_suffix(parsed->items[index].name, number);
                if (!renamed) break;
                slot = vise_name_slot(parsed, table, slots, renamed, &found);
                if (!found) break;
                xx_mem_free(renamed);
                renamed = NULL;
            }
            if (!renamed) {
                xx_mem_free(table);
                return false;
            }
            xx_mem_free(parsed->items[index].name);
            parsed->items[index].name = renamed;
        }
        table[slot] = index;
    }
    xx_mem_free(table);
    return true;
}

/* --- member table ---------------------------------------------------------- */

static void vise_parsed_free(void *opaque) {
    vise_parsed *parsed = (vise_parsed *)opaque;
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index)
        if (parsed->items[index].name) xx_mem_free(parsed->items[index].name);
    if (parsed->items) xx_mem_free(parsed->items);
    xx_mem_free(parsed);
}

static bool vise_add(vise_parsed *parsed, const vise_member *member) {
    if (parsed->count >= VISE_MAX_MEMBERS) return false;
    if (parsed->count == parsed->capacity) {
        size_t grown = parsed->capacity ? parsed->capacity * 2U : 32U;
        vise_member *items = (vise_member *)xx_mem_realloc(
            parsed->items, grown * sizeof(*items));
        if (!items) return false;
        parsed->items = items;
        parsed->capacity = grown;
    }
    parsed->items[parsed->count++] = *member;
    return true;
}

static bool vise_add_named(vise_parsed *parsed, vise_member *member,
                           const uint8_t *name, size_t name_length) {
    member->name = vise_make_name(member->kind, name, name_length);
    if (!member->name) return false;
    if (!vise_add(parsed, member)) {
        xx_mem_free(member->name);
        member->name = NULL;
        return false;
    }
    return true;
}

/* Decodes a candidate stream without keeping its output and charges the
 * work (bytes read plus bytes produced) to the listing budget, so failed
 * candidates cannot make the listing unbounded. */
static bool vise_validate(Abstractformat *format, vise_parsed *parsed,
                          int64_t offset, int64_t packed, int64_t expected,
                          uint64_t *raw, xx_pd_struct *pd) {
    uint64_t produced = 0U, fetched = 0U, cap, cost;
    bool ok;
    if (parsed->budget == 0U) return false;
    if (expected >= 0 && (uint64_t)expected > parsed->budget) return false;
    cap = parsed->budget < VISE_SETUP_MAX_RAW ? parsed->budget
                                              : VISE_SETUP_MAX_RAW;
    ok = vise_decode_device(format->device, format->base_address + offset,
                            (uint64_t)packed, expected, cap, NULL, &produced,
                            &fetched, pd);
    cost = produced + fetched;
    parsed->budget -= cost < parsed->budget ? cost : parsed->budget;
    if (ok && raw) *raw = produced;
    return ok;
}

/* The first setup table.  With @p parsed NULL only the structure is
 * checked (the detector's probe); otherwise every stream is decoded. */
static bool vise_setup_table(Abstractformat *format, const vise_layout *layout,
                             vise_parsed *parsed, int64_t *table_end,
                             xx_pd_struct *pd) {
    vise_cursor c;
    uint32_t count, index, skip;
    c.device = format->device;
    c.base = format->base_address;
    c.pos = layout->header + VISE_HEADER_SIZE;
    c.end = layout->end;
    c.ok = true;
    (void)vise_skip(&c, (int64_t)vise_u8(&c));
    skip = vise_u8(&c) & 0x3fU;
    (void)vise_skip(&c, (int64_t)skip);
    (void)vise_skip(&c, (int64_t)vise_u8(&c));
    count = vise_u16(&c);
    if (!c.ok || count == 0U || count > VISE_MAX_TABLE) return false;
    for (index = 0U; index < count; ++index) {
        uint8_t name[256];
        uint8_t fields[16];
        uint32_t length;
        int64_t row = c.pos, data, packed;
        if (pd && xx_pd_is_stopped(pd)) return false;
        length = vise_u8(&c);
        if (!c.ok || length == 0U || !vise_take(&c, name, length) ||
            !vise_name_ok(name, length) || !vise_take(&c, fields, 16U))
            return false;
        packed = (int64_t)vise_le32(fields + 12);
        data = c.pos;
        if (packed < 2 || (packed & 1) != 0 || !vise_skip(&c, packed))
            return false;
        if (parsed) {
            vise_member member;
            uint64_t raw = 0U;
            /* The table itself is intact (every size is explicit), so a
             * stream that does not decode is left out rather than failing
             * the whole package. */
            if (!vise_validate(format, parsed, data, packed, -1, &raw, pd)) {
                if (pd && xx_pd_is_stopped(pd)) return false;
                continue;
            }
            xx_mem_zero(&member, sizeof(member));
            member.kind = VISE_KIND_SETUP;
            member.header_offset = row;
            member.header_size = data - row;
            member.data_offset = data;
            member.packed_size = packed;
            member.unpacked_size = raw;
            member.dos_date = vise_le16(fields);
            member.dos_time = vise_le16(fields + 2);
            if (!vise_add_named(parsed, &member, name, length)) return false;
        }
    }
    *table_end = c.pos;
    return true;
}

/* The language block that follows the first table in the releases seen,
 * ending in the packed settings stream.  Returns where the object scan
 * should start: after the settings stream, or @p start when the block has
 * another shape. */
static int64_t vise_settings(Abstractformat *format, const vise_layout *layout,
                             vise_parsed *parsed, int64_t start,
                             xx_pd_struct *pd) {
    vise_cursor c;
    uint32_t count, index;
    int64_t packed, data;
    uint64_t raw = 0U;
    vise_member member;
    uint8_t peek[2];
    c.device = format->device;
    c.base = format->base_address;
    c.pos = start;
    c.end = layout->end;
    c.ok = true;
    (void)vise_skip(&c, 4);
    if (!vise_take(&c, peek, 2U)) return start;
    /* A 16-bit string length has a zero high byte; the other shape stores
     * 8-bit lengths here and is not walked. */
    if (peek[1] != 0U) return start;
    c.pos -= 2;
    (void)vise_skip_str16(&c);
    (void)vise_skip_str16(&c);
    (void)vise_skip(&c, 2);
    (void)vise_skip_str16(&c);
    (void)vise_skip_str16(&c);
    count = vise_u16(&c);
    if (!c.ok || count > VISE_MAX_LANGUAGES) return start;
    for (index = 0U; index < count && c.ok; ++index) {
        (void)vise_skip(&c, 2);
        (void)vise_skip_str16(&c);
        (void)vise_skip_str16(&c);
    }
    packed = (int64_t)vise_u32(&c);
    data = c.pos;
    if (!c.ok || packed < 2 || (packed & 1) != 0 ||
        !vise_range(layout->end, data, packed))
        return start;
    if (!vise_validate(format, parsed, data, packed, -1, &raw, pd))
        return start;
    xx_mem_zero(&member, sizeof(member));
    member.kind = VISE_KIND_SETTINGS;
    member.header_offset = data - 4;
    member.header_size = 4;
    member.data_offset = data;
    member.packed_size = packed;
    member.unpacked_size = raw;
    if (!vise_add_named(parsed, &member, NULL, 0U)) return -1;
    return data + packed;
}

/* The scan asks, at every position, whether a name of some length starts
 * there.  These cursors remember the next byte that cannot be part of a name
 * (and the next dot), so the whole scan stays linear in the window size;
 * the positions asked about only ever increase. */
typedef struct vise_run_s {
    size_t from;   /* position the answer was computed for */
    size_t stop;   /* first matching byte at or after it, or the size */
} vise_run;

static size_t vise_next_bad(const uint8_t *window, size_t size, size_t from,
                            vise_run *run) {
    if (run->stop < from || run->from > from) {
        run->stop = from;
        while (run->stop < size && vise_name_byte(window[run->stop]))
            ++run->stop;
    }
    run->from = from;
    return run->stop;
}

static size_t vise_next_dot(const uint8_t *window, size_t size, size_t from,
                            vise_run *run) {
    if (run->stop < from || run->from > from) {
        run->stop = from;
        while (run->stop < size && window[run->stop] != '.') ++run->stop;
    }
    run->from = from;
    return run->stop;
}

/* An install-object name: @p length name bytes at @p at. */
static bool vise_object_name(const uint8_t *window, size_t size, size_t at,
                             size_t length, vise_run *bad) {
    return length != 0U && window[at] != ' ' &&
           vise_next_bad(window, size, at, bad) >= at + length;
}

/* A second-table record needs a real file name: an extension that is not
 * at either end. */
static bool vise_support_name(const uint8_t *window, size_t size, size_t at,
                              size_t length, vise_run *bad, vise_run *dot) {
    uint8_t last;
    if (length < 3U || window[at] == ' ') return false;
    last = window[at + length - 1U];
    if (last == ' ' || last == '.') return false;
    return vise_next_bad(window, size, at, bad) >= at + length &&
           vise_next_dot(window, size, at + 1U, dot) < at + length - 1U;
}

/* Walks the installer script from @p start for second-table setup files and
 * install-file objects.  The data area follows the script, so the first
 * accepted object's data offset ends the walk. */
static bool vise_scan(Abstractformat *format, const vise_layout *layout,
                      vise_parsed *parsed, int64_t start, xx_pd_struct *pd) {
    uint8_t *window;
    size_t size, q = 0U;
    int64_t limit = layout->end;
    unsigned tick = 0U;
    vise_run object_bad = {0U, 0U}, support_bad = {0U, 0U}, support_dot = {0U, 0U};
    if (start < 0 || start >= layout->end) return true;
    size = (layout->end - start) > (int64_t)VISE_SCAN_WINDOW
               ? VISE_SCAN_WINDOW : (size_t)(layout->end - start);
    window = (uint8_t *)xx_mem_alloc(size);
    if (!window) return false;
    if (!vise_read_at(format->device, format->base_address + start, window,
                      size)) {
        xx_mem_free(window);
        return false;
    }
    while (q < size && start + (int64_t)q < limit && parsed->budget != 0U) {
        int64_t here = start + (int64_t)q;
        bool matched = false;
        if (pd && (++tick & 0xffffU) == 0U && xx_pd_is_stopped(pd)) break;

        /* Install-file object: u16 name length, name, 4 or 6 reserved
         * bytes, unpacked, packed, flags 1, data offset. */
        if (q + 2U <= size) {
            uint32_t length = vise_le16(window + q);
            if (length != 0U && length <= VISE_MAX_NAME16 &&
                q + 2U + length + 4U + 16U <= size &&
                vise_object_name(window, size, q + 2U, length, &object_bad)) {
                size_t reserved;
                for (reserved = 4U; reserved <= 6U && !matched; reserved += 2U) {
                    size_t fields = q + 2U + length + reserved;
                    uint32_t raw, packed, flags, relative;
                    int64_t data;
                    uint64_t produced = 0U;
                    vise_member member;
                    if (fields + 16U > size) continue;
                    raw = vise_le32(window + fields);
                    packed = vise_le32(window + fields + 4U);
                    flags = vise_le32(window + fields + 8U);
                    relative = vise_le32(window + fields + 12U);
                    if (flags != 1U || raw == 0U || packed < 2U ||
                        (packed & 1U) != 0U)
                        continue;
                    data = layout->header + (int64_t)relative;
                    if (data < start + (int64_t)(fields + 16U) ||
                        !vise_range(layout->end, data, (int64_t)packed))
                        continue;
                    if (!vise_validate(format, parsed, data, (int64_t)packed,
                                       (int64_t)raw, &produced, pd))
                        continue;
                    xx_mem_zero(&member, sizeof(member));
                    member.kind = VISE_KIND_FILE;
                    member.header_offset = here;
                    member.header_size = (int64_t)(fields + 16U - q);
                    member.data_offset = data;
                    member.packed_size = (int64_t)packed;
                    member.unpacked_size = raw;
                    /* attributes, three FILETIMEs, then DOS date and time */
                    if (fields + 16U + 32U <= size) {
                        member.dos_date = vise_le16(window + fields + 44U);
                        member.dos_time = vise_le16(window + fields + 46U);
                    }
                    if (!vise_add_named(parsed, &member, window + q + 2U,
                                        length)) {
                        xx_mem_free(window);
                        return false;
                    }
                    if (data < limit) limit = data;
                    q = fields + 16U;
                    matched = true;
                }
            }
        }
        if (matched) continue;

        /* Second-table setup file: u8 name length, name, DOS date and
         * time, 8 bytes, u32 packed, packed bytes. */
        {
            uint32_t length = window[q];
            if (length != 0U && length <= VISE_MAX_NAME8 &&
                q + 1U + length + 16U <= size &&
                vise_support_name(window, size, q + 1U, length, &support_bad,
                                  &support_dot)) {
                size_t fields = q + 1U + length;
                int64_t packed = (int64_t)vise_le32(window + fields + 12U);
                int64_t data = start + (int64_t)(fields + 16U);
                uint64_t raw = 0U;
                if (packed >= 2 && (packed & 1) == 0 &&
                    vise_range(limit, data, packed) &&
                    vise_validate(format, parsed, data, packed, -1, &raw, pd)) {
                    vise_member member;
                    xx_mem_zero(&member, sizeof(member));
                    member.kind = VISE_KIND_SUPPORT;
                    member.header_offset = here;
                    member.header_size = data - here;
                    member.data_offset = data;
                    member.packed_size = packed;
                    member.unpacked_size = raw;
                    member.dos_date = vise_le16(window + fields);
                    member.dos_time = vise_le16(window + fields + 2U);
                    if (!vise_add_named(parsed, &member, window + q + 1U,
                                        length)) {
                        xx_mem_free(window);
                        return false;
                    }
                    if (data + packed - start >= (int64_t)size) break;
                    q = (size_t)(data + packed - start);
                    continue;
                }
            }
        }
        ++q;
    }
    xx_mem_free(window);
    return true;
}

static int64_t vise_format_size(const vise_layout *layout) {
    int64_t size = layout->footer >= 0 ? layout->footer + VISE_FOOTER_SIZE
                                       : layout->end;
    if (layout->image_end > size) size = layout->image_end;
    if (layout->cert_offset >= 0 &&
        layout->cert_offset + layout->cert_size > size)
        size = layout->cert_offset + layout->cert_size;
    return size;
}

static bool vise_parse(Abstractformat *format, vise_parsed **result,
                       xx_pd_struct *pd) {
    vise_parsed *parsed;
    int64_t table_end = 0, scan_start;
    if (!result) return false;
    parsed = (vise_parsed *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!parsed) return false;
    if (!vise_locate(format, &parsed->layout)) goto fail;
    vise_find_footer(format, &parsed->layout, pd);
    parsed->budget = VISE_BUDGET_BASE +
                     (uint64_t)parsed->layout.end * VISE_BUDGET_FACTOR;
    if (!vise_setup_table(format, &parsed->layout, parsed, &table_end, pd))
        goto fail;
    scan_start = vise_settings(format, &parsed->layout, parsed, table_end, pd);
    if (scan_start < 0) goto fail;
    if (!vise_scan(format, &parsed->layout, parsed, scan_start, pd)) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    if (!vise_unique_names(parsed)) goto fail;
    parsed->format_size = vise_format_size(&parsed->layout);
    *result = parsed;
    return true;
fail:
    vise_parsed_free(parsed);
    return false;
}

static vise_parsed *vise_clone(const vise_parsed *source) {
    vise_parsed *copy;
    size_t index;
    copy = (vise_parsed *)xx_mem_calloc(1U, sizeof(*copy));
    if (!copy) return NULL;
    *copy = *source;
    copy->items = NULL;
    copy->count = 0U;
    copy->capacity = 0U;
    copy->index = 0U;
    if (source->count) {
        copy->items = (vise_member *)xx_mem_calloc(source->count,
                                                   sizeof(*copy->items));
        if (!copy->items) {
            xx_mem_free(copy);
            return NULL;
        }
        copy->capacity = source->count;
        for (index = 0U; index < source->count; ++index) {
            copy->items[index] = source->items[index];
            copy->items[index].name = xx_str_dup(source->items[index].name);
            if (!copy->items[index].name) {
                copy->count = index;
                vise_parsed_free(copy);
                return NULL;
            }
            copy->count = index + 1U;
        }
    }
    return copy;
}

/* --- record plumbing ------------------------------------------------------ */

static bool vise_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *vise_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool vise_set_record(Abstractformat *format, xx_archive_record *record,
                            const vise_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = format->base_address + member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          VISE_METHOD_DEFLATE) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_TIMESTAMP,
               ((uint64_t)member->dos_date << 16U) |
                   (uint64_t)member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

/* --- lifecycle ------------------------------------------------------------ */

void xx_installer_vise_windows_init(xx_installer_vise_windows *archive,
                                    xx_io_device *device,
                                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INSTALLER_VISE_WINDOWS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-vise-installer");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_installer_vise_windows_check_is_valid;
    archive->format.handle_base_info =
        xx_installer_vise_windows_handle_base_info;
    archive->format.get_format_size =
        xx_installer_vise_windows_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_installer_vise_windows_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_installer_vise_windows_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_installer_vise_windows_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_installer_vise_windows_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_installer_vise_windows_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_installer_vise_windows_free_archive_records_reading;
    archive->header_offset = -1;
    archive->wrapper_offset = -1;
    archive->footer_offset = -1;
    archive->container_end = -1;
}

xx_installer_vise_windows *xx_installer_vise_windows_create(
    xx_io_device *device, int64_t base_address) {
    xx_installer_vise_windows *archive =
        (xx_installer_vise_windows *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_installer_vise_windows_init(archive, device, base_address);
    return archive;
}

void xx_installer_vise_windows_destroy(xx_installer_vise_windows *archive) {
    if (!archive) return;
    vise_parsed_free(archive->parsed);
    archive->parsed = NULL;
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_installer_vise_windows_free(xx_installer_vise_windows *archive) {
    if (!archive) return;
    xx_installer_vise_windows_destroy(archive);
    xx_mem_free(archive);
}

/* The detector's probe: the stub, the container header and the structure
 * of the first setup table.  Nothing is decoded here. */
bool xx_installer_vise_windows_check_is_valid(Abstractformat *format,
                                              xx_pd_struct *pd) {
    vise_layout layout;
    int64_t table_end = 0;
    if (!vise_locate(format, &layout)) return false;
    return vise_setup_table(format, &layout, NULL, &table_end, pd);
}

bool xx_installer_vise_windows_handle_base_info(Abstractformat *format,
                                                xx_pd_struct *pd) {
    xx_installer_vise_windows *archive;
    vise_parsed *parsed = NULL;
    if (!format || !vise_parse(format, &parsed, pd)) return false;
    archive = (xx_installer_vise_windows *)format;
    vise_parsed_free(archive->parsed);
    archive->parsed = parsed;
    archive->number_of_records = parsed->count;
    archive->header_offset = parsed->layout.header;
    archive->wrapper_offset = parsed->layout.wrapper;
    archive->footer_offset = parsed->layout.footer;
    archive->container_end = parsed->layout.end;
    format->number_of_archive_records = parsed->count;
    format->format_size = parsed->format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_installer_vise_windows_get_format_size(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installer_vise_windows_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_installer_vise_windows_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installer_vise_windows_handle_base_info(format, pd))
               ? ((xx_installer_vise_windows *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_installer_vise_windows_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_installer_vise_windows *archive = (xx_installer_vise_windows *)format;
    vise_parsed *parsed = NULL;
    xx_archive_record_state *state;
    if (!format) return NULL;
    if (archive->parsed) parsed = vise_clone((vise_parsed *)archive->parsed);
    else if (!vise_parse(format, &parsed, pd)) parsed = NULL;
    if (!parsed) return NULL;
    if (parsed->count == 0U) {
        vise_parsed_free(parsed);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        vise_parsed_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = parsed;
    state->free_internal = vise_parsed_free;
    state->total_records = parsed->count;
    if (!vise_copy_options(&state->options, options) ||
        !vise_set_record(format, &state->current_record, &parsed->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_installer_vise_windows_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_installer_vise_windows_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    vise_parsed *parsed;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(parsed = (vise_parsed *)state->internal_state) ||
        ++parsed->index >= parsed->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = vise_set_record(format, &state->current_record,
                                        &parsed->items[parsed->index]);
    return state->has_record;
}

bool xx_installer_vise_windows_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    vise_parsed *parsed;
    const vise_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(parsed = (vise_parsed *)state->internal_state) ||
        parsed->index >= parsed->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &parsed->items[parsed->index];
    if (member->unpacked_size > (uint64_t)INT64_MAX) return false;
    path_option = vise_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return vise_decode_device(format->device,
                                  format->base_address + member->data_offset,
                                  (uint64_t)member->packed_size,
                                  (int64_t)member->unpacked_size, 0U, NULL,
                                  NULL, NULL, pd);
    if (!vise_safe_output_name(member->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
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
        created = destination != NULL;
        if (!destination) goto done;
        result = vise_decode_device(format->device,
                                    format->base_address + member->data_offset,
                                    (uint64_t)member->packed_size,
                                    (int64_t)member->unpacked_size, 0U,
                                    destination, NULL, NULL, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_installer_vise_windows_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
