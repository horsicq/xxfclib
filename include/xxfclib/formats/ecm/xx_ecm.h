/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ECM_H
#define XX_ECM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ecm {
    Abstractformat format;
} xx_ecm;
typedef struct xx_ecm_info {
    uint64_t output_size, records, literal_bytes;
    uint64_t mode1_sectors, mode2_form1_sectors, mode2_form2_sectors;
    uint32_t stored_check;
} xx_ecm_info;
XXFC_API void xx_ecm_init(xx_ecm *, xx_io_device *, int64_t);
XXFC_API xx_ecm *xx_ecm_create(xx_io_device *, int64_t);
XXFC_API void xx_ecm_destroy(xx_ecm *);
XXFC_API void xx_ecm_free(xx_ecm *);
/* Structural validation; extraction also verifies the reconstructed EDC. */
XXFC_API bool xx_ecm_get_info(xx_ecm *, xx_ecm_info *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_ecm_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
