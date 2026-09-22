/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BCM is a context-mixing compressor: after the four-byte magic the file is a
 * single arithmetic-coded bitstream with no member framing, no stored name and
 * no stored original size. Nothing about the content can be reported without
 * running the model, so this reader identifies and sizes the stream and stops
 * there -- the same line XArchive draws, where XBCM is an XExternalArchive
 * that delegates the payload to a separate backend.
 *
 * Reporting zero archive records is deliberate. A reader that advertised one
 * member and then failed every extraction would be worse than one that is
 * honest about what it can do: the corpus runner, the format contract test and
 * any caller iterating records all treat "no records" as a defined answer.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bcm/xx_bcm.h"

#include "xxfclib/memory/xx_memory.h"

#include <stdio.h>

/* "BCM" plus one version digit. XArchive matches the literal "BCM1"; the digit
 * is kept separate here so a later revision identifies rather than falls
 * through to Binary, and it is reported through xx_bcm_get_version. */
#define XX_BCM_MAGIC_SIZE 4U
#define XX_BCM_MIN_SIZE 8

static void xx_bcm_vtable_destroy(Abstractformat *self);

static bool xx_bcm_read_magic(Abstractformat *self, uint8_t magic[4]) {
    size_t completed = 0U;

    if (!self || !self->device || self->base_address < 0 ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        return false;
    }
    while (completed < XX_BCM_MAGIC_SIZE) {
        ssize_t received = xx_io_read(self->device, magic + completed,
                                      XX_BCM_MAGIC_SIZE - completed);
        if (received <= 0 ||
            (size_t)received > XX_BCM_MAGIC_SIZE - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

/*
 * A stream is BCM when it carries the magic and has at least one byte of
 * payload beyond it. The eight-byte floor matches XArchive: four bytes of
 * magic cannot be a compressed stream on their own, and accepting them would
 * classify any file that merely starts with those letters.
 */
static bool xx_bcm_probe(Abstractformat *self, uint8_t *version_out) {
    uint8_t magic[XX_BCM_MAGIC_SIZE];
    int64_t total;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address ||
        total - self->base_address < XX_BCM_MIN_SIZE) {
        return false;
    }
    if (!xx_bcm_read_magic(self, magic)) {
        return false;
    }
    if (magic[0] != 'B' || magic[1] != 'C' || magic[2] != 'M' ||
        magic[3] < '1' || magic[3] > '9') {
        return false;
    }
    if (version_out) {
        *version_out = (uint8_t)(magic[3] - '0');
    }
    return true;
}

void xx_bcm_init(xx_bcm *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_UNKNOWN;
    archive->format.file_type = XX_FILE_TYPE_BCM;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-bcm");
    xx_format_set_extension(&archive->format, "bcm");
    archive->format.check_is_valid = xx_bcm_check_is_valid;
    archive->format.handle_base_info = xx_bcm_handle_base_info;
    archive->format.get_format_size = xx_bcm_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bcm_get_number_of_archive_records;
    archive->format.destroy = xx_bcm_vtable_destroy;
}

xx_bcm *xx_bcm_create(xx_io_device *device, int64_t base_address) {
    xx_bcm *archive = (xx_bcm *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_bcm_init(archive, device, base_address);
    return archive;
}

void xx_bcm_destroy(xx_bcm *archive) {
    if (!archive) return;
    /* NOT xx_format_destroy: that dispatches through format.destroy, which is
     * this function, and the pair recurses until the stack is gone. The base
     * teardown is the close hook plus the extra-parameter list. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->version = 0U;
}

void xx_bcm_free(xx_bcm *archive) {
    if (!archive) return;
    xx_bcm_destroy(archive);
    xx_mem_free(archive);
}

static void xx_bcm_vtable_destroy(Abstractformat *self) {
    xx_bcm_destroy((xx_bcm *)self);
}

bool xx_bcm_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_bcm_probe(self, NULL);
}

bool xx_bcm_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_bcm *archive = (xx_bcm *)self;
    uint8_t version = 0U;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_bcm_probe(self, &version);
    if (!self->is_valid) {
        self->format_size = 0;
        return false;
    }
    archive->version = version;
    /* The stream runs to the end of the device: the bitstream carries no
     * length, so there is no way to distinguish payload from trailing data. */
    total = xx_io_total_size(self->device);
    self->format_size = total - self->base_address;
    return true;
}

int64_t xx_bcm_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_bcm_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    /* See the header: the payload cannot be produced here, so no member is
     * advertised. */
    (void)self;
    (void)pd;
    return 0U;
}

uint8_t xx_bcm_get_version(const xx_bcm *archive) {
    return archive ? archive->version : 0U;
}
