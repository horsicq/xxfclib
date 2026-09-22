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

#include "xxfclib/formats/dos16m/xx_dos16m.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/data/xx_data.h"

/* Container signatures. */
#define XX_DOS16M_MZ_SIGNATURE 0x5A4D /* 'MZ' */
#define XX_DOS16M_BW_SIGNATURE 0x5742 /* 'BW', Rational Systems dos16m_exe_header */
#define XX_DOS16M_MF_SIGNATURE 0x464D /* 'MF', spliced info block */

/* Subheader signatures of the trailing MZ payload. */
#define XX_DOS16M_NE_SIGNATURE 0x454E /* 'NE' -> DOS/16M */
#define XX_DOS16M_LE_SIGNATURE 0x454C /* 'LE' -> DOS/4G */
#define XX_DOS16M_LX_SIGNATURE 0x584C /* 'LX' -> DOS/4G */

/* MS-DOS stub field offsets. */
#define XX_DOS16M_OFF_E_MAGIC 0
#define XX_DOS16M_OFF_E_CBLP 2
#define XX_DOS16M_OFF_E_CP 4
#define XX_DOS16M_OFF_E_LFANEW 0x3C

/* dos16m_exe_header field offsets. The reference struct carries no packing
 * pragma; every member there is naturally aligned, so these literals match
 * the C++ offsetof() results exactly. */
#define XX_DOS16M_OFF_SIGNATURE 0
#define XX_DOS16M_OFF_LAST_PAGE_BYTES 2
#define XX_DOS16M_OFF_PAGES_IN_FILE 4
#define XX_DOS16M_OFF_RESERVED1 6
#define XX_DOS16M_OFF_RESERVED2 8
#define XX_DOS16M_OFF_MIN_ALLOC 10
#define XX_DOS16M_OFF_MAX_ALLOC 12
#define XX_DOS16M_OFF_STACK_SEG 14
#define XX_DOS16M_OFF_STACK_PTR 16
#define XX_DOS16M_OFF_FIRST_RELOC_SEL 18
#define XX_DOS16M_OFF_INIT_IP 20
#define XX_DOS16M_OFF_CODE_SEG 22
#define XX_DOS16M_OFF_RUNTIME_GDT_SIZE 24
#define XX_DOS16M_OFF_MAKEPM_VERSION 26
#define XX_DOS16M_OFF_NEXT_HEADER_POS 28
#define XX_DOS16M_OFF_CV_INFO_OFFSET 32
#define XX_DOS16M_OFF_LAST_SEL_USED 36
#define XX_DOS16M_OFF_PMEM_ALLOC 38
#define XX_DOS16M_OFF_ALLOC_INCR 40
#define XX_DOS16M_OFF_OPTIONS 48
#define XX_DOS16M_OFF_TRANS_STACK_SEL 50
#define XX_DOS16M_OFF_EXP_FLAGS 52
#define XX_DOS16M_OFF_PROGRAM_SIZE 54
#define XX_DOS16M_OFF_GDTIMAGE_SIZE 56
#define XX_DOS16M_OFF_FIRST_SELECTOR 58
#define XX_DOS16M_OFF_DEFAULT_MEM_STRATEGY 60
#define XX_DOS16M_OFF_TRANSFER_BUFFER_SIZE 62
#define XX_DOS16M_OFF_EXP_PATH 112
#define XX_DOS16M_HEADER_SIZE 176U

/* The outer stub must at least carry e_cp. */
#define XX_DOS16M_STUB_MIN_SIZE 6U

/* The 'MF' info block carries a 32-bit skip length at +2. */
#define XX_DOS16M_OFF_MF_LENGTH 2

/* Hard cap on chain length, so a crafted cycle-free but pathological chain
 * cannot turn detection into a long scan. Each step must also advance. */
#define XX_DOS16M_MAX_CHAIN_STEPS 4096U

static void xx_dos16m_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Chain walk                                                          */
/* ------------------------------------------------------------------ */

/** Result of walking the `BW`/`MF`/`MZ` chain that follows the DOS stub. */
typedef struct xx_dos16m_chain {
    int64_t stub_size;
    int64_t first_header_offset;
    int64_t payload_offset;
    uint16_t payload_subsignature;
    uint32_t number_of_headers;
    bool has_payload;
    xx_dos16m_variant_t variant;
} xx_dos16m_chain;

/** True when [offset, offset + size) fits entirely inside [0, available). */
static bool xx_dos16m_range_ok(int64_t offset, int64_t available, uint64_t size) {
    if (offset < 0 || available < 0 || offset > available) {
        return false;
    }
    return size <= (uint64_t)(available - offset);
}

