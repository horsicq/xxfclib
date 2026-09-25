/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Expert Witness Compression Format, version 1: EnCase / FTK Imager / linen
 * .E01 segment files and SMART .s01 files. The layout is documented in
 * xx_ewf.h; this file was written from that description (libyal's published
 * format notes), not from any implementation.
 *
 * Everything the reader does is one walk over the section chain, run in
 * three strengths:
 *
 *   PROBE   the file header, then descriptors until the volume section has
 *           given a coherent geometry. This is what detection pays for.
 *   INFO    the whole chain of every segment present, reading only fixed
 *           size headers: geometry, table entry counts, stored hashes, and
 *           where the format ends.
 *   DECODE  the same walk, but each table's entries are read and its chunks
 *           are decoded, checked and written out in order as the table is
 *           met. Chunk numbering is simply the order of the tables, so no
 *           map of the whole image is ever held in memory.
 *
 * Every descriptor must carry a correct Adler-32 and must point strictly
 * forward, so the walk cannot loop. A chunk must lie inside the section
 * that holds it and the entries of a table must increase, so chunks never
 * overlap: the output can never be more than the deflate expansion of the
 * file's own bytes, however the header describes the media.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ewf/xx_ewf.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared, so the alias macro that
 * sits next to the enumerator is tested instead. */
#ifdef EWF
#define XX_EWF_FILE_TYPE XX_FILE_TYPE_EWF
#else
#define XX_EWF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_EWF_FILE_HEADER_SIZE 13
#define XX_EWF_DESCRIPTOR_SIZE 76
#define XX_EWF_VOLUME_E01_SIZE 1052U
#define XX_EWF_VOLUME_S01_SIZE 94U
#define XX_EWF_TABLE_HEADER_SIZE 24U
#define XX_EWF_HASH_SIZE 36U
#define XX_EWF_DIGEST_SIZE 80U

/* A section is at least a descriptor and the chain only moves forward, so
 * this is never reached by a real image (a 2 TB EnCase image has a few
 * thousand sections); it only bounds the work a crafted file can ask for. */
#define XX_EWF_MAX_SECTIONS UINT32_C(0x100000)
/* The volume section follows two to five header sections. */
#define XX_EWF_PROBE_SECTIONS 16U

/* EnCase allows up to 32768 sectors per chunk; 16 MiB covers every chunk a
 * known writer produces with 512-byte sectors and bounds the two buffers
 * extraction allocates. */
#define XX_EWF_MAX_CHUNK_SIZE (UINT32_C(16) << 20)
#define XX_EWF_MAX_BYTES_PER_SECTOR UINT32_C(65536)
/* EnCase 6 writes at most 65534 entries per table. Each entry costs four
 * bytes of the section, so the section size bounds the count as well. */
#define XX_EWF_MAX_TABLE_ENTRIES UINT32_C(0x400000)

#define XX_EWF_MEMBER_NAME "disk.img"

static const uint8_t xx_ewf_signature[8] = {0x45U, 0x56U, 0x46U, 0x09U,
                                            0x0DU, 0x0AU, 0xFFU, 0x00U};

typedef enum xx_ewf_kind_e {
    XX_EWF_SECTION_OTHER = 0,
    XX_EWF_SECTION_VOLUME, /* "volume", "disk" and "data" share a layout */
    XX_EWF_SECTION_SECTORS,
    XX_EWF_SECTION_TABLE,
    XX_EWF_SECTION_TABLE2,
    XX_EWF_SECTION_HASH,
    XX_EWF_SECTION_DIGEST,
    XX_EWF_SECTION_NEXT,
    XX_EWF_SECTION_DONE
} xx_ewf_kind;

typedef enum xx_ewf_mode_e {
    XX_EWF_MODE_PROBE = 0,
    XX_EWF_MODE_INFO,
    XX_EWF_MODE_DECODE
} xx_ewf_mode;

typedef struct xx_ewf_private_s {
    int64_t input_size;
    int64_t base_address;
    int64_t format_end;          /**< Absolute end of the last section read. */
    int64_t first_data;          /**< First sectors or table section, or -1. */
    uint64_t media_size;
    uint64_t number_of_sectors;
    uint64_t table_entries;
    uint32_t number_of_chunks;
    uint32_t sectors_per_chunk;
    uint32_t bytes_per_sector;
    uint32_t chunk_size;
    uint32_t segment_count;
    uint16_t first_segment;
    uint8_t media_type;
    uint8_t media_flags;
    uint8_t compression_level;
    bool has_geometry;
    bool is_smart;
    bool finished;               /**< The chain ended in a "done" section. */
    bool is_complete;
    bool has_md5;
    bool md5_conflict;           /**< hash and digest disagree. */
    bool has_sha1;
    bool consumed;
    uint8_t md5[16];
    uint8_t sha1[20];
} xx_ewf_private;

/* A byte range of the device, absolute, end exclusive. */
typedef struct xx_ewf_range_s {
    int64_t start;
    int64_t end;
} xx_ewf_range;

