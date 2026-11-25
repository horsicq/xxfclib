/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Plan 9 a.out executable images, including extended 64-bit headers.
 */
#ifndef XX_PLAN9_EXEC_H
#define XX_PLAN9_EXEC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_plan9_exec {
    Abstractformat format;
    uint32_t magic;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t symbol_size;
    uint32_t pcsp_size;
    uint32_t pcline_size;
    uint64_t entry_point;
    uint32_t header_size;
} xx_plan9_exec;
XXFC_API void xx_plan9_exec_init(xx_plan9_exec *, xx_io_device *, int64_t);
XXFC_API xx_plan9_exec *xx_plan9_exec_create(xx_io_device *, int64_t);
XXFC_API void xx_plan9_exec_destroy(xx_plan9_exec *);
XXFC_API void xx_plan9_exec_free(xx_plan9_exec *);
XXFC_API bool xx_plan9_exec_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_plan9_exec_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_plan9_exec_get_memory_map(Abstractformat *, xx_memory_map_mode_t, xx_memory_map *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
