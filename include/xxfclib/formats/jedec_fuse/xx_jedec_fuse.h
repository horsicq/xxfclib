/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/daveho/Galasm/master/src/jedec.c
 * JEDEC fuse stream: complete STX/ETX framing, fuse count/default/security fields and bounded nonoverlapping bit assignments, checked packed-fuse checksum and
 * transmission checksum when nonzero. Original device notes/typed fields plus decoded packed fuse bits exported; test vectors and vendor extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_JEDEC_FUSE_H
#define XX_JEDEC_FUSE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_jedec_fuse {
    Abstractformat format;
} xx_jedec_fuse;
XXFC_API void xx_jedec_fuse_init(xx_jedec_fuse *, xx_io_device *, int64_t);
XXFC_API xx_jedec_fuse *xx_jedec_fuse_create(xx_io_device *, int64_t);
XXFC_API void xx_jedec_fuse_destroy(xx_jedec_fuse *);
XXFC_API void xx_jedec_fuse_free(xx_jedec_fuse *);
XXFC_API bool xx_jedec_fuse_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_jedec_fuse_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_jedec_fuse_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_jedec_fuse_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_jedec_fuse_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_jedec_fuse_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_jedec_fuse_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
