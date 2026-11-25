/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/firmware/bios_firmware/xx_bios_firmware.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

static bool bios_firmware_layout(const Abstractformat *format, int64_t *size, int64_t *reset_offset, uint8_t reset[5], xx_pd_struct *pd)
{
    int64_t total;
    uint8_t prefix[3];
    int64_t available;
    int64_t reset_absolute;
    if (!format || !format->device || format->base_address < 0 || xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = total - format->base_address;
    if (available <= UINT16_MAX || available < 16 || !xx_io_read_at(format->device, format->base_address, prefix, sizeof(prefix)) || prefix[0] == UINT8_C(0xcf) ||
        prefix[0] == UINT8_C(0x7f) || memcmp(prefix, "dex", 3U) == 0 || memcmp(prefix, "MZ", 2U) == 0)
        return false;
    if (available - 16 > INT64_MAX - format->base_address) return false;
    reset_absolute = format->base_address + available - 16;
    if (!xx_io_read_at(format->device, reset_absolute, reset, 5U) || (reset[0] != XX_BIOS_FIRMWARE_FAR_JUMP && reset[0] != XX_BIOS_FIRMWARE_NEAR_JUMP)) return false;
    if (size) *size = available;
    if (reset_offset) *reset_offset = reset_absolute;
    return true;
}

void xx_bios_firmware_init(xx_bios_firmware *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_format_init(&reader->format, device, base_address);
    reader->format.file_type = XX_FILE_TYPE_BIOS_FIRMWARE;
    reader->format.check_is_valid = xx_bios_firmware_check_is_valid;
    reader->format.handle_base_info = xx_bios_firmware_handle_base_info;
    reader->format.get_format_size = xx_bios_firmware_get_format_size;
    reader->format.get_memory_map = xx_bios_firmware_get_memory_map;
    reader->format.format_type = XX_TYPE_FIRMWARE;
    reader->format.arch = XX_ARCH_X86_16;
    reader->format.endian = XX_ENDIAN_LITTLE;
    reader->format.is_executable = true;
    reader->reset_vector_offset = -1;
    xx_format_set_extension(&reader->format, "bin");
    xx_format_set_mime_type(&reader->format, "application/x-pc-bios");
}

xx_bios_firmware *xx_bios_firmware_create(xx_io_device *device, int64_t base_address)
{
    xx_bios_firmware *reader = (xx_bios_firmware *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_bios_firmware_init(reader, device, base_address);
    return reader;
}

void xx_bios_firmware_destroy(xx_bios_firmware *reader)
{
    if (!reader) return;
    xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_bios_firmware_free(xx_bios_firmware *reader)
{
    if (!reader) return;
    xx_bios_firmware_destroy(reader);
    xx_mem_free(reader);
}

bool xx_bios_firmware_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    uint8_t reset[5];
    return bios_firmware_layout(format, NULL, NULL, reset, pd);
}

bool xx_bios_firmware_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    xx_bios_firmware *reader = (xx_bios_firmware *)format;
    uint8_t reset[5];
    int64_t size;
    int64_t reset_offset;
    if (!reader || !format->device || xx_pd_is_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    if (!bios_firmware_layout(format, &size, &reset_offset, reset, pd)) return false;
    format->format_size = size;
    format->is_valid = true;
    format->base_info_handled = true;
    format->file_type = XX_FILE_TYPE_BIOS_FIRMWARE;
    format->format_type = XX_TYPE_FIRMWARE;
    format->arch = XX_ARCH_X86_16;
    format->endian = XX_ENDIAN_LITTLE;
    format->is_executable = true;
    reader->reset_vector_offset = reset_offset;
    reader->reset_opcode = reset[0];
    if (reset[0] == XX_BIOS_FIRMWARE_FAR_JUMP) {
        reader->reset_target_offset = (uint16_t)((uint16_t)reset[1] | ((uint16_t)reset[2] << 8U));
        reader->reset_target_segment = (uint16_t)((uint16_t)reset[3] | ((uint16_t)reset[4] << 8U));
    }
    return true;
}

int64_t xx_bios_firmware_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_bios_firmware_handle_base_info(format, pd)) ? format->format_size : -1;
}

