/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/elf/xx_elf.h"

#include "xx_elf_data.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <limits.h>

#define XX_ELF_IDENT_SIZE 16U
#define XX_ELF_HEADER32_SIZE 52U
#define XX_ELF_HEADER64_SIZE 64U
#define XX_ELF_PROGRAM32_SIZE 32U
#define XX_ELF_PROGRAM64_SIZE 56U
#define XX_ELF_SECTION32_SIZE 40U
#define XX_ELF_SECTION64_SIZE 64U
#define XX_ELF_PROGRAM_FLAG_EXECUTE UINT32_C(1)
#define XX_ELF_SECTION_FLAG_EXECUTE UINT64_C(4)
#define XX_ELF_SECTION_NAME_NONE UINT16_C(0)
#define XX_ELF_SECTION_NAME_EXTENDED UINT16_C(0xffff)
#define XX_ELF_PROGRAM_COUNT_EXTENDED UINT16_C(0xffff)

typedef struct xx_elf_parsed {
    uint8_t elf_class;
    uint8_t data_encoding;
    uint8_t ident_version;
    uint8_t os_abi;
    uint8_t abi_version;
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry_point;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint64_t program_header_count;
    uint16_t section_header_entry_size;
    uint64_t section_header_count;
    uint64_t section_name_index;
    xx_elf_program_header *program_headers;
    xx_elf_section_header *section_headers;
    int64_t format_size;
} xx_elf_parsed;

static void xx_elf_vtable_destroy(Abstractformat *format);

static bool xx_elf_range_is_valid(int64_t available, uint64_t offset,
                                  uint64_t size) {
    return available >= 0 && offset <= (uint64_t)available &&
           size <= (uint64_t)available - offset;
}

static bool xx_elf_table_is_valid(int64_t available, uint64_t offset,
                                  uint16_t entry_size, uint64_t count) {
    uint64_t size;
    if (count == 0U) return true;
    if (entry_size == 0U || count > UINT64_MAX / entry_size) return false;
    size = (uint64_t)entry_size * count;
    return xx_elf_range_is_valid(available, offset, size);
}

static bool xx_elf_product_is_valid(uint64_t left, uint64_t right,
                                    uint64_t *result) {
    if (!result || (right != 0U && left > UINT64_MAX / right)) return false;
    *result = left * right;
    return true;
}

static bool xx_elf_absolute_offset(int64_t base, uint64_t relative,
                                   int64_t *result) {
    if (!result || base < 0 || relative > (uint64_t)(INT64_MAX - base))
        return false;
    *result = base + (int64_t)relative;
    return true;
}

static bool xx_elf_relocate_address(uint64_t address, uint64_t preferred,
                                    uint64_t module, uint64_t *result) {
    uint64_t relative;
    if (!result || address < preferred || module == XX_INVALID_ADDRESS)
        return false;
    relative = address - preferred;
    if (relative >= XX_INVALID_ADDRESS - module) return false;
    *result = module + relative;
    return *result != XX_INVALID_ADDRESS;
}

static void xx_elf_parsed_cleanup(xx_elf_parsed *parsed) {
    if (!parsed) return;
    if (parsed->program_headers) xx_mem_free(parsed->program_headers);
    if (parsed->section_headers) xx_mem_free(parsed->section_headers);
    xx_mem_zero(parsed, sizeof(*parsed));
}

static bool xx_elf_read_section_header(Abstractformat *format,
                                       uint8_t elf_class,
                                       bool big_endian,
                                       uint64_t relative,
                                       xx_elf_section_header *section) {
    int64_t offset;
    if (!format || !section ||
        !xx_elf_absolute_offset(format->base_address, relative, &offset)) {
        return false;
    }
    xx_mem_zero(section, sizeof(*section));
    section->name_offset = xx_io_get_u32(format->device, offset,
                                         big_endian);
    section->type = xx_io_get_u32(format->device, offset + 4,
                                  big_endian);
    if (elf_class == XX_ELF_CLASS_64) {
        section->flags = xx_io_get_u64(format->device, offset + 8,
                                       big_endian);
        section->address = xx_io_get_u64(format->device, offset + 16,
                                         big_endian);
        section->offset = xx_io_get_u64(format->device, offset + 24,
                                        big_endian);
        section->size = xx_io_get_u64(format->device, offset + 32,
                                      big_endian);
        section->link = xx_io_get_u32(format->device, offset + 40,
                                      big_endian);
        section->info = xx_io_get_u32(format->device, offset + 44,
                                      big_endian);
        section->address_alignment = xx_io_get_u64(
            format->device, offset + 48, big_endian);
        section->entry_size = xx_io_get_u64(format->device, offset + 56,
                                            big_endian);
    } else {
        section->flags = xx_io_get_u32(format->device, offset + 8,
                                       big_endian);
        section->address = xx_io_get_u32(format->device, offset + 12,
                                         big_endian);
        section->offset = xx_io_get_u32(format->device, offset + 16,
                                        big_endian);
        section->size = xx_io_get_u32(format->device, offset + 20,
                                      big_endian);
        section->link = xx_io_get_u32(format->device, offset + 24,
                                      big_endian);
        section->info = xx_io_get_u32(format->device, offset + 28,
                                      big_endian);
        section->address_alignment = xx_io_get_u32(
            format->device, offset + 32, big_endian);
        section->entry_size = xx_io_get_u32(format->device, offset + 36,
                                            big_endian);
    }
    return true;
}

