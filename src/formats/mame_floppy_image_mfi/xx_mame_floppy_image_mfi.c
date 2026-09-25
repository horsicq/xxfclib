/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the MAME floppy image (MFI).
 *
 * Layout (all little endian):
 *   +0x00  16-byte signature "MAMEFLOPPYIMAGE\0" (or "MESSFLOPPYIMAGE\0",
 *          the older variant written before the flux codes were renumbered)
 *   +0x10  u32 cylinder count; its top two bits are the track resolution
 *          (0 whole, 1 half, 2 quarter tracks)
 *   +0x14  u32 head count
 *   +0x18  u32 form factor, u32 variant (informational)
 *   +0x20  one 16-byte entry per track position, cylinder-major then head:
 *          u32 file offset, u32 compressed size, u32 uncompressed size
 *          (0 = unformatted), u32 write splice
 *   then   one zlib stream per formatted track.
 *
 * A decompressed track is a list of u32 cells: the top four bits are the
 * cell type, the low 28 bits its length, one revolution being 200,000,000
 * units.  In the MAME variant type 0 starts a cell with a flux transition;
 * in the MESS variant types 0 and 1 are the two magnetic orientations, so a
 * transition is where the orientation changes.  Types 2 and 3 in the MESS
 * variant, and 1..3 in the MAME one, mark unrecorded zones.
 *
 * The transitions are quantised to bit cells (the cell width comes from the
 * shortest common interval of each track), then IBM MFM (A1 sync with the
 * missing clock) and FM (clock C7 address marks) fields are decoded, each
 * checked against its CRC.  The sectors are laid out cylinder, head,
 * sector as a flat image.  A disk without such sectors (GCR, Amiga, ...)
 * is exposed as its tracks instead.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mame_floppy_image_mfi/xx_mame_floppy_image_mfi.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef MAME_FLOPPY_IMAGE_MFI
#define XX_MAME_FLOPPY_IMAGE_MFI_FILE_TYPE XX_FILE_TYPE_MAME_FLOPPY_IMAGE_MFI
#else
#define XX_MAME_FLOPPY_IMAGE_MFI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MFI_HEADER_SIZE 32
#define MFI_ENTRY_SIZE 16
#define MFI_MAX_CYLINDERS 84U
#define MFI_MAX_HEADS 2U
#define MFI_MAX_RESOLUTION 2U
#define MFI_MAX_ENTRIES (MFI_MAX_CYLINDERS * 4U * MFI_MAX_HEADS)
#define MFI_MAX_TRACK_BYTES (16U * 1024U * 1024U)
#define MFI_MAX_PACKED_BYTES (32U * 1024U * 1024U)
#define MFI_MAX_OUTPUT (64U * 1024U * 1024U)
#define MFI_MAX_CELLS (1U << 20)
#define MFI_MAX_EVENTS 65536U
#define MFI_TIME_MASK 0x0FFFFFFFU
#define MFI_HIST_SHIFT 4U
#define MFI_HIST_BINS 4096U
#define MFI_MIN_INTERVALS 32U
#define MFI_MAX_SIZE_CODE 7U
#define MFI_MAX_SECTOR (128U << MFI_MAX_SIZE_CODE)
/* Longest distance, in bytes, from the end of an ID field to its data mark. */
#define MFI_ID_WINDOW 96U
#define MFI_SYNC_MFM 0x4489U
#define MFI_EVENT_MFM 0U
#define MFI_EVENT_FM1 1U
#define MFI_EVENT_FM2 2U
#define MFI_METHOD_SECTORS 1U
#define MFI_METHOD_ZLIB 8U

static const uint8_t mfi_signature[16] = {
    'M', 'A', 'M', 'E', 'F', 'L', 'O', 'P',
    'P', 'Y', 'I', 'M', 'A', 'G', 'E', 0
};
static const uint8_t mfi_signature_old[16] = {
    'M', 'E', 'S', 'S', 'F', 'L', 'O', 'P',
    'P', 'Y', 'I', 'M', 'A', 'G', 'E', 0
};

typedef struct mfi_entry_s {
    uint32_t offset;
    uint32_t compressed;
    uint32_t uncompressed;
    uint32_t cylinder;
    uint32_t quarter;   /* 0..3, position between whole cylinders */
    uint32_t head;
    bool usable;
} mfi_entry;

typedef struct mfi_table_s {
    bool old_format;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t resolution;
    uint32_t count;
    uint32_t formatted;
    uint32_t usable;
    uint32_t max_compressed;
    uint32_t max_uncompressed;
    int64_t size;
    int64_t table_end;
    int64_t format_size;
    mfi_entry entries[MFI_MAX_ENTRIES];
} mfi_table;

typedef struct mfi_geometry_s {
    bool has_image;
    uint32_t size_code;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t sectors;
    int32_t base[2];
    uint64_t image_size;
} mfi_geometry;

