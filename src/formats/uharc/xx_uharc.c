/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * UHARC (.uha), Uwe Herklotz 1997-2005.
 *
 *   0..2  "UHA"
 *   3     version byte: high nibble major, low nibble minor. UHARC 0.2 writes
 *         0x02, 0.4 writes 0x04, 0.6 writes 0x06. Both UHARC 0.4 and UnUHARC
 *         0.6b read this byte first and report "use UHARC version X.Y" for
 *         any other value, so it is the only plain field in the file.
 *   4..   entropy-coded stream: the archive header (with its own CRC, per
 *         UnUHARC's "CRC-failure (archive header)" message), the member
 *         directory and the member data. Only STORE-mode member bytes appear
 *         in the clear; names and sizes never do.
 *
 * The ALZ/PPM/LZP coders are closed source and undocumented, so there is
 * nothing to decode against. This reader identifies the container and
 * advertises zero records, the same honest answer xx_freearc and xx_bcm give.
 *
 * Validity beyond the four-byte signature:
 *   - version 0x01..0x06 (the released UHARC line; 0.1 is accepted because
 *     the tools name it, higher values have never been written),
 *   - at least XX_UHARC_MIN_SIZE bytes (the smallest archive UHARC 0.4 makes,
 *     one empty member, is 68 bytes),
 *   - the twelve bytes after the signature are not one repeated value: the
 *     coder's output never starts with such a run, padded text often does.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/uharc/xx_uharc.h"

#include "xxfclib/memory/xx_memory.h"

#include <stdio.h>

#ifdef UHARC
#define XX_UHARC_FILE_TYPE XX_FILE_TYPE_UHARC
#else
#define XX_UHARC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_UHARC_PROBE_SIZE 16U
#define XX_UHARC_MIN_SIZE 32
#define XX_UHARC_VERSION_MIN 0x01U
#define XX_UHARC_VERSION_MAX 0x06U

static void xx_uharc_vtable_destroy(Abstractformat *self);

static bool xx_uharc_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 || xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received = xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_uharc_probe(Abstractformat *self, uint8_t *version_out)
{
    uint8_t header[XX_UHARC_PROBE_SIZE];
    int64_t total;
    size_t index;
    bool varied = false;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address || total - self->base_address < XX_UHARC_MIN_SIZE) {
        return false;
    }
    if (!xx_uharc_read_at(self, self->base_address, header, sizeof(header))) {
        return false;
    }
    if (header[0] != 'U' || header[1] != 'H' || header[2] != 'A' || header[3] < XX_UHARC_VERSION_MIN || header[3] > XX_UHARC_VERSION_MAX) {
        return false;
    }
    for (index = 5U; index < sizeof(header); ++index) {
        if (header[index] != header[4]) {
            varied = true;
            break;
        }
    }
    if (!varied) {
        return false;
    }
    if (version_out) {
        *version_out = header[3];
    }
    return true;
}

void xx_uharc_init(xx_uharc *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_UHARC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-uharc");
    xx_format_set_extension(&archive->format, "uha");
    archive->format.check_is_valid = xx_uharc_check_is_valid;
    archive->format.handle_base_info = xx_uharc_handle_base_info;
    archive->format.get_format_size = xx_uharc_get_format_size;
    archive->format.get_number_of_archive_records = xx_uharc_get_number_of_archive_records;
    archive->format.destroy = xx_uharc_vtable_destroy;
}

xx_uharc *xx_uharc_create(xx_io_device *device, int64_t base_address)
{
    xx_uharc *archive = (xx_uharc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_uharc_init(archive, device, base_address);
    return archive;
}

void xx_uharc_destroy(xx_uharc *archive)
{
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->version = 0U;
}

void xx_uharc_free(xx_uharc *archive)
{
    if (!archive) return;
    xx_uharc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_uharc_vtable_destroy(Abstractformat *self)
{
    xx_uharc_destroy((xx_uharc *)self);
}

bool xx_uharc_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_uharc_probe(self, NULL);
}

bool xx_uharc_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_uharc *archive = (xx_uharc *)self;
    uint8_t version = 0U;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_uharc_probe(self, &version);
    if (!self->is_valid) {
        self->format_size = 0;
        archive->version = 0U;
        return false;
    }
    archive->version = version;
    /* The stream carries no plain length, so the archive is taken to run to
     * the end of the device. */
    self->format_size = xx_io_total_size(self->device) - self->base_address;
    return true;
}

int64_t xx_uharc_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_uharc_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    /* The coder is closed source; no member is advertised. */
    (void)self;
    (void)pd;
    return 0U;
}

uint8_t xx_uharc_get_version(const xx_uharc *archive)
{
    return archive ? archive->version : 0U;
}
