/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.kodak.com/content/products-brochures/Film/Cineon-File-Format-Description.pdf
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_CINEON_H
#define XX_CINEON_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_cineon { Abstractformat format; } xx_cineon;
XXFC_API void xx_cineon_init(xx_cineon *,xx_io_device *,int64_t);
XXFC_API xx_cineon *xx_cineon_create(xx_io_device *,int64_t);
XXFC_API void xx_cineon_destroy(xx_cineon *);
XXFC_API void xx_cineon_free(xx_cineon *);
XXFC_API bool xx_cineon_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cineon_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
