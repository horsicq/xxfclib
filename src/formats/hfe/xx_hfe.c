/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the HxC Floppy Emulator (HFE v1) image.  The container
 * stores raw bit cells, so the member is produced by decoding MFM: address
 * marks, sector headers and data fields, each checked against its CRC.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hfe/xx_hfe.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef HFE
#define XX_HFE_FILE_TYPE XX_FILE_TYPE_HFE
#else
#define XX_HFE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define HFE_MAX_MEMBERS 65536U
#define HFE_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct hfe_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;      /* 0 = stored, non-zero = format codec */
    bool decode;
} hfe_member;

typedef struct hfe_stream_s {
    hfe_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} hfe_stream;

static uint16_t hfe_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t hfe_le32(const uint8_t *b) {
    return (uint32_t)hfe_le16(b) | ((uint32_t)hfe_le16(b + 2U) << 16U);
}

static bool hfe_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction.  The helper only has to be CRT free. */
static char *hfe_make_name(const char *prefix, int a, int b,
                           const char *suffix) {
    char buffer[64];
    size_t used = 0U;
    size_t index;
    char *result;
    for (index = 0U; prefix && prefix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[index];
    }
    if (a >= 0) {
        char digits[8];
        size_t count = 0U;
        int value = a;
        do {
            digits[count++] = (char)('0' + (value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 2U) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    if (b >= 0) {
        if (used >= sizeof(buffer) - 2U) return NULL;
        buffer[used++] = '_';
        buffer[used++] = (char)('0' + (b % 10));
    }
    for (index = 0U; suffix && suffix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[index];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

static void hfe_stream_free(void *opaque) {
    hfe_stream *stream = (hfe_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool hfe_add_member(hfe_stream *stream, const hfe_member *member) {
    hfe_member *grown;
    if (!stream || !member || stream->count >= HFE_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (hfe_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define HFE_HEADER_SIZE 512
#define HFE_BLOCK 512
#define HFE_CHUNK 256
#define HFE_MAX_TRACKS 256
#define HFE_MAX_SECTORS 256
#define HFE_MAX_SYNCS 65536U
#define HFE_SYNC_A1 0x4489U

typedef struct hfe_geometry_s {
    int32_t tracks;
    int32_t sides;
    int32_t sectors_per_track;
    int32_t sector_size;
    int64_t lut_offset;
    uint64_t image_size;
} hfe_geometry;

/* "HXCPICFE", a zero revision byte, the track and side counts, then a lookup
 * table whose 512-byte-block offset lives at +0x12.  Each LUT entry is a
 * block offset and a byte length for one cylinder; inside it the two sides
 * alternate in 256-byte chunks. */
static bool hfe_header(const uint8_t *file, size_t size, hfe_geometry *out) {
    int64_t lut_size;
    if (size < HFE_HEADER_SIZE) return false;
    if (file[0] != 'H' || file[1] != 'X' || file[2] != 'C' || file[3] != 'P' ||
        file[4] != 'I' || file[5] != 'C' || file[6] != 'F' || file[7] != 'E')
        return false;
    if (file[8] != 0U || file[9] == 0U || (file[10] != 1U && file[10] != 2U))
        return false;
    out->tracks = (int32_t)file[9];
    out->sides = (int32_t)file[10];
    out->lut_offset = (int64_t)hfe_le16(file + 0x12) * HFE_BLOCK;
    if (out->tracks > HFE_MAX_TRACKS || out->lut_offset < HFE_HEADER_SIZE ||
        out->lut_offset > (int64_t)size)
        return false;
    lut_size = (int64_t)out->tracks * 4;
    if (lut_size > (int64_t)size - out->lut_offset) return false;
    return true;
}

/* Deinterleave one cylinder into a bit-cell stream per side. */
static bool hfe_track_bits(const uint8_t *file, size_t size,
                           const hfe_geometry *geometry, int32_t track,
                           uint8_t *side0, uint8_t *side1, size_t capacity,
                           size_t *cells0, size_t *cells1) {
    const int64_t entry = geometry->lut_offset + (int64_t)track * 4;
    const int64_t offset = (int64_t)hfe_le16(file + entry) * HFE_BLOCK;
    const int64_t length = (int64_t)hfe_le16(file + entry + 2);
    int64_t chunk;
    size_t used[2];
    used[0] = 0U;
    used[1] = 0U;
    if (length <= 0 || offset < HFE_HEADER_SIZE ||
        length > (int64_t)size - offset)
        return false;
    for (chunk = 0; chunk < length; chunk += HFE_BLOCK) {
        int32_t side;
        for (side = 0; side < 2; ++side) {
            uint8_t *target = side ? side1 : side0;
            int64_t start = chunk + (int64_t)side * HFE_CHUNK;
            int64_t stop = start + HFE_CHUNK;
            int64_t at;
            if (stop > length) stop = length;
            for (at = start; at < stop; ++at) {
                const uint8_t value = file[offset + at];
                int32_t bit;
                for (bit = 0; bit < 8; ++bit) {
                    if (used[side] >= capacity) return false;
                    target[used[side]++] = (uint8_t)((value >> bit) & 1U);
                }
            }
        }
    }
    *cells0 = used[0];
    *cells1 = used[1];
    return used[0] != 0U;
}

static uint16_t hfe_crc16(const uint8_t *data, size_t size, uint16_t crc) {
    size_t index;
    unsigned bit;
    for (index = 0U; index < size; ++index) {
        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc & 0x8000U) ? ((crc << 1U) ^ 0x1021U)
                                             : (crc << 1U));
    }
    return crc;
}

/* One MFM byte is sixteen cells; the data bits are the odd-indexed ones. */
static bool hfe_mfm_byte(const uint8_t *bits, size_t cells, size_t position,
                         uint8_t *value) {
    uint32_t result = 0U;
    int32_t k;
    if (position + 16U > cells) return false;
    for (k = 0; k < 8; ++k)
        result = (result << 1) | bits[position + (size_t)k * 2U + 1U];
    *value = (uint8_t)result;
    return true;
}

static bool hfe_mfm_bytes(const uint8_t *bits, size_t cells, size_t position,
                          size_t count, uint8_t *out) {
    size_t index;
    for (index = 0U; index < count; ++index)
        if (!hfe_mfm_byte(bits, cells, position + index * 16U, out + index))
            return false;
    return true;
}

typedef struct hfe_sink_s {
    uint8_t *image;          /* NULL while probing */
    uint64_t image_size;
    int32_t tracks;
    int32_t sides;
    int32_t sectors_per_track;
    int32_t sector_size;
    int32_t max_sector;      /* probe output */
    bool mixed;
} hfe_sink;

/* Decode one side of one cylinder.  While probing, the sink only records the
 * largest sector number and the sector size; once the geometry is fixed the
 * same walk writes each good sector straight into the flat image. */
static bool hfe_decode_side(const uint8_t *bits, size_t cells,
                            uint32_t *syncs, hfe_sink *sink) {
    uint32_t sync_count = 0U;
    uint32_t shift = 0U;
    size_t i;
    uint32_t s;
    bool have_id = false;
    int32_t id_cylinder = 0, id_head = 0, id_sector = 0, id_size = 0;
    bool id_valid = false;
    uint8_t field[16386];
    for (i = 0U; i < cells; ++i) {
        shift = ((shift << 1) | bits[i]) & 0xffffU;
        if (i >= 15U && shift == HFE_SYNC_A1) {
            if (sync_count >= HFE_MAX_SYNCS) break;
            syncs[sync_count++] = (uint32_t)(i - 15U);
        }
    }
    for (s = 0U; s + 2U < sync_count;) {
        size_t mark;
        uint8_t mark_value;
        if (syncs[s + 1U] != syncs[s] + 16U ||
            syncs[s + 2U] != syncs[s] + 32U) {
            ++s;
            continue;
        }
        mark = (size_t)syncs[s] + 48U;
        if (!hfe_mfm_byte(bits, cells, mark, &mark_value)) break;
        if (mark_value == 0xfeU) {
            if (hfe_mfm_bytes(bits, cells, mark + 16U, 6U, field)) {
                uint8_t preamble[4];
                uint16_t crc;
                preamble[0] = 0xa1U;
                preamble[1] = 0xa1U;
                preamble[2] = 0xa1U;
                preamble[3] = 0xfeU;
                crc = hfe_crc16(preamble, 4U, 0xffffU);
                crc = hfe_crc16(field, 4U, crc);
                id_cylinder = field[0];
                id_head = field[1];
                id_sector = field[2];
                id_size = field[3] & 7;
                id_valid = crc == (uint16_t)(((uint16_t)field[4] << 8U) |
                                             field[5]);
                have_id = true;
            }
        } else if ((mark_value == 0xfbU || mark_value == 0xf8U) && have_id) {
            const int32_t sector_size = 128 << id_size;
            if (hfe_mfm_bytes(bits, cells, mark + 16U,
                              (size_t)sector_size + 2U, field)) {
                uint8_t preamble[4];
                uint16_t crc;
                bool valid;
                preamble[0] = 0xa1U;
                preamble[1] = 0xa1U;
                preamble[2] = 0xa1U;
                preamble[3] = mark_value;
                crc = hfe_crc16(preamble, 4U, 0xffffU);
                crc = hfe_crc16(field, (size_t)sector_size, crc);
                valid = id_valid &&
                        crc == (uint16_t)(((uint16_t)field[sector_size] << 8U) |
                                          field[sector_size + 1]);
                if (id_sector > 0 && id_sector <= HFE_MAX_SECTORS) {
                    if (!sink->image) {
                        if (sink->sector_size == 0)
                            sink->sector_size = sector_size;
                        else if (sink->sector_size != sector_size)
                            sink->mixed = true;
                        if (id_sector > sink->max_sector)
                            sink->max_sector = id_sector;
                    } else if (valid && sector_size == sink->sector_size &&
                               id_cylinder >= 0 && id_cylinder < sink->tracks &&
                               id_head >= 0 && id_head < sink->sides &&
                               id_sector <= sink->sectors_per_track) {
                        const uint64_t position =
                            ((((uint64_t)id_cylinder * (uint64_t)sink->sides) +
                              (uint64_t)id_head) *
                                 (uint64_t)sink->sectors_per_track +
                             (uint64_t)(id_sector - 1)) *
                            (uint64_t)sink->sector_size;
                        if (position + (uint64_t)sink->sector_size <=
                            sink->image_size)
                            xx_mem_copy(sink->image + position, field,
                                        (size_t)sink->sector_size);
                    }
                }
            }
            have_id = false;
        }
        s += 3U;
    }
    return true;
}

static bool hfe_walk(const uint8_t *file, size_t size,
                     const hfe_geometry *geometry, bool geometry_only,
                     hfe_sink *sink) {
    const size_t capacity = 0x40000U;
    uint8_t *side0 = (uint8_t *)xx_mem_alloc(capacity);
    uint8_t *side1 = (uint8_t *)xx_mem_alloc(capacity);
    uint32_t *syncs = (uint32_t *)xx_mem_alloc(HFE_MAX_SYNCS * sizeof(uint32_t));
    const int32_t limit = geometry_only ? 1 : geometry->tracks;
    int32_t track;
    bool ok = side0 != NULL && side1 != NULL && syncs != NULL;
    for (track = 0; ok && track < limit; ++track) {
        size_t cells0 = 0U, cells1 = 0U;
        int32_t side;
        if (!hfe_track_bits(file, size, geometry, track, side0, side1, capacity,
                            &cells0, &cells1)) {
            ok = false;
            break;
        }
        for (side = 0; side < geometry->sides; ++side)
            hfe_decode_side(side ? side1 : side0, side ? cells1 : cells0, syncs,
                            sink);
    }
    if (side0) xx_mem_free(side0);
    if (side1) xx_mem_free(side1);
    if (syncs) xx_mem_free(syncs);
    return ok;
}

static bool hfe_probe(const uint8_t *file, size_t size, hfe_geometry *out) {
    hfe_sink sink;
    if (!hfe_header(file, size, out)) return false;
    xx_mem_zero(&sink, sizeof(sink));
    if (!hfe_walk(file, size, out, true, &sink) || sink.mixed ||
        sink.max_sector <= 0 || sink.sector_size <= 0)
        return false;
    out->sectors_per_track = sink.max_sector;
    out->sector_size = sink.sector_size;
    out->image_size = (uint64_t)out->tracks * (uint64_t)out->sides *
                      (uint64_t)out->sectors_per_track *
                      (uint64_t)out->sector_size;
    return out->image_size != 0U && out->image_size <= HFE_MAX_OUTPUT;
}

static bool hfe_load(Abstractformat *format, int64_t base, int64_t size,
                     uint8_t **file) {
    uint8_t *data;
    if (size < HFE_HEADER_SIZE || (uint64_t)size > HFE_MAX_OUTPUT) return false;
    data = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!data) return false;
    if (!hfe_read_at(format->device, base, data, (size_t)size)) {
        xx_mem_free(data);
        return false;
    }
    *file = data;
    return true;
}

static bool hfe_parse(Abstractformat *format, hfe_stream **result) {
    uint8_t *file = NULL;
    hfe_stream *stream;
    hfe_member member;
    hfe_geometry geometry;
    int64_t total, size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (!hfe_load(format, format->base_address, size, &file)) return false;
    if (!hfe_probe(file, (size_t)size, &geometry)) {
        xx_mem_free(file);
        return false;
    }
    xx_mem_free(file);
    stream = (hfe_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = hfe_make_name("image", -1, -1, ".img");
    member.header_offset = format->base_address;
    member.header_size = HFE_HEADER_SIZE;
    /* The LUT scatters the flux over the whole container, so the member's
     * source extent is the container itself. */
    member.data_offset = format->base_address;
    member.packed_size = size;
    member.unpacked_size = geometry.image_size;
    member.method = 1U;
    member.decode = true;
    if (!member.name || !hfe_add_member(stream, &member)) {
        if (member.name) xx_mem_free(member.name);
        hfe_stream_free(stream);
        return false;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
}

static bool hfe_decode(Abstractformat *format, const hfe_member *member,
                       uint8_t **plain, size_t *plain_size) {
    uint8_t *file = NULL;
    uint8_t *output;
    hfe_geometry geometry;
    hfe_sink sink;
    if (member->unpacked_size == 0U || member->unpacked_size > HFE_MAX_OUTPUT ||
        !hfe_load(format, member->data_offset, member->packed_size, &file))
        return false;
    if (!hfe_probe(file, (size_t)member->packed_size, &geometry) ||
        geometry.image_size != member->unpacked_size) {
        xx_mem_free(file);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!output) {
        xx_mem_free(file);
        return false;
    }
    /* A sector that is missing or fails its CRC stays zero so the image keeps
     * its geometry. */
    xx_mem_zero(output, (size_t)member->unpacked_size);
    xx_mem_zero(&sink, sizeof(sink));
    sink.image = output;
    sink.image_size = member->unpacked_size;
    sink.tracks = geometry.tracks;
    sink.sides = geometry.sides;
    sink.sectors_per_track = geometry.sectors_per_track;
    sink.sector_size = geometry.sector_size;
    if (!hfe_walk(file, (size_t)member->packed_size, &geometry, false, &sink)) {
        xx_mem_free(file);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(file);
    *plain = output;
    *plain_size = (size_t)member->unpacked_size;
    return true;
}

static bool hfe_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *hfe_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool hfe_set_record(xx_archive_record *record,
                           const hfe_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Stored members are copied verbatim; everything else goes to the format
 * codec above, which is the only place a size can grow. */
static bool hfe_extract(Abstractformat *format, const hfe_member *member,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return hfe_decode(format, member, plain, plain_size);
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > HFE_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 &&
        !hfe_read_at(format->device, member->data_offset, output,
                     (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_hfe_init(xx_hfe *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_HFE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-hfe");
    xx_format_set_extension(&archive->format, "hfe");
    archive->format.check_is_valid = xx_hfe_check_is_valid;
    archive->format.handle_base_info = xx_hfe_handle_base_info;
    archive->format.get_format_size = xx_hfe_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hfe_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hfe_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hfe_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hfe_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hfe_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hfe_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_hfe *xx_hfe_create(xx_io_device *device, int64_t base_address) {
    xx_hfe *archive = (xx_hfe *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_hfe_init(archive, device, base_address);
    return archive;
}

void xx_hfe_destroy(xx_hfe *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_hfe_free(xx_hfe *archive) {
    if (!archive) return;
    xx_hfe_destroy(archive);
    xx_mem_free(archive);
}

bool xx_hfe_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    hfe_stream *stream;
    (void)pd;
    if (!hfe_parse(format, &stream)) return false;
    hfe_stream_free(stream);
    return true;
}

bool xx_hfe_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    hfe_stream *stream;
    xx_hfe *archive;
    (void)pd;
    if (!format || !hfe_parse(format, &stream)) return false;
    archive = (xx_hfe *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    hfe_stream_free(stream);
    return true;
}

int64_t xx_hfe_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hfe_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_hfe_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hfe_handle_base_info(format, pd))
               ? ((xx_hfe *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_hfe_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    hfe_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!hfe_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        hfe_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = hfe_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!hfe_copy_options(&state->options, options) ||
        !hfe_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_hfe_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_hfe_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    hfe_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (hfe_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = hfe_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_hfe_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    hfe_stream *stream;
    hfe_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (hfe_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!hfe_extract(format, member, &plain, &plain_size)) goto done;
    path_option = hfe_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
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
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_hfe_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