/* Everything DECODE needs besides the geometry. */
typedef struct xx_ewf_decoder_s {
    xx_io_device *output;        /**< NULL: decode and verify only. */
    xx_io_device *chunk_device;  /**< Fixed memory device over chunk. */
    uint8_t *chunk;              /**< chunk_size + 4 bytes. */
    uint8_t *packed;             /**< packed_capacity bytes. */
    size_t packed_capacity;
    uint64_t next_chunk;         /**< Index of the next chunk to produce. */
    xx_hash_context md5;
} xx_ewf_decoder;

static void xx_ewf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* Every read goes through xx_io_seek64: images are routinely larger than
 * 2 GB and long is 32 bits on Win64. */
static bool xx_ewf_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;

    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_ewf_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return total_size >= 0 && offset >= 0 && size >= 0 &&
           offset <= total_size && size <= total_size - offset;
}

static bool xx_ewf_write_all(xx_io_device *output, const uint8_t *data,
                             size_t size) {
    size_t done = 0U;

    while (done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) return false;
        done += (size_t)sent;
    }
    return true;
}

static bool xx_ewf_is_zero(const uint8_t *data, size_t size) {
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (data[index] != 0U) return false;
    }
    return true;
}

/* The type field is a NUL-padded name. Only the names the walk acts on are
 * told apart; "header", "header2", "error2", "session" and anything newer
 * are stepped over. */
static xx_ewf_kind xx_ewf_classify(const uint8_t *type) {
    static const struct {
        const char *name;
        xx_ewf_kind kind;
    } names[] = {
        {"volume", XX_EWF_SECTION_VOLUME}, {"disk", XX_EWF_SECTION_VOLUME},
        {"data", XX_EWF_SECTION_VOLUME},   {"sectors", XX_EWF_SECTION_SECTORS},
        {"table", XX_EWF_SECTION_TABLE},   {"table2", XX_EWF_SECTION_TABLE2},
        {"hash", XX_EWF_SECTION_HASH},     {"digest", XX_EWF_SECTION_DIGEST},
        {"next", XX_EWF_SECTION_NEXT},     {"done", XX_EWF_SECTION_DONE}};
    size_t index;

    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *name = names[index].name;
        size_t length = xx_str_len(name);
        if (xx_rt_memcmp(type, name, length) == 0 &&
            xx_ewf_is_zero(type + length, 16U - length)) {
            return names[index].kind;
        }
    }
    return XX_EWF_SECTION_OTHER;
}

/* Read the 13-byte file header at offset. */
static bool xx_ewf_read_file_header(xx_io_device *device, int64_t input_size,
                                    int64_t offset, uint16_t *segment) {
    uint8_t header[XX_EWF_FILE_HEADER_SIZE];

    if (!xx_ewf_range_within(input_size, offset, XX_EWF_FILE_HEADER_SIZE) ||
        !xx_ewf_read_at(device, offset, header, sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header, xx_ewf_signature, sizeof(xx_ewf_signature)) != 0 ||
        header[8] != 0x01U || header[11] != 0U || header[12] != 0U) {
        return false;
    }
    *segment = xx_data_get_u16(header, sizeof(header), 9U, false);
    return *segment != 0U;
}

typedef struct xx_ewf_descriptor_s {
    xx_ewf_kind kind;
    uint64_t next;               /**< From the segment start. */
    uint64_t size;
} xx_ewf_descriptor;

static bool xx_ewf_read_descriptor(xx_io_device *device, int64_t input_size,
                                   int64_t offset, xx_ewf_descriptor *out) {
    uint8_t raw[XX_EWF_DESCRIPTOR_SIZE];

    if (!xx_ewf_range_within(input_size, offset, XX_EWF_DESCRIPTOR_SIZE) ||
        !xx_ewf_read_at(device, offset, raw, sizeof(raw))) {
        return false;
    }
    if (xx_adler32(raw, 72U) != xx_data_get_u32(raw, sizeof(raw), 72U, false)) {
        return false;
    }
    out->kind = xx_ewf_classify(raw);
    out->next = xx_data_get_u64(raw, sizeof(raw), 16U, false);
    out->size = xx_data_get_u64(raw, sizeof(raw), 24U, false);
    return true;
}

/* A stored checksum of zero is accepted besides the right one: some
 * acquisition tools leave the volume and hash checksums unset. */
static bool xx_ewf_checksum_ok(const uint8_t *data, size_t covered,
                               uint32_t stored) {
    return stored == 0U || xx_adler32(data, covered) == stored;
}

/* ------------------------------------------------------------- sections -- */

