/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_dex.c - DEX script API dispatch. */

#include "xx_die_engine_api_internal.h"

static const ApiEntry g_dex_api_table[] = {
    {"isDexStringPresent", A_isDexStringPresent, 1},
    {"isDexItemStringPresent", A_isDexItemStringPresent, 1},
    {"getMapItemsHash", A_getMapItemsHash, 0},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_dex(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_dex_api_table);
}
bool die_engine_api_dispatch_dex(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_isDexStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasDex ? xx_dex_string_present(&pEngine->dex, pString) : 0;

            cd_free(pString);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isDexItemStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasDex ? xx_dex_item_string_present(&pEngine->dex, pString) : 0;

            cd_free(pString);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getMapItemsHash:
            *pResult = js_num((double)(pEngine->bHasDex ? xx_dex_get_map_hash(&pEngine->dex) : 0));
            return true;

        default:
            break;
    }

    return false;
}
