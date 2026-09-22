/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/macho/xx_macho.h"

#include "xx_macho_data.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <limits.h>

#define XX_MACHO_HEADER32_SIZE 28U
#define XX_MACHO_HEADER64_SIZE 32U
#define XX_MACHO_LOAD_COMMAND_SIZE 8U
#define XX_MACHO_SEGMENT32_SIZE 56U
#define XX_MACHO_SEGMENT64_SIZE 72U
#define XX_MACHO_SECTION32_SIZE 68U
#define XX_MACHO_SECTION64_SIZE 80U
#define XX_MACHO_MAIN_SIZE 24U
#define XX_MACHO_MAX_COMMANDS UINT32_C(0xffff)
#define XX_MACHO_PROTECTION_EXECUTE UINT32_C(4)

typedef struct xx_macho_parsed {
    uint32_t magic;
    uint32_t cpu_type;
    uint32_t cpu_subtype;
    uint32_t file_type;
    uint32_t command_count;
    uint32_t commands_size;
    uint32_t flags;
    uint32_t reserved;
    bool is_64;
    bool big_endian;
    bool has_main_entry;
    uint64_t main_entry_offset;
    uint32_t segment_count;
    xx_macho_segment *segments;
    int64_t format_size;
} xx_macho_parsed;

static void xx_macho_vtable_destroy(Abstractformat *format);

static bool xx_macho_range_is_valid(int64_t available, uint64_t offset,
                                    uint64_t size) {
    return available >= 0 && offset <= (uint64_t)available &&
           size <= (uint64_t)available - offset;
}

static bool xx_macho_absolute_offset(int64_t base, uint64_t relative,
                                     int64_t *result) {
    if (!result || base < 0 || relative > (uint64_t)(INT64_MAX - base))
        return false;
    *result = base + (int64_t)relative;
    return true;
}

static bool xx_macho_relocate_address(uint64_t address, uint64_t preferred,
                                      uint64_t module, uint64_t *result) {
    uint64_t relative;
    if (!result || address < preferred || module == XX_INVALID_ADDRESS)
        return false;
    relative = address - preferred;
    if (relative >= XX_INVALID_ADDRESS - module) return false;
    *result = module + relative;
    return *result != XX_INVALID_ADDRESS;
}

static void xx_macho_parsed_cleanup(xx_macho_parsed *parsed) {
    if (!parsed) return;
    if (parsed->segments) xx_mem_free(parsed->segments);
    xx_mem_zero(parsed, sizeof(*parsed));
}

static bool xx_macho_decode_magic(uint32_t magic, bool *is_64,
                                  bool *big_endian) {
    if (!is_64 || !big_endian) return false;
    switch (magic) {
        case XX_MACHO_MAGIC_32:
            *is_64 = false;
            *big_endian = false;
            return true;
        case XX_MACHO_MAGIC_64:
            *is_64 = true;
            *big_endian = false;
            return true;
        case XX_MACHO_CIGAM_32:
            *is_64 = false;
            *big_endian = true;
            return true;
        case XX_MACHO_CIGAM_64:
            *is_64 = true;
            *big_endian = true;
            return true;
        default:
            return false;
    }
}

static bool xx_macho_file_type_is_valid(uint32_t file_type) {
    return file_type >= XX_MACHO_FILE_OBJECT &&
           file_type <= XX_MACHO_FILE_GPU_DYLIB;
}

static void xx_macho_read_segment_name(xx_io_device *device,
                                       int64_t offset, char name[17]) {
    size_t index;
    for (index = 0U; index < 16U; ++index) {
        name[index] = (char)xx_io_get_u8(device, offset + (int64_t)index);
    }
    name[16] = '\0';
    for (index = 0U; index < 16U; ++index) {
        if (name[index] == '\0') break;
    }
    if (index == 16U) name[16] = '\0';
}

