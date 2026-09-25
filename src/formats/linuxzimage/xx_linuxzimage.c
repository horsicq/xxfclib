/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/linuxzimage/xx_linuxzimage.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_LINUX_ZIMAGE exists. */
#ifdef LINUX_ZIMAGE
#define XX_LINUXZIMAGE_FILE_TYPE XX_FILE_TYPE_LINUX_ZIMAGE
#else
#define XX_LINUXZIMAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct xx_linuxzimage_parsed_s {
    uint32_t start_address;
    uint32_t end_address;
    uint32_t branch;
    uint32_t table_offset;
    int64_t image_size;
    bool code_big_endian;
    bool header_big_endian;
    bool has_endian_flag;
    bool has_table_magic;
    bool kernel_big_endian;
} xx_linuxzimage_parsed;

static void xx_linuxzimage_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: long is 32-bit on Win64. */
static bool xx_linuxzimage_read_at(xx_io_device *device, int64_t offset,
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
 * One bounded pass: a single read of at most 60 bytes, no allocation and no
 * loop over a file-controlled count.
 *
 * binwalk's checks (signatures/linux.rs, structures/linux.rs):
 *   - 0x016F2818 at 0x24, stored little- or big-endian;
 *   - the eight words at 0x00, read little-endian, are identical and equal
 *     0xE1A00000 (little) or 0x0000A0E1 (big).
 * Tightenings (binwalk leaves the size at 0 and carves to EOF):
 *   - start and end, read in the byte order in which the magic matched,
 *     satisfy start < end;
 *   - end - start >= 0x30, so the image holds its own header;
 *   - end - start <= the bytes available from base_address.
 * The format size is end - start.
 */
static bool xx_linuxzimage_parse(Abstractformat *self,
                                 xx_linuxzimage_parsed *parsed,
                                 xx_pd_struct *pd) {
    uint8_t header[XX_LINUXZIMAGE_EXTENDED_HEADER_SIZE];
    int64_t total_size;
    int64_t available;
    size_t have;
    size_t index;
    uint32_t nop;
    uint32_t size;
    bool header_be;

    if (parsed) xx_mem_zero(parsed, sizeof(*parsed));
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < 0 || total_size < self->base_address) return false;
    available = total_size - self->base_address;
    if (available < (int64_t)XX_LINUXZIMAGE_HEADER_SIZE) return false;
    have = available >= (int64_t)sizeof(header) ? sizeof(header)
                                                : (size_t)available;
    xx_mem_zero(header, sizeof(header));
    if (!xx_linuxzimage_read_at(self->device, self->base_address, header,
                                have)) {
        return false;
    }

    nop = xx_data_get_u32(header, have, 0U, false);
    if (nop != XX_LINUXZIMAGE_NOP_LE && nop != XX_LINUXZIMAGE_NOP_BE) {
        return false;
    }
    for (index = 1U; index < XX_LINUXZIMAGE_NOP_COUNT; ++index) {
        if (xx_data_get_u32(header, have, index * 4U, false) != nop) {
            return false;
        }
    }
    if (xx_data_get_u32(header, have, XX_LINUXZIMAGE_MAGIC_OFFSET, false) ==
        XX_LINUXZIMAGE_MAGIC) {
        header_be = false;
    } else if (xx_data_get_u32(header, have, XX_LINUXZIMAGE_MAGIC_OFFSET,
                               true) == XX_LINUXZIMAGE_MAGIC) {
        header_be = true;
    } else {
        return false;
    }

    parsed->code_big_endian = (nop == XX_LINUXZIMAGE_NOP_BE);
    parsed->header_big_endian = header_be;
    parsed->start_address = xx_data_get_u32(
        header, have, XX_LINUXZIMAGE_START_OFFSET, header_be);
    parsed->end_address = xx_data_get_u32(
        header, have, XX_LINUXZIMAGE_END_OFFSET, header_be);
    parsed->branch = xx_data_get_u32(header, have,
                                     XX_LINUXZIMAGE_BRANCH_OFFSET,
                                     parsed->code_big_endian);
    if (parsed->end_address <= parsed->start_address) return false;
    /* Both are u32 and end > start, so the difference cannot wrap. */
    size = parsed->end_address - parsed->start_address;
    if (size < XX_LINUXZIMAGE_HEADER_SIZE) return false;
    if ((int64_t)size > available) return false;
    parsed->image_size = (int64_t)size;

    /* Optional v3.x+ words, looked at only where they lie inside both the
     * bytes read and the image itself. */
    if (have >= XX_LINUXZIMAGE_ENDIAN_FLAG_OFFSET + 4U &&
        size >= XX_LINUXZIMAGE_ENDIAN_FLAG_OFFSET + 4U) {
        if (xx_data_get_u32(header, have, XX_LINUXZIMAGE_ENDIAN_FLAG_OFFSET,
                            false) == XX_LINUXZIMAGE_ENDIAN_FLAG) {
            parsed->has_endian_flag = true;
            parsed->kernel_big_endian = false;
        } else if (xx_data_get_u32(header, have,
                                   XX_LINUXZIMAGE_ENDIAN_FLAG_OFFSET, true) ==
                   XX_LINUXZIMAGE_ENDIAN_FLAG) {
            parsed->has_endian_flag = true;
            parsed->kernel_big_endian = true;
        }
    }
    if (!parsed->has_endian_flag) parsed->kernel_big_endian = header_be;
    if (have >= XX_LINUXZIMAGE_EXTENDED_HEADER_SIZE &&
        size >= XX_LINUXZIMAGE_EXTENDED_HEADER_SIZE &&
        xx_data_get_u32(header, have, XX_LINUXZIMAGE_TABLE_MAGIC_OFFSET,
                        false) == XX_LINUXZIMAGE_TABLE_MAGIC) {
        parsed->has_table_magic = true;
        parsed->table_offset = xx_data_get_u32(
            header, have, XX_LINUXZIMAGE_TABLE_OFFSET_FIELD, header_be);
    }
    return !(pd && xx_pd_is_stopped(pd));
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_linuxzimage_init(xx_linuxzimage *image, xx_io_device *dev,
                         int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, dev, base_address);
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_LINUXZIMAGE_FILE_TYPE;
    image->format.os = XX_OS_LINUX;
    image->format.arch = XX_ARCH_ARM;
    image->format.format_type = XX_TYPE_BOOT;
    image->format.is_executable = true;
    image->format.is_archive = false;
    xx_format_set_mime_type(&image->format, "application/x-linux-arm-zimage");
    xx_format_set_extension(&image->format, "bin");
    image->format.check_is_valid = xx_linuxzimage_check_is_valid;
    image->format.handle_base_info = xx_linuxzimage_handle_base_info;
    image->format.get_format_size = xx_linuxzimage_get_format_size;
    image->format.destroy = xx_linuxzimage_vtable_destroy;
}