static bool xx_elf_parse_program_headers(Abstractformat *format,
                                         xx_elf_parsed *parsed,
                                         int64_t available,
                                         bool big_endian,
                                         uint64_t *extent,
                                         xx_pd_struct *pd) {
    uint64_t index;
    uint16_t canonical_size = parsed->elf_class == XX_ELF_CLASS_64
                                  ? XX_ELF_PROGRAM64_SIZE
                                  : XX_ELF_PROGRAM32_SIZE;
    if (parsed->program_header_count == 0U) return true;
    if (parsed->program_header_entry_size < canonical_size ||
        !xx_elf_table_is_valid(available, parsed->program_header_offset,
                               parsed->program_header_entry_size,
                               parsed->program_header_count)) {
        return false;
    }
    if (parsed->program_header_count >
        (uint64_t)(SIZE_MAX / sizeof(*parsed->program_headers))) {
        return false;
    }
    parsed->program_headers = (xx_elf_program_header *)xx_mem_alloc(
        (size_t)parsed->program_header_count *
        sizeof(*parsed->program_headers));
    if (!parsed->program_headers) return false;
    xx_mem_zero(parsed->program_headers,
                (size_t)parsed->program_header_count *
                    sizeof(*parsed->program_headers));
    for (index = 0U; index < parsed->program_header_count; ++index) {
        xx_elf_program_header *program = &parsed->program_headers[index];
        uint64_t relative = parsed->program_header_offset +
                            (uint64_t)index *
                                parsed->program_header_entry_size;
        int64_t offset;
        uint64_t end;
        if (xx_pd_is_stopped(pd) ||
            !xx_elf_absolute_offset(format->base_address, relative, &offset))
            return false;
        if (parsed->elf_class == XX_ELF_CLASS_64) {
            program->type = xx_io_get_u32(format->device, offset, big_endian);
            program->flags = xx_io_get_u32(format->device, offset + 4,
                                           big_endian);
            program->offset = xx_io_get_u64(format->device, offset + 8,
                                            big_endian);
            program->virtual_address = xx_io_get_u64(
                format->device, offset + 16, big_endian);
            program->physical_address = xx_io_get_u64(
                format->device, offset + 24, big_endian);
            program->file_size = xx_io_get_u64(format->device, offset + 32,
                                               big_endian);
            program->memory_size = xx_io_get_u64(format->device, offset + 40,
                                                 big_endian);
            program->alignment = xx_io_get_u64(format->device, offset + 48,
                                               big_endian);
        } else {
            program->type = xx_io_get_u32(format->device, offset, big_endian);
            program->offset = xx_io_get_u32(format->device, offset + 4,
                                            big_endian);
            program->virtual_address = xx_io_get_u32(
                format->device, offset + 8, big_endian);
            program->physical_address = xx_io_get_u32(
                format->device, offset + 12, big_endian);
            program->file_size = xx_io_get_u32(format->device, offset + 16,
                                               big_endian);
            program->memory_size = xx_io_get_u32(format->device, offset + 20,
                                                 big_endian);
            program->flags = xx_io_get_u32(format->device, offset + 24,
                                           big_endian);
            program->alignment = xx_io_get_u32(format->device, offset + 28,
                                               big_endian);
        }
        if (program->offset > (uint64_t)available)
            return false;
        /* A UPX-packed ELF rounds its first PT_LOAD's p_filesz up to a page,
         * so it can name a few bytes past the physical end of file; the kernel
         * simply zero-fills the tail of the final page. Clamp p_filesz to what
         * the file actually holds instead of rejecting the image - no consumer
         * can read past EOF anyway, so this only narrows an impossible read. */
        if (program->file_size > (uint64_t)available - program->offset)
            program->file_size = (uint64_t)available - program->offset;
        if ((program->type == XX_ELF_PROGRAM_LOAD &&
             (program->memory_size < program->file_size ||
              program->memory_size > INT64_MAX ||
              (program->memory_size > 0U &&
               program->virtual_address >
                   XX_INVALID_ADDRESS - program->memory_size)))) {
            return false;
        }
        end = program->offset + program->file_size;
        if (end > *extent) *extent = end;
    }
    return true;
}