/**
 * Walk the extender chain and resolve the variant.
 *
 * Mirrors XDOS16::getFileType(): a `BW` header alone already means DOS/16M;
 * a trailing `MZ` whose subheader is `NE` keeps it DOS/16M, while `LE` or
 * `LX` promotes it to DOS/4G. Unlike the reference this refuses non-advancing
 * and out-of-range links instead of looping or reading past the device.
 */
static bool xx_dos16m_walk(const Abstractformat *self, xx_dos16m_chain *chain,
                           xx_pd_struct *pd) {
    int64_t total_size;
    int64_t available;
    int64_t base;
    int64_t cursor;
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint32_t steps;

    if (!self || !self->device || !chain || self->base_address < 0) {
        return false;
    }
    if (xx_pd_is_stopped(pd)) {
        return false;
    }

    xx_mem_zero(chain, sizeof(*chain));
    chain->payload_offset = -1;
    chain->variant = XX_DOS16M_VARIANT_NONE;

    base = self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size < base) {
        return false;
    }
    available = total_size - base;
    if (available < (int64_t)XX_DOS16M_STUB_MIN_SIZE) {
        return false;
    }

    e_magic = xx_io_get_u16(self->device, base + XX_DOS16M_OFF_E_MAGIC, false);
    if (e_magic != XX_DOS16M_MZ_SIGNATURE) {
        return false;
    }

    e_cblp = xx_io_get_u16(self->device, base + XX_DOS16M_OFF_E_CBLP, false);
    e_cp = xx_io_get_u16(self->device, base + XX_DOS16M_OFF_E_CP, false);
    if (e_cp == 0) {
        return false;
    }

    /* (e_cp - 1) * 512 + e_cblp cannot overflow int64_t: both operands are
     * 16-bit, so the product is at most 0x1FFFE00. */
    chain->stub_size = ((int64_t)e_cp - 1) * 512 + (int64_t)e_cblp;
    chain->first_header_offset = chain->stub_size;

    /* The chain must start strictly after the stub and inside the file. */
    if (chain->stub_size <= 0) {
        return false;
    }
    if (!xx_dos16m_range_ok(chain->stub_size, available, 2U)) {
        return false;
    }

    cursor = chain->stub_size;

    for (steps = 0; steps < XX_DOS16M_MAX_CHAIN_STEPS; steps++) {
        uint16_t signature;

        if (xx_pd_is_stopped(pd)) {
            return false;
        }
        if (!xx_dos16m_range_ok(cursor, available, 2U)) {
            break;
        }

        signature = xx_io_get_u16(self->device, base + cursor, false);

        if (signature == XX_DOS16M_BW_SIGNATURE) {
            uint32_t next;
            int64_t next_offset;

            if (!xx_dos16m_range_ok(cursor, available, XX_DOS16M_HEADER_SIZE)) {
                /* A truncated first header is not a credible extender. */
                break;
            }

            chain->number_of_headers++;
            chain->variant = XX_DOS16M_VARIANT_DOS16M;

            next = xx_io_get_u32(self->device,
                                 base + cursor + XX_DOS16M_OFF_NEXT_HEADER_POS,
                                 false);
            next_offset = (int64_t)next;
            if (next_offset <= cursor || next_offset >= available) {
                /* Terminal header: nothing further to walk. */
                break;
            }
            cursor = next_offset;
        } else if (signature == XX_DOS16M_MF_SIGNATURE) {
            uint32_t skip;
            int64_t next_offset;

            if (!xx_dos16m_range_ok(cursor, available,
                                    (uint64_t)XX_DOS16M_OFF_MF_LENGTH + 4U)) {
                break;
            }
            skip = xx_io_get_u32(self->device,
                                 base + cursor + XX_DOS16M_OFF_MF_LENGTH, false);
            if (skip == 0U) {
                break;
            }
            next_offset = cursor + (int64_t)skip;
            if (next_offset <= cursor || next_offset >= available) {
                break;
            }
            cursor = next_offset;
        } else if (signature == XX_DOS16M_MZ_SIGNATURE) {
            uint32_t e_lfanew;
            int64_t sub_offset;

            chain->payload_offset = cursor;
            chain->has_payload = true;

            if (!xx_dos16m_range_ok(cursor, available,
                                    (uint64_t)XX_DOS16M_OFF_E_LFANEW + 4U)) {
                break;
            }
            e_lfanew = xx_io_get_u32(self->device,
                                     base + cursor + XX_DOS16M_OFF_E_LFANEW,
                                     false);
            /* e_lfanew is relative to the payload MZ, not to the file. */
            if ((uint64_t)e_lfanew + 2U > (uint64_t)(available - cursor)) {
                break;
            }
            sub_offset = cursor + (int64_t)e_lfanew;
            chain->payload_subsignature =
                xx_io_get_u16(self->device, base + sub_offset, false);

            if (chain->number_of_headers > 0) {
                if (chain->payload_subsignature == XX_DOS16M_NE_SIGNATURE) {
                    chain->variant = XX_DOS16M_VARIANT_DOS16M;
                } else if (chain->payload_subsignature == XX_DOS16M_LE_SIGNATURE ||
                           chain->payload_subsignature == XX_DOS16M_LX_SIGNATURE) {
                    chain->variant = XX_DOS16M_VARIANT_DOS4G;
                }
            }
            break;
        } else {
            break;
        }
    }

    return chain->variant != XX_DOS16M_VARIANT_NONE;
}

