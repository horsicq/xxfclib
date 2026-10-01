/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/KLayout/klayout/master/src/plugins/streamers/gds2/db_plugin/dbGDS2Reader.cc
 * Calma GDSII Stream: complete typed library/structure/element framing, bounded coordinates and names, finite positive units, geometry and local references. Original layout records exported; no layout execution. OASIS is a distinct unsupported grammar.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_CALMA_GDSII_H
#define XX_CALMA_GDSII_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_calma_gdsii {Abstractformat format;} xx_calma_gdsii;
XXFC_API void xx_calma_gdsii_init(xx_calma_gdsii *,xx_io_device *,int64_t);
XXFC_API xx_calma_gdsii *xx_calma_gdsii_create(xx_io_device *,int64_t);
XXFC_API void xx_calma_gdsii_destroy(xx_calma_gdsii *);
XXFC_API void xx_calma_gdsii_free(xx_calma_gdsii *);
XXFC_API bool xx_calma_gdsii_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_calma_gdsii_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
