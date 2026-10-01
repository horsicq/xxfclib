/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/nasa/NASTRAN-95/master/um/BULK.TXT
 * NASTRAN bounded mesh bulk-data subset: small/free-field GRID and standard shell/solid connectivity with unique IDs, finite coordinates, zero coordinate systems and resolved local node references. Original mesh cards exported; solver/external includes/large-field/property/constraint/load extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_NASTRAN_BULK_H
#define XX_NASTRAN_BULK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nastran_bulk {Abstractformat format;} xx_nastran_bulk;
XXFC_API void xx_nastran_bulk_init(xx_nastran_bulk *,xx_io_device *,int64_t);
XXFC_API xx_nastran_bulk *xx_nastran_bulk_create(xx_io_device *,int64_t);
XXFC_API void xx_nastran_bulk_destroy(xx_nastran_bulk *);
XXFC_API void xx_nastran_bulk_free(xx_nastran_bulk *);
XXFC_API bool xx_nastran_bulk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nastran_bulk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