/* ------------------------------------------------------------------ */
/* Shared implementation                                               */
/* ------------------------------------------------------------------ */

static void xx_dos16m_init_common(xx_dos16m *dos16m, xx_io_device *dev,
                                  int64_t base_address,
                                  xx_file_type_t file_type) {
    if (!dos16m) {
        return;
    }
    xx_mem_zero(dos16m, sizeof(xx_dos16m));

    xx_format_init(&dos16m->format, dev, base_address);

    dos16m->format.endian = XX_ENDIAN_LITTLE;
    dos16m->format.file_type = file_type;
    dos16m->format.os = XX_OS_DOS;
    dos16m->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    /* The stub runs in real mode but the extended images are 386 code; the
     * reference reports "386" for both variants. */
    dos16m->format.arch = XX_ARCH_X86;
    dos16m->format.is_executable = true;
    dos16m->format.is_archive = false;
    xx_format_set_mime_type(&dos16m->format, "application/x-dosexec");
    xx_format_set_extension(&dos16m->format, ".exe");

    dos16m->payload_offset = -1;
    dos16m->variant = XX_DOS16M_VARIANT_NONE;
}

static bool xx_dos16m_check_variant(Abstractformat *self, xx_pd_struct *pd,
                                    xx_dos16m_variant_t wanted) {
    xx_dos16m_chain chain;

    if (!xx_dos16m_walk(self, &chain, pd)) {
        return false;
    }
    return chain.variant == wanted;
}

static bool xx_dos16m_read_first_header(xx_dos16m *dos16m,
                                        const xx_dos16m_chain *chain) {
    Abstractformat *self;
    int64_t at;
    uint32_t i;

    if (!dos16m || !chain) {
        return false;
    }
    self = &dos16m->format;
    at = self->base_address + chain->first_header_offset;

    dos16m->signature = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_SIGNATURE, false);
    dos16m->last_page_bytes = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_LAST_PAGE_BYTES, false);
    dos16m->pages_in_file = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_PAGES_IN_FILE, false);
    dos16m->reserved1 = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_RESERVED1, false);
    dos16m->reserved2 = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_RESERVED2, false);
    dos16m->min_alloc = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_MIN_ALLOC, false);
    dos16m->max_alloc = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_MAX_ALLOC, false);
    dos16m->stack_seg = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_STACK_SEG, false);
    dos16m->stack_ptr = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_STACK_PTR, false);
    dos16m->first_reloc_sel = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_FIRST_RELOC_SEL, false);
    dos16m->init_ip = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_INIT_IP, false);
    dos16m->code_seg = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_CODE_SEG, false);
    dos16m->runtime_gdt_size = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_RUNTIME_GDT_SIZE, false);
    dos16m->MAKEPM_version = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_MAKEPM_VERSION, false);
    dos16m->next_header_pos = xx_io_get_u32(self->device, at + XX_DOS16M_OFF_NEXT_HEADER_POS, false);
    dos16m->cv_info_offset = xx_io_get_u32(self->device, at + XX_DOS16M_OFF_CV_INFO_OFFSET, false);
    dos16m->last_sel_used = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_LAST_SEL_USED, false);
    dos16m->pmem_alloc = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_PMEM_ALLOC, false);
    dos16m->alloc_incr = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_ALLOC_INCR, false);
    dos16m->options = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_OPTIONS, false);
    dos16m->trans_stack_sel = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_TRANS_STACK_SEL, false);
    dos16m->exp_flags = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_EXP_FLAGS, false);
    dos16m->program_size = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_PROGRAM_SIZE, false);
    dos16m->gdtimage_size = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_GDTIMAGE_SIZE, false);
    dos16m->first_selector = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_FIRST_SELECTOR, false);
    dos16m->default_mem_strategy =
        (uint8_t)(xx_io_get_u16(self->device, at + XX_DOS16M_OFF_DEFAULT_MEM_STRATEGY, false) & 0xFFU);
    dos16m->transfer_buffer_size = xx_io_get_u16(self->device, at + XX_DOS16M_OFF_TRANSFER_BUFFER_SIZE, false);

    /* EXP_path is a fixed 64-byte field that need not be NUL-terminated.
     * Read it byte by byte and terminate it ourselves. */
    if (xx_io_seek64(self->device, at + XX_DOS16M_OFF_EXP_PATH, SEEK_SET) != 0) {
        return false;
    }
    if (xx_io_read(self->device, dos16m->EXP_path, XX_DOS16M_EXP_PATH_SIZE) !=
        (ssize_t)XX_DOS16M_EXP_PATH_SIZE) {
        return false;
    }
    dos16m->EXP_path[XX_DOS16M_EXP_PATH_SIZE] = '\0';
    for (i = 0; i < XX_DOS16M_EXP_PATH_SIZE; i++) {
        unsigned char c = (unsigned char)dos16m->EXP_path[i];
        if (c < 0x20U || c > 0x7EU) {
            dos16m->EXP_path[i] = '\0';
            break;
        }
    }

    return true;
}