typedef struct mfi_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;
    uint32_t entry;       /* table index of a track member */
    bool image;
} mfi_member;

typedef struct mfi_stream_s {
    mfi_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    mfi_geometry geometry;
} mfi_stream;

/* Where decoded sectors go: statistics while the geometry is unknown, the
 * flat image once it is fixed. */
typedef struct mfi_sink_s {
    uint32_t good[MFI_MAX_SIZE_CODE + 1U];
    int32_t min_r[MFI_MAX_SIZE_CODE + 1U][MFI_MAX_HEADS];
    int32_t max_r[MFI_MAX_SIZE_CODE + 1U][MFI_MAX_HEADS];
    int32_t last_cylinder[MFI_MAX_SIZE_CODE + 1U];
    int32_t last_head[MFI_MAX_SIZE_CODE + 1U];
    const mfi_geometry *geometry;
    uint8_t *image;
    uint8_t *filled;      /* per slot: 0 empty, 1 bad CRC data, 2 good */
} mfi_sink;

typedef struct mfi_work_s {
    uint8_t *packed;
    uint8_t *raw;
    uint8_t *cells;
    uint32_t *events;
    uint32_t hist[MFI_HIST_BINS];
    uint8_t field[MFI_MAX_SECTOR + 2U];
} mfi_work;

typedef struct mfi_pending_s {
    bool valid;
    bool crc_ok;
    uint32_t scale;
    uint32_t kind;
    size_t end;
    uint8_t c, h, r, n;
} mfi_pending;

static uint32_t mfi_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static bool mfi_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ---------------------------------------------------------------------- */
/* Header and track table                                                  */

static bool mfi_read_table(xx_io_device *device, int64_t base, mfi_table *t) {
    uint8_t head[MFI_HEADER_SIZE];
    uint8_t *raw = NULL;
    uint32_t raw_cylinders, index;
    int64_t total;
    if (!device || !t || base < 0) return false;
    xx_mem_zero(t, sizeof(*t));
    total = xx_io_total_size(device);
    if (total < base || total - base < MFI_HEADER_SIZE) return false;
    t->size = total - base;
    if (!mfi_read_at(device, base, head, sizeof(head))) return false;
    if (xx_rt_memcmp(head, mfi_signature, 16U) == 0)
        t->old_format = false;
    else if (xx_rt_memcmp(head, mfi_signature_old, 16U) == 0)
        t->old_format = true;
    else
        return false;
    raw_cylinders = mfi_le32(head + 16);
    t->resolution = raw_cylinders >> 30U;
    t->cylinders = raw_cylinders & 0x3FFFFFFFU;
    t->heads = mfi_le32(head + 20);
    if (t->resolution > MFI_MAX_RESOLUTION || t->cylinders > MFI_MAX_CYLINDERS ||
        t->heads > MFI_MAX_HEADS)
        return false;
    t->count = (t->cylinders << t->resolution) * t->heads;
    t->table_end = MFI_HEADER_SIZE + (int64_t)t->count * MFI_ENTRY_SIZE;
    if (t->table_end > t->size) return false;
    t->format_size = t->table_end;
    if (t->count == 0U) return true;
    raw = (uint8_t *)xx_mem_alloc((size_t)t->count * MFI_ENTRY_SIZE);
    if (!raw) return false;
    if (!mfi_read_at(device, base + MFI_HEADER_SIZE, raw,
                     (size_t)t->count * MFI_ENTRY_SIZE)) {
        xx_mem_free(raw);
        return false;
    }
    for (index = 0U; index < t->count; ++index) {
        mfi_entry *e = &t->entries[index];
        const uint8_t *p = raw + (size_t)index * MFI_ENTRY_SIZE;
        const uint32_t row = index / t->heads;
        int64_t end;
        e->offset = mfi_le32(p);
        e->compressed = mfi_le32(p + 4);
        e->uncompressed = mfi_le32(p + 8);
        e->head = index % t->heads;
        e->cylinder = row >> t->resolution;
        e->quarter = (row & ((1U << t->resolution) - 1U))
                     << (MFI_MAX_RESOLUTION - t->resolution);
        if (e->uncompressed == 0U) continue;
        ++t->formatted;
        end = (int64_t)e->offset + (int64_t)e->compressed;
        /* A track that cannot be read is left out, not fatal: a truncated
         * file still yields every track that is present. */
        if (e->uncompressed > MFI_MAX_TRACK_BYTES || e->uncompressed < 4U ||
            e->compressed < 2U || e->compressed > MFI_MAX_PACKED_BYTES ||
            (int64_t)e->offset < t->table_end || end > t->size)
            continue;
        e->usable = true;
        ++t->usable;
        if (e->compressed > t->max_compressed)
            t->max_compressed = e->compressed;
        if (e->uncompressed > t->max_uncompressed)
            t->max_uncompressed = e->uncompressed;
        if (end > t->format_size) t->format_size = end;
    }
    xx_mem_free(raw);
    /* A blank disk has no formatted track; a disk whose formatted tracks all
     * point outside the file is not an MFI worth claiming. */
    return t->formatted == 0U || t->usable != 0U;
}