xx_linuxzimage *xx_linuxzimage_create(xx_io_device *dev, int64_t base_address) {
    xx_linuxzimage *image = (xx_linuxzimage *)xx_mem_alloc(sizeof(*image));
    if (image) xx_linuxzimage_init(image, dev, base_address);
    return image;
}

void xx_linuxzimage_destroy(xx_linuxzimage *image) {
    if (!image) return;
    xx_format_cleanup_extra_parameters(&image->format);
}

static void xx_linuxzimage_vtable_destroy(Abstractformat *self) {
    xx_linuxzimage_destroy((xx_linuxzimage *)self);
}

void xx_linuxzimage_free(xx_linuxzimage *image) {
    if (!image) return;
    xx_linuxzimage_destroy(image);
    xx_mem_free(image);
}

bool xx_linuxzimage_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_linuxzimage_parsed parsed;
    return xx_linuxzimage_parse(self, &parsed, pd);
}

bool xx_linuxzimage_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_linuxzimage_parsed parsed;
    xx_linuxzimage *image = (xx_linuxzimage *)self;
    int64_t total_size;
    int64_t image_end;

    if (!self) return false;
    if (!xx_linuxzimage_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    image->start_address = parsed.start_address;
    image->end_address = parsed.end_address;
    image->branch = parsed.branch;
    image->table_offset = parsed.table_offset;
    image->code_big_endian = parsed.code_big_endian;
    image->header_big_endian = parsed.header_big_endian;
    image->has_endian_flag = parsed.has_endian_flag;
    image->has_table_magic = parsed.has_table_magic;
    image->kernel_big_endian = parsed.kernel_big_endian;
    self->endian =
        parsed.kernel_big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;

    /* The zImage runs from start to _edata; an appended DTB or padding
     * after it is overlay.  parse() bounded image_size by the device. */
    self->format_size = parsed.image_size;
    image_end = self->base_address + parsed.image_size;
    total_size = xx_io_total_size(self->device);
    if (total_size > image_end) {
        self->overlay_offset = image_end;
        self->overlay_size = total_size - image_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_linuxzimage_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint32_t xx_linuxzimage_get_start_address(const xx_linuxzimage *image) {
    return image ? image->start_address : 0U;
}
uint32_t xx_linuxzimage_get_end_address(const xx_linuxzimage *image) {
    return image ? image->end_address : 0U;
}
uint32_t xx_linuxzimage_get_table_offset(const xx_linuxzimage *image) {
    return image ? image->table_offset : 0U;
}
bool xx_linuxzimage_is_code_big_endian(const xx_linuxzimage *image) {
    return image ? image->code_big_endian : false;
}
bool xx_linuxzimage_is_header_big_endian(const xx_linuxzimage *image) {
    return image ? image->header_big_endian : false;
}
bool xx_linuxzimage_is_kernel_big_endian(const xx_linuxzimage *image) {
    return image ? image->kernel_big_endian : false;
}
bool xx_linuxzimage_has_endian_flag(const xx_linuxzimage *image) {
    return image ? image->has_endian_flag : false;
}
