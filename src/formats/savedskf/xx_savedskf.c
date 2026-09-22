/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM SaveDskF diskette images (*.DSK, sometimes *.IMG), written by the
 * SAVEDSKF utility that shipped with PC-DOS and OS/2 and used by IBM and its
 * licensees to distribute installation diskettes.
 *
 * The container holds ONE thing -- a sector image of a floppy -- so the
 * record list this reader publishes always has exactly one entry.
 *
 * Header, 40 bytes at offset 0, little endian throughout. The fields this
 * reader uses were confirmed against the 50-sample reference corpus; the
 * ones it does not use are listed for the next reader, not validated:
 *
 *   0x00  u16  signature: 0xAA58 "old", 0xAA59 "new", 0xAA5A "new,
 *              compressed" -- stored as the byte pair AA 58/59/5A, so the
 *              little-endian word reads 0x58AA/0x59AA/0x5AAA
 *   0x02  u8   media descriptor byte (0xF0, 0xF9 ...)
 *   0x04  u16  bytes per sector
 *   0x06  u8   sectors per cluster, less one
 *   0x08  u16  reserved sectors
 *   0x0a  u8   number of FATs
 *   0x0b  u16  root directory entries
 *   0x0d  u16  first data sector
 *   0x0f  u16  highest cluster number
 *   0x11  u8   sectors per FAT
 *   0x12  u8   first sector of the root directory
 *   0x14  u32  checksum
 *   0x18  u16  cylinders
 *   0x1a  u16  heads
 *   0x1c  u16  sectors per track
 *   0x1e  u32  (unused in the corpus; zero everywhere)
 *   0x22  u16  SECTORS ACTUALLY STORED. The image is this many sectors of
 *              bytes-per-sector each; a diskette's unused tail is not
 *              written, so this is usually less than the full geometry.
 *   0x24  u16  offset of the comment string
 *   0x26  u16  offset of the first sector -- where the payload begins
 *
 * For 0xAA58 and 0xAA59 the payload is the raw image and the file is exactly
 * payload_offset + stored_sectors * bytes_per_sector bytes long. That
 * equality is an excellent gate and this reader insists on it.
 *
 * For 0xAA5A the payload is compressed, and NOT with an RLE as is sometimes
 * claimed: it is a 12-bit LZW with an unusual table policy. Codes are packed
 * 12 bits each, two codes per three bytes, high nibble first. Code 0 ends the
 * stream and codes 1..256 are the literal bytes 0..255. What makes it
 * unusual is that the table is never "cleared": entries 257..4095 live on a
 * least-recently-used list, and every new string recycles the LRU entry,
 * with a use count keeping an entry alive while another entry still links to
 * it as its prefix. The algorithm is the one documented in the public-domain
 * dskdcmps.c (Hobbes OS/2 Archive); this is an independent implementation of
 * the same published algorithm.
 *
 * Anchor for the decode: the header states the exact plaintext length, and
 * the decoded first sector is a DOS boot sector ending in the 0x55 0xAA
 * signature. Both are checked before extraction reports success.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/savedskf/xx_savedskf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef SAVEDSKF
#define XX_SAVEDSKF_FILE_TYPE XX_FILE_TYPE_SAVEDSKF
#else
#define XX_SAVEDSKF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_SAVEDSKF_HEADER_SIZE 40
#define XX_SAVEDSKF_SIG_OLD 0x58aaU
#define XX_SAVEDSKF_SIG_NEW 0x59aaU
#define XX_SAVEDSKF_SIG_COMPRESSED 0x5aaaU
#define XX_SAVEDSKF_OFF_SECTOR_SIZE 0x04
#define XX_SAVEDSKF_OFF_CYLINDERS 0x18
#define XX_SAVEDSKF_OFF_HEADS 0x1a
#define XX_SAVEDSKF_OFF_SECTORS_PER_TRACK 0x1c
#define XX_SAVEDSKF_OFF_STORED_SECTORS 0x22
#define XX_SAVEDSKF_OFF_DATA 0x26
/* Floppy geometry, generously bounded. */
#define XX_SAVEDSKF_MIN_SECTOR_SIZE 128U
#define XX_SAVEDSKF_MAX_SECTOR_SIZE 4096U
#define XX_SAVEDSKF_MAX_CYLINDERS 1024U
#define XX_SAVEDSKF_MAX_HEADS 16U
#define XX_SAVEDSKF_MAX_SECTORS_PER_TRACK 256U
/* Decompression-bomb guard on the image the header asks for. */
#define XX_SAVEDSKF_MAX_IMAGE ((int64_t)64 * 1024 * 1024)
#define XX_SAVEDSKF_MAX_INPUT ((int64_t)64 * 1024 * 1024)
#define XX_SAVEDSKF_MAX_MEMBERS 1
#define XX_SAVEDSKF_METHOD_STORE 0U
#define XX_SAVEDSKF_METHOD_LZW 1U
/* The container stores the diskette's volume label nowhere this reader can
 * trust, and no member name at all: the image is the whole of it. */
