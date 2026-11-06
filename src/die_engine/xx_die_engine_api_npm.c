/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_npm.c - NPM script API dispatch. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_npm_api_table[] = {
    {"getPackageJsonRecord", A_getPackageJsonRecord, 1},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_npm(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_npm_api_table);
}
bool die_engine_api_dispatch_npm(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_getPackageJsonRecord: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = native_str_take(pCtx, pEngine->bHasZip ? xx_zip_packagejson_record(&pEngine->zip, pKey) : xx_str_create(""));

            cd_free(pKey);

            *pResult = result;
            return true;
        }

        default:
            break;
    }

    return false;
}
