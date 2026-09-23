/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/linuxarm64/xx_linuxarm64.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_LINUX_ARM64 exists. */
#ifdef LINUX_ARM64
#define XX_LINUXARM64_FILE_TYPE XX_FILE_TYPE_LINUX_ARM64
#else
#define XX_LINUXARM64_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The PE/COFF signature the res5 word must point at.  binwalk compares only
 * the first two bytes ("PE"); the full four-byte signature is what every
 * EFI-stub kernel carries and what the PE/COFF specification defines, so the
 * reader is one step stricter than binwalk here. */
#define XX_LINUXARM64_PE_SIGNATURE "PE\0\0"
#define XX_LINUXARM64_PE_SIGNATURE_SIZE 4U

typedef struct xx_linuxarm64_parsed_s {
    uint32_t code0;
    uint32_t code1;
    uint64_t text_offset;
    uint64_t image_size;
    uint64_t flags;
    uint32_t pe_offset;
    bool has_mz;
} xx_linuxarm64_parsed;

static void xx_linuxarm64_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: long is 32-bit on Win64. */
static bool xx_linuxarm64_read_at(xx_io_device *device, int64_t offset,
                                  void *data, size_t size) {
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

/*
 * One bounded pass: two reads (the 64-byte header and the 4-byte PE
 * signature), no allocation, no loops over file-controlled counts.
 *
 * Checks, in binwalk's order (structures/linux.rs), plus the named
 * tightenings:
 *   - at least 64 bytes from base_address;
 *   - "ARM\x64" at 0x38 (the signature; binwalk's magic also covers the
 *     eight zero bytes of res4 at 0x30);
 *   - res2, res3, res4 (0x20..0x37) all zero;
 *   - flags bits 4..63 zero;
 *   - res5 >= 64 (tightening: the PE header cannot start inside this header)
 *     and "PE\0\0" (tightening: binwalk compares "PE") lies wholly inside the
 *     device at base_address + res5.
 */
static bool xx_linuxarm64_parse(Abstractformat *self,
                                xx_linuxarm64_parsed *parsed,
                                xx_pd_struct *pd) {
    uint8_t header[XX_LINUXARM64_HEADER_SIZE];
    uint8_t signature[XX_LINUXARM64_PE_SIGNATURE_SIZE];
    int64_t total_size;
    int64_t available;
    int64_t pe_at;
    size_t index;

    if (parsed) xx_mem_zero(parsed, sizeof(*parsed));
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    available = total_size - self->base_address;
    if (available < (int64_t)XX_LINUXARM64_HEADER_SIZE) return false;
    if (!xx_linuxarm64_read_at(self->device, self->base_address, header,
                               sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header + XX_LINUXARM64_MAGIC_OFFSET, XX_LINUXARM64_MAGIC,
                     XX_LINUXARM64_MAGIC_SIZE) != 0) {
        return false;
    }
    for (index = 0U; index < XX_LINUXARM64_RESERVED_SIZE; ++index) {
        if (header[XX_LINUXARM64_RESERVED_OFFSET + index] != 0U) return false;
    }
    parsed->code0 = xx_data_get_u32(header, sizeof(header), 0x00U, false);
    parsed->code1 = xx_data_get_u32(header, sizeof(header), 0x04U, false);
    parsed->text_offset = xx_data_get_u64(header, sizeof(header), 0x08U, false);
    parsed->image_size = xx_data_get_u64(header, sizeof(header), 0x10U, false);
    parsed->flags = xx_data_get_u64(header, sizeof(header), 0x18U, false);
    parsed->pe_offset = xx_data_get_u32(header, sizeof(header),
                                        XX_LINUXARM64_PE_OFFSET_FIELD, false);
    parsed->has_mz = header[0] == 'M' && header[1] == 'Z';
    if ((parsed->flags & XX_LINUXARM64_FLAGS_RESERVED) != 0U) return false;
    if (parsed->pe_offset < XX_LINUXARM64_HEADER_SIZE) return false;
    /* pe_offset is at most 2^32-1 and available is non-negative, so the
     * comparison below cannot overflow; the addition that follows is then
     * bounded by total_size. */
    if ((int64_t)parsed->pe_offset >
        available - (int64_t)XX_LINUXARM64_PE_SIGNATURE_SIZE) {
        return false;
    }
    pe_at = self->base_address + (int64_t)parsed->pe_offset;
    if (!xx_linuxarm64_read_at(self->device, pe_at, signature,
                               sizeof(signature)) ||
        xx_rt_memcmp(signature, XX_LINUXARM64_PE_SIGNATURE,
                     XX_LINUXARM64_PE_SIGNATURE_SIZE) != 0) {
        return false;
    }
    return !(pd && xx_pd_is_stopped(pd));
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_linuxarm64_init(xx_linuxarm64 *image, xx_io_device *dev,
                        int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, dev, base_address);
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_LINUXARM64_FILE_TYPE;
    image->format.os = XX_OS_LINUX;
    image->format.arch = XX_ARCH_ARM64;
    image->format.format_type = XX_TYPE_BOOT;
    image->format.is_executable = true;
    image->format.is_archive = false;
    xx_format_set_mime_type(&image->format, "application/x-linux-arm64-image");
    xx_format_set_extension(&image->format, "img");
    image->format.check_is_valid = xx_linuxarm64_check_is_valid;
    image->format.handle_base_info = xx_linuxarm64_handle_base_info;
    image->format.get_format_size = xx_linuxarm64_get_format_size;
    image->format.destroy = xx_linuxarm64_vtable_destroy;
}

xx_linuxarm64 *xx_linuxarm64_create(xx_io_device *dev, int64_t base_address) {
    xx_linuxarm64 *image = (xx_linuxarm64 *)xx_mem_alloc(sizeof(*image));
    if (image) xx_linuxarm64_init(image, dev, base_address);
    return image;
}

void xx_linuxarm64_destroy(xx_linuxarm64 *image) {
    if (!image) return;
    xx_format_cleanup_extra_parameters(&image->format);
}

static void xx_linuxarm64_vtable_destroy(Abstractformat *self) {
    xx_linuxarm64_destroy((xx_linuxarm64 *)self);
}

void xx_linuxarm64_free(xx_linuxarm64 *image) {
    if (!image) return;
    xx_linuxarm64_destroy(image);
    xx_mem_free(image);
}

bool xx_linuxarm64_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_linuxarm64_parsed parsed;
    return xx_linuxarm64_parse(self, &parsed, pd);
}

