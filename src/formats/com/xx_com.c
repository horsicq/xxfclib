/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxfclib/formats/com/xx_com.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/data/xx_data.h"

/* Signatures of the MZ-family executables that share DOS with .COM.  They are
 * only used as a NEGATIVE test: a file that announces itself as one of these
 * is by definition not the signature-less format. */
#define XX_COM_MZ_SIGNATURE 0x5A4D /* 'MZ' -- MSDOS / NE / LE / LX / PE */
#define XX_COM_ZM_SIGNATURE 0x4D5A /* 'ZM' -- alternate DOS stub spelling */

/* Forward declaration of vtable callbacks */
static void xx_com_vtable_destroy(Abstractformat *self);

/**
 * Compute the file/image split.  A COM file has no header, so this is pure
 * arithmetic on the device size: everything up to XX_COM_MAX_FILE_SIZE is
 * code mapped at XX_COM_ADDRESS_BEGIN, the rest (which a valid COM never has,
 * see xx_com_check_is_valid) is overlay, and the remainder of the 64 KiB
 * segment is uninitialised virtual space.
 */
static bool xx_com_get_layout(const Abstractformat *format,
                              int64_t *binary_size,
                              int64_t *code_size,
                              int64_t *overlay_size) {
    int64_t total_size;
    int64_t available;
    int64_t code;

    if (!format || !format->device || format->base_address < 0) {
        return false;
    }
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address) {
        return false;
    }
    available = total_size - format->base_address;

    code = available;
    if (code > (int64_t)XX_COM_MAX_FILE_SIZE) {
        code = (int64_t)XX_COM_MAX_FILE_SIZE;
    }

    if (binary_size) *binary_size = available;
    if (code_size) *code_size = code;
    if (overlay_size) *overlay_size = available - code;
    return true;
}

void xx_com_init(xx_com *com, xx_io_device *dev, int64_t base_address) {
    if (!com) {
        return;
    }
    xx_mem_zero(com, sizeof(xx_com));

    /* Initialize base Abstractformat */
    xx_format_init(&com->format, dev, base_address);

    /* Setup default COM format attributes */
    com->format.endian = XX_ENDIAN_LITTLE;
    com->format.file_type = XX_FILE_TYPE_COM;
    com->format.os = XX_OS_DOS;
    com->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    com->format.arch = XX_ARCH_X86_16;
    com->format.is_executable = true;
    com->format.is_archive = false;
    xx_format_set_mime_type(&com->format, "application/x-dosexec");
    xx_format_set_extension(&com->format, ".com");

    /* Setup vtable callbacks */
    com->format.check_is_valid = xx_com_check_is_valid;
    com->format.handle_base_info = xx_com_handle_base_info;
    com->format.get_format_size = xx_com_get_format_size;
    com->format.get_memory_map = xx_com_get_memory_map;
    com->format.destroy = xx_com_vtable_destroy;

    /* Initialize fields.  None of these are parsed: the format has no
     * header, so address_begin and image_size are constants and the rest is
     * derived from the device size in handle_base_info. */
    com->address_begin = XX_COM_ADDRESS_BEGIN;
    com->image_size = XX_COM_IMAGE_SIZE;
    com->code_size = 0;
    com->entry_bytes = 0;
}

xx_com *xx_com_create(xx_io_device *dev, int64_t base_address) {
    xx_com *com = (xx_com *)xx_mem_alloc(sizeof(xx_com));
    if (!com) {
        return NULL;
    }
    xx_com_init(com, dev, base_address);
    return com;
}

void xx_com_destroy(xx_com *com) {
    if (!com) {
        return;
    }
    if (com->format.close) {
        com->format.close(&com->format);
    }
    xx_format_cleanup_extra_parameters(&com->format);
}

static void xx_com_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_com *com = (xx_com *)self;
        xx_com_destroy(com);
    }
}

void xx_com_free(xx_com *com) {
    if (!com) {
        return;
    }
    xx_com_destroy(com);
    xx_mem_free(com);
}