static bool xx_macho_parse_segment(Abstractformat *format,
                                   xx_macho_parsed *parsed,
                                   int64_t command_offset,
                                   uint32_t command_size,
                                   int64_t available,
                                   uint64_t *extent) {
    xx_macho_segment *segment;
    uint32_t section_size;
    uint32_t segment_size;
    uint64_t required;
    uint64_t end;
    bool big_endian = parsed->big_endian;
    if (!parsed->segments || parsed->segment_count >= parsed->command_count)
        return false;
    segment = &parsed->segments[parsed->segment_count];
    xx_mem_zero(segment, sizeof(*segment));
    segment_size = parsed->is_64 ? XX_MACHO_SEGMENT64_SIZE
                                 : XX_MACHO_SEGMENT32_SIZE;
    section_size = parsed->is_64 ? XX_MACHO_SECTION64_SIZE
                                 : XX_MACHO_SECTION32_SIZE;
    if (command_size < segment_size) return false;
    xx_macho_read_segment_name(format->device, command_offset + 8,
                               segment->name);
    if (parsed->is_64) {
        segment->virtual_address = xx_io_get_u64(
            format->device, command_offset + 24, big_endian);
        segment->virtual_size = xx_io_get_u64(
            format->device, command_offset + 32, big_endian);
        segment->file_offset = xx_io_get_u64(
            format->device, command_offset + 40, big_endian);
        segment->file_size = xx_io_get_u64(
            format->device, command_offset + 48, big_endian);
        segment->maximum_protection = xx_io_get_u32(
            format->device, command_offset + 56, big_endian);
        segment->initial_protection = xx_io_get_u32(
            format->device, command_offset + 60, big_endian);
        segment->section_count = xx_io_get_u32(
            format->device, command_offset + 64, big_endian);
        segment->flags = xx_io_get_u32(format->device,
                                       command_offset + 68, big_endian);
    } else {
        segment->virtual_address = xx_io_get_u32(
            format->device, command_offset + 24, big_endian);
        segment->virtual_size = xx_io_get_u32(
            format->device, command_offset + 28, big_endian);
        segment->file_offset = xx_io_get_u32(
            format->device, command_offset + 32, big_endian);
        segment->file_size = xx_io_get_u32(
            format->device, command_offset + 36, big_endian);
        segment->maximum_protection = xx_io_get_u32(
            format->device, command_offset + 40, big_endian);
        segment->initial_protection = xx_io_get_u32(
            format->device, command_offset + 44, big_endian);
        segment->section_count = xx_io_get_u32(
            format->device, command_offset + 48, big_endian);
        segment->flags = xx_io_get_u32(format->device,
                                       command_offset + 52, big_endian);
    }
    required = segment_size;
    if (segment->section_count >
        (UINT64_MAX - required) / section_size) return false;
    required += (uint64_t)segment->section_count * section_size;
    if (required > command_size ||
        !xx_macho_range_is_valid(available, segment->file_offset,
                                 segment->file_size) ||
        segment->virtual_size < segment->file_size ||
        segment->virtual_size > INT64_MAX ||
        segment->file_size > INT64_MAX ||
        (segment->virtual_size > 0U &&
         segment->virtual_address >
             XX_INVALID_ADDRESS - segment->virtual_size)) {
        return false;
    }
    end = segment->file_offset + segment->file_size;
    if (end > *extent) *extent = end;
    ++parsed->segment_count;
    return true;
}

