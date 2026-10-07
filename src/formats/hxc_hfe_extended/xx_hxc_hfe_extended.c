/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the HxC Floppy Emulator "extended" HFE image: signature
 * "HXCPICFE" with format revision 1, the variant hxcfe writes as HXC_EXTHFE.
 * The layout is that of HFE revision 0:
 *
 *   0x000  "HXCPICFE"
 *   0x008  format revision (1 here)
 *   0x009  number of cylinders
 *   0x00A  number of sides (1 or 2)
 *   0x00B  track encoding, 0x0C bit rate, 0x0E rpm, 0x10 interface mode ...
 *   0x012  LE16 block (512 bytes) of the cylinder LUT
 *
 * Each LUT entry is {LE16 block offset, LE16 byte length for both sides}.
 * Inside a cylinder the two sides alternate in 256-byte halves of every
 * 512-byte block; each side owns length/2 bytes, bit cells LSB first.  What
 * sits past a side's length in its last half block is filler (revision 1
 * writers put a fixed pattern there instead of repeating the track).
 *
 * The member is produced by decoding IBM MFM from the cells: ID and data
 * address marks, each checked against its CRC.  Only sectors whose ID and
 * data CRCs both hold are used.  The flat image is laid out by physical
 * cylinder and side, sectors numbered from the lowest ID seen; cylinders
 * past the last one that holds a good sector (hxcfe pads 80-cylinder disks
 * to 84) are left out.  Missing sectors inside that range stay zero.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/hxc_hfe_extended/xx_hxc_hfe_extended.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef HXC_HFE_EXTENDED
#define XX_HXC_HFE_EXTENDED_FILE_TYPE XX_FILE_TYPE_HXC_HFE_EXTENDED
#else
#define XX_HXC_HFE_EXTENDED_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XHFE_MAX_FILE (64U * 1024U * 1024U)
#define XHFE_MAX_OUTPUT (64U * 1024U * 1024U)
#define XHFE_HEADER_SIZE 512
#define XHFE_BLOCK 512
#define XHFE_HALF 256
#define XHFE_REVISION 1U
#define XHFE_MAX_SYNCS 65536U
#define XHFE_SYNC_A1 0x4489U
/* One side holds at most 0xFFFF / 2 bytes of cells. */
#define XHFE_SIDE_CELLS (0x8000U * 8U)

typedef struct xhfe_geometry_s {
    int32_t tracks;           /* header cylinders */
    int32_t sides;            /* header sides */
    int64_t lut_offset;
    int32_t out_tracks;
    int32_t out_sides;
    int32_t first_sector;
    int32_t sectors_per_track;
    int32_t sector_size;
    uint64_t image_size;
} xhfe_geometry;

typedef struct xhfe_sink_s {
    uint8_t *image;           /* NULL while probing */
    uint64_t image_size;
    int32_t out_tracks;
    int32_t out_sides;
    int32_t first_sector;
    int32_t sectors_per_track;
    int32_t sector_size;
    /* probe output */
    int32_t min_sector;
    int32_t max_sector;
    int32_t max_track;
    bool side1_used;
    uint32_t good;
} xhfe_sink;

typedef struct xhfe_stream_s {
    char *name;
    int64_t archive_size;
    uint64_t unpacked_size;
    bool done;
} xhfe_stream;

static bool xhfe_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool xhfe_header(const uint8_t *file, size_t size, xhfe_geometry *out) {
    int64_t lut_size;
    if (size < XHFE_HEADER_SIZE ||
        xx_rt_memcmp(file, "HXCPICFE", 8U) != 0 || file[8] != XHFE_REVISION ||
        file[9] == 0U || (file[10] != 1U && file[10] != 2U))
        return false;
    xx_rt_memset(out, 0, sizeof(*out));
    out->tracks = (int32_t)file[9];
    out->sides = (int32_t)file[10];
    out->lut_offset = (int64_t)xx_data_get_u16(file + 0x12, 2, 0, false) * XHFE_BLOCK;
    if (out->lut_offset < XHFE_HEADER_SIZE || out->lut_offset > (int64_t)size)
        return false;
    lut_size = (int64_t)out->tracks * 4;
    return lut_size <= (int64_t)size - out->lut_offset;
}

