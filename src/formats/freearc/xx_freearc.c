/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FreeArc header, eight bytes:
 *
 *   0..3  "ArC\x01"
 *   4..5  uint16 LE flags
 *   6..7  uint16 LE version
 *
 * followed immediately by the first block, which repeats the same four magic
 * bytes at offset 8. That repetition is the whole reason this is identifiable:
 * "ArC\x01" on its own is four bytes that could open anything, whereas the
 * same four bytes appearing again at a fixed distance is not a coincidence
 * ordinary data produces.
 *
 * FreeArc members sit behind a configurable codec chain -- LZMA, PPMD,
 * Tornado, GRZip and more, in whatever order the archive's directory block
 * names -- so there is no single codec to implement. XArchive's XFREEARC is an
 * XExternalArchive for the same reason, and this reader advertises no records.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/freearc/xx_freearc.h"

#include "xxfclib/memory/xx_memory.h"

#include <stdio.h>

#define XX_FREEARC_SIGNATURE_SIZE 4U
#define XX_FREEARC_HEADER_SIZE 8
#define XX_FREEARC_MIN_SIZE (XX_FREEARC_HEADER_SIZE + (int)XX_FREEARC_SIGNATURE_SIZE)

static const uint8_t XX_FREEARC_MAGIC[XX_FREEARC_SIGNATURE_SIZE] = {
    'A', 'r', 'C', 0x01U};

static void xx_freearc_vtable_destroy(Abstractformat *self);

static bool xx_freearc_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_freearc_probe(Abstractformat *self, xx_freearc *out) {
    uint8_t header[XX_FREEARC_HEADER_SIZE];
    uint8_t block_magic[XX_FREEARC_SIGNATURE_SIZE];
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < XX_FREEARC_MIN_SIZE) {
        return false;
    }
    if (!xx_freearc_read_at(self, self->base_address, header,
                            sizeof(header)) ||
        xx_rt_memcmp(header, XX_FREEARC_MAGIC, XX_FREEARC_SIGNATURE_SIZE) !=
            0) {
        return false;
    }
    /* The first block repeats the magic at offset 8. Without this second
     * check a four-byte prefix would be the entire evidence. */
    if (!xx_freearc_read_at(self, self->base_address + XX_FREEARC_HEADER_SIZE,
                            block_magic, sizeof(block_magic)) ||
        xx_rt_memcmp(block_magic, XX_FREEARC_MAGIC,
                     XX_FREEARC_SIGNATURE_SIZE) != 0) {
        return false;
    }

    if (out) {
        out->flags = (uint16_t)((uint16_t)header[4] |
                                ((uint16_t)header[5] << 8));
        out->version = (uint16_t)((uint16_t)header[6] |
                                  ((uint16_t)header[7] << 8));
    }
    return true;
}

void xx_freearc_init(xx_freearc *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_FREEARC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-freearc");
    xx_format_set_extension(&archive->format, "arc");
    archive->format.check_is_valid = xx_freearc_check_is_valid;
    archive->format.handle_base_info = xx_freearc_handle_base_info;
    archive->format.get_format_size = xx_freearc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_freearc_get_number_of_archive_records;
    archive->format.destroy = xx_freearc_vtable_destroy;
}

xx_freearc *xx_freearc_create(xx_io_device *device, int64_t base_address) {
    xx_freearc *archive = (xx_freearc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_freearc_init(archive, device, base_address);
    return archive;
}

void xx_freearc_destroy(xx_freearc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->flags = 0U;
    archive->version = 0U;
}

void xx_freearc_free(xx_freearc *archive) {
    if (!archive) return;
    xx_freearc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_freearc_vtable_destroy(Abstractformat *self) {
    xx_freearc_destroy((xx_freearc *)self);
}

bool xx_freearc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_freearc_probe(self, NULL);
}

bool xx_freearc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_freearc *archive = (xx_freearc *)self;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_freearc_probe(self, archive);
    if (!self->is_valid) {
        self->format_size = 0;
        return false;
    }
    /* The block chain is not walked, so the archive is taken to run to the end
     * of the device. */
    self->format_size = xx_io_total_size(self->device) - self->base_address;
    return true;
}

int64_t xx_freearc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_freearc_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    /* The codec chain is not implemented, so no member is advertised. */
    (void)self;
    (void)pd;
    return 0U;
}

uint16_t xx_freearc_get_flags(const xx_freearc *archive) {
    return archive ? archive->flags : 0U;
}

uint16_t xx_freearc_get_version(const xx_freearc *archive) {
    return archive ? archive->version : 0U;
}