static bool xx_macho_parse(Abstractformat *format, xx_macho_parsed *parsed,
                           xx_pd_struct *pd) {
    int64_t total;
    int64_t available;
    int64_t base;
    uint32_t header_size;
    uint64_t commands_end_relative;
    uint64_t extent;
    uint32_t index;
    int64_t command_offset;
    if (!format || !format->device || !parsed || format->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }
    xx_mem_zero(parsed, sizeof(*parsed));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = total - format->base_address;
    base = format->base_address;
    if (available < 4) return false;
    parsed->magic = xx_io_get_u32(format->device, base, false);
    if (!xx_macho_decode_magic(parsed->magic, &parsed->is_64,
                               &parsed->big_endian)) {
        return false;
    }
    header_size = parsed->is_64 ? XX_MACHO_HEADER64_SIZE
                                : XX_MACHO_HEADER32_SIZE;
    if (available < header_size) return false;
    parsed->cpu_type = xx_io_get_u32(format->device, base + 4,
                                     parsed->big_endian);
    parsed->cpu_subtype = xx_io_get_u32(format->device, base + 8,
                                        parsed->big_endian);
    parsed->file_type = xx_io_get_u32(format->device, base + 12,
                                      parsed->big_endian);
    parsed->command_count = xx_io_get_u32(format->device, base + 16,
                                          parsed->big_endian);
    parsed->commands_size = xx_io_get_u32(format->device, base + 20,
                                          parsed->big_endian);
    parsed->flags = xx_io_get_u32(format->device, base + 24,
                                  parsed->big_endian);
    if (parsed->is_64) {
        parsed->reserved = xx_io_get_u32(format->device, base + 28,
                                         parsed->big_endian);
    }
    if (!xx_macho_file_type_is_valid(parsed->file_type) ||
        parsed->command_count > XX_MACHO_MAX_COMMANDS ||
        !xx_macho_range_is_valid(available, header_size,
                                 parsed->commands_size)) {
        return false;
    }
    if ((parsed->command_count == 0U) != (parsed->commands_size == 0U))
        return false;
    commands_end_relative = (uint64_t)header_size + parsed->commands_size;
    extent = commands_end_relative;
    if (parsed->command_count != 0U) {
        parsed->segments = (xx_macho_segment *)xx_mem_alloc(
            (size_t)parsed->command_count * sizeof(*parsed->segments));
        if (!parsed->segments) return false;
        xx_mem_zero(parsed->segments,
                    (size_t)parsed->command_count *
                        sizeof(*parsed->segments));
    }
    command_offset = base + (int64_t)header_size;
    for (index = 0U; index < parsed->command_count; ++index) {
        uint32_t command;
        uint32_t command_size;
        uint64_t command_relative = (uint64_t)(command_offset - base);
        if (xx_pd_is_stopped(pd) ||
            !xx_macho_range_is_valid(available, command_relative,
                                     XX_MACHO_LOAD_COMMAND_SIZE)) {
            xx_macho_parsed_cleanup(parsed);
            return false;
        }
        command = xx_io_get_u32(format->device, command_offset,
                                parsed->big_endian);
        command_size = xx_io_get_u32(format->device, command_offset + 4,
                                     parsed->big_endian);
        if (command_size < XX_MACHO_LOAD_COMMAND_SIZE ||
            (command_size & (parsed->is_64 ? 7U : 3U)) != 0U ||
            !xx_macho_range_is_valid(available, command_relative,
                                     command_size) ||
            command_relative + command_size > commands_end_relative) {
            xx_macho_parsed_cleanup(parsed);
            return false;
        }
        if ((command == XX_MACHO_LOAD_SEGMENT && parsed->is_64) ||
            (command == XX_MACHO_LOAD_SEGMENT_64 && !parsed->is_64)) {
            xx_macho_parsed_cleanup(parsed);
            return false;
        }
        if (command == (parsed->is_64 ? XX_MACHO_LOAD_SEGMENT_64
                                      : XX_MACHO_LOAD_SEGMENT)) {
            if (!xx_macho_parse_segment(format, parsed, command_offset,
                                        command_size, available, &extent)) {
                xx_macho_parsed_cleanup(parsed);
                return false;
            }
        } else if (command == XX_MACHO_LOAD_MAIN) {
            uint64_t entry_offset;
            if (command_size < XX_MACHO_MAIN_SIZE) {
                xx_macho_parsed_cleanup(parsed);
                return false;
            }
            entry_offset = xx_io_get_u64(format->device,
                                         command_offset + 8,
                                         parsed->big_endian);
            if (entry_offset >= (uint64_t)available) {
                xx_macho_parsed_cleanup(parsed);
                return false;
            }
            if (!parsed->has_main_entry) {
                parsed->has_main_entry = true;
                parsed->main_entry_offset = entry_offset;
            }
        }
        command_offset += (int64_t)command_size;
    }
    if ((uint64_t)(command_offset - base) != commands_end_relative ||
        extent > (uint64_t)available || extent > INT64_MAX) {
        xx_macho_parsed_cleanup(parsed);
        return false;
    }
    parsed->format_size = (int64_t)extent;
    return true;
}

static xx_arch_t xx_macho_cpu_to_arch(uint32_t cpu_type) {
    switch (cpu_type) {
        case UINT32_C(7): return XX_ARCH_X86;
        case UINT32_C(0x01000007): return XX_ARCH_X86_64;
        case UINT32_C(12): return XX_ARCH_ARM;
        case UINT32_C(0x0100000c):
        case UINT32_C(0x0200000c): return XX_ARCH_ARM64;
        case UINT32_C(8): return XX_ARCH_MIPS;
        case UINT32_C(18): return XX_ARCH_PPC;
        case UINT32_C(0x01000012): return XX_ARCH_PPC64;
        case UINT32_C(14): return XX_ARCH_SPARC;
        case UINT32_C(6): return XX_ARCH_M68K;
        default: return XX_ARCH_UNKNOWN;
    }
}

