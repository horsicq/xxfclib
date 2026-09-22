/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/pe/xx_pe.h"

#include "xx_pe_data.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <limits.h>

#define XX_PE_DOS_HEADER_SIZE 64U
#define XX_PE_FILE_HEADER_SIZE 20U
#define XX_PE_SECTION_HEADER_SIZE 40U
#define XX_PE_MAX_SECTIONS 4096U
#define XX_PE_MAX_IMPORTS 65536U
#define XX_PE_MAX_RESOURCE_DEPTH 32U

#define XX_PE_DIRECTORY_EXPORT 0U
#define XX_PE_DIRECTORY_IMPORT 1U
#define XX_PE_DIRECTORY_RESOURCE 2U
#define XX_PE_DIRECTORY_SECURITY 4U

typedef struct xx_pe_parsed_s {
    uint32_t pe_offset;
    uint16_t machine;
    uint16_t section_count;
    uint32_t timestamp;
    uint16_t optional_size;
    uint16_t characteristics;
    uint16_t magic;
    uint32_t entry_rva;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint32_t image_size;
    uint32_t headers_size;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint32_t directory_count;
    uint32_t directory_rva[16];
    uint32_t directory_size[16];
    xx_pe_section *sections;
    int64_t format_size;
    int64_t overlay_offset;
    int64_t overlay_size;
} xx_pe_parsed;

static void xx_pe_parsed_cleanup(xx_pe_parsed *parsed) {
    if (!parsed) return;
    if (parsed->sections) xx_mem_free(parsed->sections);
    xx_mem_zero(parsed, sizeof(*parsed));
}

static int64_t xx_pe_relative_range_to_offset(
    const xx_memory_map *memory_map, uint64_t relative_address,
    uint64_t size) {
    uint64_t address;
    int64_t offset;
    if (!memory_map || size == 0U || relative_address > (uint64_t)INT64_MAX ||
        size > (uint64_t)INT64_MAX)
        return -1;
    address = xx_memory_map_relative_address_to_address(
        memory_map, (int64_t)relative_address);
    if (address == XX_INVALID_ADDRESS ||
        !xx_memory_map_is_physical_address_range(memory_map, address,
                                                  (int64_t)size))
        return -1;
    offset = xx_memory_map_relative_address_to_offset(
        memory_map, (int64_t)relative_address);
    return offset;
}

static uint64_t xx_pe_count_imports(const xx_pe *pe,
                                    const xx_memory_map *memory_map,
                                    xx_io_device *device, int64_t total) {
    uint32_t rva = pe->data_directory_rva[XX_PE_DIRECTORY_IMPORT];
    uint32_t size = pe->data_directory_size[XX_PE_DIRECTORY_IMPORT];
    uint64_t limit = size / 20U;
    uint64_t count = 0U;
    if (!rva || size < 20U) return 0U;
    if (limit > XX_PE_MAX_IMPORTS) limit = XX_PE_MAX_IMPORTS;
    while (count < limit) {
        uint64_t descriptor_rva = (uint64_t)rva + count * 20U;
        int64_t descriptor_offset;
        if (descriptor_rva > UINT32_MAX) break;
        descriptor_offset = xx_pe_relative_range_to_offset(
            memory_map, descriptor_rva, 20U);
        if (descriptor_offset < 0 || descriptor_offset > total ||
            total - descriptor_offset < 20)
            break;
        if (xx_io_get_u32(device, descriptor_offset, false) == 0U &&
            xx_io_get_u32(device, descriptor_offset + 4, false) == 0U &&
            xx_io_get_u32(device, descriptor_offset + 8, false) == 0U &&
            xx_io_get_u32(device, descriptor_offset + 12, false) == 0U &&
            xx_io_get_u32(device, descriptor_offset + 16, false) == 0U) break;
        ++count;
    }
    return count;
}