static bool xx_elf_parse_section_headers(Abstractformat *format,
                                         xx_elf_parsed *parsed,
                                         int64_t available,
                                         bool big_endian,
                                         uint64_t *extent,
                                         xx_pd_struct *pd) {
    uint64_t index;
    uint16_t canonical_size = parsed->elf_class == XX_ELF_CLASS_64
                                  ? XX_ELF_SECTION64_SIZE
                                  : XX_ELF_SECTION32_SIZE;
    if (parsed->section_header_count == 0U) {
        return parsed->section_name_index == XX_ELF_SECTION_NAME_NONE;
    }
    if (parsed->section_header_entry_size < canonical_size ||
        !xx_elf_table_is_valid(available, parsed->section_header_offset,
                               parsed->section_header_entry_size,
                               parsed->section_header_count) ||
        (parsed->section_name_index != XX_ELF_SECTION_NAME_NONE &&
         parsed->section_name_index >= parsed->section_header_count)) {
        return false;
    }
    if (parsed->section_header_count >
        (uint64_t)(SIZE_MAX / sizeof(*parsed->section_headers))) {
        return false;
    }
    parsed->section_headers = (xx_elf_section_header *)xx_mem_alloc(
        (size_t)parsed->section_header_count *
        sizeof(*parsed->section_headers));
    if (!parsed->section_headers) return false;
    xx_mem_zero(parsed->section_headers,
                (size_t)parsed->section_header_count *
                    sizeof(*parsed->section_headers));
    for (index = 0U; index < parsed->section_header_count; ++index) {
        xx_elf_section_header *section = &parsed->section_headers[index];
        uint64_t relative = parsed->section_header_offset +
                            (uint64_t)index *
                                parsed->section_header_entry_size;
        uint64_t end;
        if (xx_pd_is_stopped(pd) ||
            !xx_elf_read_section_header(format, parsed->elf_class,
                                        big_endian, relative, section))
            return false;
        if (section->size > INT64_MAX ||
            (((section->flags & XX_ELF_SECTION_FLAG_ALLOC) != 0U) &&
             section->size > 0U &&
             section->address > XX_INVALID_ADDRESS - section->size)) {
            return false;
        }
        if (section->type != XX_ELF_SECTION_NOBITS) {
            if (!xx_elf_range_is_valid(available, section->offset,
                                       section->size)) {
                return false;
            }
            end = section->offset + section->size;
            if (end > *extent) *extent = end;
        }
    }
    return true;
}