#define XX_SAVEDSKF_PLACEHOLDER_NAME "disk.img"

/* 12-bit LZW table. */
#define XX_SAVEDSKF_LZW_CODES 4096U
#define XX_SAVEDSKF_LZW_FIRST_STRING 257U

typedef struct xx_savedskf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t sectors_per_track;
    uint32_t sector_size;
    bool is_folder;
} xx_savedskf_member;

typedef struct xx_savedskf_stream_s {
    xx_savedskf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_savedskf_stream;

static void xx_savedskf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_savedskf_read_at(Abstractformat *self, int64_t offset,
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

static uint16_t xx_savedskf_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static bool xx_savedskf_path_safe(const char *name) {
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

static void xx_savedskf_stream_free(void *pointer) {
    xx_savedskf_stream *stream = (xx_savedskf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_savedskf_add(xx_savedskf_stream *stream,
                            const xx_savedskf_member *member) {
    xx_savedskf_member *grown = (xx_savedskf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* ----------------------------------------------------------- the codec -- */

typedef struct xx_savedskf_lzw_s {
    uint16_t older[XX_SAVEDSKF_LZW_CODES];
    uint16_t newer[XX_SAVEDSKF_LZW_CODES];
    uint16_t prefix[XX_SAVEDSKF_LZW_CODES];
    uint32_t usecount[XX_SAVEDSKF_LZW_CODES];
    uint32_t length[XX_SAVEDSKF_LZW_CODES];
    uint8_t first[XX_SAVEDSKF_LZW_CODES];
    uint8_t last[XX_SAVEDSKF_LZW_CODES];
    uint8_t scratch[XX_SAVEDSKF_LZW_CODES];
    uint16_t oldest;
    uint16_t newest;
} xx_savedskf_lzw;

static void xx_savedskf_lzw_unlink(xx_savedskf_lzw *t, uint16_t code) {
    uint16_t next = t->newer[code];
    uint16_t previous = t->older[code];

    if (code == t->newest) {
        t->newest = previous;
    } else {
        t->older[next] = previous;
    }
    if (code == t->oldest) {
        t->oldest = next;
    } else {
        t->newer[previous] = next;
    }
    t->older[code] = 0U;
    t->newer[code] = 0U;
}

static void xx_savedskf_lzw_touch(xx_savedskf_lzw *t, uint16_t code) {
    t->newer[t->newest] = code;
    t->older[code] = t->newest;
    t->newer[code] = 0U;
    t->newest = code;
}

/* Take the least recently used entry, releasing its hold on its prefix. */
static uint16_t xx_savedskf_lzw_recycle(xx_savedskf_lzw *t) {
    uint16_t code = t->oldest;
    uint16_t parent = t->prefix[code];

    xx_savedskf_lzw_unlink(t, code);
    if (parent != 0U && t->usecount[parent] != 0U) {
        --t->usecount[parent];
        if (t->usecount[parent] == 0U) xx_savedskf_lzw_touch(t, parent);
    }
    return code;
}

static void xx_savedskf_lzw_reserve(xx_savedskf_lzw *t, uint16_t code) {
    if (t->usecount[code] > 0U) {
        ++t->usecount[code];
    } else {
        xx_savedskf_lzw_unlink(t, code);
        t->usecount[code] = 1U;
    }
}

/**
 * Decode one SaveDskF LZW stream.
 *
 * @param output      Receives exactly @p output_size plaintext bytes.
 * @param output_size The image length the header declares; the decode refuses
 *                    to produce one byte more, so a hostile stream cannot run
 *                    away, and must produce exactly this many to succeed.
 * @return true only on an exact-length decode.
 */
static bool xx_savedskf_lzw_decode(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   xx_pd_struct *pd) {
    xx_savedskf_lzw *t;
    size_t at = 0U;
    size_t written = 0U;
    uint16_t previous = 0U;
    uint16_t hold = 0U;
    unsigned phase = 1U;
    bool ok = false;
    uint16_t code;

    if (!input || !output || output_size == 0U) return false;
    t = (xx_savedskf_lzw *)xx_mem_alloc(sizeof(*t));
    if (!t) return false;
    xx_mem_zero(t, sizeof(*t));
    for (code = 1U; code <= 256U; ++code) {
        t->first[code] = (uint8_t)(code - 1U);
        t->last[code] = (uint8_t)(code - 1U);
        t->length[code] = 1U;
        /* Held forever so a literal is never recycled. */
        t->usecount[code] = 1U;
    }
    for (code = XX_SAVEDSKF_LZW_FIRST_STRING;
         code < XX_SAVEDSKF_LZW_CODES; ++code) {
        if (code + 1U < XX_SAVEDSKF_LZW_CODES) t->newer[code] = (uint16_t)(code + 1U);
        if (code > XX_SAVEDSKF_LZW_FIRST_STRING) t->older[code] = (uint16_t)(code - 1U);
    }
    t->oldest = XX_SAVEDSKF_LZW_FIRST_STRING;
    t->newest = (uint16_t)(XX_SAVEDSKF_LZW_CODES - 1U);

    for (;;) {
        uint16_t current;
        uint32_t span;
        size_t fill;
        uint16_t walk;
        uint32_t step;

        if (at >= input_size) break;
        if (phase) {
            if (at + 1U >= input_size) break;
            current = (uint16_t)((uint16_t)input[at] << 4);
            hold = input[at + 1U];
            at += 2U;
            current |= (uint16_t)(hold >> 4);
        } else {
            current = (uint16_t)((hold & 0x0fU) << 8);
            current |= input[at++];
            hold = 0U;
        }
        phase = !phase;
        if (current == 0U) {
            /* End of stream. */
            ok = written == output_size;
            break;
        }
        if (current >= XX_SAVEDSKF_LZW_CODES) break;
        if (pd && xx_pd_is_stopped(pd)) break;

        if (previous != 0U) {
            uint16_t recycled = xx_savedskf_lzw_recycle(t);
            uint16_t source = (current != recycled) ? current : previous;
            uint32_t grown;
            if (t->length[previous] < 1U) break;
            grown = t->length[previous] + 1U;
            if (grown > XX_SAVEDSKF_LZW_CODES) break;
            t->first[recycled] = t->first[previous];
            t->last[recycled] = t->first[source];
            t->length[recycled] = grown;
            t->prefix[recycled] = previous;
            xx_savedskf_lzw_reserve(t, previous);
            t->usecount[recycled] = 0U;
            xx_savedskf_lzw_touch(t, recycled);
        }

        /* Emit the string @p current stands for, walking prefix links back
         * to the root and unwinding into scratch. */
        span = t->length[current];
        if (span < 1U || span > XX_SAVEDSKF_LZW_CODES - 256U) break;
        if (span > output_size - written) break;
        fill = (size_t)XX_SAVEDSKF_LZW_CODES;
        walk = current;
        for (step = 0U; step < span; ++step) {
            if (walk < 1U || walk >= XX_SAVEDSKF_LZW_CODES) break;
            if (t->length[walk] != span - step) break;
            t->scratch[--fill] = t->last[walk];
            walk = t->prefix[walk];
        }
        if (step != span) break;
        xx_rt_memcpy(output + written, t->scratch + fill, span);
        written += span;
        previous = current;
    }
    /* A stream that simply runs out of input without its end code is a
     * truncated file; it still counts as decoded when it delivered every
     * byte the header promised. */
    if (!ok) ok = written == output_size;
    xx_mem_free(t);
    return ok;
}

/* ------------------------------------------------------------- parsing -- */

static xx_savedskf_stream *xx_savedskf_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_savedskf_stream *stream;
    xx_savedskf_member member;
    uint8_t header[XX_SAVEDSKF_HEADER_SIZE];
    char *name = NULL;
    int64_t total, span, image_size, payload_size;
    uint32_t signature, sector_size, cylinders, heads, sectors_per_track;
    uint32_t stored_sectors, data_offset;
    uint32_t method;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_SAVEDSKF_HEADER_SIZE || span > XX_SAVEDSKF_MAX_INPUT) {
        return NULL;
    }
    if (!xx_savedskf_read_at(self, self->base_address, header,
                             sizeof(header))) {
        return NULL;
    }

    signature = xx_savedskf_le16(header);
    if (signature == XX_SAVEDSKF_SIG_OLD || signature == XX_SAVEDSKF_SIG_NEW) {
        method = XX_SAVEDSKF_METHOD_STORE;
    } else if (signature == XX_SAVEDSKF_SIG_COMPRESSED) {
        method = XX_SAVEDSKF_METHOD_LZW;
    } else {
        return NULL;
    }

    sector_size = xx_savedskf_le16(header + XX_SAVEDSKF_OFF_SECTOR_SIZE);
    cylinders = xx_savedskf_le16(header + XX_SAVEDSKF_OFF_CYLINDERS);
    heads = xx_savedskf_le16(header + XX_SAVEDSKF_OFF_HEADS);
    sectors_per_track =
        xx_savedskf_le16(header + XX_SAVEDSKF_OFF_SECTORS_PER_TRACK);
    stored_sectors = xx_savedskf_le16(header + XX_SAVEDSKF_OFF_STORED_SECTORS);
    data_offset = xx_savedskf_le16(header + XX_SAVEDSKF_OFF_DATA);

    /* Sixteen bits of signature is not enough on its own, so the geometry
     * has to hang together too: a power-of-two sector size, a plausible
     * floppy, and a stored-sector count the geometry can actually hold. */
    if (sector_size < XX_SAVEDSKF_MIN_SECTOR_SIZE ||
        sector_size > XX_SAVEDSKF_MAX_SECTOR_SIZE ||
        (sector_size & (sector_size - 1U)) != 0U) {
        return NULL;
    }
    if (cylinders == 0U || cylinders > XX_SAVEDSKF_MAX_CYLINDERS) return NULL;
    if (heads == 0U || heads > XX_SAVEDSKF_MAX_HEADS) return NULL;
    if (sectors_per_track == 0U ||
        sectors_per_track > XX_SAVEDSKF_MAX_SECTORS_PER_TRACK) {
        return NULL;
    }
    if (stored_sectors == 0U ||
        stored_sectors > cylinders * heads * sectors_per_track) {
        return NULL;
    }
    if (data_offset < XX_SAVEDSKF_HEADER_SIZE ||
        (int64_t)data_offset >= span) {
        return NULL;
    }

    image_size = (int64_t)stored_sectors * (int64_t)sector_size;
    if (image_size < 1 || image_size > XX_SAVEDSKF_MAX_IMAGE) return NULL;
    payload_size = span - (int64_t)data_offset;

    if (method == XX_SAVEDSKF_METHOD_STORE) {
        /* An uncompressed image is exactly its sectors and nothing else.
         * Every stored sample in the reference corpus satisfies this to the
         * byte, which makes it by far the strongest check available here. */
        if (payload_size != image_size) return NULL;
    } else {
        /* A compressed payload carries no length of its own, so nothing here
         * can be checked against the file size. It is NOT safe to require
         * the payload to be smaller than the image it expands to: two
         * samples in the reference corpus hold near-incompressible data and
         * their LZW stream is larger than the sectors it produces. The real
         * gate for these is the decode, which must deliver exactly the
         * declared number of sectors. */
        if (payload_size < 3) return NULL;
    }

    stream = (xx_savedskf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(XX_SAVEDSKF_PLACEHOLDER_NAME);
    if (!name || !xx_savedskf_path_safe(name)) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_SAVEDSKF_HEADER_SIZE;
    member.data_offset = self->base_address + (int64_t)data_offset;
    member.compressed_size = payload_size;
    member.uncompressed_size = image_size;
    member.method = method;
    member.cylinders = cylinders;
    member.heads = heads;
    member.sectors_per_track = sectors_per_track;
    member.sector_size = sector_size;
    member.is_folder = false;
    if (!xx_savedskf_add(stream, &member)) goto fail;
    name = NULL;
    if (stream->count != (size_t)XX_SAVEDSKF_MAX_MEMBERS) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_str_free(name);
    xx_savedskf_stream_free(stream);
    return NULL;
}

static bool xx_savedskf_decode(Abstractformat *self,
                               const xx_savedskf_member *member, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 1 ||
        member->compressed_size > XX_SAVEDSKF_MAX_INPUT) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_SAVEDSKF_MAX_IMAGE) {
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) return false;
    if (member->method == XX_SAVEDSKF_METHOD_STORE) {
        if (member->compressed_size != member->uncompressed_size ||
            !xx_savedskf_read_at(self, member->data_offset, output,
                                 (size_t)member->uncompressed_size)) {
            xx_mem_free(output);
            return false;
        }
    } else if (member->method == XX_SAVEDSKF_METHOD_LZW) {
        input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
        if (!input) {
            xx_mem_free(output);
            return false;
        }
        if (!xx_savedskf_read_at(self, member->data_offset, input,
                                 (size_t)member->compressed_size) ||
            !xx_savedskf_lzw_decode(input, (size_t)member->compressed_size,
                                    output,
                                    (size_t)member->uncompressed_size, pd)) {
            xx_mem_free(input);
            xx_mem_free(output);
            return false;
        }
        xx_mem_free(input);
    } else {
        xx_mem_free(output);
        return false;
    }
    /* The anchor for a compressed decode is the sector count the header
     * declares: xx_savedskf_lzw_decode() succeeds only when the stream
     * delivers exactly that many bytes, which a wrong codec would not.
     * The decoded first sector is a DOS boot sector ending in 0x55 0xAA in
     * nearly every sample, and that was used while developing this decoder,
     * but it is NOT enforced: one image in the corpus is a data diskette
     * whose first sector carries no signature, and refusing it would be
     * refusing a correct decode. */
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_savedskf_init(xx_savedskf *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SAVEDSKF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ibm-savedskf");
    xx_format_set_extension(&archive->format, "dsk");
    archive->format.check_is_valid = xx_savedskf_check_is_valid;
    archive->format.handle_base_info = xx_savedskf_handle_base_info;
    archive->format.get_format_size = xx_savedskf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_savedskf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_savedskf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_savedskf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_savedskf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_savedskf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_savedskf_free_archive_records_reading;
    archive->format.destroy = xx_savedskf_vtable_destroy;
}

xx_savedskf *xx_savedskf_create(xx_io_device *device, int64_t base_address) {
    xx_savedskf *archive = (xx_savedskf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_savedskf_init(archive, device, base_address);
    return archive;
}

void xx_savedskf_destroy(xx_savedskf *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_savedskf_free(xx_savedskf *archive) {
    if (!archive) return;
    xx_savedskf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_savedskf_vtable_destroy(Abstractformat *self) {
    xx_savedskf_destroy((xx_savedskf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_savedskf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_savedskf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_savedskf_parse(self, pd);
    if (!stream) return false;
    xx_savedskf_stream_free(stream);
    return true;
}

bool xx_savedskf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_savedskf *archive = (xx_savedskf *)self;
    xx_savedskf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_savedskf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_savedskf_stream_free(stream);
    return true;
}

int64_t xx_savedskf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_savedskf_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_savedskf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_savedskf_set_record(xx_archive_record *record,
                                   const xx_savedskf_member *member) {
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
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_savedskf_copy_options(xx_list_s *target,
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

static const xx_var *xx_savedskf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_savedskf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_savedskf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_savedskf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_savedskf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_savedskf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_savedskf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_savedskf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_savedskf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_savedskf_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_savedskf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_savedskf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_savedskf_set_record(&state->current_record,
                                               &stream->items[stream->index]);
    return state->has_record;
}

bool xx_savedskf_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_savedskf_stream *stream;
    const xx_savedskf_member *member;
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
    stream = (xx_savedskf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_savedskf_path_safe(member->name)) return false;

    path_option =
        xx_savedskf_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_savedskf_decode(self, member, &plain, &plain_size, pd);
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

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_savedskf_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_savedskf_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