static xx_format_type_t xx_macho_classify(uint32_t file_type) {
    switch (file_type) {
        case XX_MACHO_FILE_OBJECT:
        case XX_MACHO_FILE_DSYM:
            return XX_TYPE_OBJECT;
        case XX_MACHO_FILE_EXECUTE:
        case XX_MACHO_FILE_GPU_EXECUTE:
        case XX_MACHO_FILE_DYLINKER:
            return XX_TYPE_CONSOLE_APPLICATION;
        case XX_MACHO_FILE_FVMLIB:
        case XX_MACHO_FILE_DYLIB:
        case XX_MACHO_FILE_BUNDLE:
        case XX_MACHO_FILE_DYLIB_STUB:
        case XX_MACHO_FILE_GPU_DYLIB:
            return XX_TYPE_LIBRARY;
        case XX_MACHO_FILE_KEXT_BUNDLE:
            return XX_TYPE_DRIVER;
        case XX_MACHO_FILE_PRELOAD:
            return XX_TYPE_FIRMWARE;
        case XX_MACHO_FILE_FILESET:
            return XX_TYPE_PACKAGE;
        default:
            return XX_TYPE_UNKNOWN;
    }
}

void xx_macho_init(xx_macho *macho, xx_io_device *device,
                   int64_t base_address) {
    if (!macho) return;
    xx_mem_zero(macho, sizeof(*macho));
    xx_format_init(&macho->format, device, base_address);
    macho->format.os = XX_OS_MACOS;
    xx_format_set_mime_type(&macho->format, "application/x-mach-o");
    xx_format_set_extension(&macho->format, "macho");
    macho->format.check_is_valid = xx_macho_check_is_valid;
    macho->format.handle_base_info = xx_macho_handle_base_info;
    macho->format.get_format_size = xx_macho_get_format_size;
    macho->format.get_memory_map = xx_macho_get_memory_map;
    xx_macho_setup_data_struct_callbacks(macho);
    macho->format.destroy = xx_macho_vtable_destroy;
}

xx_macho *xx_macho_create(xx_io_device *device, int64_t base_address) {
    xx_macho *macho = (xx_macho *)xx_mem_alloc(sizeof(*macho));
    if (macho) xx_macho_init(macho, device, base_address);
    return macho;
}

void xx_macho_destroy(xx_macho *macho) {
    if (!macho) return;
    if (macho->segments) {
        xx_mem_free(macho->segments);
        macho->segments = NULL;
    }
    xx_format_cleanup_extra_parameters(&macho->format);
}

static void xx_macho_vtable_destroy(Abstractformat *format) {
    xx_macho_destroy((xx_macho *)format);
}

void xx_macho_free(xx_macho *macho) {
    if (!macho) return;
    xx_macho_destroy(macho);
    xx_mem_free(macho);
}

bool xx_macho_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    xx_macho_parsed parsed;
    bool result = xx_macho_parse(format, &parsed, pd);
    if (result) xx_macho_parsed_cleanup(&parsed);
    return result;
}

bool xx_macho_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_macho_parsed parsed;
    xx_macho *macho;
    int64_t total;
    if (!format || !xx_macho_parse(format, &parsed, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
            xx_format_invalidate_memory_map(format);
        }
        return false;
    }
    macho = (xx_macho *)format;
    xx_format_invalidate_memory_map(format);
    if (macho->segments) xx_mem_free(macho->segments);
    macho->magic = parsed.magic;
    macho->cpu_type = parsed.cpu_type;
    macho->cpu_subtype = parsed.cpu_subtype;
    macho->macho_file_type = parsed.file_type;
    macho->command_count = parsed.command_count;
    macho->commands_size = parsed.commands_size;
    macho->flags = parsed.flags;
    macho->reserved = parsed.reserved;
    macho->is_64 = parsed.is_64;
    macho->has_main_entry = parsed.has_main_entry;
    macho->main_entry_offset = parsed.main_entry_offset;
    macho->segment_count = parsed.segment_count;
    macho->segments = parsed.segments;
    parsed.segments = NULL;
    format->file_type = macho->is_64 ? XX_FILE_TYPE_MACHO64
                                     : XX_FILE_TYPE_MACHO32;
    format->endian = parsed.big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    format->os = XX_OS_MACOS;
    format->arch = xx_macho_cpu_to_arch(macho->cpu_type);
    format->format_type = xx_macho_classify(macho->macho_file_type);
    format->is_executable =
        macho->macho_file_type == XX_MACHO_FILE_EXECUTE ||
        macho->macho_file_type == XX_MACHO_FILE_GPU_EXECUTE ||
        macho->macho_file_type == XX_MACHO_FILE_DYLINKER ||
        macho->macho_file_type == XX_MACHO_FILE_PRELOAD;
    if (macho->macho_file_type == XX_MACHO_FILE_DYLIB ||
        macho->macho_file_type == XX_MACHO_FILE_FVMLIB ||
        macho->macho_file_type == XX_MACHO_FILE_DYLIB_STUB)
        xx_format_set_extension(format, "dylib");
    else if (macho->macho_file_type == XX_MACHO_FILE_OBJECT)
        xx_format_set_extension(format, "o");
    else if (macho->macho_file_type == XX_MACHO_FILE_BUNDLE)
        xx_format_set_extension(format, "bundle");
    else
        xx_format_set_extension(format, "macho");
    format->format_size = parsed.format_size;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) {
        xx_macho_parsed_cleanup(&parsed);
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
    xx_macho_parsed_cleanup(&parsed);
    return true;
}