static bool xx_elf_parse(Abstractformat *format, xx_elf_parsed *parsed,
                         xx_pd_struct *pd) {
    int64_t total;
    int64_t available;
    int64_t base;
    uint16_t canonical_header;
    uint16_t canonical_section;
    uint16_t raw_program_header_count;
    uint16_t raw_section_header_count;
    uint16_t raw_section_name_index;
    uint64_t extent;
    bool big_endian;
    if (!format || !format->device || !parsed || format->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }
    xx_mem_zero(parsed, sizeof(*parsed));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = total - format->base_address;
    if (available < XX_ELF_IDENT_SIZE) return false;
    base = format->base_address;
    if (xx_io_get_u8(format->device, base) != UINT8_C(0x7f) ||
        xx_io_get_u8(format->device, base + 1) != (uint8_t)'E' ||
        xx_io_get_u8(format->device, base + 2) != (uint8_t)'L' ||
        xx_io_get_u8(format->device, base + 3) != (uint8_t)'F') {
        return false;
    }
    parsed->elf_class = xx_io_get_u8(format->device, base + 4);
    parsed->data_encoding = xx_io_get_u8(format->device, base + 5);
    parsed->ident_version = xx_io_get_u8(format->device, base + 6);
    parsed->os_abi = xx_io_get_u8(format->device, base + 7);
    parsed->abi_version = xx_io_get_u8(format->device, base + 8);
    if ((parsed->elf_class != XX_ELF_CLASS_32 &&
         parsed->elf_class != XX_ELF_CLASS_64) ||
        (parsed->data_encoding != XX_ELF_DATA_LSB &&
         parsed->data_encoding != XX_ELF_DATA_MSB) ||
        parsed->ident_version != 1U) {
        return false;
    }
    canonical_header = parsed->elf_class == XX_ELF_CLASS_64
                           ? XX_ELF_HEADER64_SIZE : XX_ELF_HEADER32_SIZE;
    canonical_section = parsed->elf_class == XX_ELF_CLASS_64
                            ? XX_ELF_SECTION64_SIZE : XX_ELF_SECTION32_SIZE;
    if (available < canonical_header) return false;
    big_endian = parsed->data_encoding == XX_ELF_DATA_MSB;
    parsed->type = xx_io_get_u16(format->device, base + 16, big_endian);
    parsed->machine = xx_io_get_u16(format->device, base + 18, big_endian);
    parsed->version = xx_io_get_u32(format->device, base + 20, big_endian);
    if (parsed->version != 1U) return false;
    if (parsed->elf_class == XX_ELF_CLASS_64) {
        parsed->entry_point = xx_io_get_u64(format->device, base + 24,
                                            big_endian);
        parsed->program_header_offset = xx_io_get_u64(
            format->device, base + 32, big_endian);
        parsed->section_header_offset = xx_io_get_u64(
            format->device, base + 40, big_endian);
        parsed->flags = xx_io_get_u32(format->device, base + 48, big_endian);
        parsed->header_size = xx_io_get_u16(format->device, base + 52,
                                            big_endian);
        parsed->program_header_entry_size = xx_io_get_u16(
            format->device, base + 54, big_endian);
        raw_program_header_count = xx_io_get_u16(
            format->device, base + 56, big_endian);
        parsed->section_header_entry_size = xx_io_get_u16(
            format->device, base + 58, big_endian);
        raw_section_header_count = xx_io_get_u16(
            format->device, base + 60, big_endian);
        raw_section_name_index = xx_io_get_u16(
            format->device, base + 62, big_endian);
    } else {
        parsed->entry_point = xx_io_get_u32(format->device, base + 24,
                                            big_endian);
        parsed->program_header_offset = xx_io_get_u32(
            format->device, base + 28, big_endian);
        parsed->section_header_offset = xx_io_get_u32(
            format->device, base + 32, big_endian);
        parsed->flags = xx_io_get_u32(format->device, base + 36, big_endian);
        parsed->header_size = xx_io_get_u16(format->device, base + 40,
                                            big_endian);
        parsed->program_header_entry_size = xx_io_get_u16(
            format->device, base + 42, big_endian);
        raw_program_header_count = xx_io_get_u16(
            format->device, base + 44, big_endian);
        parsed->section_header_entry_size = xx_io_get_u16(
            format->device, base + 46, big_endian);
        raw_section_header_count = xx_io_get_u16(
            format->device, base + 48, big_endian);
        raw_section_name_index = xx_io_get_u16(
            format->device, base + 50, big_endian);
    }
    if (parsed->header_size < canonical_header ||
        parsed->header_size > (uint64_t)available) {
        return false;
    }
    parsed->program_header_count = raw_program_header_count;
    parsed->section_header_count = raw_section_header_count;
    parsed->section_name_index = raw_section_name_index;
    if (parsed->section_header_offset == 0U) {
        if (raw_section_header_count != 0U ||
            raw_section_name_index != XX_ELF_SECTION_NAME_NONE ||
            raw_program_header_count == XX_ELF_PROGRAM_COUNT_EXTENDED) {
            return false;
        }
        parsed->section_header_count = 0U;
        parsed->section_name_index = XX_ELF_SECTION_NAME_NONE;
    } else {
        xx_elf_section_header section_zero;
        if (parsed->section_header_entry_size < canonical_section ||
            !xx_elf_range_is_valid(available,
                                   parsed->section_header_offset,
                                   parsed->section_header_entry_size) ||
            !xx_elf_read_section_header(format, parsed->elf_class,
                                        big_endian,
                                        parsed->section_header_offset,
                                        &section_zero)) {
            return false;
        }
        if (raw_section_header_count == 0U)
            parsed->section_header_count = section_zero.size;
        if (raw_program_header_count == XX_ELF_PROGRAM_COUNT_EXTENDED)
            parsed->program_header_count = section_zero.info;
        if (raw_section_name_index == XX_ELF_SECTION_NAME_EXTENDED)
            parsed->section_name_index = section_zero.link;
        if (parsed->section_header_count == 0U) return false;
    }
    if ((parsed->program_header_count != 0U &&
         parsed->program_header_offset == 0U) ||
        (parsed->section_header_count != 0U &&
         parsed->section_header_offset == 0U)) {
        return false;
    }
    extent = parsed->header_size;
    if (parsed->program_header_count != 0U) {
        uint64_t table_size;
        uint64_t table_end;
        if (!xx_elf_product_is_valid(parsed->program_header_count,
                                     parsed->program_header_entry_size,
                                     &table_size) ||
            !xx_elf_range_is_valid(available,
                                   parsed->program_header_offset,
                                   table_size)) {
            return false;
        }
        table_end = parsed->program_header_offset + table_size;
        if (table_end > extent) extent = table_end;
    }
    if (!xx_elf_parse_program_headers(format, parsed, available, big_endian,
                                      &extent, pd)) {
        xx_elf_parsed_cleanup(parsed);
        return false;
    }
    if (parsed->section_header_count != 0U) {
        uint64_t table_size;
        uint64_t table_end;
        if (!xx_elf_product_is_valid(parsed->section_header_count,
                                     parsed->section_header_entry_size,
                                     &table_size) ||
            !xx_elf_range_is_valid(available,
                                   parsed->section_header_offset,
                                   table_size)) {
            xx_elf_parsed_cleanup(parsed);
            return false;
        }
        table_end = parsed->section_header_offset + table_size;
        if (table_end > extent) extent = table_end;
    }
    if (!xx_elf_parse_section_headers(format, parsed, available, big_endian,
                                      &extent, pd) ||
        extent > (uint64_t)available || extent > INT64_MAX) {
        xx_elf_parsed_cleanup(parsed);
        return false;
    }
    parsed->format_size = (int64_t)extent;
    return true;
}