static uint64_t xx_pe_count_exports(const xx_pe *pe,
                                    const xx_memory_map *memory_map,
                                    xx_io_device *device, int64_t total) {
    uint32_t rva = pe->data_directory_rva[XX_PE_DIRECTORY_EXPORT];
    uint32_t size = pe->data_directory_size[XX_PE_DIRECTORY_EXPORT];
    int64_t offset;
    int64_t table_offset;
    uint32_t count;
    uint32_t table_rva;
    if (!rva || size < 40U) return 0U;
    offset = xx_pe_relative_range_to_offset(memory_map, rva, 40U);
    if (offset < 0 || offset > total || total - offset < 40) return 0U;
    count = xx_io_get_u32(device, offset + 20, false);
    table_rva = xx_io_get_u32(device, offset + 28, false);
    if (!count) return 0U;
    if (count > UINT32_C(1000000) || !table_rva) return 0U;
    table_offset = xx_pe_relative_range_to_offset(
        memory_map, table_rva, (uint64_t)count * 4U);
    if (table_offset < 0 || table_offset > total ||
        (uint64_t)count * 4U > (uint64_t)(total - table_offset))
        return 0U;
    return count;
}

static uint64_t xx_pe_count_resource_dir(const xx_memory_map *memory_map,
                                         xx_io_device *device, int64_t total,
                                         uint32_t root_rva,
                                         uint32_t dir_size, uint32_t relative,
                                         uint32_t depth, uint64_t *budget) {
    int64_t offset;
    uint32_t entries;
    uint32_t i;
    uint64_t result = 0U;
    if (!budget || !*budget || depth > XX_PE_MAX_RESOURCE_DEPTH ||
        relative > dir_size || dir_size - relative < 16U ||
        root_rva > UINT32_MAX - relative) return 0U;
    offset = xx_pe_relative_range_to_offset(
        memory_map, (uint64_t)root_rva + relative, 16U);
    if (offset < 0 || offset > total || total - offset < 16) return 0U;
    entries = (uint32_t)xx_io_get_u16(device, offset + 12, false) +
              xx_io_get_u16(device, offset + 14, false);
    if (entries > (dir_size - relative - 16U) / 8U) return 0U;
    for (i = 0U; i < entries && *budget; ++i) {
        int64_t entry_offset;
        uint32_t child;
        --*budget;
        entry_offset = xx_pe_relative_range_to_offset(
            memory_map, (uint64_t)root_rva + relative + 16U +
                            (uint64_t)i * 8U,
            8U);
        if (entry_offset < 0) break;
        if (entry_offset > total || total - entry_offset < 8) break;
        child = xx_io_get_u32(device, entry_offset + 4, false);
        if (child & UINT32_C(0x80000000)) {
            result += xx_pe_count_resource_dir(memory_map, device, total,
                                               root_rva, dir_size,
                                               child & UINT32_C(0x7fffffff),
                                               depth + 1U, budget);
        } else if (child <= dir_size && dir_size - child >= 16U &&
                   root_rva <= UINT32_MAX - child &&
                   xx_pe_relative_range_to_offset(
                       memory_map, (uint64_t)root_rva + child, 16U) >= 0) {
            ++result;
        }
    }
    return result;
}

static uint64_t xx_pe_count_resources(const xx_pe *pe,
                                      const xx_memory_map *memory_map,
                                      xx_io_device *device, int64_t total) {
    uint64_t budget = UINT64_C(1000000);
    uint32_t rva = pe->data_directory_rva[XX_PE_DIRECTORY_RESOURCE];
    uint32_t size = pe->data_directory_size[XX_PE_DIRECTORY_RESOURCE];
    if (!rva || size < 16U) return 0U;
    return xx_pe_count_resource_dir(memory_map, device, total, rva, size, 0U,
                                    0U, &budget);
}

