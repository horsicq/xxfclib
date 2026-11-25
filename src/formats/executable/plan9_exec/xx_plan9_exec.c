/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/plan9_exec/xx_plan9_exec.h"
#include "../../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define P9_HDR_MAGIC UINT32_C(0x8000)
#define P9_KERNEL_MASK UINT64_C(0xffff800000000000)
#define P9_MAGIC(b) ((uint32_t)((4U * (b)) * (b) + 7U))

static bool p9_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static uint64_t p9_align(uint64_t value, uint64_t alignment)
{
    if (!alignment || value > UINT64_MAX - (alignment - 1U)) return 0;
    return (value + alignment - 1U) & ~(alignment - 1U);
}
static bool p9_arch(uint32_t magic, xx_arch_t *arch, bool *big, uint32_t *bits)
{
    *big = false;
    *bits = 32;
    switch (magic) {
        case P9_MAGIC(8):
            *arch = XX_ARCH_M68K;
            *big = true;
            return true;
        case P9_MAGIC(11): *arch = XX_ARCH_X86; return true;
        case P9_HDR_MAGIC | P9_MAGIC(26):
            *arch = XX_ARCH_X86_64;
            *bits = 64;
            return true;
        case P9_MAGIC(13):
            *arch = XX_ARCH_SPARC;
            *big = true;
            return true;
        case P9_MAGIC(25):
            *arch = XX_ARCH_SPARC64;
            *big = true;
            *bits = 64;
            return true;
        case P9_MAGIC(16):
        case P9_MAGIC(24):
            *arch = XX_ARCH_MIPS;
            *big = magic == P9_MAGIC(16);
            return true;
        case P9_MAGIC(18):
        case P9_MAGIC(22):
            *arch = XX_ARCH_MIPS64;
            *big = magic == P9_MAGIC(18);
            *bits = 64;
            return true;
        case P9_MAGIC(20): *arch = XX_ARCH_ARM; return true;
        case P9_HDR_MAGIC | P9_MAGIC(28):
            *arch = XX_ARCH_ARM64;
            *bits = 64;
            return true;
        case P9_MAGIC(21):
            *arch = XX_ARCH_PPC;
            *big = true;
            return true;
        case P9_HDR_MAGIC | P9_MAGIC(27):
            *arch = XX_ARCH_PPC64;
            *big = true;
            *bits = 64;
            return true;
        case P9_HDR_MAGIC | P9_MAGIC(30):
            *arch = XX_ARCH_RISCV64;
            *bits = 64;
            return true;
        default: return false;
    }
}
static uint64_t p9_base(const xx_plan9_exec *reader)
{
    bool kernel = (reader->entry_point & P9_KERNEL_MASK) != 0;
    if (reader->magic == (P9_HDR_MAGIC | P9_MAGIC(28))) return kernel ? UINT64_C(0xffffffffc0080000) : UINT64_C(0x10000);
    if (reader->magic == (P9_HDR_MAGIC | P9_MAGIC(30))) return kernel ? UINT64_C(0xffffffffc0200000) : UINT64_C(0x10000);
    if (reader->magic == (P9_HDR_MAGIC | P9_MAGIC(26))) return kernel ? UINT64_C(0xffffffff80110000) : UINT64_C(0x200000);
    if (reader->magic == (P9_HDR_MAGIC | P9_MAGIC(27))) return UINT64_C(0x200000);
    return UINT64_C(0x1000);
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    xx_plan9_exec *reader = (xx_plan9_exec *)format;
    uint8_t header[40];
    int64_t available = pm_available(format);
    uint64_t total;
    bool big;
    uint32_t bits;
    xx_arch_t arch;
    if (available < 32 || p9_stop(pd) || !pm_read(format, 0, header, 32)) return false;
    reader->magic = xx_data_get_u32(header, 4, 0, true);
    if (!p9_arch(reader->magic, &arch, &big, &bits)) return false;
    reader->text_size = xx_data_get_u32(header + 4, 4, 0, true);
    reader->data_size = xx_data_get_u32(header + 8, 4, 0, true);
    reader->bss_size = xx_data_get_u32(header + 12, 4, 0, true);
    reader->symbol_size = xx_data_get_u32(header + 16, 4, 0, true);
    reader->entry_point = xx_data_get_u32(header + 20, 4, 0, true);
    reader->pcsp_size = xx_data_get_u32(header + 24, 4, 0, true);
    reader->pcline_size = xx_data_get_u32(header + 28, 4, 0, true);
    reader->header_size = (reader->magic & P9_HDR_MAGIC) ? 40U : 32U;
    if (reader->header_size == 40U) {
        if (available < 40 || !pm_read(format, 32, header + 32, 8)) return false;
        reader->entry_point = xx_data_get_u64(header + 32, 8, 0, true);
    }
    total = reader->header_size;
#define P9_ADD_SIZE(value)                                        \
    do {                                                          \
        if ((uint64_t)(value) > UINT64_MAX - total) return false; \
        total += (uint64_t)(value);                               \
    } while (0)
    P9_ADD_SIZE(reader->text_size);
    P9_ADD_SIZE(reader->data_size);
    P9_ADD_SIZE(reader->symbol_size);
    P9_ADD_SIZE(reader->pcsp_size);
    P9_ADD_SIZE(reader->pcline_size);
#undef P9_ADD_SIZE
    if (!reader->text_size || total > (uint64_t)available || reader->entry_point == UINT64_MAX) return false;
    if (!pm_add(format, stream, "header.bin", 0, reader->header_size) || !pm_add(format, stream, "text.bin", reader->header_size, reader->text_size) ||
        (reader->data_size && !pm_add(format, stream, "data.bin", reader->header_size + reader->text_size, reader->data_size)) ||
        (reader->symbol_size && !pm_add(format, stream, "symbols.bin", reader->header_size + reader->text_size + reader->data_size, reader->symbol_size)) ||
        (reader->pcsp_size &&
         !pm_add(format, stream, "pcsp.bin", reader->header_size + reader->text_size + reader->data_size + reader->symbol_size, reader->pcsp_size)) ||
        (reader->pcline_size && !pm_add(format, stream, "pcline.bin",
                                        reader->header_size + reader->text_size + reader->data_size + reader->symbol_size + reader->pcsp_size, reader->pcline_size)))
        return false;
    format->arch = arch;
    format->endian = big ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    stream->size = (int64_t)total;
    return !p9_stop(pd);
}

