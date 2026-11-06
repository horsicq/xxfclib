/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_jar.c - JAR script API dispatch. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_jar_api_table[] = {
    {"getManifestRecord", A_getManifestRecord, 1},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_jar(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_jar_api_table);
}
bool die_engine_api_dispatch_jar(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_getManifestRecord: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = native_str_take(pCtx, pEngine->bHasZip ? xx_zip_manifest_record(&pEngine->zip, pKey) : xx_str_create(""));

            cd_free(pKey);

            *pResult = result;
            return true;
        }

        default:
            break;
    }

    return false;
}