/* ---------------------------------------------------------------------- */
/* Track decoding                                                          */

static void mfi_work_free(mfi_work *w) {
    if (!w) return;
    if (w->packed) xx_mem_free(w->packed);
    if (w->raw) xx_mem_free(w->raw);
    if (w->cells) xx_mem_free(w->cells);
    if (w->events) xx_mem_free(w->events);
    xx_mem_free(w);
}

static mfi_work *mfi_work_create(const mfi_table *t) {
    mfi_work *w = (mfi_work *)xx_mem_calloc(1U, sizeof(*w));
    if (!w) return NULL;
    w->packed = (uint8_t *)xx_mem_alloc(t->max_compressed ? t->max_compressed
                                                          : 1U);
    w->raw = (uint8_t *)xx_mem_alloc(t->max_uncompressed ? t->max_uncompressed
                                                         : 1U);
    w->cells = (uint8_t *)xx_mem_alloc((size_t)MFI_MAX_CELLS * 2U);
    w->events = (uint32_t *)xx_mem_alloc(MFI_MAX_EVENTS * sizeof(uint32_t));
    if (!w->packed || !w->raw || !w->cells || !w->events) {
        mfi_work_free(w);
        return NULL;
    }
    return w;
}

static bool mfi_inflate(xx_io_device *device, int64_t base,
                        const mfi_entry *e, mfi_work *w) {
    size_t written = 0U;
    if (!e->usable ||
        !mfi_read_at(device, base + (int64_t)e->offset, w->packed,
                     e->compressed) ||
        !xx_zlib_stream_decode_memory(w->packed, e->compressed, w->raw,
                                      e->uncompressed, &written))
        return false;
    return written == e->uncompressed;
}

/* Walks the cells of a track and reports each flux transition's time. */
typedef struct mfi_flux_s {
    const uint8_t *raw;
    size_t count;
    size_t index;
    uint64_t time;
    int level;
    bool old_format;
} mfi_flux;

static void mfi_flux_start(mfi_flux *f, const uint8_t *raw, size_t count,
                           bool old_format) {
    f->raw = raw;
    f->count = count;
    f->index = 0U;
    f->time = 0U;
    f->old_format = old_format;
    f->level = -1;
    if (old_format && count != 0U) {
        const uint32_t type = mfi_le32(raw + (count - 1U) * 4U) >> 28U;
        f->level = type <= 1U ? (int)type : -1;
    }
}

static bool mfi_flux_next(mfi_flux *f, uint64_t *when) {
    while (f->index < f->count) {
        const uint32_t value = mfi_le32(f->raw + f->index * 4U);
        const uint32_t type = value >> 28U;
        const uint64_t start = f->time;
        bool change;
        f->time += value & MFI_TIME_MASK;
        ++f->index;
        if (f->old_format) {
            change = type <= 1U && (int)type != f->level;
            f->level = type <= 1U ? (int)type : -1;
        } else {
            change = type == 0U;
        }
        if (change) {
            *when = start;
            return true;
        }
    }
    return false;
}

static void mfi_hist_add(mfi_work *w, uint64_t interval, uint32_t *total) {
    if (interval == 0U || interval >= ((uint64_t)MFI_HIST_BINS << MFI_HIST_SHIFT))
        return;
    ++w->hist[(size_t)(interval >> MFI_HIST_SHIFT)];
    ++*total;
}

/* Cell width in 1/16 units: the shortest interval that is common enough to
 * be data (the 10th percentile), refined to the mean of its cluster, is two
 * cells for both MFM and FM. */
static uint64_t mfi_cell_width(mfi_work *w, uint32_t total) {
    uint64_t seen = 0U, sum = 0U, weight = 0U;
    uint64_t target, center, low, high;
    uint32_t bin, found = MFI_HIST_BINS;
    if (total < MFI_MIN_INTERVALS) return 0U;
    target = (uint64_t)total / 10U + 1U;
    for (bin = 0U; bin < MFI_HIST_BINS; ++bin) {
        seen += w->hist[bin];
        if (seen >= target) {
            found = bin;
            break;
        }
    }
    if (found == MFI_HIST_BINS) return 0U;
    center = ((uint64_t)found << MFI_HIST_SHIFT) + (1U << (MFI_HIST_SHIFT - 1U));
    low = center * 7U / 10U;
    high = center * 13U / 10U;
    for (bin = 0U; bin < MFI_HIST_BINS; ++bin) {
        const uint64_t middle =
            ((uint64_t)bin << MFI_HIST_SHIFT) + (1U << (MFI_HIST_SHIFT - 1U));
        if (middle < low || middle > high) continue;
        sum += middle * w->hist[bin];
        weight += w->hist[bin];
    }
    if (weight == 0U) return 0U;
    /* Mean interval * 16 / 2. */
    return (sum * 8U) / weight;
}

