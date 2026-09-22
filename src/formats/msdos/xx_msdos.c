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

#include "xxfclib/formats/msdos/xx_msdos.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/data/xx_data.h"

#include <limits.h>

/* MS-DOS/MZ header signature */
#define XX_MSDOS_SIGNATURE 0x5A4D
#define XX_MSDOS_HEADER_MIN_SIZE 28U
#define XX_MSDOS_HEADER_MAX_SIZE 64U

/* Executable subheader signatures commonly found at offset 0x3C in DOS headers */
#define XX_PE_SIGNATURE 0x00004550
#define XX_NE_SIGNATURE 0x454E
#define XX_LE_SIGNATURE 0x454C
#define XX_LX_SIGNATURE 0x584C

/* Forward declaration of vtable callbacks */
static void xx_msdos_vtable_destroy(Abstractformat *self);

static bool xx_msdos_get_layout(const Abstractformat *format,
                                const xx_msdos *msdos,
                                int64_t *binary_size,
                                int64_t *image_size,
                                int64_t *header_size) {
    int64_t total_size;
    int64_t available;
    uint64_t declared_size;
    uint64_t declared_header_size;

    if (!format || !format->device || !msdos || format->base_address < 0) {
        return false;
    }
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address) {
        return false;
    }
    available = total_size - format->base_address;

    /* e_cblp is in the range 0..511; zero denotes a complete final page.
     * A zero page count or a malformed final-page count cannot describe a
     * trustworthy end, so keep every available byte in the DOS image. */
    if (msdos->pages_in_file == 0 || msdos->bytes_on_last_page > 511U) {
        declared_size = (uint64_t)available;
    } else {
        declared_size = ((uint64_t)msdos->pages_in_file - 1U) * 512U;
        declared_size += msdos->bytes_on_last_page
                             ? (uint64_t)msdos->bytes_on_last_page
                             : 512U;
        if (declared_size > (uint64_t)available) {
            declared_size = (uint64_t)available;
        }
    }

    declared_header_size = (uint64_t)msdos->header_size * 16U;
    if (declared_header_size > declared_size) {
        declared_header_size = declared_size;
    }

    if (binary_size) *binary_size = available;
    if (image_size) *image_size = (int64_t)declared_size;
    if (header_size) *header_size = (int64_t)declared_header_size;
    return true;
}

static bool xx_msdos_update_layout_metadata(xx_msdos *msdos) {
    int64_t binary_size;
    int64_t image_size;
    Abstractformat *format;

    if (!msdos) return false;
    format = &msdos->format;
    if (!xx_msdos_get_layout(format, msdos, &binary_size, &image_size,
                             NULL)) {
        return false;
    }
    format->format_size = image_size;
    if (image_size < binary_size) {
        format->overlay_offset = format->base_address + image_size;
        format->overlay_size = binary_size - image_size;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    return true;
}

static bool xx_msdos_add_address(uint64_t base, uint64_t displacement,
                                 uint64_t *result) {
    if (!result || base == XX_INVALID_ADDRESS ||
        displacement >= XX_INVALID_ADDRESS - base) {
        return false;
    }
    *result = base + displacement;
    return true;
}

void xx_msdos_init(xx_msdos *msdos, xx_io_device *dev, int64_t base_address) {
    if (!msdos) {
        return;
    }
    xx_mem_zero(msdos, sizeof(xx_msdos));

    /* Initialize base Abstractformat */
    xx_format_init(&msdos->format, dev, base_address);

    /* Setup default MS-DOS format attributes */
    msdos->format.endian = XX_ENDIAN_LITTLE;
    msdos->format.file_type = XX_FILE_TYPE_MSDOS;
    msdos->format.os = XX_OS_DOS;
    msdos->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    msdos->format.arch = XX_ARCH_X86_16;
    msdos->format.is_executable = true;
    msdos->format.is_archive = false;
    xx_format_set_mime_type(&msdos->format, "application/x-dosexec");
    xx_format_set_extension(&msdos->format, ".exe");

    /* Setup vtable callbacks */
    msdos->format.check_is_valid = xx_msdos_check_is_valid;
    msdos->format.handle_base_info = xx_msdos_handle_base_info;
    msdos->format.get_format_size = xx_msdos_get_format_size;
    msdos->format.get_memory_map = xx_msdos_get_memory_map;
    msdos->format.destroy = xx_msdos_vtable_destroy;

    /* Initialize fields */
    msdos->signature = 0;
    msdos->bytes_on_last_page = 0;
    msdos->pages_in_file = 0;
    msdos->relocations = 0;
    msdos->header_size = 0;
    msdos->minalloc = 0;
    msdos->maxalloc = 0;
    msdos->ss = 0;
    msdos->sp = 0;
    msdos->checksum = 0;
    msdos->ip = 0;
    msdos->cs = 0;
    msdos->reloc_offset = 0;
    msdos->overlay_number = 0;
    msdos->pe_offset = -1;
    msdos->has_pe_header = false;
}

xx_msdos *xx_msdos_create(xx_io_device *dev, int64_t base_address) {
    xx_msdos *msdos = (xx_msdos *)xx_mem_alloc(sizeof(xx_msdos));
    if (!msdos) {
        return NULL;
    }
    xx_msdos_init(msdos, dev, base_address);
    return msdos;
}

void xx_msdos_destroy(xx_msdos *msdos) {
    if (!msdos) {
        return;
    }
    if (msdos->format.close) {
        msdos->format.close(&msdos->format);
    }
    xx_format_cleanup_extra_parameters(&msdos->format);
}

static void xx_msdos_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_msdos *msdos = (xx_msdos *)self;
        xx_msdos_destroy(msdos);
    }
}