static bool xx_pe_parse(Abstractformat *format, xx_pe_parsed *parsed,
                        xx_pd_struct *pd) {
    int64_t total;
    int64_t relative_size;
    int64_t nt_offset;
    int64_t optional_offset;
    int64_t section_offset;
    int64_t image_end;
    uint32_t directory_offset;
    uint32_t available_directories;
    uint16_t i;
    bool ok = false;
    if (!format || !format->device || !parsed || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_mem_zero(parsed, sizeof(*parsed));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    relative_size = total - format->base_address;
    if (relative_size < XX_PE_DOS_HEADER_SIZE ||
        xx_io_get_u16(format->device, format->base_address, false) !=
            UINT16_C(0x5a4d)) return false;
    parsed->pe_offset = xx_io_get_u32(format->device,
                                      format->base_address + 0x3c, false);
    if (parsed->pe_offset < XX_PE_DOS_HEADER_SIZE ||
        format->base_address > INT64_MAX - (int64_t)parsed->pe_offset ||
        (nt_offset = format->base_address + (int64_t)parsed->pe_offset) < 0 ||
        nt_offset > total || total - nt_offset < 24 ||
        xx_io_get_u32(format->device, nt_offset, false) != XX_PE_SIGNATURE)
        return false;
    parsed->machine = xx_io_get_u16(format->device, nt_offset + 4, false);
    parsed->section_count = xx_io_get_u16(format->device, nt_offset + 6, false);
    parsed->timestamp = xx_io_get_u32(format->device, nt_offset + 8, false);
    parsed->optional_size = xx_io_get_u16(format->device, nt_offset + 20, false);
    parsed->characteristics = xx_io_get_u16(format->device, nt_offset + 22, false);
    if (!parsed->section_count || parsed->section_count > XX_PE_MAX_SECTIONS ||
        parsed->optional_size < 2U) return false;
    optional_offset = nt_offset + 24;
    if (optional_offset > total || parsed->optional_size > total - optional_offset)
        return false;
    parsed->magic = xx_io_get_u16(format->device, optional_offset, false);
    if (parsed->magic == XX_PE_MAGIC_32) {
        if (parsed->optional_size < 96U) goto done;
        parsed->image_base = xx_io_get_u32(format->device,
                                           optional_offset + 28, false);
        directory_offset = 96U;
        parsed->directory_count = xx_io_get_u32(format->device,
                                                optional_offset + 92, false);
    } else if (parsed->magic == XX_PE_MAGIC_64) {
        if (parsed->optional_size < 112U) goto done;
        parsed->image_base = xx_io_get_u64(format->device,
                                           optional_offset + 24, false);
        directory_offset = 112U;
        parsed->directory_count = xx_io_get_u32(format->device,
                                                optional_offset + 108, false);
    } else {
        goto done;
    }
    parsed->entry_rva = xx_io_get_u32(format->device,
                                      optional_offset + 16, false);
    parsed->section_alignment = xx_io_get_u32(format->device,
                                              optional_offset + 32, false);
    parsed->file_alignment = xx_io_get_u32(format->device,
                                           optional_offset + 36, false);
    parsed->image_size = xx_io_get_u32(format->device,
                                       optional_offset + 56, false);
    parsed->headers_size = xx_io_get_u32(format->device,
                                         optional_offset + 60, false);
    parsed->subsystem = xx_io_get_u16(format->device,
                                      optional_offset + 68, false);
    parsed->dll_characteristics = xx_io_get_u16(format->device,
                                                optional_offset + 70, false);
    if (!parsed->headers_size || parsed->headers_size > (uint64_t)relative_size)
        goto done;
    available_directories = (parsed->optional_size - directory_offset) / 8U;
    if (parsed->directory_count > available_directories)
        parsed->directory_count = available_directories;
    if (parsed->directory_count > 16U) parsed->directory_count = 16U;
    for (i = 0U; i < parsed->directory_count; ++i) {
        int64_t entry_offset = optional_offset + directory_offset + i * 8U;
        parsed->directory_rva[i] = xx_io_get_u32(format->device,
                                                  entry_offset, false);
        parsed->directory_size[i] = xx_io_get_u32(format->device,
                                                   entry_offset + 4, false);
    }
    section_offset = optional_offset + parsed->optional_size;
    if (section_offset > total ||
        (uint64_t)parsed->section_count * XX_PE_SECTION_HEADER_SIZE >
            (uint64_t)(total - section_offset)) goto done;
    parsed->sections = (xx_pe_section *)xx_mem_calloc(parsed->section_count,
                                                       sizeof(*parsed->sections));
    if (!parsed->sections) goto done;
    image_end = parsed->headers_size;
    for (i = 0U; i < parsed->section_count; ++i) {
        xx_pe_section *section = &parsed->sections[i];
        int64_t entry_offset = section_offset +
                               (int64_t)i * XX_PE_SECTION_HEADER_SIZE;
        int64_t raw_end;
        size_t name_index;
        for (name_index = 0U; name_index < 8U; ++name_index)
            section->name[name_index] = (char)xx_io_get_u8(
                format->device, entry_offset + (int64_t)name_index);
        section->name[8] = '\0';
        section->virtual_size = xx_io_get_u32(format->device,
                                              entry_offset + 8, false);
        section->virtual_address = xx_io_get_u32(format->device,
                                                 entry_offset + 12, false);
        section->raw_size = xx_io_get_u32(format->device,
                                          entry_offset + 16, false);
        section->raw_offset = xx_io_get_u32(format->device,
                                            entry_offset + 20, false);
        section->characteristics = xx_io_get_u32(format->device,
                                                 entry_offset + 36, false);
        if (!format->is_mapped && section->raw_size) {
            if (section->raw_offset > (uint64_t)relative_size ||
                section->raw_size >
                    (uint64_t)relative_size - section->raw_offset) goto done;
            raw_end = (int64_t)section->raw_offset + section->raw_size;
            if (raw_end > image_end) image_end = raw_end;
        }
    }
    if (!format->is_mapped &&
        parsed->directory_count > XX_PE_DIRECTORY_SECURITY &&
        parsed->directory_rva[XX_PE_DIRECTORY_SECURITY] &&
        parsed->directory_size[XX_PE_DIRECTORY_SECURITY]) {
        uint64_t certificate_end =
            (uint64_t)parsed->directory_rva[XX_PE_DIRECTORY_SECURITY] +
            parsed->directory_size[XX_PE_DIRECTORY_SECURITY];
        if (certificate_end > (uint64_t)relative_size) goto done;
        if (certificate_end > (uint64_t)image_end) image_end = (int64_t)certificate_end;
    }
    if (format->is_mapped && parsed->image_size) {
        image_end = parsed->image_size < (uint64_t)relative_size
                        ? (int64_t)parsed->image_size
                        : relative_size;
    }
    parsed->format_size = image_end;
    if (image_end < relative_size) {
        parsed->overlay_offset = format->base_address + image_end;
        parsed->overlay_size = relative_size - image_end;
    } else {
        parsed->overlay_offset = -1;
        parsed->overlay_size = 0;
    }
    ok = true;
done:
    if (!ok) xx_pe_parsed_cleanup(parsed);
    return ok;
}

static xx_arch_t xx_pe_machine_to_arch(uint16_t machine) {
    switch (machine) {
        case 0x014c: return XX_ARCH_X86;
        case 0x8664: return XX_ARCH_X86_64;
        case 0x01c0: case 0x01c2: case 0x01c4: return XX_ARCH_ARM;
        case 0xaa64: return XX_ARCH_ARM64;
        case 0x0166: case 0x0266: case 0x0366: case 0x0466: return XX_ARCH_MIPS;
        case 0x01f0: case 0x01f1: return XX_ARCH_PPC;
        case 0x5032: return XX_ARCH_RISCV;
        case 0x5064: case 0x5128: return XX_ARCH_RISCV64;
        case 0x01a2: case 0x01a3: case 0x01a6: case 0x01a8: return XX_ARCH_SH;
        default: return XX_ARCH_UNKNOWN;
    }
}

static xx_format_type_t xx_pe_classify(const xx_pe_parsed *parsed) {
    if (parsed->characteristics & UINT16_C(0x2000)) return XX_TYPE_LIBRARY;
    if ((parsed->characteristics & UINT16_C(0x1000)) || parsed->subsystem == 1U)
        return XX_TYPE_DRIVER;
    if (parsed->subsystem == 2U || parsed->subsystem == 9U)
        return XX_TYPE_GUI_APPLICATION;
    return XX_TYPE_CONSOLE_APPLICATION;
}

static uint32_t xx_pe_map_alignment(uint32_t value, uint32_t fallback) {
    return value && value <= UINT32_C(0x10000) ? value : fallback;
}

static bool xx_pe_align_up(uint64_t value, uint32_t alignment,
                           uint64_t *result) {
    uint64_t remainder;
    uint64_t increment;
    if (!result || !alignment) return false;
    remainder = value % alignment;
    increment = remainder ? alignment - remainder : 0U;
    if (value > UINT64_MAX - increment) return false;
    *result = value + increment;
    return true;
}

static bool xx_pe_map_address(uint64_t base, uint64_t relative,
                              uint64_t *result) {
    if (!result || base == XX_INVALID_ADDRESS ||
        relative >= XX_INVALID_ADDRESS - base)
        return false;
    *result = base + relative;
    return *result != XX_INVALID_ADDRESS;
}

static bool xx_pe_map_offset(int64_t base, uint64_t relative,
                             int64_t *result) {
    if (!result || base < 0 || relative > (uint64_t)(INT64_MAX - base))
        return false;
    *result = base + (int64_t)relative;
    return true;
}

static uint16_t *xx_pe_get_sorted_section_indices(const xx_pe *pe) {
    uint16_t *indices;
    size_t i;
    if (!pe || !pe->number_of_sections || !pe->sections)
        return NULL;
    indices = (uint16_t *)xx_mem_alloc(
        (size_t)pe->number_of_sections * sizeof(*indices));
    if (!indices) return NULL;
    for (i = 0U; i < pe->number_of_sections; ++i) {
        uint16_t key = (uint16_t)i;
        size_t j = i;
        while (j > 0U &&
               pe->sections[indices[j - 1U]].virtual_address >
                   pe->sections[key].virtual_address) {
            indices[j] = indices[j - 1U];
            --j;
        }
        indices[j] = key;
    }
    return indices;
}

bool xx_pe_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode,
                          xx_memory_map *output, xx_pd_struct *pd) {
    xx_pe *pe = (xx_pe *)format;
    int64_t total;
    int64_t binary_size;
    uint32_t section_alignment;
    uint64_t module_address;
    uint64_t header_virtual;
    uint64_t header_physical;
    uint64_t mapped_extent;
    uint64_t mapped_virtual_extent;
    uint64_t maximum_physical_end;
    uint16_t *section_indices;
    uint16_t i;
    if (!format || !output || !format->device ||
        !format->base_info_handled || format->base_address < 0 ||
        xx_pd_is_stopped(pd))
        return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN)
        mode = XX_MEMORY_MAP_MODE_SECTIONS;
    if (mode != XX_MEMORY_MAP_MODE_SECTIONS) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    binary_size = total - format->base_address;
    section_alignment = xx_pe_map_alignment(pe->section_alignment, 0x1000U);
    module_address = format->module_address != XX_INVALID_ADDRESS
                         ? format->module_address
                         : pe->image_base;

    output->binary_offset = format->base_address;
    output->module_address = module_address;
    output->is_image = format->is_mapped;
    output->binary_size = binary_size;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = mode;
    output->entry_point_address = XX_INVALID_ADDRESS;
    (void)xx_pe_map_address(module_address, pe->entry_point_rva,
                            &output->entry_point_address);

    if (!xx_pe_align_up(pe->size_of_headers, section_alignment,
                        &header_virtual) || header_virtual > INT64_MAX)
        return false;
    mapped_extent = 0U;
    mapped_virtual_extent = 0U;
    if (format->is_mapped) {
        mapped_virtual_extent = pe->size_of_image
                                    ? pe->size_of_image
                                    : (uint64_t)binary_size;
        mapped_extent = mapped_virtual_extent;
        if (mapped_extent > (uint64_t)binary_size)
            mapped_extent = (uint64_t)binary_size;
        if (!xx_memory_map_add_part(
                output, format->base_address, (int64_t)mapped_extent,
                module_address, (int64_t)mapped_virtual_extent,
                XX_FILE_PART_REGION, 0, "Mapped image", true))
            return false;
    }
    header_physical = format->is_mapped ? header_virtual
                                        : (uint64_t)pe->size_of_headers;
    if (header_physical > (format->is_mapped ? mapped_extent
                                             : (uint64_t)binary_size))
        header_physical = format->is_mapped ? mapped_extent
                                            : (uint64_t)binary_size;
    if (!xx_memory_map_add_part(output, format->base_address,
                                (int64_t)header_physical, module_address,
                                (int64_t)header_virtual, XX_FILE_PART_HEADER,
                                0, "Header", false))
        return false;
    maximum_physical_end = format->is_mapped ? mapped_extent
                                              : header_physical;
    section_indices = xx_pe_get_sorted_section_indices(pe);
    if (!section_indices) return false;

    for (i = 0U; i < pe->number_of_sections; ++i) {
        uint16_t section_index = section_indices[i];
        const xx_pe_section *section = &pe->sections[section_index];
        uint64_t relative_offset;
        uint64_t physical_size;
        uint64_t virtual_source = section->virtual_size;
        uint64_t virtual_size;
        uint64_t section_address;
        uint64_t physical_end;
        int64_t offset;
        char name[XX_MEMORY_RECORD_NAME_SIZE];
        if (xx_pd_is_stopped(pd)) {
            xx_mem_free(section_indices);
            return false;
        }
        if (format->is_mapped && virtual_source < section->raw_size)
            virtual_source = section->raw_size;
        if (!xx_pe_align_up(virtual_source, section_alignment, &virtual_size) ||
            virtual_size > INT64_MAX)
        {
            xx_mem_free(section_indices);
            return false;
        }
        if (format->is_mapped) {
            relative_offset = section->virtual_address;
            physical_size = virtual_size;
        } else {
            relative_offset = section->raw_offset;
            physical_size = section->raw_size;
        }
        if (relative_offset > (format->is_mapped ? mapped_extent
                                                 : (uint64_t)binary_size))
            physical_size = 0U;
        else if (physical_size >
                 (format->is_mapped ? mapped_extent
                                    : (uint64_t)binary_size) - relative_offset)
            physical_size = (format->is_mapped ? mapped_extent
                                               : (uint64_t)binary_size) -
                            relative_offset;
        if (!xx_pe_map_offset(format->base_address, relative_offset, &offset) ||
            !xx_pe_map_address(module_address, section->virtual_address,
                               &section_address)) {
            xx_mem_free(section_indices);
            return false;
        }
        (void)xx_rt_snprintf(name, sizeof(name), "Section %u [\"%s\"]",
                             (unsigned)section_index + 1U, section->name);
        if (!xx_memory_map_add_part(output, offset, (int64_t)physical_size,
                                    section_address, (int64_t)virtual_size,
                                    XX_FILE_PART_SECTION,
                                    (int32_t)section_index + 1, name, false)) {
            xx_mem_free(section_indices);
            return false;
        }
        if (!format->is_mapped && physical_size > 0U &&
            relative_offset <= UINT64_MAX - physical_size) {
            physical_end = relative_offset + physical_size;
            if (physical_end > maximum_physical_end)
                maximum_physical_end = physical_end;
        } else if (!format->is_mapped && physical_size > 0U) {
            xx_mem_free(section_indices);
            return false;
        }
    }
    xx_mem_free(section_indices);

    if (maximum_physical_end < (uint64_t)binary_size) {
        int64_t overlay_offset;
        if (!xx_pe_map_offset(format->base_address, maximum_physical_end,
                              &overlay_offset) ||
            !xx_memory_map_add_part(
                output, overlay_offset,
                (int64_t)((uint64_t)binary_size - maximum_physical_end),
                XX_INVALID_ADDRESS, 0, XX_FILE_PART_OVERLAY,
                (int32_t)pe->number_of_sections + 1, "Overlay", false))
            return false;
    }
    return xx_memory_map_finalize(output);
}

