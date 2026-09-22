/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PEA layout, as far as identification needs it:
 *
 *   archive header, 10 bytes
 *     0     0xEA
 *     1     0x01
 *     2     version, 0..6
 *     3     object control (non-stream set)
 *     8     bit 0x80 selects big-endian for the size fields that follow
 *
 *   10..11  uint16 name size, in the endianness byte 8 selected
 *   12      "POD\0" stream trigger, when the name size is zero
 *     +4    compression, 0..3
 *     +6    stream control (stream set)
 *     +7    object control (non-stream set)
 *
 * The control bytes are what make this identifiable. Two magic bytes would
 * not be: 0xEA 0x01 turns up readily in binary data. The reference's four
 * additional constraints -- version ceiling, two control-byte alphabets, and
 * a "POD\0" trigger found at a computed rather than fixed offset -- are what
 * separate a PEA archive from a coincidence, so all of them are applied here.
 *
 * The payload codecs are not implemented, so no archive record is advertised.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pea/xx_pea.h"

#include "xxfclib/memory/xx_memory.h"

#include <stdio.h>

#define XX_PEA_ARCHIVE_HEADER_SIZE 10
#define XX_PEA_STREAM_FIXED_SIZE 10
#define XX_PEA_MAX_VERSION 6U
#define XX_PEA_MAX_COMPRESSION 3U

static void xx_pea_vtable_destroy(Abstractformat *self);

/*
 * The control alphabet used by object headers and by the archive header's
 * byte 3: 0x00..0x03 and 0x10..0x19.
 */
static bool xx_pea_is_non_stream_control(uint8_t value) {
    return value <= 3U || (value >= 0x10U && value <= 0x19U);
}

/*
 * Stream headers accept everything an object header does, plus 0x30..0x33
 * and 0x41..0x4C.
 */
static bool xx_pea_is_stream_control(uint8_t value) {
    return xx_pea_is_non_stream_control(value) ||
           (value >= 0x30U && value <= 0x33U) ||
           (value >= 0x41U && value <= 0x4CU);
}

static bool xx_pea_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_pea_probe(Abstractformat *self, xx_pea *out) {
    uint8_t header[XX_PEA_ARCHIVE_HEADER_SIZE];
    uint8_t name_size_raw[2];
    uint8_t stream[8];
    bool big_endian;
    uint16_t name_size;
    int64_t trigger;
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < XX_PEA_ARCHIVE_HEADER_SIZE + XX_PEA_STREAM_FIXED_SIZE) {
        return false;
    }

    if (!xx_pea_read_at(self, self->base_address, header, sizeof(header))) {
        return false;
    }
    if (header[0] != 0xEAU || header[1] != 0x01U ||
        header[2] > XX_PEA_MAX_VERSION ||
        !xx_pea_is_non_stream_control(header[3])) {
        return false;
    }

    /* Byte 8's top bit chooses the byte order of every size field that
     * follows, including the name size read next. */
    big_endian = (header[8] & 0x80U) != 0U;

    if (!xx_pea_read_at(self, self->base_address + XX_PEA_ARCHIVE_HEADER_SIZE,
                        name_size_raw, sizeof(name_size_raw))) {
        return false;
    }
    name_size = big_endian
                    ? (uint16_t)(((uint16_t)name_size_raw[0] << 8) |
                                 name_size_raw[1])
                    : (uint16_t)(((uint16_t)name_size_raw[1] << 8) |
                                 name_size_raw[0]);
    /* The reference only resolves the trigger for an unnamed first object; a
     * non-zero name size means the offset is not computable this way, which
     * it reports as "no trigger" rather than guessing. */
    if (name_size != 0U) {
        return false;
    }
    trigger = XX_PEA_ARCHIVE_HEADER_SIZE + 2;
    if (trigger < XX_PEA_ARCHIVE_HEADER_SIZE + 2 || trigger > span - 8) {
        return false;
    }

    if (!xx_pea_read_at(self, self->base_address + trigger, stream,
                        sizeof(stream))) {
        return false;
    }
    if (stream[0] != 'P' || stream[1] != 'O' || stream[2] != 'D' ||
        stream[3] != 0x00U || stream[4] > XX_PEA_MAX_COMPRESSION ||
        !xx_pea_is_stream_control(stream[6]) ||
        !xx_pea_is_non_stream_control(stream[7])) {
        return false;
    }

    if (out) {
        out->version = header[2];
        out->object_control = header[3];
        out->compression = stream[4];
        out->stream_control = stream[6];
        out->big_endian = big_endian;
        out->first_stream_offset = trigger;
    }
    return true;
}

void xx_pea_init(xx_pea *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    /* Byte 8 of the header decides this per archive; the default stands until
     * handle_base_info reads it. */
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PEA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    archive->first_stream_offset = -1;
    xx_format_set_mime_type(&archive->format, "application/x-pea");
    xx_format_set_extension(&archive->format, "pea");
    archive->format.check_is_valid = xx_pea_check_is_valid;
    archive->format.handle_base_info = xx_pea_handle_base_info;
    archive->format.get_format_size = xx_pea_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pea_get_number_of_archive_records;
    archive->format.destroy = xx_pea_vtable_destroy;
}

xx_pea *xx_pea_create(xx_io_device *device, int64_t base_address) {
    xx_pea *archive = (xx_pea *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_pea_init(archive, device, base_address);
    return archive;
}

void xx_pea_destroy(xx_pea *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it calls back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->first_stream_offset = -1;
}

void xx_pea_free(xx_pea *archive) {
    if (!archive) return;
    xx_pea_destroy(archive);
    xx_mem_free(archive);
}

static void xx_pea_vtable_destroy(Abstractformat *self) {
    xx_pea_destroy((xx_pea *)self);
}

bool xx_pea_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_pea_probe(self, NULL);
}

bool xx_pea_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pea *archive = (xx_pea *)self;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_pea_probe(self, archive);
    if (!self->is_valid) {
        self->format_size = 0;
        return false;
    }
    self->endian = archive->big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    /* The object chain is not walked, so the archive is taken to run to the
     * end of the device rather than reporting a size it has not verified. */
    self->format_size = xx_io_total_size(self->device) - self->base_address;
    return true;
}

int64_t xx_pea_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pea_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    /* The stream codecs are not implemented, so no member is advertised. */
    (void)self;
    (void)pd;
    return 0U;
}

uint8_t xx_pea_get_version(const xx_pea *archive) {
    return archive ? archive->version : 0U;
}

uint8_t xx_pea_get_compression(const xx_pea *archive) {
    return archive ? archive->compression : 0U;
}

bool xx_pea_is_big_endian(const xx_pea *archive) {
    return archive ? archive->big_endian : false;
}
