/* Copyright (c) 2017-2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Derived from the SpecAbstract NFD engine. The original project is licensed
 * under the MIT License; this public adapter is a C implementation of its
 * engine boundary.
 */

#ifndef XX_NFD_ENGINE_H
#define XX_NFD_ENGINE_H

#include "xxfclib/scan/xx_scan.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFD_ENGINE_ABI_VERSION UINT32_C(1)

/** Versioned configuration. Phase 1 currently has no private resource knobs. */
typedef struct nfd_engine_config {
    uint32_t abi_version;
    size_t struct_size;
} nfd_engine_config;

/** Fill a configuration with ABI-v1 defaults. */
XXFC_API void nfd_engine_config_init(nfd_engine_config *config);

/** Public adapter ABI and generated signature-data identity. */
XXFC_API uint32_t nfd_engine_get_abi_version(void);
XXFC_API const char *nfd_engine_get_data_version(void);

/** Immutable common scan-engine ABI table for Nauz File Detector. */
XXFC_API const xx_scan_engine_ops *nfd_engine_get_ops(void);

/** Initialise one NFD implementation of xx_scan_engine.
 * NFD uses built-in tables and never accepts an external database. config may
 * be NULL for defaults. The engine is released with xx_scan_engine_cleanup(). */
XXFC_API bool nfd_engine_init(xx_scan_engine *engine, const nfd_engine_config *config,
                              xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_NFD_ENGINE_H */