static bool xx_dos16m_handle_base_info_common(Abstractformat *self,
                                              xx_pd_struct *pd,
                                              xx_dos16m_variant_t wanted) {
    xx_dos16m *dos16m;
    xx_dos16m_chain chain;
    int64_t total_size;
    int64_t available;

    if (!self || !self->device || self->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }

    if (!xx_dos16m_walk(self, &chain, pd) || chain.variant != wanted) {
        self->is_valid = false;
        return false;
    }

    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) {
        self->is_valid = false;
        return false;
    }
    available = total_size - self->base_address;

    dos16m = (xx_dos16m *)self;

    dos16m->e_magic = xx_io_get_u16(self->device,
                                    self->base_address + XX_DOS16M_OFF_E_MAGIC, false);
    dos16m->e_cblp = xx_io_get_u16(self->device,
                                   self->base_address + XX_DOS16M_OFF_E_CBLP, false);
    dos16m->e_cp = xx_io_get_u16(self->device,
                                 self->base_address + XX_DOS16M_OFF_E_CP, false);

    dos16m->stub_size = chain.stub_size;
    dos16m->first_header_offset = chain.first_header_offset;
    dos16m->payload_offset = chain.payload_offset;
    dos16m->payload_subsignature = chain.payload_subsignature;
    dos16m->number_of_headers = chain.number_of_headers;
    dos16m->has_payload = chain.has_payload;
    dos16m->variant = chain.variant;

    if (!xx_dos16m_read_first_header(dos16m, &chain)) {
        self->is_valid = false;
        return false;
    }

    /* The extender image always spans the whole file: the chain is spliced,
     * not appended, so there is nothing to call an overlay. */
    self->format_size = available;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->format_type = XX_TYPE_CONSOLE_APPLICATION;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

static int64_t xx_dos16m_get_format_size_common(Abstractformat *self,
                                                xx_pd_struct *pd,
                                                xx_dos16m_variant_t wanted) {
    if (!self) {
        return -1;
    }
    if (!self->base_info_handled &&
        !xx_dos16m_handle_base_info_common(self, pd, wanted)) {
        return -1;
    }
    return self->format_size;
}

