/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for KryoFlux stream files ("trackNN.S.raw").  One file holds
 * the flux transitions of one side of one track, sampled for a few
 * revolutions, interleaved with out-of-band (OOB) blocks.  A disk is a set of
 * such files; this reader handles one of them.
 *
 * Written from the published KryoFlux stream protocol description:
 *   0x00-0x07  Flux2  value = (code << 8) | next byte
 *   0x08       Nop1   (1 byte)          0x09  Nop2 (2 bytes)
 *   0x0A       Nop3   (3 bytes)         0x0B  Ovl16: add 0x10000 to next flux
 *   0x0C       Flux3  value = (b1 << 8) | b2
 *   0x0D       OOB    type, 16-bit LE size, payload
 *   0x0E-0xFF  Flux1  value = code
 * OOB types: 1 StreamInfo, 2 Index, 3 StreamEnd, 4 KFInfo (ASCII), 0x0D EOF
 * (size field 0x0D0D, no payload).  StreamInfo and StreamEnd carry the count
 * of in-stream bytes (everything that is not OOB) sent so far.
 *
 * The member is the track's sectors.  The flux intervals are turned into bit
 * cells with a small frequency-tracking clock recovery, then searched for
 * IBM MFM (A1 A1 A1 FE / FB) and Amiga MFM (4489 4489, odd/even longs)
 * sectors, each checked against its CRC or checksum.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/kryoflux_stream/xx_kryoflux_stream.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef KRYOFLUX_STREAM
#define XX_KRYOFLUX_STREAM_FILE_TYPE XX_FILE_TYPE_KRYOFLUX_STREAM
#else
#define XX_KRYOFLUX_STREAM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define KF_OOB 0x0DU
#define KF_OOB_STREAM_INFO 0x01U
#define KF_OOB_INDEX 0x02U
#define KF_OOB_STREAM_END 0x03U
#define KF_OOB_KFINFO 0x04U
#define KF_OOB_EOF 0x0DU
#define KF_EOF_SIZE 0x0D0DU

#define KF_MAX_STREAM (64 * 1024 * 1024)  /* bytes of stream scanned at most */
#define KF_CHUNK 65536U
#define KF_MAX_PADDING 512                /* 0x0D filler kept after EOF */
#define KF_MIN_SIZE 12                    /* StreamEnd + EOF */
#define KF_HIST 2048U                     /* interval histogram, in ticks */
#define KF_MAX_CELLS (32U * 1024U * 1024U)
#define KF_MAX_RUN 64U                    /* cells emitted for one interval */
#define KF_SYNC 0x4489U
#define KF_IBM_SLOTS 256U
#define KF_IBM_MAX_CODE 7U
#define KF_IBM_MAX_SECTOR (128U << KF_IBM_MAX_CODE)
#define KF_IBM_ID_REACH 4096U             /* cells from ID end to data mark */
#define KF_AMIGA_SLOTS 22U
#define KF_AMIGA_SECTOR 512U
#define KF_AMIGA_SPAN (480U + 8192U)      /* cells from the first sync */

static uint32_t kf_le16(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U);
}

static uint32_t kf_le32(const uint8_t *b) {
    return kf_le16(b) | (kf_le16(b + 2U) << 16U);
}

static bool kf_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ---- buffered sequential input ---------------------------------------- */

typedef struct kf_input_s {
    xx_io_device *device;
    int64_t next;   /* absolute offset of the next chunk */
    int64_t stop;   /* absolute end of what may be read */
    uint8_t *buffer;
    size_t fill;
    size_t at;
} kf_input;

static bool kf_input_byte(kf_input *in, uint8_t *value) {
    if (in->at >= in->fill) {
        const int64_t left = in->stop - in->next;
        size_t want;
        if (left <= 0) return false;
        want = left > (int64_t)KF_CHUNK ? (size_t)KF_CHUNK : (size_t)left;
        if (!kf_read_at(in->device, in->next, in->buffer, want)) return false;
        in->next += (int64_t)want;
        in->fill = want;
        in->at = 0U;
    }
    *value = in->buffer[in->at++];
    return true;
}

static bool kf_input_bytes(kf_input *in, uint8_t *out, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index)
        if (!kf_input_byte(in, out + index)) return false;
    return true;
}