/**
 * THIS IS NOT A SIGNATURE CHECK AND CANNOT BE MADE INTO ONE.
 *
 * The reference implementation (Formats/exec/xcom.cpp) is
 *
 *     bool XCOM::isValid(PDSTRUCT *) {
 *         return getSize() <= (XCOM_DEF::IMAGESIZE - XCOM_DEF::ADDRESS_BEGIN);
 *     }
 *
 * i.e. "the file fits in one 64 KiB segment behind the PSP" -- true of every
 * file on disk smaller than 65280 bytes, of any format whatsoever.  The
 * reference never calls this in isolation.  XBinary::getFileTypes gates it
 * with two things this function has no access to:
 *
 *   1. `(stResult.count() <= 1) || stResult.contains(FT_PLAINTEXT)` -- run
 *      only after every self-identifying format has failed to claim the file;
 *   2. `getDeviceFileSuffix(getDevice()).toUpper() == "COM"` -- the actual
 *      discriminator is the FILE NAME, not the content.
 *
 * xx_io_device carries no file name, so condition 2 is unavailable here and
 * condition 1 is a property of dispatch order, not of this function.  What is
 * left below is the size bound plus the one negative content test that is
 * genuinely load-bearing: an MZ/ZM header means MSDOS/NE/LE/LX/PE, never COM.
 * That is far from sufficient.  A 4 KiB PNG, a text file, or a fragment of
 * anything at all passes.  Treat a true result as "not ruled out", and see
 * the dispatch note at the end of this file.
 */
bool xx_com_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    int64_t total_size;
    int64_t available;
    uint16_t lead;

    if (!self || !self->device || self->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }

    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) {
        return false;
    }
    available = total_size - self->base_address;

    /* The reference accepts size 0 (getSize() <= 0xFF00 is true for an empty
     * device).  A zero-byte image has no entry point to jump to, so require
     * at least one byte of code.  Deliberate, named deviation. */
    if (available < 1) {
        return false;
    }
    if (available > (int64_t)XX_COM_MAX_FILE_SIZE) {
        return false;
    }

    /* Negative test: the MZ family identifies itself and owns those files. */
    if (available >= 2) {
        lead = xx_io_get_u16(self->device, self->base_address, false);
        if (lead == XX_COM_MZ_SIGNATURE || lead == XX_COM_ZM_SIGNATURE) {
            return false;
        }
    }

    return true;
}

bool xx_com_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    int64_t binary_size;
    int64_t code_size;
    int64_t overlay_size;
    xx_com *com;

    if (!self || !self->device || self->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }

    if (!xx_com_get_layout(self, &binary_size, &code_size, &overlay_size)) {
        self->is_valid = false;
        return false;
    }
    if (code_size < 1) {
        self->is_valid = false;
        return false;
    }

    com = (xx_com *)self;
    com->address_begin = XX_COM_ADDRESS_BEGIN;
    com->image_size = XX_COM_IMAGE_SIZE;
    com->code_size = code_size;

    /* The only "field" the reference exposes for this format is the pair of
     * bytes sitting at the entry point (XCOM::getXFRecords -> "EntryBytes"). */
    com->entry_bytes = 0;
    if (code_size >= 2) {
        com->entry_bytes = xx_io_get_u16(self->device, self->base_address, false);
    }

    self->format_type = XX_TYPE_CONSOLE_APPLICATION;
    self->format_size = binary_size;
    if (overlay_size > 0) {
        self->overlay_offset = self->base_address + code_size;
        self->overlay_size = overlay_size;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }

    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_com_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_com_handle_base_info(self, pd))
               ? self->format_size
               : -1;
}