static bool xx_dos16m_get_memory_map_common(Abstractformat *self,
                                            xx_memory_map_mode_t mode,
                                            xx_memory_map *output,
                                            xx_pd_struct *pd) {
    xx_dos16m *dos16m;
    xx_dos16m_chain chain;
    int64_t total_size;
    int64_t available;
    int64_t base;
    int64_t cursor;
    int32_t index;
    uint32_t steps;

    if (!self || !output || !self->device || !self->base_info_handled ||
        self->base_address < 0 || xx_pd_is_stopped(pd)) {
        return false;
    }
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) {
        mode = XX_MEMORY_MAP_MODE_REGIONS;
    }
    if (mode != XX_MEMORY_MAP_MODE_REGIONS) {
        return false;
    }

    dos16m = (xx_dos16m *)self;
    base = self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size < base) {
        return false;
    }
    available = total_size - base;

    if (!xx_dos16m_walk(self, &chain, pd) || chain.variant != dos16m->variant) {
        return false;
    }

    output->binary_offset = base;
    output->module_address = self->module_address != XX_INVALID_ADDRESS
                                 ? self->module_address
                                 : 0U;
    output->is_image = self->is_mapped;
    output->binary_size = available;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = -1;
    output->start_load_offset = base;
    output->file_type = self->file_type;
    output->format_type = self->format_type;
    output->endian = self->endian;
    output->arch = self->arch;
    output->mode = mode;

    index = 0;

    /* The real-mode loader stub in front of the first `BW` header. */
    if (!xx_memory_map_add_part(output, base, chain.stub_size,
                                XX_INVALID_ADDRESS, 0,
                                XX_FILE_PART_HEADER, index++,
                                "Loader", false)) {
        return false;
    }

    cursor = chain.stub_size;
    for (steps = 0; steps < XX_DOS16M_MAX_CHAIN_STEPS; steps++) {
        uint16_t signature;

        if (xx_pd_is_stopped(pd)) {
            return false;
        }
        if (!xx_dos16m_range_ok(cursor, available, 2U)) {
            break;
        }

        signature = xx_io_get_u16(self->device, base + cursor, false);

        if (signature == XX_DOS16M_BW_SIGNATURE) {
            uint32_t next;
            int64_t end;

            if (!xx_dos16m_range_ok(cursor, available, XX_DOS16M_HEADER_SIZE)) {
                break;
            }
            next = xx_io_get_u32(self->device,
                                 base + cursor + XX_DOS16M_OFF_NEXT_HEADER_POS,
                                 false);
            end = (int64_t)next;
            if (end <= cursor || end > available) {
                end = available;
            }

            if (!xx_memory_map_add_part(output, base + cursor, end - cursor,
                                        XX_INVALID_ADDRESS, 0,
                                        XX_FILE_PART_SEGMENT, index++,
                                        "Segment", false)) {
                return false;
            }

            if (end >= available || end <= cursor) {
                break;
            }
            cursor = end;
        } else if (signature == XX_DOS16M_MF_SIGNATURE) {
            uint32_t skip;
            int64_t end;

            if (!xx_dos16m_range_ok(cursor, available,
                                    (uint64_t)XX_DOS16M_OFF_MF_LENGTH + 4U)) {
                break;
            }
            skip = xx_io_get_u32(self->device,
                                 base + cursor + XX_DOS16M_OFF_MF_LENGTH, false);
            if (skip == 0U) {
                break;
            }
            end = cursor + (int64_t)skip;
            if (end <= cursor || end > available) {
                break;
            }
            if (!xx_memory_map_add_part(output, base + cursor, end - cursor,
                                        XX_INVALID_ADDRESS, 0,
                                        XX_FILE_PART_REGION, index++,
                                        "Info", false)) {
                return false;
            }
            cursor = end;
        } else if (signature == XX_DOS16M_MZ_SIGNATURE) {
            if (!xx_memory_map_add_part(output, base + cursor,
                                        available - cursor,
                                        XX_INVALID_ADDRESS, 0,
                                        XX_FILE_PART_DATA, index++,
                                        "Payload", false)) {
                return false;
            }
            cursor = available;
            break;
        } else {
            break;
        }
    }

    /* Whatever the walk could not account for is trailing data. */
    if ((cursor > 0) && (cursor < available) &&
        !xx_memory_map_add_part(output, base + cursor, available - cursor,
                                XX_INVALID_ADDRESS, 0,
                                XX_FILE_PART_OVERLAY, index++,
                                "Overlay", false)) {
        return false;
    }

    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(output);
}

/* ------------------------------------------------------------------ */
/* DOS/16M surface                                                     */
/* ------------------------------------------------------------------ */

void xx_dos16m_init(xx_dos16m *dos16m, xx_io_device *dev, int64_t base_address) {
    if (!dos16m) {
        return;
    }
    xx_dos16m_init_common(dos16m, dev, base_address, XX_FILE_TYPE_DOS16M);

    dos16m->format.check_is_valid = xx_dos16m_check_is_valid;
    dos16m->format.handle_base_info = xx_dos16m_handle_base_info;
    dos16m->format.get_format_size = xx_dos16m_get_format_size;
    dos16m->format.get_memory_map = xx_dos16m_get_memory_map;
    dos16m->format.destroy = xx_dos16m_vtable_destroy;
}

