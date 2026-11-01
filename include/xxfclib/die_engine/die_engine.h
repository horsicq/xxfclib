/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* die_engine.h - the Detect It Easy signature execution engine.
 *
 * Shared database, option and result types are declared in scan/xx_scan.h.
 * The engine's own state and script bindings remain private to src/die_engine/.
 */

#ifndef XX_DIE_ENGINE_H
#define XX_DIE_ENGINE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/scan/xx_scan.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- types  */

/** @brief Opaque engine state. Defined in src/die_engine/. */
typedef struct DieEngine DieEngine;

/* Compatibility aliases for DIE engine scan options and results */
typedef xx_scan_options ScanOptions;
typedef xx_scan_result ScanResult;

#define scan_options_init xx_scan_options_init
#define scan_options_free xx_scan_options_free
#define scan_result_free xx_scan_result_free

#define bShowType show_type
#define bShowVersion show_version
#define bShowInfo show_info
#define bUseCustomDatabase use_custom_database
#define bUseExtraDatabase use_extra_database /* deprecated, see xx_scan_options */
#define bSort sort
#define bFormatResult format_result

#define die_engine_format_text xx_scan_format_text
#define die_engine_format_json xx_scan_format_json
#define die_engine_format_xml xx_scan_format_xml
#define die_engine_format_csv xx_scan_format_csv

/** Initialise the signature implementation of every xx_scan_engine callback.
 * The engine borrows database until xx_scan_engine_cleanup(); it never frees it.
 * Results returned by callbacks are owned and released with xx_scan_free_result().
 * Use xx_scan_file/device/memory for bounded scans, parent passes, overlays
 * and PE/ELF resources, plus decoded ZIP/LHA/GZIP/BZIP2 members and Mach-O Fat
 * slices. Recursive and first-wrapper modes are unsupported.
 * Returns false for a NULL engine or database. Initialise a fresh/cleaned engine. */
XXFC_API bool die_engine_init(xx_scan_engine *engine, DBase *database);

/* ---------------------------------------------------------------- scan  */

XXFC_API int die_engine_scan_file(const char *pFileName, DBase *pDb, xx_scan_options *pOptions, xx_scan_result *pResult);

/* Reinterprets a file as one explicit type. XFT_UNKNOWN retains automatic
 * detection, including its COM/binary fallback passes. An explicit type runs
 * exactly that type's pass and reports that type in the result. Invalid types
 * fail with an empty result. */
XXFC_API int die_engine_scan_file_type(const char *pFileName, DBase *pDb, xx_scan_options *pOptions, XFileType fileType, xx_scan_result *pResult);

/* As die_engine_scan_file_type, with the caller's progress object: the engine
 * enters its levels in pProgress and stops early once it is stopped, failing
 * with pProgress->is_stop set. NULL behaves exactly like the call above. To
 * follow or stop the scan from another thread, bind an observer on the
 * scanning thread with xx_pd_set_observer (xxfclib/data/xx_pd.h). */
XXFC_API int die_engine_scan_file_type_pd(const char *pFileName, DBase *pDb, xx_scan_options *pOptions, XFileType fileType, xx_scan_result *pResult,
                                          xx_pd_struct *pProgress);

/* Enumerates detected types without scanning signature databases. The preferred
 * type is first, followed by the other detected types in descending type order.
 * Names can be obtained with xft_to_string(). No callback runs when opening or
 * reading the file fails. The callback receives no borrowed file state. */
typedef void (*die_engine_file_type_fn)(XFileType fileType, void *pUserData);
XXFC_API int die_engine_detect_file_types(const char *pFileName, die_engine_file_type_fn pTypeFn, void *pUserData);

/* Scan a seekable device without taking ownership, respecting options.offset/size.
 * The caller's position is restored before return, even when the scan fails. */
XXFC_API int die_engine_scan_device(xx_io_device *pDevice, DBase *pDb, xx_scan_options *pOptions, xx_scan_result *pResult);

/* Synchronously scan a borrowed memory buffer. Backs DIE_ScanMemory. */
XXFC_API int die_engine_scan_memory(const void *pData, int64_t nSize, DBase *pDb, xx_scan_options *pOptions, xx_scan_result *pResult);

#ifdef __cplusplus
}
#endif

#endif /* XX_DIE_ENGINE_H */