static bool xx_ewf_read_volume(xx_io_device *device, xx_ewf_private *parsed,
                               int64_t data_at, uint64_t data_size) {
    uint8_t volume[XX_EWF_VOLUME_E01_SIZE];
    uint64_t sectors;
    uint64_t chunks;
    uint32_t declared;
    uint32_t spc;
    uint32_t bps;

    if (data_size >= XX_EWF_VOLUME_E01_SIZE) {
        if (!xx_ewf_read_at(device, data_at, volume, XX_EWF_VOLUME_E01_SIZE) ||
            !xx_ewf_checksum_ok(volume, 1048U,
                                xx_data_get_u32(volume, sizeof(volume), 1048U,
                                                false))) {
            return false;
        }
        sectors = xx_data_get_u64(volume, sizeof(volume), 16U, false);
        parsed->media_type = volume[0];
        parsed->media_flags = volume[36];
        parsed->compression_level = volume[52];
        parsed->is_smart = false;
    } else if (data_size >= XX_EWF_VOLUME_S01_SIZE) {
        if (!xx_ewf_read_at(device, data_at, volume, XX_EWF_VOLUME_S01_SIZE) ||
            !xx_ewf_checksum_ok(volume, 90U,
                                xx_data_get_u32(volume, sizeof(volume), 90U,
                                                false))) {
            return false;
        }
        sectors = xx_data_get_u32(volume, sizeof(volume), 16U, false);
        parsed->media_type = 0U;
        parsed->media_flags = 0U;
        parsed->compression_level = 0U;
        parsed->is_smart = true;
    } else {
        return false;
    }
    declared = xx_data_get_u32(volume, sizeof(volume), 4U, false);
    spc = xx_data_get_u32(volume, sizeof(volume), 8U, false);
    bps = xx_data_get_u32(volume, sizeof(volume), 12U, false);
    if (spc == 0U || bps == 0U || bps > XX_EWF_MAX_BYTES_PER_SECTOR ||
        spc > XX_EWF_MAX_CHUNK_SIZE / bps) {
        return false;
    }
    if (sectors > (uint64_t)INT64_MAX / bps) return false;
    /* The chunk count is redundant with the sector count; requiring the two
     * to agree is what makes a stray match of the signature fail here. */
    chunks = sectors / spc + (sectors % spc != 0U ? 1U : 0U);
    if (chunks != (uint64_t)declared) return false;
    parsed->number_of_chunks = declared;
    parsed->sectors_per_chunk = spc;
    parsed->bytes_per_sector = bps;
    parsed->chunk_size = spc * bps;
    parsed->number_of_sectors = sectors;
    parsed->media_size = sectors * bps;
    parsed->has_geometry = true;
    return true;
}

static void xx_ewf_read_hashes(xx_io_device *device, xx_ewf_private *parsed,
                               xx_ewf_kind kind, int64_t data_at,
                               uint64_t data_size) {
    uint8_t data[XX_EWF_DIGEST_SIZE];

    /* A damaged hash section is not a reason to refuse the image; the
     * hashes are simply not used. */
    if (kind == XX_EWF_SECTION_HASH) {
        if (data_size < XX_EWF_HASH_SIZE ||
            !xx_ewf_read_at(device, data_at, data, XX_EWF_HASH_SIZE) ||
            !xx_ewf_checksum_ok(data, 32U,
                                xx_data_get_u32(data, sizeof(data), 32U,
                                                false))) {
            return;
        }
    } else {
        if (data_size < XX_EWF_DIGEST_SIZE ||
            !xx_ewf_read_at(device, data_at, data, XX_EWF_DIGEST_SIZE) ||
            !xx_ewf_checksum_ok(data, 76U,
                                xx_data_get_u32(data, sizeof(data), 76U,
                                                false))) {
            return;
        }
        if (!xx_ewf_is_zero(data + 16, 20U)) {
            xx_rt_memcpy(parsed->sha1, data + 16, 20U);
            parsed->has_sha1 = true;
        }
    }
    if (!xx_ewf_is_zero(data, 16U)) {
        /* An image carrying two MD5s that disagree cannot be verified. */
        if (parsed->has_md5 && xx_rt_memcmp(parsed->md5, data, 16U) != 0) {
            parsed->md5_conflict = true;
        }
        xx_rt_memcpy(parsed->md5, data, 16U);
        parsed->has_md5 = true;
    }
}

/* The fixed part of a table section. */
typedef struct xx_ewf_table_s {
    uint32_t entries;
    uint64_t base;
    int64_t entries_at;          /**< Absolute offset of entry 0. */
    int64_t trailing_start;      /**< After the entry array and its footer. */
    int64_t section_end;
    bool has_footer;
} xx_ewf_table;

static bool xx_ewf_read_table_header(xx_io_device *device, int64_t data_at,
                                     uint64_t data_size, xx_ewf_table *table) {
    uint8_t header[XX_EWF_TABLE_HEADER_SIZE];
    uint64_t array_size;

    if (data_size < XX_EWF_TABLE_HEADER_SIZE ||
        !xx_ewf_read_at(device, data_at, header, sizeof(header)) ||
        xx_adler32(header, 20U) !=
            xx_data_get_u32(header, sizeof(header), 20U, false)) {
        return false;
    }
    table->entries = xx_data_get_u32(header, sizeof(header), 0U, false);
    table->base = xx_data_get_u64(header, sizeof(header), 8U, false);
    if (table->entries > XX_EWF_MAX_TABLE_ENTRIES ||
        table->base > (uint64_t)INT64_MAX) {
        return false;
    }
    array_size = (uint64_t)table->entries * 4U;
    if (array_size > data_size - XX_EWF_TABLE_HEADER_SIZE) return false;
    table->entries_at = data_at + (int64_t)XX_EWF_TABLE_HEADER_SIZE;
    table->section_end = data_at + (int64_t)data_size;
    table->has_footer =
        data_size - XX_EWF_TABLE_HEADER_SIZE - array_size >= 4U;
    table->trailing_start = table->entries_at + (int64_t)array_size +
                            (table->has_footer ? 4 : 0);
    return true;
}