static void xx_pe_vtable_destroy(Abstractformat *format) {
    xx_pe_destroy((xx_pe *)format);
}

void xx_pe_init(xx_pe *pe, xx_io_device *device, int64_t base_address) {
    if (!pe) return;
    xx_mem_zero(pe, sizeof(*pe));
    xx_format_init(&pe->format, device, base_address);
    pe->format.endian = XX_ENDIAN_LITTLE;
    pe->format.os = XX_OS_WINDOWS;
    pe->format.is_executable = true;
    xx_format_set_mime_type(&pe->format, "application/vnd.microsoft.portable-executable");
    xx_format_set_extension(&pe->format, "exe");
    pe->format.check_is_valid = xx_pe_check_is_valid;
    pe->format.handle_base_info = xx_pe_handle_base_info;
    pe->format.get_format_size = xx_pe_get_format_size;
    pe->format.get_number_of_imports = xx_pe_get_number_of_imports;
    pe->format.get_number_of_exports = xx_pe_get_number_of_exports;
    pe->format.get_number_of_resources = xx_pe_get_number_of_resources;
    xx_pe_setup_data_struct_callbacks(pe);
    pe->format.get_memory_map = xx_pe_get_memory_map;
    pe->format.destroy = xx_pe_vtable_destroy;
}

