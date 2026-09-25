/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ARDI self-extracting diskette image (Daniel F Valot, ARDI 4.31).  The
 * field tables are in xx_sfx_ardi_diskette_image.h.
 *
 * The container checks (EOF trailer, the record predicate tied to the
 * trailer, the 85 04 00 00 00 needle, the tagged prologue ended by 0xFF and
 * the label record) follow XArchive's sfx/xardi1sfx.cpp (MIT, same author).
 * Two things differ: the record is looked for first at the end of the NE
 * segment data, where every known carrier has it, before the needle scan;
 * and the image is inflated in one streaming pass that splits off the
 * prologue on the fly and checks the image CRC-32, instead of measuring the
 * prologue in a separate pass and publishing a substream window.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_ardi_diskette_image/xx_sfx_ardi_diskette_image.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: picks up the real file type as soon as the
 * enumerator (and its alias macro) exist in xxfc_defs.h. */
#ifdef SFX_ARDI_DISKETTE_IMAGE
#define XX_SFX_ARDI_DISKETTE_IMAGE_FILE_TYPE \
    XX_FILE_TYPE_SFX_ARDI_DISKETTE_IMAGE
#else
#define XX_SFX_ARDI_DISKETTE_IMAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ARDI_MZ_HEADER 0x40
#define ARDI_TRAILER_SIZE 51
#define ARDI_TAIL_TEXT_SIZE 31
#define ARDI_RECORD_SIZE 0x33
#define ARDI_MIN_SIZE \
    (ARDI_MZ_HEADER + ARDI_RECORD_SIZE + 1 + ARDI_TRAILER_SIZE)

#define ARDI_OFF_BPS 0x00
#define ARDI_OFF_TOTAL 0x08
#define ARDI_OFF_MEDIA 0x0A
#define ARDI_OFF_SPT 0x0D
#define ARDI_OFF_HEADS 0x0F
#define ARDI_OFF_SIGNATURE 0x24
#define ARDI_OFF_PACKED 0x26
#define ARDI_OFF_CONSTANT 0x2A
#define ARDI_OFF_ZERO 0x2E
#define ARDI_OFF_CRC 0x2F
#define ARDI_BYTES_PER_SECTOR 512U
#define ARDI_SIGNATURE 0x55AAU
#define ARDI_CONSTANT 0x00000485U

/* The NE walk: segment tables are tiny in practice (the ARDI 4.31 stub has
 * four entries), the cap only bounds a hostile table. */
#define ARDI_NE_HEADER 0x40
#define ARDI_MAX_SEGMENTS 1024U
#define ARDI_NE_RELOCATIONS 0x0100U

/* The needle scan.  The record sits 89,902 bytes in on every known
 * carrier; the window is far above that and keeps a hostile file that
 * happens to end in the trailer from turning the probe into a full read. */
#define ARDI_SCAN_CHUNK 0x10000U
#define ARDI_MAX_SCAN (INT64_C(16) * 1024 * 1024)
#define ARDI_MAX_CANDIDATES 64

/* The prologue is 4,563 or 4,570 bytes on every known carrier. */
#define ARDI_MAX_PROLOGUE 0x10000U
#define ARDI_MAX_PROLOGUE_RECORDS 64U
#define ARDI_PROLOGUE_END 0xFFU
#define ARDI_TAG_TEXT 0x02U

#define ARDI_METHOD_DEFLATE 8U
#define ARDI_MEMBER_NAME "disk.img"