/* ------------------------------------------------------------- decoding -- */

/* Produce one chunk of expected bytes from the packed extent [at, end). */
static bool xx_ewf_decode_chunk(xx_io_device *device,
                                const xx_ewf_private *parsed,
                                xx_ewf_decoder *decoder, int64_t at,
                                int64_t end, bool compressed,
                                size_t expected, xx_pd_struct *pd) {
    int64_t length = end - at;
    size_t consumed = 0U;
    int64_t produced;
    size_t trailer;

    if (length <= 0 || (uint64_t)length > decoder->packed_capacity) {
        return false;
    }
    if (!compressed) {
        /* The plain bytes and their Adler-32. The final chunk carries only
         * what is left of the media; a writer that pads it to a whole chunk
         * is accepted as well. */
        size_t stored = (size_t)length >= (size_t)parsed->chunk_size + 4U
                            ? (size_t)parsed->chunk_size
                            : expected;
        if ((size_t)length < stored + 4U) return false;
        if (!xx_ewf_read_at(device, at, decoder->chunk, stored + 4U)) {
            return false;
        }
        return xx_adler32(decoder->chunk, stored) ==
               xx_data_get_u32(decoder->chunk, stored + 4U, stored, false);
    }
    if (length < 6 ||
        !xx_ewf_read_at(device, at, decoder->packed, (size_t)length) ||
        !xx_zlib_stream_header_is_valid(decoder->packed, (size_t)length) ||
        xx_io_seek64(decoder->chunk_device, 0, SEEK_SET) != 0) {
        return false;
    }
    /* The memory device is exactly one chunk long, so a stream that would
     * inflate past a chunk fails on the write instead of growing anything. */
    if (!xx_deflate_unpack_memory_to_device_ex(
            decoder->packed + 2, (size_t)length - 2U, decoder->chunk_device,
            &consumed, false, pd)) {
        return false;
    }
    produced = xx_io_tell(decoder->chunk_device);
    if (produced < 0 || (uint64_t)produced < (uint64_t)expected) return false;
    /* The zlib trailer is the only integrity check a compressed chunk has. */
    trailer = 2U + consumed;
    if (trailer > (size_t)length || (size_t)length - trailer < 4U) return false;
    return xx_data_get_u32(decoder->packed, (size_t)length, trailer, true) ==
           xx_adler32(decoder->chunk, (size_t)produced);
}

/* Where a table entry puts its chunk. EnCase 6.7.1 let a sectors section
 * grow past 2 GiB and then used bit 31 as an offset bit. A masked offset
 * that would step backwards while the full value steps forward is that
 * case: from there on the whole value is the offset and the chunks are
 * stored, never compressed. */
static int64_t xx_ewf_entry_offset(uint32_t value, int64_t origin,
                                   int64_t previous, bool *overflow,
                                   bool *compressed) {
    int64_t masked = origin + (int64_t)(value & 0x7FFFFFFFU);
    int64_t full = origin + (int64_t)value;

    if (!*overflow && (value & 0x80000000U) != 0U && previous >= 0 &&
        masked <= previous && full > previous) {
        *overflow = true;
    }
    *compressed = !*overflow && (value & 0x80000000U) != 0U;
    return *overflow ? full : masked;
}

/* Decode every chunk a table lists. Returns false on any damage; *entries_ok
 * is cleared when only the entry array's checksum failed, which lets the
 * caller fall back to table2 before anything has been written. */
