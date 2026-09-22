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

#include "xxfclib/formats/atarist/xx_atarist.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/formats/xx_memory_map.h"

/* GEMDOS program header */
#define XX_ATARIST_MAGIC       0x601AU
#define XX_ATARIST_HEADER_SIZE 28U

/* Wired serially; swap for XX_FILE_TYPE_ATARIST / XX_OS_ATARIST once
 * xxfc_defs.h carries them. Kept as local macros so that adding the
 * enumerators cannot be shadowed by a compatibility #define here. */
#define XX_ATARIST_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#define XX_ATARIST_OS        XX_OS_GENERIC

/* Forward declaration of vtable callbacks */
static void xx_atarist_vtable_destroy(Abstractformat *self);

/**
 * Resolve the on-disk layout.
 *
 * @param binary_size   bytes available from base_address to the end of device
 * @param declared_size 28 + text + data + symbols, clamped to what is there
 * @param body_size     text + data actually present in the file
 *
 * Every field is attacker controlled, so the running total is accumulated in
 * uint64_t and clamped against @c binary_size at each step.
 */
static bool xx_atarist_get_layout(const Abstractformat *format,
                                  const xx_atarist *atarist,
                                  int64_t *binary_size,
                                  int64_t *declared_size,
                                  int64_t *body_size) {
    int64_t total_size;
    int64_t available;
    uint64_t limit;
    uint64_t text_present;
    uint64_t data_present;
    uint64_t symbol_present;
    uint64_t used;

    if (!format || !format->device || !atarist || format->base_address < 0) {
        return false;
    }
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address) {
        return false;
    }
    available = total_size - format->base_address;
    if (available < (int64_t)XX_ATARIST_HEADER_SIZE) {
        return false;
    }

    limit = (uint64_t)available;
    used = (uint64_t)XX_ATARIST_HEADER_SIZE;

    text_present = (uint64_t)atarist->text_size;
    if (text_present > limit - used) {
        text_present = limit - used;
    }
    used += text_present;

    data_present = (uint64_t)atarist->data_size;
    if (data_present > limit - used) {
        data_present = limit - used;
    }
    used += data_present;

    symbol_present = (uint64_t)atarist->symbol_size;
    if (symbol_present > limit - used) {
        symbol_present = limit - used;
    }
    used += symbol_present;

    if (binary_size) *binary_size = available;
    if (declared_size) *declared_size = (int64_t)used;
    if (body_size) *body_size = (int64_t)(text_present + data_present);
    return true;
}

static bool xx_atarist_update_layout_metadata(xx_atarist *atarist) {
    int64_t binary_size;
    int64_t declared_size;
    Abstractformat *format;

    if (!atarist) return false;
    format = &atarist->format;
    if (!xx_atarist_get_layout(format, atarist, &binary_size, &declared_size,
                               NULL)) {
        return false;
    }

    /* ABSFLAG zero means a relocation table of unstated length follows the
     * symbols and runs to the end of the file, so nothing past the declared
     * body can be called an overlay. A non-zero ABSFLAG states that no
     * relocation information is present, which makes the tail real slack. */
    if (atarist->relocation != 0 && declared_size < binary_size) {
        format->format_size = declared_size;
        format->overlay_offset = format->base_address + declared_size;
        format->overlay_size = binary_size - declared_size;
    } else {
        format->format_size = binary_size;
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    return true;
}

static bool xx_atarist_add_address(uint64_t base, uint64_t displacement,
                                   uint64_t *result) {
    if (!result || base == XX_INVALID_ADDRESS ||
        displacement >= XX_INVALID_ADDRESS - base) {
        return false;
    }
    *result = base + displacement;
    return true;
}

void xx_atarist_init(xx_atarist *atarist, xx_io_device *dev, int64_t base_address) {
    if (!atarist) {
        return;
    }
    xx_mem_zero(atarist, sizeof(xx_atarist));

    /* Initialize base Abstractformat */
    xx_format_init(&atarist->format, dev, base_address);

    /* Setup default Atari ST format attributes */
    atarist->format.endian = XX_ENDIAN_BIG;
    atarist->format.file_type = XX_ATARIST_FILE_TYPE;
    atarist->format.os = XX_ATARIST_OS;
    atarist->format.format_type = XX_TYPE_GUI_APPLICATION;
    atarist->format.arch = XX_ARCH_M68K;
    atarist->format.is_executable = true;
    atarist->format.is_archive = false;
    xx_format_set_mime_type(&atarist->format, "application/x-gemdos-executable");
    xx_format_set_extension(&atarist->format, ".prg");

    /* Setup vtable callbacks */
    atarist->format.check_is_valid = xx_atarist_check_is_valid;
    atarist->format.handle_base_info = xx_atarist_handle_base_info;
    atarist->format.get_format_size = xx_atarist_get_format_size;
    atarist->format.get_memory_map = xx_atarist_get_memory_map;
    atarist->format.destroy = xx_atarist_vtable_destroy;

    /* Initialize fields */
    atarist->magic = 0;
    atarist->text_size = 0;
    atarist->data_size = 0;
    atarist->bss_size = 0;
    atarist->symbol_size = 0;
    atarist->reserved = 0;
    atarist->flags = 0;
    atarist->relocation = 0;
}

xx_atarist *xx_atarist_create(xx_io_device *dev, int64_t base_address) {
    xx_atarist *atarist = (xx_atarist *)xx_mem_alloc(sizeof(xx_atarist));
    if (!atarist) {
        return NULL;
    }
    xx_atarist_init(atarist, dev, base_address);
    return atarist;
}

void xx_atarist_destroy(xx_atarist *atarist) {
    if (!atarist) {
        return;
    }
    if (atarist->format.close) {
        atarist->format.close(&atarist->format);
    }
    xx_format_cleanup_extra_parameters(&atarist->format);
}

static void xx_atarist_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_atarist *atarist = (xx_atarist *)self;
        xx_atarist_destroy(atarist);
    }
}

