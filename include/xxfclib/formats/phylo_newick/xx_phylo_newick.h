/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/biopython/biopython/blob/master/Bio/Phylo/NewickIO.py */
#ifndef XX_PHYLO_NEWICK_H
#define XX_PHYLO_NEWICK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_phylo_newick {
    Abstractformat format;
} xx_phylo_newick;
XXFC_API void xx_phylo_newick_init(xx_phylo_newick *, xx_io_device *, int64_t);
XXFC_API xx_phylo_newick *xx_phylo_newick_create(xx_io_device *, int64_t);
XXFC_API void xx_phylo_newick_destroy(xx_phylo_newick *);
XXFC_API void xx_phylo_newick_free(xx_phylo_newick *);
XXFC_API bool xx_phylo_newick_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_phylo_newick_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_phylo_newick_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_phylo_newick_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_phylo_newick_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_phylo_newick_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_phylo_newick_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
