/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/biopython/biopython/blob/master/Bio/Phylo/NexusIO.py */
#ifndef XX_PHYLO_NEXUS_H
#define XX_PHYLO_NEXUS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_phylo_nexus { Abstractformat format; } xx_phylo_nexus;
XXFC_API void xx_phylo_nexus_init(xx_phylo_nexus *,xx_io_device *,int64_t);
XXFC_API xx_phylo_nexus *xx_phylo_nexus_create(xx_io_device *,int64_t);
XXFC_API void xx_phylo_nexus_destroy(xx_phylo_nexus *);
XXFC_API void xx_phylo_nexus_free(xx_phylo_nexus *);
XXFC_API bool xx_phylo_nexus_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_phylo_nexus_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