void xx_msdos_free(xx_msdos *msdos) {
    if (!msdos) {
        return;
    }
    xx_msdos_destroy(msdos);
    xx_mem_free(msdos);
}

bool xx_msdos_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || !self->device || self->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }

    int64_t total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < XX_MSDOS_HEADER_MIN_SIZE) {
        return false;
    }

    /* Check MZ signature at offset 0 */
    uint16_t signature = xx_io_get_u16(self->device, self->base_address, false);
    return (signature == XX_MSDOS_SIGNATURE);
}

bool xx_msdos_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    int64_t total_size;
    int64_t available;
    size_t read_size;
    xx_msdos *msdos;
    uint8_t header[XX_MSDOS_HEADER_MAX_SIZE];

    if (!self || !self->device || self->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }

    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) {
        self->is_valid = false;
        return false;
    }
    available = total_size - self->base_address;
    if (available < XX_MSDOS_HEADER_MIN_SIZE) {
        self->is_valid = false;
        return false;
    }

    msdos = (xx_msdos *)self;

    /* The original MZ header is 28 bytes. Later executables commonly extend
     * it to 64 bytes and store e_lfanew at 0x3c. */
    if (xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        self->is_valid = false;
        return false;
    }

    xx_mem_zero(header, sizeof(header));
    read_size = available < (int64_t)sizeof(header)
                    ? (size_t)available
                    : sizeof(header);
    if (xx_io_read(self->device, header, read_size) != (ssize_t)read_size) {
        self->is_valid = false;
        return false;
    }

    /* Parse MS-DOS header fields (all little-endian) */
    msdos->signature = (uint16_t)(header[0] | (header[1] << 8));
    if (msdos->signature != XX_MSDOS_SIGNATURE) {
        self->is_valid = false;
        return false;
    }

    msdos->bytes_on_last_page = (uint16_t)(header[2] | (header[3] << 8));
    msdos->pages_in_file = (uint16_t)(header[4] | (header[5] << 8));
    msdos->relocations = (uint16_t)(header[6] | (header[7] << 8));
    msdos->header_size = (uint16_t)(header[8] | (header[9] << 8));
    msdos->minalloc = (uint16_t)(header[10] | (header[11] << 8));
    msdos->maxalloc = (uint16_t)(header[12] | (header[13] << 8));
    msdos->ss = (uint16_t)(header[14] | (header[15] << 8));
    msdos->sp = (uint16_t)(header[16] | (header[17] << 8));
    msdos->checksum = (uint16_t)(header[18] | (header[19] << 8));
    msdos->ip = (uint16_t)(header[20] | (header[21] << 8));
    msdos->cs = (uint16_t)(header[22] | (header[23] << 8));
    msdos->reloc_offset = (uint16_t)(header[24] | (header[25] << 8));
    msdos->overlay_number = (uint16_t)(header[26] | (header[27] << 8));
    msdos->pe_offset = -1;
    msdos->has_pe_header = false;
    self->format_type = XX_TYPE_CONSOLE_APPLICATION;

    /* Check for a PE/NE/LX/LE executable subheader at offset 0x3C (offset 60 in header). */
    if (read_size >= XX_MSDOS_HEADER_MAX_SIZE) {
        uint32_t pe_offset_field =
            (uint32_t)header[60] | ((uint32_t)header[61] << 8) |
            ((uint32_t)header[62] << 16) | ((uint32_t)header[63] << 24);
        if (pe_offset_field > 0U &&
            (uint64_t)pe_offset_field + 2U <= (uint64_t)available) {
            int64_t subheader_offset = self->base_address + (int64_t)pe_offset_field;
            uint32_t pe_sig = 0U;
            uint16_t short_sig = xx_io_get_u16(self->device, subheader_offset, false);

            if ((uint64_t)pe_offset_field + 4U <= (uint64_t)available) {
                pe_sig = xx_io_get_u32(self->device, subheader_offset, false);
            }

            if (pe_sig == XX_PE_SIGNATURE || short_sig == XX_NE_SIGNATURE ||
                short_sig == XX_LE_SIGNATURE || short_sig == XX_LX_SIGNATURE) {
                msdos->pe_offset = (int64_t)pe_offset_field;
                msdos->has_pe_header = true;
                if (pe_sig == XX_PE_SIGNATURE) {
                    self->format_type = XX_TYPE_GUI_APPLICATION;
                } else {
                    self->format_type = XX_TYPE_CONSOLE_APPLICATION;
                }
            }
        }
    }

    if (!xx_msdos_update_layout_metadata(msdos)) {
        self->is_valid = false;
        return false;
    }

    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_msdos_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_msdos_handle_base_info(self, pd))
               ? self->format_size
               : -1;
}