static int64_t kf_input_tell(const kf_input *in) {
    return in->next - (int64_t)(in->fill - in->at);
}

/* ---- stream walk ------------------------------------------------------ */

typedef bool (*kf_flux_fn)(void *context, uint32_t ticks);

typedef struct kf_summary_s {
    int64_t size;          /* through the EOF block and its 0x0D filler */
    uint64_t flux_count;
    uint32_t index_count;
    uint32_t info_count;
    uint32_t end_result;
} kf_summary;

/* Walks the whole stream once, checking every block, and hands each flux
 * interval (in sample-clock ticks) to fn when one is given.  The stream must
 * end with a StreamEnd block followed by the EOF block; while StreamEnd
 * reports success every position field must match the in-stream byte count. */
static bool kf_scan(xx_io_device *device, int64_t base, int64_t available,
                    kf_summary *summary, kf_flux_fn fn, void *context) {
    kf_input in;
    uint64_t overflow = 0U;
    uint32_t position = 0U;
    uint32_t mismatches = 0U;
    bool have_end = false;
    bool ok = false;
    uint8_t code;
    if (!device || !summary || base < 0 || available < KF_MIN_SIZE)
        return false;
    xx_mem_zero(summary, sizeof(*summary));
    xx_mem_zero(&in, sizeof(in));
    in.device = device;
    in.next = base;
    in.stop = base + (available > KF_MAX_STREAM ? (int64_t)KF_MAX_STREAM
                                                : available);
    in.buffer = (uint8_t *)xx_mem_alloc(KF_CHUNK);
    if (!in.buffer) return false;
    for (;;) {
        uint64_t value;
        uint8_t extra[3];
        if (!kf_input_byte(&in, &code)) goto done;
        if (code == KF_OOB) {
            uint8_t head[3];
            uint8_t payload[12];
            uint32_t type, size, index;
            if (!kf_input_bytes(&in, head, 3U)) goto done;
            type = head[0];
            size = kf_le16(head + 1U);
            if (type == KF_OOB_EOF) {
                if (size != KF_EOF_SIZE) goto done;
                break;
            }
            switch (type) {
            case KF_OOB_STREAM_INFO:
            case KF_OOB_STREAM_END:
                if (size < 8U || !kf_input_bytes(&in, payload, 8U)) goto done;
                if (kf_le32(payload) != position) ++mismatches;
                if (type == KF_OOB_STREAM_END) {
                    if (have_end) goto done;
                    have_end = true;
                    summary->end_result = kf_le32(payload + 4U);
                }
                size -= 8U;
                break;
            case KF_OOB_INDEX:
                if (size < 12U || !kf_input_bytes(&in, payload, 12U))
                    goto done;
                ++summary->index_count;
                size -= 12U;
                break;
            case KF_OOB_KFINFO:
                /* Printable "name=value, ..." text, NUL terminated. */
                for (index = 0U; index < size; ++index) {
                    uint8_t c;
                    if (!kf_input_byte(&in, &c) ||
                        (c != 0U && (c < 0x20U || c > 0x7eU)))
                        goto done;
                }
                ++summary->info_count;
                size = 0U;
                break;
            default:
                goto done;
            }
            for (index = 0U; index < size; ++index)
                if (!kf_input_byte(&in, extra)) goto done;
            continue;
        }
        /* Nothing but OOB blocks may follow StreamEnd. */
        if (have_end) goto done;
        if (code <= 0x07U) {
            if (!kf_input_byte(&in, extra)) goto done;
            value = ((uint64_t)code << 8U) | extra[0];
            position += 2U;
        } else if (code == 0x08U) {
            position += 1U;
            continue;
        } else if (code == 0x09U) {
            if (!kf_input_byte(&in, extra)) goto done;
            position += 2U;
            continue;
        } else if (code == 0x0aU) {
            if (!kf_input_bytes(&in, extra, 2U)) goto done;
            position += 3U;
            continue;
        } else if (code == 0x0bU) {
            overflow += 0x10000U;
            position += 1U;
            continue;
        } else if (code == 0x0cU) {
            if (!kf_input_bytes(&in, extra, 2U)) goto done;
            value = ((uint64_t)extra[0] << 8U) | extra[1];
            position += 3U;
        } else {
            value = code;
            position += 1U;
        }
        value += overflow;
        overflow = 0U;
        ++summary->flux_count;
        if (fn && !fn(context, value > 0xffffffffU ? 0xffffffffU
                                                   : (uint32_t)value))
            fn = NULL; /* the consumer is full; keep validating */
    }
    /* A reported buffering or index problem may legitimately leave the
     * position fields out of step; a clean stream may not. */
    if (!have_end || (summary->end_result == 0U && mismatches != 0U))
        goto done;
    summary->size = kf_input_tell(&in) - base;
    {
        int32_t count;
        for (count = 0; count < KF_MAX_PADDING; ++count) {
            if (!kf_input_byte(&in, &code) || code != KF_OOB) break;
            summary->size = kf_input_tell(&in) - base;
        }
    }
    ok = true;
done:
    xx_mem_free(in.buffer);
    return ok;
}

