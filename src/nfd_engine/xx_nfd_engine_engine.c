/* Copyright (c) 2017-2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Derived from SpecAbstract/specabstract.cpp and specabstract.h.
 * Source snapshot: F:/ownCloud/prepare/qt5/_mylibs/SpecAbstract (2026-11-05).
 */

#include "xxfclib/nfd_engine/nfd_engine.h"
#include "xxfclib/nfd_engine/nfd_tables.h"
#include "xxfclib/rt/xx_rt.h"
#include "xx_nfd_engine_internal.h"

#if !defined(XXFC_PROFILE_NFD_ENGINE) || !XXFC_PROFILE_NFD_ENGINE
#error "The NFD sources must be built through the focused nfd_engine target"
#endif

#if !defined(XXFC_STREAM_HAS_SCHEMA)
#error "The focused NFD target must declare its schema closure"
#elif XXFC_STREAM_HAS_SCHEMA
#error "The NFD engine must remain independent of the generic schema catalog"
#endif

#if defined(XXFC_NFD_ENGINE_CRT_FREE) && defined(_MSC_VER) && defined(_DLL)
#error "The CRT-free NFD profile must not use the dynamic MSVC runtime"
#endif

static bool nfd_engine_create(xx_scan_engine *engine, const void *config, xx_pd_struct *pd)
{
    const nfd_engine_config *nfd_config = (const nfd_engine_config *)config;
    if (!engine) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing NFD engine");
        return false;
    }
    if (nfd_config &&
        (nfd_config->abi_version != NFD_ENGINE_ABI_VERSION ||
         nfd_config->struct_size < sizeof(*nfd_config))) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Incompatible NFD engine configuration");
        return false;
    }
    engine->context = NULL;
    return true;
}

static void nfd_engine_destroy(xx_scan_engine *engine)
{
    if (engine) engine->context = NULL;
}

static bool nfd_engine_supports_type(const xx_scan_engine *engine, xx_file_type_t file_type)
{
    (void)engine;
    return file_type == XX_FILE_TYPE_BINARY;
}

static xx_scan_result *nfd_engine_scan_region(xx_scan_engine *engine,
                                              const xx_scan_region *region,
                                              const xx_scan_options *options,
                                              xx_pd_struct *pd)
{
    xx_scan_options local;
    xx_nfd_binary_state state;
    xx_scan_result *result;
    if (!engine || !region || !region->device) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing NFD engine or input region");
        return NULL;
    }
    if (xx_pd_is_stopped(pd)) return NULL;
    if (options) local = *options;
    else xx_scan_options_init(&local);
    if (local.recursive_scan || local.first_wrapper_only || local.all_types_scan ||
        local.overlay_scan || local.resources_scan || local.archives_scan) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "NFD region received traversal options");
        return NULL;
    }
    if (!nfd_engine_supports_type(engine, region->file_type)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Unsupported NFD file type");
        return NULL;
    }
    result = (xx_scan_result *)xx_rt_calloc(1, sizeof(*result));
    if (!result) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate NFD scan result");
        return NULL;
    }
    result->engine_id = XX_SCAN_ENGINE_NFD;
    result->file_type = region->file_type;
    result->nFileSize = region->size;
    result->sort = local.sort;
    xx_rt_memset(&state, 0, sizeof(state));
    state.device = region->device;
    state.region = region;
    state.options = &local;
    if (!xx_nfd_binary_scan(&state, result, pd) || xx_pd_is_stopped(pd) ||
        (pd && pd->last_error)) {
        xx_scan_result_free(result);
        xx_rt_free(result);
        if (!xx_pd_is_stopped(pd) && (!pd || !pd->last_error)) {
            xx_pd_set_error(pd, XXFC_ERR_IO, "NFD binary scan failed");
        }
        return NULL;
    }
    return result;
}

static const xx_scan_engine_ops nfd_ops = {
    XX_SCAN_ENGINE_ABI_VERSION,
    sizeof(xx_scan_engine_ops),
    XX_SCAN_ENGINE_NFD,
    "nfd",
    "Nauz File Detector",
    XX_SCAN_CAP_BUILTIN_SIGNATURES | XX_SCAN_CAP_DEEP |
        XX_SCAN_CAP_HEURISTIC | XX_SCAN_CAP_AGGRESSIVE | XX_SCAN_CAP_VERBOSE |
        XX_SCAN_CAP_TYPE_QUERY | XX_SCAN_CAP_THREAD_SAFE | XX_SCAN_CAP_REENTRANT,
    nfd_engine_create,
    nfd_engine_destroy,
    NULL,
    nfd_engine_supports_type,
    nfd_engine_scan_region,
    NULL,
    NULL
};

void nfd_engine_config_init(nfd_engine_config *config)
{
    if (!config) return;
    config->abi_version = NFD_ENGINE_ABI_VERSION;
    config->struct_size = sizeof(*config);
}

uint32_t nfd_engine_get_abi_version(void)
{
    return NFD_ENGINE_ABI_VERSION;
}

const char *nfd_engine_get_data_version(void)
{
    return xx_nfd_tables_data_version();
}

const xx_scan_engine_ops *nfd_engine_get_ops(void)
{
    return &nfd_ops;
}

bool nfd_engine_init(xx_scan_engine *engine, const nfd_engine_config *config,
                     xx_pd_struct *pd)
{
    return xx_scan_engine_init(engine, &nfd_ops, config, NULL, pd);
}