/* Collect the cells of one side of one cylinder. */
static bool xhfe_side_bits(const uint8_t *file, size_t size,
                           const xhfe_geometry *geometry, int32_t track,
                           int32_t side, uint8_t *bits, size_t *cells) {
    const int64_t entry = geometry->lut_offset + (int64_t)track * 4;
    const int64_t offset = (int64_t)xx_data_get_u16(file + entry, 2, 0, false) * XHFE_BLOCK;
    const int64_t per_side = (int64_t)xx_data_get_u16(file + entry + 2, 2, 0, false) / 2;
    int64_t done = 0;
    size_t used = 0U;
    if (per_side <= 0 || offset < XHFE_HEADER_SIZE || offset >= (int64_t)size)
        return false;
    while (done < per_side) {
        const int64_t block = done / XHFE_HALF;
        int64_t count = per_side - done;
        int64_t source, at;
        if (count > XHFE_HALF) count = XHFE_HALF;
        source = offset + block * XHFE_BLOCK + (int64_t)side * XHFE_HALF;
        if (source > (int64_t)size || count > (int64_t)size - source)
            return false;
        for (at = 0; at < count; ++at) {
            const uint8_t value = file[source + at];
            int32_t bit;
            if (used + 8U > XHFE_SIDE_CELLS) return false;
            for (bit = 0; bit < 8; ++bit)
                bits[used++] = (uint8_t)((value >> bit) & 1U);
        }
        done += count;
    }
    *cells = used;
    return true;
}

static uint16_t xhfe_crc16(const uint8_t *data, size_t size, uint16_t crc) {
    return xx_crc16_ccitt_calc(crc, data, size);
}

/* One MFM byte is sixteen cells; the data bits are the odd-indexed ones. */
static bool xhfe_mfm_bytes(const uint8_t *bits, size_t cells, size_t position,
                           size_t count, uint8_t *out) {
    size_t index;
    if (position > cells || count > (cells - position) / 16U) return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *p = bits + position + index * 16U;
        uint32_t value = 0U;
        int32_t k;
        for (k = 0; k < 8; ++k) value = (value << 1) | p[k * 2 + 1];
        out[index] = (uint8_t)value;
    }
    return true;
}

static uint16_t xhfe_mark_crc(uint8_t mark, const uint8_t *data, size_t size) {
    uint8_t preamble[4];
    preamble[0] = 0xa1U;
    preamble[1] = 0xa1U;
    preamble[2] = 0xa1U;
    preamble[3] = mark;
    return xhfe_crc16(data, size, xhfe_crc16(preamble, 4U, 0xffffU));
}

static void xhfe_sector(xhfe_sink *sink, int32_t track, int32_t side,
                        int32_t id, int32_t size, const uint8_t *data) {
    if (!sink->image) {
        if (sink->sector_size == 0) sink->sector_size = size;
        if (size != sink->sector_size) return;
        if (sink->good == 0U || id < sink->min_sector) sink->min_sector = id;
        if (sink->good == 0U || id > sink->max_sector) sink->max_sector = id;
        if (track > sink->max_track) sink->max_track = track;
        if (side) sink->side1_used = true;
        if (sink->good < UINT32_MAX) ++sink->good;
        return;
    }
    if (size != sink->sector_size || track >= sink->out_tracks ||
        side >= sink->out_sides || id < sink->first_sector ||
        id - sink->first_sector >= sink->sectors_per_track)
        return;
    {
        const uint64_t position =
            ((((uint64_t)track * (uint64_t)sink->out_sides) + (uint64_t)side) *
                 (uint64_t)sink->sectors_per_track +
             (uint64_t)(id - sink->first_sector)) *
            (uint64_t)sink->sector_size;
        if (position <= sink->image_size &&
            (uint64_t)sink->sector_size <= sink->image_size - position)
            xx_rt_memcpy(sink->image + position, data,
                         (size_t)sink->sector_size);
    }
}

