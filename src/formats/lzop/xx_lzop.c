/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * lzop (.lzo) container reader.  The LZO1X bit decoder and the LZOP framing
 * decoder already exist under src/algo/lzo and src/algo/lzop; this file only
 * adds the Abstractformat surface around them.  It scans the container once
 * to learn the exact stream extent, the stored file name and the expanded
 * size, then hands the byte production to xx_lzop_decode_device().
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

/* Name used when the header carries no stored file name. */
#define XX_LZOPFMT_PAYLOAD_NAME "payload"

#define XX_LZOPFMT_MIN_VERSION UINT16_C(0x0900)
#define XX_LZOPFMT_MAX_VERSION UINT16_C(0x1040)
#define XX_LZOPFMT_VERSION_LONG_HEADER UINT16_C(0x0940)

/* Absolute parse-time ceilings.  A block header is four attacker controlled
 * big-endian bytes, so a 12-byte block descriptor can otherwise claim a 4 GiB
 * expansion and a 100 KB file can claim hundreds of gigabytes in total.  Both
 * limits are enforced while SCANNING, before a single byte is decoded or a
 * single buffer is sized from the declared value.
 *
 * 64 MiB per block matches the codec and lzop's own maximum block size
 * (lzop never writes a block larger than 256 KiB by default).  4 GiB of total
 * output is far past any realistic .lzo and still bounds the walk. */
#define XX_LZOPFMT_MAX_BLOCK_SIZE (UINT32_C(64) * UINT32_C(1024) * UINT32_C(1024))
#define XX_LZOPFMT_MAX_TOTAL_OUTPUT (UINT64_C(4) * UINT64_C(1024) * \
                                     UINT64_C(1024) * UINT64_C(1024))
#define XX_LZOPFMT_MAX_BLOCKS UINT64_C(4000000)
#define XX_LZOPFMT_MAX_STREAMS UINT64_C(4096)
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

/** Everything the one-pass scan learns about a container. */
typedef struct xx_lzopfmt_scan_s {
    int64_t stream_size;      /**< Bytes consumed from base_address. */
    uint64_t uncompressed;    /**< Total expanded bytes. */
    uint64_t blocks;          /**< Block headers accepted. */
    uint64_t streams;         /**< Concatenated streams accepted. */
    uint16_t version;
    uint16_t library_version;
    uint32_t flags;
    uint8_t method;
    uint8_t level;
    char *stored_name;        /**< Owned, or NULL when absent. */
    int64_t header_size;      /**< Header bytes of the FIRST stream. */
} xx_lzopfmt_scan;

/** Bounded forward cursor over the device. */
typedef struct xx_lzopfmt_cursor_s {
    xx_io_device *device;
    int64_t position;
    int64_t end;
} xx_lzopfmt_cursor;

/** Running header checksums; lzop selects one of the two by flag. */
typedef struct xx_lzopfmt_sums_s {
    uint32_t adler32;
    uint32_t crc32;
} xx_lzopfmt_sums;

/** Counting sink so the decoder can be run without keeping the output. */
typedef struct xx_lzopfmt_counter_s {
    xx_io_device *target;
    uint64_t written;
    bool failed;
} xx_lzopfmt_counter;

static void xx_lzopfmt_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Cursor                                                              */
/* ------------------------------------------------------------------ */