/* ---- clock recovery --------------------------------------------------- */

typedef struct kf_cells_s {
    uint8_t *data;
    size_t bytes;
    size_t count;
} kf_cells;

static void kf_cells_free(kf_cells *cells) {
    if (cells->data) xx_mem_free(cells->data);
    xx_mem_zero(cells, sizeof(*cells));
}

/* Appends run - 1 empty cells and one cell holding a transition. */
static bool kf_cells_push(kf_cells *cells, size_t run) {
    size_t last;
    if (run == 0U || run > KF_MAX_CELLS || cells->count > KF_MAX_CELLS - run)
        return false;
    last = cells->count + run - 1U;
    if ((last >> 3U) >= cells->bytes) {
        size_t grown = cells->bytes ? cells->bytes * 2U : 65536U;
        uint8_t *data;
        while (grown <= (last >> 3U)) grown *= 2U;
        if (grown > KF_MAX_CELLS / 8U) grown = KF_MAX_CELLS / 8U;
        if (grown <= (last >> 3U)) return false;
        data = (uint8_t *)xx_mem_realloc(cells->data, grown);
        if (!data) return false;
        xx_mem_zero(data + cells->bytes, grown - cells->bytes);
        cells->data = data;
        cells->bytes = grown;
    }
    cells->data[last >> 3U] |= (uint8_t)(0x80U >> (last & 7U));
    cells->count = last + 1U;
    return true;
}

static uint32_t kf_cells_get(const kf_cells *cells, size_t position,
                             uint32_t width) {
    uint32_t value = 0U;
    uint32_t k;
    for (k = 0U; k < width; ++k) {
        const size_t at = position + k;
        value = (value << 1U) |
                ((uint32_t)(cells->data[at >> 3U] >> (7U - (at & 7U))) & 1U);
    }
    return value;
}

static bool kf_histogram_add(void *context, uint32_t ticks) {
    uint32_t *histogram = (uint32_t *)context;
    if (ticks < KF_HIST) ++histogram[ticks];
    return true;
}

/* The shortest common MFM interval is two cells.  Take the first histogram
 * peak that holds at least a quarter of the tallest one, and average the
 * intervals within 1/8 of it.  Returns the cell length in ticks << 16. */
static uint64_t kf_estimate_cell(const uint32_t *histogram) {
    uint32_t tallest = 0U;
    uint32_t bin, peak = 0U;
    uint64_t weight = 0U, total = 0U;
    for (bin = 8U; bin + 1U < KF_HIST; ++bin) {
        const uint32_t sum = histogram[bin - 1U] + histogram[bin] +
                             histogram[bin + 1U];
        if (sum > tallest) tallest = sum;
    }
    if (tallest < 64U) return 0U;
    for (bin = 8U; bin + 2U < KF_HIST; ++bin) {
        const uint32_t sum = histogram[bin - 1U] + histogram[bin] +
                             histogram[bin + 1U];
        const uint32_t next = histogram[bin] + histogram[bin + 1U] +
                              histogram[bin + 2U];
        if (sum >= tallest / 4U && sum >= next) {
            peak = bin;
            break;
        }
    }
    if (peak == 0U) return 0U;
    for (bin = peak - peak / 8U; bin <= peak + peak / 8U && bin < KF_HIST;
         ++bin) {
        weight += (uint64_t)histogram[bin] * bin;
        total += histogram[bin];
    }
    if (total == 0U) return 0U;
    return (weight << 15U) / total; /* half of the mean, in 16.16 */
}