xx_dos16m *xx_dos16m_create(xx_io_device *dev, int64_t base_address) {
    xx_dos16m *dos16m = (xx_dos16m *)xx_mem_alloc(sizeof(xx_dos16m));
    if (!dos16m) {
        return NULL;
    }
    xx_dos16m_init(dos16m, dev, base_address);
    return dos16m;
}

void xx_dos16m_destroy(xx_dos16m *dos16m) {
    if (!dos16m) {
        return;
    }
    if (dos16m->format.close) {
        dos16m->format.close(&dos16m->format);
    }
    xx_format_cleanup_extra_parameters(&dos16m->format);
}

static void xx_dos16m_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_dos16m_destroy((xx_dos16m *)self);
    }
}

void xx_dos16m_free(xx_dos16m *dos16m) {
    if (!dos16m) {
        return;
    }
    xx_dos16m_destroy(dos16m);
    xx_mem_free(dos16m);
}

bool xx_dos16m_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    return xx_dos16m_check_variant(self, pd, XX_DOS16M_VARIANT_DOS16M);
}

bool xx_dos16m_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    return xx_dos16m_handle_base_info_common(self, pd, XX_DOS16M_VARIANT_DOS16M);
}

int64_t xx_dos16m_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return xx_dos16m_get_format_size_common(self, pd, XX_DOS16M_VARIANT_DOS16M);
}

bool xx_dos16m_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode,
                              xx_memory_map *output, xx_pd_struct *pd) {
    return xx_dos16m_get_memory_map_common(self, mode, output, pd);
}

/* ------------------------------------------------------------------ */
/* DOS/4G surface                                                      */
/* ------------------------------------------------------------------ */

static void xx_dos4g_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_dos4g_destroy((xx_dos4g *)self);
    }
}

void xx_dos4g_init(xx_dos4g *dos4g, xx_io_device *dev, int64_t base_address) {
    if (!dos4g) {
        return;
    }
    xx_dos16m_init_common(dos4g, dev, base_address, XX_FILE_TYPE_DOS4G);

    dos4g->format.check_is_valid = xx_dos4g_check_is_valid;
    dos4g->format.handle_base_info = xx_dos4g_handle_base_info;
    dos4g->format.get_format_size = xx_dos4g_get_format_size;
    dos4g->format.get_memory_map = xx_dos4g_get_memory_map;
    dos4g->format.destroy = xx_dos4g_vtable_destroy;
}

xx_dos4g *xx_dos4g_create(xx_io_device *dev, int64_t base_address) {
    xx_dos4g *dos4g = (xx_dos4g *)xx_mem_alloc(sizeof(xx_dos4g));
    if (!dos4g) {
        return NULL;
    }
    xx_dos4g_init(dos4g, dev, base_address);
    return dos4g;
}

void xx_dos4g_destroy(xx_dos4g *dos4g) {
    if (!dos4g) {
        return;
    }
    if (dos4g->format.close) {
        dos4g->format.close(&dos4g->format);
    }
    xx_format_cleanup_extra_parameters(&dos4g->format);
}

void xx_dos4g_free(xx_dos4g *dos4g) {
    if (!dos4g) {
        return;
    }
    xx_dos4g_destroy(dos4g);
    xx_mem_free(dos4g);
}

bool xx_dos4g_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    return xx_dos16m_check_variant(self, pd, XX_DOS16M_VARIANT_DOS4G);
}

bool xx_dos4g_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    return xx_dos16m_handle_base_info_common(self, pd, XX_DOS16M_VARIANT_DOS4G);
}

int64_t xx_dos4g_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return xx_dos16m_get_format_size_common(self, pd, XX_DOS16M_VARIANT_DOS4G);
}

bool xx_dos4g_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode,
                             xx_memory_map *output, xx_pd_struct *pd) {
    return xx_dos16m_get_memory_map_common(self, mode, output, pd);
}

/* ------------------------------------------------------------------ */
/* Getters                                                             */
/* ------------------------------------------------------------------ */

uint16_t xx_dos16m_get_e_magic(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->e_magic : 0;
}

uint16_t xx_dos16m_get_e_cblp(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->e_cblp : 0;
}

uint16_t xx_dos16m_get_e_cp(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->e_cp : 0;
}

uint16_t xx_dos16m_get_signature(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->signature : 0;
}

uint16_t xx_dos16m_get_last_page_bytes(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->last_page_bytes : 0;
}

uint16_t xx_dos16m_get_pages_in_file(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->pages_in_file : 0;
}

uint16_t xx_dos16m_get_reserved1(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->reserved1 : 0;
}