static bool xx_ewf_decode_table(xx_io_device *device,
                                const xx_ewf_private *parsed,
                                xx_ewf_decoder *decoder,
                                const xx_ewf_table *table, int64_t segment_base,
                                const xx_ewf_range *sectors,
                                const xx_ewf_range *in_table, bool *entries_ok,
                                xx_pd_struct *pd) {
    uint8_t *raw = NULL;
    size_t array_size = (size_t)table->entries * 4U;
    const xx_ewf_range *region = NULL;
    int64_t origin;
    int64_t previous = -1;
    bool overflow = false;
    bool result = false;
    uint32_t index;

    *entries_ok = true;
    if (table->entries == 0U) return true;
    /* origin plus any u32 entry has to stay representable. */
    if (table->base > (uint64_t)INT64_MAX - UINT64_C(0x100000000) ||
        (uint64_t)segment_base >
            (uint64_t)INT64_MAX - UINT64_C(0x100000000) - table->base) {
        return false;
    }
    origin = segment_base + (int64_t)table->base;
    raw = (uint8_t *)xx_mem_alloc(array_size + 4U);
    if (!raw) return false;
    if (!xx_ewf_read_at(device, table->entries_at, raw,
                        array_size + (table->has_footer ? 4U : 0U))) {
        goto done;
    }
    if (table->has_footer &&
        xx_adler32(raw, array_size) !=
            xx_data_get_u32(raw, array_size + 4U, array_size, false)) {
        *entries_ok = false;
        goto done;
    }

    for (index = 0U; index < table->entries; ++index) {
        uint32_t value = xx_data_get_u32(raw, array_size, (size_t)index * 4U,
                                         false);
        bool compressed;
        int64_t at;
        int64_t end;
        uint64_t left;
        size_t expected;

        if (pd && xx_pd_is_stopped(pd)) goto done;
        at = xx_ewf_entry_offset(value, origin, previous, &overflow,
                                 &compressed);
        if (at <= previous) goto done;
        if (!region) {
            if (sectors && at >= sectors->start && at < sectors->end) {
                region = sectors;
            } else if (in_table && at >= in_table->start &&
                       at < in_table->end) {
                region = in_table;
            } else {
                goto done;
            }
        }
        if (at < region->start || at >= region->end) goto done;
        if (index + 1U < table->entries) {
            bool peek_overflow = overflow;
            bool peek_compressed;
            end = xx_ewf_entry_offset(
                xx_data_get_u32(raw, array_size, (size_t)(index + 1U) * 4U,
                                false),
                origin, at, &peek_overflow, &peek_compressed);
            if (end > region->end) goto done;
        } else {
            end = region->end;
        }
        if (end <= at) goto done;

        if (decoder->next_chunk >= (uint64_t)parsed->number_of_chunks) {
            goto done;
        }
        left = parsed->media_size -
               decoder->next_chunk * (uint64_t)parsed->chunk_size;
        expected = left < (uint64_t)parsed->chunk_size
                       ? (size_t)left
                       : (size_t)parsed->chunk_size;
        if (!xx_ewf_decode_chunk(device, parsed, decoder, at, end, compressed,
                                 expected, pd)) {
            goto done;
        }
        xx_hash_update(&decoder->md5, decoder->chunk, expected);
        if (decoder->output &&
            !xx_ewf_write_all(decoder->output, decoder->chunk, expected)) {
            goto done;
        }
        ++decoder->next_chunk;
        previous = at;
    }
    result = true;
done:
    xx_mem_free(raw);
    return result;
}

/* ----------------------------------------------------------------- walk -- */