bool xx_com_get_memory_map(Abstractformat *self,
                           xx_memory_map_mode_t mode,
                           xx_memory_map *output,
                           xx_pd_struct *pd) {
    xx_com *com;
    int64_t binary_size;
    int64_t code_size;
    int64_t overlay_size;
    int64_t tail_size;
    uint64_t module_address;
    uint64_t code_address;
    uint64_t tail_address;

    if (!self || !output || !self->device || !self->base_info_handled ||
        self->base_address < 0 || xx_pd_is_stopped(pd)) {
        return false;
    }
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) {
        mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    }
    if (mode != XX_MEMORY_MAP_MODE_SEGMENTS) {
        return false;
    }

    com = (xx_com *)self;
    if (!xx_com_get_layout(self, &binary_size, &code_size, &overlay_size)) {
        return false;
    }

    module_address = self->module_address != XX_INVALID_ADDRESS
                         ? self->module_address
                         : 0U;
    /* module_address is the segment base; the image itself starts one PSP
     * later.  Bound the arithmetic so a caller-supplied module address near
     * the top of the address space cannot wrap. */
    if ((uint64_t)XX_COM_IMAGE_SIZE > XX_INVALID_ADDRESS - module_address) {
        return false;
    }
    code_address = module_address + (uint64_t)XX_COM_ADDRESS_BEGIN;
    tail_address = code_address + (uint64_t)code_size;
    tail_size = (int64_t)XX_COM_MAX_FILE_SIZE - code_size;

    output->binary_offset = self->base_address;
    output->module_address = module_address;
    output->is_image = self->is_mapped;
    output->binary_size = binary_size;
    output->entry_point_address = code_address;
    output->code_base = (int64_t)code_address;
    output->start_load_offset = self->base_address;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = mode;

    /* The 256-byte Program Segment Prefix is built by DOS at load time and is
     * not present in the file: virtual-only, no backing offset. */
    if (!xx_memory_map_add_part(output, -1, 0, module_address,
                                (int64_t)XX_COM_ADDRESS_BEGIN,
                                XX_FILE_PART_HEADER, 0,
                                "PSP", false)) {
        return false;
    }
    if (!xx_memory_map_add_part(output, self->base_address, code_size,
                                code_address, code_size,
                                XX_FILE_PART_SEGMENT, 1,
                                "COM image", false)) {
        return false;
    }
    /* Remainder of the single 64 KiB segment: allocated to the program by DOS
     * but not initialised from the file. */
    if (tail_size > 0 &&
        !xx_memory_map_add_part(output, -1, 0, tail_address, tail_size,
                                XX_FILE_PART_SEGMENT, 2,
                                "Segment tail", false)) {
        return false;
    }
    if (overlay_size > 0 &&
        !xx_memory_map_add_part(output, self->base_address + code_size,
                                overlay_size, XX_INVALID_ADDRESS, 0,
                                XX_FILE_PART_OVERLAY, 3,
                                "Overlay", false)) {
        return false;
    }

    (void)com;
    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

/* --- Getters --- */
uint32_t xx_com_get_address_begin(const xx_com *com) {
    return com ? com->address_begin : XX_COM_ADDRESS_BEGIN;
}

uint32_t xx_com_get_image_size(const xx_com *com) {
    return com ? com->image_size : XX_COM_IMAGE_SIZE;
}

int64_t xx_com_get_code_size(const xx_com *com) {
    return com ? com->code_size : 0;
}

uint16_t xx_com_get_entry_bytes(const xx_com *com) {
    return com ? com->entry_bytes : 0;
}

uint64_t xx_com_get_entry_point_address(const xx_com *com) {
    uint64_t module_address;

    if (!com) {
        return XX_INVALID_ADDRESS;
    }
    module_address = com->format.module_address != XX_INVALID_ADDRESS
                         ? com->format.module_address
                         : 0U;
    if ((uint64_t)XX_COM_ADDRESS_BEGIN > XX_INVALID_ADDRESS - module_address) {
        return XX_INVALID_ADDRESS;
    }
    return module_address + (uint64_t)XX_COM_ADDRESS_BEGIN;
}

/*
 * DISPATCH NOTE -- read before wiring this into xx_format_detect_type().
 *
 * xx_com_check_is_valid() accepts essentially any file under 65280 bytes that
 * does not start with MZ.  Registering it in the general content sniffer will
 * make it claim small PNGs, text files, shell scripts and truncated archives
 * -- anything whose own reader happens to run after it, and, at the very end
 * of the chain, everything that no reader claimed at all.  The reference
 * avoids this with the file-name suffix test that xx_io_device cannot offer.
 *
 * Recommendation: do NOT add a `if (valid_com) return XX_FILE_TYPE_COM;` arm
 * to the sniffer.  Reach this reader by explicit request only (construct
 * xx_com_create() directly, or via a name/suffix-aware caller above the
 * library).  If it is registered anyway it MUST be the last arm of the
 * function, after every signatured format and after the text/plaintext
 * classification, and it should stay behind whatever "nothing else matched"
 * predicate the sniffer can offer.
 */