typedef struct kf_pll_s {
    kf_cells *cells;
    uint64_t cell;     /* current cell length, ticks << 16 */
    uint64_t low;
    uint64_t high;
    uint64_t carry;    /* ticks of a glitch shorter than half a cell */
} kf_pll;

static bool kf_pll_add(void *context, uint32_t ticks) {
    kf_pll *pll = (kf_pll *)context;
    const uint64_t span = ((uint64_t)ticks + pll->carry) << 16U;
    uint64_t run = (span + pll->cell / 2U) / pll->cell;
    if (run == 0U) {
        pll->carry += ticks;
        return true;
    }
    pll->carry = 0U;
    if (run <= KF_MAX_RUN) {
        /* Nudge the clock by 1/16 of the per-cell error. */
        const int64_t error = (int64_t)span - (int64_t)(run * pll->cell);
        const int64_t step = error / (int64_t)run / 16;
        int64_t cell = (int64_t)pll->cell + step;
        if (cell < (int64_t)pll->low) cell = (int64_t)pll->low;
        if (cell > (int64_t)pll->high) cell = (int64_t)pll->high;
        pll->cell = (uint64_t)cell;
    } else {
        run = KF_MAX_RUN; /* no transitions for a while: nothing to decode */
    }
    return kf_cells_push(pll->cells, (size_t)run);
}

/* ---- sector search ---------------------------------------------------- */

typedef struct kf_slot_s {
    uint8_t *data;
    uint32_t size;
    uint8_t cylinder;
    uint8_t head;
    uint8_t code;
    bool id_seen;
    bool good;
} kf_slot;

typedef struct kf_track_s {
    kf_slot ibm[KF_IBM_SLOTS];
    kf_slot amiga[KF_AMIGA_SLOTS];
    uint8_t field[KF_IBM_MAX_SECTOR + 2U];
    uint32_t amiga_track;
    bool amiga_track_set;
    bool id_open;          /* an ID field waits for its data field */
    uint8_t id_sector;
    size_t id_end;
} kf_track;

static void kf_track_free(kf_track *track) {
    size_t index;
    if (!track) return;
    for (index = 0U; index < KF_IBM_SLOTS; ++index)
        if (track->ibm[index].data) xx_mem_free(track->ibm[index].data);
    for (index = 0U; index < KF_AMIGA_SLOTS; ++index)
        if (track->amiga[index].data) xx_mem_free(track->amiga[index].data);
    xx_mem_free(track);
}

static uint16_t kf_crc16(const uint8_t *data, size_t size, uint16_t crc) {
    size_t index;
    uint32_t bit;
    for (index = 0U; index < size; ++index) {
        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc & 0x8000U) ? ((crc << 1U) ^ 0x1021U)
                                             : (crc << 1U));
    }
    return crc;
}

/* In an MFM cell pair the clock comes first and the data bit second. */
static uint8_t kf_mfm_byte(const kf_cells *cells, size_t position) {
    const uint32_t raw = kf_cells_get(cells, position, 16U);
    uint32_t value = 0U;
    int32_t bit;
    for (bit = 14; bit >= 0; bit -= 2)
        value = (value << 1U) | ((raw >> (uint32_t)bit) & 1U);
    return (uint8_t)value;
}

/* Keeps the first copy of a sector, replacing it only by a copy that passes
 * its check when the kept one did not. */
static bool kf_slot_store(kf_slot *slot, const uint8_t *data, uint32_t size,
                          bool good) {
    if (slot->data) {
        if (slot->good || !good || slot->size != size) return true;
    } else {
        slot->data = (uint8_t *)xx_mem_alloc(size);
        if (!slot->data) return false;
        slot->size = size;
    }
    xx_mem_copy(slot->data, data, size);
    slot->good = good;
    return true;
}

/* IBM: A1 A1 A1 (4489 x3) then FE + C H R N CRC, or FB/F8 + data + CRC.
 * Returns the cells consumed from the first sync, 0 when nothing matched. */