static bool xx_ewf_walk(Abstractformat *self, xx_ewf_private *parsed,
                        xx_ewf_mode mode, xx_ewf_decoder *decoder,
                        xx_pd_struct *pd) {
    xx_io_device *device = self->device;
    int64_t segment_base = self->base_address;
    int64_t at;
    uint16_t segment = 0U;
    uint32_t sections = 0U;
    xx_ewf_range sectors = {-1, -1};
    xx_ewf_range failed_region = {-1, -1};
    bool have_sectors = false;
    bool table_pending = false;  /* table failed; table2 must stand in */

    if (!xx_ewf_read_file_header(device, parsed->input_size, segment_base,
                                 &segment)) {
        return false;
    }
    parsed->first_segment = segment;
    parsed->segment_count = 1U;
    at = segment_base + XX_EWF_FILE_HEADER_SIZE;

    for (;;) {
        xx_ewf_descriptor descriptor;
        int64_t relative = at - segment_base;
        int64_t data_at = at + XX_EWF_DESCRIPTOR_SIZE;
        uint64_t size;
        uint64_t data_size;
        int64_t next;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (++sections > XX_EWF_MAX_SECTIONS) return false;
        if (mode == XX_EWF_MODE_PROBE && sections > XX_EWF_PROBE_SECTIONS) {
            return false;
        }
        if (!xx_ewf_read_descriptor(device, parsed->input_size, at,
                                    &descriptor)) {
            return false;
        }

        if (descriptor.kind == XX_EWF_SECTION_NEXT ||
            descriptor.kind == XX_EWF_SECTION_DONE) {
            int64_t end = at + XX_EWF_DESCRIPTOR_SIZE;
            uint16_t following = 0U;

            /* Both point at themselves. Pointing just past the descriptor,
             * as an ordinary section would, is tolerated too. Their size
             * is 0 (EnCase) or 76 (FTK Imager before 2.9) and is not used:
             * nothing follows them inside the segment. */
            if (descriptor.next != (uint64_t)relative &&
                descriptor.next != (uint64_t)relative + XX_EWF_DESCRIPTOR_SIZE) {
                return false;
            }
            if (mode == XX_EWF_MODE_PROBE) return false;
            if (table_pending) return false;
            parsed->format_end = end;
            if (descriptor.kind == XX_EWF_SECTION_DONE) {
                parsed->finished = true;
                break;
            }
            /* A set presented back to back continues with the next segment
             * right here; anything else ends the walk short of the media. */
            if (segment == UINT16_MAX ||
                !xx_ewf_read_file_header(device, parsed->input_size, end,
                                         &following) ||
                following != (uint16_t)(segment + 1U)) {
                break;
            }
            segment = following;
            segment_base = end;
            ++parsed->segment_count;
            have_sectors = false;
            at = segment_base + XX_EWF_FILE_HEADER_SIZE;
            continue;
        }

        size = descriptor.size;
        next = 0;
        if (descriptor.next > (uint64_t)INT64_MAX - (uint64_t)segment_base) {
            return false;
        }
        next = segment_base + (int64_t)descriptor.next;
        /* Some writers leave the size of a section zero; the chain still
         * says where it ends. */
        if (size == 0U) size = (uint64_t)(next > at ? next - at : 0);
        if (size < XX_EWF_DESCRIPTOR_SIZE || next <= at ||
            size > (uint64_t)(next - at) ||
            !xx_ewf_range_within(parsed->input_size, next,
                                 XX_EWF_DESCRIPTOR_SIZE)) {
            return false;
        }
        data_size = size - XX_EWF_DESCRIPTOR_SIZE;

        switch (descriptor.kind) {
            case XX_EWF_SECTION_VOLUME:
                if (!parsed->has_geometry) {
                    if (!xx_ewf_read_volume(device, parsed, data_at, data_size)) {
                        return false;
                    }
                    if (mode == XX_EWF_MODE_PROBE) return true;
                }
                break;
            case XX_EWF_SECTION_SECTORS:
                /* The chunks run up to the next section; the chain is the
                 * one thing every writer gets right, the size field is
                 * not (Expert Witness 1.35 leaves it zero). */
                sectors.start = data_at;
                sectors.end = next;
                have_sectors = true;
                if (parsed->first_data < 0) parsed->first_data = at;
                break;
            case XX_EWF_SECTION_TABLE:
            case XX_EWF_SECTION_TABLE2: {
                xx_ewf_table table;
                xx_ewf_range in_table;
                bool header_ok;
                bool entries_ok = true;

                if (descriptor.kind == XX_EWF_SECTION_TABLE2 && !table_pending) {
                    break; /* the mirror of a table that was fine */
                }
                /* A damaged table not followed by its table2 loses chunks. */
                if (descriptor.kind == XX_EWF_SECTION_TABLE && table_pending) {
                    return false;
                }
                if (!parsed->has_geometry) return false;
                if (parsed->first_data < 0) parsed->first_data = at;
                header_ok = xx_ewf_read_table_header(device, data_at, data_size,
                                                     &table);
                if (descriptor.kind == XX_EWF_SECTION_TABLE2) {
                    /* table2 describes the chunks where table put them. */
                    table_pending = false;
                    if (!header_ok) return false;
                    in_table = failed_region;
                } else {
                    in_table.start = header_ok ? table.trailing_start : -1;
                    in_table.end = header_ok ? table.section_end : -1;
                    failed_region = in_table;
                    if (!header_ok) {
                        table_pending = true;
                        break;
                    }
                }
                if (mode == XX_EWF_MODE_INFO) {
                    parsed->table_entries += table.entries;
                    break;
                }
                if (!xx_ewf_decode_table(device, parsed, decoder, &table,
                                         segment_base,
                                         have_sectors ? &sectors : NULL,
                                         in_table.start < in_table.end
                                             ? &in_table
                                             : NULL,
                                         &entries_ok, pd)) {
                    if (entries_ok ||
                        descriptor.kind == XX_EWF_SECTION_TABLE2) {
                        return false;
                    }
                    table_pending = true;
                    break;
                }
                parsed->table_entries += table.entries;
                break;
            }
            case XX_EWF_SECTION_HASH:
            case XX_EWF_SECTION_DIGEST:
                if (mode != XX_EWF_MODE_PROBE) {
                    xx_ewf_read_hashes(device, parsed, descriptor.kind, data_at,
                                       data_size);
                }
                break;
            default:
                break;
        }
        at = next;
    }
    if (!parsed->has_geometry) return false;
    parsed->is_complete = parsed->finished && parsed->first_segment == 1U &&
                          parsed->table_entries ==
                              (uint64_t)parsed->number_of_chunks;
    return true;
}

/* ---------------------------------------------------------------- parse -- */

static void xx_ewf_private_reset(xx_ewf_private *parsed) {
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->first_data = -1;
}

static bool xx_ewf_parse(Abstractformat *self, xx_ewf_private *parsed,
                         xx_ewf_mode mode, xx_ewf_decoder *decoder,
                         xx_pd_struct *pd) {
    if (parsed) xx_ewf_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    parsed->base_address = self->base_address;
    if (!xx_ewf_range_within(parsed->input_size, self->base_address,
                             XX_EWF_FILE_HEADER_SIZE + XX_EWF_DESCRIPTOR_SIZE)) {
        return false;
    }
    return xx_ewf_walk(self, parsed, mode, decoder, pd);
}

