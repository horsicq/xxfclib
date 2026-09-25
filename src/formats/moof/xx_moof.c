/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Applesauce MOOF floppy image (Macintosh 3.5" disks).
 * The container keeps raw bit cells or flux timings per track, so the image
 * member is produced by decoding the tracks: Macintosh 6-and-2 GCR sectors
 * (address field D5 AA 96, data field D5 AA AD carrying 12 tag bytes and 512
 * data bytes under a three-byte running checksum) for 400K/800K disks, and
 * IBM MFM sectors (A1 A1 A1 sync, CRC-16) for 1.44M disks.  Written from the
 * published MOOF chunk layout and the Sony GCR sector format.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/moof/xx_moof.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef MOOF
#define XX_MOOF_FILE_TYPE XX_FILE_TYPE_MOOF
#else
#define XX_MOOF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MOOF_HEADER_SIZE 12
#define MOOF_BLOCK 512U
#define MOOF_TRACKS 160U
#define MOOF_TRK_ENTRY 8U
#define MOOF_TRK_TABLE (MOOF_TRACKS * MOOF_TRK_ENTRY)
#define MOOF_MIN_INFO 44U
#define MOOF_MAX_CHUNKS 1024U
#define MOOF_NO_TRACK 0xFFU

/* One track keeps at most this many bit cells (flux tracks can hold several
 * revolutions; one 3.5" revolution is under 0.25M cells even at 1 us). */
#define MOOF_MAX_CELLS (4U * 1024U * 1024U)
/* Flux bytes read per track; each non-0xFF byte yields at least one cell. */
#define MOOF_MAX_FLUX (8U * 1024U * 1024U)
/* Cells repeated after the end of a track so a sector that crosses the
 * index is still seen whole. */
#define MOOF_WRAP_CELLS 32768U
/* Longest run of empty cells a single flux interval may produce. */
#define MOOF_MAX_RUN 64U
#define MOOF_MAX_IMAGE (4U * 1024U * 1024U)
#define MOOF_MAX_META (16U * 1024U * 1024U)

#define MOOF_GCR_CYLINDERS 80U
#define MOOF_GCR_NIBBLES 703U   /* 699 data + 4 checksum nibbles */
#define MOOF_GCR_BYTES 524U     /* 12 tag bytes + 512 data bytes */
#define MOOF_GCR_TAGS 12U
#define MOOF_GCR_MARK_GAP 48U   /* nibbles from address field to data mark */

#define MOOF_MFM_SYNC 0x4489U
#define MOOF_MFM_MAX_CODE 6U    /* 8192-byte sectors */
#define MOOF_MFM_MAX_SECTOR (128U << MOOF_MFM_MAX_CODE)
#define MOOF_MFM_ID_GAP 2048U   /* cells from ID sync to data sync */

#define MOOF_MODE_ANALYSE 0U

#define MOOF_KIND_STORED 0U
#define MOOF_KIND_IMAGE 1U
#define MOOF_KIND_TAGS 2U
#define MOOF_MAX_MEMBERS 3U

/* ------------------------------------------------------------------------ */

static uint16_t moof_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t moof_le32(const uint8_t *b) {
    return (uint32_t)moof_le16(b) | ((uint32_t)moof_le16(b + 2U) << 16U);
}

static bool moof_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ------------------------------------------------------------------------ */
/* Container layout                                                         */

typedef struct moof_layout_s {
    int64_t size;          /* bytes from the base to the end of the device */
    int64_t format_size;
    uint32_t disk_type;
    uint32_t bit_time;     /* nominal cell length in 125 ns ticks */
    uint8_t tmap[MOOF_TRACKS];
    uint8_t fmap[MOOF_TRACKS];
    bool has_flux;
    uint8_t trks[MOOF_TRK_TABLE];
    int64_t meta_offset;   /* relative to the base, 0 when absent */
    uint32_t meta_size;
    uint32_t valid_tracks;
} moof_layout;

typedef struct moof_source_s {
    int64_t offset;        /* relative to the base */
    uint64_t bytes;
    uint32_t count;        /* bit count, or flux byte count */
    bool flux;
} moof_source;

/* A TRKS entry names its data by 512-byte block from the start of the file;
 * for a bitstream the count is in bits, for a flux track in bytes. */
static bool moof_track_source(const moof_layout *layout, uint8_t entry,
                              bool flux, moof_source *out) {
    const uint8_t *field;
    uint64_t start, blocks, count, bytes;
    if (!layout || !out || entry >= MOOF_TRACKS) return false;
    field = layout->trks + (size_t)entry * MOOF_TRK_ENTRY;
    start = (uint64_t)moof_le16(field) * MOOF_BLOCK;
    blocks = (uint64_t)moof_le16(field + 2U) * MOOF_BLOCK;
    count = moof_le32(field + 4U);
    if (start == 0U || blocks == 0U || count == 0U) return false;
    bytes = flux ? count : (count + 7U) / 8U;
    if (bytes > blocks || start > (uint64_t)layout->size ||
        bytes > (uint64_t)layout->size - start)
        return false;
    out->offset = (int64_t)start;
    out->bytes = bytes;
    out->count = (uint32_t)count;
    out->flux = flux;
    return true;
}

static bool moof_id_is(const uint8_t *id, const char *name) {
    return id[0] == (uint8_t)name[0] && id[1] == (uint8_t)name[1] &&
           id[2] == (uint8_t)name[2] && id[3] == (uint8_t)name[3];
}

/* Walk the chunks.  INFO must come first; TMAP and TRKS are required; FLUX
 * and META are optional.  The walk stops at a chunk that runs past the end
 * of the data and at an all-zero chunk id (MAME pads the file to a block
 * boundary with zeros). */
static bool moof_layout_read(Abstractformat *format, moof_layout *out) {
    static const uint8_t signature[8] = {'M', 'O', 'O', 'F',
                                         0xFFU, 0x0AU, 0x0DU, 0x0AU};
    uint8_t header[MOOF_HEADER_SIZE];
    uint8_t chunk[8];
    uint8_t info[8];
    int64_t total, position, end, data_end;
    uint32_t chunks = 0U, index;
    bool have_info = false, have_tmap = false, have_trks = false;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    xx_mem_zero(out, sizeof(*out));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    out->size = total - format->base_address;
    if (out->size < MOOF_HEADER_SIZE + 8 + (int64_t)MOOF_MIN_INFO ||
        !moof_read_at(format->device, format->base_address, header,
                      sizeof(header)) ||
        xx_rt_memcmp(header, signature, sizeof(signature)) != 0)
        return false;
    position = MOOF_HEADER_SIZE;
    end = position;
    while (position <= out->size - 8 && chunks < MOOF_MAX_CHUNKS) {
        uint32_t length;
        int64_t data;
        if (!moof_read_at(format->device, format->base_address + position,
                          chunk, sizeof(chunk)))
            return false;
        if (chunk[0] == 0U && chunk[1] == 0U && chunk[2] == 0U &&
            chunk[3] == 0U)
            break;
        length = moof_le32(chunk + 4U);
        data = position + 8;
        if ((int64_t)length > out->size - data) break;
        if (chunks == 0U) {
            if (!moof_id_is(chunk, "INFO") || length < MOOF_MIN_INFO ||
                !moof_read_at(format->device, format->base_address + data,
                              info, sizeof(info)) ||
                info[0] == 0U)
                return false;
            out->disk_type = info[1];
            out->bit_time = info[4];
            have_info = true;
        } else if (moof_id_is(chunk, "TMAP") && !have_tmap) {
            if (length < MOOF_TRACKS ||
                !moof_read_at(format->device, format->base_address + data,
                              out->tmap, MOOF_TRACKS))
                return false;
            have_tmap = true;
        } else if (moof_id_is(chunk, "FLUX") && !out->has_flux) {
            if (length >= MOOF_TRACKS &&
                moof_read_at(format->device, format->base_address + data,
                             out->fmap, MOOF_TRACKS))
                out->has_flux = true;
        } else if (moof_id_is(chunk, "TRKS") && !have_trks) {
            if (length < MOOF_TRK_TABLE ||
                !moof_read_at(format->device, format->base_address + data,
                              out->trks, MOOF_TRK_TABLE))
                return false;
            have_trks = true;
        } else if (moof_id_is(chunk, "META") && out->meta_offset == 0) {
            out->meta_offset = data;
            out->meta_size = length;
        }
        position = data + (int64_t)length;
        end = position;
        ++chunks;
    }
    if (!have_info || !have_tmap || !have_trks) return false;
    if (!out->has_flux) xx_mem_zero(out->fmap, sizeof(out->fmap));
    for (index = 0U; index < MOOF_TRACKS; ++index) {
        moof_source source;
        bool any = false;
        if (moof_track_source(out, out->tmap[index], false, &source)) {
            any = true;
            data_end = source.offset + (int64_t)source.bytes;
            if (data_end > end) end = data_end;
        }
        if (out->has_flux &&
            moof_track_source(out, out->fmap[index], true, &source)) {
            any = true;
            data_end = source.offset + (int64_t)source.bytes;
            if (data_end > end) end = data_end;
        }
        if (any) ++out->valid_tracks;
    }
    if (out->valid_tracks == 0U) return false;
    /* Take in the zero padding up to the next block boundary. */
    out->format_size = end;
    if ((end % (int64_t)MOOF_BLOCK) != 0) {
        int64_t padded = end + ((int64_t)MOOF_BLOCK - end % (int64_t)MOOF_BLOCK);
        uint8_t pad[MOOF_BLOCK];
        size_t gap = (size_t)(padded - end), at;
        bool zero = padded <= out->size &&
                    moof_read_at(format->device, format->base_address + end,
                                 pad, gap);
        for (at = 0U; zero && at < gap; ++at)
            if (pad[at] != 0U) zero = false;
        if (zero) out->format_size = padded;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Track decoding                                                           */

typedef struct moof_decoder_s {
    Abstractformat *format;
    const moof_layout *layout;
    xx_pd_struct *pd;
    uint8_t *raw;
    size_t raw_capacity;
    uint8_t *cells;
    size_t cell_capacity;
    size_t cell_count;
    uint8_t *nibbles;
    size_t nibble_capacity;
    int16_t gcr_value[256];
    uint16_t crc_table[256];
    uint8_t values[MOOF_GCR_NIBBLES];
    uint8_t sector[MOOF_MFM_MAX_SECTOR + 2U];
    /* analysis */
    uint32_t gcr_good[2];
    bool gcr_tags[2];
    uint32_t mfm_count[MOOF_MFM_MAX_CODE + 1U];
    uint32_t mfm_rmin[MOOF_MFM_MAX_CODE + 1U];
    uint32_t mfm_rmax[MOOF_MFM_MAX_CODE + 1U];
    uint32_t mfm_cyl[MOOF_MFM_MAX_CODE + 1U];
    bool mfm_side1[MOOF_MFM_MAX_CODE + 1U];
    /* fill */
    uint32_t mode;
    uint32_t sides;
    uint32_t cylinders;
    uint32_t spt;
    uint32_t rmin;
    uint32_t size_code;
    uint32_t sector_size;
    uint8_t *image;
    size_t image_size;
    uint8_t *tags;
    size_t tags_size;
    uint8_t *filled;
    size_t slots;
    uint32_t written;
    bool tags_nonzero;
} moof_decoder;

/* The 64 disk bytes of the 6-and-2 code, in value order. */
static const uint8_t moof_gcr_code[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

static void moof_decoder_init(moof_decoder *d) {
    uint32_t index, bit;
    for (index = 0U; index < 256U; ++index) d->gcr_value[index] = -1;
    for (index = 0U; index < 64U; ++index)
        d->gcr_value[moof_gcr_code[index]] = (int16_t)index;
    for (index = 0U; index < 256U; ++index) {
        uint16_t crc = (uint16_t)(index << 8U);
        for (bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc & 0x8000U) ? ((crc << 1U) ^ 0x1021U)
                                             : (crc << 1U));
        d->crc_table[index] = crc;
    }
    for (index = 0U; index <= MOOF_MFM_MAX_CODE; ++index)
        d->mfm_rmin[index] = 0xFFFFFFFFU;
}

static void moof_decoder_release(moof_decoder *d) {
    if (d->raw) xx_mem_free(d->raw);
    if (d->cells) xx_mem_free(d->cells);
    if (d->nibbles) xx_mem_free(d->nibbles);
    if (d->filled) xx_mem_free(d->filled);
    d->raw = d->cells = d->nibbles = d->filled = NULL;
}

static bool moof_reserve(uint8_t **buffer, size_t *capacity, size_t needed) {
    uint8_t *grown;
    if (needed <= *capacity && *buffer) return true;
    grown = (uint8_t *)xx_mem_alloc(needed != 0U ? needed : 1U);
    if (!grown) return false;
    if (*buffer) xx_mem_free(*buffer);
    *buffer = grown;
    *capacity = needed;
    return true;
}

/* Flux intervals to cells.  A light clock follows the drive speed within
 * +-12.5% of the nominal cell; an interval shorter than half a cell merges
 * into the next one, and a long gap yields at most MOOF_MAX_RUN cells. */
static size_t moof_flux_cells(const uint8_t *raw, size_t bytes,
                              uint32_t nominal, uint8_t *cells, size_t cap) {
    const uint64_t base = (uint64_t)nominal * 256U;
    const uint64_t low = base - base / 8U, high = base + base / 8U;
    uint64_t period = base, ticks = 0U;
    size_t index, used = 0U;
    for (index = 0U; index < bytes && used < cap; ++index) {
        uint64_t scaled, count, run;
        ticks += raw[index];
        if (raw[index] == 0xFFU) continue;
        scaled = ticks * 256U;
        count = (scaled + period / 2U) / period;
        if (count == 0U) continue;
        if (count <= 8U) {
            uint64_t measured = scaled / count;
            if (measured > period) period += (measured - period) / 16U;
            else period -= (period - measured) / 16U;
            if (period < low) period = low;
            if (period > high) period = high;
        }
        if (count > MOOF_MAX_RUN) count = MOOF_MAX_RUN;
        for (run = 1U; run < count && used < cap; ++run) cells[used++] = 0U;
        if (used < cap) cells[used++] = 1U;
        ticks = 0U;
    }
    return used;
}

static bool moof_load_cells(moof_decoder *d, const moof_source *source) {
    size_t used = 0U, wrap, index;
    if (!source->flux) {
        const uint32_t bits = source->count > MOOF_MAX_CELLS ? MOOF_MAX_CELLS
                                                              : source->count;
        const size_t bytes = ((size_t)bits + 7U) / 8U;
        if (!moof_reserve(&d->raw, &d->raw_capacity, bytes) ||
            !moof_reserve(&d->cells, &d->cell_capacity,
                          (size_t)bits + MOOF_WRAP_CELLS) ||
            !moof_read_at(d->format->device,
                          d->format->base_address + source->offset, d->raw,
                          bytes))
            return false;
        for (index = 0U; index < bits; ++index)
            d->cells[used++] =
                (uint8_t)((d->raw[index >> 3U] >> (7U - (index & 7U))) & 1U);
    } else {
        uint32_t nominal = d->layout->bit_time;
        const size_t bytes = source->bytes > MOOF_MAX_FLUX
                                 ? MOOF_MAX_FLUX : (size_t)source->bytes;
        if (nominal == 0U) nominal = d->layout->disk_type == 3U ? 8U : 16U;
        if (!moof_reserve(&d->raw, &d->raw_capacity, bytes) ||
            !moof_reserve(&d->cells, &d->cell_capacity,
                          (size_t)MOOF_MAX_CELLS + MOOF_WRAP_CELLS) ||
            !moof_read_at(d->format->device,
                          d->format->base_address + source->offset, d->raw,
                          bytes))
            return false;
        used = moof_flux_cells(d->raw, bytes, nominal, d->cells,
                               MOOF_MAX_CELLS);
    }
    if (used == 0U) return false;
    wrap = used < MOOF_WRAP_CELLS ? used : MOOF_WRAP_CELLS;
    xx_mem_copy(d->cells + used, d->cells, wrap);
    d->cell_count = used + wrap;
    return moof_reserve(&d->nibbles, &d->nibble_capacity,
                        d->cell_count / 8U + 1U);
}

/* ---- Macintosh GCR ---- */

static uint32_t moof_gcr_spt(uint32_t cylinder) {
    return 12U - cylinder / 16U;
}

static uint32_t moof_gcr_first_block(uint32_t cylinder, uint32_t sides) {
    uint32_t zone = cylinder / 16U, before = 0U, z;
    for (z = 0U; z < zone; ++z) before += 16U * (12U - z);
    return sides * (before + (cylinder % 16U) * (12U - zone));
}

/* Undo the three-byte running checksum of a data field.  Each group of four
 * nibbles carries the top bits of three bytes and their low six bits; the
 * last group carries two bytes.  The first checksum byte rotates left before
 * every group and its carry feeds the second; carries then chain on to the
 * third and back to the first. */
static bool moof_gcr_data(const uint8_t *v, uint8_t *out) {
    uint32_t c0 = 0U, c1 = 0U, c2 = 0U, group, hi, x, y, z;
    size_t j = 0U, o = 0U;
    for (group = 0U; group < 175U; ++group) {
        uint32_t w1, w2, w3 = 0U, b;
        hi = v[j++];
        w1 = v[j++] | ((hi << 2U) & 0xC0U);
        w2 = v[j++] | ((hi << 4U) & 0xC0U);
        if (group != 174U) w3 = v[j++] | ((hi << 6U) & 0xC0U);
        c0 = (c0 & 0xFFU) << 1U;
        if (c0 & 0x100U) c0 |= 1U;
        b = (w1 ^ c0) & 0xFFU;
        c2 += b;
        if (c0 & 0x100U) {
            ++c2;
            c0 &= 0xFFU;
        }
        out[o++] = (uint8_t)b;
        b = (w2 ^ c2) & 0xFFU;
        c1 += b;
        if (c2 > 0xFFU) {
            ++c1;
            c2 &= 0xFFU;
        }
        out[o++] = (uint8_t)b;
        if (group == 174U) break;
        b = (w3 ^ c1) & 0xFFU;
        c0 += b;
        if (c1 > 0xFFU) {
            ++c0;
            c1 &= 0xFFU;
        }
        out[o++] = (uint8_t)b;
    }
    hi = v[j];
    x = v[j + 1U] | ((hi << 2U) & 0xC0U);
    y = v[j + 2U] | ((hi << 4U) & 0xC0U);
    z = v[j + 3U] | ((hi << 6U) & 0xC0U);
    return o == MOOF_GCR_BYTES && x == (c2 & 0xFFU) && y == (c1 & 0xFFU) &&
           z == (c0 & 0xFFU);
}

static void moof_gcr_deliver(moof_decoder *d, uint32_t cylinder,
                             uint32_t head, uint32_t track, uint32_t side,
                             uint32_t sector, const uint8_t *data) {
    uint32_t spt, block, index;
    bool tags = false;
    if (track != cylinder || side != head || cylinder >= MOOF_GCR_CYLINDERS)
        return;
    spt = moof_gcr_spt(cylinder);
    if (sector >= spt) return;
    for (index = 0U; index < MOOF_GCR_TAGS; ++index)
        if (data[index] != 0U) tags = true;
    if (d->mode == MOOF_MODE_ANALYSE) {
        ++d->gcr_good[head];
        if (tags) d->gcr_tags[head] = true;
        return;
    }
    if (d->mode != XX_MOOF_ENCODING_GCR || head >= d->sides) return;
    block = moof_gcr_first_block(cylinder, d->sides) + head * spt + sector;
    if ((size_t)block >= d->slots || d->filled[block]) return;
    d->filled[block] = 1U;
    xx_mem_copy(d->image + (size_t)block * MOOF_BLOCK, data + MOOF_GCR_TAGS,
                MOOF_BLOCK);
    xx_mem_copy(d->tags + (size_t)block * MOOF_GCR_TAGS, data, MOOF_GCR_TAGS);
    if (tags) d->tags_nonzero = true;
    ++d->written;
}

static void moof_gcr_scan(moof_decoder *d, uint32_t cylinder, uint32_t head) {
    const uint8_t *cells = d->cells;
    uint8_t *nib = d->nibbles;
    size_t count = 0U, index, at;
    uint32_t shift = 0U;
    /* The disk controller's latch: skip zeros, then take eight cells. */
    for (index = 0U; index < d->cell_count && count < d->nibble_capacity;
         ++index) {
        if (shift == 0U && cells[index] == 0U) continue;
        shift = (shift << 1U) | cells[index];
        if (shift & 0x80U) {
            nib[count++] = (uint8_t)shift;
            shift = 0U;
        }
    }
    for (index = 0U; index + 8U <= count; ++index) {
        int16_t v[5];
        uint32_t k, track, side, sector;
        size_t mark = 0U, start;
        bool found = false, valid = true;
        if (nib[index] != 0xD5U || nib[index + 1U] != 0xAAU ||
            nib[index + 2U] != 0x96U)
            continue;
        for (k = 0U; k < 5U; ++k) {
            v[k] = d->gcr_value[nib[index + 3U + k]];
            if (v[k] < 0) valid = false;
        }
        if (!valid || (v[0] ^ v[1] ^ v[2] ^ v[3]) != v[4]) continue;
        track = (uint32_t)v[0] | (((uint32_t)v[2] & 0x1FU) << 6U);
        side = ((uint32_t)v[2] >> 5U) & 1U;
        sector = (uint32_t)v[1];
        for (at = index + 8U; at + 3U <= count &&
                              at < index + 8U + MOOF_GCR_MARK_GAP;
             ++at) {
            if (nib[at] == 0xD5U && nib[at + 1U] == 0xAAU &&
                nib[at + 2U] == 0xADU) {
                mark = at;
                found = true;
                break;
            }
        }
        if (!found) {
            index += 7U;
            continue;
        }
        /* The nibble after the mark repeats the sector number; writers do
         * not agree on it, so it is not checked. */
        start = mark + 4U;
        if (start > count || count - start < MOOF_GCR_NIBBLES) break;
        for (k = 0U; k < MOOF_GCR_NIBBLES && valid; ++k) {
            const int16_t value = d->gcr_value[nib[start + k]];
            if (value < 0) valid = false;
            else d->values[k] = (uint8_t)value;
        }
        if (valid && moof_gcr_data(d->values, d->sector)) {
            moof_gcr_deliver(d, cylinder, head, track, side, sector,
                             d->sector);
            index = start + MOOF_GCR_NIBBLES - 1U;
        } else {
            index = mark + 2U;
        }
    }
}

/* ---- IBM MFM ---- */

static uint32_t moof_cells16(const uint8_t *cells, size_t position) {
    uint32_t value = 0U, k;
    for (k = 0U; k < 16U; ++k) value = (value << 1U) | cells[position + k];
    return value;
}

/* One MFM byte is sixteen cells; the data bits are the odd-indexed ones. */
static void moof_mfm_bytes(const uint8_t *cells, size_t position,
                           size_t count, uint8_t *out) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        const uint8_t *at = cells + position + index * 16U;
        uint32_t value = 0U, k;
        for (k = 0U; k < 8U; ++k) value = (value << 1U) | at[k * 2U + 1U];
        out[index] = (uint8_t)value;
    }
}

static uint16_t moof_crc16(const moof_decoder *d, uint8_t mark,
                           const uint8_t *data, size_t size) {
    uint16_t crc = 0xFFFFU;
    size_t index;
    static const uint8_t sync[3] = {0xA1U, 0xA1U, 0xA1U};
    for (index = 0U; index < 3U; ++index)
        crc = (uint16_t)((crc << 8U) ^ d->crc_table[(crc >> 8U) ^ sync[index]]);
    crc = (uint16_t)((crc << 8U) ^ d->crc_table[(crc >> 8U) ^ mark]);
    for (index = 0U; index < size; ++index)
        crc = (uint16_t)((crc << 8U) ^ d->crc_table[(crc >> 8U) ^ data[index]]);
    return crc;
}

static void moof_mfm_deliver(moof_decoder *d, uint32_t cylinder,
                             uint32_t head, const uint8_t *id,
                             const uint8_t *data) {
    const uint32_t code = id[3], record = id[2];
    size_t slot;
    if (id[0] != cylinder || code > MOOF_MFM_MAX_CODE) return;
    if (d->mode == MOOF_MODE_ANALYSE) {
        ++d->mfm_count[code];
        if (record < d->mfm_rmin[code]) d->mfm_rmin[code] = record;
        if (record > d->mfm_rmax[code]) d->mfm_rmax[code] = record;
        if (cylinder > d->mfm_cyl[code]) d->mfm_cyl[code] = cylinder;
        if (head != 0U) d->mfm_side1[code] = true;
        return;
    }
    if (d->mode != XX_MOOF_ENCODING_MFM || code != d->size_code ||
        head >= d->sides || cylinder >= d->cylinders || record < d->rmin ||
        record - d->rmin >= d->spt)
        return;
    slot = ((size_t)cylinder * d->sides + head) * d->spt + (record - d->rmin);
    if (slot >= d->slots || d->filled[slot]) return;
    d->filled[slot] = 1U;
    xx_mem_copy(d->image + slot * d->sector_size, data, d->sector_size);
    ++d->written;
}

static void moof_mfm_scan(moof_decoder *d, uint32_t cylinder, uint32_t head) {
    const uint8_t *cells = d->cells;
    const size_t count = d->cell_count;
    uint8_t id[6];
    bool have_id = false;
    size_t id_at = 0U, index;
    uint32_t shift = 0U;
    for (index = 0U; index < count; ++index) {
        size_t sync, body;
        uint8_t mark;
        shift = ((shift << 1U) | cells[index]) & 0xFFFFU;
        if (index < 15U || shift != MOOF_MFM_SYNC) continue;
        sync = index - 15U;
        if (count - sync < 64U + 6U * 16U) break;
        if (moof_cells16(cells, sync + 16U) != MOOF_MFM_SYNC ||
            moof_cells16(cells, sync + 32U) != MOOF_MFM_SYNC)
            continue;
        moof_mfm_bytes(cells, sync + 48U, 1U, &mark);
        body = sync + 64U;
        if (mark == 0xFEU) {
            moof_mfm_bytes(cells, body, 6U, id);
            have_id = moof_crc16(d, mark, id, 4U) ==
                      (uint16_t)(((uint16_t)id[4] << 8U) | id[5]);
            id_at = sync;
            index = body + 6U * 16U - 1U;
            shift = 0U;
        } else if ((mark == 0xFBU || mark == 0xF8U) && have_id &&
                   sync - id_at <= MOOF_MFM_ID_GAP) {
            const uint32_t code = id[3];
            have_id = false;
            if (code <= MOOF_MFM_MAX_CODE) {
                const size_t size = (size_t)128U << code;
                if ((count - body) / 16U >= size + 2U) {
                    moof_mfm_bytes(cells, body, size + 2U, d->sector);
                    if (moof_crc16(d, mark, d->sector, size) ==
                        (uint16_t)(((uint16_t)d->sector[size] << 8U) |
                                   d->sector[size + 1U]))
                        moof_mfm_deliver(d, cylinder, head, id, d->sector);
                    index = body + (size + 2U) * 16U - 1U;
                    shift = 0U;
                }
            }
        }
    }
}

/* Visit every track, the bitstream first and then the flux capture, so a
 * sector the bitstream lacks may still come from the flux. */
static bool moof_walk(moof_decoder *d) {
    uint32_t index, pass;
    for (index = 0U; index < MOOF_TRACKS; ++index) {
        const uint32_t cylinder = index / 2U, head = index % 2U;
        if (d->pd && xx_pd_is_stopped(d->pd)) return false;
        for (pass = 0U; pass < 2U; ++pass) {
            const bool flux = pass == 1U;
            const uint8_t entry = flux ? (d->layout->has_flux
                                              ? d->layout->fmap[index]
                                              : MOOF_NO_TRACK)
                                       : d->layout->tmap[index];
            moof_source source;
            if (!moof_track_source(d->layout, entry, flux, &source) ||
                !moof_load_cells(d, &source))
                continue;
            if (d->mode != XX_MOOF_ENCODING_MFM)
                moof_gcr_scan(d, cylinder, head);
            if (d->mode != XX_MOOF_ENCODING_GCR)
                moof_mfm_scan(d, cylinder, head);
        }
    }
    return true;
}

typedef struct moof_result_s {
    uint32_t encoding;
    uint8_t *image;
    size_t image_size;
    uint8_t *tags;         /* NULL unless a tag byte is non-zero */
    size_t tags_size;
    uint32_t good;
} moof_result;

static bool moof_plan_gcr(moof_decoder *d) {
    const uint32_t type = d->layout->disk_type;
    uint32_t sides;
    if (type == 1U) sides = 1U;
    else if (type == 2U) sides = 2U;
    else sides = d->gcr_good[1] != 0U ? 2U : 1U;
    if (d->gcr_good[0] == 0U && (sides == 1U || d->gcr_good[1] == 0U))
        return false;
    d->mode = XX_MOOF_ENCODING_GCR;
    d->sides = sides;
    d->slots = (size_t)moof_gcr_first_block(MOOF_GCR_CYLINDERS, sides);
    d->image_size = d->slots * MOOF_BLOCK;
    d->tags_size = d->slots * MOOF_GCR_TAGS;
    return true;
}

static bool moof_plan_mfm(moof_decoder *d) {
    uint32_t code, best = MOOF_MFM_MAX_CODE + 1U;
    uint64_t size;
    for (code = 0U; code <= MOOF_MFM_MAX_CODE; ++code)
        if (d->mfm_count[code] != 0U &&
            (best > MOOF_MFM_MAX_CODE ||
             d->mfm_count[code] > d->mfm_count[best]))
            best = code;
    if (best > MOOF_MFM_MAX_CODE) return false;
    d->mode = XX_MOOF_ENCODING_MFM;
    d->size_code = best;
    d->sector_size = 128U << best;
    d->rmin = d->mfm_rmin[best];
    d->spt = d->mfm_rmax[best] - d->mfm_rmin[best] + 1U;
    d->sides = d->mfm_side1[best] ? 2U : 1U;
    d->cylinders = d->mfm_cyl[best] + 1U;
    d->slots = (size_t)d->cylinders * d->sides * d->spt;
    size = (uint64_t)d->slots * d->sector_size;
    if (size == 0U || size > MOOF_MAX_IMAGE) return false;
    d->image_size = (size_t)size;
    d->tags_size = 0U;
    return true;
}

/* Decode the whole disk: an analysis walk counts good sectors of both
 * encodings and fixes the geometry, a second walk fills the image.  The
 * disk type decides which encoding is tried first. */
static bool moof_decode(Abstractformat *format, const moof_layout *layout,
                        xx_pd_struct *pd, moof_result *out) {
    moof_decoder *d;
    bool planned, ok = false;
    xx_mem_zero(out, sizeof(*out));
    d = (moof_decoder *)xx_mem_calloc(1U, sizeof(*d));
    if (!d) return false;
    d->format = format;
    d->layout = layout;
    d->pd = pd;
    moof_decoder_init(d);
    d->mode = MOOF_MODE_ANALYSE;
    if (!moof_walk(d)) goto done;
    if (layout->disk_type == 3U)
        planned = moof_plan_mfm(d) || moof_plan_gcr(d);
    else
        planned = moof_plan_gcr(d) || moof_plan_mfm(d);
    if (!planned) {
        ok = true;   /* a valid container whose tracks hold no sectors */
        goto done;
    }
    d->image = (uint8_t *)xx_mem_calloc(1U, d->image_size);
    d->filled = (uint8_t *)xx_mem_calloc(1U, d->slots);
    if (d->tags_size != 0U)
        d->tags = (uint8_t *)xx_mem_calloc(1U, d->tags_size);
    if (!d->image || !d->filled || (d->tags_size != 0U && !d->tags) ||
        !moof_walk(d))
        goto done;
    if (d->written == 0U) {
        ok = true;
        goto done;
    }
    out->encoding = d->mode;
    out->image = d->image;
    out->image_size = d->image_size;
    out->good = d->written;
    d->image = NULL;
    if (d->tags && d->tags_nonzero) {
        out->tags = d->tags;
        out->tags_size = d->tags_size;
        d->tags = NULL;
    }
    ok = true;
done:
    if (d->image) xx_mem_free(d->image);
    if (d->tags) xx_mem_free(d->tags);
    moof_decoder_release(d);
    xx_mem_free(d);
    return ok;
}

/* ------------------------------------------------------------------------ */
/* Members                                                                  */

typedef struct moof_member_s {
    const char *name;      /* reader-owned constant, safe by construction */
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t kind;
} moof_member;

typedef struct moof_stream_s {
    moof_member items[MOOF_MAX_MEMBERS];
    size_t count;
    size_t index;
    int64_t archive_size;
    moof_layout layout;
    moof_result result;
} moof_stream;

static void moof_stream_free(void *opaque) {
    moof_stream *stream = (moof_stream *)opaque;
    if (!stream) return;
    if (stream->result.image) xx_mem_free(stream->result.image);
    if (stream->result.tags) xx_mem_free(stream->result.tags);
    xx_mem_free(stream);
}

static void moof_add(moof_stream *stream, const char *name, int64_t offset,
                     int64_t packed, uint64_t unpacked, uint32_t kind,
                     int64_t base) {
    moof_member *member;
    if (stream->count >= MOOF_MAX_MEMBERS) return;
    member = &stream->items[stream->count++];
    member->name = name;
    member->header_offset = base;
    member->header_size = MOOF_HEADER_SIZE;
    member->data_offset = offset;
    member->packed_size = packed;
    member->unpacked_size = unpacked;
    member->kind = kind;
}

static bool moof_parse(Abstractformat *format, xx_pd_struct *pd,
                       moof_stream **result) {
    moof_stream *stream;
    int64_t base;
    if (!format || !result) return false;
    stream = (moof_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!moof_layout_read(format, &stream->layout) ||
        !moof_decode(format, &stream->layout, pd, &stream->result)) {
        moof_stream_free(stream);
        return false;
    }
    base = format->base_address;
    stream->archive_size = stream->layout.format_size;
    /* The tracks are spread over the whole container, so a decoded member's
     * source extent is the container itself. */
    if (stream->result.image)
        moof_add(stream, "image.img", base, stream->archive_size,
                 stream->result.image_size, MOOF_KIND_IMAGE, base);
    if (stream->result.tags)
        moof_add(stream, "tags.bin", base, stream->archive_size,
                 stream->result.tags_size, MOOF_KIND_TAGS, base);
    if (stream->layout.meta_offset != 0 && stream->layout.meta_size != 0U &&
        stream->layout.meta_size <= MOOF_MAX_META)
        moof_add(stream, "meta.txt", base + stream->layout.meta_offset,
                 (int64_t)stream->layout.meta_size, stream->layout.meta_size,
                 MOOF_KIND_STORED, base);
    *result = stream;
    return true;
}

static bool moof_copy_options(xx_list_s *destination,
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

static const xx_var *moof_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool moof_set_record(xx_archive_record *record,
                            const moof_member *member) {
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
                                          member->kind == MOOF_KIND_STORED
                                              ? 0U : 1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* The decoded members come from the stream's buffers; the META text is
 * read from the container.  *owned tells the caller to free *plain. */
static bool moof_member_bytes(Abstractformat *format, moof_stream *stream,
                              const moof_member *member, const uint8_t **plain,
                              size_t *plain_size, uint8_t **owned) {
    *owned = NULL;
    if (member->kind == MOOF_KIND_IMAGE) {
        *plain = stream->result.image;
        *plain_size = stream->result.image_size;
        return stream->result.image != NULL;
    }
    if (member->kind == MOOF_KIND_TAGS) {
        *plain = stream->result.tags;
        *plain_size = stream->result.tags_size;
        return stream->result.tags != NULL;
    }
    if (member->packed_size <= 0 ||
        (uint64_t)member->packed_size > MOOF_MAX_META)
        return false;
    *owned = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!*owned) return false;
    if (!moof_read_at(format->device, member->data_offset, *owned,
                      (size_t)member->packed_size)) {
        xx_mem_free(*owned);
        *owned = NULL;
        return false;
    }
    *plain = *owned;
    *plain_size = (size_t)member->packed_size;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */

void xx_moof_init(xx_moof *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MOOF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-moof");
    xx_format_set_extension(&archive->format, "moof");
    archive->format.check_is_valid = xx_moof_check_is_valid;
    archive->format.handle_base_info = xx_moof_handle_base_info;
    archive->format.get_format_size = xx_moof_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_moof_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_moof_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_moof_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_moof_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_moof_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_moof_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_moof *xx_moof_create(xx_io_device *device, int64_t base_address) {
    xx_moof *archive = (xx_moof *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_moof_init(archive, device, base_address);
    return archive;
}

void xx_moof_destroy(xx_moof *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_moof_free(xx_moof *archive) {
    if (!archive) return;
    xx_moof_destroy(archive);
    xx_mem_free(archive);
}

/* The probe checks the container structure only; decoding waits until the
 * members are asked for. */
bool xx_moof_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    moof_layout layout;
    (void)pd;
    return moof_layout_read(format, &layout);
}

bool xx_moof_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    moof_stream *stream;
    xx_moof *archive;
    if (!format || !moof_parse(format, pd, &stream)) return false;
    archive = (xx_moof *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->disk_type = stream->layout.disk_type;
    archive->encoding = stream->result.encoding;
    archive->image_size = stream->result.image_size;
    archive->good_sectors = stream->result.good;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    moof_stream_free(stream);
    return true;
}

int64_t xx_moof_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_moof_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_moof_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_moof_handle_base_info(format, pd))
               ? ((xx_moof *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_moof_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    moof_stream *stream;
    xx_archive_record_state *state;
    if (!moof_parse(format, pd, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        moof_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = moof_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!moof_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !moof_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    return state;
}

const xx_archive_record *xx_moof_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_moof_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    moof_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (moof_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = moof_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_moof_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    moof_stream *stream;
    const moof_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    const uint8_t *plain = NULL;
    uint8_t *owned = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (moof_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!moof_member_bytes(format, stream, member, &plain, &plain_size,
                           &owned))
        goto done;
    path_option = moof_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
        created = destination != NULL;
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
    if (!result && path && created) xx_rt_remove(path);
    if (owned) xx_mem_free(owned);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_moof_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
