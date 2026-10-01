/* SPDX-License-Identifier: MIT
 * Wire specification: https://biopython.org/docs/latest/api/Bio.SeqIO.InsdcIO.html */
#ifndef XX_GENOMICS_GENBANK_H
#define XX_GENOMICS_GENBANK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_genomics_genbank { Abstractformat format; } xx_genomics_genbank;
XXFC_API void xx_genomics_genbank_init(xx_genomics_genbank *,xx_io_device *,int64_t);
XXFC_API xx_genomics_genbank *xx_genomics_genbank_create(xx_io_device *,int64_t);
XXFC_API void xx_genomics_genbank_destroy(xx_genomics_genbank *);
XXFC_API void xx_genomics_genbank_free(xx_genomics_genbank *);
XXFC_API bool xx_genomics_genbank_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_genomics_genbank_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