bool xx_linuxarm64_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_linuxarm64_parsed parsed;
    xx_linuxarm64 *image = (xx_linuxarm64 *)self;
    int64_t total_size;
    int64_t header_end;
    uint32_t page_code;

    if (!self) return false;
    if (!xx_linuxarm64_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    image->code0 = parsed.code0;
    image->code1 = parsed.code1;
    image->text_offset = parsed.text_offset;
    image->image_size = parsed.image_size;
    image->flags = parsed.flags;
    image->pe_offset = parsed.pe_offset;
    image->has_mz = parsed.has_mz;
    image->kernel_big_endian = (parsed.flags & XX_LINUXARM64_FLAG_BE) != 0U;
    image->phys_placement_anywhere =
        (parsed.flags & XX_LINUXARM64_FLAG_PHYS_ANY) != 0U;
    page_code = (uint32_t)((parsed.flags >> 1U) & 3U);
    image->page_size = page_code == 1U   ? 4096U
                       : page_code == 2U ? 16384U
                       : page_code == 3U ? 65536U
                                         : 0U;
    self->endian =
        image->kernel_big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;

    /* binwalk's carve length: the 64-byte header (result.size ==
     * common::size(boot_img_structure)).  Everything after it -- the PE
     * header, the kernel text and data -- is reported as overlay. */
    self->format_size = (int64_t)XX_LINUXARM64_HEADER_SIZE;
    header_end = self->base_address + (int64_t)XX_LINUXARM64_HEADER_SIZE;
    total_size = xx_io_total_size(self->device);
    if (total_size > header_end) {
        self->overlay_offset = header_end;
        self->overlay_size = total_size - header_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_linuxarm64_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_linuxarm64_get_text_offset(const xx_linuxarm64 *image) {
    return image ? image->text_offset : 0U;
}
uint64_t xx_linuxarm64_get_image_size(const xx_linuxarm64 *image) {
    return image ? image->image_size : 0U;
}
uint64_t xx_linuxarm64_get_flags(const xx_linuxarm64 *image) {
    return image ? image->flags : 0U;
}
uint32_t xx_linuxarm64_get_pe_offset(const xx_linuxarm64 *image) {
    return image ? image->pe_offset : 0U;
}
uint32_t xx_linuxarm64_get_page_size(const xx_linuxarm64 *image) {
    return image ? image->page_size : 0U;
}
bool xx_linuxarm64_is_kernel_big_endian(const xx_linuxarm64 *image) {
    return image ? image->kernel_big_endian : false;
}
