/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_bios_firmware.h @brief Raw x86 real-mode BIOS image reader. */
#ifndef XXFCLIB_FORMAT_BIOS_FIRMWARE_H
#define XXFCLIB_FORMAT_BIOS_FIRMWARE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_BIOS_FIRMWARE_BLOCK_SIZE UINT32_C(0x10000)
#define XX_BIOS_FIRMWARE_RESET_VECTOR_ADDRESS UINT32_C(0x000ffff0)
#define XX_BIOS_FIRMWARE_DECLARED_ENTRY_OFFSET INT64_C(0)
#define XX_BIOS_FIRMWARE_FAR_JUMP UINT8_C(0xea)
#define XX_BIOS_FIRMWARE_NEAR_JUMP UINT8_C(0xe9)

typedef struct xx_bios_firmware {
    Abstractformat format;
    int64_t reset_vector_offset;
    uint8_t reset_opcode;
    uint16_t reset_target_offset;
    uint16_t reset_target_segment;
} xx_bios_firmware;

typedef xx_bios_firmware xx_bios_firmware_t;

XXFC_API void xx_bios_firmware_init(xx_bios_firmware *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_bios_firmware *xx_bios_firmware_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bios_firmware_destroy(xx_bios_firmware *reader);
XXFC_API void xx_bios_firmware_free(xx_bios_firmware *reader);

XXFC_API bool xx_bios_firmware_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_bios_firmware_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
XXFC_API int64_t xx_bios_firmware_get_format_size(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_bios_firmware_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

/** Radare2's BIOS loader declares paddr 0 for its program entry. */
XXFC_API int64_t xx_bios_firmware_get_declared_entry_offset(const xx_bios_firmware *reader);
/** Actual file offset whose bytes map to real-mode address 0xffff0. */
XXFC_API int64_t xx_bios_firmware_get_reset_vector_offset(const xx_bios_firmware *reader);
XXFC_API uint64_t xx_bios_firmware_get_entry_point_address(const xx_bios_firmware *reader);
XXFC_API uint8_t xx_bios_firmware_get_reset_opcode(const xx_bios_firmware *reader);
/** Decode the immediate target fields of an EA far jump without disassembling. */
XXFC_API bool xx_bios_firmware_get_far_jump_target(const xx_bios_firmware *reader, uint16_t *segment, uint16_t *offset);

static inline Abstractformat *xx_bios_firmware_to_format(xx_bios_firmware *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BIOS_FIRMWARE_H */
