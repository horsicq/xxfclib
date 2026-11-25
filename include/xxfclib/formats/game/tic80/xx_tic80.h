/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * TIC-80 chunked cartridge images.
 */
#ifndef XX_TIC80_H
#define XX_TIC80_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XX_TIC80_MAX_CHUNKS 4096U
typedef struct xx_tic80_chunk {
    uint8_t bank;
    uint8_t type;
    uint16_t size;
    uint32_t offset;
    uint32_t address;
    char name[32];
} xx_tic80_chunk;
typedef struct xx_tic80 {
    Abstractformat format;
    xx_tic80_chunk *chunks;
    uint32_t chunk_count;
} xx_tic80;
XXFC_API void xx_tic80_init(xx_tic80 *, xx_io_device *, int64_t);
XXFC_API xx_tic80 *xx_tic80_create(xx_io_device *, int64_t);
XXFC_API void xx_tic80_destroy(xx_tic80 *);
XXFC_API void xx_tic80_free(xx_tic80 *);
XXFC_API bool xx_tic80_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_tic80_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_tic80_get_memory_map(Abstractformat *, xx_memory_map_mode_t, xx_memory_map *, xx_pd_struct *);
XXFC_API uint32_t xx_tic80_get_chunk_count(const xx_tic80 *);
XXFC_API const xx_tic80_chunk *xx_tic80_get_chunk(const xx_tic80 *, uint32_t);
#ifdef __cplusplus
}
#endif
#endif