bool xx_msdos_get_memory_map(Abstractformat *self,
                             xx_memory_map_mode_t mode,
                             xx_memory_map *output,
                             xx_pd_struct *pd) {
    xx_msdos *msdos;
    int64_t binary_size;
    int64_t image_size;
    int64_t header_size;
    int64_t body_size;
    int64_t body_offset;
    uint64_t module_address;
    uint64_t code_displacement;
    uint64_t entry_displacement;
    uint64_t address;

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

    msdos = (xx_msdos *)self;
    if (!xx_msdos_get_layout(self, msdos, &binary_size, &image_size,
                             &header_size)) {
        return false;
    }
    body_size = image_size - header_size;
    body_offset = self->base_address + header_size;
    module_address = self->module_address != XX_INVALID_ADDRESS
                         ? self->module_address
                         : 0U;

    output->binary_offset = self->base_address;
    output->module_address = module_address;
    output->is_image = self->is_mapped;
    output->binary_size = binary_size;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = -1;
    output->start_load_offset = body_offset;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = mode;

    code_displacement = (uint64_t)msdos->cs * 16U;
    entry_displacement = code_displacement + (uint64_t)msdos->ip;
    if (entry_displacement >= UINT64_C(0x100000)) {
        entry_displacement -= UINT64_C(0x100000);
    }
    if (code_displacement < (uint64_t)body_size &&
        xx_msdos_add_address(module_address, code_displacement, &address) &&
        address <= (uint64_t)INT64_MAX) {
        output->code_base = (int64_t)address;
    }
    if (entry_displacement < (uint64_t)body_size) {
        (void)xx_msdos_add_address(module_address, entry_displacement,
                                   &output->entry_point_address);
    }

    if (!xx_memory_map_add_part(output, self->base_address, header_size,
                                XX_INVALID_ADDRESS, 0,
                                XX_FILE_PART_HEADER, 0,
                                "MS-DOS header", false) ||
        !xx_memory_map_add_part(output, body_offset, body_size,
                                module_address, body_size,
                                XX_FILE_PART_SEGMENT, 1,
                                "MS-DOS image", false)) {
        return false;
    }
    if (image_size < binary_size &&
        !xx_memory_map_add_part(output, self->base_address + image_size,
                                binary_size - image_size,
                                XX_INVALID_ADDRESS, 0,
                                XX_FILE_PART_OVERLAY, 2,
                                "Overlay", false)) {
        return false;
    }
    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

/* --- Getters --- */
uint16_t xx_msdos_get_signature(const xx_msdos *msdos) {
    return msdos ? msdos->signature : 0;
}

uint16_t xx_msdos_get_bytes_on_last_page(const xx_msdos *msdos) {
    return msdos ? msdos->bytes_on_last_page : 0;
}

uint16_t xx_msdos_get_pages_in_file(const xx_msdos *msdos) {
    return msdos ? msdos->pages_in_file : 0;
}

uint16_t xx_msdos_get_relocations(const xx_msdos *msdos) {
    return msdos ? msdos->relocations : 0;
}

uint16_t xx_msdos_get_header_size(const xx_msdos *msdos) {
    return msdos ? msdos->header_size : 0;
}

uint16_t xx_msdos_get_minalloc(const xx_msdos *msdos) {
    return msdos ? msdos->minalloc : 0;
}

uint16_t xx_msdos_get_maxalloc(const xx_msdos *msdos) {
    return msdos ? msdos->maxalloc : 0;
}

uint16_t xx_msdos_get_ss(const xx_msdos *msdos) {
    return msdos ? msdos->ss : 0;
}

uint16_t xx_msdos_get_sp(const xx_msdos *msdos) {
    return msdos ? msdos->sp : 0;
}

uint16_t xx_msdos_get_checksum(const xx_msdos *msdos) {
    return msdos ? msdos->checksum : 0;
}

uint16_t xx_msdos_get_ip(const xx_msdos *msdos) {
    return msdos ? msdos->ip : 0;
}

uint16_t xx_msdos_get_cs(const xx_msdos *msdos) {
    return msdos ? msdos->cs : 0;
}

uint16_t xx_msdos_get_reloc_offset(const xx_msdos *msdos) {
    return msdos ? msdos->reloc_offset : 0;
}

uint16_t xx_msdos_get_overlay_number(const xx_msdos *msdos) {
    return msdos ? msdos->overlay_number : 0;
}

int64_t xx_msdos_get_pe_offset(const xx_msdos *msdos) {
    return msdos ? msdos->pe_offset : -1;
}

bool xx_msdos_has_pe_header(const xx_msdos *msdos) {
    return msdos ? msdos->has_pe_header : false;
}

/* --- Setters --- */
void xx_msdos_set_signature(xx_msdos *msdos, uint16_t val) {
    if (msdos) {
        msdos->signature = val;
    }
}

void xx_msdos_set_bytes_on_last_page(xx_msdos *msdos, uint16_t val) {
    if (msdos && msdos->bytes_on_last_page != val) {
        msdos->bytes_on_last_page = val;
        xx_format_invalidate_memory_map(&msdos->format);
        if (msdos->format.base_info_handled) {
            (void)xx_msdos_update_layout_metadata(msdos);
        }
    }
}

void xx_msdos_set_pages_in_file(xx_msdos *msdos, uint16_t val) {
    if (msdos && msdos->pages_in_file != val) {
        msdos->pages_in_file = val;
        xx_format_invalidate_memory_map(&msdos->format);
        if (msdos->format.base_info_handled) {
            (void)xx_msdos_update_layout_metadata(msdos);
        }
    }
}

void xx_msdos_set_relocations(xx_msdos *msdos, uint16_t val) {
    if (msdos) {
        msdos->relocations = val;
    }
}

void xx_msdos_set_pe_offset(xx_msdos *msdos, int64_t val) {
    if (msdos) {
        msdos->pe_offset = val;
    }
}

void xx_msdos_set_has_pe_header(xx_msdos *msdos, bool val) {
    if (msdos) {
        msdos->has_pe_header = val;
    }
}