static xx_arch_t xx_elf_machine_to_arch(uint16_t machine, bool is_64) {
    switch (machine) {
        case 3U: return XX_ARCH_X86;
        case 62U: return XX_ARCH_X86_64;
        case 40U: return XX_ARCH_ARM;
        case 183U: return XX_ARCH_ARM64;
        case 8U:
        case 10U: return is_64 ? XX_ARCH_MIPS64 : XX_ARCH_MIPS;
        case 20U: return XX_ARCH_PPC;
        case 21U: return XX_ARCH_PPC64;
        case 243U: return is_64 ? XX_ARCH_RISCV64 : XX_ARCH_RISCV;
        case 2U: return XX_ARCH_SPARC;
        case 43U: return XX_ARCH_SPARC64;
        case 4U: return XX_ARCH_M68K;
        case 83U: return XX_ARCH_AVR;
        case 42U: return XX_ARCH_SH;
        default: return XX_ARCH_UNKNOWN;
    }
}

static xx_os_t xx_elf_abi_to_os(uint8_t os_abi) {
    switch (os_abi) {
        case 3U: return XX_OS_LINUX;
        case 9U: return XX_OS_FREEBSD;
        default: return XX_OS_UNIX;
    }
}

static bool xx_elf_has_program_type(const xx_elf_parsed *parsed,
                                    uint32_t type) {
    uint64_t index;
    if (!parsed) return false;
    for (index = 0U; index < parsed->program_header_count; ++index) {
        if (parsed->program_headers[index].type == type) return true;
    }
    return false;
}

static xx_format_type_t xx_elf_classify(const xx_elf_parsed *parsed) {
    if (!parsed) return XX_TYPE_UNKNOWN;
    switch (parsed->type) {
        case XX_ELF_TYPE_REL: return XX_TYPE_OBJECT;
        case XX_ELF_TYPE_EXEC: return XX_TYPE_CONSOLE_APPLICATION;
        case XX_ELF_TYPE_DYN:
            return xx_elf_has_program_type(parsed, XX_ELF_PROGRAM_INTERP)
                       ? XX_TYPE_CONSOLE_APPLICATION : XX_TYPE_LIBRARY;
        default: return XX_TYPE_UNKNOWN;
    }
}

void xx_elf_init(xx_elf *elf, xx_io_device *device, int64_t base_address) {
    if (!elf) return;
    xx_mem_zero(elf, sizeof(*elf));
    xx_format_init(&elf->format, device, base_address);
    elf->format.os = XX_OS_UNIX;
    xx_format_set_mime_type(&elf->format, "application/x-elf");
    xx_format_set_extension(&elf->format, "elf");
    elf->format.check_is_valid = xx_elf_check_is_valid;
    elf->format.handle_base_info = xx_elf_handle_base_info;
    elf->format.get_format_size = xx_elf_get_format_size;
    elf->format.get_memory_map = xx_elf_get_memory_map;
    xx_elf_setup_data_struct_callbacks(elf);
    elf->format.destroy = xx_elf_vtable_destroy;
}

