/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private shared format carrier parser. No executable content is run.
 */
#ifndef XX_SFX_CARRIER_H
#define XX_SFX_CARRIER_H
#include "xx_carrier_helpers.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/algo/crc/xx_crc.h"
typedef Abstractformat *(*sfx_carrier_open)(xx_io_device *, int64_t);
typedef void (*sfx_carrier_close)(Abstractformat *);
/* The GEMDOS executable grammar is 28 bytes, six big-endian size/flag
 * words and an absolute/relocatable word. Symbol tables and relocations
 * stay in the outer carrier; its embedded member table is validated below. */
static XXFC_MAYBE_UNUSED bool sfx_carrier_carrier(Abstractformat *f, bool atari_only, int64_t *low, xx_pd_struct *pd) {
    uint8_t h[64];
    int64_t limit = pm_available(f), overlay, cab, cabend;
    uint64_t image, headers;
    if (limit < 28 || !pm_read(f, 0, h, 28))
        return false;
    if (h[0] == 0x60 && h[1] == 0x1a) {
        uint64_t text = xx_data_get_u32(h + 2, 4, 0, true), data = xx_data_get_u32(h + 6, 4, 0, true),
                 symbols = xx_data_get_u32(h + 14, 4, 0, true);
        uint16_t absolute = xx_data_get_u16(h + 26, 2, 0, true);
        if (!text || absolute > 1 || !carrier_range(limit, 28, text + data + symbols)) {
            return false;
        }
        *low = 28;
        return true;
    }
    if (atari_only || xx_rt_memcmp(h, "MZ", 2) || limit < 64 || !pm_read(f, 0, h, 64))
        return false;
    headers = (uint64_t)xx_data_get_u16(h + 8, 2, 0, false) * 16;
    image = xx_data_get_u16(h + 4, 2, 0, false);
    if (!image || xx_data_get_u16(h + 2, 2, 0, false) > 511 || headers < 28)
        return false;
    image = (image - 1) * 512 + (xx_data_get_u16(h + 2, 2, 0, false) ? xx_data_get_u16(h + 2, 2, 0, false) : 512);
    if (headers > image || image > (uint64_t)limit ||
        (uint64_t)xx_data_get_u16(h + 24, 2, 0, false) + 4U * xx_data_get_u16(h + 6, 2, 0, false) > headers)
        return false;
    if (carrier_pe(f, &overlay, &cab, &cabend, pd))
        *low = overlay;
    else
        *low = (int64_t)image;
    return *low <= limit;
}
/* Buffered, cancellable signature location is only a candidate locator.
 * The original complete archive grammar must accept the candidate, report
 * a physical logical extent, and enumerate its member table before export. */
static XXFC_MAYBE_UNUSED bool sfx_carrier_embedded(Abstractformat *f, pm_stream *s, int64_t low, const uint8_t *sig,
                                                   size_t siglen, int adjust, sfx_carrier_open open,
                                                   sfx_carrier_close close, const char *label, xx_pd_struct *pd) {
    int64_t limit = pm_available(f), at;
    size_t n, i, capacity = xx_get_file_buffer_size(), completed = 0;
    unsigned candidates = 0;
    uint8_t *scan;
    if (low < 0 || low >= limit || !siglen || siglen > 32) {
        return false;
    }
    n = limit - low > 16777216 ? 16777216U : (size_t)(limit - low);
    if (capacity > n)
        capacity = n;
    scan = (uint8_t *)xx_mem_alloc(capacity);
    if (!scan)
        return false;
    /* Retain the complete original scan-range I/O check before candidates. */
    while (completed < n) {
        size_t take = n - completed > capacity ? capacity : n - completed;
        if (carrier_stop(pd) || !pm_read(f, low + (int64_t)completed, scan, take)) {
            xx_mem_free(scan);
            return false;
        }
        completed += take;
    }
    xx_mem_free(scan);
    scan = NULL;
    for (i = 0; i + siglen <= n; ++i) {
        Abstractformat *reader;
        int64_t size;
        bool valid;
        if ((i & 4095U) == 0 && carrier_stop(pd)) {
            xx_mem_free(scan);
            return false;
        }
        {
            int64_t found = xx_io_find_bytes_buffer_optimize_ex(f->device, f->base_address + low + (int64_t)i,
                                                                (int64_t)(n - i), sig, siglen, capacity, pd);
            if (found < 0) {
                break;
            }
            i = (size_t)(found - f->base_address - low);
        }
        if (++candidates > 128)
            break;
        at = low + (int64_t)i + adjust;
        if (at < low)
            continue;
        reader = open(f->device, f->base_address + at);
        if (!reader) {
            xx_mem_free(scan);
            return false;
        }
        valid = xx_format_handle_base_info(reader, pd);
        size = reader->format_size;
        if (valid) {
            xx_archive_record_state *records = xx_format_create_archive_records_reading(reader, NULL, pd);
            const xx_archive_record *record;
            size_t count = 0;
            if (!records)
                valid = false;
            else {
                while ((record = xx_format_get_current_archive_record(reader, records)) != NULL) {
                    if (carrier_stop(pd) || ++count > 65536 || record->compressed_size < 0 ||
                        record->compressed_size > size) {
                        valid = false;
                        break;
                    }
                    if (!xx_format_archive_record_move_to_next(reader, records, pd))
                        break;
                }
                xx_format_free_archive_records_reading(reader, records);
                if (!count)
                    valid = false;
            }
        }
        if (valid && adjust == -2) {
            xx_archive_record_state *st = xx_format_create_archive_records_reading(reader, NULL, pd);
            const xx_archive_record *record;
            int64_t last = 0;
            uint8_t marker;
            if (!st)
                valid = false;
            else {
                while ((record = xx_format_get_current_archive_record(reader, st)) != NULL) {
                    int64_t end = record->data_offset + record->compressed_size;
                    if (end > last)
                        last = end;
                    if (!xx_format_archive_record_move_to_next(reader, st, pd))
                        break;
                }
                xx_format_free_archive_records_reading(reader, st);
                valid = !carrier_stop(pd) && last - f->base_address - at + 1 == size &&
                        pm_read(f, last - f->base_address, &marker, 1) && !marker;
            }
        }
        if (valid && size > 0 && carrier_range(limit, at, (uint64_t)size)) {
            close(reader);
            xx_mem_free(scan);
            if (!pm_add(f, s, label, at, size))
                return false;
            s->size = at + size;
            return true;
        }
        close(reader);
    }
    xx_mem_free(scan);
    return false;
}
static XXFC_MAYBE_UNUSED bool sfx_carrier_crc(Abstractformat *f, int64_t at, int64_t bytes, uint32_t expected,
                                              xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *b = NULL;
    bool buffer_result = false;
    uint32_t c = 0;
    while (bytes) {
        if (!b) {
            if ((uint64_t)(bytes) < capacity)
                capacity = (size_t)(bytes);
            b = (uint8_t *)xx_mem_alloc(capacity);
            if (!b) {
                buffer_result = (false);
                goto buffer_done;
            }
        }
        size_t n = (uint64_t)(uint64_t)(bytes) > capacity ? capacity : (size_t)bytes;
        if (carrier_stop(pd) || !pm_read(f, at, b, n)) {
            buffer_result = (false);
            goto buffer_done;
        }
        c = xx_crc32_calc(c, b, n);
        at += n;
        bytes -= n;
    }
    {
        buffer_result = (c == expected);
        goto buffer_done;
    }

buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
#endif