static size_t kf_try_ibm(const kf_cells *cells, size_t start,
                         kf_track *track, bool *failed) {
    const size_t mark_at = start + 48U;
    uint8_t mark, preamble[4];
    uint16_t crc;
    size_t index;
    if (mark_at + 16U > cells->count ||
        kf_cells_get(cells, start + 16U, 16U) != KF_SYNC ||
        kf_cells_get(cells, start + 32U, 16U) != KF_SYNC)
        return 0U;
    mark = kf_mfm_byte(cells, mark_at);
    preamble[0] = 0xa1U;
    preamble[1] = 0xa1U;
    preamble[2] = 0xa1U;
    preamble[3] = mark;
    if (mark == 0xfeU) {
        kf_slot *slot;
        if (mark_at + 16U + 6U * 16U > cells->count) return 48U;
        for (index = 0U; index < 6U; ++index)
            track->field[index] =
                kf_mfm_byte(cells, mark_at + 16U + index * 16U);
        crc = kf_crc16(track->field, 4U, kf_crc16(preamble, 4U, 0xffffU));
        if (crc != (uint16_t)(((uint32_t)track->field[4] << 8U) |
                              track->field[5]))
            return 48U;
        if (track->field[3] > KF_IBM_MAX_CODE) return 48U;
        slot = &track->ibm[track->field[2]];
        if (!slot->id_seen) {
            slot->id_seen = true;
            slot->cylinder = track->field[0];
            slot->head = track->field[1];
            slot->code = track->field[3];
        }
        track->id_open = slot->code == track->field[3];
        track->id_sector = track->field[2];
        track->id_end = mark_at + 16U + 6U * 16U;
        return 64U + 6U * 16U;
    }
    if (mark == 0xfbU || mark == 0xf8U) {
        kf_slot *slot;
        uint32_t size;
        bool good;
        if (!track->id_open || start < track->id_end ||
            start - track->id_end > KF_IBM_ID_REACH)
            return 48U;
        track->id_open = false;
        slot = &track->ibm[track->id_sector];
        size = 128U << slot->code;
        if (mark_at + 16U + ((size_t)size + 2U) * 16U > cells->count)
            return 48U;
        for (index = 0U; index < (size_t)size + 2U; ++index)
            track->field[index] =
                kf_mfm_byte(cells, mark_at + 16U + index * 16U);
        crc = kf_crc16(track->field, size, kf_crc16(preamble, 4U, 0xffffU));
        good = crc == (uint16_t)(((uint32_t)track->field[size] << 8U) |
                                 track->field[size + 1U]);
        if (!kf_slot_store(slot, track->field, size, good)) *failed = true;
        return 64U + ((size_t)size + 2U) * 16U;
    }
    return 48U;
}

static uint32_t kf_amiga_long(const kf_cells *cells, size_t odd, size_t even) {
    return ((kf_cells_get(cells, odd, 32U) & 0x55555555U) << 1U) |
           (kf_cells_get(cells, even, 32U) & 0x55555555U);
}

/* Amiga: 4489 4489, then odd/even halves of the info long (0xFF, track,
 * sector, sectors to gap), a 16-byte label, the header and data checksums
 * and 512 data bytes.  Checksums XOR the raw longs under 0x55555555. */