/* Decode the media into output (NULL verifies only). */
static bool xx_ewf_write_image(Abstractformat *self, xx_io_device *output,
                               xx_pd_struct *pd) {
    xx_ewf_private parsed;
    xx_ewf_private info;
    xx_ewf_decoder decoder;
    uint8_t digest[16];
    bool result = false;

    /* The INFO walk first: it is cheap, and it refuses an incomplete set
     * before a single byte is written. */
    if (!xx_ewf_parse(self, &info, XX_EWF_MODE_INFO, NULL, pd) ||
        !info.is_complete) {
        return false;
    }
    xx_mem_zero(&decoder, sizeof(decoder));
    decoder.output = output;
    /* No packed chunk can be longer than the device it sits in. */
    decoder.packed_capacity = (size_t)info.chunk_size * 2U + 1024U;
    if ((uint64_t)decoder.packed_capacity > (uint64_t)info.input_size) {
        decoder.packed_capacity = (size_t)info.input_size;
    }
    /* Four more than a chunk: a stored chunk is read with its checksum. */
    decoder.chunk = (uint8_t *)xx_mem_alloc((size_t)info.chunk_size + 4U);
    decoder.packed = (uint8_t *)xx_mem_alloc(decoder.packed_capacity);
    if (!decoder.chunk || !decoder.packed ||
        !xx_hash_init(&decoder.md5, XX_HASH_MD5)) {
        goto done;
    }
    decoder.chunk_device = xx_io_mem_open(decoder.chunk, info.chunk_size);
    if (!decoder.chunk_device) goto done;
    if (!xx_ewf_parse(self, &parsed, XX_EWF_MODE_DECODE, &decoder, pd) ||
        !parsed.is_complete ||
        decoder.next_chunk != (uint64_t)parsed.number_of_chunks) {
        goto done;
    }
    if (!xx_hash_final(&decoder.md5, digest, sizeof(digest))) goto done;
    result = !parsed.md5_conflict &&
             (!parsed.has_md5 ||
              xx_rt_memcmp(digest, parsed.md5, sizeof(digest)) == 0);
done:
    if (decoder.chunk_device) xx_io_close(decoder.chunk_device);
    if (decoder.chunk) xx_mem_free(decoder.chunk);
    if (decoder.packed) xx_mem_free(decoder.packed);
    return result;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_ewf_init(xx_ewf *ewf, xx_io_device *dev, int64_t base_address) {
    if (!ewf) return;
    xx_mem_zero(ewf, sizeof(*ewf));
    xx_format_init(&ewf->format, dev, base_address);
    ewf->format.endian = XX_ENDIAN_LITTLE;
    ewf->format.file_type = XX_EWF_FILE_TYPE;
    ewf->format.format_type = XX_TYPE_ARCHIVE;
    ewf->format.is_archive = true;
    xx_format_set_mime_type(&ewf->format, "application/x-ewf");
    xx_format_set_extension(&ewf->format, "E01");
    ewf->format.check_is_valid = xx_ewf_check_is_valid;
    ewf->format.handle_base_info = xx_ewf_handle_base_info;
    ewf->format.get_format_size = xx_ewf_get_format_size;
    ewf->format.get_number_of_archive_records =
        xx_ewf_get_number_of_archive_records;
    ewf->format.create_archive_records_reading =
        xx_ewf_create_archive_records_reading;
    ewf->format.get_current_archive_record = xx_ewf_get_current_archive_record;
    ewf->format.unpack_current_archive_record =
        xx_ewf_unpack_current_archive_record;
    ewf->format.archive_record_move_to_next = xx_ewf_archive_record_move_to_next;
    ewf->format.free_archive_records_reading =
        xx_ewf_free_archive_records_reading;
    ewf->format.destroy = xx_ewf_vtable_destroy;
}

xx_ewf *xx_ewf_create(xx_io_device *dev, int64_t base_address) {
    xx_ewf *ewf = (xx_ewf *)xx_mem_alloc(sizeof(*ewf));

    if (ewf) xx_ewf_init(ewf, dev, base_address);
    return ewf;
}

void xx_ewf_destroy(xx_ewf *ewf) {
    if (!ewf) return;
    if (ewf->internal) {
        xx_mem_free(ewf->internal);
        ewf->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&ewf->format);
}

static void xx_ewf_vtable_destroy(Abstractformat *self) {
    xx_ewf_destroy((xx_ewf *)self);
}

void xx_ewf_free(xx_ewf *ewf) {
    if (!ewf) return;
    xx_ewf_destroy(ewf);
    xx_mem_free(ewf);
}

/* --------------------------------------------------------------- format -- */

bool xx_ewf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ewf_private parsed;

    return xx_ewf_parse(self, &parsed, XX_EWF_MODE_PROBE, NULL, pd);
}

