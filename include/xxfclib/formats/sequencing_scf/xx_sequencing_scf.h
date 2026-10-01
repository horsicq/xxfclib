/* SPDX-License-Identifier: MIT
 * Wire specification: https://staden.sourceforge.net/manual/formats_unix_3.html */
#ifndef XX_SEQUENCING_SCF_H
#define XX_SEQUENCING_SCF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sequencing_scf { Abstractformat format; } xx_sequencing_scf;
XXFC_API void xx_sequencing_scf_init(xx_sequencing_scf *,xx_io_device *,int64_t);
XXFC_API xx_sequencing_scf *xx_sequencing_scf_create(xx_io_device *,int64_t);
XXFC_API void xx_sequencing_scf_destroy(xx_sequencing_scf *);
XXFC_API void xx_sequencing_scf_free(xx_sequencing_scf *);
XXFC_API bool xx_sequencing_scf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sequencing_scf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
