/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_ATARI_ST_MSA_H
#define XX_ATARI_ST_MSA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_atari_st_msa { Abstractformat format; } xx_atari_st_msa;
XXFC_API void xx_atari_st_msa_init(xx_atari_st_msa *,xx_io_device *,int64_t);
XXFC_API xx_atari_st_msa *xx_atari_st_msa_create(xx_io_device *,int64_t);
XXFC_API void xx_atari_st_msa_destroy(xx_atari_st_msa *);
XXFC_API void xx_atari_st_msa_free(xx_atari_st_msa *);
XXFC_API bool xx_atari_st_msa_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_atari_st_msa_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