static void xhfe_decode_side(const uint8_t *bits, size_t cells,
                             uint32_t *syncs, uint8_t *field, int32_t track,
                             int32_t side, xhfe_sink *sink) {
    uint32_t sync_count = 0U;
    uint32_t shift = 0U;
    size_t i;
    uint32_t s;
    bool have_id = false;
    int32_t id_sector = 0, id_size = 0;
    for (i = 0U; i < cells; ++i) {
        shift = ((shift << 1) | bits[i]) & 0xffffU;
        if (i >= 15U && shift == XHFE_SYNC_A1) {
            if (sync_count >= XHFE_MAX_SYNCS) break;
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
        if (!xhfe_mfm_bytes(bits, cells, mark, 1U, &mark_value)) break;
        if (mark_value == 0xfeU) {
            have_id = false;
            if (xhfe_mfm_bytes(bits, cells, mark + 16U, 6U, field) &&
                xhfe_mark_crc(0xfeU, field, 4U) ==
                    (uint16_t)(((uint16_t)field[4] << 8U) | field[5])) {
                id_sector = field[2];
                id_size = field[3] & 7;
                have_id = true;
            }
        } else if ((mark_value == 0xfbU || mark_value == 0xf8U) && have_id) {
            const int32_t sector_size = 128 << id_size;
            if (xhfe_mfm_bytes(bits, cells, mark + 16U,
                               (size_t)sector_size + 2U, field) &&
                xhfe_mark_crc(mark_value, field, (size_t)sector_size) ==
                    (uint16_t)(((uint16_t)field[sector_size] << 8U) |
                               field[sector_size + 1]))
                xhfe_sector(sink, track, side, id_sector, sector_size, field);
            have_id = false;
        }
        s += 3U;
    }
}

static bool xhfe_walk(const uint8_t *file, size_t size,
                      const xhfe_geometry *geometry, xhfe_sink *sink) {
    uint8_t *bits = (uint8_t *)xx_mem_alloc(XHFE_SIDE_CELLS);
    uint32_t *syncs =
        (uint32_t *)xx_mem_alloc(XHFE_MAX_SYNCS * sizeof(uint32_t));
    uint8_t *field = (uint8_t *)xx_mem_alloc(16384U + 2U);
    const int32_t limit = sink->image ? sink->out_tracks : geometry->tracks;
    int32_t track;
    bool ok = bits != NULL && syncs != NULL && field != NULL;
    for (track = 0; ok && track < limit; ++track) {
        int32_t side;
        const int32_t sides = sink->image ? sink->out_sides : geometry->sides;
        for (side = 0; ok && side < sides; ++side) {
            size_t cells = 0U;
            if (!xhfe_side_bits(file, size, geometry, track, side, bits,
                                &cells)) {
                ok = false;
                break;
            }
            xhfe_decode_side(bits, cells, syncs, field, track, side, sink);
        }
    }
    if (bits) xx_mem_free(bits);
    if (syncs) xx_mem_free(syncs);
    if (field) xx_mem_free(field);
    return ok;
}

static bool xhfe_probe(const uint8_t *file, size_t size, xhfe_geometry *out) {
    xhfe_sink sink;
    if (!xhfe_header(file, size, out)) return false;
    xx_rt_memset(&sink, 0, sizeof(sink));
    if (!xhfe_walk(file, size, out, &sink) || sink.good == 0U ||
        sink.sector_size <= 0)
        return false;
    out->out_tracks = sink.max_track + 1;
    out->out_sides = sink.side1_used ? 2 : 1;
    out->first_sector = sink.min_sector;
    out->sectors_per_track = sink.max_sector - sink.min_sector + 1;
    out->sector_size = sink.sector_size;
    out->image_size = (uint64_t)out->out_tracks * (uint64_t)out->out_sides *
                      (uint64_t)out->sectors_per_track *
                      (uint64_t)out->sector_size;
    return out->image_size != 0U && out->image_size <= XHFE_MAX_OUTPUT;
}

/* The whole container is needed: the LUT scatters cylinders over it. */
static bool xhfe_load(Abstractformat *format, uint8_t **file, size_t *size) {
    int64_t total, length;
    uint8_t header[16];
    uint8_t *data;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    length = total - format->base_address;
    if (length < XHFE_HEADER_SIZE) return false;
    /* Cheap gate before the big read. */
    if (!xhfe_read_at(format->device, format->base_address, header,
                      sizeof(header)) ||
        xx_rt_memcmp(header, "HXCPICFE", 8U) != 0 ||
        header[8] != XHFE_REVISION)
        return false;
    if ((uint64_t)length > XHFE_MAX_FILE) return false;
    data = (uint8_t *)xx_mem_alloc((size_t)length);
    if (!data) return false;
    if (!xhfe_read_at(format->device, format->base_address, data,
                      (size_t)length)) {
        xx_mem_free(data);
        return false;
    }
    *file = data;
    *size = (size_t)length;
    return true;
}

static void xhfe_stream_free(void *opaque) {
    xhfe_stream *stream = (xhfe_stream *)opaque;
    if (!stream) return;
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool xhfe_parse(Abstractformat *format, xhfe_stream **result) {
    uint8_t *file = NULL;
    size_t size = 0U;
    xhfe_geometry geometry;
    xhfe_stream *stream;
    static const char name[] = "image.img";
    if (!result || !xhfe_load(format, &file, &size)) return false;
    if (!xhfe_probe(file, size, &geometry)) {
        xx_mem_free(file);
        return false;
    }
    xx_mem_free(file);
    stream = (xhfe_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return false;
    xx_rt_memset(stream, 0, sizeof(*stream));
    stream->name = (char *)xx_mem_alloc(sizeof(name));
    if (!stream->name) {
        xhfe_stream_free(stream);
        return false;
    }
    xx_rt_memcpy(stream->name, name, sizeof(name));
    stream->archive_size = (int64_t)size;
    stream->unpacked_size = geometry.image_size;
    *result = stream;
    return true;
}

static bool xhfe_decode(Abstractformat *format, const xhfe_stream *stream,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *file = NULL;
    size_t size = 0U;
    uint8_t *output;
    xhfe_geometry geometry;
    xhfe_sink sink;
    if (!xhfe_load(format, &file, &size)) return false;
    if (!xhfe_probe(file, size, &geometry) ||
        geometry.image_size != stream->unpacked_size) {
        xx_mem_free(file);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)geometry.image_size);
    if (!output) {
        xx_mem_free(file);
        return false;
    }
    xx_rt_memset(output, 0, (size_t)geometry.image_size);
    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.image = output;
    sink.image_size = geometry.image_size;
    sink.out_tracks = geometry.out_tracks;
    sink.out_sides = geometry.out_sides;
    sink.first_sector = geometry.first_sector;
    sink.sectors_per_track = geometry.sectors_per_track;
    sink.sector_size = geometry.sector_size;
    if (!xhfe_walk(file, size, &geometry, &sink)) {
        xx_mem_free(file);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(file);
    *plain = output;
    *plain_size = (size_t)geometry.image_size;
    return true;
}

static bool xhfe_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *xhfe_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool xhfe_set_record(Abstractformat *format, xx_archive_record *record,
                            const xhfe_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = XHFE_HEADER_SIZE;
    record->data_offset = format->base_address;
    record->compressed_size = stream->archive_size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->archive_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          stream->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_hxc_hfe_extended_init(xx_hxc_hfe_extended *archive,
                              xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_HXC_HFE_EXTENDED_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-hfe");
    xx_format_set_extension(&archive->format, "hfe");
    archive->format.check_is_valid = xx_hxc_hfe_extended_check_is_valid;
    archive->format.handle_base_info = xx_hxc_hfe_extended_handle_base_info;
    archive->format.get_format_size = xx_hxc_hfe_extended_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hxc_hfe_extended_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hxc_hfe_extended_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hxc_hfe_extended_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hxc_hfe_extended_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hxc_hfe_extended_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hxc_hfe_extended_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_hxc_hfe_extended *xx_hxc_hfe_extended_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_hxc_hfe_extended *archive =
        (xx_hxc_hfe_extended *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_hxc_hfe_extended_init(archive, device, base_address);
    return archive;
}

void xx_hxc_hfe_extended_destroy(xx_hxc_hfe_extended *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_hxc_hfe_extended_free(xx_hxc_hfe_extended *archive) {
    if (!archive) return;
    xx_hxc_hfe_extended_destroy(archive);
    xx_mem_free(archive);
}

bool xx_hxc_hfe_extended_check_is_valid(Abstractformat *format,
                                        xx_pd_struct *pd) {
    xhfe_stream *stream;
    (void)pd;
    if (!xhfe_parse(format, &stream)) return false;
    xhfe_stream_free(stream);
    return true;
}

bool xx_hxc_hfe_extended_handle_base_info(Abstractformat *format,
                                          xx_pd_struct *pd) {
    xhfe_stream *stream;
    xx_hxc_hfe_extended *archive;
    (void)pd;
    if (!format || !xhfe_parse(format, &stream)) return false;
    archive = (xx_hxc_hfe_extended *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = 1U;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    xhfe_stream_free(stream);
    return true;
}

int64_t xx_hxc_hfe_extended_get_format_size(Abstractformat *format,
                                            xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hxc_hfe_extended_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_hxc_hfe_extended_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hxc_hfe_extended_handle_base_info(format, pd))
               ? ((xx_hxc_hfe_extended *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_hxc_hfe_extended_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xhfe_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!xhfe_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xhfe_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xhfe_stream_free;
    state->total_records = 1;
    if (!xhfe_copy_options(&state->options, options) ||
        !xhfe_set_record(format, &state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_hxc_hfe_extended_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_hxc_hfe_extended_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    xhfe_stream *stream;
    (void)pd;
    if (state) state->has_record = false;
    if (!format || !state || state->format != format ||
        !(stream = (xhfe_stream *)state->internal_state))
        return false;
    stream->done = true;
    return false;
}

bool xx_hxc_hfe_extended_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    xhfe_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (xhfe_stream *)state->internal_state) || stream->done ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!xhfe_decode(format, stream, &plain, &plain_size)) goto done;
    path_option = xhfe_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    /* The member name is the constant "image.img", never container data. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
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
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_hxc_hfe_extended_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