void xx_plan9_exec_init(xx_plan9_exec *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, XX_FILE_TYPE_PLAN9_EXEC, "plan9");
    reader->format.format_type = XX_TYPE_CONSOLE_APPLICATION;
    reader->format.os = XX_OS_PLAN9;
    reader->format.is_executable = true;
    reader->format.get_memory_map = xx_plan9_exec_get_memory_map;
}
xx_plan9_exec *xx_plan9_exec_create(xx_io_device *device, int64_t base_address)
{
    xx_plan9_exec *reader = (xx_plan9_exec *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_plan9_exec_init(reader, device, base_address);
    return reader;
}
void xx_plan9_exec_destroy(xx_plan9_exec *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}
void xx_plan9_exec_free(xx_plan9_exec *reader)
{
    if (reader) {
        xx_plan9_exec_destroy(reader);
        xx_mem_free(reader);
    }
}
bool xx_plan9_exec_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_valid(format, pd);
}
bool xx_plan9_exec_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_handle(format, pd);
}

bool xx_plan9_exec_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *map, xx_pd_struct *pd)
{
    xx_plan9_exec *reader = (xx_plan9_exec *)format;
    uint64_t base, address, text_virtual, data_virtual, bss_virtual;
    int64_t physical;
    bool kernel;
    uint64_t text_file;
    if (!reader || !map || !format->base_info_handled || p9_stop(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_MAPS;
    if (mode != XX_MEMORY_MAP_MODE_MAPS) return false;
    base = p9_base(reader);
    kernel = (reader->entry_point & P9_KERNEL_MASK) != 0;
    text_virtual = p9_align(reader->text_size, reader->magic == (P9_HDR_MAGIC | P9_MAGIC(26))   ? UINT64_C(0x200000)
                                               : reader->magic == P9_MAGIC(16)                  ? UINT64_C(0x4000)
                                               : reader->magic == (P9_HDR_MAGIC | P9_MAGIC(28)) ? UINT64_C(0x10000)
                                                                                                : UINT64_C(0x1000));
    data_virtual = p9_align(reader->data_size, UINT64_C(0x1000));
    bss_virtual = p9_align(reader->bss_size, UINT64_C(0x1000));
    if (!text_virtual || reader->entry_point < base) return false;
    physical = format->base_address + (kernel ? reader->header_size : 0);
    text_file = reader->text_size + (kernel ? 0U : reader->header_size);
    map->binary_offset = format->base_address;
    map->module_address = base;
    map->binary_size = format->format_size;
    map->entry_point_address = reader->entry_point;
    map->code_base = (int64_t)base;
    map->start_load_offset = physical;
    map->file_type = format->file_type;
    map->format_type = format->format_type;
    map->endian = format->endian;
    map->arch = format->arch;
    map->mode = mode;
    if (!xx_memory_map_add_part(map, physical, (int64_t)text_file, base, (int64_t)text_virtual, XX_FILE_PART_SECTION, 0, "text", false)) return false;
    physical += (int64_t)text_file;
    address = base + text_virtual;
    if (reader->data_size && !xx_memory_map_add_part(map, physical, reader->data_size, address, (int64_t)data_virtual, XX_FILE_PART_SECTION, 1, "data", false))
        return false;
    physical += reader->data_size;
    address += data_virtual;
    if (bss_virtual && !xx_memory_map_add_part(map, 0, 0, address, (int64_t)bss_virtual, XX_FILE_PART_SECTION, 2, "bss", false)) return false;
    address += bss_virtual;
    if (reader->symbol_size && !xx_memory_map_add_part(map, physical, reader->symbol_size, address, reader->symbol_size, XX_FILE_PART_SECTION, 3, "symbols", true))
        return false;
    physical += reader->symbol_size;
    address += p9_align(reader->symbol_size, UINT64_C(0x1000));
    if (reader->pcsp_size && !xx_memory_map_add_part(map, physical, reader->pcsp_size, address, reader->pcsp_size, XX_FILE_PART_SECTION, 4, "pcsp", true)) return false;
    physical += reader->pcsp_size;
    address += p9_align(reader->pcsp_size, UINT64_C(0x1000));
    if (reader->pcline_size && !xx_memory_map_add_part(map, physical, reader->pcline_size, address, reader->pcline_size, XX_FILE_PART_SECTION, 5, "pcline", true))
        return false;
    return xx_memory_map_finalize(map);
}