static bool xx_lzopfmt_read(xx_lzopfmt_cursor *cursor, void *data,
                            size_t size) {
    size_t done = 0U;
    if (!cursor || (!data && size != 0U) || cursor->position < 0 ||
        cursor->position > cursor->end ||
        (uint64_t)size > (uint64_t)(cursor->end - cursor->position)) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(cursor->device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    cursor->position += (int64_t)size;
    return true;
}

/* Advance without reading; the caller has already checked that the skipped
 * bytes really exist, because the cursor end is the device end. */
static bool xx_lzopfmt_skip(xx_lzopfmt_cursor *cursor, uint64_t size) {
    if (!cursor || cursor->position < 0 || cursor->position > cursor->end ||
        size > (uint64_t)(cursor->end - cursor->position)) {
        return false;
    }
    cursor->position += (int64_t)size;
    return xx_io_seek64(cursor->device, cursor->position, SEEK_SET) == 0;
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
/* Scan                                                                */
/* ------------------------------------------------------------------ */

static void xx_lzopfmt_scan_cleanup(xx_lzopfmt_scan *scan) {
    if (!scan) return;
    if (scan->stored_name) xx_str_free(scan->stored_name);
    xx_mem_zero(scan, sizeof(*scan));
    scan->stream_size = -1;
    scan->header_size = -1;
}

/* A stored name is one path component written by lzop(1).  Anything with a
 * separator, a control byte or a drive colon is dropped rather than trusted;
 * the payload then falls back to XX_LZOPFMT_PAYLOAD_NAME. */
static bool xx_lzopfmt_plausible_name(const uint8_t *name, size_t length) {
    size_t index;
    if (!name || length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t ch = name[index];
        if (ch < 32U || ch == 127U || ch == '/' || ch == '\\' || ch == ':' ||
            ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' ||
            ch == '*') {
            return false;
        }
    }
    if (name[0] == '.' &&
        (length == 1U || (length == 2U && name[1] == '.'))) {
        return false;
    }
    return name[length - 1U] != ' ' && name[length - 1U] != '.';
}

/* The optional extra field is length-prefixed and checksummed exactly like
 * the header; it is skipped but still validated so a corrupt one is refused
 * here instead of inside the codec. */
static bool xx_lzopfmt_scan_extra(xx_lzopfmt_cursor *cursor, bool use_crc) {
    uint32_t length;
    uint32_t expected;
    xx_lzopfmt_sums sums;
    uint8_t buffer[4096];
    uint8_t length_bytes[4];
    if (!xx_lzopfmt_read_u32_plain(cursor, &length) ||
        length > XX_LZOPFMT_MAX_EXTRA_SIZE) {
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

/* Parse one stream header, the magic having been consumed already.  When
 * first_stream is true the learnt values are stored in the scan result. */
static bool xx_lzopfmt_scan_header(xx_lzopfmt_cursor *cursor,
                                   xx_lzopfmt_scan *scan, bool first_stream,
                                   uint32_t *out_flags) {
    xx_lzopfmt_sums sums;
    uint16_t version;
    uint16_t library_version;
    uint16_t needed_version = 0U;
    uint32_t flags;
    uint32_t discard;
    uint32_t expected;
    uint8_t method;
    uint8_t level = 0U;
    uint8_t name_length;
    uint8_t name[XX_LZOP_MAX_NAME_LENGTH];

    xx_lzopfmt_sums_init(&sums);
    if (!xx_lzopfmt_read_u16(cursor, &version, &sums) ||
        !xx_lzopfmt_read_u16(cursor, &library_version, &sums) ||
        version < XX_LZOPFMT_MIN_VERSION || version > XX_LZOPFMT_MAX_VERSION) {
        return false;
    }
    if (version >= XX_LZOPFMT_VERSION_LONG_HEADER &&
        (!xx_lzopfmt_read_u16(cursor, &needed_version, &sums) ||
         needed_version < XX_LZOPFMT_MIN_VERSION ||
         needed_version > XX_LZOPFMT_MAX_VERSION || needed_version > version)) {
        return false;
    }
    if (!xx_lzopfmt_read_sum(cursor, &method, 1U, &sums) ||
        (method != 1U && method != 2U && method != 3U)) {
        return false;
    }
    if (version >= XX_LZOPFMT_VERSION_LONG_HEADER &&
        !xx_lzopfmt_read_sum(cursor, &level, 1U, &sums)) {
        return false;
    }
    /* The filter and multipart flags describe payloads the codec refuses to
     * decode, so they are refused here too rather than listed and then failed
     * at extraction time. */
    if (!xx_lzopfmt_read_u32(cursor, &flags, &sums) ||
        (flags & ~XX_LZOPFMT_ALLOWED_FLAGS) != 0U ||
        (flags & (XX_LZOPFMT_FLAG_FILTER | XX_LZOPFMT_FLAG_MULTIPART)) != 0U ||
        !xx_lzopfmt_read_u32(cursor, &discard, &sums) ||  /* mode */
        !xx_lzopfmt_read_u32(cursor, &discard, &sums) ||  /* mtime low */
        (version >= XX_LZOPFMT_VERSION_LONG_HEADER &&
         !xx_lzopfmt_read_u32(cursor, &discard, &sums)) || /* mtime high */
        !xx_lzopfmt_read_sum(cursor, &name_length, 1U, &sums)) {
        return false;
    }
    if (name_length != 0U &&
        !xx_lzopfmt_read_sum(cursor, name, name_length, &sums)) {
        return false;
    }
    if (!xx_lzopfmt_read_u32_plain(cursor, &expected) ||
        expected != ((flags & XX_LZOPFMT_FLAG_HEADER_CRC) != 0U ? sums.crc32
                                                                : sums.adler32)) {
        return false;
    }
    if ((flags & XX_LZOPFMT_FLAG_HEADER_EXTRA) != 0U &&
        !xx_lzopfmt_scan_extra(
            cursor, (flags & XX_LZOPFMT_FLAG_HEADER_CRC) != 0U)) {
        return false;
    }
    if (first_stream && scan) {
        scan->version = version;
        scan->library_version = library_version;
        scan->flags = flags;
        scan->method = method;
        scan->level = level;
        if (name_length != 0U &&
            xx_lzopfmt_plausible_name(name, name_length)) {
            char *copy = (char *)xx_mem_alloc((size_t)name_length + 1U);
            if (!copy) return false;
            xx_rt_memcpy(copy, name, name_length);
            copy[name_length] = '\0';
            scan->stored_name = copy;
        }
    }
    if (out_flags) *out_flags = flags;
    return true;
}

/* Walk the block chain of one stream, skipping payloads.  Every declared
 * uncompressed length is bounded here, at parse time. */
static bool xx_lzopfmt_scan_blocks(xx_lzopfmt_cursor *cursor,
                                   xx_lzopfmt_scan *scan, uint32_t flags,
                                   xx_pd_struct *pd) {
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
            scan->uncompressed > XX_LZOPFMT_MAX_TOTAL_OUTPUT - expanded ||
            scan->blocks >= XX_LZOPFMT_MAX_BLOCKS) {
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
        scan->uncompressed += expanded;
        ++scan->blocks;
    }
}

/* One pass over the whole container: every concatenated stream, every block.
 * On success scan->stream_size is the exact byte length of the container. */
static bool xx_lzopfmt_scan_run(Abstractformat *self, xx_lzopfmt_scan *scan,
                                xx_pd_struct *pd) {
    xx_lzopfmt_cursor cursor;
    int64_t total_size;
    uint8_t magic[XX_LZOP_MAGIC_SIZE];
    if (scan) {
        xx_mem_zero(scan, sizeof(*scan));
        scan->stream_size = -1;
        scan->header_size = -1;
    }
    if (!self || !self->device || !scan || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < (int64_t)XX_LZOP_MAGIC_SIZE ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        goto fail;
    }
    xx_mem_zero(&cursor, sizeof(cursor));
    cursor.device = self->device;
    cursor.position = self->base_address;
    cursor.end = total_size;

    while (cursor.position < cursor.end) {
        uint32_t flags = 0U;
        bool first = scan->streams == 0U;
        int64_t before = cursor.position;
        if (scan->streams >= XX_LZOPFMT_MAX_STREAMS) goto fail;
        if (!xx_lzopfmt_read(&cursor, magic, sizeof(magic))) {
            if (first) goto fail;
            /* Fewer than nine trailing bytes: not another stream. */
            cursor.position = before;
            break;
        }
        if (!xx_lzop_has_header(magic, sizeof(magic))) {
            if (first) goto fail;
            /* Trailing bytes that are not a further stream become overlay. */
            cursor.position = before;
            break;
        }
        if (!xx_lzopfmt_scan_header(&cursor, scan, first, &flags)) {
            if (first) goto fail;
            cursor.position = before;
            break;
        }
        if (first) scan->header_size = cursor.position - self->base_address;
        if (!xx_lzopfmt_scan_blocks(&cursor, scan, flags, pd)) {
            if (first) goto fail;
            cursor.position = before;
            break;
        }
        ++scan->streams;
    }
    if (scan->streams == 0U || scan->blocks == 0U ||
        cursor.position <= self->base_address) {
        goto fail;
    }
    scan->stream_size = cursor.position - self->base_address;
    return true;
fail:
    xx_lzopfmt_scan_cleanup(scan);
    return false;
}

/* ------------------------------------------------------------------ */
/* Decoding                                                            */
/* ------------------------------------------------------------------ */

static ssize_t xx_lzopfmt_counter_write(xx_io_device *device, const void *data,
                                        size_t size) {
    xx_lzopfmt_counter *counter =
        device ? (xx_lzopfmt_counter *)device->priv : NULL;
    size_t done = 0U;
    if (!counter || (!data && size != 0U) ||
        (uint64_t)size > UINT64_MAX - counter->written) {
        if (counter) counter->failed = true;
        return -1;
    }
    while (counter->target && done < size) {
        ssize_t amount = xx_io_write(counter->target,
                                     (const uint8_t *)data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) {
            counter->failed = true;
            return -1;
        }
        done += (size_t)amount;
    }
    counter->written += (uint64_t)size;
    return (ssize_t)size;
}

/* Run the shared LZOP codec over the exact extent the scan measured.  The
 * codec insists on consuming its whole input, which is why the extent has to
 * be measured first rather than simply handed the rest of the device. */
static bool xx_lzopfmt_decode(Abstractformat *self, int64_t stream_size,
                              xx_io_device *destination, uint64_t *produced,
                              uint64_t *streams, xx_pd_struct *pd) {
    xx_lzopfmt_counter counter;
    xx_io_device sink;
    int64_t decoded_size = -1;
    size_t count = 0U;
    if (!self || !self->device || stream_size <= 0) return false;
    xx_mem_zero(&counter, sizeof(counter));
    xx_mem_zero(&sink, sizeof(sink));
    counter.target = destination;
    sink.write = xx_lzopfmt_counter_write;
    sink.priv = &counter;
    if (!xx_lzop_decode_device(self->device, self->base_address, stream_size,
                               &sink, &decoded_size, &count, pd) ||
        counter.failed || decoded_size < 0 ||
        (uint64_t)decoded_size != counter.written || count == 0U) {
        return false;
    }
    if (produced) *produced = counter.written;
    if (streams) *streams = (uint64_t)count;
    return true;
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

static const char *xx_lzopfmt_record_name(const xx_lzop *archive) {
    const xx_lzopfmt_scan *scan =
        archive ? (const xx_lzopfmt_scan *)archive->internal : NULL;
    return (scan && scan->stored_name) ? scan->stored_name
                                       : XX_LZOPFMT_PAYLOAD_NAME;
}

static bool xx_lzopfmt_populate_record(Abstractformat *self,
                                       xx_archive_record *record) {
    const xx_lzop *archive;
    const xx_lzopfmt_scan *scan;
    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    archive = (const xx_lzop *)self;
    scan = (const xx_lzopfmt_scan *)archive->internal;
    if (!scan || scan->header_size < 0 || self->format_size < scan->header_size) {
        return false;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = scan->header_size;
    record->data_offset = self->base_address + scan->header_size;
    record->compressed_size = self->format_size - scan->header_size;
    return xx_archive_record_set_original_name(
               record, xx_lzopfmt_record_name(archive)) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          archive->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_LEVEL,
                                          archive->level) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

/* Extraction-time check on the name that will become a path component. */
static bool xx_lzopfmt_safe_name(const char *name) {
    size_t length;
    size_t index;
    if (!name) return false;
    length = xx_str_len(name);
    if (length == 0U || length > XX_LZOP_MAX_NAME_LENGTH) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == 127U || ch == '/' || ch == '\\' || ch == ':' ||
            ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' ||
            ch == '*') {
            return false;
        }
    }
    if (name[0] == '.' &&
        (length == 1U || (length == 2U && name[1] == '.'))) {
        return false;
    }
    return name[length - 1U] != ' ' && name[length - 1U] != '.';
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

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
    if (archive->internal) {
        xx_lzopfmt_scan_cleanup((xx_lzopfmt_scan *)archive->internal);
        xx_mem_free(archive->internal);
        archive->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_streams = 0U;
    archive->number_of_blocks = 0U;
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
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
    bool result = xx_lzopfmt_scan_run(self, &scan, pd);
    xx_lzopfmt_scan_cleanup(&scan);
    return result;
}

bool xx_lzop_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzopfmt_scan *scan;
    xx_lzop *archive = (xx_lzop *)self;
    int64_t total_size;
    if (!self) return false;
    scan = (xx_lzopfmt_scan *)xx_mem_alloc(sizeof(*scan));
    if (!scan || !xx_lzopfmt_scan_run(self, scan, pd)) {
        if (scan) xx_mem_free(scan);
        archive->number_of_streams = 0U;
        archive->number_of_blocks = 0U;
        archive->uncompressed_size = 0U;
        archive->stream_end = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (archive->internal) {
        xx_lzopfmt_scan_cleanup((xx_lzopfmt_scan *)archive->internal);
        xx_mem_free(archive->internal);
    }
    archive->internal = scan;
    archive->number_of_streams = scan->streams;
    archive->number_of_blocks = scan->blocks;
    archive->uncompressed_size = scan->uncompressed;
    archive->version = scan->version;
    archive->library_version = scan->library_version;
    archive->flags = scan->flags;
    archive->method = scan->method;
    archive->level = scan->level;
    archive->stream_end = self->base_address + scan->stream_size;
    total_size = xx_io_total_size(self->device);
    self->format_size = scan->stream_size;
    self->overlay_offset = archive->stream_end < total_size
                               ? archive->stream_end
                               : -1;
    self->overlay_size = archive->stream_end < total_size
                             ? total_size - archive->stream_end
                             : 0;
    self->number_of_archive_records = 1U;
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
    return 1U;
}

bool xx_lzop_unpack_to_device(xx_lzop *archive, xx_io_device *destination,
                              xx_pd_struct *pd) {
    uint64_t produced = 0U;
    uint64_t streams = 0U;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_lzopfmt_decode(&archive->format, archive->format.format_size,
                           destination, &produced, &streams, pd)) {
        return false;
    }
    /* The codec must agree with the scan on both counts; a disagreement means
     * the two disagree about where the container ends. */
    return produced == archive->uncompressed_size &&
           streams == archive->number_of_streams;
}

xx_archive_record_state *xx_lzop_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_lzopfmt_copy_options(&state->options, options) ||
        !xx_lzopfmt_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
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
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* Exactly one record; moving past it ends the enumeration. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_lzop_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    const char *name;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    xx_lzop *archive = (xx_lzop *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    name = xx_archive_record_get_original_name(&state->current_record);
    if (!xx_lzopfmt_safe_name(name)) name = XX_LZOPFMT_PAYLOAD_NAME;
    path_value = xx_lzopfmt_find_option(&state->options,
                                        XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        /* No destination: verify the container decodes end to end. */
        uint64_t produced = 0U;
        uint64_t streams = 0U;
        return xx_lzopfmt_decode(self, self->format_size, NULL, &produced,
                                 &streams, pd) &&
               produced == archive->uncompressed_size;
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
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
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
        result = output && xx_lzop_unpack_to_device(archive, output, pd);
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
    const xx_lzopfmt_scan *scan =
        archive ? (const xx_lzopfmt_scan *)archive->internal : NULL;
    return scan ? scan->stored_name : NULL;
}