void xx_atarist_free(xx_atarist *atarist) {
    if (!atarist) {
        return;
    }
    xx_atarist_destroy(atarist);
    xx_mem_free(atarist);
}

bool xx_atarist_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    int64_t total_size;
    uint16_t magic;

    if (!self || !self->device || self->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }

    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < (int64_t)XX_ATARIST_HEADER_SIZE) {
        return false;
    }

    /* The GEMDOS magic is the only signature the format has; this matches the
     * FT_ATARIST branch in XBinary, which also keys on a big-endian 0x601A at
     * offset zero with at least 28 bytes available. */
    magic = xx_io_get_u16(self->device, self->base_address, true);
    return (magic == XX_ATARIST_MAGIC);
}

bool xx_atarist_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    int64_t total_size;
    int64_t available;
    xx_atarist *atarist;
    uint8_t header[XX_ATARIST_HEADER_SIZE];

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
    if (available < (int64_t)XX_ATARIST_HEADER_SIZE) {
        self->is_valid = false;
        return false;
    }

    atarist = (xx_atarist *)self;

    if (xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        self->is_valid = false;
        return false;
    }

    xx_mem_zero(header, sizeof(header));
    if (xx_io_read(self->device, header, sizeof(header)) !=
        (ssize_t)sizeof(header)) {
        self->is_valid = false;
        return false;
    }

    /* Parse the GEMDOS header (big-endian throughout) */
    atarist->magic = (uint16_t)(((uint16_t)header[0] << 8) | header[1]);
    if (atarist->magic != XX_ATARIST_MAGIC) {
        self->is_valid = false;
        return false;
    }

    atarist->text_size = ((uint32_t)header[2] << 24) |
                         ((uint32_t)header[3] << 16) |
                         ((uint32_t)header[4] << 8) | (uint32_t)header[5];
    atarist->data_size = ((uint32_t)header[6] << 24) |
                         ((uint32_t)header[7] << 16) |
                         ((uint32_t)header[8] << 8) | (uint32_t)header[9];
    atarist->bss_size = ((uint32_t)header[10] << 24) |
                        ((uint32_t)header[11] << 16) |
                        ((uint32_t)header[12] << 8) | (uint32_t)header[13];
    atarist->symbol_size = ((uint32_t)header[14] << 24) |
                           ((uint32_t)header[15] << 16) |
                           ((uint32_t)header[16] << 8) | (uint32_t)header[17];
    atarist->reserved = ((uint32_t)header[18] << 24) |
                        ((uint32_t)header[19] << 16) |
                        ((uint32_t)header[20] << 8) | (uint32_t)header[21];
    atarist->flags = ((uint32_t)header[22] << 24) |
                     ((uint32_t)header[23] << 16) |
                     ((uint32_t)header[24] << 8) | (uint32_t)header[25];
    atarist->relocation = (uint16_t)(((uint16_t)header[26] << 8) | header[27]);

    self->format_type = XX_TYPE_GUI_APPLICATION;

    if (!xx_atarist_update_layout_metadata(atarist)) {
        self->is_valid = false;
        return false;
    }

    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_atarist_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_atarist_handle_base_info(self, pd))
               ? self->format_size
               : -1;
}