uint16_t xx_dos16m_get_reserved2(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->reserved2 : 0;
}

uint16_t xx_dos16m_get_min_alloc(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->min_alloc : 0;
}

uint16_t xx_dos16m_get_max_alloc(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->max_alloc : 0;
}

uint16_t xx_dos16m_get_stack_seg(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->stack_seg : 0;
}

uint16_t xx_dos16m_get_stack_ptr(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->stack_ptr : 0;
}

uint16_t xx_dos16m_get_first_reloc_sel(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->first_reloc_sel : 0;
}

uint16_t xx_dos16m_get_init_ip(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->init_ip : 0;
}

uint16_t xx_dos16m_get_code_seg(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->code_seg : 0;
}

uint16_t xx_dos16m_get_runtime_gdt_size(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->runtime_gdt_size : 0;
}

uint16_t xx_dos16m_get_MAKEPM_version(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->MAKEPM_version : 0;
}

uint32_t xx_dos16m_get_next_header_pos(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->next_header_pos : 0;
}

uint32_t xx_dos16m_get_cv_info_offset(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->cv_info_offset : 0;
}

uint16_t xx_dos16m_get_last_sel_used(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->last_sel_used : 0;
}

uint16_t xx_dos16m_get_pmem_alloc(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->pmem_alloc : 0;
}

uint16_t xx_dos16m_get_alloc_incr(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->alloc_incr : 0;
}

uint16_t xx_dos16m_get_options(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->options : 0;
}

uint16_t xx_dos16m_get_trans_stack_sel(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->trans_stack_sel : 0;
}

uint16_t xx_dos16m_get_exp_flags(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->exp_flags : 0;
}

uint16_t xx_dos16m_get_program_size(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->program_size : 0;
}

uint16_t xx_dos16m_get_gdtimage_size(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->gdtimage_size : 0;
}

uint16_t xx_dos16m_get_first_selector(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->first_selector : 0;
}

uint8_t xx_dos16m_get_default_mem_strategy(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->default_mem_strategy : 0;
}

uint16_t xx_dos16m_get_transfer_buffer_size(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->transfer_buffer_size : 0;
}

const char *xx_dos16m_get_EXP_path(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->EXP_path : NULL;
}

int64_t xx_dos16m_get_stub_size(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->stub_size : 0;
}

int64_t xx_dos16m_get_first_header_offset(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->first_header_offset : -1;
}

int64_t xx_dos16m_get_payload_offset(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->payload_offset : -1;
}

uint16_t xx_dos16m_get_payload_subsignature(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->payload_subsignature : 0;
}

uint32_t xx_dos16m_get_number_of_headers(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->number_of_headers : 0;
}

bool xx_dos16m_has_payload(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->has_payload : false;
}

xx_dos16m_variant_t xx_dos16m_get_variant(const xx_dos16m *dos16m) {
    return dos16m ? dos16m->variant : XX_DOS16M_VARIANT_NONE;
}

/* DOS/4G getters read the same shared state. */
uint16_t xx_dos4g_get_e_magic(const xx_dos4g *dos4g) {
    return xx_dos16m_get_e_magic(dos4g);
}

uint16_t xx_dos4g_get_e_cblp(const xx_dos4g *dos4g) {
    return xx_dos16m_get_e_cblp(dos4g);
}

uint16_t xx_dos4g_get_e_cp(const xx_dos4g *dos4g) {
    return xx_dos16m_get_e_cp(dos4g);
}

uint16_t xx_dos4g_get_signature(const xx_dos4g *dos4g) {
    return xx_dos16m_get_signature(dos4g);
}

uint16_t xx_dos4g_get_last_page_bytes(const xx_dos4g *dos4g) {
    return xx_dos16m_get_last_page_bytes(dos4g);
}

uint16_t xx_dos4g_get_pages_in_file(const xx_dos4g *dos4g) {
    return xx_dos16m_get_pages_in_file(dos4g);
}

uint16_t xx_dos4g_get_reserved1(const xx_dos4g *dos4g) {
    return xx_dos16m_get_reserved1(dos4g);
}

uint16_t xx_dos4g_get_reserved2(const xx_dos4g *dos4g) {
    return xx_dos16m_get_reserved2(dos4g);
}

uint16_t xx_dos4g_get_min_alloc(const xx_dos4g *dos4g) {
    return xx_dos16m_get_min_alloc(dos4g);
}

uint16_t xx_dos4g_get_max_alloc(const xx_dos4g *dos4g) {
    return xx_dos16m_get_max_alloc(dos4g);
}