xx_elf *xx_elf_create(xx_io_device *device, int64_t base_address) {
    xx_elf *elf = (xx_elf *)xx_mem_alloc(sizeof(*elf));
    if (elf) xx_elf_init(elf, device, base_address);
    return elf;
}

void xx_elf_destroy(xx_elf *elf) {
    if (!elf) return;
    if (elf->program_headers) {
        xx_mem_free(elf->program_headers);
        elf->program_headers = NULL;
    }
    if (elf->section_headers) {
        xx_mem_free(elf->section_headers);
        elf->section_headers = NULL;
    }
    xx_format_cleanup_extra_parameters(&elf->format);
}

static void xx_elf_vtable_destroy(Abstractformat *format) {
    xx_elf_destroy((xx_elf *)format);
}

void xx_elf_free(xx_elf *elf) {
    if (!elf) return;
    xx_elf_destroy(elf);
    xx_mem_free(elf);
}

bool xx_elf_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    xx_elf_parsed parsed;
    bool result = xx_elf_parse(format, &parsed, pd);
    if (result) xx_elf_parsed_cleanup(&parsed);
    return result;
}

bool xx_elf_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_elf_parsed parsed;
    xx_elf *elf;
    int64_t total;
    xx_format_type_t format_type;
    if (!format || !xx_elf_parse(format, &parsed, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
            xx_format_invalidate_memory_map(format);
        }
        return false;
    }
    elf = (xx_elf *)format;
    format_type = xx_elf_classify(&parsed);
    xx_format_invalidate_memory_map(format);
    if (elf->program_headers) xx_mem_free(elf->program_headers);
    if (elf->section_headers) xx_mem_free(elf->section_headers);
    elf->elf_class = parsed.elf_class;
    elf->data_encoding = parsed.data_encoding;
    elf->ident_version = parsed.ident_version;
    elf->os_abi = parsed.os_abi;
    elf->abi_version = parsed.abi_version;
    elf->type = parsed.type;
    elf->machine = parsed.machine;
    elf->version = parsed.version;
    elf->entry_point = parsed.entry_point;
    elf->program_header_offset = parsed.program_header_offset;
    elf->section_header_offset = parsed.section_header_offset;
    elf->flags = parsed.flags;
    elf->header_size = parsed.header_size;
    elf->program_header_entry_size = parsed.program_header_entry_size;
    elf->program_header_count = parsed.program_header_count;
    elf->section_header_entry_size = parsed.section_header_entry_size;
    elf->section_header_count = parsed.section_header_count;
    elf->section_name_index = parsed.section_name_index;
    elf->program_headers = parsed.program_headers;
    elf->section_headers = parsed.section_headers;
    parsed.program_headers = NULL;
    parsed.section_headers = NULL;
    format->file_type = elf->elf_class == XX_ELF_CLASS_64
                            ? XX_FILE_TYPE_ELF64 : XX_FILE_TYPE_ELF32;
    format->endian = elf->data_encoding == XX_ELF_DATA_MSB
                         ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    format->os = xx_elf_abi_to_os(elf->os_abi);
    format->arch = xx_elf_machine_to_arch(elf->machine,
                                         elf->elf_class == XX_ELF_CLASS_64);
    format->format_type = format_type;
    format->is_executable = elf->type == XX_ELF_TYPE_EXEC ||
                            elf->type == XX_ELF_TYPE_DYN;
    if (elf->type == XX_ELF_TYPE_DYN)
        xx_format_set_extension(format, "so");
    else if (elf->type == XX_ELF_TYPE_REL)
        xx_format_set_extension(format, "o");
    else
        xx_format_set_extension(format, "elf");
    format->format_size = parsed.format_size;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) {
        xx_elf_parsed_cleanup(&parsed);
        return false;
    }
    if (format->format_size < total - format->base_address) {
        format->overlay_offset = format->base_address + format->format_size;
        format->overlay_size = total - format->base_address -
                               format->format_size;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    format->is_valid = true;
    format->base_info_handled = true;
    xx_elf_parsed_cleanup(&parsed);
    return true;
}

int64_t xx_elf_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_elf_handle_base_info(format, pd))
               ? format->format_size : -1;
}

