/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.gnu.org/software/grub/manual/grub-dev/html_node/File-Structure.html
 * GNU GRUB PFF2: complete typed sections, sorted unique Unicode index with resolved nonoverlapping offsets and complete uncompressed glyph dimensions/bitmap extents. Original descriptor/index and glyph records exported; compressed glyph flags declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_GRUB_PFF2_H
#define XX_GRUB_PFF2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_grub_pff2 {Abstractformat format;} xx_grub_pff2;
XXFC_API void xx_grub_pff2_init(xx_grub_pff2 *,xx_io_device *,int64_t);
XXFC_API xx_grub_pff2 *xx_grub_pff2_create(xx_io_device *,int64_t);
XXFC_API void xx_grub_pff2_destroy(xx_grub_pff2 *);
XXFC_API void xx_grub_pff2_free(xx_grub_pff2 *);
XXFC_API bool xx_grub_pff2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_grub_pff2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_grub_pff2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_grub_pff2_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_grub_pff2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_grub_pff2_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_grub_pff2_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