static size_t mfi_emit_gap(mfi_work *w, size_t used, uint64_t interval,
                           uint64_t width16, bool final_one) {
    uint64_t cells = (interval * 16U + width16 / 2U) / width16;
    uint64_t zeros;
    if (cells == 0U) return used;
    zeros = cells - 1U;
    if (zeros > (uint64_t)(MFI_MAX_CELLS - used))
        zeros = (uint64_t)(MFI_MAX_CELLS - used);
    xx_mem_zero(w->cells + used, (size_t)zeros);
    used += (size_t)zeros;
    if (final_one && used < MFI_MAX_CELLS) w->cells[used++] = 1U;
    return used;
}

/* Turns the decompressed track into a circular bit-cell stream, followed by
 * a copy of its start so a field crossing the index reads straight through.
 * Returns the cell count of one revolution. */
static size_t mfi_track_cells(const mfi_table *t, mfi_work *w, size_t count) {
    mfi_flux flux;
    uint64_t when, first = 0U, last = 0U, total_time, width16;
    uint32_t intervals = 0U;
    size_t used = 0U, extra;
    bool any = false;
    xx_mem_zero(w->hist, sizeof(w->hist));
    mfi_flux_start(&flux, w->raw, count, t->old_format);
    while (mfi_flux_next(&flux, &when)) {
        if (!any) {
            first = when;
            any = true;
        } else {
            mfi_hist_add(w, when - last, &intervals);
        }
        last = when;
    }
    if (!any) return 0U;
    total_time = flux.time;
    mfi_hist_add(w, total_time - last + first, &intervals);
    width16 = mfi_cell_width(w, intervals);
    /* Narrower than one unit per cell is not a recording. */
    if (width16 < 16U) return 0U;
    mfi_flux_start(&flux, w->raw, count, t->old_format);
    any = false;
    while (used < MFI_MAX_CELLS && mfi_flux_next(&flux, &when)) {
        if (!any) {
            w->cells[used++] = 1U;
            any = true;
        } else {
            used = mfi_emit_gap(w, used, when - last, width16, true);
        }
        last = when;
    }
    if (used < MFI_MAX_CELLS)
        used = mfi_emit_gap(w, used, total_time - last + first, width16, false);
    extra = used;
    xx_mem_copy(w->cells + used, w->cells, extra);
    return used;
}

static uint16_t mfi_crc16(const uint8_t *data, size_t size, uint16_t crc) {
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

/* One encoded byte: data bit k (MSB first) is cell position + scale*(2k+1)
 * for MFM (scale 1), FM at its own cell width (scale 1) and FM at half its
 * cell width (scale 2). */
static bool mfi_read_bytes(const mfi_work *w, size_t limit, size_t position,
                           uint32_t scale, size_t count, uint8_t *out) {
    const size_t span = (size_t)16U * scale;
    size_t index;
    if (position > limit || count > (limit - position) / span) return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *cell = w->cells + position + index * span;
        uint32_t value = 0U;
        uint32_t k;
        for (k = 0U; k < 8U; ++k)
            value = (value << 1U) | cell[scale * (2U * k + 1U)];
        out[index] = (uint8_t)value;
    }
    return true;
}

static uint32_t mfi_expand(uint32_t value) {
    uint32_t result = 0U;
    uint32_t bit;
    for (bit = 0U; bit < 16U; ++bit)
        if (value & (1U << bit)) result |= 1U << (2U * bit + 1U);
    return result;
}

static void mfi_sink_sector(mfi_sink *sink, const mfi_entry *e,
                            const mfi_pending *id, bool data_ok,
                            const uint8_t *data) {
    const uint32_t n = id->n;
    if (n > MFI_MAX_SIZE_CODE || e->head >= MFI_MAX_HEADS) return;
    if (!sink->image) {
        if (data_ok) ++sink->good[n];
        if ((int32_t)id->r < sink->min_r[n][e->head])
            sink->min_r[n][e->head] = id->r;
        if ((int32_t)id->r > sink->max_r[n][e->head])
            sink->max_r[n][e->head] = id->r;
        if ((int32_t)e->cylinder > sink->last_cylinder[n])
            sink->last_cylinder[n] = (int32_t)e->cylinder;
        if ((int32_t)e->head > sink->last_head[n])
            sink->last_head[n] = (int32_t)e->head;
    } else {
        const mfi_geometry *g = sink->geometry;
        const int32_t slot_in_track = (int32_t)id->r - g->base[e->head];
        const uint32_t size = 128U << n;
        uint64_t slot;
        uint8_t state = data_ok ? 2U : 1U;
        if (n != g->size_code || e->cylinder >= g->cylinders ||
            e->head >= g->heads || slot_in_track < 0 ||
            (uint32_t)slot_in_track >= g->sectors)
            return;
        slot = ((uint64_t)e->cylinder * g->heads + e->head) * g->sectors +
               (uint64_t)slot_in_track;
        if ((slot + 1U) * size > g->image_size || sink->filled[slot] >= state)
            return;
        xx_mem_copy(sink->image + slot * size, data, size);
        sink->filled[slot] = state;
    }
}

