/* SPDX-License-Identifier: MIT
 * Independently implemented from https://arrow.apache.org/docs/format/Columnar.html
 * Bounded encoded-component extraction. */
#ifndef XX_APACHE_ARROW_FILE_H
#define XX_APACHE_ARROW_FILE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_apache_arrow_file { Abstractformat format; } xx_apache_arrow_file;
XXFC_API void xx_apache_arrow_file_init(xx_apache_arrow_file *,xx_io_device *,int64_t);
XXFC_API xx_apache_arrow_file *xx_apache_arrow_file_create(xx_io_device *,int64_t);
XXFC_API void xx_apache_arrow_file_destroy(xx_apache_arrow_file *);
XXFC_API void xx_apache_arrow_file_free(xx_apache_arrow_file *);
XXFC_API bool xx_apache_arrow_file_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apache_arrow_file_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