bool xx_bios_firmware_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd)
{
    int64_t size;
    int64_t boot_offset;
    int64_t early_offset;
    xx_file_part_t part;
    if (!format || !output || (!format->base_info_handled && !xx_bios_firmware_handle_base_info(format, pd)) || xx_pd_is_stopped(pd)) return false;
    if (mode == XX_MEMORY_MAP_MODE_UNKNOWN) mode = XX_MEMORY_MAP_MODE_SECTIONS;
    if (mode != XX_MEMORY_MAP_MODE_SECTIONS && mode != XX_MEMORY_MAP_MODE_SEGMENTS) return false;
    size = format->format_size;
    if (size < XX_BIOS_FIRMWARE_BLOCK_SIZE || format->base_address > INT64_MAX - size) return false;
    boot_offset = format->base_address + size - XX_BIOS_FIRMWARE_BLOCK_SIZE;
    part = mode == XX_MEMORY_MAP_MODE_SECTIONS ? XX_FILE_PART_SECTION : XX_FILE_PART_SEGMENT;
    output->binary_offset = format->base_address;
    output->binary_size = size;
    output->module_address = UINT64_C(0x000e0000);
    output->entry_point_address = XX_BIOS_FIRMWARE_RESET_VECTOR_ADDRESS;
    output->code_base = INT64_C(0x000f0000);
    output->start_load_offset = boot_offset;
    output->file_type = format->file_type;
    output->format_type = format->format_type;
    output->endian = format->endian;
    output->arch = format->arch;
    output->mode = mode;
    output->is_image = true;
    if (!xx_memory_map_add_part(output, boot_offset, XX_BIOS_FIRMWARE_BLOCK_SIZE, UINT64_C(0x000f0000), XX_BIOS_FIRMWARE_BLOCK_SIZE, part, 0, "bootblk", false))
        return false;
    if (size >= 2 * (int64_t)XX_BIOS_FIRMWARE_BLOCK_SIZE) {
        early_offset = format->base_address + size - 2 * (int64_t)XX_BIOS_FIRMWARE_BLOCK_SIZE;
        if (!xx_memory_map_add_part(output, early_offset, XX_BIOS_FIRMWARE_BLOCK_SIZE, UINT64_C(0x000e0000), XX_BIOS_FIRMWARE_BLOCK_SIZE, part, 1, "_e000", false))
            return false;
    }
    return xx_memory_map_finalize(output);
}

int64_t xx_bios_firmware_get_declared_entry_offset(const xx_bios_firmware *reader)
{
    return reader ? XX_BIOS_FIRMWARE_DECLARED_ENTRY_OFFSET : -1;
}

int64_t xx_bios_firmware_get_reset_vector_offset(const xx_bios_firmware *reader)
{
    return reader ? reader->reset_vector_offset : -1;
}

uint64_t xx_bios_firmware_get_entry_point_address(const xx_bios_firmware *reader)
{
    return reader ? XX_BIOS_FIRMWARE_RESET_VECTOR_ADDRESS : XX_INVALID_ADDRESS;
}

uint8_t xx_bios_firmware_get_reset_opcode(const xx_bios_firmware *reader)
{
    return reader ? reader->reset_opcode : 0U;
}

bool xx_bios_firmware_get_far_jump_target(const xx_bios_firmware *reader, uint16_t *segment, uint16_t *offset)
{
    if (!reader || reader->reset_opcode != XX_BIOS_FIRMWARE_FAR_JUMP || !segment || !offset) return false;
    *segment = reader->reset_target_segment;
    *offset = reader->reset_target_offset;
    return true;
}