static void xx_elf_get_section_name(const xx_elf *elf,
                                    const xx_elf_section_header *section,
                                    uint64_t section_index,
                                    char name[XX_MEMORY_RECORD_NAME_SIZE]) {
    const xx_elf_section_header *names;
    uint64_t index;
    size_t length = 0U;
    int64_t offset;
    name[0] = '\0';
    if (elf && section && elf->section_headers &&
        elf->section_name_index != XX_ELF_SECTION_NAME_NONE &&
        elf->section_name_index < elf->section_header_count) {
        names = &elf->section_headers[elf->section_name_index];
        if (names->type != XX_ELF_SECTION_NOBITS &&
            section->name_offset < names->size) {
            index = section->name_offset;
            if (xx_elf_absolute_offset(elf->format.base_address,
                                       names->offset + index, &offset)) {
                while (index < names->size &&
                       length + 1U < XX_MEMORY_RECORD_NAME_SIZE) {
                    uint8_t value = xx_io_get_u8(elf->format.device,
                                                 offset + (int64_t)length);
                    if (value == 0U) break;
                    name[length++] = (char)value;
                    ++index;
                }
                name[length] = '\0';
            }
        }
    }
    if (name[0] == '\0') {
        (void)xx_rt_snprintf(name, XX_MEMORY_RECORD_NAME_SIZE,
                             "Section(%llu)",
                             (unsigned long long)section_index);
    }
}

static bool xx_elf_map_segments(xx_elf *elf, xx_memory_map *output,
                                uint64_t module_address,
                                uint64_t preferred_address,
                                xx_pd_struct *pd) {
    uint64_t index;
    for (index = 0U; index < elf->program_header_count; ++index) {
        const xx_elf_program_header *program = &elf->program_headers[index];
        uint64_t address;
        int64_t offset;
        char name[XX_MEMORY_RECORD_NAME_SIZE];
        if (program->type != XX_ELF_PROGRAM_LOAD) continue;
        if (xx_pd_is_stopped(pd) || program->file_size > INT64_MAX ||
            program->memory_size > INT64_MAX ||
            !xx_elf_absolute_offset(elf->format.base_address,
                                    program->offset, &offset) ||
            !xx_elf_relocate_address(program->virtual_address,
                                     preferred_address, module_address,
                                     &address)) {
            return false;
        }
        (void)xx_rt_snprintf(name, sizeof(name), "PT_LOAD(%llu)",
                             (unsigned long long)index);
        if (!xx_memory_map_add_part(
                output, offset, (int64_t)program->file_size, address,
                (int64_t)program->memory_size, XX_FILE_PART_SEGMENT,
                index <= INT32_MAX ? (int32_t)index : -1, name, false)) {
            return false;
        }
        if (program->file_size > 0U &&
            (output->start_load_offset < 0 ||
             offset < output->start_load_offset)) {
            output->start_load_offset = offset;
        }
        if ((program->flags & XX_ELF_PROGRAM_FLAG_EXECUTE) != 0U &&
            output->code_base < 0 && address <= INT64_MAX) {
            output->code_base = (int64_t)address;
        }
    }
    return true;
}

static bool xx_elf_map_sections(xx_elf *elf, xx_memory_map *output,
                                uint64_t module_address,
                                uint64_t preferred_address,
                                xx_pd_struct *pd) {
    uint64_t index;
    for (index = 0U; index < elf->section_header_count; ++index) {
        const xx_elf_section_header *section = &elf->section_headers[index];
        bool allocated =
            (section->flags & XX_ELF_SECTION_FLAG_ALLOC) != 0U;
        bool no_bits = section->type == XX_ELF_SECTION_NOBITS;
        uint64_t address = XX_INVALID_ADDRESS;
        int64_t offset = -1;
        int64_t file_size = no_bits ? 0 : (int64_t)section->size;
        int64_t virtual_size = (int64_t)section->size;
        char name[XX_MEMORY_RECORD_NAME_SIZE];
        if (section->size == 0U) continue;
        if (xx_pd_is_stopped(pd)) return false;
        if (!no_bits &&
            !xx_elf_absolute_offset(elf->format.base_address,
                                    section->offset, &offset)) {
            return false;
        }
        if (allocated &&
            !xx_elf_relocate_address(section->address, preferred_address,
                                     module_address, &address)) {
            return false;
        }
        xx_elf_get_section_name(elf, section, index, name);
        if (!xx_memory_map_add_part(
                output, offset, file_size, address, virtual_size,
                XX_FILE_PART_SECTION,
                index <= INT32_MAX ? (int32_t)index : -1,
                name, false)) {
            return false;
        }
        if (file_size > 0 &&
            (output->start_load_offset < 0 ||
             offset < output->start_load_offset)) {
            output->start_load_offset = offset;
        }
        if (allocated &&
            (section->flags & XX_ELF_SECTION_FLAG_EXECUTE) != 0U &&
            output->code_base < 0 && address <= INT64_MAX) {
            output->code_base = (int64_t)address;
        }
    }
    return true;
}