bool xx_atarist_get_memory_map(Abstractformat *self,
                               xx_memory_map_mode_t mode,
                               xx_memory_map *output,
                               xx_pd_struct *pd) {
    xx_atarist *atarist;
    int64_t binary_size;
    int64_t declared_size;
    int64_t body_size;
    int64_t text_present;
    int64_t data_present;
    int64_t symbol_present;
    int64_t offset;
    uint64_t module_address;
    uint64_t address;
    int32_t part_number;

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

    atarist = (xx_atarist *)self;
    if (!xx_atarist_get_layout(self, atarist, &binary_size, &declared_size,
                               &body_size)) {
        return false;
    }

    /* Split the clamped body back into its two segments. */
    text_present = (int64_t)atarist->text_size;
    if (text_present > body_size) {
        text_present = body_size;
    }
    data_present = body_size - text_present;
    symbol_present = declared_size - (int64_t)XX_ATARIST_HEADER_SIZE - body_size;

    module_address = self->module_address != XX_INVALID_ADDRESS
                         ? self->module_address
                         : 0U;

    output->binary_offset = self->base_address;
    output->module_address = module_address;
    output->is_image = self->is_mapped;
    output->binary_size = binary_size;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = -1;
    output->start_load_offset = self->base_address + (int64_t)XX_ATARIST_HEADER_SIZE;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = mode;

    /* GEMDOS loads TEXT at the base page and enters at its first byte. */
    if (module_address <= (uint64_t)INT64_MAX) {
        output->code_base = (int64_t)module_address;
    }
    output->entry_point_address = module_address;

    offset = self->base_address;
    part_number = 0;
    if (!xx_memory_map_add_part(output, offset, (int64_t)XX_ATARIST_HEADER_SIZE,
                                XX_INVALID_ADDRESS, 0,
                                XX_FILE_PART_HEADER, part_number,
                                "GEMDOS header", false)) {
        return false;
    }
    offset += (int64_t)XX_ATARIST_HEADER_SIZE;
    part_number++;

    if (text_present > 0) {
        if (!xx_memory_map_add_part(output, offset, text_present,
                                    module_address, text_present,
                                    XX_FILE_PART_SEGMENT, part_number,
                                    "TEXT", false)) {
            return false;
        }
        offset += text_present;
        part_number++;
    }

    if (data_present > 0) {
        if (!xx_atarist_add_address(module_address, (uint64_t)text_present,
                                    &address)) {
            return false;
        }
        if (!xx_memory_map_add_part(output, offset, data_present, address,
                                    data_present,
                                    XX_FILE_PART_SEGMENT, part_number,
                                    "DATA", false)) {
            return false;
        }
        offset += data_present;
        part_number++;
    }

    if (atarist->bss_size > 0) {
        if (!xx_atarist_add_address(module_address, (uint64_t)body_size,
                                    &address)) {
            return false;
        }
        /* BSS occupies no file bytes; emitted as a virtual-only tail. */
        if (!xx_memory_map_add_part(output, -1, 0, address,
                                    (int64_t)atarist->bss_size,
                                    XX_FILE_PART_SEGMENT, part_number,
                                    "BSS", false)) {
            return false;
        }
        part_number++;
    }

    if (symbol_present > 0) {
        if (!xx_memory_map_add_part(output, offset, symbol_present,
                                    XX_INVALID_ADDRESS, 0,
                                    XX_FILE_PART_DEBUG, part_number,
                                    "Symbol table", false)) {
            return false;
        }
        offset += symbol_present;
        part_number++;
    }

    if (declared_size < binary_size) {
        int64_t tail_size = binary_size - declared_size;
        if (atarist->relocation == 0) {
            /* ABSFLAG clear: the tail is the relocation table. */
            if (!xx_memory_map_add_part(output, offset, tail_size,
                                        XX_INVALID_ADDRESS, 0,
                                        XX_FILE_PART_TABLE, part_number,
                                        "Relocations", false)) {
                return false;
            }
        } else if (!xx_memory_map_add_part(output, offset, tail_size,
                                           XX_INVALID_ADDRESS, 0,
                                           XX_FILE_PART_OVERLAY, part_number,
                                           "Overlay", false)) {
            return false;
        }
        part_number++;
    }

    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

/* --- Getters --- */
uint16_t xx_atarist_get_magic(const xx_atarist *atarist) {
    return atarist ? atarist->magic : 0;
}

uint32_t xx_atarist_get_text_size(const xx_atarist *atarist) {
    return atarist ? atarist->text_size : 0;
}

uint32_t xx_atarist_get_data_size(const xx_atarist *atarist) {
    return atarist ? atarist->data_size : 0;
}

uint32_t xx_atarist_get_bss_size(const xx_atarist *atarist) {
    return atarist ? atarist->bss_size : 0;
}

uint32_t xx_atarist_get_symbol_size(const xx_atarist *atarist) {
    return atarist ? atarist->symbol_size : 0;
}

uint32_t xx_atarist_get_reserved(const xx_atarist *atarist) {
    return atarist ? atarist->reserved : 0;
}

uint32_t xx_atarist_get_flags(const xx_atarist *atarist) {
    return atarist ? atarist->flags : 0;
}

uint16_t xx_atarist_get_relocation(const xx_atarist *atarist) {
    return atarist ? atarist->relocation : 0;
}

int64_t xx_atarist_get_text_offset(const xx_atarist *atarist) {
    return atarist ? (int64_t)XX_ATARIST_HEADER_SIZE : -1;
}

int64_t xx_atarist_get_data_offset(const xx_atarist *atarist) {
    int64_t binary_size;
    int64_t body_size;
    int64_t text_present;

    if (!atarist) return -1;
    if (!xx_atarist_get_layout(&atarist->format, atarist, &binary_size, NULL,
                               &body_size)) {
        return -1;
    }
    text_present = (int64_t)atarist->text_size;
    if (text_present > body_size) {
        text_present = body_size;
    }
    if (body_size - text_present <= 0) return -1;
    return (int64_t)XX_ATARIST_HEADER_SIZE + text_present;
}

int64_t xx_atarist_get_symbol_offset(const xx_atarist *atarist) {
    int64_t binary_size;
    int64_t declared_size;
    int64_t body_size;

    if (!atarist) return -1;
    if (!xx_atarist_get_layout(&atarist->format, atarist, &binary_size,
                               &declared_size, &body_size)) {
        return -1;
    }
    if (declared_size - (int64_t)XX_ATARIST_HEADER_SIZE - body_size <= 0) {
        return -1;
    }
    return (int64_t)XX_ATARIST_HEADER_SIZE + body_size;
}

int64_t xx_atarist_get_relocation_offset(const xx_atarist *atarist) {
    int64_t binary_size;
    int64_t declared_size;

    if (!atarist || atarist->relocation != 0) return -1;
    if (!xx_atarist_get_layout(&atarist->format, atarist, &binary_size,
                               &declared_size, NULL)) {
        return -1;
    }
    if (declared_size >= binary_size) return -1;
    return declared_size;
}

int64_t xx_atarist_get_image_size(const xx_atarist *atarist) {
    if (!atarist) return -1;
    return (int64_t)atarist->text_size + (int64_t)atarist->data_size +
           (int64_t)atarist->bss_size;
}

/* --- Setters --- */
void xx_atarist_set_text_size(xx_atarist *atarist, uint32_t val) {
    if (atarist && atarist->text_size != val) {
        atarist->text_size = val;
        xx_format_invalidate_memory_map(&atarist->format);
        if (atarist->format.base_info_handled) {
            (void)xx_atarist_update_layout_metadata(atarist);
        }
    }
}

void xx_atarist_set_data_size(xx_atarist *atarist, uint32_t val) {
    if (atarist && atarist->data_size != val) {
        atarist->data_size = val;
        xx_format_invalidate_memory_map(&atarist->format);
        if (atarist->format.base_info_handled) {
            (void)xx_atarist_update_layout_metadata(atarist);
        }
    }
}

void xx_atarist_set_bss_size(xx_atarist *atarist, uint32_t val) {
    if (atarist && atarist->bss_size != val) {
        atarist->bss_size = val;
        xx_format_invalidate_memory_map(&atarist->format);
    }
}

void xx_atarist_set_symbol_size(xx_atarist *atarist, uint32_t val) {
    if (atarist && atarist->symbol_size != val) {
        atarist->symbol_size = val;
        xx_format_invalidate_memory_map(&atarist->format);
        if (atarist->format.base_info_handled) {
            (void)xx_atarist_update_layout_metadata(atarist);
        }
    }
}

void xx_atarist_set_flags(xx_atarist *atarist, uint32_t val) {
    if (atarist) {
        atarist->flags = val;
    }
}

void xx_atarist_set_relocation(xx_atarist *atarist, uint16_t val) {
    if (atarist && atarist->relocation != val) {
        atarist->relocation = val;
        xx_format_invalidate_memory_map(&atarist->format);
        if (atarist->format.base_info_handled) {
            (void)xx_atarist_update_layout_metadata(atarist);
        }
    }
}