static size_t kf_try_amiga(const kf_cells *cells, size_t start,
                           kf_track *track, bool *failed) {
    const size_t body = start + 32U;
    uint32_t info, sum, stored, index;
    bool good;
    if (start + KF_AMIGA_SPAN > cells->count ||
        kf_cells_get(cells, start + 16U, 16U) != KF_SYNC ||
        kf_cells_get(cells, body, 16U) == KF_SYNC)
        return 0U;
    info = kf_amiga_long(cells, body, body + 32U);
    if ((info >> 24U) != 0xffU || ((info >> 8U) & 0xffU) >= KF_AMIGA_SLOTS)
        return 0U;
    sum = 0U;
    for (index = 0U; index < 10U; ++index)
        sum ^= kf_cells_get(cells, body + (size_t)index * 32U, 32U);
    if ((sum & 0x55555555U) != kf_amiga_long(cells, body + 320U, body + 352U))
        return 0U;
    stored = kf_amiga_long(cells, body + 384U, body + 416U);
    sum = 0U;
    for (index = 0U; index < 128U; ++index) {
        const size_t odd = body + 448U + (size_t)index * 32U;
        const size_t even = odd + 4096U;
        const uint32_t raw_odd = kf_cells_get(cells, odd, 32U);
        const uint32_t raw_even = kf_cells_get(cells, even, 32U);
        const uint32_t value = ((raw_odd & 0x55555555U) << 1U) |
                               (raw_even & 0x55555555U);
        sum ^= raw_odd ^ raw_even;
        track->field[index * 4U] = (uint8_t)(value >> 24U);
        track->field[index * 4U + 1U] = (uint8_t)(value >> 16U);
        track->field[index * 4U + 2U] = (uint8_t)(value >> 8U);
        track->field[index * 4U + 3U] = (uint8_t)value;
    }
    good = (sum & 0x55555555U) == stored;
    if (good && !track->amiga_track_set) {
        track->amiga_track = (info >> 16U) & 0xffU;
        track->amiga_track_set = true;
    }
    if (!kf_slot_store(&track->amiga[(info >> 8U) & 0xffU], track->field,
                       KF_AMIGA_SECTOR, good))
        *failed = true;
    return KF_AMIGA_SPAN;
}

static bool kf_find_sectors(const kf_cells *cells, kf_track *track) {
    size_t at = 0U, loaded = 0U;
    uint32_t shift = 0U;
    bool failed = false;
    while (at < cells->count && !failed) {
        size_t start, used;
        shift = ((shift << 1U) |
                 ((uint32_t)(cells->data[at >> 3U] >> (7U - (at & 7U))) & 1U)) &
                0xffffU;
        ++at;
        if (++loaded < 16U || shift != KF_SYNC) continue;
        start = at - 16U;
        used = kf_try_ibm(cells, start, track, &failed);
        if (used == 0U) used = kf_try_amiga(cells, start, track, &failed);
        if (used > 16U) {
            at = start + used;
            loaded = 0U;
            shift = 0U;
        }
    }
    return !failed;
}

/* ---- track image ------------------------------------------------------ */

typedef struct kf_image_s {
    uint8_t *data;
    size_t size;
    char name[32];
} kf_image;

