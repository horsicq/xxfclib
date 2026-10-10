/* SPDX-License-Identifier: 0BSD
 * RISC-V BCJ decoder for XZ filter ID 0x0B. Algorithm and wire encoding
 * derived from XZ Utils liblzma src/liblzma/simple/riscv.c (0BSD),
 * authored by Lasse Collin and Jia Tan. This file is an independent,
 * decoder-only implementation for one complete in-memory XZ Block. */
#ifndef XX_XZ_RISCV_NATIVE_H
#define XX_XZ_RISCV_NATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "xxfclib/data/xx_data.h"

static bool xx_xz_riscv_decode(uint8_t *data, size_t size, const uint8_t *properties, size_t properties_size)
{
    uint32_t start = 0U;
    size_t offset;
    size_t limit;
    if ((!data && size != 0U) || (properties_size != 0U && properties_size != 4U) || (!properties && properties_size != 0U)) return false;
    if (properties_size == 4U) {
        start = xx_data_get_u32(properties, 4, 0, false);
        if ((start & 1U) != 0U) return false;
    }
    if (size < 8U) return true;
    limit = size - 8U;
    for (offset = 0U; offset <= limit; offset += 2U) {
        uint32_t instruction = data[offset];
        if (instruction == 0xEFU) {
            uint32_t b1 = data[offset + 1U];
            uint32_t address;
            if ((b1 & 0x0DU) != 0U) continue;
            address = ((b1 & 0xF0U) << 13U) | ((uint32_t)data[offset + 2U] << 9U) | ((uint32_t)data[offset + 3U] << 1U);
            address -= start + (uint32_t)offset;
            data[offset + 1U] = (uint8_t)((b1 & 0x0FU) | ((address >> 8U) & 0xF0U));
            data[offset + 2U] = (uint8_t)(((address >> 16U) & 0x0FU) | ((address >> 7U) & 0x10U) | ((address << 4U) & 0xE0U));
            data[offset + 3U] = (uint8_t)(((address >> 4U) & 0x7FU) | ((address >> 13U) & 0x80U));
            offset += 2U;
        } else if ((instruction & 0x7FU) == 0x17U) {
            uint32_t second;
            instruction = xx_data_get_u32(data + offset, 4, 0, false);
            second = xx_data_get_u32(data + offset + 4U, 4, 0, false);
            if ((instruction & 0xE80U) != 0U) {
                uint32_t address;
                if ((((instruction << 8U) ^ (second - 3U)) & 0xF8003U) != 0U) {
                    offset += 4U;
                    continue;
                }
                address = (instruction & 0xFFFFF000U) + (second >> 20U);
                instruction = 0x17U | (2U << 7U) | (second << 12U);
                second = address;
            } else {
                uint32_t register_id = instruction >> 27U;
                uint32_t address;
                if (((uint32_t)((instruction - 0x3117U) << 18U)) >= (register_id & 0x1DU)) {
                    offset += 2U;
                    continue;
                }
                address = xx_data_get_u32(data + offset + 4U, 4, 0, true);
                address -= start + (uint32_t)offset;
                second = (instruction >> 12U) | (address << 20U);
                instruction = 0x17U | (register_id << 7U) | ((address + 0x800U) & 0xFFFFF000U);
            }
            xx_data_set_u32(data + offset, 4, 0, instruction, false);
            xx_data_set_u32(data + offset + 4U, 4, 0, second, false);
            offset += 6U;
        }
    }
    return true;
}

#endif /* XX_XZ_RISCV_NATIVE_H */
