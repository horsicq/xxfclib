/* SPDX-License-Identifier: MIT */
#ifndef XX_UPX_ENGINE_H
#define XX_UPX_ENGINE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The separately licensed helper returns one reconstructed executable member.
 * The source is borrowed; TEST reconstructs/checks it only in bounded RAM. */
XXFC_API Abstractformat *xx_upx_create(xx_io_device *,int64_t);
XXFC_API void xx_upx_free(Abstractformat *);
/* Cheap carrier/UPX! marker filter; does not claim an accepted packed format. */
XXFC_API bool xx_upx_has_marker_device(xx_io_device *,xx_pd_struct *);
/* The marker candidate is accepted only after the official unpacker validates
 * and reconstructs the complete image. Restores the caller's source cursor. */
XXFC_API xx_file_type_t xx_upx_detect_device(xx_io_device *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