bool xx_ewf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ewf *ewf = (xx_ewf *)self;
    xx_ewf_private *parsed;

    if (!self) return false;
    parsed = (xx_ewf_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_ewf_parse(self, parsed, XX_EWF_MODE_INFO, NULL, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (ewf->internal) xx_mem_free(ewf->internal);
    ewf->internal = parsed;
    ewf->number_of_records = 1U;
    ewf->media_size = parsed->media_size;
    ewf->number_of_sectors = parsed->number_of_sectors;
    ewf->table_entries = parsed->table_entries;
    ewf->number_of_chunks = parsed->number_of_chunks;
    ewf->sectors_per_chunk = parsed->sectors_per_chunk;
    ewf->bytes_per_sector = parsed->bytes_per_sector;
    ewf->chunk_size = parsed->chunk_size;
    ewf->segment_count = parsed->segment_count;
    ewf->first_segment = parsed->first_segment;
    ewf->media_type = parsed->media_type;
    ewf->media_flags = parsed->media_flags;
    ewf->compression_level = parsed->compression_level;
    ewf->is_smart = parsed->is_smart;
    ewf->is_complete = parsed->is_complete;
    ewf->has_md5 = parsed->has_md5;
    ewf->has_sha1 = parsed->has_sha1;
    xx_rt_memcpy(ewf->md5, parsed->md5, sizeof(ewf->md5));
    xx_rt_memcpy(ewf->sha1, parsed->sha1, sizeof(ewf->sha1));
    self->format_size = parsed->format_end - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    if (parsed->format_end < parsed->input_size) {
        self->overlay_offset = parsed->format_end;
        self->overlay_size = parsed->input_size - parsed->format_end;
    }
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_ewf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_ewf_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_ewf *)self)->number_of_records;
}

/* -------------------------------------------------------------- records -- */

static bool xx_ewf_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;

    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
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

static const xx_var *xx_ewf_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_ewf_populate_record(xx_archive_record *record,
                                   const xx_ewf_private *parsed) {
    char comment[96];
    char hex[48];
    size_t used = 0U;

    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->base_address;
    record->header_size = XX_EWF_FILE_HEADER_SIZE;
    /* The media is spread over the sectors sections of every segment; the
     * record points at the first of them. */
    record->data_offset = parsed->first_data;
    record->compressed_size = parsed->format_end - parsed->base_address;
    if (!xx_archive_record_set_original_name(record, XX_EWF_MEMBER_NAME) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        parsed->media_size) ||
        !xx_archive_record_set_meta_u64(
            record, XX_META_ID_COMPRESSED_SIZE,
            (uint64_t)(parsed->format_end - parsed->base_address)) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_LEVEL,
                                        parsed->compression_level) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false)) {
        return false;
    }
    comment[0] = '\0';
    if (parsed->has_md5 && xx_hash_to_hex(parsed->md5, 16U, hex, sizeof(hex))) {
        used = (size_t)xx_rt_snprintf(comment, sizeof(comment), "MD5 %s", hex);
    }
    if (parsed->has_sha1 && used < sizeof(comment) &&
        xx_hash_to_hex(parsed->sha1, 20U, hex, sizeof(hex))) {
        (void)xx_rt_snprintf(comment + used, sizeof(comment) - used, "%sSHA1 %s",
                             used ? "; " : "", hex);
    }
    if (comment[0] != '\0' &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, comment)) {
        return false;
    }
    return true;
}

xx_archive_record_state *xx_ewf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_ewf_private *parsed;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_ewf_private *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = parsed;
    state->free_internal = xx_mem_free;
    if (!xx_ewf_copy_options(&state->options, options) ||
        !xx_ewf_parse(self, parsed, XX_EWF_MODE_INFO, NULL, pd)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->total_records = 1;
    if (!xx_ewf_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ewf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ewf_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_ewf_private *parsed;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* One member, so the first step is always the last. */
    parsed = (xx_ewf_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_ewf_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_ewf_private *parsed;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    bool result;
    bool created = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_ewf_private *)state->internal_state;
    if (!parsed || parsed->consumed || !parsed->is_complete) return false;

    option = xx_ewf_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return xx_ewf_write_image(self, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) {
        if (owned_base) xx_str_free(owned_base);
        return false;
    }
    if (base[0] != '\0' && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", XX_EWF_MEMBER_NAME);
    } else {
        destination = xx_str_concat(base, XX_EWF_MEMBER_NAME);
    }
    if (owned_base) xx_str_free(owned_base);
    if (!destination) return false;
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    result = output != NULL && xx_ewf_write_image(self, output, pd);
    if (output && xx_io_close(output) != 0) result = false;
    if (!result && created) xx_rt_remove(destination);
    xx_str_free(destination);
    return result;
}

void xx_ewf_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

bool xx_ewf_unpack_to_device(xx_ewf *ewf, xx_io_device *output,
                             xx_pd_struct *pd) {
    if (!ewf || !ewf->format.device) return false;
    return xx_ewf_write_image(&ewf->format, output, pd);
}

uint64_t xx_ewf_get_media_size(const xx_ewf *ewf) {
    return ewf ? ewf->media_size : 0U;
}
uint32_t xx_ewf_get_chunk_size(const xx_ewf *ewf) {
    return ewf ? ewf->chunk_size : 0U;
}
uint32_t xx_ewf_get_bytes_per_sector(const xx_ewf *ewf) {
    return ewf ? ewf->bytes_per_sector : 0U;
}
bool xx_ewf_is_complete(const xx_ewf *ewf) {
    return ewf ? ewf->is_complete : false;
}