xx_pe *xx_pe_create(xx_io_device *device, int64_t base_address) {
    xx_pe *pe = (xx_pe *)xx_mem_alloc(sizeof(*pe));
    if (pe) xx_pe_init(pe, device, base_address);
    return pe;
}

void xx_pe_destroy(xx_pe *pe) {
    if (!pe) return;
    if (pe->sections) {
        xx_mem_free(pe->sections);
        pe->sections = NULL;
    }
    xx_format_cleanup_extra_parameters(&pe->format);
}

void xx_pe_free(xx_pe *pe) {
    if (!pe) return;
    xx_pe_destroy(pe);
    xx_mem_free(pe);
}

bool xx_pe_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    xx_pe_parsed parsed;
    bool result = xx_pe_parse(format, &parsed, pd);
    if (result) xx_pe_parsed_cleanup(&parsed);
    return result;
}

bool xx_pe_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_pe_parsed parsed;
    xx_pe *pe;
    const xx_memory_map *memory_map;
    int64_t total;
    if (!format || !xx_pe_parse(format, &parsed, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
            xx_format_invalidate_memory_map(format);
        }
        return false;
    }
    pe = (xx_pe *)format;
    xx_format_invalidate_memory_map(format);
    if (pe->sections) xx_mem_free(pe->sections);
    pe->pe_offset = parsed.pe_offset;
    pe->machine = parsed.machine;
    pe->number_of_sections = parsed.section_count;
    pe->timestamp = parsed.timestamp;
    pe->optional_header_size = parsed.optional_size;
    pe->characteristics = parsed.characteristics;
    pe->optional_magic = parsed.magic;
    pe->entry_point_rva = parsed.entry_rva;
    pe->image_base = parsed.image_base;
    pe->section_alignment = parsed.section_alignment;
    pe->file_alignment = parsed.file_alignment;
    pe->size_of_image = parsed.image_size;
    pe->size_of_headers = parsed.headers_size;
    pe->subsystem = parsed.subsystem;
    pe->dll_characteristics = parsed.dll_characteristics;
    pe->number_of_data_directories = parsed.directory_count;
    xx_mem_copy(pe->data_directory_rva, parsed.directory_rva,
                sizeof(pe->data_directory_rva));
    xx_mem_copy(pe->data_directory_size, parsed.directory_size,
                sizeof(pe->data_directory_size));
    pe->sections = parsed.sections;
    parsed.sections = NULL;
    format->file_type = parsed.magic == XX_PE_MAGIC_64
                            ? XX_FILE_TYPE_PE64 : XX_FILE_TYPE_PE32;
    format->arch = xx_pe_machine_to_arch(parsed.machine);
    format->format_type = xx_pe_classify(&parsed);
    if (format->format_type == XX_TYPE_LIBRARY)
        xx_format_set_extension(format, "dll");
    else if (format->format_type == XX_TYPE_DRIVER)
        xx_format_set_extension(format, "sys");
    else
        xx_format_set_extension(format, "exe");
    format->format_size = parsed.format_size;
    format->overlay_offset = parsed.overlay_offset;
    format->overlay_size = parsed.overlay_size;
    format->number_of_imports = 0U;
    format->number_of_exports = 0U;
    format->number_of_resources = 0U;
    format->is_signed = parsed.directory_count > XX_PE_DIRECTORY_SECURITY &&
                        parsed.directory_rva[XX_PE_DIRECTORY_SECURITY] != 0U &&
                        parsed.directory_size[XX_PE_DIRECTORY_SECURITY] != 0U;
    format->is_valid = true;
    format->base_info_handled = true;
    memory_map = xx_format_get_memory_map(format,
                                           XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    total = xx_io_total_size(format->device);
    if (!memory_map || total < format->base_address) {
        format->is_valid = false;
        format->base_info_handled = false;
        xx_pe_parsed_cleanup(&parsed);
        return false;
    }
    format->number_of_imports = xx_pe_count_imports(
        pe, memory_map, format->device, total);
    format->number_of_exports = xx_pe_count_exports(
        pe, memory_map, format->device, total);
    format->number_of_resources = xx_pe_count_resources(
        pe, memory_map, format->device, total);
    xx_pe_parsed_cleanup(&parsed);
    return true;
}

