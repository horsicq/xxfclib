/* SPDX-License-Identifier: MIT */
#ifndef XX_SMART_INSTALL_MAKER_H
#define XX_SMART_INSTALL_MAKER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Smart Install Maker footer/string-table packages. Stored payloads and
 * single-volume/multipart CAB (stored/MSZIP/LZX/Quantum) decode in bounded RAM.
 * Continued files/split blocks preserve codec history; non-footer layouts and
 * unknown encodings fail.
 * Absolute installation paths become symbolic relative $Drive/$SystemDrive
 * paths. Source programs/scripts are never executed. TEST writes no files. */
typedef struct xx_smart_install_maker { Abstractformat format; const xx_list_s *parse_options; } xx_smart_install_maker;
XXFC_API xx_smart_install_maker *xx_smart_install_maker_create(xx_io_device *,int64_t);
XXFC_API void xx_smart_install_maker_free(xx_smart_install_maker *);
/* Cheap EOF-layout filter; preserves the borrowed source cursor. A candidate
 * still requires check_is_valid before selecting this reader. */
XXFC_API bool xx_smart_install_maker_has_candidate_device(xx_io_device *,int64_t);
#ifdef __cplusplus
}
#endif
#endif
