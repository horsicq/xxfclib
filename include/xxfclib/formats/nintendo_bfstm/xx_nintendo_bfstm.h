/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/kinnay/Nintendo-File-Formats/master/src/libs/nw/bfstm.md
 * FSTM version0x30000, either BOM, bounded INFO/SEEK/DATA/REGN blocks and primary stream-info reference. Exports complete encoded blocks; no audio decoding, seeking,
 * prefetch data or stream playback.
 */
#ifndef XX_NINTENDO_BFSTM_H
#define XX_NINTENDO_BFSTM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bfstm {
    Abstractformat format;
} xx_nintendo_bfstm;
XXFC_API void xx_nintendo_bfstm_init(xx_nintendo_bfstm *, xx_io_device *, int64_t);
XXFC_API xx_nintendo_bfstm *xx_nintendo_bfstm_create(xx_io_device *, int64_t);
XXFC_API void xx_nintendo_bfstm_destroy(xx_nintendo_bfstm *);
XXFC_API void xx_nintendo_bfstm_free(xx_nintendo_bfstm *);
XXFC_API bool xx_nintendo_bfstm_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nintendo_bfstm_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_bfstm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_bfstm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_bfstm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_bfstm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_bfstm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