int64_t xx_pe_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pe_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_pe_get_number_of_imports(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pe_handle_base_info(format, pd))
               ? format->number_of_imports : 0U;
}

uint64_t xx_pe_get_number_of_exports(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pe_handle_base_info(format, pd))
               ? format->number_of_exports : 0U;
}

uint64_t xx_pe_get_number_of_resources(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pe_handle_base_info(format, pd))
               ? format->number_of_resources : 0U;
}

bool xx_pe_is_64(const xx_pe *pe) {
    return pe && pe->optional_magic == XX_PE_MAGIC_64;
}

uint16_t xx_pe_get_machine(const xx_pe *pe) {
    return pe ? pe->machine : 0U;
}

uint32_t xx_pe_get_entry_point_rva(const xx_pe *pe) {
    return pe ? pe->entry_point_rva : 0U;
}

uint64_t xx_pe_get_image_base(const xx_pe *pe) {
    return pe ? pe->image_base : 0U;
}

uint16_t xx_pe_get_number_of_sections(const xx_pe *pe) {
    return pe ? pe->number_of_sections : 0U;
}

const xx_pe_section *xx_pe_get_section(const xx_pe *pe, uint16_t index) {
    return pe && pe->sections && index < pe->number_of_sections
               ? &pe->sections[index] : NULL;
}