int64_t xx_macho_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_macho_handle_base_info(format, pd))
               ? format->format_size : -1;
}

bool xx_macho_get_memory_map(Abstractformat *format,
                             xx_memory_map_mode_t mode,
                             xx_memory_map *output, xx_pd_struct *pd) {
    xx_macho *macho = (xx_macho *)format;
    int64_t total;
    int64_t binary_size;
    uint64_t preferred = XX_INVALID_ADDRESS;
    uint64_t module;
    uint32_t index;
    if (!format || !output || !format->device ||
        !format->base_info_handled || format->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN)
        mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    if (mode != XX_MEMORY_MAP_MODE_SEGMENTS) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    binary_size = total - format->base_address;
    for (index = 0U; index < macho->segment_count; ++index) {
        const xx_macho_segment *segment = &macho->segments[index];
        if (segment->virtual_size > 0U &&
            (preferred == XX_INVALID_ADDRESS ||
             segment->virtual_address < preferred)) {
            preferred = segment->virtual_address;
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
    for (index = 0U; index < macho->segment_count; ++index) {
        const xx_macho_segment *segment = &macho->segments[index];
        uint64_t address;
        int64_t offset;
        char fallback[XX_MEMORY_RECORD_NAME_SIZE];
        const char *name = segment->name;
        bool invisible = segment->file_size == 0U &&
                         segment->file_offset == 0U;
        if (segment->file_size == 0U && segment->virtual_size == 0U)
            continue;
        if (xx_pd_is_stopped(pd) ||
            !xx_macho_absolute_offset(format->base_address,
                                      segment->file_offset, &offset) ||
            !xx_macho_relocate_address(segment->virtual_address,
                                       preferred, module, &address)) {
            return false;
        }
        if (name[0] == '\0') {
            (void)xx_rt_snprintf(fallback, sizeof(fallback), "Segment(%u)",
                                 (unsigned)index);
            name = fallback;
        }
        if (!xx_memory_map_add_part(
                output, offset, (int64_t)segment->file_size, address,
                (int64_t)segment->virtual_size, XX_FILE_PART_SEGMENT,
                (int32_t)index, name, invisible)) {
            return false;
        }
        if (segment->file_size > 0U &&
            (output->start_load_offset < 0 ||
             offset < output->start_load_offset)) {
            output->start_load_offset = offset;
        }
        if ((segment->initial_protection &
             XX_MACHO_PROTECTION_EXECUTE) != 0U &&
            output->code_base < 0 && address <= INT64_MAX) {
            output->code_base = (int64_t)address;
        }
    }
    if (format->format_size < binary_size &&
        !xx_memory_map_add_part(
            output, format->base_address + format->format_size,
            binary_size - format->format_size, XX_INVALID_ADDRESS, 0,
            XX_FILE_PART_OVERLAY, -1, "Overlay", false)) {
        return false;
    }
    if (!xx_memory_map_finalize(output)) return false;
    if (macho->has_main_entry) {
        int64_t entry_offset;
        if (!xx_macho_absolute_offset(format->base_address,
                                      macho->main_entry_offset,
                                      &entry_offset)) {
            return false;
        }
        output->entry_point_address =
            xx_memory_map_offset_to_address(output, entry_offset);
    }
    return !xx_pd_is_stopped(pd);
}

bool xx_macho_is_64(const xx_macho *macho) {
    return macho && macho->is_64;
}

uint32_t xx_macho_get_cpu_type(const xx_macho *macho) {
    return macho ? macho->cpu_type : 0U;
}

uint32_t xx_macho_get_file_type(const xx_macho *macho) {
    return macho ? macho->macho_file_type : 0U;
}

uint32_t xx_macho_get_number_of_segments(const xx_macho *macho) {
    return macho ? macho->segment_count : 0U;
}

const xx_macho_segment *xx_macho_get_segment(const xx_macho *macho,
                                             uint32_t index) {
    return macho && macho->segments && index < macho->segment_count
               ? &macho->segments[index] : NULL;
}