/* Finds the address marks of one revolution and decodes ID and data fields. */
static void mfi_track_sectors(mfi_work *w, size_t length, const mfi_entry *e,
                              mfi_sink *sink) {
    const size_t limit = length * 2U;
    const uint32_t fm2_mask = mfi_expand(0xFFEAU) | 0x55555555U;
    const uint32_t fm2_value = mfi_expand(0xF56AU);
    static const uint8_t a1[3] = { 0xA1U, 0xA1U, 0xA1U };
    const uint16_t mfm_seed = mfi_crc16(a1, 3U, 0xFFFFU);
    mfi_pending pending;
    uint32_t shift = 0U, events = 0U, index;
    size_t i, skip_until = 0U;
    for (i = 0U; i < limit && events < MFI_MAX_EVENTS; ++i) {
        shift = (shift << 1U) | w->cells[i];
        if (i >= 15U && i - 15U < length) {
            const uint32_t low = shift & 0xFFFFU;
            if (low == MFI_SYNC_MFM)
                w->events[events++] = (uint32_t)((i - 15U) << 2U) | MFI_EVENT_MFM;
            else if ((low & 0xFFEAU) == 0xF56AU)
                w->events[events++] = (uint32_t)((i - 15U) << 2U) | MFI_EVENT_FM1;
        }
        if (i >= 31U && i - 31U < length && events < MFI_MAX_EVENTS &&
            (shift & fm2_mask) == fm2_value)
            w->events[events++] = (uint32_t)((i - 31U) << 2U) | MFI_EVENT_FM2;
    }
    xx_mem_zero(&pending, sizeof(pending));
    for (index = 0U; index < events;) {
        const uint32_t kind = w->events[index] & 3U;
        const size_t start = (size_t)(w->events[index] >> 2U);
        uint32_t scale = 1U, used = 1U;
        size_t mark_at, fields;
        uint8_t mark;
        uint16_t crc;
        if (start < skip_until) {
            ++index;
            continue;
        }
        if (kind == MFI_EVENT_MFM) {
            if (index + 2U >= events ||
                w->events[index + 1U] !=
                    (((uint32_t)(start + 16U) << 2U) | MFI_EVENT_MFM) ||
                w->events[index + 2U] !=
                    (((uint32_t)(start + 32U) << 2U) | MFI_EVENT_MFM)) {
                ++index;
                continue;
            }
            mark_at = start + 48U;
            if (!mfi_read_bytes(w, limit, mark_at, 1U, 1U, &mark)) break;
            used = 3U;
        } else {
            scale = kind == MFI_EVENT_FM2 ? 2U : 1U;
            mark_at = start;
            if (!mfi_read_bytes(w, limit, mark_at, scale, 1U, &mark)) break;
        }
        fields = mark_at + (size_t)16U * scale;
        crc = kind == MFI_EVENT_MFM ? mfm_seed : 0xFFFFU;
        crc = mfi_crc16(&mark, 1U, crc);
        if (mark == 0xFEU) {
            uint8_t id[6];
            if (!mfi_read_bytes(w, limit, fields, scale, 6U, id)) break;
            pending.valid = true;
            pending.kind = kind;
            pending.scale = scale;
            pending.c = id[0];
            pending.h = id[1];
            pending.r = id[2];
            pending.n = id[3];
            pending.crc_ok =
                mfi_crc16(id, 4U, crc) ==
                (uint16_t)(((uint16_t)id[4] << 8U) | id[5]);
            pending.end = fields + (size_t)6U * 16U * scale;
            skip_until = pending.end;
            index += used;
        } else if (mark >= 0xF8U && mark <= 0xFBU) {
            const bool paired =
                pending.valid && pending.kind == kind &&
                mark_at >= pending.end &&
                mark_at - pending.end <= (size_t)MFI_ID_WINDOW * 16U * scale;
            if (paired && pending.crc_ok && pending.n <= MFI_MAX_SIZE_CODE) {
                const uint32_t size = 128U << pending.n;
                if (mfi_read_bytes(w, limit, fields, scale, size + 2U,
                                   w->field)) {
                    const bool ok =
                        mfi_crc16(w->field, size, crc) ==
                        (uint16_t)(((uint16_t)w->field[size] << 8U) |
                                   w->field[size + 1U]);
                    mfi_sink_sector(sink, e, &pending, ok, w->field);
                    skip_until = fields + (size_t)(size + 2U) * 16U * scale;
                }
            }
            pending.valid = false;
            index += used;
        } else {
            ++index;
        }
    }
}

