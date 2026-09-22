/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LPAQ8 header, nine bytes:
 *
 *   0..1  "pQ"
 *   2     0x08, the format version
 *   3     memory level, ASCII '0'..'9'
 *   4..7  original size, BIG endian
 *   8     data mode, 0..2
 *
 * Everything after that is one context-mixing bitstream, which this reader
 * does not decode -- XArchive's XLPAQ8 is an XExternalArchive for the same
 * reason. The header fields are reported because they are real and cheap; no
 * archive record is advertised because the payload cannot be produced.
 *
 * The four validation bounds below (version byte, level digit, size ceiling,
 * mode ceiling) are exactly the ones XArchive applies. Together they are what
 * stops a two-byte "pQ" coincidence from being classified as LPAQ8.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lpaq8/xx_lpaq8.h"

#include "xxfclib/memory/xx_memory.h"

#include <stdio.h>

#define XX_LPAQ8_HEADER_SIZE 9U
#define XX_LPAQ8_VERSION_BYTE 0x08U
/* The reference caps the stored size at INT32_MAX; a value above it means the
 * field is not a size, so the stream is not LPAQ8. */
#define XX_LPAQ8_MAX_UNCOMPRESSED 0x7FFFFFFFU
#define XX_LPAQ8_MAX_DATA_MODE 2U

static void xx_lpaq8_vtable_destroy(Abstractformat *self);

static bool xx_lpaq8_read_header(Abstractformat *self,
                                 uint8_t header[XX_LPAQ8_HEADER_SIZE]) {
    size_t completed = 0U;

    if (!self || !self->device || self->base_address < 0 ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        return false;
    }
    while (completed < XX_LPAQ8_HEADER_SIZE) {
        ssize_t received = xx_io_read(self->device, header + completed,
                                      XX_LPAQ8_HEADER_SIZE - completed);
        if (received <= 0 ||
            (size_t)received > XX_LPAQ8_HEADER_SIZE - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

/*
 * Strictly more than the header: a nine-byte file is a header with no stream,
 * which the reference rejects with "<=" and so does this.
 */
static bool xx_lpaq8_probe(Abstractformat *self, xx_lpaq8 *out) {
    uint8_t header[XX_LPAQ8_HEADER_SIZE];
    uint32_t uncompressed;
    int64_t total;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address ||
        (uint64_t)(total - self->base_address) <= XX_LPAQ8_HEADER_SIZE) {
        return false;
    }
    if (!xx_lpaq8_read_header(self, header)) {
        return false;
    }
    if (header[0] != 'p' || header[1] != 'Q' ||
        header[2] != XX_LPAQ8_VERSION_BYTE ||
        header[3] < '0' || header[3] > '9' ||
        header[8] > XX_LPAQ8_MAX_DATA_MODE) {
        return false;
    }
    uncompressed = ((uint32_t)header[4] << 24) | ((uint32_t)header[5] << 16) |
                   ((uint32_t)header[6] << 8) | (uint32_t)header[7];
    if (uncompressed > XX_LPAQ8_MAX_UNCOMPRESSED) {
        return false;
    }
    if (out) {
        out->level = (uint8_t)(header[3] - '0');
        out->data_mode = header[8];
        out->uncompressed_size = uncompressed;
    }
    return true;
}

void xx_lpaq8_init(xx_lpaq8 *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    /* The one multi-byte field in the header is stored big-endian. */
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_LPAQ8;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lpaq8");
    xx_format_set_extension(&archive->format, "lpaq8");
    archive->format.check_is_valid = xx_lpaq8_check_is_valid;
    archive->format.handle_base_info = xx_lpaq8_handle_base_info;
    archive->format.get_format_size = xx_lpaq8_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lpaq8_get_number_of_archive_records;
    archive->format.destroy = xx_lpaq8_vtable_destroy;
}

xx_lpaq8 *xx_lpaq8_create(xx_io_device *device, int64_t base_address) {
    xx_lpaq8 *archive = (xx_lpaq8 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lpaq8_init(archive, device, base_address);
    return archive;
}

void xx_lpaq8_destroy(xx_lpaq8 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->level = 0U;
    archive->data_mode = 0U;
    archive->uncompressed_size = 0U;
}

void xx_lpaq8_free(xx_lpaq8 *archive) {
    if (!archive) return;
    xx_lpaq8_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lpaq8_vtable_destroy(Abstractformat *self) {
    xx_lpaq8_destroy((xx_lpaq8 *)self);
}

bool xx_lpaq8_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_lpaq8_probe(self, NULL);
}

bool xx_lpaq8_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lpaq8 *archive = (xx_lpaq8 *)self;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_lpaq8_probe(self, archive);
    if (!self->is_valid) {
        self->format_size = 0;
        return false;
    }
    /* The compressed stream carries no length of its own, so the format runs
     * to the end of the device. */
    self->format_size = xx_io_total_size(self->device) - self->base_address;
    return true;
}

int64_t xx_lpaq8_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lpaq8_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    /* The payload is not decodable here; advertising a member that can never
     * be extracted would be worse than reporting none. */
    (void)self;
    (void)pd;
    return 0U;
}

uint8_t xx_lpaq8_get_level(const xx_lpaq8 *archive) {
    return archive ? archive->level : 0U;
}

uint8_t xx_lpaq8_get_data_mode(const xx_lpaq8 *archive) {
    return archive ? archive->data_mode : 0U;
}

uint32_t xx_lpaq8_get_uncompressed_size(const xx_lpaq8 *archive) {
    return archive ? archive->uncompressed_size : 0U;
}
