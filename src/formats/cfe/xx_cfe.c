/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Broadcom CFE bootloader images, recognised by the "CFE1CFE1" seal 28 bytes
 * into the image.  The rule follows binwalk's src/signatures/cfe.rs, the only
 * module that format has; the reasoning and the limitations are spelled out
 * in xx_cfe.h.
 *
 * This reader is DETECTION ONLY on purpose.  It publishes no archive records:
 * binwalk extracts nothing from a CFE image, and nothing in one says where
 * the loader ends and an NVRAM block or the next flash partition begins.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cfe/xx_cfe.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as CFE is registered there. */
#ifdef CFE
#define XX_CFE_FILE_TYPE XX_FILE_TYPE_CFE
#else
#define XX_CFE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct xx_cfe_parsed_s {
    int64_t input_size;
    int64_t seal_offset;
    int64_t image_size;
} xx_cfe_parsed;

static void xx_cfe_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: a CFE image is normally the first
 * partition of a flash dump, but a caller may hand in any base address, and
 * long is 32-bit on Win64. */
static bool xx_cfe_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;

    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* --------------------------------------------------------------- parse -- */

/*
 * One bounded read of eight bytes.  binwalk's parser checks nothing beyond
 * the seal and its position (the image must start 28 bytes before it), so
 * neither does this: the vectors in front of the seal are CPU instructions
 * whose encoding depends on the CPU and its byte order, and rejecting on
 * them would lose real images that binwalk accepts.
 */
static bool xx_cfe_parse(Abstractformat *self, xx_cfe_parsed *parsed,
                         xx_pd_struct *pd) {
    uint8_t seal[XX_CFE_MAGIC_SIZE];
    int64_t span;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->seal_offset = -1;
        parsed->image_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < 0 || parsed->input_size < self->base_address) {
        return false;
    }
    span = parsed->input_size - self->base_address;
    /* base_address <= input_size, so base_address + 28 cannot overflow once
     * span is known to cover the whole seal. */
    if (span < (int64_t)XX_CFE_MIN_SIZE) return false;
    if (!xx_cfe_read_at(self->device,
                        self->base_address + (int64_t)XX_CFE_MAGIC_OFFSET,
                        seal, sizeof(seal)) ||
        xx_rt_memcmp(seal, XX_CFE_MAGIC, XX_CFE_MAGIC_SIZE) != 0) {
        return false;
    }
    parsed->seal_offset = self->base_address + (int64_t)XX_CFE_MAGIC_OFFSET;
    parsed->image_size = span;
    return true;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_cfe_init(xx_cfe *cfe, xx_io_device *dev, int64_t base_address) {
    if (!cfe) return;
    xx_mem_zero(cfe, sizeof(*cfe));
    xx_format_init(&cfe->format, dev, base_address);
    /* Unknown: the seal is ASCII and says nothing about the CPU. */
    cfe->format.endian = XX_ENDIAN_UNKNOWN;
    cfe->format.file_type = XX_CFE_FILE_TYPE;
    /* A boot ROM image, not an archive: there is nothing to enumerate. */
    cfe->format.format_type = XX_TYPE_BOOT;
    cfe->format.is_archive = false;
    xx_format_set_mime_type(&cfe->format, "application/x-broadcom-cfe");
    xx_format_set_extension(&cfe->format, "bin");
    cfe->format.check_is_valid = xx_cfe_check_is_valid;
    cfe->format.handle_base_info = xx_cfe_handle_base_info;
    cfe->format.get_format_size = xx_cfe_get_format_size;
    cfe->format.destroy = xx_cfe_vtable_destroy;
    cfe->seal_offset = -1;
    cfe->image_size = -1;
}

xx_cfe *xx_cfe_create(xx_io_device *dev, int64_t base_address) {
    xx_cfe *cfe = (xx_cfe *)xx_mem_alloc(sizeof(*cfe));

    if (cfe) xx_cfe_init(cfe, dev, base_address);
    return cfe;
}

void xx_cfe_destroy(xx_cfe *cfe) {
    if (!cfe) return;
    xx_format_cleanup_extra_parameters(&cfe->format);
}

static void xx_cfe_vtable_destroy(Abstractformat *self) {
    xx_cfe_destroy((xx_cfe *)self);
}

void xx_cfe_free(xx_cfe *cfe) {
    if (!cfe) return;
    xx_cfe_destroy(cfe);
    xx_mem_free(cfe);
}

/* -------------------------------------------------------------- format -- */

bool xx_cfe_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_cfe_parsed parsed;

    return xx_cfe_parse(self, &parsed, pd);
}

bool xx_cfe_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_cfe *cfe = (xx_cfe *)self;
    xx_cfe_parsed parsed;

    if (!self || !cfe) return false;
    if (!xx_cfe_parse(self, &parsed, pd)) {
        cfe->seal_offset = -1;
        cfe->image_size = -1;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    cfe->seal_offset = parsed.seal_offset;
    cfe->image_size = parsed.image_size;
    /* binwalk reports size 0 and its scanner extends that to end of data,
     * so the image is the whole remaining device.  This is a statement about
     * what was handed to the reader, not a length recovered from the file -
     * which is also why there is no overlay. */
    self->format_size = parsed.image_size;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_cfe_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

int64_t xx_cfe_get_seal_offset(const xx_cfe *cfe) {
    return cfe ? cfe->seal_offset : -1;
}

int64_t xx_cfe_get_image_size(const xx_cfe *cfe) {
    return cfe ? cfe->image_size : -1;
}