/* Decodes every whole-cylinder track into the sink. */
static bool mfi_scan(Abstractformat *format, const mfi_table *t,
                     mfi_sink *sink) {
    mfi_work *w;
    uint32_t index;
    if (t->usable == 0U) return true;
    w = mfi_work_create(t);
    if (!w) return false;
    for (index = 0U; index < t->count; ++index) {
        const mfi_entry *e = &t->entries[index];
        size_t length;
        if (!e->usable || e->quarter != 0U) continue;
        if (!mfi_inflate(format->device, format->base_address, e, w)) continue;
        length = mfi_track_cells(t, w, e->uncompressed / 4U);
        if (length != 0U) mfi_track_sectors(w, length, e, sink);
    }
    mfi_work_free(w);
    return true;
}

static void mfi_sink_reset(mfi_sink *sink) {
    uint32_t n, h;
    xx_mem_zero(sink, sizeof(*sink));
    for (n = 0U; n <= MFI_MAX_SIZE_CODE; ++n) {
        for (h = 0U; h < MFI_MAX_HEADS; ++h) {
            sink->min_r[n][h] = INT32_MAX;
            sink->max_r[n][h] = -1;
        }
        sink->last_cylinder[n] = -1;
        sink->last_head[n] = -1;
    }
}

/* The flat image uses the most common good sector size.  Sector numbers are
 * placed relative to the lowest one seen on each side, because some formats
 * number the second side on from the first. */