uint16_t xx_dos4g_get_stack_seg(const xx_dos4g *dos4g) {
    return xx_dos16m_get_stack_seg(dos4g);
}

uint16_t xx_dos4g_get_stack_ptr(const xx_dos4g *dos4g) {
    return xx_dos16m_get_stack_ptr(dos4g);
}

uint16_t xx_dos4g_get_first_reloc_sel(const xx_dos4g *dos4g) {
    return xx_dos16m_get_first_reloc_sel(dos4g);
}

uint16_t xx_dos4g_get_init_ip(const xx_dos4g *dos4g) {
    return xx_dos16m_get_init_ip(dos4g);
}

uint16_t xx_dos4g_get_code_seg(const xx_dos4g *dos4g) {
    return xx_dos16m_get_code_seg(dos4g);
}

uint16_t xx_dos4g_get_runtime_gdt_size(const xx_dos4g *dos4g) {
    return xx_dos16m_get_runtime_gdt_size(dos4g);
}

uint16_t xx_dos4g_get_MAKEPM_version(const xx_dos4g *dos4g) {
    return xx_dos16m_get_MAKEPM_version(dos4g);
}

uint32_t xx_dos4g_get_next_header_pos(const xx_dos4g *dos4g) {
    return xx_dos16m_get_next_header_pos(dos4g);
}

uint32_t xx_dos4g_get_cv_info_offset(const xx_dos4g *dos4g) {
    return xx_dos16m_get_cv_info_offset(dos4g);
}

uint16_t xx_dos4g_get_last_sel_used(const xx_dos4g *dos4g) {
    return xx_dos16m_get_last_sel_used(dos4g);
}

uint16_t xx_dos4g_get_pmem_alloc(const xx_dos4g *dos4g) {
    return xx_dos16m_get_pmem_alloc(dos4g);
}

uint16_t xx_dos4g_get_alloc_incr(const xx_dos4g *dos4g) {
    return xx_dos16m_get_alloc_incr(dos4g);
}

uint16_t xx_dos4g_get_options(const xx_dos4g *dos4g) {
    return xx_dos16m_get_options(dos4g);
}

uint16_t xx_dos4g_get_trans_stack_sel(const xx_dos4g *dos4g) {
    return xx_dos16m_get_trans_stack_sel(dos4g);
}

uint16_t xx_dos4g_get_exp_flags(const xx_dos4g *dos4g) {
    return xx_dos16m_get_exp_flags(dos4g);
}

uint16_t xx_dos4g_get_program_size(const xx_dos4g *dos4g) {
    return xx_dos16m_get_program_size(dos4g);
}

uint16_t xx_dos4g_get_gdtimage_size(const xx_dos4g *dos4g) {
    return xx_dos16m_get_gdtimage_size(dos4g);
}

uint16_t xx_dos4g_get_first_selector(const xx_dos4g *dos4g) {
    return xx_dos16m_get_first_selector(dos4g);
}

uint8_t xx_dos4g_get_default_mem_strategy(const xx_dos4g *dos4g) {
    return xx_dos16m_get_default_mem_strategy(dos4g);
}

uint16_t xx_dos4g_get_transfer_buffer_size(const xx_dos4g *dos4g) {
    return xx_dos16m_get_transfer_buffer_size(dos4g);
}

const char *xx_dos4g_get_EXP_path(const xx_dos4g *dos4g) {
    return xx_dos16m_get_EXP_path(dos4g);
}

int64_t xx_dos4g_get_stub_size(const xx_dos4g *dos4g) {
    return xx_dos16m_get_stub_size(dos4g);
}

int64_t xx_dos4g_get_first_header_offset(const xx_dos4g *dos4g) {
    return xx_dos16m_get_first_header_offset(dos4g);
}

int64_t xx_dos4g_get_payload_offset(const xx_dos4g *dos4g) {
    return xx_dos16m_get_payload_offset(dos4g);
}

uint16_t xx_dos4g_get_payload_subsignature(const xx_dos4g *dos4g) {
    return xx_dos16m_get_payload_subsignature(dos4g);
}

uint32_t xx_dos4g_get_number_of_headers(const xx_dos4g *dos4g) {
    return xx_dos16m_get_number_of_headers(dos4g);
}

bool xx_dos4g_has_payload(const xx_dos4g *dos4g) {
    return xx_dos16m_has_payload(dos4g);
}

xx_dos16m_variant_t xx_dos4g_get_variant(const xx_dos4g *dos4g) {
    return xx_dos16m_get_variant(dos4g);
}
