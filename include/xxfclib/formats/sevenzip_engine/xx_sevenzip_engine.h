/* SPDX-License-Identifier: MIT. Generic archive/filesystem 7-Zip adapter. */
#ifndef XX_SEVENZIP_ENGINE_H
#define XX_SEVENZIP_ENGINE_H
#include "xxfclib/formats/sevenzip_backend/xx_sevenzip_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* handler is an exact upstream handler name, or NULL for automatic selection.
 * The returned format borrows device; free does not close the source. Decoding
 * runs in the separately licensed bundled helper. A NULL unpack path performs
 * a full integrity test in RAM, including encrypted and solid member data. */
XXFC_API Abstractformat *xx_sevenzip_engine_create(xx_io_device *device,
    int64_t base_address, const char *handler);
/* Fixed adjacent helpers that implement the same bounded borrowed-IO protocol.
 * filename must be a basename. It is resolved beside the host executable. */
XXFC_API Abstractformat *xx_sevenzip_engine_create_helper(xx_io_device *device,
    int64_t base_address,const char *handler,const char *filename,
    xx_file_type_t type,const char *extension);
XXFC_API void xx_sevenzip_engine_free(Abstractformat *format);
XXFC_API xx_sevenzip_backend_status xx_sevenzip_engine_get_status(
    const Abstractformat *format);
XXFC_API const char *xx_sevenzip_engine_get_handler(const Abstractformat *format);
/* Optional path of the borrowed source, used only to resolve read-only sibling
 * parts of a split archive or disk image. The adapter owns a copy. */
XXFC_API bool xx_sevenzip_engine_set_source_path(Abstractformat *format,
    const char *source_path);
/* Default false permits embedded archive scanning. True restricts every LIST
 * and READ to an archive beginning at base_address; changes discard cached
 * validity/base information so the next operation validates this policy. */
XXFC_API bool xx_sevenzip_engine_set_start_only(Abstractformat *format,
    bool start_only);
#ifdef __cplusplus
}
#endif
#endif