static bool mfi_find_geometry(Abstractformat *format, const mfi_table *t,
                              mfi_geometry *g) {
    mfi_sink sink;
    uint32_t n, best = 0U, h;
    uint64_t size;
    xx_mem_zero(g, sizeof(*g));
    mfi_sink_reset(&sink);
    if (!mfi_scan(format, t, &sink)) return false;
    for (n = 1U; n <= MFI_MAX_SIZE_CODE; ++n)
        if (sink.good[n] > sink.good[best]) best = n;
    if (sink.good[best] == 0U || sink.last_cylinder[best] < 0 ||
        sink.last_head[best] < 0)
        return true;
    g->size_code = best;
    g->cylinders = (uint32_t)sink.last_cylinder[best] + 1U;
    g->heads = (uint32_t)sink.last_head[best] + 1U;
    g->sectors = 0U;
    for (h = 0U; h < g->heads; ++h) {
        if (sink.max_r[best][h] < 0) continue;
        g->base[h] = sink.min_r[best][h];
        if ((uint32_t)(sink.max_r[best][h] - sink.min_r[best][h] + 1) >
            g->sectors)
            g->sectors =
                (uint32_t)(sink.max_r[best][h] - sink.min_r[best][h] + 1);
    }
    for (h = 0U; h < g->heads; ++h)
        if (sink.max_r[best][h] < 0) g->base[h] = g->base[h ? 0U : 1U];
    size = (uint64_t)g->cylinders * g->heads * g->sectors * (128U << best);
    if (g->sectors == 0U || size == 0U || size > MFI_MAX_OUTPUT) return true;
    g->image_size = size;
    g->has_image = true;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Members                                                                 */

static char *mfi_make_name(const mfi_table *t, const mfi_entry *e) {
    char buffer[32];
    size_t used = 0U;
    const char *prefix = "track";
    const char *suffix = ".bin";
    uint32_t value = e->cylinder;
    char *result;
    size_t index;
    for (index = 0U; prefix[index]; ++index) buffer[used++] = prefix[index];
    if (value >= 100U) buffer[used++] = (char)('0' + value / 100U);
    buffer[used++] = (char)('0' + (value / 10U) % 10U);
    buffer[used++] = (char)('0' + value % 10U);
    if (t->resolution != 0U) {
        buffer[used++] = '.';
        buffer[used++] = (char)('0' + e->quarter);
    }
    buffer[used++] = '_';
    buffer[used++] = (char)('0' + e->head);
    for (index = 0U; suffix[index]; ++index) buffer[used++] = suffix[index];
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

static char *mfi_copy_name(const char *name) {
    size_t length = xx_str_len(name);
    char *result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, name, length + 1U);
    return result;
}

static void mfi_stream_free(void *opaque) {
    mfi_stream *stream = (mfi_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool mfi_geometry_cached(Abstractformat *format, const mfi_table *t,
                                mfi_geometry *g) {
    xx_mame_floppy_image_mfi *archive = (xx_mame_floppy_image_mfi *)format;
    if (archive->scanned) {
        xx_mem_zero(g, sizeof(*g));
        g->has_image = archive->has_image;
        g->size_code = archive->image_size_code;
        g->cylinders = archive->image_cylinders;
        g->heads = archive->image_heads;
        g->sectors = archive->image_sectors;
        g->base[0] = archive->image_base[0];
        g->base[1] = archive->image_base[1];
        g->image_size = (uint64_t)g->cylinders * g->heads * g->sectors *
                        (128U << g->size_code);
        if (g->has_image &&
            (g->image_size == 0U || g->image_size > MFI_MAX_OUTPUT))
            return false;
        return true;
    }
    if (!mfi_find_geometry(format, t, g)) return false;
    archive->scanned = true;
    archive->has_image = g->has_image;
    archive->image_size_code = g->size_code;
    archive->image_cylinders = g->cylinders;
    archive->image_heads = g->heads;
    archive->image_sectors = g->sectors;
    archive->image_base[0] = g->base[0];
    archive->image_base[1] = g->base[1];
    return true;
}

static bool mfi_parse(Abstractformat *format, mfi_stream **result) {
    mfi_table *t;
    mfi_stream *stream;
    uint32_t index;
    size_t count = 0U;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    *result = NULL;
    t = (mfi_table *)xx_mem_alloc(sizeof(*t));
    if (!t) return false;
    stream = (mfi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream || !mfi_read_table(format->device, format->base_address, t) ||
        !mfi_geometry_cached(format, t, &stream->geometry)) {
        if (stream) xx_mem_free(stream);
        xx_mem_free(t);
        return false;
    }
    stream->archive_size = t->format_size;
    if (stream->geometry.has_image) {
        stream->items = (mfi_member *)xx_mem_calloc(1U, sizeof(mfi_member));
        if (!stream->items) goto fail;
        stream->items[0].name = mfi_copy_name("image.img");
        if (!stream->items[0].name) goto fail;
        stream->items[0].header_offset = format->base_address;
        stream->items[0].header_size = t->table_end;
        stream->items[0].data_offset = format->base_address;
        stream->items[0].packed_size = t->format_size;
        stream->items[0].unpacked_size = stream->geometry.image_size;
        stream->items[0].method = MFI_METHOD_SECTORS;
        stream->items[0].image = true;
        stream->count = 1U;
    } else if (t->usable != 0U) {
        stream->items =
            (mfi_member *)xx_mem_calloc(t->usable, sizeof(mfi_member));
        if (!stream->items) goto fail;
        for (index = 0U; index < t->count; ++index) {
            const mfi_entry *e = &t->entries[index];
            mfi_member *m;
            if (!e->usable) continue;
            m = &stream->items[count];
            m->name = mfi_make_name(t, e);
            if (!m->name) goto fail;
            m->header_offset = format->base_address + MFI_HEADER_SIZE +
                               (int64_t)index * MFI_ENTRY_SIZE;
            m->header_size = MFI_ENTRY_SIZE;
            m->data_offset = format->base_address + (int64_t)e->offset;
            m->packed_size = e->compressed;
            m->unpacked_size = e->uncompressed;
            m->method = MFI_METHOD_ZLIB;
            m->entry = index;
            ++count;
            stream->count = count;
        }
    }
    xx_mem_free(t);
    *result = stream;
    return true;
fail:
    mfi_stream_free(stream);
    xx_mem_free(t);
    return false;
}

static bool mfi_decode_image(Abstractformat *format, const mfi_stream *stream,
                             uint8_t **plain, size_t *plain_size) {
    const mfi_geometry *g = &stream->geometry;
    mfi_table *t;
    mfi_sink sink;
    uint64_t slots;
    bool ok;
    if (!g->has_image || g->image_size == 0U || g->image_size > MFI_MAX_OUTPUT)
        return false;
    t = (mfi_table *)xx_mem_alloc(sizeof(*t));
    if (!t) return false;
    if (!mfi_read_table(format->device, format->base_address, t)) {
        xx_mem_free(t);
        return false;
    }
    slots = g->image_size / (128U << g->size_code);
    mfi_sink_reset(&sink);
    sink.geometry = g;
    /* A sector that never decoded stays zero so the image keeps its shape. */
    sink.image = (uint8_t *)xx_mem_calloc(1U, (size_t)g->image_size);
    sink.filled = (uint8_t *)xx_mem_calloc(1U, (size_t)slots);
    ok = sink.image && sink.filled && mfi_scan(format, t, &sink);
    xx_mem_free(t);
    if (sink.filled) xx_mem_free(sink.filled);
    if (!ok) {
        if (sink.image) xx_mem_free(sink.image);
        return false;
    }
    *plain = sink.image;
    *plain_size = (size_t)g->image_size;
    return true;
}

static bool mfi_decode_track(Abstractformat *format, const mfi_member *m,
                             uint8_t **plain, size_t *plain_size) {
    mfi_table *t;
    mfi_entry *e;
    uint8_t *packed = NULL, *output = NULL;
    size_t written = 0U;
    bool ok = false;
    t = (mfi_table *)xx_mem_alloc(sizeof(*t));
    if (!t) return false;
    if (!mfi_read_table(format->device, format->base_address, t) ||
        m->entry >= t->count || !t->entries[m->entry].usable)
        goto done;
    e = &t->entries[m->entry];
    if ((uint64_t)e->uncompressed != m->unpacked_size) goto done;
    packed = (uint8_t *)xx_mem_alloc(e->compressed);
    output = (uint8_t *)xx_mem_alloc(e->uncompressed);
    if (!packed || !output ||
        !mfi_read_at(format->device, format->base_address + (int64_t)e->offset,
                     packed, e->compressed) ||
        !xx_zlib_stream_decode_memory(packed, e->compressed, output,
                                      e->uncompressed, &written) ||
        written != e->uncompressed)
        goto done;
    *plain = output;
    *plain_size = written;
    output = NULL;
    ok = true;
done:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    xx_mem_free(t);
    return ok;
}

static bool mfi_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *mfi_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool mfi_set_record(xx_archive_record *record, const mfi_member *m) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = m->header_offset;
    record->header_size = m->header_size;
    record->data_offset = m->data_offset;
    record->compressed_size = m->packed_size;
    return xx_archive_record_set_original_name(record, m->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)m->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          m->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          m->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_mame_floppy_image_mfi_init(xx_mame_floppy_image_mfi *archive,
                                   xx_io_device *device,
                                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MAME_FLOPPY_IMAGE_MFI_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mame-mfi");
    xx_format_set_extension(&archive->format, "mfi");
    archive->format.check_is_valid = xx_mame_floppy_image_mfi_check_is_valid;
    archive->format.handle_base_info =
        xx_mame_floppy_image_mfi_handle_base_info;
    archive->format.get_format_size = xx_mame_floppy_image_mfi_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mame_floppy_image_mfi_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mame_floppy_image_mfi_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mame_floppy_image_mfi_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mame_floppy_image_mfi_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mame_floppy_image_mfi_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mame_floppy_image_mfi_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_mame_floppy_image_mfi *xx_mame_floppy_image_mfi_create(xx_io_device *device,
                                                          int64_t base_address) {
    xx_mame_floppy_image_mfi *archive =
        (xx_mame_floppy_image_mfi *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_mame_floppy_image_mfi_init(archive, device, base_address);
    return archive;
}

void xx_mame_floppy_image_mfi_destroy(xx_mame_floppy_image_mfi *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_mame_floppy_image_mfi_free(xx_mame_floppy_image_mfi *archive) {
    if (!archive) return;
    xx_mame_floppy_image_mfi_destroy(archive);
    xx_mem_free(archive);
}

/* Header, table and the zlib header of one readable track: cheap enough for
 * the detector, which never needs the flux itself. */
bool xx_mame_floppy_image_mfi_check_is_valid(Abstractformat *format,
                                             xx_pd_struct *pd) {
    mfi_table *t;
    uint32_t index;
    bool valid;
    (void)pd;
    if (!format || !format->device || format->base_address < 0) return false;
    t = (mfi_table *)xx_mem_alloc(sizeof(*t));
    if (!t) return false;
    valid = mfi_read_table(format->device, format->base_address, t);
    if (valid && t->usable != 0U) {
        valid = false;
        for (index = 0U; index < t->count && !valid; ++index) {
            uint8_t zlib[2];
            const mfi_entry *e = &t->entries[index];
            if (!e->usable) continue;
            valid = mfi_read_at(format->device,
                                format->base_address + (int64_t)e->offset,
                                zlib, sizeof(zlib)) &&
                    xx_zlib_stream_header_is_valid(zlib, sizeof(zlib));
        }
    }
    xx_mem_free(t);
    return valid;
}

bool xx_mame_floppy_image_mfi_handle_base_info(Abstractformat *format,
                                               xx_pd_struct *pd) {
    mfi_stream *stream;
    xx_mame_floppy_image_mfi *archive;
    if (!format || !xx_mame_floppy_image_mfi_check_is_valid(format, pd) ||
        !mfi_parse(format, &stream))
        return false;
    archive = (xx_mame_floppy_image_mfi *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    mfi_stream_free(stream);
    return true;
}

int64_t xx_mame_floppy_image_mfi_get_format_size(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_mame_floppy_image_mfi_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_mame_floppy_image_mfi_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_mame_floppy_image_mfi_handle_base_info(format, pd))
               ? ((xx_mame_floppy_image_mfi *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_mame_floppy_image_mfi_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    mfi_stream *stream;
    xx_archive_record_state *state;
    if (!xx_mame_floppy_image_mfi_check_is_valid(format, pd) ||
        !mfi_parse(format, &stream))
        return NULL;
    if (stream->count == 0U) {
        mfi_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mfi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = mfi_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!mfi_copy_options(&state->options, options) ||
        !mfi_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_mame_floppy_image_mfi_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_mame_floppy_image_mfi_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    mfi_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (mfi_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = mfi_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_mame_floppy_image_mfi_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    mfi_stream *stream;
    mfi_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false, decoded;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (mfi_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    decoded = member->image
                  ? mfi_decode_image(format, stream, &plain, &plain_size)
                  : mfi_decode_track(format, member, &plain, &plain_size);
    if (!decoded) goto done;
    path_option = mfi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!result && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_mame_floppy_image_mfi_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