static void kf_put_number(char *out, size_t *used, uint32_t value,
                          uint32_t digits) {
    char text[10];
    uint32_t count = 0U;
    do {
        text[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(text));
    while (count < digits && count < sizeof(text)) text[count++] = '0';
    while (count != 0U) out[(*used)++] = text[--count];
}

/* "trackCC.H.img": reader-owned, so safe by construction. */
static void kf_make_name(char *out, uint32_t cylinder, uint32_t head) {
    size_t used = 0U;
    const char *prefix = "track";
    const char *suffix = ".img";
    while (*prefix) out[used++] = *prefix++;
    kf_put_number(out, &used, cylinder & 0xffU, 2U);
    out[used++] = '.';
    kf_put_number(out, &used, head & 0xffU, 1U);
    while (*suffix) out[used++] = *suffix++;
    out[used] = 0;
}

/* Amiga tracks are 11 (DD) or 22 (HD) sectors of 512 bytes, numbered from 0.
 * IBM sectors are laid out by sector number from the lowest one seen; when
 * every ID has the same size the gaps are kept, otherwise the sectors are
 * packed in order.  Missing sectors and ones failing their check are zero. */
static bool kf_assemble(const kf_track *track, kf_image *image) {
    uint32_t ibm_good = 0U, amiga_good = 0U, index;
    xx_mem_zero(image, sizeof(*image));
    for (index = 0U; index < KF_IBM_SLOTS; ++index)
        if (track->ibm[index].good) ++ibm_good;
    for (index = 0U; index < KF_AMIGA_SLOTS; ++index)
        if (track->amiga[index].good) ++amiga_good;
    if (amiga_good != 0U && amiga_good >= ibm_good) {
        uint32_t count = 11U;
        for (index = 11U; index < KF_AMIGA_SLOTS; ++index)
            if (track->amiga[index].good) count = KF_AMIGA_SLOTS;
        image->size = (size_t)count * KF_AMIGA_SECTOR;
        image->data = (uint8_t *)xx_mem_alloc(image->size);
        if (!image->data) return false;
        xx_mem_zero(image->data, image->size);
        for (index = 0U; index < count; ++index)
            if (track->amiga[index].good)
                xx_mem_copy(image->data + (size_t)index * KF_AMIGA_SECTOR,
                            track->amiga[index].data, KF_AMIGA_SECTOR);
        kf_make_name(image->name, track->amiga_track >> 1U,
                     track->amiga_track & 1U);
        return true;
    }
    if (ibm_good != 0U) {
        uint32_t first = KF_IBM_SLOTS, last = 0U, name_slot = KF_IBM_SLOTS;
        bool uniform = true;
        size_t at = 0U;
        for (index = 0U; index < KF_IBM_SLOTS; ++index) {
            const kf_slot *slot = &track->ibm[index];
            if (!slot->id_seen) continue;
            if (first == KF_IBM_SLOTS) first = index;
            else if (slot->code != track->ibm[first].code) uniform = false;
            last = index;
            if (slot->good && name_slot == KF_IBM_SLOTS) name_slot = index;
        }
        if (uniform) {
            image->size = (size_t)(last - first + 1U) *
                          ((size_t)128U << track->ibm[first].code);
        } else {
            for (index = first; index <= last; ++index)
                if (track->ibm[index].id_seen)
                    image->size += (size_t)128U << track->ibm[index].code;
        }
        image->data = (uint8_t *)xx_mem_alloc(image->size);
        if (!image->data) return false;
        xx_mem_zero(image->data, image->size);
        for (index = first; index <= last; ++index) {
            const kf_slot *slot = &track->ibm[index];
            const size_t size = (size_t)128U << slot->code;
            if (uniform)
                at = (size_t)(index - first) *
                     ((size_t)128U << track->ibm[first].code);
            else if (!slot->id_seen)
                continue;
            if (slot->good && slot->size == size && at + size <= image->size)
                xx_mem_copy(image->data + at, slot->data, size);
            if (!uniform) at += size;
        }
        kf_make_name(image->name, track->ibm[name_slot].cylinder,
                     track->ibm[name_slot].head);
        return true;
    }
    return true; /* a valid stream with no sector this reader can decode */
}

/* Structure first, then (when asked) clock recovery and sector search. */
static bool kf_decode(xx_io_device *device, int64_t base, int64_t available,
                      kf_summary *summary, kf_image *image) {
    uint32_t *histogram;
    kf_cells cells;
    kf_pll pll;
    kf_track *track;
    bool ok = false;
    xx_mem_zero(image, sizeof(*image));
    histogram = (uint32_t *)xx_mem_calloc(KF_HIST, sizeof(uint32_t));
    if (!histogram) return false;
    if (!kf_scan(device, base, available, summary, kf_histogram_add,
                 histogram)) {
        xx_mem_free(histogram);
        return false;
    }
    xx_mem_zero(&pll, sizeof(pll));
    pll.cell = kf_estimate_cell(histogram);
    xx_mem_free(histogram);
    if (pll.cell < ((uint64_t)2U << 16U)) return true; /* nothing to decode */
    pll.low = pll.cell - pll.cell / 10U;
    pll.high = pll.cell + pll.cell / 10U;
    xx_mem_zero(&cells, sizeof(cells));
    pll.cells = &cells;
    track = (kf_track *)xx_mem_calloc(1U, sizeof(*track));
    if (!track) return false;
    if (kf_scan(device, base, available, summary, kf_pll_add, &pll) &&
        cells.count != 0U && kf_find_sectors(&cells, track) &&
        kf_assemble(track, image))
        ok = true;
    else if (cells.count == 0U)
        ok = true;
    kf_cells_free(&cells);
    kf_track_free(track);
    return ok;
}

/* ---- archive plumbing ------------------------------------------------- */

typedef struct kf_stream_s {
    kf_image image;
    int64_t archive_size;
    int64_t header_offset;
    size_t count;
    size_t index;
} kf_stream;

static void kf_stream_free(void *opaque) {
    kf_stream *stream = (kf_stream *)opaque;
    if (!stream) return;
    if (stream->image.data) xx_mem_free(stream->image.data);
    xx_mem_free(stream);
}

static bool kf_bounds(Abstractformat *format, int64_t *available) {
    int64_t total;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    *available = total - format->base_address;
    return *available >= KF_MIN_SIZE;
}

static bool kf_parse(Abstractformat *format, bool decode, kf_stream **result) {
    kf_stream *stream;
    int64_t available;
    if (!result || !kf_bounds(format, &available)) return false;
    stream = (kf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    {
        kf_summary summary;
        const bool ok =
            decode ? kf_decode(format->device, format->base_address,
                               available, &summary, &stream->image)
                   : kf_scan(format->device, format->base_address, available,
                             &summary, NULL, NULL);
        if (!ok) {
            kf_stream_free(stream);
            return false;
        }
        stream->archive_size = summary.size;
    }
    stream->header_offset = format->base_address;
    stream->count = stream->image.data ? 1U : 0U;
    *result = stream;
    return true;
}

static bool kf_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *kf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool kf_set_record(xx_archive_record *record, const kf_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    /* The flux of every sector is spread over the whole file, so the
     * member's source extent is the stream itself. */
    record->header_offset = stream->header_offset;
    record->header_size = 0;
    record->data_offset = stream->header_offset;
    record->compressed_size = stream->archive_size;
    return xx_archive_record_set_original_name(record, stream->image.name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->archive_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)stream->image.size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_kryoflux_stream_init(xx_kryoflux_stream *archive,
                             xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_KRYOFLUX_STREAM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-kryoflux-stream");
    xx_format_set_extension(&archive->format, "raw");
    archive->format.check_is_valid = xx_kryoflux_stream_check_is_valid;
    archive->format.handle_base_info = xx_kryoflux_stream_handle_base_info;
    archive->format.get_format_size = xx_kryoflux_stream_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_kryoflux_stream_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_kryoflux_stream_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_kryoflux_stream_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_kryoflux_stream_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_kryoflux_stream_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_kryoflux_stream_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_kryoflux_stream *xx_kryoflux_stream_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_kryoflux_stream *archive =
        (xx_kryoflux_stream *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_kryoflux_stream_init(archive, device, base_address);
    return archive;
}

void xx_kryoflux_stream_destroy(xx_kryoflux_stream *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_kryoflux_stream_free(xx_kryoflux_stream *archive) {
    if (!archive) return;
    xx_kryoflux_stream_destroy(archive);
    xx_mem_free(archive);
}

/* Structure only: no clock recovery, so the probe stays a single buffered
 * pass over the stream. */
bool xx_kryoflux_stream_check_is_valid(Abstractformat *format,
                                       xx_pd_struct *pd) {
    kf_stream *stream;
    (void)pd;
    if (!kf_parse(format, false, &stream)) return false;
    kf_stream_free(stream);
    return true;
}

bool xx_kryoflux_stream_handle_base_info(Abstractformat *format,
                                         xx_pd_struct *pd) {
    kf_stream *stream;
    xx_kryoflux_stream *archive;
    (void)pd;
    if (!format || !kf_parse(format, true, &stream)) return false;
    archive = (xx_kryoflux_stream *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    kf_stream_free(stream);
    return true;
}

int64_t xx_kryoflux_stream_get_format_size(Abstractformat *format,
                                           xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_kryoflux_stream_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_kryoflux_stream_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_kryoflux_stream_handle_base_info(format, pd))
               ? ((xx_kryoflux_stream *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_kryoflux_stream_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    kf_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!kf_parse(format, true, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        kf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = kf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!kf_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count != 0U) {
        if (!kf_set_record(&state->current_record, stream)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
    }
    return state;
}

const xx_archive_record *xx_kryoflux_stream_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_kryoflux_stream_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    kf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (kf_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = kf_set_record(&state->current_record, stream);
    return state->has_record;
}

bool xx_kryoflux_stream_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    kf_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    size_t written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (kf_stream *)state->internal_state) ||
        stream->index >= stream->count || !stream->image.data ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = kf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
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
               ? xx_str_concat3(base, "/", stream->image.name)
               : xx_str_concat(base, stream->image.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < stream->image.size) {
            ssize_t amount = xx_io_write(destination,
                                         stream->image.data + written,
                                         stream->image.size - written);
            if (amount <= 0 || (size_t)amount > stream->image.size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_kryoflux_stream_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