static const char ardi_tail_prefix[] = "ARDI-(C)1991-";
static const char ardi_tail_suffix[] = "-Daniel Valot"; /* then 0x00 */

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static uint32_t ardi_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t ardi_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool ardi_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Reads up to @p size bytes; returns how many arrived. */
static size_t ardi_read_some(xx_io_device *device, int64_t offset,
                             uint8_t *buffer, size_t size) {
    size_t done = 0U;
    if (!device || !buffer || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return 0U;
    while (done < size) {
        ssize_t amount = xx_io_read(device, buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) break;
        done += (size_t)amount;
    }
    return done;
}

/* ---------------------------------------------------------------------- */
/* Container                                                               */

typedef struct ardi_context_s {
    int64_t size;         /* device bytes from base_address on */
    int64_t record;       /* relative to base_address */
    int64_t stream;       /* relative */
    int64_t stream_size;
    int64_t trailer;      /* relative */
    uint64_t image_size;
    uint32_t image_crc;
    uint16_t total_sectors;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint8_t media;
    bool scanned;
    char year[5];
} ardi_context;

/* The record predicate: the reference's field checks, and the declared
 * stream has to end on the byte where the trailer starts. */
static bool ardi_accept(Abstractformat *format, ardi_context *context,
                        int64_t record) {
    uint8_t r[ARDI_RECORD_SIZE];
    uint32_t total, packed;
    if (record < ARDI_MZ_HEADER ||
        record > context->trailer - (int64_t)ARDI_RECORD_SIZE - 1)
        return false;
    if (!ardi_read_at(format->device, format->base_address + record, r,
                      sizeof(r)))
        return false;
    if (ardi_le16(r + ARDI_OFF_SIGNATURE) != ARDI_SIGNATURE ||
        ardi_le32(r + ARDI_OFF_CONSTANT) != ARDI_CONSTANT ||
        r[ARDI_OFF_ZERO] != 0U ||
        ardi_le16(r + ARDI_OFF_BPS) != ARDI_BYTES_PER_SECTOR)
        return false;
    total = ardi_le16(r + ARDI_OFF_TOTAL);
    packed = ardi_le32(r + ARDI_OFF_PACKED);
    if (total == 0U || packed == 0U) return false;
    /* record <= trailer - 0x34, so this cannot overflow. */
    if (record + (int64_t)ARDI_RECORD_SIZE + (int64_t)packed !=
        context->trailer)
        return false;
    context->record = record;
    context->stream = record + (int64_t)ARDI_RECORD_SIZE;
    context->stream_size = (int64_t)packed;
    context->total_sectors = (uint16_t)total;
    context->image_size = (uint64_t)total * ARDI_BYTES_PER_SECTOR;
    context->image_crc = ardi_le32(r + ARDI_OFF_CRC);
    context->media = r[ARDI_OFF_MEDIA];
    context->sectors_per_track = (uint16_t)ardi_le16(r + ARDI_OFF_SPT);
    context->heads = (uint16_t)ardi_le16(r + ARDI_OFF_HEADS);
    return true;
}

/* Where the NE segment data ends: the highest segment end, relocation
 * records included.  -1 when the tables do not lead anywhere usable. */
static int64_t ardi_ne_end(Abstractformat *format, const ardi_context *context) {
    uint8_t mz[ARDI_MZ_HEADER];
    uint8_t ne[ARDI_NE_HEADER];
    uint8_t *table;
    int64_t header, table_offset, end = -1;
    uint32_t count, shift, index;
    if (!ardi_read_at(format->device, format->base_address, mz, sizeof(mz)))
        return -1;
    header = (int64_t)ardi_le32(mz + 0x3C);
    if (header < ARDI_MZ_HEADER ||
        header > context->trailer - (int64_t)ARDI_NE_HEADER)
        return -1;
    if (!ardi_read_at(format->device, format->base_address + header, ne,
                      sizeof(ne)) ||
        ne[0] != 'N' || ne[1] != 'E')
        return -1;
    count = ardi_le16(ne + 0x1C);
    shift = ardi_le16(ne + 0x32);
    if (shift == 0U) shift = 9U; /* the loader's default */
    if (count == 0U || count > ARDI_MAX_SEGMENTS || shift > 15U) return -1;
    table_offset = header + (int64_t)ardi_le16(ne + 0x22);
    if (table_offset + (int64_t)count * 8 > context->trailer) return -1;
    table = (uint8_t *)xx_mem_alloc((size_t)count * 8U);
    if (!table) return -1;
    if (!ardi_read_at(format->device, format->base_address + table_offset,
                      table, (size_t)count * 8U)) {
        xx_mem_free(table);
        return -1;
    }
    for (index = 0U; index < count; ++index) {
        const uint8_t *entry = table + (size_t)index * 8U;
        uint32_t sector = ardi_le16(entry);
        uint32_t length = ardi_le16(entry + 2U);
        uint32_t flags = ardi_le16(entry + 4U);
        int64_t segment_end;
        if (sector == 0U) continue; /* no data in the file */
        segment_end = ((int64_t)sector << shift) +
                      (int64_t)(length ? length : 0x10000U);
        if (segment_end > context->trailer) {
            end = -1;
            break;
        }
        if (flags & ARDI_NE_RELOCATIONS) {
            uint8_t word[2];
            if (segment_end > context->trailer - 2 ||
                !ardi_read_at(format->device,
                              format->base_address + segment_end, word,
                              sizeof(word))) {
                end = -1;
                break;
            }
            segment_end += 2 + (int64_t)ardi_le16(word) * 8;
        }
        if (segment_end > end) end = segment_end;
    }
    xx_mem_free(table);
    return end;
}

/* The reference's search: the 85 04 00 00 00 run at +0x2A, bounded by
 * ARDI_MAX_SCAN and ARDI_MAX_CANDIDATES. */
static bool ardi_scan(Abstractformat *format, ardi_context *context,
                      xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t first = ARDI_MZ_HEADER + ARDI_OFF_CONSTANT;
    int64_t last = context->trailer - (int64_t)ARDI_RECORD_SIZE - 1 +
                   ARDI_OFF_CONSTANT; /* last needle start */
    int64_t position;
    int candidates = 0;
    bool found = false;
    if (last > ARDI_MAX_SCAN) last = ARDI_MAX_SCAN;
    if (last < first) return false;
    buffer = (uint8_t *)xx_mem_alloc(ARDI_SCAN_CHUNK + 4U);
    if (!buffer) return false;
    for (position = first; position <= last && !found;
         position += ARDI_SCAN_CHUNK) {
        size_t want = ARDI_SCAN_CHUNK + 4U, got, index, limit;
        if (pd && xx_pd_is_stopped(pd)) break;
        if ((int64_t)want > context->trailer - position)
            want = (size_t)(context->trailer - position);
        got = ardi_read_some(format->device, format->base_address + position,
                             buffer, want);
        if (got < 5U) break;
        limit = got - 4U;
        if ((int64_t)limit > last - position + 1)
            limit = (size_t)(last - position + 1);
        for (index = 0U; index < limit; ++index) {
            if (buffer[index] != 0x85U || buffer[index + 1U] != 0x04U ||
                buffer[index + 2U] != 0U || buffer[index + 3U] != 0U ||
                buffer[index + 4U] != 0U)
                continue;
            if (ardi_accept(format, context,
                            position + (int64_t)index - ARDI_OFF_CONSTANT)) {
                found = true;
                break;
            }
            if (++candidates >= ARDI_MAX_CANDIDATES) break;
        }
        if (candidates >= ARDI_MAX_CANDIDATES) break;
    }
    xx_mem_free(buffer);
    return found;
}

static bool ardi_parse(Abstractformat *format, ardi_context *out,
                       xx_pd_struct *pd) {
    uint8_t mz[2];
    uint8_t tail[ARDI_TAIL_TEXT_SIZE];
    ardi_context context;
    int64_t total, ne_end;
    size_t index;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&context, sizeof(context));
    context.size = total - format->base_address;
    if (context.size < ARDI_MIN_SIZE) return false;
    if (!ardi_read_at(format->device, format->base_address, mz, sizeof(mz)) ||
        mz[0] != 'M' || mz[1] != 'Z')
        return false;

    /* The EOF trailer gates everything else: one short read. */
    if (!ardi_read_at(format->device,
                      format->base_address + context.size -
                          ARDI_TAIL_TEXT_SIZE,
                      tail, sizeof(tail)) ||
        xx_rt_memcmp(tail, ardi_tail_prefix, 13U) != 0 ||
        xx_rt_memcmp(tail + 17U, ardi_tail_suffix, 13U) != 0 ||
        tail[30] != 0U)
        return false;
    for (index = 0U; index < 4U; ++index) {
        uint8_t digit = tail[13U + index];
        if (digit < '0' || digit > '9') return false;
        context.year[index] = (char)digit;
    }
    context.year[4] = '\0';
    context.trailer = context.size - ARDI_TRAILER_SIZE;

    ne_end = ardi_ne_end(format, &context);
    if (ne_end < 0 || !ardi_accept(format, &context, ne_end)) {
        if (!ardi_scan(format, &context, pd)) return false;
        context.scanned = true;
    }
    *out = context;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Inflating                                                               */

/* The decoder's output device.  It takes the prologue apart as it arrives,
 * then passes exactly image_size bytes on (CRC-32 running) and refuses a
 * single byte more.  Refusing a write is also how the measuring pass stops
 * the decoder once the prologue is complete. */
typedef struct ardi_sink_s {
    uint8_t *prologue;          /* ARDI_MAX_PROLOGUE bytes */
    size_t fill;
    size_t cursor;              /* next record header in the prologue */
    uint32_t records;
    size_t prologue_size;
    bool prologue_done;
    bool stop_after_prologue;
    bool stopped;               /* refused on purpose (measuring pass) */
    bool failed;                /* malformed prologue, too much output,
                                   or the destination refused a write */
    uint64_t image_expected;
    uint64_t image_written;
    uint32_t crc;
    xx_io_device *destination;  /* NULL: verify only */
} ardi_sink;

/* 1: prologue complete; 0: needs more bytes; -1: malformed. */
static int ardi_prologue_walk(ardi_sink *sink) {
    for (;;) {
        uint32_t length;
        if (sink->cursor >= sink->fill) return 0;
        if (sink->prologue[sink->cursor] == ARDI_PROLOGUE_END) {
            sink->prologue_size = sink->cursor + 1U;
            sink->prologue_done = true;
            return 1;
        }
        if (sink->records >= ARDI_MAX_PROLOGUE_RECORDS) return -1;
        if (sink->fill - sink->cursor < 5U) return 0;
        length = ardi_le32(sink->prologue + sink->cursor + 1U);
        /* cursor + 5 + length must leave room for the 0xFF tag. */
        if ((uint64_t)length >=
            (uint64_t)ARDI_MAX_PROLOGUE - sink->cursor - 5U)
            return -1;
        sink->cursor += 5U + (size_t)length;
        ++sink->records;
    }
}

static bool ardi_write_all(xx_io_device *destination, const uint8_t *data,
                           size_t size) {
    size_t written = 0U;
    while (written < size) {
        ssize_t amount =
            xx_io_write(destination, data + written, size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

static ssize_t ardi_sink_write(xx_io_device *self, const void *buffer,
                               size_t size) {
    ardi_sink *sink = self ? (ardi_sink *)self->priv : NULL;
    const uint8_t *data = (const uint8_t *)buffer;
    size_t left = size;
    if (!sink || (!data && size != 0U) || sink->failed || sink->stopped ||
        size > ((size_t)-1) / 2U)
        return -1;
    while (left != 0U && !sink->prologue_done) {
        size_t room = ARDI_MAX_PROLOGUE - sink->fill;
        size_t take = left < room ? left : room;
        int state;
        if (take == 0U) {
            sink->failed = true;
            return -1;
        }
        xx_rt_memcpy(sink->prologue + sink->fill, data, take);
        sink->fill += take;
        state = ardi_prologue_walk(sink);
        if (state < 0) {
            sink->failed = true;
            return -1;
        }
        if (state > 0) {
            /* Bytes behind the 0xFF tag belong to the image. */
            size_t excess = sink->fill - sink->prologue_size;
            sink->fill = sink->prologue_size;
            data += take - excess;
            left -= take - excess;
            if (sink->stop_after_prologue) {
                sink->stopped = true;
                return -1;
            }
            break;
        }
        data += take;
        left -= take;
    }
    if (left != 0U) {
        if ((uint64_t)left > sink->image_expected - sink->image_written) {
            sink->failed = true;
            return -1;
        }
        sink->crc = xx_crc32_calc(sink->crc, data, left);
        if (sink->destination &&
            !ardi_write_all(sink->destination, data, left)) {
            sink->failed = true;
            return -1;
        }
        sink->image_written += (uint64_t)left;
    }
    return (ssize_t)size;
}

static bool ardi_inflate(Abstractformat *format, const ardi_context *context,
                         ardi_sink *sink, xx_pd_struct *pd) {
    xx_io_device device;
    xx_mem_zero(&device, sizeof(device));
    device.write = ardi_sink_write;
    device.priv = sink;
    sink->prologue = (uint8_t *)xx_mem_alloc(ARDI_MAX_PROLOGUE);
    if (!sink->prologue) return false;
    return xx_deflate_unpack_device(format->device,
                                    format->base_address + context->stream,
                                    context->stream_size, &device, false, pd);
}

/* Keeps printable ASCII, turns every run of anything else into one space. */
static void ardi_copy_label(char *label, const uint8_t *text, size_t size) {
    size_t index, used = 0U;
    bool space = false;
    for (index = 0U; index < size && text[index] != 0U; ++index) {
        uint8_t c = text[index];
        if (c > 0x20U && c < 0x7FU) {
            if (space && used != 0U) {
                /* Room for the space and at least one more character. */
                if (used + 1U >= XX_SFX_ARDI_DISKETTE_IMAGE_LABEL_MAX) break;
                label[used++] = ' ';
            }
            space = false;
            if (used >= XX_SFX_ARDI_DISKETTE_IMAGE_LABEL_MAX) break;
            label[used++] = (char)c;
        } else {
            space = true;
        }
    }
    label[used] = '\0';
}

/* The measuring pass: inflate until the prologue is complete, which takes
 * one output buffer of the decoder, and pick up the first label text. */
static int64_t ardi_measure(Abstractformat *format, const ardi_context *context,
                            char *label, xx_pd_struct *pd) {
    ardi_sink sink;
    int64_t result = -1;
    size_t cursor = 0U;
    label[0] = '\0';
    xx_mem_zero(&sink, sizeof(sink));
    sink.stop_after_prologue = true;
    sink.image_expected = context->image_size;
    (void)ardi_inflate(format, context, &sink, pd);
    if (sink.prologue && sink.prologue_done && !sink.failed) {
        result = (int64_t)sink.prologue_size;
        while (cursor + 5U <= sink.prologue_size) {
            uint8_t tag = sink.prologue[cursor];
            size_t length;
            if (tag == ARDI_PROLOGUE_END) break;
            length = (size_t)ardi_le32(sink.prologue + cursor + 1U);
            if (length > sink.prologue_size - cursor - 5U) break;
            if (tag == ARDI_TAG_TEXT && length > 4U) {
                ardi_copy_label(label, sink.prologue + cursor + 9U,
                                length - 4U);
                break;
            }
            cursor += 5U + length;
        }
    }
    if (sink.prologue) xx_mem_free(sink.prologue);
    return result;
}

static bool ardi_decode(Abstractformat *format, const ardi_context *context,
                        xx_io_device *destination, xx_pd_struct *pd) {
    ardi_sink sink;
    bool inflated, result;
    xx_mem_zero(&sink, sizeof(sink));
    sink.image_expected = context->image_size;
    sink.destination = destination;
    inflated = ardi_inflate(format, context, &sink, pd);
    result = inflated && sink.prologue && sink.prologue_done &&
             !sink.failed && !sink.stopped &&
             sink.image_written == context->image_size &&
             sink.crc == context->image_crc;
    if (sink.prologue) xx_mem_free(sink.prologue);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

typedef struct ardi_stream_s {
    ardi_context context;
    size_t index;
    size_t count;
} ardi_stream;

static void ardi_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool ardi_copy_options(xx_list_s *destination,
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

static const xx_var *ardi_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ardi_set_record(Abstractformat *format, xx_archive_record *record,
                            const ardi_context *context, const char *label) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + context->record;
    record->header_size = ARDI_RECORD_SIZE;
    record->data_offset = format->base_address + context->stream;
    record->compressed_size = context->stream_size;
    if (!xx_archive_record_set_original_name(record, ARDI_MEMBER_NAME) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)context->stream_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        context->image_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        ARDI_METHOD_DEFLATE) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                        context->image_crc) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (label && label[0] &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, label))
        return false;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_sfx_ardi_diskette_image_init(xx_sfx_ardi_diskette_image *archive,
                                     xx_io_device *device,
                                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_ARDI_DISKETTE_IMAGE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdos-program");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_ardi_diskette_image_check_is_valid;
    archive->format.handle_base_info =
        xx_sfx_ardi_diskette_image_handle_base_info;
    archive->format.get_format_size =
        xx_sfx_ardi_diskette_image_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_ardi_diskette_image_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_ardi_diskette_image_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_ardi_diskette_image_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_ardi_diskette_image_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_ardi_diskette_image_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_ardi_diskette_image_free_archive_records_reading;
    archive->record_offset = -1;
    archive->stream_offset = -1;
    archive->trailer_offset = -1;
    archive->prologue_size = -1;
}

xx_sfx_ardi_diskette_image *xx_sfx_ardi_diskette_image_create(
    xx_io_device *device, int64_t base_address) {
    xx_sfx_ardi_diskette_image *archive =
        (xx_sfx_ardi_diskette_image *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_ardi_diskette_image_init(archive, device, base_address);
    return archive;
}

void xx_sfx_ardi_diskette_image_destroy(xx_sfx_ardi_diskette_image *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_ardi_diskette_image_free(xx_sfx_ardi_diskette_image *archive) {
    if (!archive) return;
    xx_sfx_ardi_diskette_image_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_ardi_diskette_image_check_is_valid(Abstractformat *format,
                                               xx_pd_struct *pd) {
    ardi_context context;
    return ardi_parse(format, &context, pd);
}

bool xx_sfx_ardi_diskette_image_handle_base_info(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    ardi_context context;
    xx_sfx_ardi_diskette_image *archive;
    char version[16];
    if (!format || !ardi_parse(format, &context, pd)) return false;
    archive = (xx_sfx_ardi_diskette_image *)format;
    archive->record_offset = format->base_address + context.record;
    archive->stream_offset = format->base_address + context.stream;
    archive->stream_size = context.stream_size;
    archive->trailer_offset = format->base_address + context.trailer;
    archive->image_size = context.image_size;
    archive->image_crc32 = context.image_crc;
    archive->bytes_per_sector = (uint16_t)ARDI_BYTES_PER_SECTOR;
    archive->total_sectors = context.total_sectors;
    archive->sectors_per_track = context.sectors_per_track;
    archive->heads = context.heads;
    archive->media_descriptor = context.media;
    archive->located_by_scan = context.scanned;
    xx_rt_memcpy(archive->year, context.year, sizeof(archive->year));
    /* A stream whose prologue cannot be taken apart still leaves a valid
     * container; the member then fails to extract. */
    archive->prologue_size =
        ardi_measure(format, &context, archive->label, pd);
    archive->number_of_records = 1U;
    xx_rt_memcpy(version, "1991-", 5U);
    xx_rt_memcpy(version + 5, context.year, 5U);
    xx_format_set_version(format, version);
    format->number_of_archive_records = 1U;
    format->format_size = context.size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_ardi_diskette_image_get_format_size(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_ardi_diskette_image_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_sfx_ardi_diskette_image_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_ardi_diskette_image_handle_base_info(format, pd))
               ? ((xx_sfx_ardi_diskette_image *)format)->number_of_records
               : 0U;
}

bool xx_sfx_ardi_diskette_image_unpack_to_device(
    xx_sfx_ardi_diskette_image *archive, xx_io_device *destination,
    xx_pd_struct *pd) {
    ardi_context context;
    if (!archive || !ardi_parse(&archive->format, &context, pd)) return false;
    return ardi_decode(&archive->format, &context, destination, pd);
}

xx_archive_record_state *
xx_sfx_ardi_diskette_image_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ardi_stream *stream;
    xx_archive_record_state *state;
    ardi_context context;
    const char *label = NULL;
    if (!ardi_parse(format, &context, pd)) return NULL;
    /* The label comes from the prologue, which handle_base_info reads. */
    if (format->base_info_handled ||
        xx_sfx_ardi_diskette_image_handle_base_info(format, pd))
        label = ((xx_sfx_ardi_diskette_image *)format)->label;
    stream = (ardi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ardi_stream_free;
    state->total_records = 1;
    if (!ardi_copy_options(&state->options, options) ||
        !ardi_set_record(format, &state->current_record, &stream->context,
                         label)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfx_ardi_diskette_image_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sfx_ardi_diskette_image_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ardi_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ardi_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_sfx_ardi_diskette_image_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ardi_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ardi_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = ardi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return ardi_decode(format, &stream->context, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* The member name is the reader's own constant, never taken from the
     * file, so it needs no sanitising. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", ARDI_MEMBER_NAME)
               : xx_str_concat(base, ARDI_MEMBER_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = ardi_decode(format, &stream->context, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_ardi_diskette_image_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