bool xx_elf_get_memory_map(Abstractformat *format,
                           xx_memory_map_mode_t mode,
                           xx_memory_map *output, xx_pd_struct *pd) {
    xx_elf *elf = (xx_elf *)format;
    int64_t total;
    int64_t binary_size;
    uint64_t preferred = XX_INVALID_ADDRESS;
    uint64_t module;
    uint64_t entry;
    uint64_t index;
    bool success;
    if (!format || !output || !format->device ||
        !format->base_info_handled || format->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN)
        mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    if (mode != XX_MEMORY_MAP_MODE_SEGMENTS &&
        mode != XX_MEMORY_MAP_MODE_SECTIONS) {
        return false;
    }
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    binary_size = total - format->base_address;
    if (mode == XX_MEMORY_MAP_MODE_SEGMENTS) {
        for (index = 0U; index < elf->program_header_count; ++index) {
            const xx_elf_program_header *program =
                &elf->program_headers[index];
            if (program->type == XX_ELF_PROGRAM_LOAD &&
                program->memory_size > 0U &&
                (preferred == XX_INVALID_ADDRESS ||
                 program->virtual_address < preferred)) {
                preferred = program->virtual_address;
            }
        }
    } else {
        for (index = 0U; index < elf->section_header_count; ++index) {
            const xx_elf_section_header *section =
                &elf->section_headers[index];
            if ((section->flags & XX_ELF_SECTION_FLAG_ALLOC) != 0U &&
                section->size > 0U &&
                (preferred == XX_INVALID_ADDRESS ||
                 section->address < preferred)) {
                preferred = section->address;
            }
        }
    }
    if (preferred == XX_INVALID_ADDRESS) preferred = 0U;
    module = format->module_address != XX_INVALID_ADDRESS
                 ? format->module_address : preferred;
    output->binary_offset = format->base_address;
    output->module_address = module;
    output->is_image = format->is_mapped;
    output->binary_size = binary_size;
    output->entry_point_address = XX_INVALID_ADDRESS;
    output->code_base = -1;
    output->start_load_offset = -1;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = mode;
    success = mode == XX_MEMORY_MAP_MODE_SEGMENTS
                  ? xx_elf_map_segments(elf, output, module, preferred, pd)
                  : xx_elf_map_sections(elf, output, module, preferred, pd);
    if (!success) return false;
    if (format->format_size < binary_size &&
        !xx_memory_map_add_part(
            output, format->base_address + format->format_size,
            binary_size - format->format_size, XX_INVALID_ADDRESS, 0,
            XX_FILE_PART_OVERLAY, -1, "Overlay", false)) {
        return false;
    }
    if (!xx_memory_map_finalize(output)) return false;
    if (xx_elf_relocate_address(elf->entry_point, preferred, module,
                                &entry) &&
        xx_memory_map_record_by_address(output, entry) != NULL) {
        output->entry_point_address = entry;
    }
    return !xx_pd_is_stopped(pd);
}

bool xx_elf_is_64(const xx_elf *elf) {
    return elf && elf->elf_class == XX_ELF_CLASS_64;
}

uint16_t xx_elf_get_machine(const xx_elf *elf) {
    return elf ? elf->machine : 0U;
}

uint16_t xx_elf_get_type(const xx_elf *elf) {
    return elf ? elf->type : 0U;
}

uint64_t xx_elf_get_entry_point(const xx_elf *elf) {
    return elf ? elf->entry_point : 0U;
}

uint64_t xx_elf_get_number_of_program_headers(const xx_elf *elf) {
    return elf ? elf->program_header_count : 0U;
}

uint64_t xx_elf_get_number_of_section_headers(const xx_elf *elf) {
    return elf ? elf->section_header_count : 0U;
}

const xx_elf_program_header *xx_elf_get_program_header(
    const xx_elf *elf, uint64_t index) {
    return elf && elf->program_headers && index < elf->program_header_count
               ? &elf->program_headers[index] : NULL;
}

const xx_elf_section_header *xx_elf_get_section_header(
    const xx_elf *elf, uint64_t index) {
    return elf && elf->section_headers && index < elf->section_header_count
               ? &elf->section_headers[index] : NULL;
}
