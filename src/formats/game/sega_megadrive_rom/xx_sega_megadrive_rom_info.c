/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/game/sega_megadrive_rom/xx_sega_megadrive_rom.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

static const char *const smd_vector_names[XX_SEGA_MEGADRIVE_ROM_VECTOR_COUNT] = {
    "SSP",      "Reset",    "BusErr",   "AdrErr",   "InvOpCode", "DivBy0",   "Check",    "TrapV",    "GPF",      "Trace",    "Reserv0",  "Reserv1",  "Reserv2",
    "Reserv3",  "Reserv4",  "BadInt",   "Reserv10", "Reserv11",  "Reserv12", "Reserv13", "Reserv14", "Reserv15", "Reserv16", "Reserv17", "BadIRQ",   "IRQ1",
    "EXT",      "IRQ3",     "HBLANK",   "IRQ5",     "VBLANK",    "IRQ7",     "Trap0",    "Trap1",    "Trap2",    "Trap3",    "Trap4",    "Trap5",    "Trap6",
    "Trap7",    "Trap8",    "Trap9",    "Trap10",   "Trap11",    "Trap12",   "Trap13",   "Trap14",   "Trap15",   "Reserv30", "Reserv31", "Reserv32", "Reserv33",
    "Reserv34", "Reserv35", "Reserv36", "Reserv37", "Reserv38",  "Reserv39", "Reserv3A", "Reserv3B", "Reserv3C", "Reserv3D", "Reserv3E", "Reserv3F"};

const char *xx_sega_megadrive_rom_vector_name(uint32_t index)
{
    return index < XX_SEGA_MEGADRIVE_ROM_VECTOR_COUNT ? smd_vector_names[index] : NULL;
}

static bool smd_read_exact(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || offset < 0 || (!buffer && size) || xx_io_seek64(device, offset, XX_RT_SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static void smd_copy_field(char *destination, size_t capacity, const uint8_t *source, size_t size)
{
    size_t length = size;
    if (!destination || !capacity) return;
    while (length && source[length - 1U] == ' ') --length;
    if (length >= capacity) length = capacity - 1U;
    if (length) xx_mem_copy(destination, source, length);
    destination[length] = '\0';
}

bool xx_sega_megadrive_rom_read_info(Abstractformat *format, xx_sega_megadrive_rom_info *info, xx_pd_struct *pd)
{
    uint8_t bytes[512];
    int64_t saved;
    uint32_t index;
    bool result = false;
    if (info) xx_mem_zero(info, sizeof(*info));
    if (!format || !info || !format->device || format->base_address < 0 || format->file_type != XX_FILE_TYPE_SEGA_MEGADRIVE_ROM || (pd && xx_pd_is_stopped(pd)))
        return false;
    saved = xx_io_tell(format->device);
    if (!format->base_info_handled && !xx_sega_megadrive_rom_handle_base_info(format, pd)) goto done;
    if (format->format_size < (int64_t)sizeof(bytes) || !smd_read_exact(format->device, format->base_address, bytes, sizeof(bytes))) goto done;

    smd_copy_field(info->copyright, sizeof(info->copyright), bytes + 0x100U, 32U);
    smd_copy_field(info->domestic_name, sizeof(info->domestic_name), bytes + 0x120U, 48U);
    smd_copy_field(info->overseas_name, sizeof(info->overseas_name), bytes + 0x150U, 48U);
    smd_copy_field(info->product_code, sizeof(info->product_code), bytes + 0x180U, 14U);
    smd_copy_field(info->peripherals, sizeof(info->peripherals), bytes + 0x190U, 16U);
    smd_copy_field(info->sram_code, sizeof(info->sram_code), bytes + 0x1b0U, 12U);
    smd_copy_field(info->modem_code, sizeof(info->modem_code), bytes + 0x1bcU, 12U);
    smd_copy_field(info->country_code, sizeof(info->country_code), bytes + 0x1f0U, 16U);
    info->checksum = xx_data_get_u16(bytes + 0x18eU, 2U, 0U, true);
    info->rom_start = xx_data_get_u32(bytes + 0x1a0U, 4U, 0U, true);
    info->rom_end = xx_data_get_u32(bytes + 0x1a4U, 4U, 0U, true);
    info->ram_start = xx_data_get_u32(bytes + 0x1a8U, 4U, 0U, true);
    info->ram_end = xx_data_get_u32(bytes + 0x1acU, 4U, 0U, true);
    for (index = 0U; index < XX_SEGA_MEGADRIVE_ROM_VECTOR_COUNT; ++index) info->vectors[index] = xx_data_get_u32(bytes + index * 4U, 4U, 0U, true);
    info->image_size = (uint64_t)format->format_size;
    result = true;

done:
    if (saved >= 0 && xx_io_seek64(format->device, saved, XX_RT_SEEK_SET) != 0) result = false;
    if (!result) xx_mem_zero(info, sizeof(*info));
    return result;
}
